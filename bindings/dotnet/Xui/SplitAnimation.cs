using System.Runtime.InteropServices;

namespace Xui;

public sealed partial class SplitView
{
    public uint TransitionDuration
    {
        get { Window.Guard(); Window.Check(Native.SplitDurationGet(Handle, out uint value)); return value; }
        set { Window.Guard(); Window.Check(Native.SplitDurationSet(Handle, value)); }
    }
    public float Progress
    {
        get { Window.Guard(); Window.Check(Native.SplitProgress(Handle, out float value)); return value; }
    }
    public bool Animating
    {
        get { Window.Guard(); Window.Check(Native.SplitAnimating(Handle, out uint value)); return value != 0; }
    }
    public SplitView SetTransitionDuration(uint milliseconds) { TransitionDuration = milliseconds; return this; }
}

internal static partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_split_view_set_transition_duration")]
    internal static partial int SplitDurationSet(ulong target, uint milliseconds);
    [LibraryImport("xui", EntryPoint = "xui_split_view_get_transition_duration")]
    internal static partial int SplitDurationGet(ulong target, out uint milliseconds);
    [LibraryImport("xui", EntryPoint = "xui_split_view_get_progress")]
    internal static partial int SplitProgress(ulong target, out float progress);
    [LibraryImport("xui", EntryPoint = "xui_split_view_get_animating")]
    internal static partial int SplitAnimating(ulong target, out uint animating);
}
