using System.Runtime.InteropServices;

namespace Xui;

/// <summary>Windows 11 system material behind unpainted client pixels.</summary>
public enum WindowBackdrop : uint { None, Mica, MicaAlt, Acrylic }

/// <summary>The material painted by a <see cref="Stack"/> surface.</summary>
public enum SurfaceMaterial : uint { Solid, Acrylic }

public sealed partial class Window
{
    /// <summary>Requests a system backdrop before or while the window is open. Incompatible with
    /// <see cref="SetTransparent"/>. Unsupported systems, disabled transparency effects, and high
    /// contrast keep the solid theme background; read <see cref="BackdropActive"/>.</summary>
    public Window SetBackdrop(WindowBackdrop backdrop)
    {
        Guard();
        if (!Enum.IsDefined(backdrop)) throw new ArgumentOutOfRangeException(nameof(backdrop));
        Check(Native.WindowBackdropSet(Handle, (uint)backdrop));
        return this;
    }

    public WindowBackdrop Backdrop
    {
        get { Guard(); Check(Native.WindowBackdropGet(Handle, out var value, out _)); return (WindowBackdrop)value; }
        set => SetBackdrop(value);
    }

    /// <summary>True only while Windows presents the requested material.</summary>
    public bool BackdropActive
    {
        get { Guard(); Check(Native.WindowBackdropGet(Handle, out _, out var active)); return active != 0; }
    }
}

internal static partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_window_backdrop_set")]
    internal static partial int WindowBackdropSet(ulong window, uint backdrop);
    [LibraryImport("xui", EntryPoint = "xui_window_backdrop_get")]
    internal static partial int WindowBackdropGet(ulong window, out uint backdrop, out uint active);
    [LibraryImport("xui", EntryPoint = "xui_stack_material")]
    internal static partial int StackMaterial(ulong stack, uint material);
}
