using System.Runtime.InteropServices;

namespace Xui;

public enum LabelOverflow : uint { Clip, CharacterEllipsis }
public readonly record struct LabelLayout(bool Wrapping, uint MaximumLines = 0, LabelOverflow Overflow = LabelOverflow.Clip);

public sealed partial class Label
{
    public static bool SupportsTextLayout => Native.LabelLayoutAvailable.Value;

    public void SetTextLayout(LabelLayout? layout)
    {
        Window.Guard();
        if (!SupportsTextLayout) throw new NotSupportedException("The native runtime does not provide explicit label text layout.");
        var value = new Native.LabelLayoutValue { Size = 24, Version = 0x10000 };
        if (layout is { } setting)
        {
            if (!Enum.IsDefined(setting.Overflow) || setting.MaximumLines > 32 ||
                (setting.Wrapping && setting.Overflow != LabelOverflow.Clip))
                throw new ArgumentOutOfRangeException(nameof(layout));
            value.Mode = setting.Wrapping ? 2u : 1u;
            value.Overflow = (uint)setting.Overflow;
            value.MaximumLines = setting.Wrapping ? setting.MaximumLines : 0;
        }
        Window.Check(Native.LabelLayoutSet(Handle, in value));
    }

    public LabelLayout? GetTextLayout()
    {
        Window.Guard();
        if (!SupportsTextLayout) throw new NotSupportedException("The native runtime does not provide explicit label text layout.");
        var value = new Native.LabelLayoutValue { Size = 24, Version = 0x10000 };
        Window.Check(Native.LabelLayoutGet(Handle, ref value));
        if (value.Size != 24 || value.Version != 0x10000 || value.Reserved != 0 || value.Mode > 2 ||
            value.Overflow > 1 || value.MaximumLines > 32 ||
            (value.Mode is 0 or 1 && value.MaximumLines != 0) || (value.Mode is 0 or 2 && value.Overflow != 0))
            throw new InvalidOperationException("Native label layout returned an invalid state.");
        return value.Mode == 0 ? null : new(value.Mode == 2, value.Mode == 2 ? value.MaximumLines : 1, (LabelOverflow)value.Overflow);
    }
}

internal static partial class Native
{
    internal static readonly Lazy<bool> LabelLayoutAvailable = new(() =>
        HasExports("xui_label_layout_version", "xui_label_layout_set", "xui_label_layout_get") &&
        LabelLayoutVersion() == 0x10000);
    [StructLayout(LayoutKind.Sequential)]
    internal struct LabelLayoutValue { internal uint Size, Version, Mode, Overflow, MaximumLines, Reserved; }
    [LibraryImport("xui", EntryPoint = "xui_label_layout_version")] internal static partial uint LabelLayoutVersion();
    [LibraryImport("xui", EntryPoint = "xui_label_layout_set")] internal static partial int LabelLayoutSet(ulong label, in LabelLayoutValue value);
    [LibraryImport("xui", EntryPoint = "xui_label_layout_get")] internal static partial int LabelLayoutGet(ulong label, ref LabelLayoutValue value);
}
