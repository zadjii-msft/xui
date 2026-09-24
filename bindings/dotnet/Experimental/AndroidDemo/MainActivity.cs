using Android.App;
using Android.OS;
using Android.Views;
using Android.Widget;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidDemo;

[Activity(Label = "XUI portable greeting", MainLauncher = true, Exported = true,
    Theme = "@android:style/Theme.Material.Light.NoActionBar",
    WindowSoftInputMode = SoftInput.AdjustResize)]
public sealed class MainActivity : Activity
{
    private Host? host;
    private Greeting? demo;
    private FrameLayout? surface;
    private AndroidDispatcher? dispatcher;
    private AndroidBackend? backend;
    private bool restoreFocus;
    private int selectionStart;
    private int selectionEnd;

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        dispatcher = new AndroidDispatcher();
        surface = new InsetSurface(this);
        SetContentView(surface);
        host = new Host(dispatcher);
        try
        {
            demo = new Greeting(host);
            if (savedInstanceState is not null)
            {
                demo.Count = savedInstanceState.GetInt("xui.count");
                demo.Entry = savedInstanceState.GetString("xui.entry") ?? "";
                demo.Message = savedInstanceState.GetString("xui.message") ?? demo.Message;
                restoreFocus = savedInstanceState.GetBoolean("xui.focus");
                selectionStart = savedInstanceState.GetInt("xui.selectionStart");
                selectionEnd = savedInstanceState.GetInt("xui.selectionEnd");
            }
        }
        catch (Exception creationError)
        {
            try { host.Dispose(); }
            catch (Exception cleanupError) { throw new AggregateException(creationError, cleanupError); }
            throw;
        }
    }

    protected override void OnStart()
    {
        base.OnStart();
        backend = new AndroidBackend(surface!, dispatcher!);
        host!.Attach(backend);
        if (restoreFocus && backend.FindViews("name").Single() is EditText input)
        {
            input.RequestFocus();
            int length = input.Text?.Length ?? 0;
            input.SetSelection(Math.Clamp(selectionStart, 0, length), Math.Clamp(selectionEnd, 0, length));
        }
    }

    private void CaptureInput()
    {
        if (host!.IsAttached && backend!.FindViews("name").Single() is EditText input)
        {
            restoreFocus = input.HasFocus;
            selectionStart = input.SelectionStart;
            selectionEnd = input.SelectionEnd;
        }
    }

    protected override void OnSaveInstanceState(Bundle outState)
    {
        CaptureInput();
        outState.PutInt("xui.count", demo!.Count);
        outState.PutString("xui.entry", demo.Entry);
        outState.PutString("xui.message", demo.Message);
        outState.PutBoolean("xui.focus", restoreFocus);
        outState.PutInt("xui.selectionStart", selectionStart);
        outState.PutInt("xui.selectionEnd", selectionEnd);
        base.OnSaveInstanceState(outState);
    }

    protected override void OnStop()
    {
        try
        {
            CaptureInput();
            host!.Detach();
        }
        finally
        {
            backend = null;
            base.OnStop();
        }
    }

    protected override void OnDestroy()
    {
        try { host?.Dispose(); }
        finally
        {
            host = null;
            demo = null;
            surface?.Dispose();
            base.OnDestroy();
        }
    }

    private sealed class InsetSurface(global::Android.Content.Context context) : FrameLayout(context)
    {
        public override WindowInsets? OnApplyWindowInsets(WindowInsets? insets)
        {
            if (insets is null) return null;
            if (OperatingSystem.IsAndroidVersionAtLeast(30))
            {
                var padding = insets.GetInsets(WindowInsets.Type.SystemBars() |
                    WindowInsets.Type.DisplayCutout() | WindowInsets.Type.Ime());
                SetPadding(padding.Left, padding.Top, padding.Right, padding.Bottom);
            }
            else
            {
                SetPadding(insets.SystemWindowInsetLeft, insets.SystemWindowInsetTop,
                    insets.SystemWindowInsetRight, insets.SystemWindowInsetBottom);
            }
            return insets;
        }
    }
}
