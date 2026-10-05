using System.Runtime.InteropServices;
using System.Text;
using Xui;

internal static class ItemActionTests
{
    private sealed class Source(ulong version = 7, bool enabled = true, string action = "More") : IReadOnlyImmutableSource
    {
        public ulong Count => 3;
        public ItemKey Key(ulong index) => new(index + 1, version);
        public ulong? Find(ItemKey key) => key.Version == version && key.Id is > 0 and <= 3 ? key.Id - 1 : null;
        public ItemContent Item(ulong index, ulong column = 0) => new($"Row {index}", Enabled: enabled, Action: action);
    }
    private static void Require(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
    }
    private static void Reject(Action action)
    {
        try { action(); }
        catch (XuiException error) when (error.Status == 1) { return; }
        throw new InvalidOperationException("Expected a stale or unavailable row error.");
    }
    private static void Flush(nint hwnd)
    {
        SendMessageW(hwnd, 0x800c, 0, 0);
        InvalidateRect(hwnd, 0, false);
        UpdateWindow(hwnd);
    }
    private static nint Child(nint hwnd, string name)
    {
        nint found = 0;
        EnumChildWindows(hwnd, (child, _) =>
        {
            var text = new StringBuilder(128);
            GetWindowTextW(child, text, text.Capacity);
            if (text.ToString() != name) return true;
            found = child; return false;
        }, 0);
        Require(found != 0, "The native collection window is missing.");
        return found;
    }
    internal static void Run()
    {
        RunActions();
        RunCallbackError();
        Console.WriteLine("Managed inline action delivery, row menus, stale keys, disabled rows and callback errors passed.");
    }
    private static void RunActions()
    {
        using var app = new Xui.Application();
        using var window = app.CreateWindow("Managed inline actions", 520, 300, visualStyle: VisualStyle.WinUI);
        window.SetShowActivated(false);
        var list = window.ItemsView("Action rows");
        using var source = window.ImmutableSource(new Source());
        list.SetSource(source);
        var menu = window.MenuFlyout("Row menu").SetCommands([new(1, "Open")]);
        int actions = 0, commands = 0;
        list.Event += e =>
        {
            if (e.Kind != EventKind.Action) return;
            Require(e.Value == 1, "Inline action lost the row identity.");
            ++actions;
        };
        menu.OnCommand((id, pin) => { Require(id == 1 && !pin, "Wrong row command."); ++commands; });
        window.SetContent(window.Stack().Add(list, 1));
        app.Show(window);
        var hwnd = window.NativeHwnd;
        Flush(hwnd);
        var native = Child(hwnd, "Action rows");
        GetClientRect(native, out var bounds);
        var dpi = GetDpiForWindow(native);
        var position = (nint)((bounds.Right - (int)(24 * dpi / 96)) | ((int)(20 * dpi / 96) << 16));
        SendMessageW(native, 0x0201, 1, position);
        SendMessageW(native, 0x0202, 0, position);
        Flush(hwnd);
        Require(actions == 1, "Managed source action did not receive native pointer activation.");
        menu.Show(list, new ItemKey(1, 7));
        Require(menu.IsOpen, "Row-anchored menu did not open.");
        menu.Invoke(1);
        Require(commands == 1, "Row command did not invoke.");
        menu.Dismiss();
        Reject(() => menu.Show(list, new ItemKey(1, 8)));
        Reject(() => menu.Show(list, new ItemKey(99, 7)));
        using var replaced = window.ImmutableSource(new Source(8));
        list.SetSource(replaced); Flush(hwnd);
        Reject(() => menu.Show(list, new ItemKey(1, 7)));
        using var disabled = window.ImmutableSource(new Source(8, false));
        list.SetSource(disabled); Flush(hwnd);
        Reject(() => menu.Show(list, new ItemKey(1, 8)));
        SendMessageW(native, 0x0201, 1, position);
        SendMessageW(native, 0x0202, 0, position);
        Flush(hwnd);
        Require(actions == 1, "Disabled inline action invoked.");
        window.Close(); app.Run();
    }
    private static void RunCallbackError()
    {
        using var failed = new Window("Invalid action field");
        var invalid = failed.ItemsView("Invalid actions");
        using var invalidSource = failed.ImmutableSource(new Source(action: new string('x', 1025)));
        invalid.SetSource(invalidSource);
        failed.SetContent(failed.Stack().Add(invalid, 1));
        try { failed.Run(); throw new InvalidOperationException("Oversized action was accepted."); }
        catch (XuiException error)
        {
            Require(error.Status == 8 && error.InnerException is ArgumentException,
                "Action callback error did not preserve its managed exception.");
        }
    }
    private delegate bool EnumCallback(nint hwnd, nint context);
    [StructLayout(LayoutKind.Sequential)]
    private struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(nint hwnd, EnumCallback callback, nint context);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextW(nint hwnd, StringBuilder text, int capacity);
    [DllImport("user32.dll")] private static extern bool GetClientRect(nint hwnd, out Rect bounds);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(nint hwnd);
    [DllImport("user32.dll")] private static extern nint SendMessageW(nint hwnd, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll")] private static extern bool InvalidateRect(nint hwnd, nint rect, bool erase);
    [DllImport("user32.dll")] private static extern bool UpdateWindow(nint hwnd);
}
