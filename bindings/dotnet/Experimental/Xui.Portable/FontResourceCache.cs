using System.Security.Cryptography;
using System.Text;

namespace Xui.Experimental.Portable;

public readonly record struct FontResourceStatistics(long ResidentBytes, long ReservedBytes, int Faces, int PinnedFaces, int InFlight);

/// <summary>An explicit owner of verified font/license bytes, not native font registrations.</summary>
/// <remarks>
/// The 16 MiB/four-face/one-load limits bound owned buffers including reservations.
/// They exclude font-engine, glyph-cache, GPU and process-wide allocations.
/// Native registration must use a separate lifetime and collision registry; disposing this cache
/// must never be interpreted as permission to unregister a font still used by a native peer.
/// </remarks>
public sealed class FontResourceCache : IDisposable
{
    public const int MaximumBytes = 16 * 1024 * 1024;
    public const int MaximumFaces = 4;
    public const int MaximumInFlight = 1;
    public const int MaximumLicenseBytes = 64 * 1024;
    private readonly object gate = new();
    private readonly Dictionary<Key, Entry> entries = [];
    private readonly CancellationTokenSource lifetime = new();
    private readonly CancellationToken lifetimeToken;
    private long resident, reserved, clock;
    private int loading;
    private bool disposed;

    public FontResourceCache() => lifetimeToken = lifetime.Token;

    public FontResourceStatistics Statistics
    {
        get { lock (gate) return new(resident, reserved, entries.Count, entries.Values.Count(entry => entry.Pins > 0), loading); }
    }

    public Task<FontResourceLease> AcquireAsync(PackagedFontSource source, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<FontResourceLease>(cancellationToken);
        var key = new Key(source.Manifest, source.AssetId, source.Sha256, source.License.AssetId, source.License.Sha256);
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            if (entries.TryGetValue(key, out var entry))
            {
                entry.Pins++;
                entry.LastUse = ++clock;
                return Task.FromResult(new FontResourceLease(this, entry, source));
            }
            if (loading == MaximumInFlight)
                return Task.FromException<FontResourceLease>(new FontResourceLimitException("Another font resource load is in flight."));
            while (entries.Count >= MaximumFaces)
                if (!EvictOne()) return Task.FromException<FontResourceLease>(new FontResourceLimitException("All font resource slots are pinned."));
            loading++;
        }
        return LoadAsync(key, source, cancellationToken);
    }

    private async Task<FontResourceLease> LoadAsync(Key key, PackagedFontSource source, CancellationToken cancellationToken)
    {
        long reservation = 0;
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, lifetimeToken);
        var token = linked.Token;
        void Reserve(int amount)
        {
            lock (gate)
            {
                token.ThrowIfCancellationRequested();
                ObjectDisposedException.ThrowIf(disposed, this);
                while (amount > MaximumBytes - resident - reserved)
                    if (!EvictOne()) throw new FontResourceLimitException("The owned font byte budget is full.");
                reserved += amount;
                reservation += amount;
            }
        }
        try
        {
            byte[] font = await ReadAsync(source.Manifest, source.AssetId, FontResourceMetadata.MaximumEncodedBytes, Reserve, token);
            byte[] license = await ReadAsync(source.Manifest, source.License.AssetId, MaximumLicenseBytes, Reserve, token);
            token.ThrowIfCancellationRequested();
            string fontHash = Convert.ToHexString(SHA256.HashData(font)).ToLowerInvariant();
            string licenseHash = Convert.ToHexString(SHA256.HashData(license)).ToLowerInvariant();
            if (fontHash != source.Sha256 || licenseHash != source.License.Sha256)
                throw new InvalidDataException("Font or license bytes do not match their pinned SHA-256 identity.");
            try
            {
                string text = new UTF8Encoding(false, true).GetString(license);
                if (string.IsNullOrWhiteSpace(text) || text.Contains('\0')) throw new InvalidDataException("The font license must be nonempty UTF-8 text.");
            }
            catch (DecoderFallbackException error) { throw new InvalidDataException("The font license is not valid UTF-8 text.", error); }
            var metadata = FontResourceMetadata.Inspect(font, token);
            lock (gate)
            {
                token.ThrowIfCancellationRequested();
                ObjectDisposedException.ThrowIf(disposed, this);
                foreach (var existing in entries.Values)
                {
                    if ((string.Equals(existing.Metadata.FamilyName, metadata.FamilyName, StringComparison.OrdinalIgnoreCase) ||
                        string.Equals(existing.Metadata.PostScriptName, metadata.PostScriptName, StringComparison.OrdinalIgnoreCase)) &&
                        existing.Metadata.Sha256 != metadata.Sha256)
                        throw new FontResourceLimitException("Different font bytes use a family or PostScript name already owned by this cache.");
                }
                var entry = new Entry(key, font, license, metadata) { Pins = 1, LastUse = ++clock };
                entries.Add(key, entry);
                reserved -= reservation;
                reservation = 0;
                resident += entry.Bytes;
                return new FontResourceLease(this, entry, source);
            }
        }
        finally
        {
            lock (gate) { reserved -= reservation; loading--; }
        }
    }

    private static async Task<byte[]> ReadAsync(PackagedAssetManifest manifest, string id, int limit,
        Action<int> reserve, CancellationToken token)
    {
        var source = await manifest.OpenReadAsync(id, limit, token);
        try
        {
            // Assembly resources normally have an exact length; reject unknown lengths rather than
            // temporarily doubling allocations while growing a buffer.
            if (source.Length is not { } length || length < 1 || length > limit)
                throw new InvalidDataException("A packaged font or license requires a known, nonempty, bounded length.");
            reserve(checked((int)length));
            var bytes = new byte[(int)length];
            await source.Content.ReadExactlyAsync(bytes, token);
            if (await source.Content.ReadAsync(new byte[1], token) != 0)
                throw new InvalidDataException("The packaged font or license changed length while being read.");
            await source.DisposeAsync();
            token.ThrowIfCancellationRequested();
            return bytes;
        }
        catch (Exception error)
        {
            try { await source.DisposeAsync(); }
            catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            throw;
        }
    }

    private bool EvictOne()
    {
        Entry? candidate = null;
        foreach (var entry in entries.Values)
            if (entry.Pins == 0 && (candidate is null || entry.LastUse < candidate.LastUse)) candidate = entry;
        if (candidate is null) return false;
        entries.Remove(candidate.Key);
        resident -= candidate.Bytes;
        return true;
    }

    internal void Release(Entry entry)
    {
        lock (gate)
        {
            entry.Pins--;
            if (disposed && entry.Pins == 0) { entries.Remove(entry.Key); resident -= entry.Bytes; }
        }
    }

    public void ClearUnused()
    {
        lock (gate) { ObjectDisposedException.ThrowIf(disposed, this); while (EvictOne()) { } }
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

    internal readonly record struct Key(PackagedAssetManifest Manifest, string AssetId, string Hash, string LicenseId, string LicenseHash);
    internal sealed class Entry(Key key, byte[] font, byte[] license, FontResourceMetadata metadata)
    {
        internal readonly Key Key = key;
        internal readonly byte[] Font = font, License = license;
        internal readonly FontResourceMetadata Metadata = metadata;
        internal long Bytes => (long)Font.Length + License.Length;
        internal int Pins;
        internal long LastUse;
    }
}

/// <summary>A pin on verified package bytes. Borrowed memory is immutable and valid only for this lease's lifetime.</summary>
public sealed class FontResourceLease : IDisposable
{
    private FontResourceCache? owner;
    private FontResourceCache.Entry? entry;
    public PackagedFontSource Source { get; }
    public FontResourceMetadata Metadata { get; }
    public ReadOnlyMemory<byte> FontBytes => Current.Font;
    public ReadOnlyMemory<byte> LicenseBytes => Current.License;
    private FontResourceCache.Entry Current
    {
        get { ObjectDisposedException.ThrowIf(Volatile.Read(ref owner) is null, this); return entry!; }
    }
    internal FontResourceLease(FontResourceCache owner, FontResourceCache.Entry entry, PackagedFontSource source)
    {
        this.owner = owner; this.entry = entry; Source = source; Metadata = entry.Metadata;
    }
    public void Dispose()
    {
        var cache = Interlocked.Exchange(ref owner, null);
        if (cache is not null) cache.Release(Interlocked.Exchange(ref entry, null)!);
    }
}

public sealed class FontResourceLimitException(string message) : IOException(message);
