using System.Buffers.Binary;
using System.Reflection;
using Xui.Experimental.Portable;

internal static class Program
{
    private static int assertions;
    private static async Task Main()
    {
        Headers();
        PixelReservations();
        await SourcePixelReservations();
        RequestOwnership();
        await CacheOwnership();
        await CacheCancellation();
        Console.WriteLine($"Image resource helpers: {assertions} assertions passed. No decoder, native image, or rendering claim.");
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }
    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }
    private static async Task<T> Fails<T>(Func<Task> action) where T : Exception
    {
        try { await action().WaitAsync(TimeSpan.FromSeconds(5)); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }
    private static ImageDecodePlan Inspect(byte[] bytes, ImageDecodeOptions? options = null) =>
        ImageDecodePlan.Inspect(bytes, options ?? new());

    private static void Headers()
    {
        foreach (int size in new[] { 0, -1, 1025, int.MaxValue })
        {
            Throws<ArgumentOutOfRangeException>(() => new ImageDecodeOptions(size, 1));
            Throws<ArgumentOutOfRangeException>(() => new ImageDecodeOptions(1, size));
        }
        Throws<ArgumentOutOfRangeException>(() => new ImageDecodeOptions(maximumEncodedBytes: 0));
        Throws<ArgumentOutOfRangeException>(() => new ImageDecodeOptions(maximumEncodedBytes: ImageDecodeOptions.EncodedByteLimit + 1));
        Throws<ArgumentNullException>(() => ImageDecodePlan.Inspect([1], null!));
        Throws<OperationCanceledException>(() => ImageDecodePlan.Inspect(Fixtures.Png(), new(), new(true)));
        Throws<InvalidDataException>(() => Inspect([]));
        Throws<NotSupportedException>(() => Inspect("GIF89a"u8.ToArray()));
        var png = Fixtures.Png();
        var plan = Inspect(png);
        Check(plan.Format == PackagedImageFormat.Png && plan.ContentType == "image/png" &&
            plan.SourceWidth == 3 && plan.SourceHeight == 2 && plan.OutputWidth == 3 && plan.OutputHeight == 2,
            "PNG signature and IHDR are inspected, without upscaling.");
        plan.ValidateCodecSource(3, 2);
        plan.ValidateDecodedOutput(3, 2);
        plan.ValidateOptions(new());
        Throws<ArgumentNullException>(() => plan.ValidateOptions(null!));
        Throws<InvalidDataException>(() => plan.ValidateOptions(new(1, 1)));
        var undersized = Inspect(Fixtures.Png(640, 480), new(100, 100));
        Throws<InvalidDataException>(() => undersized.ValidateOptions(new(192, 144)));
        Check(undersized.OutputWidth == 100 && undersized.OutputHeight == 75, "A smaller valid output is not accepted for a different decode request.");
        Throws<InvalidDataException>(() => plan.ValidateCodecSource(4, 2));
        Throws<InvalidDataException>(() => plan.ValidateDecodedOutput(3, 3));
        Throws<InvalidDataException>(() => Inspect(png, new(maximumEncodedBytes: png.Length - 1)));
        Check(Inspect(png, new(maximumEncodedBytes: png.Length)).SourcePixels == 6, "Exact encoded-byte limit.");
        Check(Inspect(Fixtures.ValidPng()).SourcePixels == 6, "The original valid 3-by-2 RGBA fixture passes header preflight.");
        foreach (var size in new[] { (400u, 200u, 192, 96), (200u, 400u, 72, 144), (16384u, 1u, 192, 1) })
        {
            var fit = Inspect(Fixtures.Png(size.Item1, size.Item2));
            Check(fit.OutputWidth == size.Item3 && fit.OutputHeight == size.Item4, "Contain decode sizing is exact, bounded, and nonzero.");
        }
        Check(Inspect(Fixtures.Png(4096, 4096)).SourcePixels == ImageDecodeOptions.SourcePixelLimit, "Exact source pixel boundary.");
        foreach (var size in new[] { (0u, 1u), (1u, 0u), (16385u, 1u), (4097u, 4096u), (uint.MaxValue, uint.MaxValue) })
            Throws<InvalidDataException>(() => Inspect(Fixtures.Png(size.Item1, size.Item2)));
        foreach (string animated in new[] { "acTL", "fcTL", "fdAT" })
            Throws<NotSupportedException>(() => Inspect(Fixtures.Png(3, 2, animated)));
        Throws<NotSupportedException>(() => Inspect(Fixtures.Png(3, 2, "ABCD")));
        Check(Inspect(Fixtures.Png(3, 2, "tEXt", "owned fixture"u8.ToArray())).SourceWidth == 3, "Unknown ancillary chunks do not change dimensions.");
        var corrupt = png.ToArray(); corrupt[20] ^= 1;
        Throws<InvalidDataException>(() => Inspect(corrupt));
        var hugeChunk = png.ToArray(); BinaryPrimitives.WriteUInt32BigEndian(hugeChunk.AsSpan(8), uint.MaxValue);
        Throws<InvalidDataException>(() => Inspect(hugeChunk));
        Throws<InvalidDataException>(() => Inspect([.. png, 0]));
        for (int length = 8; length < png.Length; length++)
            Throws<InvalidDataException>(() => Inspect(png[..length]));

        foreach (byte frame in new byte[] { 0xc0, 0xc2 })
        {
            var jpeg = Inspect(Fixtures.Jpeg(640, 480, frame));
            Check(jpeg.Format == PackagedImageFormat.Jpeg && jpeg.OutputWidth == 192 && jpeg.OutputHeight == 144,
                "Baseline/progressive JPEG use the same containment plan.");
        }
        Throws<NotSupportedException>(() => Inspect(Fixtures.Jpeg(frame: 0xc1)));
        Throws<InvalidDataException>(() => Inspect(Fixtures.Jpeg(0, 2)));
        Throws<InvalidDataException>(() => Inspect(Fixtures.Jpeg(4097, 4096)));
        foreach (bool little in new[] { true, false })
        {
            Check(Inspect(Fixtures.Jpeg(exif: Fixtures.Exif(1, little))).SourceWidth == 3, "Identity EXIF orientation accepted in either endian.");
            foreach (ushort orientation in new ushort[] { 0, 2, 3, 4, 5, 6, 7, 8, ushort.MaxValue })
                Throws<NotSupportedException>(() => Inspect(Fixtures.Jpeg(exif: Fixtures.Exif(orientation, little))));
        }
        var exifOffset = Fixtures.Exif(1); BinaryPrimitives.WriteUInt32LittleEndian(exifOffset.AsSpan(4), uint.MaxValue);
        Throws<InvalidDataException>(() => Inspect(Fixtures.Jpeg(exif: exifOffset)));
        var exifCount = Fixtures.Exif(1); BinaryPrimitives.WriteUInt16LittleEndian(exifCount.AsSpan(8), ushort.MaxValue);
        Throws<InvalidDataException>(() => Inspect(Fixtures.Jpeg(exif: exifCount)));
        Throws<InvalidDataException>(() => Inspect(Fixtures.Jpeg(exif: [1, 2, 3])));
        var exifType = Fixtures.Exif(1); exifType[12] = 4;
        Throws<InvalidDataException>(() => Inspect(Fixtures.Jpeg(exif: exifType)));
        var exifValues = Fixtures.Exif(1); exifValues[14] = 2;
        Throws<InvalidDataException>(() => Inspect(Fixtures.Jpeg(exif: exifValues)));
        Throws<NotSupportedException>(() => Inspect(Fixtures.Png(3, 2, "eXIf", Fixtures.Exif(6))));
        var completeJpeg = Fixtures.Jpeg();
        for (int length = 2; length < completeJpeg.Length; length++)
            Throws<InvalidDataException>(() => Inspect(completeJpeg[..length]));
        Throws<InvalidDataException>(() => Inspect([.. completeJpeg, 0]));
        var random = new Random(2718);
        for (int i = 0; i < 200; i++)
        {
            byte[] hostile = (i % 2 == 0 ? png : completeJpeg).ToArray();
            hostile[random.Next(2, hostile.Length)] ^= (byte)random.Next(1, 256);
            try
            {
                var inspected = Inspect(hostile);
                Check(inspected.OutputWidth is >= 1 and <= 1024 && inspected.OutputHeight is >= 1 and <= 1024,
                    "A surviving mutated header still has bounded output.");
            }
            catch (Exception error) when (error is InvalidDataException or NotSupportedException) { assertions++; }
        }
    }

    private static void PixelReservations()
    {
        var plan = Inspect(Fixtures.Png(3, 2));
        var budget = new ImagePixelBudget(48);
        var first = budget.Reserve(plan);
        var second = budget.Reserve(plan);
        Check(budget.ReservedBytes == 48 && first.Bytes == 24, "Reservations cover output bytes before allocation.");
        Throws<ImageResourceLimitException>(() => budget.Reserve(plan));
        first.Dispose(); first.Dispose();
        Check(budget.ReservedBytes == 24, "Reservation release is idempotent.");
        using var third = budget.Reserve(plan);
        second.Dispose();
        Check(budget.ReservedBytes == 24, "Released capacity is reusable without weakening dimensions.");
        Throws<ArgumentOutOfRangeException>(() => new ImagePixelBudget(3));
        Throws<ArgumentOutOfRangeException>(() => new ImagePixelBudget(long.MaxValue));
    }

    private static async Task SourcePixelReservations()
    {
        var maximum = Inspect(Fixtures.Png(4096, 4096), new(1024, 1024));
        const long sourceBytes = 64L * 1024 * 1024;
        var sourceBudget = new ImagePixelBudget(sourceBytes);
        var outputBudget = new ImagePixelBudget();
        Throws<ArgumentNullException>(() => sourceBudget.ReserveSource(null!));
        using var source = sourceBudget.ReserveSource(maximum);
        Check(source.Bytes == sourceBytes && sourceBudget.ReservedBytes == sourceBytes,
            "The exact maximum source uses a 64-MiB reservation independently of its decode box.");
        Throws<ImageResourceLimitException>(() => sourceBudget.ReserveSource(maximum));
        Throws<ImageResourceLimitException>(() => new ImagePixelBudget(sourceBytes - 1).ReserveSource(maximum));
        using var bitmap = outputBudget.Reserve(maximum);
        using var canvas = outputBudget.Reserve(maximum);
        Check(bitmap.Bytes == 4L * 1024 * 1024 && outputBudget.ReservedBytes == 8L * 1024 * 1024 &&
            sourceBudget.ReservedBytes == sourceBytes,
            "Raw pixels, resized bitmap and canvas remain separately charged while concurrently owned.");
        Throws<ImageResourceLimitException>(() => outputBudget.Reserve(maximum));
        bitmap.Dispose();
        Check(outputBudget.ReservedBytes == canvas.Bytes && sourceBudget.ReservedBytes == sourceBytes,
            "Releasing one output copy cannot release the source or another output copy.");
        source.Dispose();
        source.Dispose();
        Check(sourceBudget.ReservedBytes == 0 && outputBudget.ReservedBytes == canvas.Bytes,
            "Source retirement is idempotent and does not alter independent output accounting.");
        canvas.Dispose();

        var contenders = await Task.WhenAll(Enumerable.Range(0, 8).Select(_ => Task.Run(() =>
        {
            try { return sourceBudget.ReserveSource(maximum); }
            catch (ImageResourceLimitException) { return null; }
        })));
        try
        {
            Check(contenders.Count(reservation => reservation is not null) == 1 &&
                sourceBudget.ReservedBytes == sourceBytes,
                "Concurrent source reservations admit exactly one full-budget owner.");
        }
        finally { foreach (var reservation in contenders) reservation?.Dispose(); }
        Check(sourceBudget.ReservedBytes == 0, "Concurrent admission leaves no leaked reservations.");

        using var canceled = new CancellationTokenSource();
        var nativeStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var nativeSettled = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        bool rawClosed = false;
        async Task Decode()
        {
            using var reservation = sourceBudget.ReserveSource(maximum);
            nativeStarted.SetResult();
            try
            {
                await nativeSettled.Task;
                maximum.ValidateCodecSource(4096, 4096);
                canceled.Token.ThrowIfCancellationRequested();
            }
            finally { rawClosed = true; }
        }
        Task decode = Decode();
        await nativeStarted.Task;
        canceled.Cancel();
        Check(!decode.IsCompleted && !rawClosed && sourceBudget.ReservedBytes == sourceBytes,
            "Caller cancellation cannot release a raw allocation still owned by an uncancellable decoder.");
        Throws<ImageResourceLimitException>(() => sourceBudget.ReserveSource(maximum));
        nativeSettled.SetResult();
        await Fails<OperationCanceledException>(() => decode);
        Check(rawClosed && sourceBudget.ReservedBytes == 0,
            "Canceled native completion closes the raw bitmap before releasing its admission reservation.");
        Throws<InvalidDataException>(() =>
        {
            using var reservation = sourceBudget.ReserveSource(maximum);
            maximum.ValidateCodecSource(4095, 4096);
        });
        Check(sourceBudget.ReservedBytes == 0, "Actual codec dimension mismatch releases source admission.");

        var tiny = Inspect(Fixtures.Png(1, 1));
        var exact = new ImagePixelBudget(4);
        using var minimum = exact.ReserveSource(tiny);
        Check(minimum.Bytes == 4 && exact.ReservedBytes == 4, "Minimum source admission uses exactly one pixel.");
    }

    private static void RequestOwnership()
    {
        var errors = new List<Exception>();
        var dispatcher = new Dispatcher();
        using var owner = new ImageRequestLifetime(dispatcher, errors.Add);
        var old = owner.Begin();
        var current = owner.Begin();
        Check(old.Token.IsCancellationRequested && !old.IsCurrent && current.IsCurrent, "Replacement invalidates the old attachment request before delivery.");
        var late = new Tracked();
        Check(!old.TryOwn(late) && late.Disposes == 1, "An obsolete completion cannot leak or publish its decoded lease.");
        var frame = new Tracked();
        Check(current.TryOwn(frame) && !current.TryOwn(frame) && frame.Disposes == 0, "Current frame is owned exactly once.");
        var replacement = owner.Begin();
        Check(frame.Disposes == 1 && current.Token.IsCancellationRequested, "Source replacement releases the previous native frame.");
        var extra = new Tracked();
        Check(!current.TryOwn(extra) && extra.Disposes == 1, "An additional late resource is cleaned up independently.");
        Check(!current.TryOwn(frame) && frame.Disposes == 1, "A repeated completed native result cannot double-dispose the retired frame.");
        dispatcher.Access = false;
        Throws<InvalidOperationException>(() => owner.Begin());
        dispatcher.Access = true;
        owner.Dispose();
        var afterClose = new Tracked();
        Check(!replacement.TryOwn(afterClose) && afterClose.Disposes == 1, "Detach/disposal drops late native frames.");
        replacement.TryOwn(new Tracked { Error = new IOException("late cleanup") });
        Check(errors.Single().Message == "late cleanup", "Late cleanup failures reach the required reporter.");
        Throws<ObjectDisposedException>(() => owner.Begin());
        using var failureOwner = new ImageRequestLifetime(dispatcher, errors.Add);
        failureOwner.Begin().TryOwn(new Tracked { Error = new IOException("owned cleanup") });
        Throws<AggregateException>(() => failureOwner.Begin());
        Check(failureOwner.Begin().IsCurrent, "A cleanup failure never leaves the failed old request current.");
        using var reentrant = new ImageRequestLifetime(dispatcher, errors.Add);
        var request = reentrant.Begin();
        var tracked = new Tracked();
        request.TryOwn(tracked);
        using var callback = request.Token.Register(() => reentrant.Begin());
        Throws<AggregateException>(() => reentrant.Begin());
        Check(tracked.Disposes == 1 && !request.IsCurrent, "A reentrant cancellation error still releases the owned frame.");
    }

    private static async Task CacheOwnership()
    {
        var bytes = Fixtures.Png();
        var assembly = new FixtureAssembly(bytes);
        var manifest = new PackagedAssetManifest(assembly);
        var a = new PackagedImageSource(manifest, "a.png");
        var same = new PackagedImageSource(manifest, "a.png");
        var b = new PackagedImageSource(manifest, "b.jpg");
        using var cache = new ImageResourceCache(bytes.Length * 2, 2, 2);
        var first = await cache.AcquireAsync(a, new());
        var again = await cache.AcquireAsync(same, new(1, 1));
        Check(assembly.Opens == 1 && again.Plan.OutputWidth == 1 && cache.Statistics.PinnedEntries == 1,
            "Same manifest/id shares encoded bytes but has a request-specific decode plan.");
        using var second = await cache.AcquireAsync(b, new());
        Check(second.Plan.Format == PackagedImageFormat.Png, "Actual encoded signature overrides an untrusted extension.");
        await Fails<ImageResourceLimitException>(() => cache.AcquireAsync(new(manifest, "c.png"), new()));
        await Fails<InvalidDataException>(() => cache.AcquireAsync(a, new(maximumEncodedBytes: bytes.Length - 1)));
        first.Dispose(); first.Dispose(); again.Dispose();
        using var third = await cache.AcquireAsync(new(manifest, "c.png"), new());
        Check(cache.Statistics.Entries == 2 && cache.Statistics.ResidentBytes == bytes.Length * 2 &&
            cache.Statistics.ReservedBytes == 0, "Unpinned LRU eviction respects resident budget.");
        cache.Dispose();
        Check(second.Bytes.Span.SequenceEqual(bytes) && cache.Statistics.PinnedEntries == 2,
            "Disposing the cache preserves already transferred encoded leases.");
        second.Dispose(); third.Dispose();
        Check(cache.Statistics.ResidentBytes == 0 && cache.Statistics.Entries == 0, "Last pins release all disposed cache bytes.");
        Throws<ObjectDisposedException>(() => _ = first.Bytes);
        Throws<ObjectDisposedException>(() => cache.AcquireAsync(a, new()));

        using var small = new ImageResourceCache(bytes.Length - 1);
        await Fails<ImageResourceLimitException>(() => small.AcquireAsync(a, new()));
        Check(small.Statistics == default && assembly.Last!.Disposes == 1, "Budget rejection closes provider and releases its request slot.");
        using var cleared = new ImageResourceCache(bytes.Length);
        (await cleared.AcquireAsync(a, new())).Dispose();
        cleared.ClearUnused();
        Check(cleared.Statistics == default, "Explicit cleanup releases unpinned buffers.");
        using var isolated = new ImageResourceCache(bytes.Length * 2);
        using var firstAssembly = await isolated.AcquireAsync(a, new());
        var otherAssembly = new FixtureAssembly(Fixtures.Png(2, 3));
        using var other = await isolated.AcquireAsync(new(new(otherAssembly), "a.png"), new());
        Check(otherAssembly.Opens == 1 && other.Plan.SourceWidth == 2 && isolated.Statistics.Entries == 2,
            "The same asset id in a different manifest cannot alias cached content.");
        var dispatcher = new Dispatcher();
        var pixelBudget = new ImagePixelBudget(24);
        using var repeated = new ImageResourceCache(bytes.Length, 1);
        for (int cycle = 0; cycle < 110; cycle++)
        {
            using (var attachment = new ImageRequestLifetime(dispatcher, error => throw error))
            using (var encoded = await repeated.AcquireAsync(a, new()))
            {
                var ticket = attachment.Begin();
                Check(ticket.TryOwn(pixelBudget.Reserve(encoded.Plan)), "Attachment owns one decoded output reservation.");
            }
            repeated.ClearUnused();
            Check(repeated.Statistics == default && pixelBudget.ReservedBytes == 0,
                "Repeated attachment disposal releases encoded and decoded reservations.");
        }
    }

    private static async Task CacheCancellation()
    {
        var bytes = Fixtures.Png();
        var assembly = new FixtureAssembly(bytes);
        var manifest = new PackagedAssetManifest(assembly);
        using var cache = new ImageResourceCache(bytes.Length * 2, 4, 1);
        using var cancellation = new CancellationTokenSource();
        var paused = new ObservedStream(bytes) { Pause = new(TaskCreationOptions.RunContinuationsAsynchronously) };
        assembly.Factory = () => paused;
        Task<EncodedImageLease> pending = cache.AcquireAsync(new(manifest, "a.png"), new(), cancellation.Token);
        Check(cache.Statistics.InFlight == 1 && cache.Statistics.ReservedBytes == bytes.Length, "In-flight memory is reserved before buffer reads.");
        await Fails<ImageResourceLimitException>(() => cache.AcquireAsync(new(manifest, "b.jpg"), new()));
        cancellation.Cancel();
        // The fixture provider ignores cancellation until its native operation returns.
        paused.Pause.SetResult();
        await Fails<OperationCanceledException>(() => pending);
        Check(paused.Disposes == 1 && cache.Statistics == default, "Late canceled bytes never enter the cache and release all reservations.");
        var next = new ObservedStream(bytes) { Pause = new(TaskCreationOptions.RunContinuationsAsynchronously) };
        assembly.Factory = () => next;
        pending = cache.AcquireAsync(new(manifest, "c.png"), new());
        cache.Dispose();
        next.Pause.SetResult();
        await Fails<OperationCanceledException>(() => pending);
        Check(next.Disposes == 1 && cache.Statistics == default, "Cache disposal cancels delivery without abandoning a native read handle.");
        using var errors = new ImageResourceCache();
        var cleanup = new ObservedStream(bytes) { Error = new IOException("provider cleanup") };
        assembly.Factory = () => cleanup;
        await Fails<IOException>(() => errors.AcquireAsync(new(manifest, "a.png"), new()));
        Check(errors.Statistics == default && cleanup.Disposes == 1, "Failed provider disposal cannot publish a cache entry.");
        assembly.Factory = () => new ObservedStream([1, 2, 3]);
        await Fails<NotSupportedException>(() => errors.AcquireAsync(new(manifest, "a.png"), new()));
        Check(errors.Statistics == default, "Malformed/unsupported payload releases all resource accounting.");
        assembly.Factory = () => new ObservedStream([1, 2, 3]) { Error = new IOException("cleanup") };
        var combined = await Fails<AggregateException>(() => errors.AcquireAsync(new(manifest, "a.png"), new()));
        Check(combined.InnerExceptions[0] is NotSupportedException && combined.InnerExceptions[1].Message == "cleanup",
            "Header failure and provider cleanup errors are both preserved.");
    }

    private sealed class Dispatcher : IUiDispatcher
    {
        public bool Access { get; set; } = true;
        public bool CheckAccess() => Access;
        public void Post(Action action) => throw new NotSupportedException();
    }
    private sealed class Tracked : IDisposable
    {
        public int Disposes { get; private set; }
        public Exception? Error { get; init; }
        public void Dispose() { Disposes++; if (Error is not null) throw Error; }
    }
    private sealed class FixtureAssembly(byte[] bytes) : Assembly
    {
        public int Opens { get; private set; }
        public ObservedStream? Last { get; private set; }
        public Func<ObservedStream> Factory { get; set; } = () => new ObservedStream(bytes);
        public override string[] GetManifestResourceNames() => ["Xui.Asset.a.png", "Xui.Asset.b.jpg", "Xui.Asset.c.png"];
        public override Stream? GetManifestResourceStream(string name) { Opens++; return Last = Factory(); }
    }
    private sealed class ObservedStream(byte[] bytes) : MemoryStream(bytes)
    {
        public int Disposes { get; private set; }
        public TaskCompletionSource? Pause { get; init; }
        public Exception? Error { get; init; }
        public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default)
        {
            if (Pause is not null) await Pause.Task;
            return Read(buffer.Span);
        }
        public override int Read(Span<byte> buffer)
        {
            byte[] bytes = new byte[buffer.Length];
            int count = base.Read(bytes, 0, bytes.Length);
            bytes.AsSpan(0, count).CopyTo(buffer);
            return count;
        }
        protected override void Dispose(bool disposing)
        {
            if (disposing) Disposes++;
            base.Dispose(disposing);
            if (Error is not null) throw Error;
        }
    }
}
