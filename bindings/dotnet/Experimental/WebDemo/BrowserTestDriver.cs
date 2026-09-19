#if DEBUG
using Microsoft.JSInterop;
using PortableDemo;
using Xui.Experimental.Portable;
using Xui.Experimental.Web;

// This bridge is absent from Release builds and inactive without the explicit test query.
internal sealed class BrowserTestDriver : IDisposable
{
    private readonly Host host;
    private readonly Greeting demo;
    private readonly IJSInProcessObjectReference module;
    private readonly DotNetObjectReference<BrowserTestDriver> reference;
    private readonly IJSInProcessObjectReference bridge;
    private bool disposed;

    private BrowserTestDriver(Host host, Greeting demo, IJSInProcessObjectReference module, IJSInProcessObjectReference bridge)
    {
        this.host = host;
        this.demo = demo;
        this.module = module;
        this.bridge = bridge;
        reference = DotNetObjectReference.Create(this);
        bridge.InvokeVoid("install", reference);
    }

    internal static async Task<BrowserTestDriver?> Install(IJSRuntime js, IJSInProcessObjectReference module, Host host, Greeting demo)
    {
        var bridge = await js.InvokeAsync<IJSInProcessObjectReference>("import", "./test-driver.js");
        if (!bridge.Invoke<bool>("requested")) { bridge.Dispose(); return null; }
        return new BrowserTestDriver(host, demo, module, bridge);
    }

    [JSInvokable]
    public async Task<object> Command(string command, string? value)
    {
        switch (command)
        {
            case "state": break;
            case "entry": demo.Entry = value ?? throw new ArgumentNullException(nameof(value)); break;
            case "count": demo.Count = int.Parse(value!); break;
            case "visible": demo.Input.Visible = bool.Parse(value!); break;
            case "enabled": ((Control)demo.Root.Children[1]).Enabled = bool.Parse(value!); break;
            case "caption": demo.Input.SetCaptionVisible(bool.Parse(value!)); break;
            case "help": demo.Input.Help = value!; break;
            case "detach": host.Detach(); break;
            case "attach": host.Attach(new DomBackend(module, "app", "errors")); break;
            case "dispose": host.Dispose(); return new { disposed = true };
            case "throw": demo.Input.Submitted += Fail; break;
            case "unthrow": demo.Input.Submitted -= Fail; break;
            case "dispatch": await host.DispatchAsync(() => demo.Count++); break;
            default: throw new ArgumentException("Unknown test command.", nameof(command));
        }
        return new { count = demo.Count, entry = demo.Entry, message = demo.Message, attached = host.IsAttached };
    }

    private static void Fail() => throw new InvalidOperationException("Intentional browser callback failure.");

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        try { bridge.InvokeVoid("uninstall"); }
        finally { reference.Dispose(); bridge.Dispose(); }
    }
}
#endif
