namespace Xui.Experimental.Portable;

/// <summary>Application-declared license provenance; a matching hash is not a legal rights determination.</summary>
public sealed class FontLicenseDeclaration
{
    public string AssetId { get; }
    public string Sha256 { get; }
    public string LicenseExpression { get; }
    public Uri Source { get; }

    public FontLicenseDeclaration(string assetId, string sha256, string licenseExpression, Uri source)
    {
        PackagedAssetIds.Validate(assetId);
        FontResourceIdentity.ValidateHash(sha256);
        ArgumentException.ThrowIfNullOrWhiteSpace(licenseExpression);
        ArgumentNullException.ThrowIfNull(source);
        if (licenseExpression.Length > 128 || licenseExpression.Any(char.IsControl) ||
            !source.IsAbsoluteUri || source.Scheme != Uri.UriSchemeHttps || source.UserInfo.Length != 0 ||
            source.OriginalString.Length > 2048 || source.OriginalString.Any(char.IsControl))
            throw new ArgumentException("Font licenses require a bounded expression and absolute HTTPS provenance without credentials.");
        AssetId = assetId;
        Sha256 = sha256;
        LicenseExpression = licenseExpression;
        Source = source;
    }
}

/// <summary>One pinned, licensed font resource. Only package assets are read; Source is provenance, never fetched.</summary>
public sealed class PackagedFontSource
{
    public PackagedAssetManifest Manifest { get; }
    public string AssetId { get; }
    public string Sha256 { get; }
    public FontLicenseDeclaration License { get; }
    public string AndroidAssetName => $"xui-fonts/{Sha256}.ttf";

    public PackagedFontSource(PackagedAssetManifest manifest, string assetId, string sha256, FontLicenseDeclaration license)
    {
        Manifest = manifest ?? throw new ArgumentNullException(nameof(manifest));
        ArgumentNullException.ThrowIfNull(license);
        FontResourceIdentity.ValidateHash(sha256);
        manifest.Get(assetId);
        manifest.Get(license.AssetId);
        if (assetId == license.AssetId) throw new ArgumentException("Font bytes and license text must be separate assets.", nameof(assetId));
        AssetId = assetId;
        Sha256 = sha256;
        License = license;
    }
}

internal static class FontResourceIdentity
{
    internal static void ValidateHash(string hash)
    {
        ArgumentNullException.ThrowIfNull(hash);
        if (hash.Length != 64 || hash.Any(character => character is not (>= '0' and <= '9' or >= 'a' and <= 'f')))
            throw new ArgumentException("A font resource digest must be exactly 64 lowercase SHA-256 hexadecimal characters.", nameof(hash));
    }
}
