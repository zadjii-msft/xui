using Xui.Experimental.Portable;

namespace ControlledFontConsumer;

public static class FontProof
{
    public const string Hash = "8809dcad25318225052f88333e208c5aad4adcb7b2c934c135735ec19aa410b4";
    public const string LicenseHash = "4f4bc3806a1e55789c6ef75ca5fc628297b05292f74966474dc0d40324abc609";
    public const string AndroidName = "xui-fonts/" + Hash + ".ttf";
    public const string AndroidLicenseName = "xui-fonts/" + Hash + ".license.txt";

    public static async Task VerifyAsync()
    {
        var manifest = new PackagedAssetManifest(typeof(FontProof).Assembly);
        var license = new FontLicenseDeclaration("licenses/abel-ofl.txt", LicenseHash, "OFL-1.1",
            new Uri("https://github.com/google/fonts/tree/9437b806936896fa1a8c812e561067a5f30f5933/ofl/abel"));
        var source = new PackagedFontSource(manifest, "fonts/abel-regular.ttf", Hash, license);
        using var cache = new FontResourceCache();
        using var resource = await cache.AcquireAsync(source);
        if (resource.Metadata.Sha256 != Hash || resource.Metadata.FamilyName != "Abel" ||
            resource.Metadata.Weight != 400 || resource.FontBytes.Length != 35220 || resource.LicenseBytes.Length != 4418 ||
            source.AndroidAssetName != AndroidName)
            throw new InvalidDataException("The controlled font identity or projection mapping changed.");
    }
}
