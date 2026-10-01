using System.Runtime.InteropServices;
using Xui;

internal static class WindowNativeTests
{
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
    private static void Status(Action action, int expected)
    {
        try { action(); }
        catch (XuiException error) when (error.Status == expected) { return; }
        throw new InvalidOperationException($"Expected XUI status {expected}.");
    }

    internal static void Run()
    {
        Status(() => { using var invalid = new Window(customTitlebar: true, transparent: true); }, 1);
        using (var plain = new Window())
            Status(() => plain.SetDragRegion(0, 0, 20, 20), 1);
        using (var app = new Xui.Application())
        using (var window = app.CreateWindow("Native window contract", 380, 220, transparent: true))
        {
            Status(() => _ = window.NativeHwnd, 7);
            Status(() => window.SetDragRegion(0, 0, float.NaN, 8), 1);
            Status(() => window.SetDragRegion(0, 0, 0, 8), 1);
            window.SetDragRegion(8, 8, 30, 12);
            nint initial = 0;
            int callbacks = 0;
            window.NativeHwndCreated = hwnd =>
            {
                ++callbacks;
                Require(IsWindow(hwnd) && !IsWindowVisible(hwnd), "The HWND was not delivered before Show.");
                Status(() => _ = window.NativeHwnd, 7);
                initial = hwnd;
            };
            window.SetContent(window.Stack().Add(window.Label("Alpha host")));
            app.Show(window);
            Require(initial != 0 && initial == window.NativeHwnd && callbacks == 1, "Borrowed HWND changed.");
            Task.Run(() => Status(() => { var unused = window.NativeHwnd; }, 4)).GetAwaiter().GetResult();
            Require((GetWindowLongPtr(window.NativeHwnd, -20) & 0x00080000) != 0, "Layered style is missing.");
            Status(() => window.SetTransparent(false), 7);
            Status(() => window.NativeHwndCreated = null, 7);
            nint handle = window.NativeHwnd;
            window.Closed += _ =>
            {
                Status(() => { var unused = window.NativeHwnd; }, 11);
                Require(!IsWindow(handle), "The borrowed HWND survived closure.");
            };
            Require(app.Post(window.Close), "Cannot schedule closure.");
            app.Run();
            Require(callbacks == 1 && !IsWindow(handle), "Window was not closed.");
        }
        using (var window = new Window("Standalone native host", transparent: true))
        {
            nint seen = 0;
            window.NativeHwndCreated = hwnd => seen = hwnd;
            window.SetContent(window.Stack().Add(window.Label("Standalone")));
            Require(window.Post(window.Close), "Standalone close was not accepted.");
            window.Run();
            Require(seen != 0 && !IsWindow(seen), "Standalone callback was skipped.");
        }
        using (var window = new Window("Fail native configuration"))
        {
            window.NativeHwndCreated = _ => throw new InvalidOperationException("configuration sentinel");
            window.SetContent(window.Stack().Add(window.Label("Never shown")));
            try { window.Run(); throw new InvalidOperationException("A failed callback was ignored."); }
            catch (XuiException error)
            {
                Require(error.Status == 8 && error.InnerException?.Message == "configuration sentinel",
                    "Native-window callback failure lost its managed exception.");
            }
        }
        Console.WriteLine("Managed HWND ownership, creation, threading, and callback failures passed");
    }

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindow(nint window);
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(nint window);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    private static extern nint GetWindowLongPtr(nint window, int index);
}
