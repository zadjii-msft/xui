using System.ComponentModel;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;

namespace Xui.Development;

public static class ReloadHost
{
    private static readonly object Gate = new();
    private static readonly List<Session> Sessions = [];
    [ThreadStatic] private static Session? current;

    public static void Run(Func<Window> createWindow, Action<Window> build)
    {
        ArgumentNullException.ThrowIfNull(createWindow);
        ArgumentNullException.ThrowIfNull(build);
        if (current is not null) throw new InvalidOperationException("Nested XUI development hosts are unsupported.");
        Console.WriteLine($"XUI development process {Environment.ProcessId}");
        bool restart;
        do
        {
            using var window = createWindow();
            using var session = new Session(window);
            current = session;
            try
            {
                build(window);
                lock (Gate) Sessions.Add(session);
                window.Run();
                session.Error?.Throw();
                restart = session.Restart;
            }
            finally
            {
                lock (Gate) Sessions.Remove(session);
                current = null;
            }
        } while (restart);
    }

    public static void Register(Window window, Func<bool> refresh)
    {
        window.VerifyAccess();
        ArgumentNullException.ThrowIfNull(refresh);
        var session = current;
        if (session is null || !ReferenceEquals(session.Window, window))
            throw new InvalidOperationException("Debug XUI components require ReloadHost.Run. Set XuiHotReload=false to opt out.");
        session.Refresh.Add(refresh);
    }

    public static void UpdateApplication(Type[]? updatedTypes)
    {
        lock (Gate)
        {
            foreach (var session in Sessions) session.Dispatcher.Post();
        }
    }

    private sealed class Session : IDisposable
    {
        internal readonly Window Window;
        internal readonly List<Func<bool>> Refresh = [];
        internal readonly Dispatcher Dispatcher;
        internal bool Restart;
        internal ExceptionDispatchInfo? Error;

        internal Session(Window window)
        {
            Window = window;
            Dispatcher = new Dispatcher(Reload);
        }
        private void Reload()
        {
            if (Restart || Error is not null) return;
            try
            {
                foreach (var refresh in Refresh)
                {
                    if (refresh()) continue;
                    Restart = true;
                    Console.WriteLine("XUI topology/state schema changed: recreating the window; component state resets.");
                    Window.Close();
                    return;
                }
                Console.WriteLine("XUI hot reload applied on UI thread; retained controls and state preserved.");
            }
            catch (Exception error)
            {
                Error = ExceptionDispatchInfo.Capture(error);
                Console.Error.WriteLine($"XUI hot reload failed: {error}");
                Window.Close();
            }
        }
        public void Dispose() => Dispatcher.Dispose();
    }
}

internal sealed class Dispatcher : IDisposable
{
    private const uint RefreshMessage = 0x8000 + 317;
    private static readonly object Gate = new();
    private static readonly Dictionary<nint, Dispatcher> Windows = [];
    private static readonly WindowProcedure Procedure = WindowProc;
    private readonly Action refresh;
    private readonly string className = "Xui.Reload." + Guid.NewGuid().ToString("N");
    private readonly nint module;
    private nint handle;
    private bool registered;

    internal Dispatcher(Action refresh)
    {
        this.refresh = refresh;
        module = GetModuleHandleW(null);
        var cls = new WindowClass { Procedure = Marshal.GetFunctionPointerForDelegate(Procedure), Instance = module, ClassName = className };
        if (RegisterClassW(ref cls) == 0) throw new Win32Exception(Marshal.GetLastWin32Error(), "Register XUI reload dispatcher.");
        registered = true;
        handle = CreateWindowExW(0, className, "", 0, 0, 0, 0, 0, new nint(-3), 0, module, 0);
        if (handle == 0)
        {
            int error = Marshal.GetLastWin32Error();
            Dispose();
            throw new Win32Exception(error, "Create XUI reload dispatcher.");
        }
        lock (Gate) Windows.Add(handle, this);
    }
    internal void Post()
    {
        lock (Gate)
        {
            if (handle == 0) throw new ObjectDisposedException(nameof(Dispatcher));
            if (!PostMessageW(handle, RefreshMessage, 0, 0))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "XUI hot reload dispatch failed; restart the application.");
        }
    }
    private static nint WindowProc(nint window, uint message, nuint wparam, nint lparam)
    {
        if (message != RefreshMessage) return DefWindowProcW(window, message, wparam, lparam);
        Dispatcher? dispatcher;
        lock (Gate) Windows.TryGetValue(window, out dispatcher);
        // Managed exceptions must never escape a native window procedure.
        try { dispatcher?.refresh(); }
        catch (Exception error)
        {
            Console.Error.WriteLine($"XUI dispatcher could not close after a reload failure: {error}");
            PostQuitMessage(1);
        }
        return 0;
    }
    public void Dispose()
    {
        lock (Gate)
        {
            if (handle != 0)
            {
                if (!DestroyWindow(handle)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Destroy XUI reload dispatcher.");
                Windows.Remove(handle);
                handle = 0;
            }
            if (registered)
            {
                if (!UnregisterClassW(className, module)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Unregister XUI reload dispatcher.");
                registered = false;
            }
        }
    }
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    private delegate nint WindowProcedure(nint window, uint message, nuint wparam, nint lparam);
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct WindowClass
    {
        internal uint Style;
        internal nint Procedure;
        internal int ClassExtra, WindowExtra;
        internal nint Instance, Icon, Cursor, Background;
        [MarshalAs(UnmanagedType.LPWStr)] internal string? MenuName;
        [MarshalAs(UnmanagedType.LPWStr)] internal string ClassName;
    }
    [DllImport("kernel32.dll", ExactSpelling = true, CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern nint GetModuleHandleW(string? name);
    [DllImport("user32.dll", ExactSpelling = true, CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern ushort RegisterClassW(ref WindowClass cls);
    [DllImport("user32.dll", ExactSpelling = true, CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool UnregisterClassW(string name, nint instance);
    [DllImport("user32.dll", ExactSpelling = true, CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern nint CreateWindowExW(uint exStyle, string className, string title, uint style,
        int x, int y, int width, int height, nint parent, nint menu, nint instance, nint param);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DestroyWindow(nint window);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessageW(nint window, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern nint DefWindowProcW(nint window, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern void PostQuitMessage(int exitCode);
}
