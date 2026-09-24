using Android.App;
using Android.OS;
using Android.Views;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidGalleryDemo;

[Activity(Name = "dev.xui.portable.gallery.VirtualListActivity", Label = "XUI virtual task list",
    MainLauncher = true, Exported = true, Theme = "@android:style/Theme.Material.Light.NoActionBar",
    WindowSoftInputMode = SoftInput.AdjustResize)]
public sealed class VirtualListActivity : Activity
{
    private Host? host;
    private VirtualList? component;
    private OrderSurface? surface;
    private AndroidDispatcher? dispatcher;

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        dispatcher = new AndroidDispatcher();
        surface = new OrderSurface(this);
        SetContentView(surface);
        host = new Host(dispatcher);
        try { component = VirtualList.CreateForViewport(host); }
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
        var backend = new AndroidBackend(surface!, dispatcher!);
        host!.Attach(backend);
        bool trace = Intent?.GetBooleanExtra("trace-viewport", false) == true;
        var lease = host.BeginVirtualViewport(component!.Viewport, component.Controller.RequestedCount,
            component.Controller.RowHeight, component.Controller.RequestedSourceVersion, request =>
            {
                if (trace) global::Android.Util.Log.Info("Xui.Android.VirtualList", $"Request: {request}");
                component.OnViewportRequested(request);
                if (trace)
                {
                    var scroll = backend.FindViews("virtual-scroll").Single();
                    var frame = (View)scroll.Parent!;
                    global::Android.Util.Log.Info("Xui.Android.VirtualList",
                        $"Native: height={scroll.Height}, measured={scroll.MeasuredHeight}, frame={frame.Height}/{frame.MeasuredHeight}, offset={scroll.ScrollY}, model={component.Controller.Offset}/{component.Controller.ViewportHeight}.");
                }
            });
        component.AttachViewport(lease, host.SetVirtualItemInfo);
    }

    protected override void OnStop()
    {
        try
        {
            component!.CaptureEditingState();
            host!.Detach();
            component.PrepareForViewportAttachment();
        }
        finally { base.OnStop(); }
    }

    protected override void OnDestroy()
    {
        try { host?.Dispose(); }
        finally
        {
            component = null;
            host = null;
            surface?.Dispose();
            base.OnDestroy();
        }
    }
}
