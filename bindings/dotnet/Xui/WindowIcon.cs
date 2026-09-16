using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Xui;

public sealed unsafe partial class Window
{
    private GCHandle iconErrorRoot;
    private Action<string>? iconErrorHandler;

    /// <summary>Requests a Shell thumbnail or icon for the native window. An empty path clears it.</summary>
    /// <remarks>Replacement and closure cancel delivery. Errors use IconErrorHandler on the UI thread.</remarks>
    public Window SetIconSource(string path)
    {
        Guard();
        ArgumentNullException.ThrowIfNull(path);
        if (path.Length > 32767) throw new ArgumentOutOfRangeException(nameof(path));
        using var pins = new Pins();
        Check(Native.WindowSetIconSource(Handle, pins.Text(path)));
        return this;
    }

    public Action<string>? IconErrorHandler
    {
        get { Guard(); return iconErrorHandler; }
        set
        {
            Guard();
            if (value is null)
            {
                Check(Native.WindowOnIconError(Handle, null, 0));
                ReleaseIconCallback();
                return;
            }
            bool allocated = !iconErrorRoot.IsAllocated;
            if (allocated) iconErrorRoot = GCHandle.Alloc(this, GCHandleType.Weak);
            try
            {
                Check(Native.WindowOnIconError(Handle, &IconErrorTrampoline, GCHandle.ToIntPtr(iconErrorRoot)));
                iconErrorHandler = value;
            }
            catch
            {
                if (allocated) iconErrorRoot.Free();
                throw;
            }
        }
    }

    private void ReleaseIconCallback()
    {
        if (iconErrorRoot.IsAllocated) iconErrorRoot.Free();
        iconErrorHandler = null;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int IconErrorTrampoline(nint context, byte* text, uint length)
    {
        Window? window = null;
        try
        {
            window = GCHandle.FromIntPtr(context).Target as Window;
            if (window is null) return 8;
            ++window.callbacks;
            try { window.iconErrorHandler?.Invoke(Encoding.GetString(text, checked((int)length))); }
            finally { --window.callbacks; }
            return 0;
        }
        catch (Exception error) { if (window is not null) window.callbackError = error; return 8; }
    }
}
