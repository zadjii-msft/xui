using System.Runtime.InteropServices;

namespace Xui;

public sealed partial class NavigationView
{
    public uint Duration
    {
        get { Window.Guard(); Window.Check(Native.NavigationDurationGet(Handle, out uint value)); return value; }
        set { Window.Guard(); Window.Check(Native.NavigationDurationSet(Handle, value)); }
    }
    public bool Animating
    {
        get { Window.Guard(); Window.Check(Native.NavigationAnimating(Handle, out uint value)); return value != 0; }
    }
    public NavigationView SetDuration(uint milliseconds) { Duration = milliseconds; return this; }
}

internal static partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_navigation_view_set_duration")]
    internal static partial int NavigationDurationSet(ulong target, uint milliseconds);
    [LibraryImport("xui", EntryPoint = "xui_navigation_view_get_duration")]
    internal static partial int NavigationDurationGet(ulong target, out uint milliseconds);
    [LibraryImport("xui", EntryPoint = "xui_navigation_view_get_animating")]
    internal static partial int NavigationAnimating(ulong target, out uint animating);
}
