using Android.App;
using Android.OS;
using Android.Widget;

[Activity(Label = "XUI localization fixture", MainLauncher = true, Exported = true)]
public sealed class MainActivity : Activity
{
    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        try
        {
            LocalizationConsumer.LocalizationProof.Verify();
            SetContentView(new TextView(this) { Text = "PASS: packaged localization cultures and fallback." });
            Android.Util.Log.Info("Xui.Localization", "PASS: packaged localization cultures and fallback.");
        }
        catch (Exception error)
        {
            Android.Util.Log.Error("Xui.Localization", error.ToString());
            throw;
        }
    }
}
