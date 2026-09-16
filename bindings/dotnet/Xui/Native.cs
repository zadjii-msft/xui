using System.Runtime.InteropServices;

namespace Xui;

internal static unsafe partial class Native
{
    internal const uint Version = 0x10000;
    [StructLayout(LayoutKind.Sequential)]
    internal struct Text { internal byte* Data; internal uint Length, Reserved; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Options
    {
        internal uint Size, Version;
        internal Text Title;
        internal float Width, Height;
        internal uint Theme, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Property
    {
        internal uint Size, Kind;
        internal ulong Target;
        internal Text Text;
        internal float A, B, C, D;
        internal ulong Integer;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Event { internal uint Size, Kind; internal ulong Source, Value; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Item
    {
        internal uint Size, Directory;
        internal ulong Id;
        internal Text Name, Path;
    }
    [LibraryImport("xui", EntryPoint = "xui_abi_version")]
    internal static partial uint VersionGet();
    [LibraryImport("xui", EntryPoint = "xui_error_copy")]
    internal static partial int ErrorCopy(byte* buffer, uint capacity, out uint required, out int error);
    [LibraryImport("xui", EntryPoint = "xui_window_create")]
    internal static partial int WindowCreate(in Options options, out ulong result);
    [LibraryImport("xui", EntryPoint = "xui_window_destroy")]
    internal static partial int WindowDestroy(ulong window);
    [LibraryImport("xui", EntryPoint = "xui_window_run")]
    internal static partial int Run(ulong window);
    [LibraryImport("xui", EntryPoint = "xui_window_close")]
    internal static partial int Close(ulong window);
    [LibraryImport("xui", EntryPoint = "xui_window_callback_error")]
    internal static partial int CallbackError(ulong window, out int status);
    [LibraryImport("xui", EntryPoint = "xui_create")]
    internal static partial int Create(ulong window, uint kind, Text name, ulong content, out ulong result);
    [LibraryImport("xui", EntryPoint = "xui_stack_create")]
    internal static partial int StackCreate(ulong window, uint axis, out ulong result);
    [LibraryImport("xui", EntryPoint = "xui_stack_add")]
    internal static partial int StackAdd(ulong stack, ulong child, float flex);
    [LibraryImport("xui", EntryPoint = "xui_window_content")]
    internal static partial int Content(ulong window, ulong stack);
    [LibraryImport("xui", EntryPoint = "xui_update")]
    internal static partial int Update(ulong window, Property* properties, uint count);
    [LibraryImport("xui", EntryPoint = "xui_subscribe")]
    internal static partial int Subscribe(ulong target, delegate* unmanaged[Cdecl]<nint, Event*, int> callback, nint context);
    [LibraryImport("xui", EntryPoint = "xui_text_copy")]
    internal static partial int TextCopy(ulong target, byte* buffer, uint capacity, out uint required);
    [LibraryImport("xui", EntryPoint = "xui_focus")]
    internal static partial int Focus(ulong target, uint selectAll);
    [LibraryImport("xui", EntryPoint = "xui_invoke")]
    internal static partial int Invoke(ulong target);
    [LibraryImport("xui", EntryPoint = "xui_image_source")]
    internal static partial int ImageSource(ulong image, Text path, uint width, uint height);
    [LibraryImport("xui", EntryPoint = "xui_image_shell_source")]
    internal static partial int ImageShellSource(ulong image, Text path, uint width, uint height);
    [LibraryImport("xui", EntryPoint = "xui_image_state")]
    internal static partial int ImageState(ulong image, out uint state);
    [LibraryImport("xui", EntryPoint = "xui_list_items")]
    internal static partial int ListItems(ulong list, Item* items, uint count);
    [LibraryImport("xui", EntryPoint = "xui_list_filter")]
    internal static partial int ListFilter(ulong list, Text query);
    [LibraryImport("xui", EntryPoint = "xui_list_select")]
    internal static partial int ListSelect(ulong list, uint index);
    [LibraryImport("xui", EntryPoint = "xui_list_state")]
    internal static partial int ListState(ulong list, out uint count, out ulong id, out uint hasSelection);
}
