using System.Runtime.InteropServices;

namespace Xui;

internal static unsafe partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_application_create")]
    internal static partial int ApplicationCreate(out ulong application);
    [LibraryImport("xui", EntryPoint = "xui_application_window_create")]
    internal static partial int ApplicationWindowCreate(ulong application, in Options options, uint customTitlebar, out ulong window);
    [LibraryImport("xui", EntryPoint = "xui_application_show")]
    internal static partial int ApplicationShow(ulong application, ulong window);
    [LibraryImport("xui", EntryPoint = "xui_application_run")]
    internal static partial int ApplicationRun(ulong application);
    [LibraryImport("xui", EntryPoint = "xui_application_shutdown")]
    internal static partial int ApplicationShutdown(ulong application);
    [LibraryImport("xui", EntryPoint = "xui_application_destroy")]
    internal static partial int ApplicationDestroy(ulong application);
    [LibraryImport("xui", EntryPoint = "xui_application_post")]
    internal static partial int ApplicationPost(ulong application, delegate* unmanaged[Cdecl]<nint, uint, int> callback, nint context);
    [LibraryImport("xui", EntryPoint = "xui_window_state")]
    internal static partial int WindowState(ulong window, out uint state);
    [LibraryImport("xui", EntryPoint = "xui_window_closed")]
    internal static partial int WindowClosed(ulong window, delegate* unmanaged[Cdecl]<nint, Event*, int> callback, nint context);
    [LibraryImport("xui", EntryPoint = "xui_window_error")]
    internal static partial int WindowError(ulong window, byte* buffer, uint capacity, out uint required);
}
