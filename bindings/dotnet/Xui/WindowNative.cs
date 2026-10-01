using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Xui;

public sealed unsafe partial class Window
{
    private GCHandle nativeCreatedRoot;
    private Action<nint>? nativeCreated;

    /// <summary>Borrowed HWND. Available after Application.Show while open; never destroy or subclass it.</summary>
    public nint NativeHwnd
    {
        get { Guard(); Check(Native.WindowNativeHandle(Handle, out var hwnd)); return hwnd; }
    }

    /// <summary>Opt into a borderless, per-pixel-alpha window before Show or Run.</summary>
    public Window SetTransparent(bool enabled = true)
    {
        Guard();
        Check(Native.WindowTransparent(Handle, enabled ? 1u : 0u));
        return this;
    }

    /// <summary>Marks a rectangle in client DIPs as a caption drag area. Configure before Show or Run.</summary>
    public Window SetDragRegion(float x, float y, float width, float height)
    {
        Guard();
        Check(Native.WindowDragRegion(Handle, x, y, width, height));
        return this;
    }

    /// <summary>Runs on the UI thread after HWND creation but before the window is shown.
    /// Only configure Win32 here; XUI content is not yet open.</summary>
    public Action<nint>? NativeHwndCreated
    {
        get { Guard(); return nativeCreated; }
        set
        {
            Guard();
            bool allocated = false;
            if (value is not null && !nativeCreatedRoot.IsAllocated)
            {
                nativeCreatedRoot = GCHandle.Alloc(this, GCHandleType.Weak);
                allocated = true;
            }
            try
            {
                Check(Native.WindowNativeCreated(Handle, value is null ? null : &NativeCreatedTrampoline,
                    value is null ? 0 : GCHandle.ToIntPtr(nativeCreatedRoot)));
                nativeCreated = value;
                if (value is null) ReleaseNativeCreatedCallback();
            }
            catch
            {
                if (allocated) ReleaseNativeCreatedCallback();
                throw;
            }
        }
    }

    private void ReleaseNativeCreatedCallback()
    {
        nativeCreated = null;
        if (nativeCreatedRoot.IsAllocated) nativeCreatedRoot.Free();
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int NativeCreatedTrampoline(nint context, Native.Event* value)
    {
        Window? window = null;
        try
        {
            window = GCHandle.FromIntPtr(context).Target as Window;
            if (window is null) return 8;
            ++window.callbacks;
            try { window.nativeCreated?.Invoke((nint)value->Value); }
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
