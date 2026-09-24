namespace Xui.Experimental.Portable;

/// <summary>Owned four-byte-per-pixel reservations, excluding opaque codec, GPU, and driver allocations.</summary>
/// <remarks>
/// Use separate budget instances for raw source pixels and decoded output copies. A source
/// budget may be configured up to 64 MiB; the output budget defaults to 8 MiB.
/// These reservations are admission estimates, not hard limits on total native memory.
/// </remarks>
public sealed class ImagePixelBudget
{
    private readonly object gate = new();
    private long reserved;
    public long MaximumBytes { get; }
    public long ReservedBytes { get { lock (gate) return reserved; } }

    public ImagePixelBudget(long maximumBytes = 8 * 1024 * 1024)
    {
        if (maximumBytes is < 4 or > 64 * 1024 * 1024) throw new ArgumentOutOfRangeException(nameof(maximumBytes));
        MaximumBytes = maximumBytes;
    }

    /// <summary>Reserve before allocating one output buffer. Multiple native copies need separate reservations.</summary>
    public ImagePixelReservation Reserve(ImageDecodePlan plan)
    {
        ArgumentNullException.ThrowIfNull(plan);
        return ReserveBytes(plan.OutputPixelBytes);
    }

    /// <summary>Reserve the preflight source dimensions before a codec creates an owned raw bitmap.</summary>
    /// <remarks>
    /// Keep this reservation and the backend's native in-flight permit until decoding settles
    /// and the raw bitmap is closed, even if the request is canceled or its attachment retires.
    /// A codec without metadata-only inspection must validate its actual dimensions after raw
    /// decoding and before allocating output. Reserve every concurrently owned output copy
    /// separately with Reserve; do not substitute an output plan for source accounting.
    /// </remarks>
    public ImagePixelReservation ReserveSource(ImageDecodePlan plan)
    {
        ArgumentNullException.ThrowIfNull(plan);
        return ReserveBytes(checked(plan.SourcePixels * 4));
    }

    private ImagePixelReservation ReserveBytes(long bytes)
    {
        lock (gate)
        {
            if (bytes > MaximumBytes - reserved) throw new ImageResourceLimitException("The owned decoded-image pixel budget is full.");
            reserved += bytes;
        }
        return new ImagePixelReservation(this, bytes);
    }

    internal void Release(long bytes) { lock (gate) reserved -= bytes; }
}

public sealed class ImagePixelReservation : IDisposable
{
    private ImagePixelBudget? owner;
    public long Bytes { get; }
    internal ImagePixelReservation(ImagePixelBudget owner, long bytes) { this.owner = owner; Bytes = bytes; }
    public void Dispose() => Interlocked.Exchange(ref owner, null)?.Release(Bytes);
}
