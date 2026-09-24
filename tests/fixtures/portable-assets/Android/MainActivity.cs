using Android.App;
using Android.OS;
using Android.Widget;

[Activity(Label = "XUI owned asset fixture", MainLauncher = true, Exported = true)]
public sealed class MainActivity : Activity
{
    private readonly CancellationTokenSource lifetime = new();

    protected override async void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        var text = new TextView(this);
        SetContentView(text);
        CancellationToken token = lifetime.Token;
        try
        {
            await AssetConsumer.AssetProof.VerifyAsync(token);
            token.ThrowIfCancellationRequested();
            text.Text = "PASS: shared packaged image/font fixture bytes.";
            Android.Util.Log.Info("Xui.Assets", text.Text);
        }
        catch (System.OperationCanceledException) when (token.IsCancellationRequested)
        {
            Android.Util.Log.Info("Xui.Assets", "Fixture canceled during Activity destruction.");
        }
        catch (Exception error)
        {
            Android.Util.Log.Error("Xui.Assets", error.ToString());
            throw;
        }
    }

    protected override void OnDestroy()
    {
        try { lifetime.Cancel(); }
        finally { lifetime.Dispose(); base.OnDestroy(); }
    }
}
