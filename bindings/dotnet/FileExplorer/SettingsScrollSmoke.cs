using System.Diagnostics;
using System.Drawing;
using System.Runtime.InteropServices;

namespace Xui.FileExplorer;

internal static class SettingsScrollSmoke
{
    [DllImport("user32.dll")] private static extern nint GetFocus();
    [DllImport("user32.dll")] private static extern nint GetAncestor(nint window, uint flags);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(nint window);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ClientToScreen(nint window, ref Point point);
    [DllImport("user32.dll", EntryPoint = "SendMessageW")]
    private static extern nint SendMessageW(nint window, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll", EntryPoint = "SendMessageW", CharSet = CharSet.Unicode)]
    private static extern nint SendTextW(nint window, uint message, nuint wparam, string text);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool RedrawWindow(nint window, nint rect, nint region, uint flags);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumChildWindows(nint window, EnumWindow callback, nint param);
    private delegate bool EnumWindow(nint window, nint param);

    internal static async Task Run(ExplorerApplication app, Func<Action, Task> ui,
        Func<Func<bool>, Task> until, string fixture)
    {
        static void Check(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
        }
        await ui(() =>
        {
            app.Left.Navigate(fixture);
            var options = app.State.Customization.Clone();
            options.ToolbarLabels = true;
            options.ToolbarCommands = ["customization", "new-tab", "back"];
            options.SidebarCommands = ["customization"];
            options.SidebarSections = ["bookmarks", "recents"];
            options.Animations = false;
            options.RowHeight = 42;
            options.FontSize = 16;
            options.Theme = "light";
            app.SetCustomization(options);
            if (!app.SecondPaneVisible) app.ToggleSplit();
        });
        await until(() => !app.Left.IsLoading && !app.Right.IsLoading && !app.Left.IsFiltering && !app.Right.IsFiltering);
        var editor = app.CustomizationEditor!;
        await ui(app.ShowCustomization);
        await until(() => editor.Row("font").Text!.GetBounds().Height > 20);
        nint hwnd = 0, searchPeer = 0, fontPeer = 0;
        int children = 0, rows = 0;
        float startingY = 0;
        string saved = "";
        await ui(() =>
        {
            editor.Row("font").Text!.Focus();
            fontPeer = GetFocus();
            Check(fontPeer != 0 && editor.Row("font").Text!.Focused, "The settings font editor did not receive focus.");
            SendTextW(fontPeer, 0x000c, 0, "Consolas draft");
            editor.SearchInput.Focus();
            searchPeer = GetFocus();
            hwnd = GetAncestor(searchPeer, 2);
            Check(hwnd != 0 && editor.SearchInput.Focused, "The settings search did not receive focus.");
            Check(EnumChildWindows(hwnd, (_, _) => { ++children; return true; }, 0), "Could not count native settings peers.");
            rows = editor.ResultCount;
            Check(rows > 100, "The scrolling fixture must include the complete command settings.");
            startingY = editor.Row("theme").Root.GetBounds().Y;
            saved = app.State.Customization.ToJson();
        });

        var dispatch = new List<double>();
        var painted = new List<double>();
        int movements = 0;
        for (int sample = -8; sample < 40; ++sample)
        {
            int index = sample;
            bool down = sample < 0 ? sample < -4 : sample < 20;
            await ui(() =>
            {
                var bounds = editor.Scroller.GetBounds();
                float scale = GetDpiForWindow(hwnd) / 96f;
                var point = new Point((int)((bounds.X + bounds.Width / 2) * scale),
                    (int)((bounds.Y + bounds.Height / 2) * scale));
                Check(ClientToScreen(hwnd, ref point), "Could not locate the settings viewport.");
                float before = editor.Row("theme").Root.GetBounds().Y;
                var watch = Stopwatch.StartNew();
                SendMessageW(hwnd, 0x020a, unchecked((uint)((down ? -120 : 120) << 16)),
                    (point.Y << 16) | (point.X & 0xffff));
                double handled = watch.Elapsed.TotalMilliseconds;
                // Include pending native painting so deferred work cannot masquerade as a faster scroll.
                Check(RedrawWindow(hwnd, 0, 0, 0x0180), "Could not flush the scrolling frame.");
                double frame = watch.Elapsed.TotalMilliseconds;
                float after = editor.Row("theme").Root.GetBounds().Y;
                Check(down ? after <= before : after >= before, "Settings moved in the wrong scroll direction.");
                if (index >= 0)
                {
                    dispatch.Add(handled);
                    painted.Add(frame);
                    if (Math.Abs(after - before) > 0.1f) ++movements;
                }
                Check(GetFocus() == searchPeer, "Scrolling stole native text focus.");
                Check(editor.Row("font").Text!.Text == "Consolas draft", "Scrolling discarded the font draft.");
            });
            await Task.Delay(20);
        }
        await ui(() =>
        {
            Check(movements >= 30, "The wheel benchmark did not move enough rows. Use normal line-based Windows wheel scrolling.");
            Check(Math.Abs(editor.Row("theme").Root.GetBounds().Y - startingY) < 1, "Round-trip scrolling did not return to the start.");
            Check(app.State.Customization.ToJson() == saved, "Scrolling saved an uncommitted draft.");
            editor.Row("font").Text!.Focus();
            Check(GetFocus() == fontPeer, "Scrolling replaced the native font editor.");
            editor.SearchInput.Focus();
            SendTextW(GetFocus(), 0x000c, 0, "Keyboard: New tab");
        });
        await until(() => editor.Row("key:new-tab").Text!.GetBounds().Height > 20);
        await ui(() =>
        {
            Check(editor.ResultCount == 1, "Search stopped working after scrolling.");
            editor.Row("key:new-tab").Text!.Text = "Ctrl+Shift+F12";
            editor.Row("key:new-tab").Save!.Invoke();
        });
        await until(() => app.State.Customization.Keybindings.TryGetValue("new-tab", out var bindings) &&
            bindings.SequenceEqual(["Ctrl+Shift+F12"]));
        await ui(() =>
        {
            editor.Dismiss();
            app.SetCustomization(new());
        });
        dispatch.Sort();
        painted.Sort();
        double dispatchMedian = (dispatch[19] + dispatch[20]) / 2;
        double paintedMedian = (painted[19] + painted[20]) / 2;
        Console.WriteLine($"Settings scroll: rows={rows}, native child windows={children}, samples={dispatch.Count}");
        Console.WriteLine($"Wheel handler ms: median={dispatchMedian:F2}, p95={dispatch[37]:F2}, max={dispatch[^1]:F2}");
        Console.WriteLine($"Wheel plus native paint ms: median={paintedMedian:F2}, p95={painted[37]:F2}, max={painted[^1]:F2}");
        Check(dispatchMedian < 16 && dispatch[37] < 32,
            "Settings wheel work exceeded the median 16 ms / p95 32 ms budget.");
        Check(paintedMedian < 32 && painted[37] < 64,
            "Settings wheel and painting exceeded the median 32 ms / p95 64 ms budget.");
    }
}
