using System.Runtime.InteropServices;

namespace Xui;
internal static unsafe partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct FileDialogFilter { internal Text Name, Pattern; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct FileDialogOptions
    {
        internal uint Size, Version;
        internal Text Title;
        internal FileDialogFilter* Filters;
        internal uint FilterCount, Reserved;
        internal Text DefaultExtension, SuggestedName, InitialDirectory;
    }
    [LibraryImport("xui", EntryPoint = "xui_window_open_file_dialog")]
    internal static partial int WindowOpenFileDialog(ulong window, FileDialogOptions* options,
        delegate* unmanaged[Cdecl]<nint, uint, Text, int> receiver, nint context);
    [LibraryImport("xui", EntryPoint = "xui_window_save_file_dialog")]
    internal static partial int WindowSaveFileDialog(ulong window, FileDialogOptions* options,
        delegate* unmanaged[Cdecl]<nint, uint, Text, int> receiver, nint context);
}
