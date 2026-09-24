using System.Runtime.InteropServices;

namespace Xui;

public sealed partial class ScrollView
{
    /// <summary>Whether the loaded runtime can preserve unbounded content layout without filling the viewport.</summary>
    public static bool SupportsFillViewport => Native.ScrollFillViewportAvailable.Value;

    /// <summary>Whether content fills the viewport's height. Native Windows defaults to true.</summary>
    /// <remarks>False preserves unbounded vertical measurement and arrangement for content-sized layouts.</remarks>
    public bool FillViewport
    {
        get
        {
            Window.Guard();
            RequireFillViewport();
            Window.Check(Native.ScrollGetFillViewport(Handle, out uint value));
            if (value > 1) throw new InvalidOperationException("The native runtime returned an invalid fill-viewport flag.");
            return value != 0;
        }
        set
        {
            Window.Guard();
            RequireFillViewport();
            Window.Check(Native.ScrollSetFillViewport(Handle, value ? 1u : 0u));
        }
    }

    public ScrollView SetFillViewport(bool value) { FillViewport = value; return this; }

    private static void RequireFillViewport()
    {
        if (!SupportsFillViewport)
            throw new NotSupportedException("This native XUI runtime does not support explicit scroll viewport filling.");
    }
}

internal static partial class Native
{
    internal static readonly Lazy<bool> ScrollFillViewportAvailable = new(() =>
        HasExports("xui_scroll_view_set_fill_viewport", "xui_scroll_view_get_fill_viewport"));

    [LibraryImport("xui", EntryPoint = "xui_scroll_view_set_fill_viewport")]
    internal static partial int ScrollSetFillViewport(ulong target, uint enabled);
    [LibraryImport("xui", EntryPoint = "xui_scroll_view_get_fill_viewport")]
    internal static partial int ScrollGetFillViewport(ulong target, out uint enabled);
}
