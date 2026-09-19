using System.Runtime.InteropServices;

namespace Xui;

public sealed unsafe partial class Window
{
    public Window SetPresentation(string fontFamily, float fontSize, bool smoothScrolling = true, bool animations = true)
    {
        Guard();
        if (!float.IsFinite(fontSize) || fontSize is < 8 or > 32)
            throw new ArgumentOutOfRangeException(nameof(fontSize));
        if (string.IsNullOrEmpty(fontFamily) || fontFamily.Length > 128)
            throw new ArgumentOutOfRangeException(nameof(fontFamily));
        var bytes = Utf8(fontFamily);
        fixed (byte* p = bytes)
            Check(Native.WindowSetPresentation(Handle, Span(p, bytes), fontSize,
                smoothScrolling ? 1u : 0u, animations ? 1u : 0u));
        return this;
    }
}

public abstract partial class Element
{
    public Element SetPresentationFontSize(float? fontSize)
    {
        Window.Guard();
        if (fontSize is { } value && (!float.IsFinite(value) || value is < 8 or > 32))
            throw new ArgumentOutOfRangeException(nameof(fontSize));
        Window.Check(Native.ControlPresentationFontSize(Handle, fontSize ?? 0));
        return this;
    }
}

public abstract partial class Control
{
    public Control SetPresentation(bool singleClick = false, bool thumbnailFill = false)
    {
        Window.Guard();
        Window.Check(Native.ControlSetPresentation(Handle, singleClick ? 1u : 0u, thumbnailFill ? 1u : 0u));
        return this;
    }
}

internal static partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_window_set_presentation")]
    internal static partial int WindowSetPresentation(ulong window, Text fontFamily, float fontSize,
        uint smoothScrolling, uint animations);
    [LibraryImport("xui", EntryPoint = "xui_control_set_presentation")]
    internal static partial int ControlSetPresentation(ulong control, uint singleClick, uint thumbnailFill);
    [LibraryImport("xui", EntryPoint = "xui_control_presentation_font_size")]
    internal static partial int ControlPresentationFontSize(ulong control, float fontSize);
}
