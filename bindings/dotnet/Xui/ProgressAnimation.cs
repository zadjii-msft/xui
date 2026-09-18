using System.Runtime.InteropServices;

namespace Xui;

public sealed partial class Progress
{
    public uint Duration
    {
        get { Window.Guard(); Window.Check(Native.ProgressDurationGet(Handle, out uint value)); return value; }
        set { Window.Guard(); Window.Check(Native.ProgressDurationSet(Handle, value)); }
    }
    public double PresentedValue
    {
        get { Window.Guard(); Window.Check(Native.ProgressPresentedValue(Handle, out double value)); return value; }
    }
    public bool Animating
    {
        get { Window.Guard(); Window.Check(Native.ProgressAnimating(Handle, out uint value)); return value != 0; }
    }
    public Progress SetDuration(uint milliseconds) { Duration = milliseconds; return this; }
}

internal static partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_progress_set_duration")]
    internal static partial int ProgressDurationSet(ulong target, uint milliseconds);
    [LibraryImport("xui", EntryPoint = "xui_progress_get_duration")]
    internal static partial int ProgressDurationGet(ulong target, out uint milliseconds);
    [LibraryImport("xui", EntryPoint = "xui_progress_get_presented_value")]
    internal static partial int ProgressPresentedValue(ulong target, out double value);
    [LibraryImport("xui", EntryPoint = "xui_progress_get_animating")]
    internal static partial int ProgressAnimating(ulong target, out uint animating);
}
