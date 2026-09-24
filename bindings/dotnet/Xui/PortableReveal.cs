using System.Runtime.InteropServices;

namespace Xui;

public readonly record struct PortableRevealState(bool Open, uint DurationMilliseconds, RevealDirection Direction);
public readonly record struct PortableRevealPresentation(float Progress, bool Animating);

public sealed partial class Reveal
{
    public static bool SupportsPortableState => Native.PortableRevealAvailable.Value;

    private static Native.PortableRevealStateValue Encode(PortableRevealState state, bool initial)
    {
        if (state.DurationMilliseconds > 400) throw new ArgumentOutOfRangeException(nameof(state));
        uint direction = state.Direction switch
        {
            RevealDirection.Bottom => 0,
            RevealDirection.Right => 1,
            _ => throw new NotSupportedException("Portable Reveal supports Bottom and Right only.")
        };
        return new() { Size = 24, Version = 0x10000, Open = state.Open ? 1u : 0u,
            Duration = state.DurationMilliseconds, Direction = direction, Initial = initial ? 1u : 0u };
    }
    private void RequirePortable()
    {
        Window.Guard();
        if (!SupportsPortableState)
            throw new NotSupportedException("The native runtime does not provide portable Reveal focus and accessibility semantics.");
    }
    public void ValidatePortableState(PortableRevealState state, bool initial = false)
    {
        RequirePortable();
        var value = Encode(state, initial);
        Window.Check(Native.PortableRevealValidateState(Handle, in value));
    }
    public bool CanSetPortableOpen(bool open)
    {
        RequirePortable();
        Window.Check(Native.PortableRevealCanSetOpen(Handle, open ? 1u : 0u, out uint allowed));
        if (allowed > 1) throw new InvalidOperationException("Native Reveal returned an invalid close preflight result.");
        return allowed != 0;
    }
    public void ApplyPortableState(PortableRevealState state, bool initial = false)
    {
        RequirePortable();
        var value = Encode(state, initial);
        Window.Check(Native.PortableRevealApplyState(Handle, in value));
    }
    public PortableRevealPresentation PortablePresentation
    {
        get
        {
            RequirePortable();
            var value = new Native.PortableRevealPresentationValue { Size = 16, Version = 0x10000 };
            Window.Check(Native.PortableRevealGetPresentation(Handle, ref value));
            if (value.Size != 16 || value.Version != 0x10000 || !float.IsFinite(value.Progress) ||
                value.Progress is < 0 or > 1 || value.Animating > 1)
                throw new InvalidOperationException("Native Reveal returned invalid presentation state.");
            return new(value.Progress, value.Animating != 0);
        }
    }
    public void CancelPortableMotion()
    {
        RequirePortable();
        Window.Check(Native.PortableRevealCancel(Handle));
    }
}

internal static partial class Native
{
    internal static readonly Lazy<bool> PortableRevealAvailable = new(() =>
        HasExports("xui_portable_reveal_version", "xui_portable_reveal_validate_state", "xui_portable_reveal_can_set_open",
            "xui_portable_reveal_apply_state", "xui_portable_reveal_get_presentation", "xui_portable_reveal_cancel") &&
        PortableRevealVersion() == 0x10000);
    [StructLayout(LayoutKind.Sequential)]
    internal struct PortableRevealStateValue { internal uint Size, Version, Open, Duration, Direction, Initial; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct PortableRevealPresentationValue { internal uint Size, Version; internal float Progress; internal uint Animating; }
    [LibraryImport("xui", EntryPoint = "xui_portable_reveal_version")] internal static partial uint PortableRevealVersion();
    [LibraryImport("xui", EntryPoint = "xui_portable_reveal_validate_state")] internal static partial int PortableRevealValidateState(ulong reveal, in PortableRevealStateValue state);
    [LibraryImport("xui", EntryPoint = "xui_portable_reveal_can_set_open")] internal static partial int PortableRevealCanSetOpen(ulong reveal, uint open, out uint allowed);
    [LibraryImport("xui", EntryPoint = "xui_portable_reveal_apply_state")] internal static partial int PortableRevealApplyState(ulong reveal, in PortableRevealStateValue state);
    [LibraryImport("xui", EntryPoint = "xui_portable_reveal_get_presentation")] internal static partial int PortableRevealGetPresentation(ulong reveal, ref PortableRevealPresentationValue value);
    [LibraryImport("xui", EntryPoint = "xui_portable_reveal_cancel")] internal static partial int PortableRevealCancel(ulong reveal);
}
