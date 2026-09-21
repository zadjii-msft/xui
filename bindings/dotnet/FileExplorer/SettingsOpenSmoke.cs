using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Xui.FileExplorer;

internal static class SettingsOpenSmoke
{
    [DllImport("user32.dll")] private static extern nint GetFocus();
    [DllImport("user32.dll")] private static extern nint GetAncestor(nint window, uint flags);
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
            if (!app.SecondPaneVisible) app.ToggleSplit();
        });
        await until(() => !app.Left.IsLoading && !app.Left.IsFiltering && !app.Right.IsLoading && !app.Right.IsFiltering);
        var editor = app.CustomizationEditor!;
        nint hwnd = 0;
        int basePeers = 0, maximumAdded = 0;
        await ui(() =>
        {
            app.Left.Focus();
            hwnd = GetAncestor(GetFocus(), 2);
            Check(hwnd != 0, "The opening fixture needs a native Explorer window.");
            Check(RedrawWindow(hwnd, 0, 0, 0x0180), "Could not flush the initial frame.");
            Check(EnumChildWindows(hwnd, (_, _) => { ++basePeers; return true; }, 0), "Could not count initial peers.");
        });
        var opening = new List<double>();
        var painted = new List<double>();
        for (int sample = 0; sample < 8; ++sample)
        {
            int index = sample;
            await ui(() =>
            {
                int before = 0, after = 0;
                Check(EnumChildWindows(hwnd, (_, _) => { ++before; return true; }, 0), "Could not count native peers.");
                var watch = Stopwatch.StartNew();
                app.ShowCustomization();
                double dispatched = watch.Elapsed.TotalMilliseconds;
                Check(RedrawWindow(hwnd, 0, 0, 0x0180), "Could not paint the settings popup.");
                double frame = watch.Elapsed.TotalMilliseconds;
                Check(editor.IsOpen && editor.SearchInput.Focused && editor.Row("font").Text!.GetBounds().Height > 20,
                    "The measured popup must be visible, laid out, and ready for native input.");
                Check(EnumChildWindows(hwnd, (_, _) => { ++after; return true; }, 0), "Could not count settings peers.");
                maximumAdded = Math.Max(maximumAdded, after - before);
                Check(before <= basePeers + 4, "Closing settings leaked native controls between openings.");
                opening.Add(dispatched);
                painted.Add(frame);
                Console.WriteLine($"Settings open {index}: handler={dispatched:F2} ms, with paint={frame:F2} ms, peers={before}->{after}");
                var font = editor.Row("font").Text!;
                font.Focus();
                if (index == 0) SendTextW(GetFocus(), 0x000c, 0, "Uncommitted font draft");
                else Check(font.Text == "Uncommitted font draft",
                    "Reopening settings discarded its draft.");
            });
            await ui(editor.Dismiss);
            await Task.Delay(60);
        }
        var warm = opening.Skip(1).Order().ToArray();
        var frames = painted.Skip(1).Order().ToArray();
        Console.WriteLine($"Settings opening: first={opening[0]:F2} ms, warm median={warm[3]:F2} ms, warm max={warm[^1]:F2} ms");
        Console.WriteLine($"Settings opening with native paint: first={painted[0]:F2} ms, warm median={frames[3]:F2} ms, warm max={frames[^1]:F2} ms");
        Check(maximumAdded < 180, "Opening General created controls for hidden settings pages.");
        Check(opening[0] < 500 && warm[3] < 250 && warm[^1] < 500,
            "Settings opening exceeded the first/max 500 ms or warm median 250 ms budget.");
        Check(painted[0] < 650 && frames[3] < 350 && frames[^1] < 650,
            "Settings opening and native painting exceeded the first/max 650 ms or warm median 350 ms budget.");

        await ui(app.ShowCustomization);
        foreach (var page in new[] { SettingsPage.Toolbar, SettingsPage.Navigation, SettingsPage.Keyboard })
        {
            await ui(() => editor.SelectPage(page));
            string id = page switch
            {
                SettingsPage.Toolbar => "toolbar:new-tab",
                SettingsPage.Navigation => "section:recents",
                _ => "key:new-tab"
            };
            await until(() => editor.Row(id).Root.GetBounds().Height > 20);
        }
        await ui(() =>
        {
            var shortcut = editor.Row("key:new-tab").Text!;
            shortcut.Focus();
            Check(shortcut.Focused, "A deferred page did not create its native input.");
            SendTextW(GetFocus(), 0x000c, 0, "Ctrl+Shift+F12");
            editor.Row("key:new-tab").Save!.Invoke();
        });
        await until(() => app.State.Customization.Keybindings.TryGetValue("new-tab", out var value) &&
            value.SequenceEqual(["Ctrl+Shift+F12"]));
        await ui(() =>
        {
            editor.SearchInput.Focus();
            SendTextW(GetFocus(), 0x000c, 0, ":");
            Check(editor.ResultCount > 100, "Global search must still expose every settings page.");
        });
        await ui(() =>
        {
            Check(RedrawWindow(hwnd, 0, 0, 0x0180), "Could not paint all search results.");
            editor.SelectPage(SettingsPage.General);
            editor.Dismiss();
        });
        await Task.Delay(60);
        await ui(() =>
        {
            var watch = Stopwatch.StartNew();
            app.ShowCustomization();
            Check(RedrawWindow(hwnd, 0, 0, 0x0180), "Could not paint settings after visiting all pages.");
            double elapsed = watch.Elapsed.TotalMilliseconds;
            int peers = 0;
            Check(EnumChildWindows(hwnd, (_, _) => { ++peers; return true; }, 0), "Could not count reopened settings peers.");
            Console.WriteLine($"Settings reopen after all pages: with paint={elapsed:F2} ms, added peers={peers - basePeers}");
            Check(peers - basePeers < 180 && elapsed < 650,
                "Reopening General rebuilt previously visited hidden pages.");
            Check(editor.Row("font").Text!.Text == "Uncommitted font draft", "Page visits discarded the font draft.");
            editor.Dismiss();
            app.SetCustomization(new());
        });
    }
}
