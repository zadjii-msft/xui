namespace Xui.Experimental.Portable;

public readonly record struct ImageCacheStatistics(
    long ResidentBytes, long ReservedBytes, int Entries, int PinnedEntries, int InFlight);

/// <summary>An explicit, bounded owner of encoded assembly image buffers, not a decoded bitmap cache.</summary>
/// <remarks>
/// Each backend must separately reserve decoded output and own its native resource leases.
/// Pins cannot be evicted. A full budget or request limit fails without an unbounded wait queue.
/// The key is manifest object identity plus the ordinal asset id; extensions are not trusted.
/// Dispose drops unpinned data and cancels loads. Existing leases remain valid until their disposal.
/// </remarks>
public sealed class ImageResourceCache : IDisposable
{
    private readonly object gate = new();
    private readonly Dictionary<Key, Entry> entries = [];
    private readonly HashSet<Key> loading = [];
    private readonly CancellationTokenSource lifetime = new();
    private readonly CancellationToken lifetimeToken;
    private long residentBytes, reservedBytes, clock;
    private bool disposed;
    public long MaximumBytes { get; }
    public int MaximumEntries { get; }
    public int MaximumInFlight { get; }

    public ImageResourceCache(long maximumBytes = 8 * 1024 * 1024, int maximumEntries = 32, int maximumInFlight = 2)
    {
        if (maximumBytes is < 1 or > 64 * 1024 * 1024) throw new ArgumentOutOfRangeException(nameof(maximumBytes));
        if (maximumEntries is < 1 or > 128) throw new ArgumentOutOfRangeException(nameof(maximumEntries));
        if (maximumInFlight is < 1 or > 4) throw new ArgumentOutOfRangeException(nameof(maximumInFlight));
        MaximumBytes = maximumBytes;
        MaximumEntries = maximumEntries;
        MaximumInFlight = maximumInFlight;
        lifetimeToken = lifetime.Token;
    }

    public ImageCacheStatistics Statistics
    {
        get
        {
            lock (gate) return new(residentBytes, reservedBytes, entries.Count,
                entries.Values.Count(entry => entry.Pins > 0), loading.Count);
        }
    }

    public Task<EncodedImageLease> AcquireAsync(PackagedImageSource source, ImageDecodeOptions options,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(options);
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<EncodedImageLease>(cancellationToken);
        var key = new Key(source.Manifest, source.AssetId);
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            if (entries.TryGetValue(key, out var entry))
            {
                if (entry.Length > options.MaximumEncodedBytes)
                    return Task.FromException<EncodedImageLease>(new InvalidDataException("The cached image exceeds this request's encoded-byte limit."));
                var plan = new ImageDecodePlan(entry.Format, entry.Width, entry.Height, options);
                entry.Pins++;
                entry.LastUse = ++clock;
                return Task.FromResult(new EncodedImageLease(this, entry, plan));
            }
            if (loading.Count >= MaximumInFlight || loading.Contains(key))
                return Task.FromException<EncodedImageLease>(new ImageResourceLimitException("The image request limit is full or this source is already loading."));
            while (entries.Count + loading.Count >= MaximumEntries)
                if (!EvictOne()) return Task.FromException<EncodedImageLease>(new ImageResourceLimitException("The image entry budget is pinned or loading."));
            loading.Add(key);
        }
        return LoadAsync(key, options, cancellationToken);
    }

    private async Task<EncodedImageLease> LoadAsync(Key key, ImageDecodeOptions options, CancellationToken cancellationToken)
    {
        long reservation = 0;
        OwnedAssetRead? source = null;
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, lifetimeToken);
        var token = linked.Token;
        try
        {
            source = await key.Manifest.OpenReadAsync(key.AssetId, options.MaximumEncodedBytes, token);
            token.ThrowIfCancellationRequested();
            int capacity = source.Length.HasValue ? checked((int)source.Length.Value) : options.MaximumEncodedBytes;
            if (capacity == 0) throw new InvalidDataException("An encoded image cannot be empty.");
            lock (gate)
            {
                token.ThrowIfCancellationRequested();
                ObjectDisposedException.ThrowIf(disposed, this);
                while (capacity > MaximumBytes - residentBytes - reservedBytes)
                    if (!EvictOne()) throw new ImageResourceLimitException("The encoded-image byte budget is full.");
                reservedBytes += capacity;
                reservation = capacity;
            }
            var bytes = new byte[capacity];
            int length = 0;
            while (length < bytes.Length)
            {
                token.ThrowIfCancellationRequested();
                int count = await source.Content.ReadAsync(bytes.AsMemory(length, Math.Min(65536, bytes.Length - length)), token);
                if (count == 0) break;
                length += count;
            }
            if (length == bytes.Length && await source.Content.ReadAsync(new byte[1], token) != 0)
                throw new InvalidDataException("The embedded image grew beyond its advertised length.");
            if (source.Length.HasValue && length != source.Length.Value)
                throw new InvalidDataException("The embedded image was truncated during loading.");
            var plan = ImageDecodePlan.Inspect(bytes.AsSpan(0, length), options, token);
            // Complete provider cleanup before making the new cache entry visible.
            await source.DisposeAsync();
            source = null;
            token.ThrowIfCancellationRequested();
            lock (gate)
            {
                token.ThrowIfCancellationRequested();
                ObjectDisposedException.ThrowIf(disposed, this);
                var entry = new Entry(key, bytes, length, plan) { Pins = 1, LastUse = ++clock };
                entries.Add(key, entry);
                reservedBytes -= reservation;
                reservation = 0;
                residentBytes += capacity;
                return new EncodedImageLease(this, entry, plan);
            }
        }
        catch (Exception error)
        {
            if (source is not null)
            {
                try { await source.DisposeAsync(); }
                catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            }
            throw;
        }
        finally
        {
            lock (gate)
            {
                reservedBytes -= reservation;
                loading.Remove(key);
            }
        }
    }

    private bool EvictOne()
    {
        Entry? oldest = null;
        foreach (var entry in entries.Values)
            if (entry.Pins == 0 && (oldest is null || entry.LastUse < oldest.LastUse)) oldest = entry;
        if (oldest is null) return false;
        entries.Remove(oldest.Key);
        residentBytes -= oldest.Data.Length;
        return true;
    }

    internal void Release(Entry entry)
    {
        lock (gate)
        {
            entry.Pins--;
            if (disposed && entry.Pins == 0)
            {
                entries.Remove(entry.Key);
                residentBytes -= entry.Data.Length;
            }
        }
    }

    public void ClearUnused()
    {
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            while (EvictOne()) { }
        }
    }

    public void Dispose()
    {
        lock (gate)
        {
            if (disposed) return;
            disposed = true;
            while (EvictOne()) { }
        }
        try { lifetime.Cancel(); }
        finally { lifetime.Dispose(); }
    }

    internal readonly record struct Key(PackagedAssetManifest Manifest, string AssetId);
    internal sealed class Entry(Key key, byte[] data, int length, ImageDecodePlan plan)
    {
        internal readonly Key Key = key;
        internal readonly byte[] Data = data;
        internal readonly int Length = length;
        internal readonly PackagedImageFormat Format = plan.Format;
        internal readonly int Width = plan.SourceWidth, Height = plan.SourceHeight;
        internal int Pins;
        internal long LastUse;
    }
}

/// <summary>One pin on an encoded buffer. Borrowed Bytes must not outlive or be mutated beyond this lease.</summary>
public sealed class EncodedImageLease : IDisposable
{
    private ImageResourceCache? owner;
    private ImageResourceCache.Entry? entry;
    public ImageDecodePlan Plan { get; }
    public ReadOnlyMemory<byte> Bytes
    {
        get
        {
            ObjectDisposedException.ThrowIf(Volatile.Read(ref owner) is null, this);
            var value = entry ?? throw new ObjectDisposedException(nameof(EncodedImageLease));
            return value.Data.AsMemory(0, value.Length);
        }
    }

    internal EncodedImageLease(ImageResourceCache owner, ImageResourceCache.Entry entry, ImageDecodePlan plan)
    {
        this.owner = owner;
        this.entry = entry;
        Plan = plan;
    }

    public void Dispose()
    {
        var cache = Interlocked.Exchange(ref owner, null);
        if (cache is not null) cache.Release(Interlocked.Exchange(ref entry, null)!);
    }
}
