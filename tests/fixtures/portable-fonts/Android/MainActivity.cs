using Android.App;
using Android.OS;
using Android.Widget;
using Proof = ControlledFontConsumer.FontProof;
using System.Security.Cryptography;

[Activity(Label = "XUI controlled font byte fixture", MainLauncher = true, Exported = true)]
public sealed class MainActivity : Activity
{
    protected override async void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        try
        {
            await Proof.VerifyAsync();
            using var font = Assets!.Open(Proof.AndroidName);
            using var license = Assets.Open(Proof.AndroidLicenseName);
            if (Convert.ToHexString(SHA256.HashData(font)).ToLowerInvariant() != Proof.Hash ||
                Convert.ToHexString(SHA256.HashData(license)).ToLowerInvariant() != Proof.LicenseHash)
                throw new InvalidDataException("Android projected bytes differ from the shared assembly and license.");
            if (IsDestroyed || IsFinishing) return;
            SetContentView(new TextView(this) { Text = "PASS: projected font and full OFL bytes. No Typeface registration." });
        }
        catch (Exception error)
        {
            Android.Util.Log.Error("Xui.FontBytes", error.ToString());
            throw;
        }
    }
}
