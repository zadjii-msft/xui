using System.Runtime.InteropServices;

namespace Xui;

/// <summary>Optional 0xRRGGBB tab colors. Null uses the theme. High contrast ignores overrides.</summary>
public readonly record struct TabColors(uint? RowBackground = null, uint? SelectedBackground = null,
    uint? SelectedText = null, uint? InactiveBackground = null, uint? InactiveText = null,
    uint? HoverBackground = null, uint? Border = null);

public sealed partial class TabStrip
{
    public TabColors Colors
    {
        get
        {
            Window.Guard();
            var value = Native.TabColorRecord.Empty;
            Window.Check(Native.TabGetColors(Handle, ref value));
            uint? Read(uint bit, uint color) => (value.Mask & bit) != 0 ? color : null;
            return new(Read(1, value.RowBackground), Read(2, value.SelectedBackground), Read(4, value.SelectedText),
                Read(8, value.InactiveBackground), Read(16, value.InactiveText),
                Read(32, value.HoverBackground), Read(64, value.Border));
        }
        set => SetColors(value);
    }

    public TabStrip SetColors(TabColors colors)
    {
        Window.Guard();
        var value = Native.TabColorRecord.Empty;
        uint Write(uint bit, uint? color)
        {
            if (color is > 0xffffff) throw new ArgumentOutOfRangeException(nameof(colors), "Tab colors must be 0xRRGGBB values.");
            if (color.HasValue) value.Mask |= bit;
            return color.GetValueOrDefault();
        }
        value.RowBackground = Write(1, colors.RowBackground);
        value.SelectedBackground = Write(2, colors.SelectedBackground);
        value.SelectedText = Write(4, colors.SelectedText);
        value.InactiveBackground = Write(8, colors.InactiveBackground);
        value.InactiveText = Write(16, colors.InactiveText);
        value.HoverBackground = Write(32, colors.HoverBackground);
        value.Border = Write(64, colors.Border);
        Window.Check(Native.TabSetColors(Handle, in value));
        return this;
    }
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct TabColorRecord
    {
        internal uint Size, Version, Mask;
        internal uint RowBackground, SelectedBackground, SelectedText;
        internal uint InactiveBackground, InactiveText, HoverBackground, Border;
        internal static TabColorRecord Empty => new() { Size = 40, Version = 0x10000 };
    }

    [LibraryImport("xui", EntryPoint = "xui_tab_set_colors")]
    internal static partial int TabSetColors(ulong target, in TabColorRecord colors);
    [LibraryImport("xui", EntryPoint = "xui_tab_get_colors")]
    internal static partial int TabGetColors(ulong target, ref TabColorRecord colors);
}
