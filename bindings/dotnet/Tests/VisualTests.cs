using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Xui;

internal static class VisualTests
{
    private static void Expect(bool value, string message) { if (!value) throw new Exception(message); }
    private sealed class Source(string path) : IReadOnlyImmutableSource
    {
        public int Reads;
        public ulong Count => 1000000;
        public ItemKey Key(ulong index) => new(index + 1, 9);
        public ulong? Find(ItemKey key) => key.Version == 9 && key.Id > 0 && key.Id <= Count ? key.Id - 1 : null;
        public ItemContent Item(ulong index, ulong column = 0)
        {
            ++Reads;
            return new($"Row {index}", Icon: (ButtonIcon)(19 + index % 3), ImagePath: column == 0 ? path : "");
        }
    }
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    private delegate void TimerCallback(nint window, uint message, nuint timer, uint time);
    private static readonly Dictionary<(nint, nuint), Action> MenuDrivers = [];
    private static readonly TimerCallback MenuTimer = (window, _, timer, _) =>
    {
        if (MenuDrivers.TryGetValue((window, timer), out var driver)) driver();
    };
    [StructLayout(LayoutKind.Sequential)]
    private struct Point { public int X, Y; }
    [DllImport("user32.dll")] private static extern nint GetFocus();
    [DllImport("user32.dll")] private static extern nint GetAncestor(nint window, uint flags);
    [DllImport("user32.dll")] private static extern nuint SetTimer(nint window, nuint id, uint interval, TimerCallback callback);
    [DllImport("user32.dll")] private static extern nint SendMessageW(nint window, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll")] private static extern bool PostMessageW(nint window, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll")] private static extern bool ClientToScreen(nint window, ref Point point);
    [DllImport("user32.dll")] private static extern nint GetDC(nint window);
    [DllImport("user32.dll")] private static extern int ReleaseDC(nint window, nint dc);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(nint window);
    [DllImport("gdi32.dll")] private static extern uint GetPixel(nint dc, int x, int y);
    private delegate bool EnumWindowCallback(nint window, nint parameter);
    [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] private static extern bool EnumThreadWindows(uint thread, EnumWindowCallback callback, nint parameter);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassNameW(nint window, StringBuilder name, int capacity);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetMenuStringW(nint menu, uint item, StringBuilder text, int capacity, uint flags);
    [DllImport("user32.dll")] private static extern int GetMenuItemCount(nint menu);
    [DllImport("user32.dll")] private static extern uint GetMenuState(nint menu, uint item, uint flags);
    [DllImport("user32.dll")] private static extern bool KillTimer(nint window, nuint id);
    [DllImport("user32.dll")] private static extern nint GetForegroundWindow();
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(nint window);
    [ComImport, Guid("618736E0-3C3D-11CF-810C-00AA00389B71"), InterfaceType(ComInterfaceType.InterfaceIsDual)]
    private interface AccessibleMenu
    {
        [return: MarshalAs(UnmanagedType.IDispatch)] object get_accParent();
        int get_accChildCount();
        [return: MarshalAs(UnmanagedType.IDispatch)] object get_accChild([MarshalAs(UnmanagedType.Struct)] object child);
        [return: MarshalAs(UnmanagedType.BStr)] string get_accName([MarshalAs(UnmanagedType.Struct)] object child);
        [return: MarshalAs(UnmanagedType.BStr)] string get_accValue([MarshalAs(UnmanagedType.Struct)] object child);
        [return: MarshalAs(UnmanagedType.BStr)] string get_accDescription([MarshalAs(UnmanagedType.Struct)] object child);
        [return: MarshalAs(UnmanagedType.Struct)] object get_accRole([MarshalAs(UnmanagedType.Struct)] object child);
        [return: MarshalAs(UnmanagedType.Struct)] object get_accState([MarshalAs(UnmanagedType.Struct)] object child);
        [return: MarshalAs(UnmanagedType.BStr)] string get_accHelp([MarshalAs(UnmanagedType.Struct)] object child);
        int get_accHelpTopic([MarshalAs(UnmanagedType.BStr)] out string file, [MarshalAs(UnmanagedType.Struct)] object child);
        [return: MarshalAs(UnmanagedType.BStr)] string get_accKeyboardShortcut([MarshalAs(UnmanagedType.Struct)] object child);
        [return: MarshalAs(UnmanagedType.Struct)] object get_accFocus();
        [return: MarshalAs(UnmanagedType.Struct)] object get_accSelection();
        [return: MarshalAs(UnmanagedType.BStr)] string get_accDefaultAction([MarshalAs(UnmanagedType.Struct)] object child);
        void accSelect(int flags, [MarshalAs(UnmanagedType.Struct)] object child);
        void accLocation(out int left, out int top, out int width, out int height, [MarshalAs(UnmanagedType.Struct)] object child);
        [return: MarshalAs(UnmanagedType.Struct)] object accNavigate(int direction, [MarshalAs(UnmanagedType.Struct)] object start);
        [return: MarshalAs(UnmanagedType.Struct)] object accHitTest(int x, int y);
        void accDoDefaultAction([MarshalAs(UnmanagedType.Struct)] object child);
        void set_accName([MarshalAs(UnmanagedType.Struct)] object child, [MarshalAs(UnmanagedType.BStr)] string name);
        void set_accValue([MarshalAs(UnmanagedType.Struct)] object child, [MarshalAs(UnmanagedType.BStr)] string value);
    }
    [DllImport("oleacc.dll")]
    private static extern int AccessibleObjectFromWindow(nint window, uint objectId, in Guid interfaceId,
        [MarshalAs(UnmanagedType.Interface)] out AccessibleMenu menu);
    private static nint StyledMenu()
    {
        nint popup = 0;
        EnumThreadWindows(GetCurrentThreadId(), (window, _) =>
        {
            var name = new StringBuilder(40);
            GetClassNameW(window, name, name.Capacity);
            if (name.ToString() != "#32768" || !IsWindowVisible(window)) return true;
            popup = window;
            return false;
        }, 0);
        return popup;
    }

    private static string Fixture()
    {
        var directory = Path.GetFullPath(Path.Combine("build", "explorer", "managed-visual-fixtures",
            new string('\u65e5', 100), new string('\u672c', 100)));
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, new string('\u8a9e', 150) + ".bmp");
        using var file = new BinaryWriter(File.Create(path));
        file.Write((ushort)0x4d42); file.Write(54 + 24 * 24 * 4); file.Write(0); file.Write(54);
        file.Write(40); file.Write(24); file.Write(24); file.Write((ushort)1); file.Write((ushort)32);
        file.Write(0); file.Write(24 * 24 * 4); file.Write(0); file.Write(0); file.Write(0); file.Write(0);
        for (int i = 0; i < 24 * 24; ++i) file.Write(0xff2040c0u);
        return @"\\?\" + path;
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference MenuLifetime(DataGrid grid)
    {
        var capture = new object();
        grid.OnContextMenu(() => [], _ => { }, () => { GC.KeepAlive(capture); return []; }, ShellMenuPresentation.Xui);
        return new(capture);
    }
    internal static void Run()
    {
        var path = Fixture();
        Expect(Encoding.UTF8.GetByteCount(path) > 1024, "Fixture exercises the full-length UTF-8 visual ABI");
        using (var window = new Window("Managed visuals and context", 600, 400))
        {
            var grid = window.DataGrid("Visual rows").SetColumns([new("Name", 180), new("Other", 200)]);
            // Original column zero remains the image column after display reorder.
            grid.SetColumnOrder([1, 0]);
            var data = new Source(path);
            using var source = window.ImmutableSource(data);
            grid.SetSource(source);
            var editor = window.TextInput("Initial focus");
            window.SetContent(window.Stack().Add(editor).Add(grid, 1));
            var capture = MenuLifetime(grid);
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
            Expect(capture.IsAlive, "Menu callbacks remain rooted");
            grid.ClearContextMenu();
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
            Expect(!capture.IsAlive, "Clearing the menu releases callback captures");
            int requests = 0, selections = 0, shellRequests = 0;
            grid.Event += e => { if (e.Kind == EventKind.Selection) ++selections; };
            grid.OnContextMenu(() =>
            {
                ++requests;
                Expect(grid.Selection.Focused == (requests == 3 ? null : new ItemKey(requests == 4 ? 3u : 2u, 9)),
                    "Factory observes pointer or keyboard selection");
                try { grid.OnContextMenu(() => [], _ => { }); throw new Exception("Expected menu reentry rejection"); }
                catch (XuiException error) { Expect(error.Status == 7, "Cannot replace a menu during its request"); }
                return [];
            }, _ => throw new Exception("An empty menu cannot invoke an action"), () =>
            {
                Expect(++shellRequests == requests, "Shell path factory follows the app factory after selection");
                return [];
            });
            nint native = 0;
            bool done = false;
            bool postedClick = false;
            var started = DateTime.UtcNow;
            TimerCallback timer = (_, _, _, _) => window.Post(() =>
            {
                Expect(DateTime.UtcNow - started < TimeSpan.FromSeconds(15), "Managed visual deadline");
                if (postedClick)
                {
                    if (requests != 4) return;
                    Expect(selections == 3, "Posted right-button input changes selection exactly once");
                    done = true; window.Close(); return;
                }
                var scale = GetDpiForWindow(native) / 96.0;
                var dc = GetDC(native);
                var pixel = GetPixel(dc, (int)(224 * scale), (int)(54 * scale));
                ReleaseDC(native, dc);
                if (pixel != 0x00c04020) return;
                Expect(data.Reads < 10000, "A million-row source only reads visible rows");
                var point = new Point { X = (int)(80 * scale), Y = (int)(86 * scale) };
                editor.Focus();
                var position = (nint)((point.Y << 16) | (point.X & 0xffff));
                SendMessageW(native, 0x204, 2, position);
                SendMessageW(native, 0x205, 0, position);
                Expect(requests == 1 && grid.Focused, "Right-button release generates a context request on an unfocused grid");
                SendMessageW(native, 0x7b, (nuint)native, -1);
                point = new Point { X = (int)(80 * scale), Y = (int)(12 * scale) };
                Expect(ClientToScreen(native, ref point), "Convert empty context coordinates");
                SendMessageW(native, 0x7b, (nuint)native, (nint)((point.Y << 16) | (point.X & 0xffff)));
                Expect(requests == 3 && selections == 2, "Context callbacks preserve normal selection events");
                point = new Point { X = (int)(80 * scale), Y = (int)(118 * scale) };
                position = (nint)((point.Y << 16) | (point.X & 0xffff));
                postedClick = true;
                Expect(PostMessageW(native, 0x204, 2, position) && PostMessageW(native, 0x205, 0, position),
                    "Queue the actual right-button down/up path");
            });
            Expect(window.Post(() =>
            {
                grid.Focus(); native = GetFocus();
                Expect(SetTimer(GetAncestor(native, 2), 93, 30, timer) != 0, "Start owned-window managed checks");
            }), "Accept initial UI delivery");
            window.Run();
            GC.KeepAlive(timer);
            Expect(done && shellRequests == 4, "Managed image and context workload completes");
        }
        using (var window = new Window("Palette pointer activation"))
        {
            var anchor = window.TextInput("Anchor");
            var editor = window.TextInput("Palette query");
            var items = window.ItemsView("Palette rows").ItemSize(180, 48);
            using var source = window.ImmutableSource(new Source(""));
            items.SetSource(source);
            var popup = window.Popup("Pointer palette", window.Stack().Add(editor).Add(items, 1));
            window.SetContent(window.Stack().Add(anchor));
            ulong activated = 0;
            items.Event += e => { if (e.Kind == EventKind.Click) activated = e.Value; };
            Expect(window.Post(() =>
            {
                popup.Show(anchor);
                items.Focus();
                var native = GetFocus();
                editor.Focus();
                Expect(editor.Focused && popup.IsOpen, "Palette starts with native editor focus");
                var scale = GetDpiForWindow(native) / 96.0;
                var position = (nint)(((int)(72 * scale) << 16) | (int)(32 * scale));
                SendMessageW(native, 0x201, 1, position);
                SendMessageW(native, 0x202, 0, position);
                Expect(popup.IsOpen && items.Selection.Focused == new ItemKey(2, 9),
                    "Pointer selection inside a palette does not light-dismiss it");
                SendMessageW(native, 0x203, 1, position);
                SendMessageW(native, 0x202, 0, position);
                Expect(activated == 2 && popup.IsOpen, "Double-click activates the hit palette row before application dismissal");
                window.Close();
            }), "Accept palette pointer checks");
            window.Run();
            Expect(activated == 2, "Palette pointer activation completes");
        }
        foreach (bool contextFailure in new[] { false, true })
        {
            using var window = new Window("Visual callback failure");
            var grid = window.DataGrid("Failure").SetColumns([new("Name")]);
            using var source = window.ImmutableSource(new Source(contextFailure ? "" : new string('x', 32768)));
            grid.SetSource(source); window.SetContent(window.Stack().Add(grid, 1));
            if (contextFailure)
            {
                grid.OnContextMenu(() => throw new InvalidOperationException("menu sentinel"), _ => { });
                window.Post(() => { grid.Focus(); SendMessageW(GetFocus(), 0x7b, (nuint)GetFocus(), -1); });
            }
            try { window.Run(); throw new Exception("Expected callback failure"); }
            catch (XuiException error)
            {
                Expect(error.Status == 8 && error.InnerException is not null, "Visual/menu failures preserve the managed exception");
            }
        }
        ShellMenuSnapshots();
        CustomShellMenus();
        Console.WriteLine("C# visual UTF-8 paths, context selection, callback lifetime and failure checks passed.");
    }
    private static void CustomShellMenus()
    {
        var folder = Path.GetFullPath(Path.Combine("build", "explorer", "managed-shell-menu-fixtures"));
        Directory.CreateDirectory(folder);
        foreach (int mode in new[] { 0, 1, 2, 3, 5, 6, 4 })
        {
            using var window = new Window("Custom Shell menu");
            var grid = window.DataGrid("Files").SetColumns([new("Name")]);
            using var source = window.ImmutableSource(new Source(""));
            using var replacement = window.ImmutableSource(new Source(""));
            grid.SetSource(source).Select(new(1, 9)); window.SetContent(window.Stack().Add(grid, 1));
            int factories = 0, actions = 0;
            nint peer = 0, root = 0;
            bool sawStyledMenu = false;
            var firstFrame = new System.Diagnostics.Stopwatch();
            grid.OnContextMenu(() =>
            {
                Expect(++factories == 1 && grid.Selection.Focused == new ItemKey(1, 9), "XUI menu factory observes the selected row");
                return [new(71, "Managed harmless command", ShortcutHint: "Ctrl+M")];
            }, id =>
            {
                Expect(id == 71 && StyledMenu() == 0 && (GetForegroundWindow() != root || GetFocus() == peer),
                    "Gallery context menu closes and restores active-owner focus before app dispatch");
                ++actions;
                if (mode == 5) throw new InvalidOperationException("Custom Shell action sentinel");
            }, () =>
            {
                Expect(++factories == 2, "Shell paths follow the app snapshot");
                return [folder];
            }, ShellMenuPresentation.Xui);
            try
            {
                grid.OnContextMenu(() => [], _ => { }, () => [], (ShellMenuPresentation)2);
                throw new Exception("Invalid presentation was accepted");
            }
            catch (ArgumentOutOfRangeException) { }
            var started = DateTime.UtcNow;
            Exception? timerFailure = null;
            bool dispatched = false;
            Action drive = () =>
            {
                if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("Native menu tests require Windows.");
                try
                {
                    Expect(DateTime.UtcNow - started < TimeSpan.FromSeconds(10),
                        $"Styled Shell menu deadline (mode {mode}, dispatched {dispatched}, active {GetForegroundWindow() == root})");
                    if (dispatched) return;
                    var popup = StyledMenu();
                    if (popup == 0) return;
                    dispatched = true;
                    sawStyledMenu = true;
                    Expect(firstFrame.ElapsedMilliseconds < 500,
                        $"C# Shell menu first frame exceeded 500 ms: {firstFrame.ElapsedMilliseconds} ms");
                    Console.WriteLine($"C# Shell menu mode {mode}: first visible frame {firstFrame.ElapsedMilliseconds} ms");
                    var menu = SendMessageW(popup, 0x1e1, 0, 0); // MN_GETHMENU
                    int count = GetMenuItemCount(menu);
                    Expect(count > 2, "XUI presentation uses a native menu, not a CommandSurface");
                    int appIndex = -1;
                    bool hasFallback = false;
                    for (uint i = 0; i < count; ++i)
                    {
                        Expect((GetMenuState(menu, i, 0x400) & 0x100) != 0, "File menu rows use the gallery owner-drawn renderer");
                        var label = new StringBuilder(2048);
                        GetMenuStringW(menu, i, label, label.Capacity, 0x400);
                        if (label.ToString() == "Managed harmless command\tCtrl+M") appIndex = (int)i;
                        hasFallback |= label.ToString() == "Show Windows menu...";
                    }
                    Expect(appIndex >= 0 && hasFallback, "Styled menu preserves shortcut text and native fallback");
                    if (mode == 1) grid.SetSource(replacement);
                    if (mode == 2) grid.Select(new(2, 9));
                    if (mode == 3) grid.ClearContextMenu();
                    if (mode == 4) { window.Close(); return; }
                    if (mode is 1 or 2 or 3 or 6) { SendMessageW(root, 0x1f, 0, 0); return; }
                    var interfaceId = typeof(AccessibleMenu).GUID;
                    Marshal.ThrowExceptionForHR(AccessibleObjectFromWindow(popup, 0xfffffffc, in interfaceId, out var accessible));
                    try
                    {
                        Expect(accessible.get_accName(appIndex + 1) == "Managed harmless command",
                            "Native accessibility selects only the harmless app command");
                        accessible.accDoDefaultAction(appIndex + 1);
                    }
                    finally { Marshal.ReleaseComObject(accessible); }
                }
                catch (Exception error)
                {
                    timerFailure = error;
                    KillTimer(root, 94);
                    SendMessageW(root, 0x1f, 0, 0); // WM_CANCELMODE
                }
            };
            window.Post(() =>
            {
                grid.Focus(); grid.Select(new(1, 9)); peer = GetFocus(); root = GetAncestor(peer, 2);
                MenuDrivers.Add((root, 94), drive);
                Expect(SetTimer(root, 94, 20, MenuTimer) != 0, "Start owned native-menu checks");
                firstFrame.Restart();
                SendMessageW(peer, 0x7b, (nuint)peer, -1);
                KillTimer(root, 94);
                MenuDrivers.Remove((root, 94));
                window.Close();
            });
            bool actionFailureObserved = false;
            try
            {
                window.Run();
            }
            catch (XuiException error) when (mode == 5)
            {
                actionFailureObserved = true;
                Expect(error.Status == 8 && error.InnerException is InvalidOperationException,
                    "Custom Shell action failures preserve the managed exception");
            }
            finally
            {
                KillTimer(root, 94);
                MenuDrivers.Remove((root, 94));
            }
            if (timerFailure is not null) throw new InvalidOperationException("Styled-menu driver failed.", timerFailure);
            Expect(mode != 5 || actionFailureObserved, "Custom action failure must reach the managed caller");
            Expect(sawStyledMenu && factories == 2 && actions == (mode is 0 or 5 ? 1 : 0),
                "Custom Shell actions preserve IDs and cancel stale source, selection, subscription, and owner snapshots");
        }
        Console.WriteLine("C# gallery-style Shell menus, shortcut labels, action identity, stale guards and callback failures passed.");
    }
    private static void ShellMenuSnapshots()
    {
        var missing = Path.GetFullPath(Path.Combine("build", "explorer", "missing-shell-test-entry"));
        Expect(!File.Exists(missing) && !Directory.Exists(missing), "Stale-menu fixture must not exist");
        foreach (bool replaceSource in new[] { true, false })
        {
            using var window = new Window("Stale merged menu");
            var grid = window.DataGrid("Snapshot").SetColumns([new("Name")]);
            using var source = window.ImmutableSource(new Source(""));
            using var replacement = window.ImmutableSource(new Source(""));
            grid.SetSource(source).Select(new(1, 9)); window.SetContent(window.Stack().Add(grid, 1));
            int requests = 0;
            grid.OnContextMenu(() =>
            {
                ++requests;
                if (replaceSource) grid.SetSource(replacement);
                else grid.Select(new(2, 9));
                return [new(1, "Must not invoke")];
            }, _ => throw new Exception("Stale app action invoked"), () => [missing]);
            window.Post(() =>
            {
                grid.Focus(); SendMessageW(GetFocus(), 0x7b, (nuint)GetFocus(), -1);
                Expect(requests == 1 && window.CallbackStatus == 0, "Stale source/selection cancels before Shell path discovery");
                window.Close();
            });
            window.Run();
        }
        Func<string[]>[] failures =
        [
            () => throw new InvalidOperationException("Shell factory sentinel"),
            () => null!,
            () => [""],
            () => [new string('x', 32768)],
            () => Enumerable.Repeat("x", 257).ToArray()
        ];
        foreach (var factory in failures)
        {
            using var window = new Window("Shell path callback failure");
            var grid = window.DataGrid("Failure").SetColumns([new("Name")]);
            using var source = window.ImmutableSource(new Source(""));
            grid.SetSource(source).Select(new(1, 9)); window.SetContent(window.Stack().Add(grid, 1));
            grid.OnContextMenu(() => [], _ => { }, factory);
            window.Post(() => { grid.Focus(); SendMessageW(GetFocus(), 0x7b, (nuint)GetFocus(), -1); });
            try { window.Run(); throw new Exception("Expected Shell path callback failure"); }
            catch (XuiException error)
            {
                Expect(error.Status == 8 && error.InnerException is not null, "Shell path callback errors preserve managed causes");
            }
        }
        Console.WriteLine("C# merged-menu factory order, stale source/selection, path bounds and callback failures passed.");
    }
}
