#if DEBUG
using Microsoft.JSInterop;
using Xui.Experimental.Portable;
using Xui.Experimental.Web;

public static class BrowserImageProbe
{
    internal static Host? Host;
    internal static DomBackend? Backend;
    [JSInvokable]
    public static object ImageState() => new { attached = Host?.IsAttached ?? false, resources = Backend?.ImageStatistics };
}
#endif
