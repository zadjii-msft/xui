using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Xui;

public enum WindowState : uint { Created, Open, Closing, Closed }
public sealed record WindowClosedEventArgs(Exception? Error);

/// <summary>Owns a same-thread group of independent document windows.</summary>
public sealed unsafe class Application : IDisposable
{
    internal ulong Handle { get; private set; }
    private readonly int thread = Environment.CurrentManagedThreadId;
    private readonly HashSet<Window> windows = [];
    private readonly List<Exception> errors = [];
    private bool running;
    private int callbacks;

    public Application()
    {
        Window.CheckStatus(Native.ApplicationCreate(out var handle));
        Handle = handle;
    }

    private void Guard()
    {
        if (thread != Environment.CurrentManagedThreadId) throw new XuiException(4, "Use the application UI thread.");
        ObjectDisposedException.ThrowIf(Handle == 0, this);
    }
    public Window CreateWindow(string title = "XUI", float width = 600, float height = 720,
        Theme theme = Theme.Dark, bool customTitlebar = false, VisualStyle visualStyle = VisualStyle.Classic)
    {
        Guard();
        var window = new Window(this, title, width, height, theme, customTitlebar, visualStyle);
        try { windows.Add(window); }
        catch { window.Dispose(); throw; }
        return window;
    }
    public void Show(Window window)
    {
        Guard(); window.VerifyAccess();
        if (window.Application != this) throw new ArgumentException("The window belongs to another application.", nameof(window));
        Window.CheckStatus(Native.ApplicationShow(Handle, window.Handle));
    }
    public void Run()
    {
        Guard();
        if (running || callbacks != 0) throw new XuiException(7, "An application cannot run recursively.");
        running = true;
        int status;
        try { status = Native.ApplicationRun(Handle); }
        finally { running = false; }
        if (errors.Count != 0)
            throw new XuiException(8, "One or more application windows failed.", new AggregateException(errors));
        Window.CheckStatus(status);
    }
    public void Shutdown() { Guard(); Window.CheckStatus(Native.ApplicationShutdown(Handle)); }
    internal void Forget(Window window) => windows.Remove(window);
    internal void Record(Exception? error)
    {
        if (error is not null && !errors.Contains(error)) errors.Add(error);
    }
    public bool Post(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        var handle = Handle;
        if (handle == 0) return false;
        var root = GCHandle.Alloc(new Posted(this, action));
        int status = Native.ApplicationPost(handle, &Deliver, GCHandle.ToIntPtr(root));
        if (status == 0) return true;
        root.Free();
        if (status is 2 or 11) return false;
        Window.CheckStatus(status); return false;
    }
    private sealed record Posted(Application Application, Action Action);
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Deliver(nint context, uint execute)
    {
        var root = GCHandle.FromIntPtr(context);
        Posted? posted = null;
        try
        {
            posted = root.Target as Posted;
            if (posted is null) return 8;
            if (execute != 0)
            {
                ++posted.Application.callbacks;
                try { posted.Action(); }
                finally { --posted.Application.callbacks; }
            }
            return 0;
        }
        catch (Exception error) { posted?.Application.Record(error); return 8; }
        finally { root.Free(); }
    }
    public void Dispose()
    {
        if (Handle == 0) return;
        Guard();
        if (running || callbacks != 0) throw new XuiException(7, "Return from application dispatch before Dispose.");
        foreach (var window in windows)
            if (window.State is WindowState.Open or WindowState.Closing)
                throw new XuiException(7, "Close the windows and return from Run before Dispose.");
        foreach (var window in windows.ToArray()) window.Dispose();
        Window.CheckStatus(Native.ApplicationDestroy(Handle));
        Handle = 0;
    }
}

public sealed unsafe partial class Window
{
    internal Application? Application { get; }
    private GCHandle closedRoot;
    public event Action<WindowClosedEventArgs>? Closed;
    public WindowState State { get { Guard(); Check(Native.WindowState(Handle, out uint state)); return (WindowState)state; } }
    private void InitializeClosed()
    {
        closedRoot = GCHandle.Alloc(this, GCHandleType.Weak);
        Check(Native.WindowClosed(Handle, &ClosedTrampoline, GCHandle.ToIntPtr(closedRoot)));
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ClosedTrampoline(nint context, Native.Event* value)
    {
        Window? window = null;
        try
        {
            window = GCHandle.FromIntPtr(context).Target as Window;
            if (window is null) return 8;
            ++window.callbacks;
            try
            {
                Exception? failure = window.callbackError;
                if (failure is null)
                {
                    int status = Native.WindowError(window.Handle, null, 0, out uint length);
                    if (status != 0 && status != 6) window.Check(status);
                    if (length != 0)
                    {
                        var bytes = new byte[length];
                        fixed (byte* p = bytes) window.Check(Native.WindowError(window.Handle, p, length, out _));
                        failure = new XuiException(9, Encoding.GetString(bytes));
                    }
                }
                window.Application?.Record(failure);
                window.Closed?.Invoke(new(failure));
            }
            finally { --window.callbacks; }
            return 0;
        }
        catch (Exception error)
        {
            if (window is not null) { window.callbackError = error; window.Application?.Record(error); }
            return 8;
        }
    }
}
