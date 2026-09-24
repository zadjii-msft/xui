namespace Xui.Experimental.Portable;

internal sealed class FileSelectionReadStream(Stream content, long maximumBytes) : Stream
{
    private readonly byte[] probe = new byte[1];
    private long read;
    private int reading;
    private int disposed;
    private bool ended;
    private bool oversized;

    public override bool CanRead => Volatile.Read(ref disposed) == 0 && content.CanRead;
    public override bool CanSeek => false;
    public override bool CanWrite => false;
    public override long Length => throw new NotSupportedException();
    public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
    public override void Flush() => ObjectDisposedException.ThrowIf(Volatile.Read(ref disposed) != 0, this);
    public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
    public override void SetLength(long value) => throw new NotSupportedException();
    public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();

    private void EnterRead()
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref disposed) != 0, this);
        if (Interlocked.CompareExchange(ref reading, 1, 0) != 0)
            throw new InvalidOperationException("Concurrent reads of a selected file are not supported.");
        if (Volatile.Read(ref disposed) != 0)
        {
            Volatile.Write(ref reading, 0);
            throw new ObjectDisposedException(nameof(FileSelectionReadStream));
        }
    }

    private int Requested(int length)
    {
        if (oversized) throw new FileSelectionTooLargeException(maximumBytes);
        return (int)Math.Min(length, maximumBytes - read);
    }

    private void Observe(int count, int requested)
    {
        if (count < 0 || count > requested) throw new IOException("The selected file stream returned an invalid byte count.");
        read += count;
        if (count == 0 && requested != 0) ended = true;
    }

    private void ObserveProbe(int count)
    {
        if (count != 0)
        {
            oversized = true;
            throw new FileSelectionTooLargeException(maximumBytes);
        }
        ended = true;
    }

    public override int Read(byte[] buffer, int offset, int count)
    {
        ArgumentNullException.ThrowIfNull(buffer);
        return Read(buffer.AsSpan(offset, count));
    }

    public override int Read(Span<byte> buffer)
    {
        EnterRead();
        try
        {
            int requested = Requested(buffer.Length);
            if (buffer.IsEmpty || ended) return 0;
            int count = content.Read(buffer[..requested]);
            Observe(count, requested);
            // Probe before reporting a full-budget read, so an oversized file never looks truncated.
            if (read == maximumBytes) ObserveProbe(content.Read(probe, 0, 1));
            return count;
        }
        finally { Volatile.Write(ref reading, 0); }
    }

    public override int ReadByte()
    {
        Span<byte> buffer = stackalloc byte[1];
        return Read(buffer) == 0 ? -1 : buffer[0];
    }

    public override Task<int> ReadAsync(byte[] buffer, int offset, int count, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(buffer);
        return ReadAsync(buffer.AsMemory(offset, count), cancellationToken).AsTask();
    }

    public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        EnterRead();
        try
        {
            int requested = Requested(buffer.Length);
            if (buffer.IsEmpty || ended) return 0;
            int count = await content.ReadAsync(buffer[..requested], cancellationToken);
            Observe(count, requested);
            cancellationToken.ThrowIfCancellationRequested();
            if (read == maximumBytes)
                ObserveProbe(await content.ReadAsync(probe, cancellationToken));
            cancellationToken.ThrowIfCancellationRequested();
            return count;
        }
        finally { Volatile.Write(ref reading, 0); }
    }

    protected override void Dispose(bool disposing)
    {
        try
        {
            if (disposing && Interlocked.Exchange(ref disposed, 1) == 0) content.Dispose();
        }
        finally { base.Dispose(disposing); }
    }

    public override async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) == 0)
            await content.DisposeAsync();
        GC.SuppressFinalize(this);
    }
}
