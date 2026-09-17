using System.Runtime.InteropServices;
namespace Xui;
internal static unsafe partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct PreviewStatus
    {
        internal uint Size, Version;
        internal ulong Generation;
        internal uint State, Reason, Phase;
        internal int HResult;
        internal uint Cleanup, Reserved;
    }
    [LibraryImport("xui", EntryPoint = "xui_shell_preview_create")]
    internal static partial int ShellPreviewCreate(ulong window, Text name, out ulong control);
    [LibraryImport("xui", EntryPoint = "xui_shell_preview_load_local")]
    internal static partial int ShellPreviewLoadLocal(ulong control, Text path, out ulong generation);
    [LibraryImport("xui", EntryPoint = "xui_shell_preview_cancel")]
    internal static partial int ShellPreviewCancel(ulong control, ulong generation);
    [LibraryImport("xui", EntryPoint = "xui_shell_preview_unload")]
    internal static partial int ShellPreviewUnload(ulong control);
    [LibraryImport("xui", EntryPoint = "xui_shell_preview_focus_content")]
    internal static partial int ShellPreviewFocusContent(ulong control, uint reverse);
    [LibraryImport("xui", EntryPoint = "xui_shell_preview_get_status")]
    internal static partial int ShellPreviewGetStatus(ulong control, ref PreviewStatus status);
}
