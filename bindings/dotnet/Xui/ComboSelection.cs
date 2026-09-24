using System.Runtime.InteropServices;

namespace Xui;

public sealed partial class ComboBox
{
    public static bool SupportsSelectionQuery => Native.ComboSelectionAvailable.Value;
    public ulong? Selected
    {
        get
        {
            Window.Guard();
            if (!SupportsSelectionQuery)
                throw new NotSupportedException("The native runtime does not expose actual ComboBox selection.");
            Window.Check(Native.ComboBoxGetSelected(Handle, out ulong id, out uint has));
            if (has > 1 || (has == 0 && id != 0) || (has != 0 && (id == 0 || id > (ulong)long.MaxValue - 100)))
                throw new InvalidOperationException("Native ComboBox returned an invalid selection.");
            return has == 0 ? null : id;
        }
    }
}

internal static partial class Native
{
    internal static readonly Lazy<bool> ComboSelectionAvailable = new(() =>
        HasExports("xui_combo_box_selection_version", "xui_combo_box_get_selected") &&
        ComboBoxSelectionVersion() == 0x10000);
    [LibraryImport("xui", EntryPoint = "xui_combo_box_selection_version")]
    internal static partial uint ComboBoxSelectionVersion();
    [LibraryImport("xui", EntryPoint = "xui_combo_box_get_selected")]
    internal static partial int ComboBoxGetSelected(ulong control, out ulong id, out uint has);
}
