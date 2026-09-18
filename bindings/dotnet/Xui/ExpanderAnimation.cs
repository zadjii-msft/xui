using System.Runtime.InteropServices;

namespace Xui;

public sealed partial class Expander
{
    public uint Duration
    {
        get { Window.Guard(); Window.Check(Native.ExpanderDurationGet(Handle, out uint value)); return value; }
        set { Window.Guard(); Window.Check(Native.ExpanderDurationSet(Handle, value)); }
    }
    public float Progress
    {
        get { Window.Guard(); Window.Check(Native.ExpanderProgress(Handle, out float value)); return value; }
    }
    public bool Animating
    {
        get { Window.Guard(); Window.Check(Native.ExpanderAnimating(Handle, out uint value)); return value != 0; }
    }
    public Expander SetDuration(uint milliseconds) { Duration = milliseconds; return this; }
}

internal static partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_expander_set_duration")]
    internal static partial int ExpanderDurationSet(ulong target, uint milliseconds);
    [LibraryImport("xui", EntryPoint = "xui_expander_get_duration")]
    internal static partial int ExpanderDurationGet(ulong target, out uint milliseconds);
    [LibraryImport("xui", EntryPoint = "xui_expander_get_progress")]
    internal static partial int ExpanderProgress(ulong target, out float progress);
    [LibraryImport("xui", EntryPoint = "xui_expander_get_animating")]
    internal static partial int ExpanderAnimating(ulong target, out uint animating);
}
