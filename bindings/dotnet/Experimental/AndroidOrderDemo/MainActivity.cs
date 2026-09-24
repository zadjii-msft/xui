using Android.App;
using Android.OS;
using Android.Views;
using Android.Widget;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidOrderDemo;

[Activity(Label = "XUI shared order builder", MainLauncher = true, Exported = true,
    Theme = "@android:style/Theme.Material.Light.NoActionBar",
    WindowSoftInputMode = SoftInput.AdjustResize)]
public sealed class MainActivity : Activity
{
    private static readonly string[] InputIds = ["customer-name", "email", "discount-code"];
    private Host? host;
    private OrderBuilder? order;
    private OrderSurface? surface;
    private AndroidDispatcher? dispatcher;
    private AndroidBackend? backend;
    private string? focusedInputId;
    private int selectionStart;
    private int selectionEnd;

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        dispatcher = new AndroidDispatcher();
        surface = new OrderSurface(this);
        SetContentView(surface);
        host = new Host(dispatcher);
        try
        {
            order = new OrderBuilder(host);
            if (savedInstanceState is not null)
            {
                order.State = new OrderState
                {
                    CustomerName = savedInstanceState.GetString("order.name") ?? "",
                    Email = savedInstanceState.GetString("order.email") ?? "",
                    DiscountCode = savedInstanceState.GetString("order.discount") ?? "",
                    CoffeeQuantity = savedInstanceState.GetInt("order.coffee"),
                    TeaQuantity = savedInstanceState.GetInt("order.tea"),
                    CocoaQuantity = savedInstanceState.GetInt("order.cocoa"),
                    Reviewing = savedInstanceState.GetBoolean("order.reviewing")
                };
                focusedInputId = savedInstanceState.GetString("order.focus");
                selectionStart = savedInstanceState.GetInt("order.selectionStart");
                selectionEnd = savedInstanceState.GetInt("order.selectionEnd");
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
        if (focusedInputId is not null)
        {
            var input = (EditText)backend.FindViews(focusedInputId).Single();
            input.RequestFocus();
            int length = input.Text?.Length ?? 0;
            input.SetSelection(Math.Clamp(selectionStart, 0, length), Math.Clamp(selectionEnd, 0, length));
        }
    }

    private void CaptureInput()
    {
        if (!host!.IsAttached) return;
        focusedInputId = null;
        foreach (string id in InputIds)
        {
            var input = (EditText)backend!.FindViews(id).Single();
            if (!input.HasFocus) continue;
            focusedInputId = id;
            selectionStart = input.SelectionStart;
            selectionEnd = input.SelectionEnd;
            break;
        }
    }

    protected override void OnSaveInstanceState(Bundle outState)
    {
        CaptureInput();
        var state = order!.State;
        outState.PutString("order.name", state.CustomerName);
        outState.PutString("order.email", state.Email);
        outState.PutString("order.discount", state.DiscountCode);
        outState.PutInt("order.coffee", state.CoffeeQuantity);
        outState.PutInt("order.tea", state.TeaQuantity);
        outState.PutInt("order.cocoa", state.CocoaQuantity);
        outState.PutBoolean("order.reviewing", state.Reviewing);
        outState.PutString("order.focus", focusedInputId);
        outState.PutInt("order.selectionStart", selectionStart);
        outState.PutInt("order.selectionEnd", selectionEnd);
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
            order = null;
            surface?.Dispose();
            base.OnDestroy();
        }
    }
}
