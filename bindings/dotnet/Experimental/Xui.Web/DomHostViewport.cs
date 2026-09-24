using Microsoft.JSInterop;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Web;

internal sealed class DomHostViewport : IDisposable
{
    private readonly IJSInProcessObjectReference native;
    private readonly DotNetObjectReference<DomHostViewport> reference;
    private Action<Size>? changed;
    private Action<DomHostViewport>? retired;
    private bool disposed;

    internal DomHostViewport(IJSInProcessObjectReference surface, Action<Size> changed, Action<DomHostViewport> retired)
    {
        this.changed = changed;
        this.retired = retired;
        reference = DotNetObjectReference.Create(this);
        try { native = surface.Invoke<IJSInProcessObjectReference>("observeViewport", reference); }
        catch { reference.Dispose(); throw; }
        try { changed(surface.Invoke<Size>("viewport")); }
        catch
        {
            Dispose();
            throw;
        }
    }
    [JSInvokable]
    public void Changed(float width, float height)
    {
        if (!disposed) changed?.Invoke(new(width, height));
    }
    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        changed = null;
        retired?.Invoke(this);
        retired = null;
        List<Exception> errors = [];
        foreach (var action in new Action[] { () => native.InvokeVoid("dispose"), reference.Dispose, native.Dispose })
        {
            try { action(); }
            catch (Exception error) { errors.Add(error); }
        }
        if (errors.Count != 0) throw new AggregateException("Viewport observer cleanup failed.", errors);
    }
}
