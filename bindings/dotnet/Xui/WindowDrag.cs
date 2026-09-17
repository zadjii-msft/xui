using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Xui;

/// <summary>Normal outer window bounds in physical screen pixels, including negative monitor coordinates.</summary>
public readonly record struct WindowPlacement(int X, int Y, int Width, int Height, bool Maximized);
public enum TabDragKind : uint { Reorder = 0, TearOut = 1, Drop = 2, Cancel = 3, Completed = 4, QueryDrop = 5 }
/// <summary>A source-window request. Index is the target insertion slot before removal. Strips are 0 or 1.</summary>
public readonly record struct TabDragEvent(TabDragKind Kind, uint SourceStrip, ulong TabId,
    Window? Target, uint TargetStrip, int Index);

public sealed unsafe partial class Window
{
    private GCHandle tabDragRoot;
    private Func<TabDragEvent, bool>? tabDragHandler;

    /// <summary>Gets or sets normal outer bounds and maximized state. Set also works before Show.</summary>
    /// <remarks>
    /// Get requires an open window or a previously set placement. An unknown initial placement throws XuiException.
    /// Set accepts X/Y in [-1000000,1000000] and Width/Height in [1,65536].
    /// </remarks>
    public WindowPlacement Placement
    {
        get
        {
            Guard();
            var value = new Native.WindowPlacement { Size = (uint)sizeof(Native.WindowPlacement) };
            Check(Native.WindowGetPlacement(Handle, ref value));
            if (value.Size != sizeof(Native.WindowPlacement) || value.Maximized > 1)
                throw new XuiException(5, "The native window placement record is invalid.");
            return new(value.X, value.Y, value.Width, value.Height, value.Maximized != 0);
        }
        set
        {
            Guard();
            var native = new Native.WindowPlacement
            {
                Size = (uint)sizeof(Native.WindowPlacement), X = value.X, Y = value.Y,
                Width = value.Width, Height = value.Height, Maximized = value.Maximized ? 1u : 0u
            };
            Check(Native.WindowSetPlacement(Handle, in native));
        }
    }

    /// <summary>Opts an Application window with a custom title bar into tab dragging. Return true to accept a request.</summary>
    /// <remarks>
    /// TearOut must keep the dragged tab ID on the same strip and HWND. Move other models to other windows synchronously.
    /// Reorder always uses the source strip. Drop can target the other strip in the same window at release, before tear-out.
    /// QueryDrop validates the hovered target and index without mutation. Return true to show the insertion indicator for an accepted drop.
    /// Cancel restores application state after Escape. Completed retires state after no target or a rejected drop.
    /// Exceptions close the source window. Native controls cannot move between windows.
    /// Replacement and removal fail during an active gesture. The installed handler remains unchanged.
    /// </remarks>
    public Func<TabDragEvent, bool>? TabDragHandler
    {
        get { Guard(); return tabDragHandler; }
        set
        {
            GuardWindowCallback();
            if (value is null)
            {
                Check(Native.WindowTabDragHandler(Handle, null, 0));
                ReleaseTabDragCallback();
                return;
            }
            bool allocated = !tabDragRoot.IsAllocated;
            if (allocated) tabDragRoot = GCHandle.Alloc(this, GCHandleType.Weak);
            try
            {
                Check(Native.WindowTabDragHandler(Handle, &TabDragTrampoline, GCHandle.ToIntPtr(tabDragRoot)));
                tabDragHandler = value;
            }
            catch
            {
                if (allocated) tabDragRoot.Free();
                throw;
            }
        }
    }

    private void ReleaseTabDragCallback()
    {
        if (tabDragRoot.IsAllocated) tabDragRoot.Free();
        tabDragHandler = null;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int TabDragTrampoline(nint context, Native.TabDragEvent* value, uint* accepted)
    {
        Window? window = null;
        try
        {
            if (accepted == null) return 8;
            *accepted = 0;
            window = GCHandle.FromIntPtr(context).Target as Window;
            if (window is null) return 8;
            window.Guard();
            if (value == null || value->Size != sizeof(Native.TabDragEvent))
                throw new XuiException(5, "The tab drag event size does not match.");
            if (value->Kind > (uint)TabDragKind.QueryDrop || value->SourceStrip > 1 || value->TargetStrip > 1 ||
                value->TabId == 0 || value->Index > int.MaxValue)
                throw new XuiException(1, "The tab drag event is invalid.");
            var application = window.Application ?? throw new XuiException(1, "Tab dragging requires an Application.");
            var target = value->Target == 0 ? null : application.ResolveWindow(value->Target);
            ++window.callbacks;
            try
            {
                *accepted = window.tabDragHandler?.Invoke(new((TabDragKind)value->Kind, value->SourceStrip, value->TabId,
                    target, value->TargetStrip, checked((int)value->Index))) == true ? 1u : 0u;
            }
            finally { --window.callbacks; }
            return 0;
        }
        catch (Exception error)
        {
            if (window is not null) window.callbackError = error;
            return 8;
        }
    }
}

internal static unsafe partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct WindowPlacement
    {
        internal uint Size;
        internal int X, Y, Width, Height;
        internal uint Maximized;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct TabDragEvent
    {
        internal uint Size, Kind, SourceStrip, TargetStrip;
        internal ulong TabId, Target, Index;
    }
    [LibraryImport("xui", EntryPoint = "xui_window_get_placement")]
    internal static partial int WindowGetPlacement(ulong window, ref WindowPlacement placement);
    [LibraryImport("xui", EntryPoint = "xui_window_set_placement")]
    internal static partial int WindowSetPlacement(ulong window, in WindowPlacement placement);
    [LibraryImport("xui", EntryPoint = "xui_window_tab_drag_handler")]
    internal static partial int WindowTabDragHandler(ulong window,
        delegate* unmanaged[Cdecl]<nint, TabDragEvent*, uint*, int> callback, nint context);
}
