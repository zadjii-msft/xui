using Android.App;
using Android.OS;
using Android.Views;
using Android.Widget;
using Xui.Experimental.Android;
using Xui.Experimental.Portable;

namespace PortableApp.Android;

[Activity(Label = "PortableApp", MainLauncher = true, Exported = true,
    Theme = "@android:style/Theme.Material.Light.NoActionBar", WindowSoftInputMode = SoftInput.AdjustResize)]
public sealed class MainActivity : Activity
{
    private Host? host;
    private Counter? counter;
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
        surface = new FrameLayout(this);
        surface.SetFitsSystemWindows(true);
        SetContentView(surface);
        host = new Host(dispatcher);
        try
        {
            counter = new Counter(host);
            if (savedInstanceState is not null)
            {
                counter.State = new CounterState(savedInstanceState.GetInt("count"),
                    savedInstanceState.GetString("name") ?? "");
                restoreFocus = savedInstanceState.GetBoolean("focus");
                selectionStart = savedInstanceState.GetInt("selectionStart");
                selectionEnd = savedInstanceState.GetInt("selectionEnd");
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
        outState.PutInt("count", counter!.State.Count);
        outState.PutString("name", counter.State.Name);
        outState.PutBoolean("focus", restoreFocus);
        outState.PutInt("selectionStart", selectionStart);
        outState.PutInt("selectionEnd", selectionEnd);
        base.OnSaveInstanceState(outState);
    }

    protected override void OnStop()
    {
        try { CaptureInput(); host!.Detach(); }
        finally { backend = null; base.OnStop(); }
    }

    protected override void OnDestroy()
    {
        try { host?.Dispose(); }
        finally
        {
            host = null;
            counter = null;
            surface?.Dispose();
            base.OnDestroy();
        }
    }
}
