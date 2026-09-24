using System.Runtime.InteropServices;

namespace Xui;

public sealed partial class Grid
{
    /// <summary>Whether the loaded runtime provides the portable Grid layout contract and Grid axis overrides.</summary>
    public static bool SupportsPortableLayout => Native.GridLayoutAvailable.Value;
}

internal static partial class Native
{
    internal static readonly Lazy<bool> GridLayoutAvailable = new(() =>
        HasExports("xui_grid_layout_version", "xui_element_set_axis_constraints", "xui_element_get_axis_constraints",
            "xui_scroll_view_set_fill_viewport", "xui_scroll_view_get_fill_viewport") &&
        GridLayoutVersion() == 0x00010000);

    [LibraryImport("xui", EntryPoint = "xui_grid_layout_version")]
    internal static partial uint GridLayoutVersion();
}
