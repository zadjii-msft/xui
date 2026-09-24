using System.Runtime.InteropServices;

namespace Xui;

public enum RevealLayout : uint { Fixed = 0, Expand = 1 }
public enum RevealDirection : uint { Bottom = 0, Top = 1, Left = 2, Right = 3 }

public sealed partial class Reveal : Control
{
    internal Reveal(Window window, ulong handle) : base(window, handle) { }

    public bool Open
    {
        get { Window.Guard(); Window.Check(Native.RevealGetOpen(Handle, out var value)); return value != 0; }
        set { Window.Guard(); Window.Check(Native.RevealSetOpen(Handle, value ? 1u : 0u)); }
    }
    public uint Duration
    {
        get { Window.Guard(); Window.Check(Native.RevealGetDuration(Handle, out var value)); return value; }
        set { Window.Guard(); Window.Check(Native.RevealSetDuration(Handle, value)); }
    }
    public RevealLayout Layout
    {
        get { Window.Guard(); Window.Check(Native.RevealGetLayout(Handle, out var value)); return (RevealLayout)value; }
        set { Window.Guard(); Window.Check(Native.RevealSetLayout(Handle, (uint)value)); }
    }
    public RevealDirection Direction
    {
        get { Window.Guard(); Window.Check(Native.RevealGetDirection(Handle, out var value)); return (RevealDirection)value; }
        set { Window.Guard(); Window.Check(Native.RevealSetDirection(Handle, (uint)value)); }
    }
    public float Progress
    {
        get { Window.Guard(); Window.Check(Native.RevealGetProgress(Handle, out var value)); return value; }
    }
    public bool Animating
    {
        get { Window.Guard(); Window.Check(Native.RevealGetAnimating(Handle, out var value)); return value != 0; }
    }
    public Reveal SetOpen(bool value) { Open = value; return this; }
    public Reveal SetDuration(uint milliseconds) { Duration = milliseconds; return this; }
    public Reveal SetLayout(RevealLayout value) { Layout = value; return this; }
    public Reveal SetDirection(RevealDirection value) { Direction = value; return this; }
}

public sealed unsafe partial class Window
{
    public Reveal Reveal(Element content, string name = "Reveal")
    {
        ArgumentNullException.ThrowIfNull(content);
        Guard();
        content.BelongsTo(this);
        var bytes = Utf8(name);
        fixed (byte* p = bytes)
        {
            Check(Native.RevealCreate(Handle, content.Handle, Span(p, bytes), out var handle));
            return new(this, handle);
        }
    }
}

internal static unsafe partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_reveal_create")]
    internal static partial int RevealCreate(ulong window, ulong content, Text name, out ulong result);
    [LibraryImport("xui", EntryPoint = "xui_reveal_set_open")]
    internal static partial int RevealSetOpen(ulong target, uint open);
    [LibraryImport("xui", EntryPoint = "xui_reveal_get_open")]
    internal static partial int RevealGetOpen(ulong target, out uint open);
    [LibraryImport("xui", EntryPoint = "xui_reveal_set_duration")]
    internal static partial int RevealSetDuration(ulong target, uint milliseconds);
    [LibraryImport("xui", EntryPoint = "xui_reveal_get_duration")]
    internal static partial int RevealGetDuration(ulong target, out uint milliseconds);
    [LibraryImport("xui", EntryPoint = "xui_reveal_set_layout")]
    internal static partial int RevealSetLayout(ulong target, uint layout);
    [LibraryImport("xui", EntryPoint = "xui_reveal_get_layout")]
    internal static partial int RevealGetLayout(ulong target, out uint layout);
    [LibraryImport("xui", EntryPoint = "xui_reveal_set_direction")]
    internal static partial int RevealSetDirection(ulong target, uint direction);
    [LibraryImport("xui", EntryPoint = "xui_reveal_get_direction")]
    internal static partial int RevealGetDirection(ulong target, out uint direction);
    [LibraryImport("xui", EntryPoint = "xui_reveal_get_progress")]
    internal static partial int RevealGetProgress(ulong target, out float progress);
    [LibraryImport("xui", EntryPoint = "xui_reveal_get_animating")]
    internal static partial int RevealGetAnimating(ulong target, out uint animating);
}
