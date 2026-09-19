using Microsoft.JSInterop;

namespace Xui.Experimental.Web;

/// <summary>Releases an application's host and interop handles when its page leaves the browser.</summary>
public sealed class BrowserLifetime : IDisposable
{
    private readonly Action disposeApplication;
    private readonly DotNetObjectReference<BrowserLifetime> reference;
    private readonly IJSInProcessObjectReference listener;
    private bool disposed;

    public BrowserLifetime(IJSInProcessObjectReference module, string errorId, Action disposeApplication)
    {
        ArgumentNullException.ThrowIfNull(module);
        this.disposeApplication = disposeApplication ?? throw new ArgumentNullException(nameof(disposeApplication));
        reference = DotNetObjectReference.Create(this);
        try { listener = module.Invoke<IJSInProcessObjectReference>("bindLifetime", reference, errorId); }
        catch { reference.Dispose(); throw; }
    }

    [JSInvokable]
    public void PageHide() => Dispose();

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        try { listener.InvokeVoid("dispose"); }
        finally
        {
            try { disposeApplication(); }
            finally { reference.Dispose(); listener.Dispose(); }
        }
    }
}
