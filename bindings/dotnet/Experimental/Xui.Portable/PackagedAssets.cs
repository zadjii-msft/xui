using System.Collections.ObjectModel;
using System.Reflection;
using System.Text.RegularExpressions;

namespace Xui.Experimental.Portable;

public enum PackagedAssetKind { Data, Image, Font }

/// <summary>Catalog metadata, not proof of a supported or valid image/font encoding.</summary>
public sealed class PackagedAssetDescriptor
{
    public string Id { get; }
    public PackagedAssetKind Kind { get; }
    public string ContentType { get; }

    internal PackagedAssetDescriptor(string id)
    {
        Id = id;
        (Kind, ContentType) = Path.GetExtension(id) switch
        {
            ".png" => (PackagedAssetKind.Image, "image/png"),
            ".jpg" or ".jpeg" => (PackagedAssetKind.Image, "image/jpeg"),
            ".gif" => (PackagedAssetKind.Image, "image/gif"),
            ".webp" => (PackagedAssetKind.Image, "image/webp"),
            ".ttf" => (PackagedAssetKind.Font, "font/ttf"),
            ".otf" => (PackagedAssetKind.Font, "font/otf"),
            ".woff" => (PackagedAssetKind.Font, "font/woff"),
            ".woff2" => (PackagedAssetKind.Font, "font/woff2"),
            _ => (PackagedAssetKind.Data, "application/octet-stream")
        };
    }
}

/// <summary>Canonical identifiers shared by the packaged-asset build targets and runtime.</summary>
public static class PackagedAssetIds
{
    public const string ResourcePrefix = "Xui.Asset.";
    public const int MaximumLength = 160;
    private static readonly Regex Valid = new(
        @"\A[a-z0-9](?:[a-z0-9._-]*[a-z0-9])?(?:/[a-z0-9](?:[a-z0-9._-]*[a-z0-9])?)*\z",
        RegexOptions.CultureInvariant, TimeSpan.FromSeconds(1));

    public static void Validate(string id)
    {
        ArgumentNullException.ThrowIfNull(id);
        if (id.Length is < 1 or > MaximumLength || !Valid.IsMatch(id))
            throw new ArgumentException("Asset ids require 1-160 lowercase ASCII letters, digits, dots, hyphens, underscores, or slash-separated segments; each segment starts and ends with a letter or digit.", nameof(id));
    }
}

/// <summary>An immutable catalog of XuiAsset resources embedded in one explicitly selected assembly.</summary>
/// <remarks>
/// No path, network URL, or fallback assembly is searched. Image/font metadata is inferred
/// from the id's extension; decoding, font registration, pixel limits, and caches are not provided.
/// The catalog does not retain opened streams. Each successful open transfers a fresh owner.
/// </remarks>
public sealed class PackagedAssetManifest
{
    public const int MaximumAssets = 4096;
    private readonly Assembly assembly;
    private readonly Dictionary<string, PackagedAssetDescriptor> entries = new(StringComparer.Ordinal);
    public ReadOnlyCollection<PackagedAssetDescriptor> Assets { get; }

    public PackagedAssetManifest(Assembly assembly)
    {
        this.assembly = assembly ?? throw new ArgumentNullException(nameof(assembly));
        string[] resources = assembly.GetManifestResourceNames();
        if (resources.Count(resource => resource.StartsWith(PackagedAssetIds.ResourcePrefix, StringComparison.Ordinal)) > MaximumAssets)
            throw new InvalidDataException($"An assembly may declare at most {MaximumAssets} packaged assets.");
        foreach (string resource in resources)
        {
            if (!resource.StartsWith(PackagedAssetIds.ResourcePrefix, StringComparison.Ordinal)) continue;
            string id = resource[PackagedAssetIds.ResourcePrefix.Length..];
            PackagedAssetIds.Validate(id);
            if (!entries.TryAdd(id, new PackagedAssetDescriptor(id)))
                throw new InvalidDataException($"Duplicate packaged asset id: {id}.");
        }
        Assets = Array.AsReadOnly(entries.Values.OrderBy(entry => entry.Id, StringComparer.Ordinal).ToArray());
    }

    public PackagedAssetDescriptor Get(string id)
    {
        PackagedAssetIds.Validate(id);
        return entries.TryGetValue(id, out var asset) ? asset : throw new FileNotFoundException($"Packaged asset '{id}' was not declared in the selected assembly.", id);
    }

    public Task<OwnedAssetRead> OpenReadAsync(string id, long maximumBytes, CancellationToken cancellationToken = default)
    {
        PackagedAssetIds.Validate(id);
        if (maximumBytes <= 0) throw new ArgumentOutOfRangeException(nameof(maximumBytes));
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<OwnedAssetRead>(cancellationToken);
        return OpenCoreAsync(id, maximumBytes, cancellationToken);
    }

    private async Task<OwnedAssetRead> OpenCoreAsync(string id, long maximumBytes, CancellationToken cancellationToken)
    {
        var asset = Get(id);
        Stream stream = assembly.GetManifestResourceStream(PackagedAssetIds.ResourcePrefix + id)
            ?? throw new InvalidDataException($"The declared packaged asset '{id}' has no embedded content.");
        try
        {
            if (!stream.CanRead) throw new InvalidDataException($"Packaged asset '{id}' is not readable.");
            long? length = stream.CanSeek ? stream.Length : null;
            if (length < 0) throw new InvalidDataException("The embedded asset stream has an invalid length.");
            if (length > maximumBytes) throw new IOException($"Packaged asset '{id}' exceeds the {maximumBytes}-byte read limit.");
            cancellationToken.ThrowIfCancellationRequested();
            return new OwnedAssetRead(asset, stream, length, maximumBytes);
        }
        catch (Exception error)
        {
            try { await stream.DisposeAsync(); }
            catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            throw;
        }
    }
}

/// <summary>One caller-owned, bounded, sequential asset stream. Dispose after use.</summary>
public sealed class OwnedAssetRead : IDisposable, IAsyncDisposable
{
    public PackagedAssetDescriptor Asset { get; }
    public long? Length { get; }
    public long MaximumBytes { get; }
    public Stream Content { get; }

    internal OwnedAssetRead(PackagedAssetDescriptor asset, Stream stream, long? length, long maximumBytes)
    {
        Asset = asset;
        Length = length;
        MaximumBytes = maximumBytes;
        Content = new FileSelectionReadStream(stream, maximumBytes);
    }

    public void Dispose() => Content.Dispose();
    public ValueTask DisposeAsync() => Content.DisposeAsync();
}
