using Xui.Experimental.Portable;

internal static class Program
{
    private static int assertions;
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(5);

    private static async Task Main()
    {
        ArgumentsAndOwnership();
        await Bounds();
        await RequestLifetime();
        await ConcurrentReads();
        await FixtureFile();
        Console.WriteLine($"File selection: {assertions} assertions passed. No native pickers or user files were accessed.");
    }

    private static void Check(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
        assertions++;
    }

    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private static async Task<T> ThrowsAsync<T>(Func<Task> action) where T : Exception
    {
        try { await action().WaitAsync(Timeout); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private static void ArgumentsAndOwnership()
    {
        Throws<ArgumentOutOfRangeException>(() => new FileSelectionOptions(0));
        Throws<ArgumentOutOfRangeException>(() => new FileSelectionOptions(-1));
        Check(new FileSelectionOptions(long.MaxValue).MaximumBytes == long.MaxValue, "A budget is not an allocation.");
        var stream = new TrackingStream([1, 2, 3]);
        var options = new FileSelectionOptions(3);
        Throws<ArgumentNullException>(() => new PickedFile(null!, stream, options));
        Throws<ArgumentNullException>(() => new PickedFile("a", null!, options));
        Throws<ArgumentNullException>(() => new PickedFile("a", stream, null!));
        Throws<ArgumentOutOfRangeException>(() => new PickedFile("a", stream, options, -1));
        Check(Throws<FileSelectionTooLargeException>(() => new PickedFile("a", stream, options, 4)).MaximumBytes == 3,
            "Known oversize is rejected before allocation or read.");
        Check(stream.ReadCalls == 0 && stream.DisposeCalls == 0, "Failed construction leaves input ownership with its caller.");
        using var file = new PickedFile("../untrusted/name.txt", stream, options, 3);
        Check(file.DisplayName == "../untrusted/name.txt" && file.Length == 3 && file.MaximumBytes == 3,
            "Display names are opaque metadata, never resolved paths.");
        Check(!file.Content.CanSeek && !file.Content.CanWrite && file.Content.CanRead, "Read-only forward stream.");
        Throws<NotSupportedException>(() => file.Content.Seek(0, SeekOrigin.Begin));
        Throws<NotSupportedException>(() => file.Content.SetLength(0));
        Throws<NotSupportedException>(() => file.Content.Write([1], 0, 1));
        Throws<NotSupportedException>(() => _ = file.Content.Length);
        Throws<NotSupportedException>(() => file.Content.Position = 0);
        Throws<ArgumentNullException>(() => _ = file.Content.Read(null!, 0, 0));
        Throws<ArgumentOutOfRangeException>(() => _ = file.Content.Read(new byte[1], 0, 2));
        file.Content.Dispose();
        file.Dispose();
        Check(stream.DisposeCalls == 1 && !file.Content.CanRead, "Caller can dispose stream or file, exactly once.");
        Throws<ObjectDisposedException>(() => file.Content.ReadByte());
        Throws<ObjectDisposedException>(() => file.Content.Flush());
    }

    private static async Task Bounds()
    {
        foreach (bool asynchronous in new[] { false, true })
        foreach (int length in new[] { 0, 2, 3, 4, 17 })
        foreach (int chunk in new[] { 1, 2, 8 })
        {
            var source = new TrackingStream(Enumerable.Range(0, length).Select(n => (byte)n).ToArray()) { Chunk = chunk };
            await using var file = new PickedFile("fixture", source, new FileSelectionOptions(3), length: null);
            var result = new List<byte>();
            var buffer = new byte[8];
            async Task Drain()
            {
                while (true)
                {
                    int count = asynchronous ? await file.Content.ReadAsync(buffer.AsMemory()) : file.Content.Read(buffer);
                    if (count == 0) break;
                    result.AddRange(buffer.Take(count));
                }
            }
            if (length > 3)
            {
                await ThrowsAsync<FileSelectionTooLargeException>(Drain);
                Check(source.TotalRead == 4, "Oversize detection consumes only the budget plus one probe byte.");
                Throws<FileSelectionTooLargeException>(() => file.Content.ReadByte());
            }
            else
            {
                await Drain();
                Check(result.SequenceEqual(Enumerable.Range(0, length).Select(n => (byte)n)), "Exact bytes through short reads.");
                Check(file.Content.ReadByte() == -1, "EOF is reported only after genuine EOF.");
            }
            Check(source.LargestRequest <= 3, "No unbounded native read request.");
            Check(source.ReadCalls == 0 || source.TotalRead <= 4, "Read count is bounded.");
        }
        var exactly = new TrackingStream([1, 2, 3]);
        await using (var file = new PickedFile("exact", exactly, new(3), 3))
        {
            Check(file.Content.Read(Array.Empty<byte>()) == 0 && exactly.ReadCalls == 0, "Empty read does not consume or probe.");
            Check(await file.Content.ReadAsync(new byte[3], 0, 3) == 3, "Legacy async overload honors exact budget.");
            Check(exactly.ReadCalls == 2 && exactly.TotalRead == 3, "Exact budget proves EOF with a one-byte probe.");
        }
        var stale = new TrackingStream([1, 2, 3, 4]);
        using (var file = new PickedFile("stale", stale, new(3), 1))
            Throws<FileSelectionTooLargeException>(() => _ = file.Content.Read(new byte[3]));
        var failing = new TrackingStream([1]) { ReadError = new IOException("provider failed") };
        using (var file = new PickedFile("failure", failing, new(3)))
            Check(Throws<IOException>(() => file.Content.ReadByte()).Message == "provider failed", "Provider errors are not EOF.");
        var canceled = new CancellationToken(canceled: true);
        using (var file = new PickedFile("canceled", new TrackingStream([1]), new(3)))
            await ThrowsAsync<OperationCanceledException>(() => file.Content.ReadAsync(new byte[1], canceled).AsTask());
    }

    private static async Task RequestLifetime()
    {
        var errors = new List<Exception>();
        Throws<ArgumentNullException>(() => new FileSelectionRequest(default, null!));
        using (var cancellation = new CancellationTokenSource())
        using (var request = new FileSelectionRequest(cancellation.Token, errors.Add))
        {
            cancellation.Cancel();
            var error = await ThrowsAsync<OperationCanceledException>(() => request.Task);
            Check(error.CancellationToken == cancellation.Token, "External token identity is preserved.");
            var source = new TrackingStream([1]);
            Check(!request.TryComplete(OperationResult<PickedFile>.Completed(new("late", source, new(3)))),
                "A canceled request cannot deliver a late file.");
            Check(source.DisposeCalls == 1 && request.Task.IsCanceled, "Late owned stream is released.");
        }
        using (var request = new FileSelectionRequest(new CancellationToken(true), errors.Add))
            await ThrowsAsync<OperationCanceledException>(() => request.Task);
        foreach (var result in new[]
        {
            OperationResult<PickedFile>.Cancelled(), OperationResult<PickedFile>.Denied(),
            OperationResult<PickedFile>.Unsupported(), OperationResult<PickedFile>.Failed(new IOException("picker failed"))
        })
        {
            using var request = new FileSelectionRequest(default, errors.Add);
            Check(request.TryComplete(result), "A native outcome completes once.");
            Check(ReferenceEquals(await request.Task, result) && !request.Task.IsCanceled, "Native cancellation is an operation result.");
            Throws<InvalidOperationException>(() => _ = result.Value);
            Check(!request.TryComplete(result), "Repeated result is not delivered twice.");
        }
        using (var cancellation = new CancellationTokenSource())
        using (var request = new FileSelectionRequest(cancellation.Token, errors.Add))
        {
            var source = new TrackingStream([1]);
            using var file = new PickedFile("owned", source, new(3));
            var result = OperationResult<PickedFile>.Completed(file);
            Check(request.TryComplete(result), "Success transfers ownership.");
            cancellation.Cancel();
            request.Dispose();
            Check(ReferenceEquals((await request.Task).Value, file) && source.DisposeCalls == 0,
                "Later cancellation/request disposal cannot revoke caller ownership.");
            Check(!request.TryComplete(result) && source.DisposeCalls == 0, "Duplicate delivery cannot dispose the winning file.");
            var duplicate = new TrackingStream([2]);
            Check(!request.TryComplete(OperationResult<PickedFile>.Completed(new("duplicate", duplicate, new(3)))) &&
                duplicate.DisposeCalls == 1, "A different duplicate file is cleaned up.");
        }
        using (var request = new FileSelectionRequest(default, errors.Add))
        {
            request.Dispose();
            await ThrowsAsync<OperationCanceledException>(() => request.Task);
            request.TryComplete(OperationResult<PickedFile>.Completed(new("cleanup", new TrackingStream([1])
                { DisposeError = new IOException("cleanup failed") }, new(3))));
            Check(errors.Count == 1 && errors[0].Message == "cleanup failed", "Late cleanup errors are reported explicitly.");
            request.TryComplete(OperationResult<PickedFile>.Failed(new IOException("late picker error")));
            Check(errors.Count == 2 && errors[1].Message == "late picker error", "Late picker errors are reported explicitly.");
        }
        for (int i = 0; i < 50; i++)
        {
            using var cancellation = new CancellationTokenSource();
            using var request = new FileSelectionRequest(cancellation.Token, errors.Add);
            var source = new TrackingStream([1]);
            var file = new PickedFile("race", source, new(3));
            await Task.WhenAll(Task.Run(cancellation.Cancel),
                Task.Run(() => request.TryComplete(OperationResult<PickedFile>.Completed(file))));
            if (request.Task.IsCompletedSuccessfully)
            {
                (await request.Task).Value.Dispose();
                Check(request.Task.Result.Status == OperationStatus.Completed, "Completion won the cancellation race.");
            }
            else await ThrowsAsync<OperationCanceledException>(() => request.Task);
            Check(source.DisposeCalls == 1, "Cancellation/completion race releases exactly one owner.");
        }
    }

    private static async Task ConcurrentReads()
    {
        var source = new PausedStream();
        await using var file = new PickedFile("paused", source, new(3));
        Task<int> read = file.Content.ReadAsync(new byte[1]).AsTask();
        Throws<InvalidOperationException>(() => file.Content.ReadByte());
        await ThrowsAsync<InvalidOperationException>(() => file.Content.ReadAsync(new byte[1]).AsTask());
        source.Release.SetResult();
        Check(await read.WaitAsync(Timeout) == 0, "Original read completes after rejected overlapping reads.");
        await file.DisposeAsync();
        Check(source.Disposed, "Async disposal reaches the owned stream.");
    }

    private static async Task FixtureFile()
    {
        string path = Path.Combine(Path.GetTempPath(), $"xui-picked-{Guid.NewGuid():N}.fixture");
        try
        {
            await File.WriteAllBytesAsync(path, [7, 8, 9]);
            await using (var file = new PickedFile("fixture.txt",
                new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.None, 4096, FileOptions.Asynchronous), new(3), 3))
            {
                using var destination = new MemoryStream();
                await file.Content.CopyToAsync(destination);
                Check(destination.ToArray().SequenceEqual(new byte[] { 7, 8, 9 }), "Owned fixture file reads exact bytes.");
            }
            using var reopened = File.Open(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
            Check(reopened.Length == 3, "Disposal closes the actual owned OS handle.");
        }
        finally { File.Delete(path); }
    }

    private sealed class TrackingStream(byte[] bytes) : MemoryStream(bytes)
    {
        public int DisposeCalls { get; private set; }
        public int ReadCalls { get; private set; }
        public int TotalRead { get; private set; }
        public int LargestRequest { get; private set; }
        public int Chunk { get; init; } = int.MaxValue;
        public Exception? ReadError { get; init; }
        public Exception? DisposeError { get; init; }
        public override int Read(Span<byte> buffer)
        {
            ReadCalls++;
            LargestRequest = Math.Max(LargestRequest, buffer.Length);
            if (ReadError is not null) throw ReadError;
            var temporary = new byte[Math.Min(buffer.Length, Chunk)];
            int count = base.Read(temporary, 0, temporary.Length);
            temporary.AsSpan(0, count).CopyTo(buffer);
            TotalRead += count;
            return count;
        }
        public override int Read(byte[] buffer, int offset, int count) => Read(buffer.AsSpan(offset, count));
        public override ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(Read(buffer.Span));
        }
        protected override void Dispose(bool disposing)
        {
            if (disposing) DisposeCalls++;
            base.Dispose(disposing);
            if (DisposeError is not null) throw DisposeError;
        }
    }

    private sealed class PausedStream : Stream
    {
        public TaskCompletionSource Release { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public bool Disposed { get; private set; }
        public override bool CanRead => true;
        public override bool CanSeek => false;
        public override bool CanWrite => false;
        public override long Length => throw new NotSupportedException();
        public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
        public override void Flush() => throw new NotSupportedException();
        public override int Read(byte[] buffer, int offset, int count) => throw new NotSupportedException();
        public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default)
        {
            await Release.Task.WaitAsync(cancellationToken);
            return 0;
        }
        public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
        public override void SetLength(long value) => throw new NotSupportedException();
        public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
        public override ValueTask DisposeAsync() { Disposed = true; return ValueTask.CompletedTask; }
    }
}
