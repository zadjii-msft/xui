using System.Runtime.InteropServices;
namespace Xui;
internal static unsafe partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_window_set_file_clipboard")]
    internal static partial int WindowSetFileClipboard(ulong window, Text* paths, uint count, uint effect);
    [LibraryImport("xui", EntryPoint = "xui_window_get_file_clipboard")]
    internal static partial int WindowGetFileClipboard(ulong window, delegate* unmanaged[Cdecl]<nint, Text*, uint, uint, int> receiver, nint context);
    [LibraryImport("xui", EntryPoint = "xui_window_set_clipboard_text")]
    internal static partial int WindowSetClipboardText(ulong window, Text text);
    [LibraryImport("xui", EntryPoint = "xui_window_transfer_files")]
    internal static partial int WindowTransferFiles(ulong window, Text* paths, uint count, Text destination, uint effect, uint* completed);
    [LibraryImport("xui", EntryPoint = "xui_window_paste_files")]
    internal static partial int WindowPasteFiles(ulong window, Text destination, uint* result);
    [LibraryImport("xui", EntryPoint = "xui_grid_file_drag_bind")]
    internal static partial int GridFileDragBind(ulong grid, delegate* unmanaged[Cdecl]<nint, Event*, int> callback, nint context);
    [LibraryImport("xui", EntryPoint = "xui_grid_file_drag_paths")]
    internal static partial int GridFileDragPaths(ulong grid, Text* paths, uint count);
    [LibraryImport("xui", EntryPoint = "xui_grid_file_drop_bind")]
    internal static partial int GridFileDropBind(ulong grid, delegate* unmanaged[Cdecl]<nint, ulong, ulong, uint, Text*, uint, uint, uint, uint*, int> callback, nint context);
}
