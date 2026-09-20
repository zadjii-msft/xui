using System.Runtime.InteropServices;
using System.Diagnostics;
using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal enum ExplorerSmokeMode { Full, ViewSwitch, PaneAnimation, Hover, Views, Address, Partition, Preview }

internal static class ExplorerSmoke
{
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern nint GetFocus();
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern nint SetFocus(nint window);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern nint GetForegroundWindow();
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern nint SendMessageW(nint window, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll", EntryPoint = "SendMessageW", ExactSpelling = true, CharSet = CharSet.Unicode)]
    private static extern nint SendMessageTextW(nint window, uint message, nuint wparam, string text);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern bool PostMessageW(nint window, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll", ExactSpelling = true, CharSet = CharSet.Unicode)]
    private static extern nint FindWindowExW(nint parent, nint after, string? className, string? windowName);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern bool ClientToScreen(nint window, ref NativePoint point);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern uint GetDpiForWindow(nint window);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern nint GetAncestor(nint window, uint flags);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern nint GetWindow(nint window, uint command);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern bool IsWindow(nint window);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern bool IsWindowVisible(nint window);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern bool IsWindowEnabled(nint window);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern bool SystemParametersInfoW(uint action, uint parameter, out int value, uint flags);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern uint GetWindowThreadProcessId(nint window, out uint process);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern bool SetWindowPos(nint window, nint after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern bool GetWindowRect(nint window, out NativeRect rectangle);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern bool OpenClipboard(nint window);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern bool CloseClipboard();
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern nint GetClipboardData(uint format);
    [DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern nint GlobalLock(nint memory);
    [DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern bool GlobalUnlock(nint memory);
    [DllImport("user32.dll", ExactSpelling = true, CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextW(nint window, System.Text.StringBuilder text, int capacity);
    [StructLayout(LayoutKind.Sequential)]
    private struct NativePoint { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRect { public int Left, Top, Right, Bottom; }

    private static void CheckCommandSnapshot()
    {
        bool enabled = true;
        int evaluations = 0, executions = 0;
        var command = new ExplorerCommand("Snapshot command", "Ctrl+T", () => executions++,
            () => { evaluations++; return enabled; });
        var commands = new List<ExplorerCommand> { command };
        var rows = new CommandRows(commands);
        if (evaluations != 1 || executions != 0)
            throw new InvalidOperationException("Command rows must evaluate availability once before source callbacks, without executing actions.");
        enabled = false;
        commands.Clear();
        for (int i = 0; i < 3; i++)
            if (rows.Count != 1 || rows.Find(rows.Key(0)) != 0 ||
                rows.Item(0) != new ItemContent("Snapshot command", "Ctrl+T", true) ||
                evaluations != 1 || executions != 0)
                throw new InvalidOperationException("Command source callbacks must read only the captured row snapshot.");
        var refreshed = new CommandRows([command]);
        if (refreshed.Item(0).Enabled || evaluations != 2 || command.Enabled)
            throw new InvalidOperationException("A new snapshot and the live execution guard must see current command availability.");
        if (new CommandRows([]).Count != 0)
            throw new InvalidOperationException("An empty command snapshot must contain no rows.");
    }

    private static void ClickFirstTab(FilePaneView pane)
    {
        pane.Tabs.Focus();
        nint target = GetFocus();
        if (target == 0 || !pane.Tabs.Focused) throw new InvalidOperationException("The tab peer did not receive native focus.");
        pane.Address.Focus();
        SendMessageW(target, 0x0201, 1, (20 << 16) | 12);
        if (!pane.FilesFocused || pane.Tabs.Focused)
            throw new InvalidOperationException("A tab click must focus the file pane, not the tab strip.");
    }

    public static Task Start(ExplorerApplication app, ExplorerSmokeMode mode = ExplorerSmokeMode.Full)
    {
        return Run();
        async Task Run()
        {
            string fixture = Path.Combine(Environment.CurrentDirectory, $".xui-explorer-ui-{Guid.NewGuid():N}");
            try
            {
                CheckCommandSnapshot();
                Directory.CreateDirectory(Path.Combine(fixture, "alpha", "child"));
                Directory.CreateDirectory(Path.Combine(fixture, "beta"));
                await File.WriteAllTextAsync(Path.Combine(fixture, "small.txt"), "abc");
                await File.WriteAllTextAsync(Path.Combine(fixture, "large.txt"), new string('x', 4000));
                await Until(() => !app.Left.IsLoading && !app.Left.IsFiltering);
                if (mode == ExplorerSmokeMode.Preview)
                {
                    await PreviewChecks();
                    await DetachedLifetimeChecks();
                    Console.WriteLine("Explorer preview smoke passed: declarative metadata, text and image content, cancellation, native input, and opener-first lifetime.");
                    return;
                }
                if (mode == ExplorerSmokeMode.Partition)
                {
                    await PartitionChecks();
                    Console.WriteLine("Explorer partition smoke passed.");
                    await Ui(app.Window.Close);
                    return;
                }
                if (mode == ExplorerSmokeMode.Views)
                {
                    await AdditionalViewsChecks();
                    Console.WriteLine("Explorer views passed: gallery sizes, List, lazy Tree, compact Columns, view menu, filtering, selection, focus, tab state, and cancellation.");
                    await Ui(app.Window.Close);
                    return;
                }
                if (mode == ExplorerSmokeMode.Address)
                {
                    await AddressChecks();
                    Console.WriteLine("Explorer address smoke passed: compact chevrons, content-sized current folder, trailing-space palette activation, shortcuts, folder menus, cancellation, overflow, and pane isolation.");
                    await Ui(app.Window.Close);
                    return;
                }
                if (mode == ExplorerSmokeMode.ViewSwitch)
                {
                    await CreateViewportFixture();
                    await ViewSwitchChecks();
                    Console.WriteLine("Explorer view switches passed: no animation timer, stationary content and native peers, immediate ownership, retained selection/viewport, and rapid switching.");
                    await Ui(app.Window.Close);
                    return;
                }
                if (mode == ExplorerSmokeMode.PaneAnimation)
                {
                    await PaneAnimationChecks();
                    Console.WriteLine("Explorer pane animation passed: intermediate layout, retained native input, immediate closure ownership, and endpoint geometry.");
                    await Ui(app.Window.Close);
                    return;
                }
                if (mode == ExplorerSmokeMode.Hover)
                {
                    await HoverJoinChecks();
                    await Ui(app.Window.Close);
                    Console.WriteLine("Explorer hover smoke passed: Join, hosted reorder, Leave, repeated targets, both strips, Cancel, Drop, competing gestures, and source/target closure.");
                    return;
                }
                await Check(() => app.Window.Style == VisualStyle.WinUI, "Explorer uses the WinUI visual style");
                await Check(() => ReferenceEquals(app.Sidebar.View.Search.ControlStyle, ExplorerStyles.NavigationFilter)
                    && app.Sidebar.View.Search.GetControlStyleValues(StylePart.Root, effective: true).Background == new ThemeColor(0xF3F3F3, 0x202020)
                    && app.Sidebar.View.Search.GetControlStyleValues(StylePart.Root, effective: true).BorderThickness == new Insets(0, 0, 0, 1)
                    && app.Sidebar.View.Search.GetControlStyleValues(StylePart.Root, effective: true).CornerRadius == 0,
                    "Navigation filter blends into the sidebar with only a bottom border in both themes");
                await Check(() => new[] { app.Sidebar.View.Items, app.Sidebar.View.HeaderItems,
                    app.Sidebar.View.FooterItems }.All(items =>
                        ReferenceEquals(items.ControlStyle, ExplorerStyles.NavigationItems)
                        && items.GetControlStyleValues(StylePart.Root, effective: true).RowHeight == 28
                        && items.GetControlStyleValues(StylePart.Root, effective: true).FontSize == 12
                        && items.GetControlStyleValues(StylePart.Icon, effective: true).Size == 16),
                    "Navigation lists share compact row, text, and icon metrics");
                await Check(() => new[] { app.Window.TitlebarLeading, app.Left.Tabs.NewTabButton,
                    app.Right.Tabs.NewTabButton, app.Left.BackButton, app.Right.BackButton }.All(button =>
                        ReferenceEquals(button.Style, ExplorerStyles.IconButton)
                        && button.EffectiveStyleValues.Background == new ThemeColor(0xF3F3F3, 0x202020)
                        && button.EffectiveStyleValues.BorderThickness == new Insets(0)),
                    "Header icons share a borderless style with the window background in both themes");
                await Ui(() => Shortcut(0x75, KeyModifiers.Control));
                await Check(() => app.Window.Style == VisualStyle.WinUI, "Light theme retains the WinUI visual style");
                await Ui(() => Shortcut(0x75, KeyModifiers.Control));
                await Check(() => app.Window.Style == VisualStyle.WinUI, "Dark theme retains the WinUI visual style");
                await Check(() => !ReferenceEquals(app.Left.Root, app.Right.Root)
                    && !ReferenceEquals(app.Left.Grid, app.Right.Grid)
                    && !ReferenceEquals(app.Left.FindInput, app.Right.FindInput)
                    && app.Left.BackButton.Icon == ButtonIcon.Back && app.Right.BackButton.Icon == ButtonIcon.Back,
                    "Declarative components create independent pane controls");
                await Ui(() => app.Left.Navigate(fixture));
                await Ready(app.Left);
                await Check(() => app.Left.VisibleCount == 4, "Folder rows");
                await Check(() => app.Left.Model.Active.Path == fixture, "Committed address");
                await AddressChecks();
                await CaptionCheck(app.Left);
                await NavigationMenuChecks();
                await AdditionalViewsChecks();
                await TypeToFindChecks(app.Left);
                await RevealChecks(app.Left);
                await RevealChecks(app.Left, animate: false);
                await Ui(() =>
                {
                    app.Left.Focus();
                    app.Left.Grid.Navigate(GridNavigation.First);
                });
                await CommandPaletteChecks();
                await PreviewChecks();
                await Ui(() =>
                {
                    ulong identity = 0;
                    var details = new FileRows(app.Left.Model.Active.Entries, _ => ++identity);
                    var suggestions = new FileRows(app.Left.Model.Active.Entries, _ => ++identity, suggestions: true);
                    for (ulong index = 0; index < details.Count; ++index)
                    {
                        string path = app.Left.Model.Active.Entries[(int)index].FullPath;
                        if (details.Item(index).ImagePath != path || suggestions.Item(index).ImagePath != path
                            || !string.IsNullOrEmpty(details.Item(index, 1).ImagePath))
                            throw new InvalidOperationException("File visuals must use the full path only in the name column and palette rows.");
                    }
                });
                await Until(() => app.Window.TitlebarTabs.GetBounds().X == app.Left.Root.GetBounds().X);
                await Check(() => app.Window.TitlebarTabs.GetBounds().Y + app.Window.TitlebarTabs.GetBounds().Height
                    == app.Left.Root.GetBounds().Y, "The selected title tab joins the pane without a bottom gap");
                await Check(() => app.Sidebar.View.Search.GetBounds().Y - app.Sidebar.View.GetBounds().Y == 4,
                    "Navigation has no title header");
                await Ui(app.Sidebar.Toggle);
                await Until(() => !app.Sidebar.Presentation.Animating && app.Sidebar.Presentation.GetBounds().Width == 0);
                await Check(() => app.Left.Root.GetBounds().X == 0 && app.Window.TitlebarTabs.GetBounds().X == 44
                    && app.Window.TitlebarLeading.GetBounds().X == 0 && !app.Sidebar.View.Search.Focused,
                    "Hidden navigation has no rail and leaves the hamburger before the first tab");
                await Ui(() => Shortcut(0x46, KeyModifiers.Alt));
                await Until(() => !app.Sidebar.Presentation.Animating && app.Sidebar.Presentation.GetBounds().Width > 0);
                await Check(() => app.Sidebar.IsOpen && app.Window.TitlebarTabs.GetBounds().X == app.Left.Root.GetBounds().X,
                    "Alt+F restores navigation and aligned tabs");
                await NavigationAnimationChecks();
                await NavigationGroupChecks();
                await PaneAnimationChecks();
                await TabAnimationChecks();
                await TabOverflowChecks();
                await Ui(() => ClickFirstTab(app.Left));
                await NewTabButtonChecks(app.Left);

                float unfilteredHeight = 0;
                await Ui(() =>
                {
                    unfilteredHeight = app.Left.Grid.GetBounds().Height;
                    Shortcut(0x46, KeyModifiers.Control);
                    app.Left.SetFilter("small");
                });
                await Ready(app.Left);
                await Check(() => app.Left.VisibleCount == 1 && app.Left.Model.Active.FindOpen, "Pane-local find");
                await Until(() => FindFits(app.Left));
                await Check(() => FindFits(app.Left) && app.Left.FindInput.Focused
                    && app.Left.Grid.GetBounds().Height == unfilteredHeight - 56,
                    "Find reserves one complete row instead of clipping a scrolling container");
                await Ui(() => Shortcut(0x54, KeyModifiers.Control));
                await Ready(app.Left);
                await Check(() => app.Left.Model.Tabs.Count == 2 && app.Left.VisibleCount == 4, "Independent new tab");
                await Ui(() => ClickFirstTab(app.Left));
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Filter == "small" && app.Left.Grid.Focused,
                    "A pointer tab switch restores its state and focuses its file grid");
                await Ui(() => Shortcut(0x09, KeyModifiers.Control));
                await Ready(app.Left);
                await Ui(() => Shortcut(0x09, KeyModifiers.Control | KeyModifiers.Shift));
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Filter == "small" && app.Left.VisibleCount == 1, "Restored tab filter");
                await Until(() => FindFits(app.Left));
                await Check(() => FindFits(app.Left) && app.Left.FindInput.Text == "small", "Restored Find has a complete input row");
                await Ui(app.Left.CloseFindButton.Invoke);
                await Ready(app.Left);
                await Until(() => !app.Left.FindReveal.Animating && app.Left.FindBounds.Height == 0);
                await Check(() => !app.Left.Model.Active.FindOpen && app.Left.FindBounds.Height == 0
                    && app.Left.VisibleCount == 4 && app.Left.Grid.GetBounds().Height == unfilteredHeight
                    && app.Left.Grid.Focused, "X clears Find, restores row space, and returns focus to the files");

                await Ui(() => { app.Left.ShowFind(); app.Left.SetFilter(".txt"); });
                await Ready(app.Left);
                ItemKey? firstFindKey = null;
                await Ui(() =>
                {
                    app.Left.Grid.Navigate(GridNavigation.First);
                    firstFindKey = app.Left.Grid.Selection.Focused;
                    if (!PostMessageW(GetFocus(), 0x100, 0x28, 0))
                        throw new InvalidOperationException("Could not post Down to the native Find editor.");
                });
                await Until(() => app.Left.SelectedEntry?.Name == "small.txt");
                await Check(() => app.Left.FindInput.Focused && app.Left.FindInput.Text == ".txt",
                    "Down from native Find moves list selection without changing the query or focus");
                await Ui(() => Shortcut(0x26));
                await Check(() => app.Left.SelectedEntry?.Name == "large.txt", "Find Up selects the previous filtered file");
                await Ui(() => Shortcut(0x22));
                await Check(() => app.Left.SelectedEntry?.Name == "small.txt", "Find PageDown reaches the last filtered file");
                await Ui(() => Shortcut(0x21));
                await Check(() => app.Left.SelectedEntry?.Name == "large.txt", "Find PageUp reaches the first filtered file");
                await Ui(() =>
                {
                    Shortcut(0x28, KeyModifiers.Shift);
                    if (firstFindKey is not { } firstKey || !app.Left.Grid.Contains(firstKey) ||
                        app.Left.Grid.Selection.Focused is not { } lastKey || firstKey == lastKey || !app.Left.Grid.Contains(lastKey))
                        throw new InvalidOperationException("Find Shift+Down must extend the file selection.");
                    Shortcut(0x24, KeyModifiers.Control);
                });
                await Check(() => app.Left.SelectedEntry?.Name == "large.txt", "Find Ctrl+Home selects the first file");
                await Ui(() => Shortcut(0x23, KeyModifiers.Control));
                await Check(() => app.Left.SelectedEntry?.Name == "small.txt" && app.Left.FindInput.Focused,
                    "Find Ctrl+End selects the last file without leaving the editor");
                await Ui(() =>
                {
                    foreach (uint key in new uint[] { 0x25, 0x27, 0x24, 0x23 })
                        if (app.Window.KeyHandler?.Invoke(new(key, KeyModifiers.None, app.Left.FindInput.Id)) == true)
                            throw new InvalidOperationException("Left, Right, Home and End must remain native query-editing keys.");
                    app.Left.SetFilter("no matching files");
                });
                await Ready(app.Left);
                await Ui(() => { Shortcut(0x28); Shortcut(0x21); Shortcut(0x23, KeyModifiers.Control); });
                await Check(() => app.Left.VisibleCount == 0 && app.Left.SelectedEntry is null && app.Left.FindInput.Focused,
                    "Find navigation safely handles no matches");
                await Ui(app.Left.HideFind);
                await Ready(app.Left);

                await Ui(() =>
                {
                    app.Left.ShowFind();
                    app.Left.SetFilter("small");
                });
                await Ready(app.Left);
                await Ui(app.Left.Refresh);
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Filter == "small" && app.Left.VisibleCount == 1,
                    "Refresh preserves the folder filter");
                await Ui(() => app.Left.Navigate(Path.Combine(fixture, "missing-filter")));
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Filter == "small" && app.Left.VisibleCount == 1 && app.Left.Error is not null,
                    "Failed navigation preserves the folder filter");
                await Ui(() =>
                {
                    app.Window.SetIconSource("");
                    nint hwnd = GetAncestor(GetFocus(), 2);
                    if (SendMessageW(hwnd, 0x7f, 0, 0) != 0 || SendMessageW(hwnd, 0x7f, 1, 0) != 0)
                        throw new InvalidOperationException("Clearing the window icon must clear both HWND icon slots.");
                    app.Left.Navigate(Path.Combine(fixture, "alpha"));
                });
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Filter == "" && app.Left.FindInput.Text == ""
                    && app.Left.Model.Active.FindOpen && app.Left.VisibleCount == 1,
                    "Successful folder navigation clears the model and native Find text");
                await CaptionCheck(app.Left);
                await Ui(() => MouseTravel(app.Left.Grid, NavigationDirection.Back));
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Path == fixture, "Mouse Back navigates the file pane");
                await Ui(app.Left.HideFind);
                await Ready(app.Left);
                await Ui(() => MouseTravel(app.Left.Grid, NavigationDirection.Forward));
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Path == Path.Combine(fixture, "alpha"), "Mouse Forward restores the next location");
                await Ui(() => MouseTravel(app.Left.Grid, NavigationDirection.Back));
                await Ready(app.Left);

                await Ui(() => app.Left.Navigate(Path.Combine(fixture, "missing")));
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Path == fixture && app.Left.VisibleCount == 4 && app.Left.Error is not null,
                    "Failed navigation preserves committed view");
                await DriveNavigationChecks(fixture);

                await Ui(() => Shortcut(0x47, KeyModifiers.Control));
                await Until(() => !app.Palettes.Pending);
                await Check(() => app.Palettes.IsOpen && app.Palettes.QueryText == fixture + Path.DirectorySeparatorChar && app.Palettes.ResultCount == 4,
                    "Navigation palette opens at CWD");
                await Ui(() =>
                {
                    if (SendMessageW(GetFocus(), 0x319, (nuint)GetFocus(), 1 << 16) != 1 ||
                        app.Left.IsLoading || !app.Palettes.IsOpen || app.Left.Model.Active.Path != fixture)
                        throw new InvalidOperationException("Mouse/browser navigation must not change the folder behind an open palette.");
                });
                ElementBounds firstPaletteBounds = default;
                await Ui(() => firstPaletteBounds = app.Palettes.Bounds);
                await Check(() => Math.Abs(firstPaletteBounds.X + firstPaletteBounds.Width / 2
                    - (app.Left.Root.GetBounds().X + app.Left.Root.GetBounds().Width) / 2) < 1
                    && app.Palettes.StatusHeight == 0, "Palette centers in the window without a redundant result-count footer");
                await Ui(() =>
                {
                    foreach (var (query, count) in new[] { ("al", 2), ("alp", 1), ("alpha", 1), ("AL", 2), ("nothing-matches", 0), ("", 4) })
                    {
                        app.Palettes.EditQuery(query.Length == 0 ? fixture + Path.DirectorySeparatorChar : Path.Combine(fixture, query));
                        if (app.Palettes.Pending || app.Palettes.ResultCount != count)
                            throw new InvalidOperationException("Cached suggestions must update synchronously without an empty intermediate view.");
                    }
                    Shortcut(0x09);
                    if (!app.Palettes.Pending || app.Palettes.ResultCount != 0)
                        throw new InvalidOperationException("A cold folder query must retire stale logical rows before its scan completes.");
                    app.Palettes.Accept(false);
                    if (!app.Palettes.IsOpen || app.Left.Model.Active.Path != fixture)
                        throw new InvalidOperationException("Pending suggestions must not activate stale rows.");
                });
                await Until(() => !app.Palettes.Pending);
                await Check(() => app.Palettes.QueryText == Path.Combine(fixture, "alpha") + Path.DirectorySeparatorChar
                    && app.Left.Model.Active.Path == fixture && app.Palettes.ResultCount == 1, "Tab completes without navigating");
                await Check(() => app.Palettes.QuerySelection == new TextSelection((ulong)app.Palettes.QueryText.Length,
                    (ulong)app.Palettes.QueryText.Length), "Completion leaves the caret at the end of the path");
                await Ui(() => Shortcut(0x25, KeyModifiers.Alt));
                await Until(() => !app.Palettes.Pending);
                await Ui(() =>
                {
                    if (app.Palettes.QueryText != fixture + Path.DirectorySeparatorChar)
                        throw new InvalidOperationException(
                            $"Palette history works without a Back button: query={app.Palettes.QueryText}, " +
                            $"expected={fixture + Path.DirectorySeparatorChar}, open={app.Palettes.IsOpen}, " +
                            $"pending={app.Palettes.Pending}, folder={app.Left.Model.Active.Path}.");
                });
                await Ui(() => Shortcut(0x27, KeyModifiers.Alt));
                await Until(() => !app.Palettes.Pending);
                await Check(() => app.Palettes.QueryText == Path.Combine(fixture, "alpha") + Path.DirectorySeparatorChar,
                    "Palette history works without a Forward button");
                await Ui(() => app.Palettes.EditQuery(Path.Combine(fixture, "alpha")));
                await Until(() => !app.Palettes.Pending);
                await Ui(() => Shortcut(0x0d));
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Path == Path.Combine(fixture, "alpha") && !app.Palettes.IsOpen,
                    "Enter on an exact directory without a slash opens that directory, not its first child");
                await Ui(() => app.Palettes.ShowNavigation(app.Left));
                await Until(() => !app.Palettes.Pending);
                await Ui(() => Shortcut(0x26, KeyModifiers.Alt));
                await Until(() => !app.Palettes.Pending);
                await Check(() => app.Palettes.QueryText == fixture + Path.DirectorySeparatorChar && app.Palettes.ResultCount == 4,
                    "Parent query appends a slash and lists the parent directory's children");
                await Ui(() => app.Palettes.EditQuery(Path.Combine(fixture, "alpha") + "/"));
                await Until(() => !app.Palettes.Pending);
                await Ui(() => Shortcut(0x0d));
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Path == Path.Combine(fixture, "alpha", "child") && !app.Palettes.IsOpen,
                    "Enter navigates selected folder");

                await Ui(() =>
                {
                    app.Palettes.ShowNavigation(app.Left);
                    app.Palettes.EditQuery(Path.Combine(fixture, "alpha") + Path.DirectorySeparatorChar);
                    app.Palettes.EditQuery(Path.Combine(fixture, "beta") + Path.DirectorySeparatorChar);
                });
                await Until(() => !app.Palettes.Pending);
                await Check(() => app.Palettes.QueryText == Path.Combine(fixture, "beta") + Path.DirectorySeparatorChar && app.Palettes.ResultCount == 0,
                    "Only the latest folder suggestions replace the displayed rows");
                await Ui(app.Palettes.Dismiss);

                await Ui(() => app.Palettes.ShowNavigation(app.Left));
                await Ui(() => app.Palettes.EditQuery(Path.Combine(fixture, "et")));
                await Until(() => !app.Palettes.Pending);
                await Check(() => app.Palettes.ResultCount == 1, "Partial path substring suggestions");
                await Ui(() => app.Palettes.Accept(true));
                await Ready(app.Right);
                await Check(() => app.Right.Model.Active.Path == Path.Combine(fixture, "beta")
                    && app.Left.Model.Active.Path == Path.Combine(fixture, "alpha", "child"), "Ctrl+Enter targets other split");
                await Until(() => app.Window.TitlebarSecondaryTabs.GetBounds().X == app.Right.Root.GetBounds().X);
                await Check(() => app.Window.TitlebarSecondaryTabs.GetBounds().Y + app.Window.TitlebarSecondaryTabs.GetBounds().Height
                    == app.Right.Root.GetBounds().Y, "The secondary title tabs join their pane");
                await Check(() => app.Window.TitlebarTabs.GetBounds().X == app.Left.Root.GetBounds().X
                    && app.Window.TitlebarSecondaryTabs.GetBounds().Width > 0, "Each split has a pane-aligned tab band");
                await Ui(() => ClickFirstTab(app.Right));
                await NewTabButtonChecks(app.Right);
                await Ui(() => app.Right.Navigate(Path.Combine(fixture, "alpha")));
                await Ready(app.Right);
                await Ui(() =>
                {
                    app.Right.ShowFind();
                    MouseTravel(app.Right.FindInput, NavigationDirection.Back, app.Left.Focus);
                });
                await Ready(app.Right);
                await Check(() => ReferenceEquals(app.Active, app.Right) && app.Right.Model.Active.Path == Path.Combine(fixture, "beta")
                    && app.Left.Model.Active.Path == Path.Combine(fixture, "alpha", "child"),
                    "Mouse Back over the inactive pane's native Find editor activates only that pane");
                await CaptionCheck(app.Left);
                await CaptionCheck(app.Right);
                await Ui(app.Right.HideFind);
                await Ready(app.Right);
                await Ui(() => app.Palettes.ShowNavigation(app.Right));
                await Check(() => app.Palettes.Bounds == firstPaletteBounds && app.Palettes.StatusHeight > 0,
                    "Second-pane navigation uses the same centered bounds and retains empty-state feedback");
                await Ui(app.Palettes.Dismiss);

                await Ui(() => Shortcut(0x50, KeyModifiers.Control | KeyModifiers.Shift));
                await Check(() => app.Palettes.Bounds == firstPaletteBounds && app.Palettes.StatusHeight == 0,
                    "Second-pane command palette uses the same centered bounds");
                await Ui(() => app.Palettes.EditQuery("new tab"));
                await Check(() => app.Palettes.ResultCount == 2, "Filtered command palette");
                await Ui(() => app.Palettes.Accept(false));
                await Ready(app.Right);
                await Check(() => app.Right.Model.Tabs.Count == 2 && app.Left.Model.Tabs.Count == 2, "Commands target active pane");
                await Ui(() => app.Right.Navigate(fixture));
                await Ready(app.Right);
                await Ui(() => { app.Right.Focus(); Shortcut(0x46, KeyModifiers.Control); app.Right.SetFilter("alpha"); });
                await Ready(app.Right);
                await Until(() => FindFits(app.Right) && !app.Left.FindReveal.Animating && app.Left.FindBounds.Height == 0);
                await Check(() => FindFits(app.Right) && app.Right.VisibleCount == 1
                    && app.Left.Model.Active.Filter == "" && app.Left.FindBounds.Height == 0,
                    "Second-pane Find fits its split without changing the first pane");
                await Ui(() => Shortcut(0x1b));
                await Ready(app.Right);
                await Until(() => !app.Right.FindReveal.Animating && app.Right.FindBounds.Height == 0);
                await Check(() => !app.Right.Model.Active.FindOpen && app.Right.FindBounds.Height == 0
                    && app.Right.VisibleCount == 4 && app.Right.Grid.Focused, "Escape closes second-pane Find");

                await Ui(() =>
                {
                    app.Sidebar.FocusFilter();
                    app.Sidebar.View.Search.Text = "Documents";
                    app.Sidebar.Toggle();
                    app.Sidebar.Toggle();
                    app.Left.Grid.SetColumnWidth(0, 330);
                    app.Left.Grid.SetSort(3, true);
                });
                await Ready(app.Left);
                string many = await CreateViewportFixture();
                await Ui(() => { app.Left.Focus(); app.Left.Navigate(many); });
                await Ready(app.Left);
                string selectedPath = Path.Combine(many, "item-015.txt");
                ulong sourceTabId = 0;
                await Ui(() =>
                {
                    sourceTabId = app.Left.Model.Active.Id;
                    app.Left.SelectPath(selectedPath);
                    app.Left.Grid.Offset = 320;
                    app.Left.NewTab(fixture);
                });
                await Ready(app.Left);
                await Ui(() => app.Left.SelectTab(sourceTabId));
                await Ready(app.Left);
                await Check(() => app.Left.SelectedEntry?.FullPath == selectedPath && app.Left.Grid.Offset == 320,
                    "Tab refresh preserves selected path and scroll offset");
                await Ui(() => app.Left.Navigate(fixture));
                await Ready(app.Left);
                await Ui(() =>
                {
                    var emptyMenu = app.Left.ContextMenu.GetCommands();
                    if (!emptyMenu.Select(c => c.Id).Order().SequenceEqual(new[] { FileContextMenu.Refresh, FileContextMenu.Paste }.Order())
                        || app.Left.ContextMenu.GetShellPaths().Length != 0)
                        throw new InvalidOperationException("Empty-area menus must not target an old selection.");
                    app.Left.SelectPath(Path.Combine(fixture, "small.txt"));
                    var fileMenu = app.Left.ContextMenu.GetCommands();
                    if (fileMenu.Any(c => c.Id == FileContextMenu.Open || c.Id == FileContextMenu.NewTab)
                        || !app.Left.ContextMenu.GetShellPaths().SequenceEqual([Path.Combine(fixture, "small.txt")]))
                        throw new InvalidOperationException("File menus must use Shell verbs without duplicate Open or folder-only commands.");
                    app.Left.SelectPath(Path.Combine(fixture, "alpha"));
                    app.Left.ContextMenu.GetCommands();
                    app.Left.SelectPath(Path.Combine(fixture, "beta"));
                    if (!app.Left.ContextMenu.GetShellPaths().SequenceEqual([Path.Combine(fixture, "alpha")]))
                        throw new InvalidOperationException("Shell paths must retain the same snapshot as the app commands.");
                    app.Left.ContextMenu.Invoke(FileContextMenu.Bookmark);
                    if (!app.State.Bookmarks.Contains(Path.Combine(fixture, "alpha"))
                        || app.State.Bookmarks.Contains(Path.Combine(fixture, "beta")))
                        throw new InvalidOperationException("A menu command must retain the item clicked when the menu opened.");
                    app.Left.SelectPath(Path.Combine(fixture, "alpha"));
                    app.Left.ContextMenu.GetCommands();
                    app.Left.ContextMenu.Invoke(FileContextMenu.NewTab);
                });
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Path == Path.Combine(fixture, "alpha"), "Context menu opens a folder in a new tab");
                await Ui(() =>
                {
                    app.Left.Navigate(Path.Combine(fixture, "alpha"));
                    app.Left.Navigate(Path.Combine(fixture, "beta"));
                });
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Path == Path.Combine(fixture, "beta"), "Latest navigation wins");
                await ViewSwitchChecks();
                await ColumnsChecks();
                await PartitionChecks();
                await PerColumnFindChecks();
                await Transfers(fixture);
                await FeedbackChecks();
                await HoverJoinChecks();
                await TabDragChecks();
                await TabMenuChecks(fixture);
                await DetachedLifetimeChecks();
                Console.WriteLine("Explorer smoke passed: navigation, completion, panes, tabs, tab menus, tab drag handlers, reversible hover joins, tear-out rollback, same-Application merge, filtering, sorting, partitions, columns, commands, detached previews, opener-first lifetime, native copy, image reuse, and file transfers.");
            }
            catch (Exception error)
            {
                if (!app.Application.Post(() => throw new InvalidOperationException("Explorer UI smoke failed.", error))) throw;
            }
            finally
            {
                if (Directory.Exists(fixture)) Directory.Delete(fixture, recursive: true);
            }

            async Task AddressChecks()
            {
                void ClickFirstFolder()
                {
                    nint target = GetFocus();
                    int point = (int)(12 * GetDpiForWindow(target) / 96);
                    SendMessageW(target, 0x0201, 1, (point << 16) | point);
                    SendMessageW(target, 0x0202, 0, (point << 16) | point);
                }
                var pane = app.Left;
                var bar = pane.AddressBar;
                await Ui(() => pane.Navigate(fixture));
                await Ready(pane);
                await Check(() => bar.Segments.Count > 1 && bar.Segments[^1].Path == fixture,
                    "Breadcrumbs reflect the committed folder");
                await Ui(() =>
                {
                    var segments = bar.Segments.Select(segment => segment.Name).ToArray();
                    var style = segments[0].Style;
                    var space = bar.TrailingSpace;
                    var spaceStyle = space.ControlStyle;
                    app.Right.AddressBar.SetPath(app.Right.Model.Active.Path);
                    if (style is null || segments.Any(button => !ReferenceEquals(button.Style, style))
                        || !ReferenceEquals(app.Right.AddressBar.Segments[0].Name.Style, style)
                        || space.GetControlStyleValues(StylePart.Icon, effective: true).Size != 0)
                        throw new InvalidOperationException("Declarative breadcrumbs must share styles across segments and panes and hide the trailing icon.");
                    bar.SetPath(fixture);
                    if (!segments.SequenceEqual(bar.Segments.Select(segment => segment.Name))
                        || !ReferenceEquals(space, bar.TrailingSpace))
                        throw new InvalidOperationException("An unchanged path must retain its controls without reconstructing declarative components.");
                    string deep = Path.GetPathRoot(fixture)! + string.Join(Path.DirectorySeparatorChar, Enumerable.Repeat("folder", 80));
                    bar.SetPath(deep);
                    if (bar.Segments.Count != 64 || bar.Segments.Any(segment => !ReferenceEquals(segment.Name.Style, style)))
                        throw new InvalidOperationException("Declarative path rebuilds must retain the 64-segment bound and reuse the cached style.");
                    bar.SetPath(fixture);
                    if (!ReferenceEquals(bar.TrailingSpace.ControlStyle, spaceStyle))
                        throw new InvalidOperationException("Declarative trailing-space styles must be shared across path replacements.");
                });
                foreach (string folder in new[] { "dev", "WWW", "\u8cc7\u6599" })
                {
                    string parent = Path.Combine(Path.GetPathRoot(fixture)!, folder);
                    await Ui(() => bar.SetPath(parent));
                    await Until(() => bar.Segments[^1].Name.GetBounds().Width > 0);
                    float naturalWidth = 0;
                    await Ui(() => naturalWidth = bar.Segments[^1].Name.GetBounds().Width);
                    await Ui(() => bar.SetPath(Path.Combine(parent, "bin", "terminal-releases")));
                    await Until(() => bar.Segments.Single(segment => segment.Path == parent).Name.GetBounds().Width > 0);
                    await Check(() => bar.Segments.Single(segment => segment.Path == parent).Name.GetBounds().Width >= naturalWidth,
                        $"Ancestor '{folder}' retains its measured name width instead of a character-count estimate");
                }
                await Ui(() => bar.SetPath(fixture));
                await Ui(() => Shortcut(0x4c, KeyModifiers.Control));
                await Until(() => !app.Palettes.Pending);
                await Check(() => app.Palettes.IsOpen && app.Palettes.QueryText == fixture + Path.DirectorySeparatorChar,
                    "Ctrl+L opens the existing navigation palette");
                await Ui(() =>
                {
                    SendMessageTextW(GetFocus(), 0x000c, 0, "draft \u8cc7\U0001f600");
                    if (app.Palettes.QueryText != "draft \u8cc7\U0001f600")
                        throw new InvalidOperationException("The navigation palette must retain native Unicode text input.");
                    Shortcut(0x1b);
                });
                await Check(() => !app.Palettes.IsOpen && pane.Model.Active.Path == fixture,
                    "Escape closes the palette without navigating");
                await Ui(() => Shortcut(0x44, KeyModifiers.Alt));
                await Check(() => app.Palettes.IsOpen, "Alt+D opens the navigation palette");
                await Ui(() =>
                {
                    app.Palettes.Dismiss();
                    pane.Navigate(Path.Combine(fixture, "alpha"));
                });
                await Ready(pane);
                float currentWidth = 0;
                await Ui(() => currentWidth = bar.Segments[^1].Name.GetBounds().Width);
                await Check(() => currentWidth is > 0 and < 80 && bar.TrailingSpace.GetBounds().Width > 0
                    && bar.Segments.All(segment => segment.Children.Icon == ButtonIcon.ChevronRight)
                    && bar.Segments[^1].Name.EffectiveStyleValues.Padding == new Insets(2, 0, 2, 0),
                    "Short current names use their measured width, compact padding, and right chevrons");
                await Ui(() => bar.Root.MaximumSize(400, 36));
                await Until(() => bar.Root.GetBounds().Width <= 400);
                await Check(() => bar.Segments[^1].Name.GetBounds().Width == currentWidth,
                    "The current name does not expand with the address slot");
                await Ui(() => bar.Root.MaximumSize(float.MaxValue, float.MaxValue));
                await Until(() => bar.TrailingSpace.GetBounds().Width > 0);
                await Ui(() =>
                {
                    bar.TrailingSpace.Focus();
                    nint target = GetFocus();
                    SendMessageW(target, 0x0201, 1, (12 << 16) | 4);
                    SendMessageW(target, 0x0202, 0, (12 << 16) | 4);
                });
                await Until(() => app.Palettes.IsOpen && !app.Palettes.Pending);
                await Check(() => app.Palettes.QueryText == Path.Combine(fixture, "alpha") + Path.DirectorySeparatorChar,
                    "Clicking past the final segment opens the palette for that pane");
                await Ui(app.Palettes.Dismiss);
                await Ui(() => bar.Segments.Single(segment => segment.Path == fixture).Name.Invoke());
                await Until(() => pane.Model.Active.Path == fixture && !pane.IsLoading && !pane.IsFiltering);
                await Ui(() => bar.Segments[^1].Children.Invoke());
                await Until(() => bar.MenuOpen && !bar.Pending);
                await Check(() => bar.FolderCount == 2 && pane.Model.Active.Path == fixture,
                    "The separator lists child folders without files or implicit navigation");
                await Ui(ClickFirstFolder);
                await Until(() => pane.Model.Active.Path == Path.Combine(fixture, "alpha") && !pane.IsLoading && !pane.IsFiltering);
                await Check(() => !bar.MenuOpen && pane.FilesFocused,
                    "One click activates the already-selected first folder and closes its flyout");
                await Ui(() => pane.Navigate(fixture));
                await Ready(pane);
                await Ui(() => bar.Segments[^1].Children.Invoke());
                await Until(() => bar.MenuOpen && !bar.Pending);
                await Ui(() => SendMessageW(GetFocus(), 0x0100, 0x28, 1));
                await Check(() => bar.MenuOpen && pane.Model.Active.Path == fixture,
                    "Arrow selection does not activate a flyout folder");
                await Ui(() => SendMessageW(GetFocus(), 0x0100, 0x26, 1));
                await Ui(() =>
                {
                    if (!PostMessageW(GetFocus(), 0x0100, 0x0d, 1))
                        throw new InvalidOperationException("Could not activate a folder with native Enter.");
                });
                await Until(() => pane.Model.Active.Path == Path.Combine(fixture, "alpha") && !pane.IsLoading && !pane.IsFiltering);
                await Check(() => !bar.MenuOpen && pane.FilesFocused, "Folder activation closes the menu and focuses its pane");
                await Ui(() => pane.Navigate(fixture));
                await Ready(pane);
                await Ui(() => bar.Segments[^1].Name.Invoke());
                await Until(() => app.Palettes.IsOpen);
                await Check(() => app.Palettes.QueryText == fixture + Path.DirectorySeparatorChar,
                    "The current folder name opens the navigation palette");
                await Ui(() => Shortcut(0x1b));
                await Ui(() =>
                {
                    var name = bar.Segments[^1].Name;
                    name.Focus();
                    if (app.Window.KeyHandler?.Invoke(new(0x28, KeyModifiers.None, name.Id)) != true)
                        throw new InvalidOperationException("Down must open the focused breadcrumb's folder menu.");
                });
                await Until(() => bar.MenuOpen && !bar.Pending);
                await Ui(() => Shortcut(0x1b));
                await Check(() => !bar.MenuOpen, "Escape dismisses the folder dropdown");
                await Ui(() =>
                {
                    bar.ShowChildren(Path.Combine(fixture, "missing"));
                });
                await Until(() => !bar.Pending);
                await Check(() => bar.MenuOpen && bar.FolderCount == 0 && bar.MenuMessage.StartsWith("Cannot list folders:"),
                    "Folder enumeration errors remain explicit");
                await Ui(() => bar.ShowChildren(Path.Combine(fixture, "beta")));
                await Until(() => !bar.Pending);
                await Check(() => bar.FolderCount == 0 && bar.MenuMessage == "No subfolders.",
                    "An empty directory is distinct from a failed enumeration");
                await Ui(() =>
                {
                    bar.ShowChildren(fixture);
                    bar.ShowChildren(Path.Combine(fixture, "beta"));
                });
                await Until(() => !bar.Pending);
                await Task.Delay(100);
                await Check(() => bar.FolderCount == 0 && bar.MenuMessage == "No subfolders.",
                    "A canceled folder request cannot replace a newer result");
                await Ui(() =>
                {
                    bar.ShowChildren(fixture);
                    bar.DismissMenu();
                });
                await Task.Delay(100);
                await Check(() => !bar.Pending && !bar.MenuOpen, "Dismissal cancels late folder results");
                await Ui(bar.ShowAncestors);
                await Check(() => bar.MenuOpen && bar.FolderCount == BreadcrumbPath.Create(fixture).Count,
                    "Ancestor overflow retains every location");
                await Ui(ClickFirstFolder);
                await Until(() => pane.Model.Active.Path == BreadcrumbPath.Create(fixture)[0].Path && !pane.IsLoading && !pane.IsFiltering);
                await Check(() => !bar.MenuOpen, "One click activates an ancestor and closes its flyout");
                await Ui(() => pane.Navigate(fixture));
                await Ready(pane);
                await Ui(() =>
                {
                    bar.DismissMenu();
                    bar.Root.MaximumSize(180, 36);
                });
                await Until(() => bar.Root.GetBounds().Width <= 180);
                await Check(() => bar.Segments[^1].Name.GetBounds().Width > 0
                    && bar.Segments.Take(bar.Segments.Count - 1).Any(segment => segment.Name.GetBounds().Width == 0)
                    && bar.Segments[^1].Name.GetBounds().X + bar.Segments[^1].Name.GetBounds().Width
                        <= bar.Root.GetBounds().X + bar.Root.GetBounds().Width,
                    "Narrow layout keeps the current folder inside the address slot");
                await Ui(() =>
                {
                    bar.Root.MaximumSize(float.MaxValue, float.MaxValue);
                    bar.ShowChildren(fixture);
                    pane.NewTab(fixture);
                });
                await Ready(pane);
                await Check(() => !bar.MenuOpen, "Tab switching cancels the folder dropdown");
                await Ui(() => pane.CloseTab());
                await Ready(pane);
                await Ui(() => pane.SetViewMode(ExplorerViewMode.Columns));
                await Ready(pane);
                await Ui(() =>
                {
                    bar.ShowNavigation();
                });
                await Until(() => !app.Palettes.Pending);
                await Ui(() => app.Palettes.EditQuery(Path.Combine(fixture, "alpha")));
                await Until(() => !app.Palettes.Pending);
                await Ui(() => app.Palettes.Accept(false));
                await Ready(pane);
                await Check(() => pane.IsColumns && !app.Palettes.IsOpen && pane.FilesFocused
                    && pane.Model.Active.Path == Path.Combine(fixture, "alpha"),
                    "Palette navigation preserves Columns view and its focus");
                await Ui(() =>
                {
                    pane.SetViewMode(ExplorerViewMode.Details);
                    pane.Navigate(fixture);
                });
                await Ready(pane);
                await Ui(() => app.ToggleSplit());
                await Ready(app.Right);
                await Until(() => app.SecondPaneVisible);
                await Ui(() =>
                {
                    app.Right.AddressBar.ShowNavigation();
                });
                await Check(() => app.Palettes.IsOpen
                    && ReferenceEquals(app.Active, app.Right),
                    "Address palette activation uses its owning pane");
                await Ui(() =>
                {
                    app.Palettes.Dismiss();
                    app.ToggleSplit();
                    pane.Focus();
                    Shortcut(0x47, KeyModifiers.Control);
                });
                await Until(() => !app.Palettes.Pending);
                await Check(() => app.Palettes.IsOpen, "Ctrl+G retains the searchable navigation palette");
                await Ui(() =>
                {
                    app.Palettes.Dismiss();
                    pane.Navigate(fixture);
                    pane.Focus();
                });
                await Ready(pane);
            }

            async Task HoverJoinChecks()
            {
                ExplorerApplication source = null!, target = null!, remainder = null!;
                await Ui(() =>
                {
                    app.NewWindow(fixture);
                    source = app.ExplorerWindows.Last();
                    app.NewWindow(fixture);
                    target = app.ExplorerWindows.Last();
                });
                await Ready(source.Left);
                await Ready(target.Left);
                await Ui(() =>
                {
                    source.Left.NewTab(fixture);
                    target.Left.NewTab(fixture);
                    source.ToggleSplit();
                    target.ToggleSplit();
                });
                await Ready(source.Left);
                await Ready(target.Left);
                await Ready(source.Right);
                await Ready(target.Right);
                await Until(() => source.SecondPaneVisible && target.SecondPaneVisible);
                await Ui(() => source.Right.NewTab(fixture));
                await Ready(source.Right);
                for (uint sourceStrip = 0; sourceStrip < 2; ++sourceStrip)
                for (uint targetStrip = 0; targetStrip < 2; ++targetStrip)
                {
                    var sourcePane = sourceStrip == 0 ? source.Left : source.Right;
                    var targetPane = targetStrip == 0 ? target.Left : target.Right;
                    ExplorerTab[] leftOrder = [], rightOrder = [], targetOrder = [];
                    ExplorerTab tab = null!, targetActive = null!;
                    await Ui(() =>
                    {
                        leftOrder = source.Left.Model.Tabs.ToArray();
                        rightOrder = source.Right.Model.Tabs.ToArray();
                        tab = sourcePane.Model.Active;
                        targetOrder = targetPane.Model.Tabs.ToArray();
                        targetActive = targetPane.Model.Active;
                        if (!Send(source, TabDragKind.TearOut, sourceStrip, tab.Id))
                            throw new InvalidOperationException($"Hover tear-out failed: {source.Notification.Text}");
                        remainder = source.DragRemainder!;
                        if (!Send(source, TabDragKind.Join, sourceStrip, tab.Id, target, targetStrip, 0)
                            || source.CloseRequested || sourcePane.Model.Tabs.Count != 0
                            || targetPane.Model.Tabs[0] != tab || source.DragRemainder != remainder
                            || !source.HasTabDrag || !target.HasTabDrag)
                            throw new InvalidOperationException($"Hover join must retain source/remainder and move actual tab: {source.Notification.Text}");
                        if (sourceStrip == 1 && !source.Window.TitlebarSecondaryTabs.Visible)
                            throw new InvalidOperationException("Hosted right-strip gestures must retain secondary strip identity.");
                        if (!Send(source, TabDragKind.QueryDrop, sourceStrip, tab.Id, target, targetStrip, 0)
                            || Send(target, TabDragKind.QueryDrop, targetStrip, tab.Id, remainder, 0, 0))
                            throw new InvalidOperationException("Hosted-tab preview must use its live model and reject competing gestures.");
                        targetPane.Navigate(Path.Combine(fixture, "alpha"));
                        var entries = tab.Entries;
                        for (int i = 0; i < 4; ++i)
                        {
                            if (!Send(source, TabDragKind.Join, sourceStrip, tab.Id, target, targetStrip, i % 2)
                                || !targetPane.IsLoading || targetPane.Model.Active != tab
                                || !ReferenceEquals(tab.Entries, entries) || targetPane.Model.Tabs[0] != tab)
                                throw new InvalidOperationException("Unchanged hosted insertion slots must not cancel navigation or rebuild tab content.");
                        }
                        if (!Send(source, TabDragKind.Join, sourceStrip, tab.Id, target, targetStrip, targetPane.Model.Tabs.Count)
                            || targetPane.Model.Tabs[^1] != tab
                            || !Send(source, TabDragKind.Leave, sourceStrip, tab.Id, target, targetStrip)
                            || sourcePane.Model.Active != tab || target.HasTabDrag
                            || !targetPane.Model.Tabs.SequenceEqual(targetOrder) || targetPane.Model.Active != targetActive)
                            throw new InvalidOperationException($"Join/reorder/Leave must restore target and source strip: {source.Notification.Text}");
                        uint otherStrip = 1 - targetStrip;
                        if (!Send(source, TabDragKind.Join, sourceStrip, tab.Id, target, otherStrip, 0)
                            || !Send(source, TabDragKind.Leave, sourceStrip, tab.Id, target, otherStrip))
                            throw new InvalidOperationException("Retargeting to the other pane of the same destination must be reversible.");
                        // Retarget the same gesture to its remainder, then return before cancellation.
                        if (!Send(source, TabDragKind.Join, sourceStrip, tab.Id, remainder, 0, 0)
                            || !Send(source, TabDragKind.Leave, sourceStrip, tab.Id, remainder, 0)
                            || !Send(source, TabDragKind.Cancel, sourceStrip, tab.Id)
                            || !Send(source, TabDragKind.Completed, sourceStrip, tab.Id)
                            || !source.Left.Model.Tabs.SequenceEqual(leftOrder)
                            || !source.Right.Model.Tabs.SequenceEqual(rightOrder))
                            throw new InvalidOperationException($"Retarget/Leave/Cancel must restore both original panes: {source.Notification.Text}");
                    });
                    await Until(() => remainder.IsDisposed);
                    await Ready(source.Left);
                    await Ready(source.Right);
                    await Ready(targetPane);
                }
                ExplorerTab committed = null!;
                await Ui(() =>
                {
                    committed = source.Right.Model.Active;
                    if (!Send(source, TabDragKind.TearOut, 1, committed.Id))
                        throw new InvalidOperationException("Commit fixture tear-out failed.");
                    remainder = source.DragRemainder!;
                    int count = target.Right.Model.Tabs.Count;
                    if (!Send(source, TabDragKind.Join, 1, committed.Id, target, 1, count)
                        || !Send(source, TabDragKind.Drop, 1, committed.Id, target, 1, count)
                        || target.Right.Model.Tabs.Count != count + 1
                        || target.Right.Model.Tabs.Count(tab => tab == committed) != 1
                        || target.HasTabDrag || source.CloseRequested || !source.HasTabDrag)
                        throw new InvalidOperationException($"Hosted release must commit without duplicate transfer: {source.Notification.Text}");
                });
                await Check(() => source.Window.State == WindowState.Open && !source.CloseRequested
                    && remainder.Window.State == WindowState.Open,
                    "Hosted Drop retains every participating window until Completed");
                await Ui(() => Send(source, TabDragKind.Completed, 1, committed.Id));
                await Until(() => source.IsDisposed);
                await Ready(target.Right);
                await Ui(() =>
                {
                    remainder.Window.Close();
                    target.Window.Close();
                });
                await Until(() => remainder.IsDisposed && target.IsDisposed && app.ExplorerWindows.Count == 1);

                // Destination closure returns the hosted model to the still-live source.
                await Ui(() =>
                {
                    app.NewWindow(fixture);
                    source = app.ExplorerWindows.Last();
                    app.NewWindow(fixture);
                    target = app.ExplorerWindows.Last();
                });
                await Ready(source.Left);
                await Ready(target.Left);
                await Ui(() =>
                {
                    committed = source.Left.Model.Active;
                    if (!Send(source, TabDragKind.TearOut, 0, committed.Id)
                        || !Send(source, TabDragKind.Join, 0, committed.Id, target, 0, 0))
                        throw new InvalidOperationException("Closing-host fixture could not join.");
                    target.Window.Close();
                });
                await Until(() => target.IsDisposed);
                await Ready(source.Left);
                await Check(() => source.Left.Model.Active == committed && !source.CloseRequested,
                    "Closing a hover target returns the tab to its retained source");
                await Ui(() =>
                {
                    if (!Send(source, TabDragKind.Leave, 0, committed.Id))
                        throw new InvalidOperationException("Leave with a null disposed target must acknowledge source recovery.");
                    Send(source, TabDragKind.Completed, 0, committed.Id);
                    app.NewWindow(fixture);
                    target = app.ExplorerWindows.Last();
                });
                await Ready(target.Left);
                await Ui(() =>
                {
                    if (!Send(source, TabDragKind.TearOut, 0, committed.Id)
                        || !Send(source, TabDragKind.Join, 0, committed.Id, target, 0, 0))
                        throw new InvalidOperationException("Closing-source fixture could not join.");
                    source.Window.Close();
                });
                await Until(() => source.IsDisposed);
                await Ready(target.Left);
                await Check(() => target.Left.Model.Tabs.Contains(committed) && !target.HasTabDrag,
                    "Closing the drag initiator preserves the live hosted model and releases the target guard");
                await Ui(target.Window.Close);
                await Until(() => target.IsDisposed && app.ExplorerWindows.Count == 1);

                ExplorerApplication recovery = null!;
                await Ui(() =>
                {
                    app.NewWindow(fixture);
                    source = app.ExplorerWindows.Last();
                    app.NewWindow(fixture);
                    target = app.ExplorerWindows.Last();
                });
                await Ready(source.Left);
                await Ready(target.Left);
                await Ui(() =>
                {
                    committed = source.Left.Model.Active;
                    if (!Send(source, TabDragKind.TearOut, 0, committed.Id)
                        || !Send(source, TabDragKind.Join, 0, committed.Id, target, 0, 0))
                        throw new InvalidOperationException("Retained-model recovery fixture could not join.");
                    target.Left.ResetTabs(fixture);
                    if (!Send(source, TabDragKind.Leave, 0, committed.Id, target)
                        || source.Left.Model.Active != committed || target.HasTabDrag)
                        throw new InvalidOperationException("Leave must recover the retained model when the destination replaced its pane.");
                    if (!Send(source, TabDragKind.Join, 0, committed.Id, target, 0, 0))
                        throw new InvalidOperationException("Recovery fixture could not rejoin.");
                    while (source.Left.Model.Tabs.Count < ExplorerPane.TabLimit)
                        source.Left.Model.AddTab(fixture);
                    if (Send(source, TabDragKind.Leave, 0, committed.Id, target))
                        throw new InvalidOperationException("A full original source must not claim successful Leave.");
                    recovery = app.ExplorerWindows.Last();
                    if (recovery == source || recovery == target || recovery.Left.Model.Active != committed
                        || target.Left.Model.Tabs.Contains(committed) || target.HasTabDrag)
                        throw new InvalidOperationException("An impossible rollback must preserve the actual tab in a recovery window.");
                });
                await Ready(recovery.Left);
                await Ui(() =>
                {
                    source.Window.Close();
                    target.Window.Close();
                    recovery.Window.Close();
                });
                await Until(() => source.IsDisposed && target.IsDisposed && recovery.IsDisposed
                    && app.ExplorerWindows.Count == 1);

                static bool Send(ExplorerApplication owner, TabDragKind kind, uint strip, ulong id,
                    ExplorerApplication? destination = null, uint targetStrip = 0, int index = 0)
                    => owner.Window.TabDragHandler?.Invoke(new(kind, strip, id,
                        destination?.Window, targetStrip, index)) == true;
            }

            async Task TabDragChecks()
            {
                ExplorerApplication source = null!, target = null!, remainder = null!, stale = null!, survivor = null!;
                ExplorerTab[] left = [], right = [];
                WindowPlacement placement = default;
                await Ui(() =>
                {
                    app.NewWindow(fixture);
                    source = app.ExplorerWindows.Last();
                    if (ReferenceEquals(source, app) || !ReferenceEquals(source.Application, app.Application))
                        throw new InvalidOperationException("New windows must join the existing Application.");
                    source.Window.Placement = new(110, 120, 1320, 840, false);
                });
                await Ready(source.Left);
                await Ui(() => source.Left.NewTab(fixture));
                await Ready(source.Left);
                await Ui(() => source.ToggleSplit());
                await Until(() => source.SecondPaneVisible);
                await Ready(source.Right);
                await Ui(() => source.Right.NewTab(fixture));
                await Ready(source.Right);
                await Ui(() =>
                {
                    left = source.Left.Model.Tabs.ToArray();
                    right = source.Right.Model.Tabs.ToArray();
                    placement = source.Window.Placement;
                    if (!Drag(source, TabDragKind.Reorder, 0, left[0].Id, source, 0, left.Length)
                        || source.Left.Model.Tabs[^1] != left[0]
                        || !Drag(source, TabDragKind.Cancel, 0, left[0].Id)
                        || !Drag(source, TabDragKind.Completed, 0, left[0].Id)
                        || !source.Left.Model.Tabs.SequenceEqual(left)
                        || !source.Right.Model.Tabs.SequenceEqual(right))
                        throw new InvalidOperationException("Reorder cancellation must restore both pane orders.");
                });
                await Ready(source.Left);
                await Ready(source.Right);
                await Ui(() =>
                {
                    if (Drag(source, TabDragKind.Reorder, 0, left[0].Id, source, 0, -1)
                        || source.HasTabDrag || !source.Left.Model.Tabs.SequenceEqual(left))
                        throw new InvalidOperationException("Invalid insertion slots must not mutate the source.");
                    if (Drag(source, TabDragKind.Reorder, 0, left[0].Id, source, 1, 1))
                        throw new InvalidOperationException("Reorder must stay in the source strip.");
                    string notification = source.Notification.Text;
                    var leftActive = source.Left.Model.Active;
                    var rightActive = source.Right.Model.Active;
                    for (int i = 0; i < 3; ++i)
                    {
                        if (!Drag(source, TabDragKind.QueryDrop, 0, left[0].Id, source, 1, 1)
                            || Drag(source, TabDragKind.QueryDrop, 0, left[0].Id, source, 1, -1)
                            || Drag(source, TabDragKind.QueryDrop, 0, ulong.MaxValue, source, 1, 0)
                            || source.HasTabDrag || source.Notification.Text != notification
                            || source.Left.Model.Active != leftActive || source.Right.Model.Active != rightActive
                            || !source.Left.Model.Tabs.SequenceEqual(left) || !source.Right.Model.Tabs.SequenceEqual(right))
                            throw new InvalidOperationException("Drop preview must validate without changing either pane or reporting errors.");
                    }
                    if (!Drag(source, TabDragKind.Drop, 0, left[0].Id, source, 1, 1)
                        || source.Right.Model.Tabs[1] != left[0] || !source.HasTabDrag
                        || !Drag(source, TabDragKind.Completed, 0, left[0].Id)
                        || source.Right.Model.Tabs[1] != left[0])
                        throw new InvalidOperationException("Release into another pane must transfer once and accept Completed.");
                    if (!Drag(source, TabDragKind.Drop, 1, left[0].Id, source, 0, 0)
                        || !Drag(source, TabDragKind.Completed, 1, left[0].Id)
                        || !source.Left.Model.Tabs.SequenceEqual(left)
                        || !source.Right.Model.Tabs.SequenceEqual(right))
                        throw new InvalidOperationException("Cross-pane release must preserve identities and insertion order.");
                    source.Left.SelectTab(left[0].Id);
                });
                await Ready(source.Left);
                await Ui(() =>
                {
                    source.Left.SetViewMode(ExplorerViewMode.Columns);
                    source.Left.SetFilter("small");
                });
                await Ready(source.Left);
                await Ui(() =>
                {
                    source.Left.SelectColumnPath(0, Path.Combine(fixture, "small.txt"));
                    source.Left.CaptureViewport();
                    source.Left.Navigate(Path.Combine(fixture, "alpha"));
                    if (!source.Left.IsLoading
                        || !Drag(source, TabDragKind.QueryDrop, 0, left[0].Id, source, 1, 0)
                        || !source.Left.IsLoading || source.HasTabDrag)
                        throw new InvalidOperationException("Drop preview must leave pending navigation and transaction state unchanged.");
                    if (!source.Left.IsLoading || !Drag(source, TabDragKind.TearOut, 0, left[0].Id))
                        throw new InvalidOperationException($"Tear-out must cancel pending navigation. {source.Notification.Text}");
                    remainder = source.DragRemainder
                        ?? throw new InvalidOperationException("Tear-out must create the remaining document.");
                    if (source.Left.Model.Tabs.Count != 1 || source.Left.Model.Active != left[0]
                        || source.Right.Model.Tabs.Count != 0 || source.Left.IsLoading
                        || remainder.Window.Placement != placement || !source.HasTabDrag)
                        throw new InvalidOperationException("Tear-out must keep the dragged tab and original remainder placement.");
                });
                await Ready(source.Left);
                await Ready(remainder.Left);
                await Ready(remainder.Right);
                await Check(() => source.Left.Model.Active.Path == fixture && source.Left.IsColumns
                    && source.Left.FilterQuery == "small" && source.Left.Model.Active.Columns[0].Filter == "small"
                    && source.Left.SelectedEntry?.Name == "small.txt",
                    "Transferred Columns state survives canceled obsolete navigation");
                await Ui(() =>
                {
                    if (!Drag(source, TabDragKind.Cancel, 0, left[0].Id)
                        || !source.Left.Model.Tabs.SequenceEqual(left) || !source.Right.Model.Tabs.SequenceEqual(right)
                        || !source.HasTabDrag || remainder.CloseRequested)
                        throw new InvalidOperationException("Tear-out cancellation must restore the original models first.");
                });
                await Check(() => remainder.Window.State == WindowState.Open,
                    "Canceled remainder remains alive until Completed");
                await Ui(() => Drag(source, TabDragKind.Completed, 0, left[0].Id));
                await Until(() => remainder.IsDisposed && !app.ExplorerWindows.Contains(remainder));
                await Ready(source.Left);
                await Ready(source.Right);
                await Ui(() =>
                {
                    source.Right.SelectTab(right[0].Id);
                });
                await Ready(source.Right);
                await Ui(() =>
                {
                    if (!Drag(source, TabDragKind.TearOut, 1, right[0].Id))
                        throw new InvalidOperationException("Right-pane tear-out was rejected.");
                    remainder = source.DragRemainder!;
                    if (source.Right.Model.Active != right[0] || source.Left.Model.Tabs.Count != 0
                        || source.Window.TitlebarTabs.Visible || !source.Window.TitlebarSecondaryTabs.Visible)
                        throw new InvalidOperationException("Right-pane tear-out must retain the secondary strip.");
                });
                await Until(() => FillsPaneArea(source, source.Right));
                await Check(() => !source.Panes.FirstVisible && source.SecondPaneVisible
                    && source.Left.Root.GetBounds().Width == 0,
                    "Right-strip tear-out fills the split area without reserving a blank left pane");
                await Ui(() => source.Window.Placement = placement with { Width = 640 });
                await Until(() => FillsPaneArea(source, source.Right));
                await Check(() => source.SecondPaneVisible && !source.Panes.FirstVisible
                    && source.Right.Model.Active == right[0] && source.Window.TitlebarSecondaryTabs.Visible,
                    "Right-strip tear-out stays full-width and visible in a narrow window");
                await Ui(() =>
                {
                    if (!Drag(source, TabDragKind.Cancel, 1, right[0].Id)
                        || !source.Panes.FirstVisible
                        || !source.Left.Model.Tabs.SequenceEqual(left) || !source.Right.Model.Tabs.SequenceEqual(right)
                        || source.Window.Placement != placement)
                        throw new InvalidOperationException("Right-strip cancellation must restore primary visibility, pane order, and placement.");
                });
                await Ui(() => Drag(source, TabDragKind.Completed, 1, right[0].Id));
                await Until(() => remainder.IsDisposed);
                await Ready(source.Left);
                await Ready(source.Right);
                await Ui(() =>
                {
                    if (!Drag(source, TabDragKind.TearOut, 1, right[0].Id))
                        throw new InvalidOperationException("Repeated right-pane tear-out was rejected.");
                    remainder = source.DragRemainder!;
                });
                await Until(() => FillsPaneArea(source, source.Right));
                await Ui(() =>
                {
                    if (!Drag(source, TabDragKind.Completed, 1, right[0].Id)
                        || source.Left.Model.Active != right[0] || source.HasTabDrag
                        || source.Window.TitlebarSecondaryTabs.Visible || !source.Panes.FirstVisible)
                        throw new InvalidOperationException("Completed right-pane tear-out must normalize the remaining workspace.");
                });
                await Until(() => FillsPaneArea(source, source.Left));
                await Ready(source.Left);
                await Ready(remainder.Left);
                await Ready(remainder.Right);
                await Ui(source.ToggleSplit);
                await Until(() => source.SecondPaneVisible);
                await Ready(source.Right);
                await Check(() =>
                {
                    var tabs = source.Window.TitlebarSecondaryTabs;
                    tabs.Focus();
                    nint strip = GetFocus();
                    bool interactive = strip != 0 && tabs.Focused && IsWindowEnabled(strip);
                    source.Right.Focus();
                    return source.Right.Model.Tabs.Count == 1 && tabs.Visible && interactive
                        && source.Right.BackButton.GetBounds().Height > 0;
                },
                    "A normalized tear-out window can create and display another pane with an interactive tab strip");
                await Ui(() => source.ClosePane(source.Right));
                await Ui(() =>
                {
                    if (!Drag(source, TabDragKind.TearOut, 0, right[0].Id) || source.DragRemainder is not null
                        || !Drag(source, TabDragKind.Cancel, 0, right[0].Id)
                        || !Drag(source, TabDragKind.Completed, 0, right[0].Id))
                        throw new InvalidOperationException("A lone tab must not create an empty remainder.");
                    app.NewWindow(fixture);
                    target = app.ExplorerWindows.Last();
                });
                await Ready(target.Left);
                await Ui(() =>
                {
                    app.NewWindow(fixture);
                    stale = app.ExplorerWindows.Last();
                    stale.Window.Close();
                });
                await Until(() => stale.IsDisposed);
                await Ui(() =>
                {
                    string notification = source.Notification.Text;
                    if (Drag(source, TabDragKind.QueryDrop, 0, right[0].Id, stale, 0, 0)
                        || Drag(source, TabDragKind.QueryDrop, 0, right[0].Id, target, 1, 0)
                        || !Drag(source, TabDragKind.QueryDrop, 0, right[0].Id, target, 0, 1)
                        || source.Notification.Text != notification || source.HasTabDrag)
                        throw new InvalidOperationException("Preview must reject closed or hidden targets without side effects.");
                    if (Drag(source, TabDragKind.Drop, 0, right[0].Id, stale, 0, 0)
                        || source.Left.Model.Active != right[0])
                        throw new InvalidOperationException("Closed destinations must not mutate the source.");
                    if (Drag(source, TabDragKind.Drop, 0, right[0].Id, target, 1, 0))
                        throw new InvalidOperationException("A hidden destination strip must reject a drop.");
                    for (int i = target.Left.Model.Tabs.Count; i < ExplorerPane.TabLimit; ++i)
                        target.Left.Model.AddTab(fixture);
                    notification = source.Notification.Text;
                    if (Drag(source, TabDragKind.QueryDrop, 0, right[0].Id, target, 0, 0)
                        || source.Notification.Text != notification || source.HasTabDrag
                        || source.Left.Model.Active != right[0])
                        throw new InvalidOperationException("Full-pane preview must not transfer or report an error.");
                    if (Drag(source, TabDragKind.Drop, 0, right[0].Id, target, 0, 0)
                        || source.Left.Model.Active != right[0])
                        throw new InvalidOperationException("A full target must leave the source unchanged.");
                    target.Left.ResetTabs(fixture);
                    target.Left.RenderTransferredModel();
                    if (!Drag(source, TabDragKind.Drop, 0, right[0].Id, target, 0, 1)
                        || target.Left.Model.Tabs[1] != right[0] || source.CloseRequested)
                        throw new InvalidOperationException("Merge must transfer identity without retiring its source before Completed.");
                });
                await Check(() => source.Window.State == WindowState.Open, "Release-only Drop retains its source until Completed");
                await Ui(() => Drag(source, TabDragKind.Completed, 0, right[0].Id));
                await Until(() => source.IsDisposed && !app.ExplorerWindows.Contains(source));
                await Ready(target.Left);
                await Check(() => target.Window.State == WindowState.Open
                    && ReferenceEquals(target.Left.Model.Active, right[0]),
                    "Merged windows survive opener retirement");
                await Ui(() =>
                {
                    app.NewWindow(fixture);
                    stale = app.ExplorerWindows.Last();
                });
                await Ready(stale.Left);
                await Ui(() =>
                {
                    ulong id = stale.Left.Model.Active.Id;
                    if (!Drag(stale, TabDragKind.Drop, 0, id,
                        target, 0, target.Left.Model.Tabs.Count) || stale.CloseRequested
                        || !Drag(stale, TabDragKind.Completed, 0, id) || !stale.CloseRequested)
                        throw new InvalidOperationException("Merging a fresh window's last tab must discard its dormant placeholder pane.");
                });
                await Until(() => stale.IsDisposed);
                await Ready(target.Left);
                await Ui(() =>
                {
                    if (!Drag(target, TabDragKind.TearOut, 0, right[0].Id))
                        throw new InvalidOperationException("The merged tab could not tear out again.");
                    survivor = target.DragRemainder!;
                    target.Window.Close();
                });
                await Until(() => target.IsDisposed);
                await Ready(survivor.Left);
                await Check(() => survivor.Window.State == WindowState.Open && survivor.Left.HasCurrentRows
                    && !app.ExplorerWindows.Contains(target),
                    "The remainder survives source closure during a tear-out transaction");
                await Ui(() =>
                {
                    GC.Collect();
                    GC.WaitForPendingFinalizers();
                    GC.Collect();
                    remainder.Window.Close();
                    survivor.Window.Close();
                });
                await Until(() => remainder.IsDisposed && survivor.IsDisposed && app.ExplorerWindows.Count == 1);

                static bool Drag(ExplorerApplication controller, TabDragKind kind, uint strip, ulong id,
                    ExplorerApplication? destination = null, uint targetStrip = 0, int index = 0)
                    => controller.Window.TabDragHandler?.Invoke(new(kind, strip, id,
                        destination?.Window, targetStrip, index)) == true;

                static bool FillsPaneArea(ExplorerApplication controller, FilePaneView pane)
                {
                    var area = controller.Panes.GetBounds();
                    var bounds = pane.Root.GetBounds();
                    return area.Width > 0 && bounds.X == area.X && bounds.Y == area.Y
                        && bounds.Width == area.Width && bounds.Height == area.Height;
                }
            }

            async Task PreviewChecks()
            {
                string root = Path.Combine(fixture, "preview-fixtures");
                Directory.CreateDirectory(Path.Combine(root, "folder"));
                await File.WriteAllTextAsync(Path.Combine(root, "notes.txt"), "one\r\ntwo");
                const string syntaxSource = "component Preview {\rview { Text(\"source\"); }\r}";
                await File.WriteAllTextAsync(Path.Combine(root, "source.xui"), syntaxSource);
                await File.WriteAllTextAsync(Path.Combine(root, "large.txt"), new string('x', FilePreviewService.MaximumTextLength + 10));
                await File.WriteAllTextAsync(Path.Combine(root, "invalid.txt"), "binary\0text");
                await File.WriteAllTextAsync(Path.Combine(root, "unsupported.pdf"), "not a PDF");
                await File.WriteAllTextAsync(Path.Combine(root, "broken.bmp"), "not an image");
                await File.WriteAllTextAsync(Path.Combine(root, "restricted.txt"), "must not enter the text preview");
                await File.WriteAllTextAsync(Path.Combine(root, "restricted.txt") + ":Zone.Identifier", "[ZoneTransfer]\r\nZoneId=3\r\n");
                await using (var file = File.Create(Path.Combine(root, "pixel.bmp")))
                using (var writer = new BinaryWriter(file))
                {
                    writer.Write((ushort)0x4d42);
                    writer.Write(58);
                    writer.Write(0);
                    writer.Write(54);
                    writer.Write(40);
                    writer.Write(1);
                    writer.Write(1);
                    writer.Write((ushort)1);
                    writer.Write((ushort)24);
                    writer.Write(0);
                    writer.Write(4);
                    for (int i = 0; i < 4; i++) writer.Write(0);
                    writer.Write(new byte[] { 0x40, 0x80, 0xff, 0 });
                }
                await Ui(() => app.Left.Navigate(root));
                await Ready(app.Left);
                int opens = 0;
                nint filePeer = 0;
                await Ui(() =>
                {
                    opens = app.FileOpenCount;
                    app.Left.SelectPath(Path.Combine(root, "notes.txt"));
                    app.Left.ShowFind();
                    if (app.Window.KeyHandler?.Invoke(new(0x20, KeyModifiers.None, app.Left.FindInput.Id)) != false)
                        throw new InvalidOperationException("Space must remain native input in Find.");
                    app.Left.Focus();
                    filePeer = GetFocus();
                    if (!PostMessageW(filePeer, 0x100, 0x20, 1))
                        throw new InvalidOperationException("Could not post the preview shortcut to the owned file view.");
                });
                await Until(() => app.Preview.IsOpen && !app.Preview.Pending && !app.Preview.Current!.EntryReveal.Animating);
                await Check(() =>
                {
                    var preview = app.Preview.Current!;
                    return !preview.Window.TitlebarClose.Focused
                        && !preview.Window.TitlebarMinimize.Focused
                        && !preview.Window.TitlebarMaximize.Focused;
                }, "Opening a preview must not focus a caption button or draw its focus outline");
                await Check(() =>
                {
                    var preview = app.Preview.Current!;
                    var caption = preview.Window.Titlebar.GetBounds();
                    var open = preview.OpenButton.GetBounds();
                    return open.Y >= caption.Y && open.Y + open.Height <= caption.Y + caption.Height
                        && open.Width > 0 && preview.Window.TitlebarTitle.Text == $"Preview: {preview.Target.Name}"
                        && !preview.Window.TitlebarTabs.Visible && !preview.Window.TitlebarSecondaryTabs.Visible
                        && preview.CloseButton.Id == preview.Window.TitlebarClose.Id
                        && preview.BodyBounds.Y == preview.Bounds.Y + 12
                        && preview.BodyBounds.Height == preview.Bounds.Height - preview.StatusBounds.Height - 32;
                }, "Open occupies the titlebar; content has only its status row beneath it, without a duplicate header or plugin footer");
                await Check(() => app.Preview.Text.Text == "one\rtwo" && app.Preview.Text.ReadOnly
                    && app.Preview.Bounds.Width > 0 && app.Preview.Bounds.Height > 0
                    && app.Preview.Text.GetBounds().Height > 0 && app.FileOpenCount == opens
                    && !app.Preview.StatusVisible && app.Preview.Message == ""
                    && app.Preview.OpenButton.Icon == ButtonIcon.Open
                    && app.Preview.Text.GetControlStyleValues(StylePart.Root, effective: true).BorderThickness == new Insets(0)
                    && app.Preview.Text.GetControlStyleValues(StylePart.Text, effective: true).FontFamily == "Cascadia Mono",
                    "Text preview uses borderless Cascadia Mono without a read-only notice, plus an Open icon");
                await Check(() =>
                {
                    var rootStyle = app.Preview.Text.GetControlStyleValues(StylePart.Root, effective: true);
                    return rootStyle.Background == new ThemeColor(0xF3F3F3, 0x202020)
                        && rootStyle.Padding == new Insets(12) && rootStyle.CornerRadius == 0
                        && app.Preview.Text.GetControlStyleValues(StylePart.Text, effective: true).FontSize == 14;
                }, "Declarative text styling preserves the themed background, padding, corners, and native font size");
                await Ui(() =>
                {
                    if (!PostMessageW(GetFocus(), 0x100, 0x09, 1))
                        throw new InvalidOperationException("Could not post Tab to the preview.");
                });
                await Until(() => app.Preview.OpenButton.Focused || app.Preview.Text.Focused);
                await Ui(() =>
                {
                    if (!PostMessageW(GetFocus(), 0x100, 0x20, (1 << 30) | 1))
                        throw new InvalidOperationException("Could not post a repeated Space to the preview.");
                });
                await Ui(() =>
                {
                    if (!app.Preview.IsOpen)
                        throw new InvalidOperationException("A held Space key must not dismiss the preview.");
                    if (app.Preview.Current!.Window.KeyHandler?.Invoke(new(0x57, KeyModifiers.Control, 0)) != false)
                        throw new InvalidOperationException("Preview must not route Explorer shortcuts.");
                    app.Preview.Text.Focus();
                    nint format = Marshal.AllocHGlobal(116);
                    try
                    {
                        Marshal.Copy(new byte[116], 0, format, 116);
                        Marshal.WriteInt32(format, 116);
                        SendMessageW(GetFocus(), 0x043a, 0, format);
                        if (Marshal.PtrToStringUni(format + 26, 32)?.TrimEnd('\0') != "Cascadia Mono")
                            throw new InvalidOperationException("The native preview document did not receive Cascadia Mono.");
                    }
                    finally { Marshal.FreeHGlobal(format); }
                    app.Preview.Text.Selection = new(0, 3);
                    if (app.Preview.Text.Selection != new TextSelection(0, 3)
                        || app.Preview.Current!.Window.KeyHandler?.Invoke(new(0x43, KeyModifiers.Control, app.Preview.Text.Id)) != false)
                        throw new InvalidOperationException("Native preview selection and copying must remain available.");
                    app.Preview.Current!.Window.KeyHandler?.Invoke(new(0x1b, KeyModifiers.None, 0));
                });
                await Until(() => !app.Preview.IsOpen && app.Preview.Current!.IsDisposed);
                await Check(() => app.Left.Model.Active.FindOpen && !app.Left.IsLoading,
                    "Held Space does not toggle; preview Escape closes only its window without clearing Explorer Find");
                await Ui(() => app.Left.HideFind());
                await EntryChecks();

                async Task EntryChecks()
                {
                    PreviewSession preview = null!;
                    nint editor = 0, owner = 0;
                    bool motion = false;
                    ElementBounds caption = default, status = default;
                    var positions = new List<int>();
                    await Ui(() =>
                    {
                        app.Left.SelectPath(Path.Combine(root, "notes.txt"));
                        app.Preview.ShowSelected(app.Left);
                        preview = app.Preview.Current!;
                        preview.EntryReveal.Duration = 1200;
                    });
                    await Until(() => !preview.Pending);
                    await Ui(() =>
                    {
                        preview.Text.Focus();
                        editor = GetFocus();
                        owner = GetAncestor(editor, 2);
                        SendMessageW(owner, 0x800C, 0, 0);
                        if (!preview.Text.Focused || preview.Text.Text != "one\rtwo" || !preview.Text.ReadOnly)
                            throw new InvalidOperationException("Preview content must accept native read-only focus before entry finishes.");
                        preview.Text.Selection = new(0, 3);
                        caption = preview.Window.Titlebar.GetBounds();
                        status = preview.StatusBounds;
                        if (!SystemParametersInfoW(0x1042, 0, out int allowed, 0))
                            throw new InvalidOperationException("Could not read the system motion preference.");
                        motion = allowed != 0;
                        if (motion && !preview.EntryReveal.Animating)
                            throw new InvalidOperationException("Diagnostic preview entry completed before observation.");
                    });
                    await Until(() =>
                    {
                        var currentCaption = preview.Window.Titlebar.GetBounds();
                        if (GetFocus() != editor || preview.Text.Selection != new TextSelection(0, 3)
                            || currentCaption.Y != caption.Y || currentCaption.Height != caption.Height
                            || preview.StatusBounds.Y != status.Y)
                            throw new InvalidOperationException("Entry must retain native selection and focus without moving the caption or status.");
                        var position = new NativePoint();
                        if (!ClientToScreen(editor, ref position))
                            throw new InvalidOperationException("Could not read the moving preview editor.");
                        positions.Add(position.Y);
                        return !preview.EntryReveal.Animating;
                    });
                    await Ui(() =>
                    {
                        var final = new NativePoint();
                        if (!ClientToScreen(editor, ref final))
                            throw new InvalidOperationException("Could not read the settled preview editor.");
                        double extent = preview.BodyBounds.Height * GetDpiForWindow(editor) / 96.0;
                        if (motion && !positions.Any(y => y > final.Y + 1 && y < final.Y + extent - 1))
                            throw new InvalidOperationException("Preview entry must move the live native document through an intermediate position.");
                        if (SendMessageW(owner, 0x803C, 33, 0) != 0 || preview.EntryReveal.Progress != 1)
                            throw new InvalidOperationException("Completed preview entry must stop its shared clock at the endpoint.");
                        preview.Dismiss();
                    });
                    await Until(() => preview.IsDisposed);
                    foreach (uint duration in new uint[] { 10000, 0 })
                    {
                        long closeStarted = 0;
                        await Ui(() =>
                        {
                            app.Preview.ShowSelected(app.Left);
                            preview = app.Preview.Current!;
                            preview.EntryReveal.Duration = duration;
                        });
                        await Until(() => !preview.Pending);
                        await Ui(() =>
                        {
                            preview.Text.Focus();
                            owner = GetAncestor(GetFocus(), 2);
                            SendMessageW(owner, 0x800C, 0, 0);
                            if (duration == 0 && (preview.EntryReveal.Animating || SendMessageW(owner, 0x803C, 33, 0) != 0))
                                throw new InvalidOperationException("Immediate preview content must start no animation clock.");
                            if (duration != 0 && motion && !preview.EntryReveal.Animating)
                                throw new InvalidOperationException("The interrupted preview fixture must close during active entry.");
                            closeStarted = Stopwatch.GetTimestamp();
                            preview.Dismiss();
                        });
                        await Until(() => preview.IsDisposed);
                        await Check(() => !IsWindow(owner) && Stopwatch.GetElapsedTime(closeStarted) < TimeSpan.FromSeconds(2),
                            "Queued preview closure must finish without waiting for the ten-second entry");
                    }
                }

                foreach (string name in new[] { "source.xui", "large.txt", "invalid.txt", "unsupported.pdf", "folder", "pixel.bmp", "broken.bmp", "restricted.txt" })
                {
                    nint previewHost = 0, initialIcon = 0;
                    await Ui(() =>
                    {
                        app.Left.SelectPath(Path.Combine(root, name));
                        app.Left.ContextMenu.GetCommands();
                        app.Left.ContextMenu.Invoke(FileContextMenu.Preview);
                        previewHost = GetAncestor(GetFocus(), 2);
                        initialIcon = SendMessageW(previewHost, 0x7f, 0, 0);
                        if (initialIcon == 0 || SendMessageW(previewHost, 0x7f, 1, 0) == 0)
                            throw new InvalidOperationException("Preview HWND must supply small and large native icons.");
                        if (app.Preview.Current!.CloseButton.Focused)
                            throw new InvalidOperationException("A loading preview must not focus its caption Close button.");
                    });
                    await Until(() => app.Preview.IsOpen && !app.Preview.Pending && !app.Preview.Current!.EntryReveal.Animating);
                    if (name == "source.xui")
                        await Check(() => app.Preview.Text.Text == syntaxSource && !app.Preview.StatusVisible,
                            "Source previews retain readable native text without highlighting errors");
                    else if (name == "large.txt")
                        await Check(() => app.Preview.Text.Text.Length == FilePreviewService.MaximumTextLength
                            && app.Preview.Message.Contains("truncated"), "Large previews are bounded and visibly truncated");
                    else if (name == "invalid.txt")
                        await Check(() => app.Preview.Message.Contains("Cannot preview"), "Binary text produces an explicit error");
                    else if (name == "unsupported.pdf")
                        await Check(() => !app.Preview.StatusVisible && app.Preview.MetadataName == "unsupported.pdf"
                            && app.Preview.MetadataKind == "File Type: PDF file"
                            && app.Preview.MetadataSize.Contains("bytes"), "Unsupported files use the icon-and-details layout without a warning");
                    else if (name == "folder")
                        await Check(() => !app.Preview.StatusVisible && app.Preview.MetadataName == "folder"
                            && app.Preview.MetadataKind == "File Type: File folder"
                            && app.Preview.MetadataSize == "Size: Not calculated", "Folder metadata does not invent a recursive size");
                    else if (name == "restricted.txt")
                    {
                        await Check(() => SendMessageW(previewHost, 0x7f, 0, 0) == initialIcon
                            && app.Preview.Image.Status == ImageStatus.Empty && app.Preview.Text.Text == ""
                            && app.Preview.Message.Contains("generic metadata"),
                            "Restricted input invokes neither WIC nor text preview; metadata and window icons remain generic");
                    }
                    else
                    {
                        await Until(() => app.Preview.Image.Status == (name == "pixel.bmp" ? ImageStatus.Ready : ImageStatus.Error));
                        await Check(() => app.Preview.Image.GetBounds().Width > 0 && app.Preview.Image.GetBounds().Height > 0,
                            "Image preview occupies the visible content area");
                        await Check(() => !app.Preview.StatusVisible && app.Preview.Message == "",
                            "Images omit routine decode-size notices while the image control retains its errors");
                        await Check(() =>
                        {
                            var style = app.Preview.Image.GetControlStyleValues(StylePart.Root, effective: true);
                            return style.Background == new ThemeColor(0xF3F3F3, 0x202020)
                                && style.BorderThickness == new Insets(0) && style.CornerRadius == 0;
                        }, "Declarative image styling preserves the themed borderless surface");
                    }
                    if (name is "folder" or "unsupported.pdf")
                    {
                        await Check(() => app.Preview.MetadataIcon.GetBounds().Width == 160
                            && app.Preview.MetadataIcon.GetBounds().Height == 160
                            && app.Preview.MetadataNameBounds.X >= app.Preview.MetadataIcon.GetBounds().X + 192,
                            "Metadata has a large retained generic icon to the left of the heading, without ShellSource");
                    }
                    await Ui(app.Preview.Dismiss);
                    await Until(() => app.Preview.Current!.IsDisposed);
                    await Check(() => !app.Preview.IsOpen, "Closing retires the preview window and its resources");
                }
                PreviewSession? earlier = null;
                await Ui(() =>
                {
                    app.Left.SelectPath(Path.Combine(root, "notes.txt"));
                    app.Commands.Single(command => command.Name == "Preview selected item").Execute();
                    earlier = app.Preview.Current;
                    app.Left.SelectPath(Path.Combine(root, "large.txt"));
                    app.Preview.ShowSelected(app.Left);
                });
                await Until(() => app.Preview.IsOpen && !app.Preview.Pending);
                await Until(() => earlier is { Pending: false });
                await Check(() => earlier!.IsOpen && earlier.Text.Text == "one\rtwo"
                    && app.Preview.Text.Text.Length == FilePreviewService.MaximumTextLength,
                    "Each captured target completes into its own independent preview");
                await Ui(() =>
                {
                    SendMessageW(filePeer, 0x201, 1, (80 << 16) | 12);
                    SendMessageW(filePeer, 0x202, 0, (80 << 16) | 12);
                });
                await Check(() => app.Preview.IsOpen && earlier!.IsOpen, "Explorer clicks do not dismiss detached previews");
                await Ui(app.Preview.CloseAll);
                await Until(() => app.Preview.Sessions.Count == 0);
                await Ui(() =>
                {
                    app.Left.SelectPath(Path.Combine(root, "notes.txt"));
                    app.Preview.ShowSelected(app.Left);
                    app.Preview.Dismiss();
                });
                await Until(() => app.Preview.Current!.IsDisposed);
                await Check(() => !app.Preview.IsOpen && !app.Preview.Pending, "Closing cancels pending text delivery");
                await Ui(() =>
                {
                    app.Left.SelectPath(Path.Combine(root, "notes.txt"));
                    File.Delete(Path.Combine(root, "notes.txt"));
                    app.Preview.ShowSelected(app.Left);
                });
                await Until(() => app.Preview.IsOpen && !app.Preview.Pending);
                await Check(() => app.Preview.Message.Contains("Cannot preview")
                    && !app.Preview.Current!.EntryReveal.Open && !app.Preview.Current.EntryReveal.Animating,
                    "Deleted files produce an explicit error without a false content entry");
                await File.WriteAllTextAsync(Path.Combine(root, "notes.txt"), "one\r\ntwo");
                await Ui(() => app.Left.Navigate(root));
                await Ready(app.Left);
                await Check(() => app.Preview.IsOpen && !app.Preview.Pending, "Navigation leaves the captured preview open");
                await Ui(app.Preview.Dismiss);
                await Until(() => app.Preview.Current!.IsDisposed);
                await Ui(() =>
                {
                    app.Left.Grid.Navigate(GridNavigation.First);
                    app.Left.Grid.Navigate(GridNavigation.Next, KeyModifiers.Shift);
                    if (app.Preview.CanPreview(app.Left)
                        || app.Left.ContextMenu.GetCommands().Any(command => command.Id == FileContextMenu.Preview))
                        throw new InvalidOperationException("Multi-selection must not choose an arbitrary preview target.");
                    app.Left.SelectPath(Path.Combine(root, "notes.txt"));
                    app.Preview.ShowSelected(app.Left);
                    app.Left.NewTab(root);
                });
                await Ready(app.Left);
                await Until(() => !app.Preview.Pending);
                await Check(() => app.Preview.IsOpen && app.Preview.Text.Text == "one\rtwo", "Tab changes leave preview content intact");
                await Ui(app.Preview.Dismiss);
                await Until(() => app.Preview.Current!.IsDisposed);
                await Ui(() => app.Left.CloseTab());
                await Ready(app.Left);

                await Ui(() => app.Left.SetViewMode(ExplorerViewMode.Columns));
                await Ready(app.Left);
                await Ui(() => app.Left.SelectColumnPath(0, Path.Combine(root, "notes.txt")));
                await Ready(app.Left);
                await Ui(() => { app.Left.Focus(); Shortcut(0x20); });
                await Until(() => app.Preview.IsOpen && !app.Preview.Pending);
                await Check(() => app.Preview.Text.Text == "one\rtwo", "Columns selection uses the same preview");
                await Ui(() => app.Preview.OpenButton.Invoke());
                await Until(() => app.Preview.Current!.IsDisposed);
                await Check(() => !app.Preview.IsOpen && app.Preview.OpenCount == 1 && app.FileOpenCount == opens,
                    "Only explicit Open invokes the associated application");
                await Ui(() =>
                {
                    app.Left.SetViewMode(ExplorerViewMode.Details);
                    app.Left.Navigate(fixture);
                });
                await Ready(app.Left);
                Directory.Delete(root, recursive: true);
                await Ui(() => app.Left.Refresh());
                await Ready(app.Left);
                await Ui(() => { app.Left.Focus(); app.Left.Grid.Navigate(GridNavigation.First); });
            }

            async Task DetachedLifetimeChecks()
            {
                await File.WriteAllTextAsync(Path.Combine(fixture, "survivor.txt"), "abc");
                string imagePath = Path.Combine(fixture, "survivor.bmp");
                using (var writer = new BinaryWriter(File.Create(imagePath)))
                {
                    writer.Write((ushort)0x4d42);
                    writer.Write(58); writer.Write(0); writer.Write(54); writer.Write(40);
                    writer.Write(1); writer.Write(1); writer.Write((ushort)1); writer.Write((ushort)24);
                    writer.Write(0); writer.Write(4);
                    for (int i = 0; i < 4; ++i) writer.Write(0);
                    writer.Write(new byte[] { 0x40, 0x80, 0xff, 0 });
                }
                await Ui(() => { app.Left.HideFind(); app.Left.SetViewMode(ExplorerViewMode.Details); app.Left.Navigate(fixture); });
                await Ready(app.Left);
                PreviewSession text = null!, image = null!, twin = null!, folder = null!;
                nint opener = 0, textHost = 0, imageHost = 0, folderHost = 0;
                bool folderMotion = false;
                await Ui(() =>
                {
                    if (!SystemParametersInfoW(0x1042, 0, out int allowed, 0))
                        throw new InvalidOperationException("Could not read the detached-window motion preference.");
                    folderMotion = allowed != 0;
                    app.Left.Focus();
                    opener = GetAncestor(GetFocus(), 2);
                    text = Show("survivor.txt");
                    image = Show("survivor.bmp");
                    twin = Show("survivor.bmp");
                    folder = Show("alpha");
                    text.CloseButton.Focus();
                    textHost = GetAncestor(GetFocus(), 2);
                    image.CloseButton.Focus();
                    imageHost = GetAncestor(GetFocus(), 2);
                    folder.CloseButton.Focus();
                    folderHost = GetAncestor(GetFocus(), 2);
                    uint thread = GetWindowThreadProcessId(opener, out uint process);
                    foreach (nint host in new[] { textHost, imageHost, folderHost })
                        if (host == opener || GetWindow(host, 4) != 0 || !IsWindowVisible(host)
                            || GetWindowThreadProcessId(host, out uint other) != thread || other != process
                            || process != Environment.ProcessId || GetDpiForWindow(host) == 0)
                            throw new InvalidOperationException("Preview HWNDs must be visible ownerless documents on the Explorer STA.");
                    GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();

                    PreviewSession Show(string name)
                    {
                        app.Left.SelectPath(Path.Combine(fixture, name));
                        app.Preview.ShowSelected(app.Left);
                        if (app.Preview.Current is not { IsOpen: true } current || current.Target.Name != name)
                            throw new InvalidOperationException($"Could not select the {name} lifetime fixture.");
                        if (name == "alpha") current.EntryReveal.Duration = 10000;
                        return app.Preview.Current!;
                    }
                });
                await Until(() => !text.Pending && !folder.Pending && !text.EntryReveal.Animating
                    && !image.EntryReveal.Animating && !twin.EntryReveal.Animating
                    && image.Image.Status == ImageStatus.Ready && twin.Image.Status == ImageStatus.Ready);
                await Check(() => image.Image.ControlStyle is not null
                    && ReferenceEquals(image.Image.ControlStyle, twin.Image.ControlStyle),
                    "Independent preview windows share the immutable declarative image style");
                await Ui(twin.Dismiss);
                await Until(() => twin.IsDisposed);
                await Check(() => image.Image.Status == ImageStatus.Ready && image.IsOpen,
                    "Retiring one image preview does not invalidate another window's cached pixels");
                await Ui(() =>
                {
                    if (folderMotion && !folder.EntryReveal.Animating)
                        throw new InvalidOperationException("The owner-first fixture must close Explorer during preview entry.");
                    app.Window.Close();
                });
                await Until(() => app.IsDisposed);
                await Check(() => !IsWindow(opener) && IsWindow(textHost) && IsWindow(imageHost)
                    && text.IsOpen && image.IsOpen && folder.IsOpen, "Previews survive native opener destruction and managed disposal");
                await Check(() => !folderMotion ||
                    (folder.EntryReveal.Animating && SendMessageW(folderHost, 0x803C, 33, 0) != 0),
                    "An ownerless preview keeps its own entry clock after Explorer is disposed");
                float previousWidth = 0;
                await Ui(() =>
                {
                    previousWidth = image.Bounds.Width;
                    if (!SetWindowPos(imageHost, 0, 70, 80, 680, 480, 0x14))
                        throw new InvalidOperationException("The surviving image window could not move and resize.");
                    text.Text.Focus();
                    SendMessageW(GetFocus(), 0x00b1, 0, 3);
                    SendMessageW(GetFocus(), 0x301, 0, 0);
                    if (text.Window.KeyHandler?.Invoke(new(0x57, KeyModifiers.Control, text.Text.Id)) != false)
                        throw new InvalidOperationException("A surviving preview routed an Explorer shortcut.");
                });
                await Until(() =>
                {
                    if (!OpenClipboard(textHost)) return false;
                    try
                    {
                        nint data = GetClipboardData(13), value = GlobalLock(data);
                        try
                        {
                            string? copied = value == 0 ? null : Marshal.PtrToStringUni(value);
                            if (copied != "abc")
                                throw new InvalidOperationException($"Surviving native RichEdit copy failed: content={text.Text.Text}, selection={text.Text.Selection}, clipboard={copied}, focused={text.Text.Focused}.");
                        }
                        finally { if (value != 0) GlobalUnlock(data); }
                    }
                    finally { CloseClipboard(); }
                    return true;
                });
                await Ui(folder.Open);
                await Until(() => folder.IsDisposed && image.Bounds.Width != previousWidth);
                await Check(() => app.Preview.LastOpenedPath == Path.Combine(fixture, "alpha")
                    && image.Image.Status == ImageStatus.Ready && image.Image.GetBounds().Height > 0,
                    "Open uses the captured folder without a live Explorer pane; surviving image remains visible after resize");
                await Ui(image.Dismiss);
                await Until(() => image.IsDisposed);
                await Ui(text.Open);
                // Last-window retirement and queued disposal finish before Application.Run returns.
            }

            async Task PerColumnFindChecks()
            {
                var pane = app.Left;
                string root = Path.Combine(fixture, "per-column-find");
                string top = Path.Combine(root, "top");
                string middle = Path.Combine(top, "middle");
                string leaf = Path.Combine(middle, "leaf");
                Directory.CreateDirectory(leaf);
                Directory.CreateDirectory(Path.Combine(root, "elsewhere"));
                await File.WriteAllTextAsync(Path.Combine(top, "note.txt"), "note");
                await File.WriteAllTextAsync(Path.Combine(middle, "other.txt"), "other");
                await File.WriteAllTextAsync(Path.Combine(leaf, "inside.txt"), "inside");
                await Ui(() => pane.Navigate(root));
                await Ready(pane);
                await Ui(() => pane.SetViewMode(ExplorerViewMode.Columns));
                await Ready(pane);
                await Ui(() => pane.SelectColumnPath(0, top));
                await Ready(pane);
                await Ui(() => pane.SelectColumnPath(1, middle));
                await Ready(pane);
                await Ui(() => pane.SelectColumnPath(2, leaf));
                await Ready(pane);
                ExplorerColumn[] path = [];
                ulong[] peers = [];
                nint ancestorList = 0;
                await Ui(() =>
                {
                    path = pane.Model.Active.Columns.ToArray();
                    peers = Enumerable.Range(0, 4).Select(i => pane.Columns.Column((uint)i).Id).ToArray();
                    pane.HideFind();
                    pane.Columns.FocusColumn(1);
                    ancestorList = GetFocus();
                    pane.Columns.FocusColumn(2);
                    pane.Columns.HorizontalOffset = Math.Min(pane.Columns.ColumnWidth, pane.Columns.MaximumHorizontalOffset);
                });
                await Ui(() =>
                {
                    if (!GetWindowRect(ancestorList, out var listBounds))
                        throw new InvalidOperationException("The ancestor list bounds are unavailable.");
                    nint header = 0, parent = GetAncestor(ancestorList, 1);
                    for (nint child = FindWindowExW(parent, 0, null, null); child != 0;
                        child = FindWindowExW(parent, child, null, null))
                        if (GetWindowRect(child, out var bounds) && bounds.Left == listBounds.Left &&
                            bounds.Right == listBounds.Right && bounds.Bottom == listBounds.Top && bounds.Top < bounds.Bottom)
                        {
                            header = child;
                            break;
                        }
                    if (header == 0) throw new InvalidOperationException("The ancestor directory header is missing.");
                    SendMessageW(header, 0x201, 1, (16 << 16) | 12);
                    ancestorList = GetFocus();
                    if (!pane.Columns.Column(1).Focused || pane.Columns.ActiveColumn != 1 ||
                        !path.SequenceEqual(pane.Model.Active.Columns))
                        throw new InvalidOperationException("Clicking an ancestor header must focus it without changing the path.");
                    if (!PostMessageW(GetFocus(), 0x100, 0x4e, 1))
                        throw new InvalidOperationException("Could not type into the ancestor column.");
                });
                await Until(() => pane.FindInput.Focused && pane.FilterQuery == "n" && !pane.IsFiltering);
                await Check(() => pane.Columns.ActiveColumn == 1 && pane.VisibleCount == 1 &&
                    pane.Model.Active.Path == leaf && path.SequenceEqual(pane.Model.Active.Columns) &&
                    path[1].SelectedPath == middle && pane.SelectedEntry is null &&
                    !pane.HasSelection && pane.Status.Text.Contains("1 of 2"),
                    "Type-to-find targets an ancestor, hides its selected branch, and preserves all descendants");
                await Ui(() => pane.SetFilter("no matches"));
                await Ready(pane);
                await Check(() => pane.VisibleCount == 0 && path[1].SelectedPath == middle &&
                    path.SequenceEqual(pane.Model.Active.Columns) &&
                    peers.SequenceEqual(Enumerable.Range(0, 4).Select(i => pane.Columns.Column((uint)i).Id)),
                    "An empty ancestor result preserves logical path selection and retained column peers");
                await Ui(() => SendMessageW(ancestorList, 0x201, 1, (80 << 16) | 12));
                await Check(() => pane.Columns.Column(1).Focused && pane.FilterQuery == "no matches" &&
                    pane.Model.Active.Path == leaf && pane.Columns.ColumnCount == 4,
                    "Clicking an empty filtered column focuses it without clearing its query or descendants");
                await Ui(() => pane.SetFilter("mid"));
                await Ready(pane);
                await Check(() => pane.VisibleCount == 1 && pane.SelectedEntry?.FullPath == middle &&
                    pane.Model.Active.Path == leaf && pane.Columns.ColumnCount == 4,
                    "Restoring a matching branch restores its selection without navigating or trimming descendants");
                await Ui(() =>
                {
                    pane.Columns.FocusColumn(2);
                    pane.ShowFind();
                    pane.SetFilter("other");
                });
                await Ready(pane);
                await Check(() => path[1].Filter == "mid" && path[2].Filter == "other" &&
                    path[2].SelectedPath == leaf && pane.VisibleCount == 1 && pane.Columns.ColumnCount == 4,
                    "Each column keeps its own query and hidden path selection");
                await Ui(() => pane.Columns.FocusColumn(1));
                await Check(() => pane.FilterQuery == "mid" && pane.FindInput.Text == "mid" &&
                    pane.Columns.ActiveColumn == 1 && pane.FilesFocused,
                    "Refocusing an ancestor restores its remembered query without moving focus into Find");
                await Ui(pane.HideFind);
                await Ready(pane);
                await Check(() => path[1].Filter == "" && path[2].Filter == "other" &&
                    pane.VisibleCount == 2 && pane.Model.Active.Path == leaf,
                    "Closing Find clears only its target column and preserves the open path");
                await Ui(() =>
                {
                    pane.ShowFind();
                    pane.SetFilter("no matches");
                    pane.Columns.FocusColumn(2);
                    pane.SetFilter("leaf");
                    pane.Columns.FocusColumn(1);
                });
                await Ready(pane);
                await Check(() => pane.FindInput.Text == "no matches" && pane.FilterQuery == "no matches" &&
                    pane.VisibleCount == 0 && path[2].Filter == "leaf" &&
                    pane.Model.Active.Path == leaf && pane.Columns.ActiveColumn == 1,
                    "Rapid query and target changes cannot deliver results into the wrong Find target");
                ExplorerTab? original = null;
                ulong duplicate = 0;
                await Ui(() =>
                {
                    original = pane.Model.Active;
                    pane.DuplicateTab(original);
                    duplicate = pane.Model.Active.Id;
                });
                await Ready(pane);
                await Check(() => pane.FilterQuery == "no matches" &&
                    pane.Model.Active.Columns[2].Filter == "leaf" &&
                    !ReferenceEquals(path[1], pane.Model.Active.Columns[1]),
                    "Tab duplication copies independent per-column queries and restores the Find target");
                await Ui(() => pane.SetFilter("note"));
                await Ready(pane);
                await Check(() => path[1].Filter == "no matches" && pane.VisibleCount == 1,
                    "Editing a duplicate's query does not change the original tab");
                await Ui(() =>
                {
                    pane.CloseTab(duplicate);
                    pane.SelectTab(original?.Id ?? throw new InvalidOperationException("The original column tab is missing."));
                });
                await Ready(pane);
                await Check(() => ReferenceEquals(original, pane.Model.Active) &&
                    pane.FilterQuery == "no matches" && pane.Model.Active.Columns[2].Filter == "leaf",
                    "Tab restoration restores the original target and all column queries");
                await Ui(() =>
                {
                    pane.SetFilter("obsolete");
                    pane.SelectColumnPath(0, Path.Combine(root, "elsewhere"));
                });
                await Ready(pane);
                await Check(() => pane.Columns.ColumnCount == 2 && pane.Model.Active.Path == Path.Combine(root, "elsewhere") &&
                    pane.Model.Active.Columns[1].Filter == "" && pane.FilterQuery == "" &&
                    !ReferenceEquals(path[1], pane.Model.Active.Columns[1]),
                    "Replacing a branch cancels obsolete filters and gives replacement columns fresh query state");
                await Ui(() => { pane.HideFind(); pane.SetViewMode(ExplorerViewMode.Details); pane.Navigate(fixture); });
                await Ready(pane);
            }

            async Task<string> CreateViewportFixture()
            {
                string many = Path.Combine(fixture, "many");
                Directory.CreateDirectory(many);
                for (int i = 0; i < 80; i++)
                    await File.WriteAllTextAsync(Path.Combine(many, $"item-{i:D3}.txt"), "row");
                return many;
            }

            async Task AdditionalViewsChecks()
            {
                var pane = app.Left;
                string root = Path.Combine(fixture, "view-modes");
                string folder = Path.Combine(root, "folder");
                string nested = Path.Combine(folder, "nested");
                string missing = Path.Combine(folder, "missing");
                string leaf = Path.Combine(nested, "leaf.txt");
                Directory.CreateDirectory(nested);
                Directory.CreateDirectory(missing);
                await File.WriteAllTextAsync(leaf, "nested leaf");
                for (int i = 0; i < 80; i++)
                    await File.WriteAllTextAsync(Path.Combine(root, $"file-{i:D3}.txt"), "file");
                string selected = Path.Combine(root, "file-010.txt");
                try
                {
                    await Ui(() => pane.Navigate(root));
                    await Ready(pane);
                    await Ui(() => pane.SelectPath(selected));
                    ExplorerViewMode[] modes = [ExplorerViewMode.ExtraLargeIcons, ExplorerViewMode.LargeIcons,
                        ExplorerViewMode.MediumIcons, ExplorerViewMode.List, ExplorerViewMode.Tree,
                        ExplorerViewMode.Details, ExplorerViewMode.Columns];
                    await Ui(pane.ViewModeButton.Invoke);
                    await Ui(() =>
                    {
                        double previous = double.NegativeInfinity;
                        foreach (var view in modes)
                        {
                            double y = pane.ViewOption(view).GetBounds().Y;
                            if (y <= previous) throw new InvalidOperationException("The view menu must keep Columns below Details.");
                            previous = y;
                        }
                        pane.ViewMenu.Dismiss();
                    });
                    foreach (var view in modes)
                    {
                        await Ui(pane.ViewModeButton.Invoke);
                        await Check(() => pane.ViewMenu.IsOpen, "View menu opens for each presentation");
                        await Ui(() => pane.ViewOption(view).Invoke());
                        await Ready(pane);
                        await Check(() => !pane.ViewMenu.IsOpen && pane.Model.Active.ViewMode == view
                            && pane.VisibleCount == 81 && pane.FilesFocused
                            && pane.SelectedEntry?.FullPath == selected,
                            $"{view} keeps the folder, selection, and native focus");
                        if (view == ExplorerViewMode.Columns)
                            await Ui(() =>
                            {
                                var list = pane.Columns.Column(0);
                                list.Offset = 0;
                                pane.Columns.FocusColumn(0);
                                nint peer = GetFocus();
                                double scale = GetDpiForWindow(peer) / 96.0;
                                foreach (int row in new[] { 1, 2 })
                                {
                                    nint point = ((int)((row * 32 + 4) * scale) << 16) | (int)(48 * scale);
                                    SendMessageW(peer, 0x201, 1, point);
                                    SendMessageW(peer, 0x202, 0, point);
                                    if (pane.SelectedEntry?.Name != $"file-{row - 1:D3}.txt"
                                        || pane.Model.Active.Path != root || pane.Columns.ColumnCount != 1)
                                        throw new InvalidOperationException("Columns must use 32-DIP row selection targets like Details without opening files.");
                                }
                                list.Offset = 32;
                                nint scrolledPoint = ((int)(4 * scale) << 16) | (int)(48 * scale);
                                SendMessageW(peer, 0x201, 1, scrolledPoint);
                                SendMessageW(peer, 0x202, 0, scrolledPoint);
                                if (pane.SelectedEntry?.Name != "file-000.txt")
                                    throw new InvalidOperationException("Column scrolling must use the compact row height.");
                                pane.SelectPath(selected);
                            });
                        if (view == ExplorerViewMode.Tree)
                            await Ui(() =>
                            {
                                var style = pane.Tree.GetControlStyleValues(StylePart.Root, effective: true);
                                if (pane.Tree.ControlStyle is null
                                    || !ReferenceEquals(pane.Tree.ControlStyle, app.Right.Tree.ControlStyle)
                                    || style.RowHeight != 24 || style.FontSize != 12 || style.Indentation != 16
                                    || pane.Tree.GetControlStyleValues(StylePart.Icon, effective: true).Size != 16)
                                    throw new InvalidOperationException("Tree must use compact single-line rows, small icons, and shallow nesting.");
                                pane.Tree.Offset = 0;
                                pane.Tree.Focus();
                                nint peer = GetFocus();
                                double scale = GetDpiForWindow(peer) / 96.0;
                                nint point = ((int)(36 * scale) << 16) | (int)((pane.Tree.GetBounds().Width - 32) * scale);
                                SendMessageW(peer, 0x201, 1, point);
                                SendMessageW(peer, 0x202, 0, point);
                                if (pane.SelectedEntry?.Name != "file-000.txt" || pane.Model.Active.Path != root)
                                    throw new InvalidOperationException("Tree metadata cells must share the 24-DIP row selection target without opening a folder.");
                                pane.SelectPath(selected);
                            });
                        if (view is ExplorerViewMode.MediumIcons or ExplorerViewMode.LargeIcons or ExplorerViewMode.ExtraLargeIcons)
                            await Ui(() =>
                            {
                                var (width, height) = view switch
                                {
                                    ExplorerViewMode.ExtraLargeIcons => (256, 288),
                                    ExplorerViewMode.LargeIcons => (160, 192),
                                    _ => (96, 128)
                                };
                                pane.Items.Offset = 0;
                                pane.Items.Focus();
                                nint peer = GetFocus();
                                double scale = GetDpiForWindow(peer) / 96.0;
                                int columns = Math.Max(1, (int)((pane.Items.GetBounds().Width - 12) / width));
                                nint point = ((int)((height + 10) * scale) << 16) | (int)(10 * scale);
                                SendMessageW(peer, 0x201, 1, point);
                                SendMessageW(peer, 0x202, 0, point);
                                if (pane.SelectedEntry?.Name != $"file-{columns - 1:D3}.txt")
                                    throw new InvalidOperationException($"{view} must use its gallery tile dimensions for pointer selection.");
                                pane.SelectPath(selected);
                            });
                        await Ui(() => pane.ShowFind());
                        await Ui(() => pane.SetFilter("file-01"));
                        await Ready(pane);
                        await Check(() => pane.VisibleCount == 10 && pane.FindInput.Focused,
                            $"{view} shares native Find without taking focus");
                        await Ui(() => Shortcut(0x23, KeyModifiers.Control));
                        await Check(() => pane.SelectedEntry?.Name == "file-019.txt" && pane.FindInput.Focused,
                            $"{view} Find navigation reaches the last match");
                        await Ui(pane.HideFind);
                        await Ready(pane);
                        await Ui(() => pane.SelectPath(selected));
                        if (pane.Model.Active.ViewMode != ExplorerViewMode.Columns)
                            await Ui(() =>
                            {
                                if (pane.IsItems) pane.Items.SelectAll();
                                else if (pane.IsTree) pane.Tree.SelectAll();
                                else pane.Grid.SelectAll();
                                if (pane.SelectedEntries.Length != 81 || !pane.HasSelection)
                                    throw new InvalidOperationException($"{view} must retain exact multiselection.");
                                pane.SelectPath(selected);
                            });
                    }
                    await Ui(() =>
                    {
                        pane.SetViewMode(ExplorerViewMode.ExtraLargeIcons);
                        pane.SetViewMode(ExplorerViewMode.Tree);
                        pane.SetViewMode(ExplorerViewMode.MediumIcons);
                    });
                    await Ready(pane);
                    await Check(() => pane.Model.Active.ViewMode == ExplorerViewMode.MediumIcons
                        && pane.SelectedEntry?.FullPath == selected && pane.Items.Focused,
                        "Rapid view changes keep only the latest presentation without losing selection");
                    await Ui(() =>
                    {
                        pane.Items.Offset = 128;
                        pane.CaptureViewport();
                        pane.NewTab(root);
                    });
                    await Ready(pane);
                    await Check(() => pane.Model.Active.ViewMode == ExplorerViewMode.Details,
                        "New tabs default to Details");
                    await Ui(() => pane.CloseTab());
                    await Ready(pane);
                    await Check(() => pane.Model.Active.ViewMode == ExplorerViewMode.MediumIcons
                        && pane.SelectedEntry?.FullPath == selected && pane.Items.Offset == 128,
                        "Returning to a tab restores its gallery, selection, and scroll");

                    await Ui(() => pane.SetViewMode(ExplorerViewMode.Tree));
                    await Ready(pane);
                    await Ui(() =>
                    {
                        pane.SelectPath(folder);
                        var key = pane.Tree.Selection.Focused!.Value;
                        pane.Tree.Expand(key);
                        pane.Tree.Expand(key, false);
                    });
                    await Until(() => pane.TreeController.PendingCount == 0);
                    await Check(() => pane.SelectedEntry?.FullPath == folder && pane.TreeController.PendingCount == 0,
                        "Collapsing an in-flight Tree branch rejects its obsolete result");
                    await Ui(() => pane.Tree.Expand(pane.Tree.Selection.Focused!.Value));
                    await Until(() => pane.TreeController.PendingCount == 0);
                    await Ui(() =>
                    {
                        pane.Tree.Offset = 0;
                        pane.Tree.Focus();
                        nint peer = GetFocus();
                        double scale = GetDpiForWindow(peer) / 96.0;
                        nint point = ((int)(36 * scale) << 16) | (int)((pane.Tree.GetBounds().Width - 32) * scale);
                        SendMessageW(peer, 0x201, 1, point);
                        SendMessageW(peer, 0x202, 0, point);
                        if (pane.SelectedEntry?.FullPath != missing || pane.Model.Active.Path != root
                            || pane.TreeController.PendingCount != 0)
                            throw new InvalidOperationException("Expanded children must use the same compact full-row metadata selection as root entries.");
                    });
                    await Ui(() =>
                    {
                        pane.SelectPath(nested);
                        if (pane.SelectedEntry?.FullPath != nested)
                            throw new InvalidOperationException("Tree expansion must expose child folders without navigating.");
                        pane.Tree.Expand(pane.Tree.Selection.Focused!.Value);
                    });
                    await Until(() => pane.TreeController.PendingCount == 0);
                    await Ui(() => pane.SelectPath(leaf));
                    await Check(() => pane.Model.Active.Path == root && pane.SelectedEntry?.FullPath == leaf
                        && pane.ContextMenu.GetCommands().Any(command => command.Id == FileContextMenu.Copy),
                        "Nested Tree selection drives file commands without changing the committed folder");
                    await Ui(pane.Refresh);
                    await Ready(pane);
                    await Until(() => pane.TreeController.PendingCount == 0 && !pane.TreeController.Restoring);
                    await Check(() => pane.SelectedEntry?.FullPath == leaf,
                        "Tree refresh restores the selected descendant through lazy ancestor expansion");
                    Directory.Delete(missing);
                    await Ui(() =>
                    {
                        pane.SelectPath(missing);
                        pane.Tree.Expand(pane.Tree.Selection.Focused!.Value);
                    });
                    await Until(() => pane.TreeController.PendingCount == 0);
                    await Check(() => app.Notification.Text.Contains($"Cannot open {missing}", StringComparison.Ordinal)
                        && pane.Model.Active.Path == root,
                        "Tree read errors remain explicit without navigating or replacing the folder");
                    Directory.CreateDirectory(missing);
                    string recovered = Path.Combine(missing, "recovered.txt");
                    await File.WriteAllTextAsync(recovered, "retry");
                    await Ui(() => pane.Tree.Expand(pane.Tree.Selection.Focused!.Value));
                    await Until(() => pane.TreeController.PendingCount == 0);
                    await Ui(() => pane.SelectPath(recovered));
                    await Check(() => pane.SelectedEntry?.FullPath == recovered,
                        "Tree branches can retry a failed directory read");
                    await Ui(() =>
                    {
                        pane.SelectPath(folder);
                        var key = pane.Tree.Selection.Focused!.Value;
                        pane.Tree.Expand(key, false);
                        pane.Tree.Expand(key);
                        pane.SetViewMode(ExplorerViewMode.List);
                    });
                    await Ready(pane);
                    await Check(() => pane.TreeController.PendingCount == 0 && pane.Items.Focused,
                        "Leaving Tree retires its requests and native focus");
                    await Ui(() => pane.SetFilter("no-such-file"));
                    await Ready(pane);
                    await Check(() => pane.VisibleCount == 0 && !pane.HasSelection && pane.SelectedEntry is null,
                        "An empty List filter cannot activate stale files");
                }
                finally
                {
                    await Ui(() =>
                    {
                        pane.SetViewMode(ExplorerViewMode.Details);
                        pane.Navigate(fixture);
                    });
                    await Ready(pane);
                    Directory.Delete(root, recursive: true);
                    await Ui(pane.Refresh);
                    await Ready(pane);
                }
            }

            async Task ViewSwitchChecks()
            {
                var pane = app.Left;
                string path = Path.Combine(fixture, "many");
                string selected = Path.Combine(path, "item-015.txt");
                // Keep the saved offset inside even the shortest gallery's scroll range.
                const double scrollOffset = 128;
                nint gridPeer = 0, owner = 0;
                ElementBounds slot = default, address = default, footer = default;
                await Ui(() => { pane.SetViewMode(ExplorerViewMode.Details); pane.Navigate(path); });
                await Ready(pane);
                await Ui(() =>
                {
                    pane.SelectPath(selected);
                    pane.Grid.Offset = scrollOffset;
                    pane.Focus();
                    gridPeer = GetFocus();
                    owner = GetAncestor(gridPeer, 2);
                    SendMessageW(owner, 0x800C, 0, 0);
                    slot = pane.ViewBounds;
                    address = pane.Address.GetBounds();
                    footer = pane.Footer.GetBounds();
                    if (gridPeer == 0) throw new InvalidOperationException("View switches require a native file peer.");
                });
                await Until(() => SendMessageW(owner, 0x803C, 33, 0) == 0);
                foreach (var view in new[] { ExplorerViewMode.Columns, ExplorerViewMode.Details,
                    ExplorerViewMode.MediumIcons, ExplorerViewMode.LargeIcons, ExplorerViewMode.ExtraLargeIcons,
                    ExplorerViewMode.List, ExplorerViewMode.Tree, ExplorerViewMode.Details })
                {
                    await Ui(() =>
                    {
                        pane.SetViewMode(view);
                        SendMessageW(owner, 0x800C, 0, 0);
                        if (pane.Model.Active.ViewMode != view || pane.ViewBounds != slot
                            || SendMessageW(owner, 0x803C, 33, 0) != 0)
                            throw new InvalidOperationException("A view switch must update immediately without starting an animation.");
                    });
                    await Ready(pane);
                    await ObserveStationaryView();
                }
                await Check(() => GetFocus() == gridPeer && pane.Grid.Offset == scrollOffset,
                    "Returning to Details retains its native file peer and saved scroll offset");
                await Ui(() =>
                {
                    pane.SetViewMode(ExplorerViewMode.Columns);
                    pane.SetViewMode(ExplorerViewMode.ExtraLargeIcons);
                    pane.SetViewMode(ExplorerViewMode.Columns);
                });
                await Ready(pane);
                await Check(() => pane.IsColumns && pane.FilesFocused && pane.SelectedEntry?.FullPath == selected,
                    "Rapid view changes publish only the latest mode and selection");
                await ObserveStationaryView();
                await Ui(() => pane.Navigate(fixture));
                await Ready(pane);
                await Check(() => pane.Model.Active.Path == fixture,
                    "Navigation cannot replay a retired view request");
                await Ui(() => pane.SetViewMode(ExplorerViewMode.Details));
                await Ready(pane);

                async Task ObserveStationaryView()
                {
                    nint peer = 0;
                    NativePoint position = default;
                    long started = 0;
                    await Ui(() =>
                    {
                        SendMessageW(owner, 0x800C, 0, 0);
                        peer = GetFocus();
                        if (peer == 0 || !ClientToScreen(peer, ref position))
                            throw new InvalidOperationException("The selected view must have its native peer immediately.");
                        if (pane.Model.Active.ViewMode != ExplorerViewMode.Details && IsWindowVisible(gridPeer))
                            throw new InvalidOperationException("The outgoing Details peer must be hidden immediately.");
                        started = Stopwatch.GetTimestamp();
                    });
                    await Until(() =>
                    {
                        NativePoint current = default;
                        ElementBounds content = pane.IsColumns ? pane.Columns.GetBounds() :
                            pane.IsTree ? pane.Tree.GetBounds() : pane.IsItems ? pane.Items.GetBounds() : pane.Grid.GetBounds();
                        if (pane.ViewBounds != slot || content != slot || pane.Address.GetBounds() != address
                            || pane.Footer.GetBounds() != footer || GetFocus() != peer || !ClientToScreen(peer, ref current)
                            || current.X != position.X || current.Y != position.Y || !pane.FilesFocused
                            || pane.SelectedEntry?.FullPath != selected || pane.Model.Active.ScrollOffset != scrollOffset
                            || SendMessageW(owner, 0x803C, 33, 0) != 0)
                            throw new InvalidOperationException(
                                $"View changes must stay stationary without an animation timer: mode={pane.Model.Active.ViewMode}, " +
                                $"slot={slot}, view={pane.ViewBounds}, content={content}, toolbarStable={pane.Address.GetBounds() == address}, footerStable={pane.Footer.GetBounds() == footer}, " +
                                $"peer={peer:X}, focus={GetFocus():X}, position={position.X},{position.Y}, current={current.X},{current.Y}, " +
                                $"filesFocused={pane.FilesFocused}, selected={pane.SelectedEntry?.FullPath}, expected={selected}, offset={pane.Model.Active.ScrollOffset}, timer={SendMessageW(owner, 0x803C, 33, 0)}.");
                        return Stopwatch.GetElapsedTime(started) >= TimeSpan.FromMilliseconds(240);
                    });
                }
            }

            async Task PartitionChecks()
            {
                var pane = app.Left;
                bool restoreSplit = false;
                string root = Path.Combine(fixture, "partition");
                string folder = Path.Combine(root, "b-folder");
                Directory.CreateDirectory(Path.Combine(folder, "b-child"));
                await File.WriteAllTextAsync(Path.Combine(root, "a.txt"), "a");
                await File.WriteAllTextAsync(Path.Combine(root, "c.txt"), "ccc");
                await File.WriteAllTextAsync(Path.Combine(folder, "a-child.txt"), "a");
                await File.WriteAllTextAsync(Path.Combine(folder, "c-child.txt"), "ccc");
                await Ui(() =>
                {
                    restoreSplit = app.SecondPaneVisible;
                    pane.Focus();
                    pane.SetViewMode(ExplorerViewMode.Details);
                    pane.Model.Active.SortColumn = 0;
                    pane.Model.Active.SortDescending = false;
                    pane.SetPartition(ExplorerPartition.FoldersFirst);
                    pane.Navigate(root);
                });
                await Ready(pane);
                foreach (var mode in Enum.GetValues<ExplorerViewMode>())
                {
                    await Ui(() =>
                    {
                        pane.SetViewMode(mode);
                        pane.Navigate(root);
                    });
                    await Ready(pane);
                    if (mode == ExplorerViewMode.Columns)
                    {
                        await Ui(() => pane.SelectColumnPath(0, folder));
                        await Ready(pane);
                        await Check(() => pane.Columns.ColumnCount == 2, "Partition fixture opens a retained parent and child column");
                    }
                    foreach (var partition in Enum.GetValues<ExplorerPartition>())
                    {
                        await Ui(pane.PartitionButton.Invoke);
                        await Check(() => pane.PartitionMenu.IsOpen
                            && pane.PartitionButton.GetBounds().X >= pane.ViewModeButton.GetBounds().X + pane.ViewModeButton.GetBounds().Width
                            && pane.PartitionMenu.GetBounds().Y + pane.PartitionMenu.GetBounds().Height <= pane.PartitionButton.GetBounds().Y,
                            "Partition button beside View opens an upward flyout");
                        await Check(() => pane.PartitionMenu.Menu.GetBounds().Height >= 96
                            && pane.PartitionMenu.GetBounds().Height <= 112
                            && pane.PartitionCommands().Select(c => c.Icon).SequenceEqual(
                                new[] { ButtonIcon.FoldersFirst, ButtonIcon.FilesFirst, ButtonIcon.Mixed })
                            && pane.PartitionCommands().Count(c => c.Checked == true) == 1,
                            "Partition flyout uses compact checked menu rows with icons");
                        await Ui(() =>
                        {
                            var menu = FocusPartitionMenu();
                            if (!PostMessageW(menu, 0x100, 0x24, 0))
                                throw new InvalidOperationException("Could not post Home to the partition menu.");
                            for (int row = 0; row < (int)partition; ++row)
                                if (!PostMessageW(menu, 0x100, 0x28, 0))
                                    throw new InvalidOperationException("Could not post Down to the partition menu.");
                            if (!PostMessageW(menu, 0x100, 0x0d, 0))
                                throw new InvalidOperationException("Could not post Enter to the partition menu.");
                        });
                        await Until(() => !pane.PartitionMenu.IsOpen);
                        await Ready(pane);
                        await Check(() => pane.Model.Active.Partition == partition && !pane.PartitionMenu.IsOpen && pane.FilesFocused
                            && PartitionIconMatches(pane) && pane.PartitionButton.GetBounds().Width == 32
                            && pane.PartitionButton.GetBounds().Height == 32,
                            "Choosing a partition stores it and restores file focus");
                        await CheckOrder(partition);
                        if (pane.IsTree)
                        {
                            await Ui(() =>
                            {
                                pane.SelectPath(folder);
                                pane.Tree.Expand(pane.Tree.Selection.Focused!.Value);
                            });
                            await Until(() => pane.TreeController.PendingCount == 0 && !pane.TreeController.Restoring);
                            await Ui(() =>
                            {
                                pane.SelectPath(folder);
                                pane.Tree.Focus();
                                string[] expected = partition switch
                                {
                                    ExplorerPartition.FoldersFirst => ["b-child", "a-child.txt", "c-child.txt"],
                                    ExplorerPartition.FilesFirst => ["a-child.txt", "c-child.txt", "b-child"],
                                    _ => ["a-child.txt", "b-child", "c-child.txt"]
                                };
                                foreach (string name in expected)
                                {
                                    SendMessageW(GetFocus(), 0x100, 0x28, 0);
                                    if (pane.SelectedEntry?.Name != name)
                                        throw new InvalidOperationException("Lazy Tree children must use the active partition order.");
                                }
                                pane.SelectPath(folder);
                                pane.Tree.Expand(pane.Tree.Selection.Focused!.Value, false);
                            });
                        }
                    }
                    await Ui(() =>
                    {
                        pane.SetPartition(ExplorerPartition.FilesFirst);
                        pane.SetPartition(ExplorerPartition.FoldersFirst);
                        pane.SetPartition(ExplorerPartition.Mixed);
                    });
                    await Ready(pane);
                    await CheckOrder(ExplorerPartition.Mixed);
                    bool restoresFocus = false;
                    await Ui(() =>
                    {
                        restoresFocus = GetForegroundWindow() == GetAncestor(GetFocus(), 2);
                        pane.PartitionButton.Invoke();
                        FocusPartitionMenu();
                        if (!PostMessageW(GetFocus(), 0x100, 0x1b, 0))
                            throw new InvalidOperationException("Could not post Escape to the partition flyout.");
                    });
                    await Until(() => !pane.PartitionMenu.IsOpen);
                    await Check(() => pane.Model.Active.Partition == ExplorerPartition.Mixed
                        && (!restoresFocus || pane.FilesFocused),
                        "Escape preserves the partition and restores foreground-window file focus");
                    await Ui(pane.Focus);
                }
                nint FocusPartitionMenu()
                {
                    var owner = GetAncestor(GetFocus(), 2);
                    if (GetForegroundWindow() != owner)
                    {
                        // Native popups do not take focus in an inactive smoke window.
                        var peer = FindMenu(owner);
                        if (peer == 0) throw new InvalidOperationException("The partition menu has no native input peer.");
                        SetFocus(peer);
                    }
                    if (GetFocus() == 0 || pane.FilesFocused)
                        throw new InvalidOperationException("The partition menu did not receive keyboard focus.");
                    return GetFocus();
                }

                static nint FindMenu(nint owner)
                {
                    for (nint child = GetWindow(owner, 5); child != 0; child = GetWindow(child, 2))
                    {
                        var name = new System.Text.StringBuilder(128);
                        GetWindowTextW(child, name, name.Capacity);
                        if (name.ToString() == "Folder and file order in pane 1" && GetWindow(child, 5) == 0)
                            return child;
                        nint nested = FindMenu(child);
                        if (nested != 0) return nested;
                    }
                    return 0;
                }

                ExplorerTab original = null!;
                await Ui(() =>
                {
                    original = pane.Model.Active;
                    pane.NewTab(root);
                });
                await Ready(pane);
                await Check(() => pane.Model.Active.Partition == ExplorerPartition.Mixed && PartitionIconMatches(pane),
                    "New tab inherits active partition and icon");
                await Ui(() =>
                {
                    pane.SetPartition(ExplorerPartition.FilesFirst);
                    pane.DuplicateTab(original);
                });
                await Ready(pane);
                await Check(() => pane.Model.Active.Partition == ExplorerPartition.Mixed
                    && original.Partition == ExplorerPartition.Mixed, "Duplicate inherits its non-active source rather than the active tab");
                await Ui(() =>
                {
                    pane.SetPartition(ExplorerPartition.FoldersFirst);
                    pane.SelectTab(original.Id);
                });
                await Ready(pane);
                await Check(() => pane.Model.Active.Partition == ExplorerPartition.Mixed && PartitionIconMatches(pane),
                    "Tab changes remain independent and restore their icon");
                await Ui(() =>
                {
                    pane.PartitionButton.Invoke();
                    if (pane.PartitionCommands().Single(c => c.Checked == true) is not { Id: 3, Label: "Mixed", Icon: ButtonIcon.Mixed })
                        throw new InvalidOperationException("Partition flyout must identify the current tab's setting.");
                    pane.SelectTab(pane.Model.Tabs.First(tab => tab.Id != original.Id).Id);
                });
                await Ready(pane);
                await Check(() => !pane.PartitionMenu.IsOpen, "Tab switch dismisses stale partition flyout");
                await Ui(() =>
                {
                    if (app.SecondPaneVisible) app.ClosePane(app.Right);
                    pane.SetPartition(ExplorerPartition.FilesFirst);
                    app.ToggleSplit();
                });
                await Ready(app.Right);
                await Check(() => app.Right.Model.Active.Partition == ExplorerPartition.FilesFirst && PartitionIconMatches(app.Right),
                    "A newly opened pane inherits the active partition");
                await Ui(() =>
                {
                    app.Right.SetPartition(ExplorerPartition.FoldersFirst);
                    app.DuplicateInNewPane(pane, original);
                });
                await Ready(app.Right);
                await Check(() => app.Right.Model.Active.Partition == ExplorerPartition.Mixed
                    && pane.Model.Active.Partition == ExplorerPartition.FilesFirst,
                    "A pane duplicate inherits its source and leaves the other tab unchanged");
                foreach (bool duplicate in new[] { true, false })
                {
                    ExplorerApplication? created = null;
                    await Ui(() =>
                    {
                        pane.Focus();
                        app.NewWindow(root, duplicate ? original : null);
                        created = app.ExplorerWindows.Last();
                    });
                    if (created is null || ReferenceEquals(created, app))
                        throw new InvalidOperationException("Partition smoke could not create another window.");
                    await Ready(created.Left);
                    await Check(() => created.Left.Model.Active.Partition ==
                        (duplicate ? ExplorerPartition.Mixed : ExplorerPartition.FilesFirst) && PartitionIconMatches(created.Left),
                        "New windows inherit the source or active partition");
                    await Ui(created.Window.Close);
                    await Until(() => created.IsDisposed);
                }
                Directory.Delete(root, recursive: true);
                await Ui(() =>
                {
                    app.ClosePane(app.Right);
                    pane.ResetTabs(fixture);
                    pane.Navigate(fixture);
                    if (restoreSplit) app.ToggleSplit();
                    pane.Focus();
                });
                await Ready(pane);
                if (restoreSplit) await Ready(app.Right);

                Task CheckOrder(ExplorerPartition partition) => Check(() =>
                {
                    if (!PartitionIconMatches(pane)) return false;
                    string[] names = partition switch
                    {
                        ExplorerPartition.FoldersFirst => ["b-folder", "a.txt", "c.txt"],
                        ExplorerPartition.FilesFirst => ["a.txt", "c.txt", "b-folder"],
                        _ => ["a.txt", "b-folder", "c.txt"]
                    };
                    if (!pane.DisplayedPaths().Select(Path.GetFileName).SequenceEqual(names))
                        throw new InvalidOperationException($"Unexpected {pane.Model.Active.ViewMode}/{partition} order: " +
                            string.Join(", ", pane.DisplayedPaths().Select(Path.GetFileName)));
                    if (!pane.IsColumns) return true;
                    string[] childNames = partition switch
                    {
                        ExplorerPartition.FoldersFirst => ["b-child", "a-child.txt", "c-child.txt"],
                        ExplorerPartition.FilesFirst => ["a-child.txt", "c-child.txt", "b-child"],
                        _ => ["a-child.txt", "b-child", "c-child.txt"]
                    };
                    return pane.Columns.ColumnCount == 2
                        && pane.DisplayedPaths(1).Select(Path.GetFileName).SequenceEqual(childNames)
                        && pane.Model.Active.Columns[0].SelectedPath == folder;
                }, "Partition order applies to every displayed source without losing the parent selection");

                static bool PartitionIconMatches(FilePaneView view)
                {
                    var (icon, label) = view.Model.Active.Partition switch
                    {
                        ExplorerPartition.FoldersFirst => (ButtonIcon.FoldersFirst, "Folders, then files"),
                        ExplorerPartition.FilesFirst => (ButtonIcon.FilesFirst, "Files, then folders"),
                        _ => (ButtonIcon.Mixed, "Mixed")
                    };
                    return view.PartitionButton.Icon == icon && view.PartitionButton.Text == label;
                }
            }

            async Task ColumnsChecks()
            {
                var pane = app.Left;
                string alpha = Path.Combine(fixture, "alpha");
                string beta = Path.Combine(fixture, "beta");
                string small = Path.Combine(fixture, "small.txt");
                await Ui(() => { pane.Focus(); pane.Navigate(fixture); });
                await Ready(pane);
                await Ui(pane.ViewModeButton.Invoke);
                await Check(() => pane.ViewMenu.IsOpen && !pane.IsColumns
                    && pane.ViewModeButton.Icon == ButtonIcon.Library
                    && pane.ViewModeButton.GetBounds().Y >= pane.Footer.GetBounds().Y
                    && pane.ViewMenu.GetBounds().Y + pane.ViewMenu.GetBounds().Height <= pane.ViewModeButton.GetBounds().Y,
                    "Footer view icon opens an upward flyout without changing the current view");
                await Check(() => pane.DetailsOption.Text == "Details (current)" && pane.ColumnsOption.Text == "Columns",
                    "View flyout exposes Details and Columns with the current choice");
                await Ui(pane.ColumnsOption.Invoke);
                await Ready(pane);
                await Check(() => pane.IsColumns && pane.Columns.ColumnCount == 1 && pane.FilesFocused
                    && pane.Model.Active.Path == fixture && !pane.Grid.Focused && !pane.ViewMenu.IsOpen,
                    "Footer flyout enables focused columns without changing the committed path");
                await TypeToFindChecks(pane);
                await Ui(() =>
                {
                    pane.ViewModeButton.Invoke();
                    if (!PostMessageW(GetFocus(), 0x100, 0x1b, 0))
                        throw new InvalidOperationException("Could not post Escape to the view flyout.");
                });
                await Until(() => !pane.ViewMenu.IsOpen);
                await Check(() => pane.IsColumns && pane.FilesFocused,
                    "Escape closes the view flyout without changing the view and restores file focus");
                double columnWidth = 0, horizontalOffset = 0;
                await Ui(() =>
                {
                    columnWidth = pane.Columns.ColumnWidth;
                    pane.Columns.ColumnWidth = 2000;
                });
                bool LastColumnVisible()
                {
                    var viewport = pane.Columns.GetBounds();
                    var last = pane.Columns.Column(pane.Columns.ColumnCount - 1).GetBounds();
                    return last.Width > 0 && last.X >= viewport.X &&
                        last.X + last.Width <= viewport.X + viewport.Width;
                }
                await Ui(() => pane.SelectColumnPath(0, alpha));
                await Ready(pane);
                await Check(() => pane.Columns.ColumnCount == 2 && pane.Model.Active.Path == alpha
                    && pane.Columns.ActiveColumn == 0 && LastColumnVisible(),
                    "Single selection reveals the entire child while retaining its active ancestor");
                await Ui(() => pane.SelectColumnPath(1, Path.Combine(alpha, "child")));
                await Ready(pane);
                await Check(() => pane.Columns.ColumnCount == 3 && pane.Columns.ActiveColumn == 1 && LastColumnVisible(),
                    "Nested selection reveals the entire appended column without moving the active ancestor");
                await Until(() => pane.Columns.MaximumHorizontalOffset > 0);
                await Ui(() =>
                {
                    pane.Columns.FocusColumn(1);
                    horizontalOffset = pane.Columns.MaximumHorizontalOffset / 2;
                    pane.Columns.HorizontalOffset = horizontalOffset;
                });
                await Check(() => pane.Columns.HorizontalOffset == horizontalOffset
                    && pane.Model.Active.Path == Path.Combine(alpha, "child") && pane.Columns.ActiveColumn == 1,
                    "Managed horizontal scrolling survives layout without navigating or changing the active column");
                await Ui(() => pane.Columns.ColumnWidth = columnWidth);
                await Ui(() => pane.SelectColumnPath(0, beta));
                await Ready(pane);
                await Check(() => pane.Columns.ColumnCount == 2 && pane.Model.Active.Path == beta,
                    "Selecting an ancestor sibling replaces descendants");
                int opens = app.FileOpenCount;
                await Ui(() => pane.SelectColumnPath(0, small));
                await Ready(pane);
                await Check(() => pane.Columns.ColumnCount == 1 && pane.Model.Active.Path == fixture
                    && pane.SelectedEntry?.FullPath == small && app.FileOpenCount == opens,
                    "Selecting a leaf trims descendants but never launches it");
                await CommandPaletteChecks();
                await Ui(() =>
                {
                    pane.Columns.FocusColumn(0);
                    if (!PostMessageW(GetFocus(), 0x100, 0x0d, 0))
                        throw new InvalidOperationException("Could not post Enter to the column list.");
                });
                await Until(() => app.FileOpenCount == opens + 1);
                await Ui(() => { pane.SelectColumnPath(0, alpha); pane.SelectColumnPath(0, beta); });
                await Ready(pane);
                await Check(() => pane.Model.Active.Path == beta && pane.Columns.ColumnCount == 2,
                    "Obsolete drill results cannot overwrite a newer sibling");
                await Ui(() =>
                {
                    pane.Columns.FocusColumn(0);
                    pane.ContextMenu.GetCommands();
                    if (!pane.ContextMenu.GetShellPaths().SequenceEqual([beta]))
                        throw new InvalidOperationException("Ancestor context menus must target the active column row.");
                    if (!pane.HasSelection || !pane.SelectedEntries.Select(e => e.FullPath).SequenceEqual([beta])
                        || pane.TransferDirectory != fixture
                        || !pane.ContextMenu.GetCommands().Any(c => c.Id == FileContextMenu.Copy && c.Enabled))
                        throw new InvalidOperationException("Column clipboard commands must use the active ancestor selection and folder.");
                    pane.Columns.FocusColumn(1);
                    if (pane.SelectedEntry is not null)
                        throw new InvalidOperationException("Focusing the empty rightmost column must clear the menu target.");
                    if (pane.HasSelection || pane.SelectedEntries.Length != 0 || pane.TransferDirectory != beta
                        || app.Transfers.QueryDrop(pane, null, FileTransferEffect.Move) != FileTransferEffect.None)
                        throw new InvalidOperationException("An empty column must not reuse a hidden Details selection or drop target.");
                });
                await Ui(() => pane.SelectColumnPath(0, alpha));
                await Ready(pane);
                await Ui(() => { pane.Columns.FocusColumn(1); pane.ShowFind(); pane.SetFilter("child"); });
                await Ready(pane);
                await Check(() => pane.VisibleCount == 1 && pane.Columns.ColumnCount == 2
                    && pane.Model.Active.Columns[0].Snapshot.Entries.Count == 5
                    && pane.Model.Active.Columns[1].SelectedPath is null,
                    "Find filters its focused column without removing siblings or selecting its focus-only row");
                await Ui(() =>
                {
                    foreach (uint key in new uint[] { 0x25, 0x27, 0x24, 0x23 })
                        if (app.Window.KeyHandler?.Invoke(new(key, KeyModifiers.None, pane.FindInput.Id)) == true)
                            throw new InvalidOperationException("Columns must preserve native Find editing keys.");
                    foreach (uint key in new uint[] { 0x43, 0x58, 0x56 })
                        if (app.Window.KeyHandler?.Invoke(new(key, KeyModifiers.Control, pane.FindInput.Id)) == true)
                            throw new InvalidOperationException("Column clipboard commands must preserve native Find clipboard keys.");
                    Shortcut(0x28);
                });
                await Ready(pane);
                await Ui(() =>
                {
                    if (!pane.FindInput.Focused || pane.FindInput.Text != "child" || pane.FilterQuery != "child"
                        || pane.Model.Active.Path != Path.Combine(alpha, "child"))
                        throw new InvalidOperationException(
                            $"Find Down opens the folder and retains the ancestor query without moving input focus: " +
                            $"findFocused={pane.FindInput.Focused}, query={pane.FindInput.Text}, path={pane.Model.Active.Path}, " +
                            $"activeColumn={pane.Columns.ActiveColumn}, columns={pane.Columns.ColumnCount}, filesFocused={pane.FilesFocused}.");
                });
                await Ui(() => pane.SetFilter("child"));
                await Ready(pane);
                ulong columnsTab = 0;
                await Ui(() =>
                {
                    columnsTab = pane.Model.Active.Id;
                    pane.NewTab(fixture);
                    if (pane.Columns.ColumnCount != 0)
                        throw new InvalidOperationException("Changing tabs must immediately detach native column menu sources.");
                });
                await Ready(pane);
                await Check(() => !pane.IsColumns && pane.Model.Active.Filter == "", "New tabs default to Details");
                await Ui(() => pane.SelectTab(columnsTab));
                await Ready(pane);
                await Check(() => pane.IsColumns && pane.Columns.ColumnCount == 3 && pane.FilterQuery == "child",
                    "Tab selection restores mode, chain, and filter");
                await Ui(pane.HideFind);
                await Ready(pane);
                await Ui(() => pane.MoveHistory(-1));
                await Ready(pane);
                await Check(() => pane.Model.Active.Path == alpha && pane.Columns.ColumnCount == 1,
                    "History resets the columns root at the committed destination");
                await Ui(() => pane.Navigate(fixture));
                await Ready(pane);
                await Ui(() =>
                {
                    pane.SelectColumnPath(0, alpha);
                    pane.SetViewMode(ExplorerViewMode.Details);
                    if (pane.Columns.ColumnCount != 0)
                        throw new InvalidOperationException("Hiding Columns must immediately detach native menu sources.");
                });
                await Ready(pane);
                await Check(() => !pane.IsColumns && pane.Model.Active.Path == fixture && pane.VisibleCount == 5,
                    "Mode changes cancel pending drills and preserve committed Details rows");
                await Ui(() => app.Commands.Single(c => c.Name == "Use Columns view").Execute());
                await Ready(pane);
                await Ui(() => pane.Navigate(Path.Combine(fixture, "missing-columns")));
                await Ready(pane);
                await Check(() => pane.IsColumns && pane.Columns.ColumnCount == 1 && pane.Model.Active.Path == fixture
                    && pane.VisibleCount == 5 && pane.Error is not null, "Failed column navigation preserves path and rows with an error");
                await Ui(() =>
                {
                    pane.SelectColumnPath(0, alpha);
                    pane.NewTab(fixture);
                });
                await Ready(pane);
                await Check(() => !pane.IsColumns && pane.Model.Active.Path == fixture,
                    "Tab changes cancel pending column selection");
                await Ui(() => pane.SelectTab(columnsTab));
                await Ready(pane);
                await Check(() => pane.Model.Active.Path == fixture && pane.Columns.ColumnCount == 1,
                    "Canceled inactive-tab work cannot commit later");
                await Ui(pane.Refresh);
                await Ready(pane);
                await Check(() => pane.Columns.ColumnCount == 1 && pane.Error is null, "Refresh rebuilds the columns root");
                await Ui(() => { app.Right.Focus(); app.Right.SetViewMode(ExplorerViewMode.Columns); });
                await Ready(app.Right);
                await Check(() => app.Right.IsColumns && pane.IsColumns && app.Right.FilesFocused
                    && !ReferenceEquals(app.Right.Columns, pane.Columns), "Split panes own independent column views and focus");
                await Ui(() => { pane.Focus(); ClickFirstTab(pane); });
                await Ready(pane);
                await Check(() => pane.FilesFocused, "Tab clicks focus the selected view");
                await Ui(() => app.Commands.Single(c => c.Name == "Use Details view").Execute());
                await Ready(pane);
                await Check(() => !pane.IsColumns && pane.Grid.Focused, "Command palette Details choice restores grid focus");
                await Ui(pane.ViewModeButton.Invoke);
                await Ui(pane.ColumnsOption.Invoke);
                await Ready(pane);
                await Ui(pane.ViewModeButton.Invoke);
                await Ui(pane.DetailsOption.Invoke);
                await Ready(pane);
                await Check(() => !pane.ViewMenu.IsOpen && !pane.IsColumns && pane.Grid.Focused,
                    "Both footer flyout choices close the popup and restore file-view focus");
            }
        }

        async Task CommandPaletteChecks()
        {
            await Ui(app.Palettes.ShowCommands);
            foreach (var (query, count) in new[] { ("Copy", 2), ("Cut files", 1), ("no matching commands", 0), ("", app.Commands.Count) })
            {
                await Ui(() => app.Palettes.EditQuery(query));
                await Check(() => app.Palettes.IsOpen && app.Palettes.ResultCount == count
                    && app.Window.CallbackStatus == 0, "Selection-dependent command rows do not reenter the native window");
            }
            await Ui(app.Palettes.Dismiss);
            await PaletteEntryChecks();
        }

        async Task PaletteEntryChecks()
        {
            try
            {
                foreach (bool navigation in new[] { false, true })
                {
                    nint editor = 0, results = 0;
                    nint owner = await QuietOwner();
                    ElementBounds frame = default, query = default, resultBounds = default;
                    NativePoint editorPosition = default, resultsPosition = default;
                    string text = "";
                    long observed = 0;
                    await Ui(() =>
                    {
                        if (navigation) app.Palettes.ShowNavigation(app.Active);
                        else app.Palettes.ShowCommands();
                        editor = GetFocus();
                        SendMessageW(owner, 0x800C, 0, 0);
                        frame = app.Palettes.Bounds;
                        query = app.Palettes.QueryBounds;
                        if (editor == 0 || !ClientToScreen(editor, ref editorPosition))
                            throw new InvalidOperationException("The palette must immediately focus its stationary native query editor.");
                        results = FindResults(owner);
                        resultBounds = app.Palettes.ResultsBounds;
                        if (results == 0 || !IsWindowVisible(results) || !ClientToScreen(results, ref resultsPosition) ||
                            resultBounds.Width <= 0 || resultBounds.Height <= 0)
                            throw new InvalidOperationException("Palette results must be fully arranged and visible immediately.");
                        NoPaletteTimer(owner);
                        text = navigation ? app.Palettes.QueryText + "a" : "Copy";
                        if (navigation)
                        {
                            int end = app.Palettes.QueryText.Length;
                            SendMessageW(editor, 0x00B1, (nuint)end, end);
                        }
                        foreach (char character in navigation ? "a" : "Copy")
                            SendMessageW(editor, 0x0102, character, 0);
                        SendMessageW(editor, 0x00B1, 1, 3);
                        if (app.Palettes.QueryText != text || app.Palettes.Pending ||
                            app.Palettes.QuerySelection != new TextSelection(1, 3) || SendMessageW(editor, 0x00C6, 0, 0) == 0)
                            throw new InvalidOperationException("Native typing, cached filtering, selection, and undo must work immediately after opening the palette.");
                        NoPaletteTimer(owner);
                        observed = Stopwatch.GetTimestamp();
                    });
                    await Until(() =>
                    {
                        NativePoint current = default, result = default;
                        var nativeText = new System.Text.StringBuilder(text.Length + 1);
                        GetWindowTextW(editor, nativeText, nativeText.Capacity);
                        if (GetFocus() != editor || !IsWindow(results) || FindResults(owner) != results ||
                            !ClientToScreen(editor, ref current) || !ClientToScreen(results, ref result) ||
                            current.X != editorPosition.X || current.Y != editorPosition.Y ||
                            result.X != resultsPosition.X || result.Y != resultsPosition.Y ||
                            app.Palettes.Bounds != frame || app.Palettes.QueryBounds != query ||
                            app.Palettes.ResultsBounds != resultBounds ||
                            app.Palettes.QueryText != text || nativeText.ToString() != text ||
                            app.Palettes.QuerySelection != new TextSelection(1, 3) || SendMessageW(editor, 0x00C6, 0, 0) == 0)
                            throw new InvalidOperationException("Palette results and query must remain stationary with native identities, text, selection, undo, and popup placement preserved.");
                        NoPaletteTimer(owner);
                        return Stopwatch.GetElapsedTime(observed) >= TimeSpan.FromMilliseconds(250);
                    });
                    long dismissed = 0;
                    await Ui(() =>
                    {
                        dismissed = Stopwatch.GetTimestamp();
                        app.Palettes.Dismiss();
                        ClosedImmediately(editor, results);
                    });
                    await Retired(editor, results, dismissed, owner);
                }

                // A cache miss removes the old logical rows before any asynchronous result can arrive.
                string cold = "";
                await Ui(() =>
                {
                    cold = Path.TrimEndingDirectorySeparator(app.Active.Model.Active.Path) + Path.DirectorySeparatorChar
                        + ".xui-palette-missing-" + Guid.NewGuid().ToString("N") + Path.DirectorySeparatorChar;
                    app.Palettes.ShowNavigation(app.Active);
                    app.Palettes.EditQuery(cold);
                    if (!app.Palettes.Pending || app.Palettes.ResultCount != 0 || app.Palettes.SelectedIndex != -1)
                        throw new InvalidOperationException("Cold palette queries must publish an empty logical source, not stale disabled suggestions.");
                    app.Palettes.Accept(false);
                    if (!app.Palettes.IsOpen) throw new InvalidOperationException("Pending palette queries must not execute.");
                    app.Palettes.Dismiss();
                    app.Palettes.ShowCommands();
                    app.Palettes.EditQuery("Copy");
                });
                await Task.Delay(100);
                await Check(() => app.Palettes.IsOpen && !app.Palettes.Pending && app.Palettes.QueryText == "Copy"
                    && app.Palettes.ResultCount == 2, "A retired cold-query generation cannot replace the next palette's commands");
                await Ui(app.Palettes.Dismiss);

                for (int generation = 0; generation < 3; ++generation)
                {
                    nint editor = 0, results = 0, returnFocus = 0;
                    nint owner = await QuietOwner();
                    long started = 0;
                    await Ui(() =>
                    {
                        app.Active.Focus();
                        returnFocus = GetFocus();
                        app.Palettes.ShowCommands();
                        editor = GetFocus();
                        SendMessageW(owner, 0x800C, 0, 0);
                        results = FindResults(owner);
                        if (results == 0 || !IsWindowVisible(results) || app.Palettes.ResultsBounds.Height <= 0)
                            throw new InvalidOperationException("Each palette generation must immediately show its native results.");
                        NoPaletteTimer(owner);
                        started = Stopwatch.GetTimestamp();
                        if (!PostMessageW(editor, 0x0100, 0x1b, 1))
                            throw new InvalidOperationException("Could not post native Escape after opening the palette.");
                    });
                    await Until(() => !app.Palettes.IsOpen);
                    await Ui(() =>
                    {
                        ClosedImmediately(editor, results);
                        if (GetFocus() != returnFocus)
                            throw new InvalidOperationException("Native Escape must immediately restore the previous focus.");
                    });
                    await Retired(editor, results, started, owner);
                }

                bool sidebarOpen = false;
                nint executionEditor = 0, executionResults = 0;
                nint executionOwner = await QuietOwner();
                long executed = 0;
                await Ui(() =>
                {
                    sidebarOpen = app.Sidebar.IsOpen;
                    app.Palettes.ShowCommands();
                    app.Palettes.EditQuery("Toggle navigation pane");
                    nint editor = GetFocus();
                    SendMessageW(GetAncestor(editor, 2), 0x800C, 0, 0);
                    executionEditor = editor;
                    executionResults = FindResults(GetAncestor(editor, 2));
                    if (app.Palettes.ResultCount != 1)
                        throw new InvalidOperationException("The execution fixture must immediately select a real command.");
                    NoPaletteTimer(executionOwner);
                    executed = Stopwatch.GetTimestamp();
                    if (!PostMessageW(editor, 0x0100, 0x0d, 1))
                        throw new InvalidOperationException("Could not post native command execution after opening the palette.");
                });
                await Until(() => !app.Palettes.IsOpen);
                await Ui(() =>
                {
                    ClosedImmediately(executionEditor, executionResults);
                    if (app.Sidebar.IsOpen == sidebarOpen)
                        throw new InvalidOperationException("Enter must dismiss immediately and execute the selected command.");
                });
                // Command execution can start unrelated sidebar motion.
                await Retired(executionEditor, executionResults, executed);
                await Ui(() =>
                {
                    app.Sidebar.Toggle();
                    app.Palettes.ShowCommands();
                    nint editor = GetFocus();
                    SendMessageW(GetAncestor(editor, 2), 0x800C, 0, 0);
                    SendMessageW(editor, 0x0102, 'C', 0);
                    if (app.Palettes.ResultsBounds.Height <= 0 || app.Palettes.QueryText != "C" || GetFocus() != editor)
                        throw new InvalidOperationException("Reopening after command execution must immediately accept native typing with fully arranged results.");
                    app.Palettes.Dismiss();
                });
            }
            finally
            {
                await Ui(() =>
                {
                    if (app.Palettes.IsOpen) app.Palettes.Dismiss();
                });
            }

            async Task<nint> QuietOwner()
            {
                nint owner = 0;
                await Ui(() =>
                {
                    app.Active.Focus();
                    owner = GetAncestor(GetFocus(), 2);
                    if (owner == 0) throw new InvalidOperationException("Could not find the palette owner.");
                    SendMessageW(owner, 0x800C, 0, 0);
                });
                // Let unrelated view/tab motion finish without changing its duration or target.
                await Until(() => SendMessageW(owner, 0x803C, 33, 0) == 0);
                return owner;
            }

            static void NoPaletteTimer(nint owner)
            {
                if (SendMessageW(owner, 0x803C, 33, 0) != 0)
                    throw new InvalidOperationException("Opening, filtering, or dismissing a palette must not start an animation timer on an otherwise settled window.");
            }

            void ClosedImmediately(nint editor, nint results)
            {
                bool editorVisible = IsWindowVisible(editor), resultsVisible = IsWindowVisible(results);
                if (app.Palettes.IsOpen || app.Palettes.Pending || editorVisible || resultsVisible ||
                    GetFocus() == editor || GetFocus() == results)
                    throw new InvalidOperationException(
                        $"Palette dismissal must immediately hide its peers, cancel suggestions, and release focus: " +
                        $"open={app.Palettes.IsOpen}, pending={app.Palettes.Pending}, " +
                        $"editorVisible={editorVisible}, resultsVisible={resultsVisible}, " +
                        $"focus={GetFocus()}, editor={editor}, results={results}.");
            }

            async Task Retired(nint editor, nint results, long started, nint quietOwner = default)
            {
                // Popup pruning defers HWND destruction until the dismissal callback has returned.
                await Until(() =>
                {
                    bool editorAlive = IsWindow(editor), resultsAlive = IsWindow(results);
                    if (quietOwner != 0) NoPaletteTimer(quietOwner);
                    var elapsed = Stopwatch.GetElapsedTime(started);
                    if (elapsed >= TimeSpan.FromSeconds(2))
                        throw new InvalidOperationException(
                            $"Palette peer retirement must complete promptly: elapsed={elapsed.TotalMilliseconds:F0}ms, " +
                            $"editorAlive={editorAlive}, resultsAlive={resultsAlive}, " +
                            $"open={app.Palettes.IsOpen}, pending={app.Palettes.Pending}.");
                    return !editorAlive && !resultsAlive;
                });
            }

            static nint FindResults(nint owner)
            {
                for (nint child = GetWindow(owner, 5); child != 0; child = GetWindow(child, 2))
                {
                    var name = new System.Text.StringBuilder(128);
                    GetWindowTextW(child, name, name.Capacity);
                    if (name.ToString() == "Palette results") return child;
                    nint nested = FindResults(child);
                    if (nested != 0) return nested;
                }
                return 0;
            }
        }

        async Task DriveNavigationChecks(string fixture)
        {
            string root = Path.GetPathRoot(fixture)!;
            foreach (string query in new[] { root, root[..2].ToLowerInvariant() })
            {
                await Ui(() =>
                {
                    app.Palettes.ShowNavigation(app.Left);
                    app.Palettes.EditQuery(query);
                });
                await Until(() => !app.Palettes.Pending);
                await Ui(() =>
                {
                    if (app.Palettes.SelectedIndex != -1)
                        throw new InvalidOperationException("Root queries must not select a child automatically.");
                    if (app.Palettes.ResultCount > 0)
                    {
                        Shortcut(0x26);
                        if (app.Palettes.SelectedIndex != app.Palettes.ResultCount - 1)
                            throw new InvalidOperationException("Up from an unselected root query must select the last child.");
                        Shortcut(0x28);
                        if (app.Palettes.SelectedIndex != 0)
                            throw new InvalidOperationException("Down must wrap to the first child.");
                        app.Palettes.EditQuery(query);
                    }
                });
                await Until(() => !app.Palettes.Pending);
                await Ui(() => app.Palettes.Accept(false));
                await Ready(app.Left);
                await Check(() => string.Equals(app.Left.Model.Active.Path, root, StringComparison.OrdinalIgnoreCase)
                    && !app.Palettes.IsOpen && app.Left.Error is null,
                    "Enter on a drive root opens the drive, not its first child or drive-relative directory");
                await CaptionCheck(app.Left);
            }
            await Ui(() => app.Left.Navigate(fixture));
            await Ready(app.Left);
        }

        async Task TabMenuChecks(string fixture)
        {
            var pane = app.Left;
            ulong first = 0, second = 0;
            await Ui(() =>
            {
                pane.ResetTabs(fixture);
                first = pane.Model.Active.Id;
                pane.NewTab(Path.Combine(fixture, "alpha"));
                second = pane.Model.Active.Id;
            });
            await Ready(pane);
            await Ui(() =>
            {
                pane.Tabs.Focus();
                nint strip = GetFocus();
                ulong requested = 0;
                int requests = 0;
                pane.Tabs.OnContextMenu(id => { requested = id; requests++; return []; }, _ => { });
                try
                {
                    var point = new NativePoint
                    {
                        X = (int)(12 * GetDpiForWindow(strip) / 96),
                        Y = (int)(12 * GetDpiForWindow(strip) / 96)
                    };
                    if (!ClientToScreen(strip, ref point))
                        throw new InvalidOperationException("Cannot locate the tab for its native context menu.");
                    SendMessageW(strip, 0x007B, (nuint)strip, (point.Y << 16) | (point.X & 0xffff));
                    if (requests != 1 || requested != first || pane.Model.Active.Id != second)
                        throw new InvalidOperationException("Right-click must target the inactive tab without selecting it.");
                    SendMessageW(strip, 0x007B, (nuint)strip, -1);
                    if (requests != 2 || requested != second)
                        throw new InvalidOperationException("Keyboard menus must target the selected tab.");
                }
                finally { pane.Tabs.OnContextMenu(pane.TabMenu.GetCommands, pane.TabMenu.Invoke); }
                var commands = pane.TabMenu.GetCommands(first);
                if (commands.Count(command => command.Kind == CommandKind.Action) != 11 ||
                    commands.Single(command => command.Id == TabContextMenu.ShiftLeft).Enabled ||
                    !commands.Single(command => command.Id == TabContextMenu.ShiftRight).Enabled ||
                    commands.Single(command => command.Id == TabContextMenu.CloseLeft).Enabled)
                    throw new InvalidOperationException("Tab menus must expose every command and disable unavailable directions.");
                pane.TabMenu.Invoke(TabContextMenu.ShiftRight);
                if (pane.Model.Tabs[1].Id != first || pane.Model.Active.Id != second)
                    throw new InvalidOperationException("Reordering must preserve the active tab and stable identities.");
                pane.TabMenu.GetCommands(first);
                pane.TabMenu.Invoke(TabContextMenu.ShiftLeft);
                pane.TabMenu.GetCommands(first);
                pane.TabMenu.Invoke(TabContextMenu.NewWindow);
                if (app.NewWindowPath != fixture)
                    throw new InvalidOperationException("New windows must use the right-clicked tab's folder.");
                var created = app.ExplorerWindows.Last();
                if (ReferenceEquals(created, app) || !ReferenceEquals(created.Application, app.Application))
                    throw new InvalidOperationException("New windows must use the existing Application.");
                created.Window.Close();
                pane.Model.Tabs[0].Filter = "original";
                pane.TabMenu.GetCommands(first);
                pane.TabMenu.Invoke(TabContextMenu.Duplicate);
            });
            await Ready(pane);
            await Check(() => pane.Model.Tabs.Count == 3 && pane.Model.Active.Id != first
                && pane.Model.Tabs[1] == pane.Model.Active && pane.Model.Active.Filter == "original"
                && pane.Model.Active.Path == fixture, "Duplicate retains the target folder and filter beside its source");
            await Ui(() =>
            {
                pane.TabMenu.GetCommands(first);
                pane.TabMenu.Invoke(TabContextMenu.CloseOthers);
            });
            await Ready(pane);
            await Check(() => pane.Model.Tabs.Count == 1 && pane.Model.Active.Id == first,
                "Close others preserves only the right-clicked tab");
            await Ui(() =>
            {
                pane.NewTab(fixture);
                pane.NewTab(fixture);
            });
            await Ready(pane);
            await Ui(() =>
            {
                ulong middle = pane.Model.Tabs[1].Id;
                pane.TabMenu.GetCommands(middle);
                pane.TabMenu.Invoke(TabContextMenu.CloseLeft);
                pane.TabMenu.GetCommands(middle);
                pane.TabMenu.Invoke(TabContextMenu.CloseRight);
                if (pane.Model.Tabs.Count != 1 || pane.Model.Active.Id != middle)
                    throw new InvalidOperationException("Directional closure must preserve the target tab.");
            });
            await Ready(pane);
            await Ui(() =>
            {
                pane.TabMenu.GetCommands(pane.Model.Active.Id);
                pane.NewTab(fixture);
                int count = pane.Model.Tabs.Count;
                pane.TabMenu.Invoke(TabContextMenu.CloseAll);
                if (pane.Model.Tabs.Count != count || app.CloseRequested ||
                    !app.Notification.Text.Contains("no longer available", StringComparison.Ordinal))
                    throw new InvalidOperationException("Changed tab order must invalidate a pending menu.");
            });
            await Ready(pane);
            await Ui(() =>
            {
                pane.Focus();
                ulong selected = pane.Model.Active.Id;
                Shortcut(0x21, KeyModifiers.Control | KeyModifiers.Shift);
                if (pane.Model.Tabs[0].Id != selected)
                    throw new InvalidOperationException("Ctrl+Shift+PageUp must move the active tab.");
                Shortcut(0x22, KeyModifiers.Control | KeyModifiers.Shift);
                Shortcut(0x73, KeyModifiers.Control);
                if (pane.Model.Tabs.Any(tab => tab.Id == selected))
                    throw new InvalidOperationException("Ctrl+F4 must close the active tab.");
            });
            await Ready(pane);
            await Ui(() =>
            {
                if (!app.SecondPaneVisible) app.ToggleSplit();
                app.ClosePane(app.Right);
                pane.SetFilter("");
            });
            await Ready(pane);
            await Ui(() =>
            {
                pane.TabMenu.GetCommands(pane.Model.Active.Id);
                pane.TabMenu.Invoke(TabContextMenu.NewPane);
            });
            await Ready(app.Right);
            await Check(() => app.SecondPaneVisible && app.Right.Model.Tabs.Count == 1
                && app.Right.Model.Active.Path == pane.Model.Active.Path,
                "Duplicate in a new pane starts with the requested tab");
            await Ui(() =>
            {
                app.Left.TabMenu.GetCommands(app.Left.Model.Active.Id);
                app.Left.TabMenu.Invoke(TabContextMenu.NewPane);
            });
            await Ready(app.Right);
            await Check(() => app.Right.Model.Tabs.Count == 2,
                "Duplication into an existing pane adds a tab without replacing its tabs");
            await Ui(() =>
            {
                app.Right.Model.Active.Filter = "retained";
                app.ClosePane(app.Left);
            });
            await Ready(app.Left);
            await Check(() => !app.SecondPaneVisible && app.Left.Model.Tabs.Count == 2
                && app.Left.Model.Active.Filter == "retained" && !app.CloseRequested,
                "Closing the left pane preserves the other pane's tabs and active state");
            await Ui(() =>
            {
                app.Left.TabMenu.GetCommands(app.Left.Model.Active.Id);
                app.Left.TabMenu.Invoke(TabContextMenu.CloseAll);
                if (!app.CloseRequested)
                    throw new InvalidOperationException("Closing the last pane must request window closure.");
            });
        }

        async Task TabAnimationChecks()
        {
            var pane = app.Left;
            uint duration = 0;
            ulong selected = 0, inserted = 0;
            float start = 0;
            bool motion = false;
            var positions = new List<float>();
            nint owner = 0;
            await Ui(() =>
            {
                pane.Focus();
                owner = GetAncestor(GetFocus(), 2);
                if (!SystemParametersInfoW(0x1042, 0, out int enabled, 0))
                    throw new InvalidOperationException("Read system tab animation policy.");
                motion = enabled != 0;
                duration = pane.Tabs.Duration;
                pane.Tabs.Duration = 1200;
                selected = pane.Model.Active.Id;
                start = pane.Tabs.NewTabButton.GetBounds().X;
                pane.NewTab(pane.Model.Active.Path);
                inserted = pane.Model.Active.Id;
                SendMessageW(owner, 0x800C, 0, 0);
                if (inserted == selected || !pane.FilesFocused
                    || (SendMessageW(owner, 0x803C, 33, 0) != 0) != motion)
                    throw new InvalidOperationException("Animated insertion must select the new tab, focus its files, and honor the independently read motion policy.");
            });
            await Until(() =>
            {
                SendMessageW(owner, 0x800C, 0, 0);
                positions.Add(pane.Tabs.NewTabButton.GetBounds().X);
                return SendMessageW(owner, 0x803C, 33, 0) == 0;
            });
            await Ui(() =>
            {
                float end = pane.Tabs.NewTabButton.GetBounds().X;
                if (motion && (!positions.Any(x => x > start + 0.1f && x < end - 0.1f)
                    || positions.Any(x => x < start - 0.1f || x > end + 0.1f)))
                    throw new InvalidOperationException("Tab insertion must move its New tab button through bounded intermediate positions.");
                start = end;
                positions.Clear();
                pane.CloseTab(inserted);
                pane.SelectTab(selected);
                SendMessageW(owner, 0x800C, 0, 0);
                if (pane.Model.Tabs.Any(tab => tab.Id == inserted) || !pane.FilesFocused
                    || (SendMessageW(owner, 0x803C, 33, 0) != 0) != motion)
                    throw new InvalidOperationException("Animated removal must retire the tab, restore file focus, and honor the independently read motion policy.");
            });
            await Until(() =>
            {
                SendMessageW(owner, 0x800C, 0, 0);
                positions.Add(pane.Tabs.NewTabButton.GetBounds().X);
                return SendMessageW(owner, 0x803C, 33, 0) == 0;
            });
            await Ui(() =>
            {
                float end = pane.Tabs.NewTabButton.GetBounds().X;
                if (motion && (!positions.Any(x => x < start - 0.1f && x > end + 0.1f)
                    || positions.Any(x => x > start + 0.1f || x < end - 0.1f)))
                    throw new InvalidOperationException("Tab removal must move its New tab button through bounded intermediate positions.");
                pane.Tabs.Duration = duration;
            });
            await Ready(pane);
        }

        async Task TabOverflowChecks()
        {
            var pane = app.Left;
            uint duration = 0;
            ulong selected = 0, first = 0, last = 0;
            var added = new List<ulong>();
            nint owner = 0, strip = 0, button = 0;
            ElementBounds buttonBounds = default;
            bool motion = false;
            await Ui(() =>
            {
                duration = pane.Tabs.Duration;
                selected = pane.Model.Active.Id;
                first = pane.Model.Tabs[0].Id;
                pane.Tabs.Duration = 0;
                pane.Focus();
                while (pane.Model.Tabs.Count < 12)
                {
                    pane.NewTab(pane.Model.Active.Path);
                    added.Add(pane.Model.Active.Id);
                }
                last = pane.Model.Tabs[^1].Id;
                pane.SelectTab(last);
            });
            await Ready(pane);
            await Ui(() =>
            {
                pane.Tabs.Focus();
                strip = GetFocus();
                owner = GetAncestor(strip, 2);
                button = FindWindowExW(strip, 0, null, "New tab");
                pane.Focus();
                if (strip == 0 || button == 0 || !SystemParametersInfoW(0x1042, 0, out int enabled, 0))
                    throw new InvalidOperationException("Overflow acceptance requires its native strip, New button, and readable system motion policy.");
                motion = enabled != 0;
                buttonBounds = pane.Tabs.NewTabButton.GetBounds();
                pane.Tabs.Duration = 1200;
                pane.SelectTab(first);
                SendMessageW(owner, 0x800C, 0, 0);
                if (pane.Model.Active.Id != first || !pane.FilesFocused
                    || (SendMessageW(owner, 0x803C, 33, 0) != 0) != motion)
                    throw new InvalidOperationException(
                        $"Explorer selection must publish immediately and use the native overflow clock without losing file focus: " +
                        $"selected={pane.Model.Active.Id}, expected={first}, filesFocused={pane.FilesFocused}, " +
                        $"clock={SendMessageW(owner, 0x803C, 33, 0)}, systemMotion={motion}, duration={pane.Tabs.Duration}, " +
                        $"tabs={pane.Model.Tabs.Count}, strip={pane.Tabs.GetBounds()}, button={pane.Tabs.NewTabButton.GetBounds()}.");

                // The first pixels still belong to the prior viewport before the first timer frame.
                SendMessageW(strip, 0x0201, 1, (20 << 16) | 12);
                SendMessageW(strip, 0x0202, 0, (20 << 16) | 12);
                if (!pane.FilesFocused || (motion && pane.Model.Active.Id == first))
                    throw new InvalidOperationException("Native overflow clicks must target the displayed tab rather than the logical destination viewport.");
                if (pane.Tabs.NewTabButton.GetBounds() != buttonBounds)
                    throw new InvalidOperationException("Overflow retargeting must keep the New button stationary.");
            });
            await Ui(() =>
            {
                int count = pane.Model.Tabs.Count;
                SendMessageW(button, 0x0201, 1, (16 << 16) | 16);
                SendMessageW(button, 0x0202, 0, (16 << 16) | 16);
                if (pane.Model.Tabs.Count != count + 1 || !pane.FilesFocused)
                    throw new InvalidOperationException("The native New tab button must remain usable during overflow motion.");
                added.Add(pane.Model.Active.Id);
                pane.CloseTab(pane.Model.Active.Id);
                pane.SelectTab(last);
                pane.SelectTab(first);
                SendMessageW(owner, 0x800C, 0, 0);
                if (pane.Model.Active.Id != first || !pane.FilesFocused)
                    throw new InvalidOperationException("Rapid Explorer overflow reversal must preserve the last logical selection and file focus.");
            });
            await Until(() =>
            {
                if (pane.Tabs.NewTabButton.GetBounds() != buttonBounds
                    || FindWindowExW(strip, 0, null, "New tab") != button)
                    throw new InvalidOperationException("Overflow frames must retain the native New button and its position.");
                return SendMessageW(owner, 0x803C, 33, 0) == 0;
            });
            await Ready(pane);
            await Ui(() =>
            {
                pane.Tabs.Duration = 0;
                pane.SelectTab(last);
                pane.SelectTab(first);
                SendMessageW(owner, 0x800C, 0, 0);
                if (pane.Model.Active.Id != first || SendMessageW(owner, 0x803C, 33, 0) != 0)
                    throw new InvalidOperationException("Immediate Explorer overflow must not retain the animation clock.");
                pane.CloseTabs(added);
                pane.SelectTab(selected);
                pane.Tabs.Duration = duration;
            });
            await Ready(pane);
        }

        async Task NewTabButtonChecks(FilePaneView pane)
        {
            int count = 0;
            ulong selected = 0;
            string path = "";
            await Ui(() =>
            {
                count = pane.Model.Tabs.Count;
                selected = pane.Model.Active.Id;
                path = pane.Model.Active.Path;
                if (!pane.Tabs.NewTabButtonVisible)
                    throw new InvalidOperationException("Each pane must enable its tab-strip New tab button.");
                pane.Tabs.Focus();
                nint strip = GetFocus();
                nint button = FindWindowExW(strip, 0, null, "New tab");
                if (strip == 0 || !pane.Tabs.Focused || button == 0)
                    throw new InvalidOperationException("New tab must be a native child of its pane's tab strip.");
                var other = ReferenceEquals(pane, app.Left) ? app.Right : app.Left;
                if (other.Root.GetBounds().Width > 0) other.Focus();
                else pane.Address.Focus();
                SendMessageW(button, 0x0201, 1, (16 << 16) | 16);
                SendMessageW(button, 0x0202, 0, (16 << 16) | 16);
            });
            await Ready(pane);
            await Ui(() =>
            {
                if (pane.Model.Tabs.Count != count + 1 || pane.Model.Active.Id == selected
                    || pane.Model.Active.Path != path || !pane.FilesFocused || !ReferenceEquals(app.Active, pane))
                    throw new InvalidOperationException(
                        $"The native New tab button must create a tab and restore file focus: " +
                        $"count={pane.Model.Tabs.Count}, expected={count + 1}, selected={pane.Model.Active.Id}, previous={selected}, " +
                        $"path={pane.Model.Active.Path}, expectedPath={path}, filesFocused={pane.FilesFocused}, activePane={ReferenceEquals(app.Active, pane)}.");
            });
            await Ui(() =>
            {
                pane.CloseTab(pane.Model.Active.Id);
                pane.SelectTab(selected);
            });
            await Ready(pane);
        }

        async Task FeedbackChecks()
        {
            string? count = null;
            ElementBounds footer = default;
            var elapsed = Stopwatch.StartNew();
            await Ui(() =>
            {
                count = app.Left.Status.Text;
                footer = app.Left.Footer.GetBounds();
                app.Transfers.ReportCopied(app.Left, 1, cut: false);
                app.Transfers.ReportCopied(app.Right, 2, cut: true);
            });
            await Check(() => app.Left.Feedback.Text == "1 item copied."
                && app.Right.Feedback.Text == "2 items cut. Paste to move."
                && app.Left.Status.Text == count && app.Left.Footer.GetBounds() == footer
                && app.Left.Feedback.GetBounds().X + app.Left.Feedback.GetBounds().Width <= app.Left.Status.GetBounds().X
                && app.Notification.GetBounds().Height == 0,
                "Copy feedback uses the originating pane footer without adding a persistent bar or changing the item count");
            await Task.Delay(2000);
            await Ui(() => app.Transfers.ReportCopied(app.Left, 2, cut: false));
            var replacement = Stopwatch.StartNew();
            await Ui(() => app.Report("Persistent error feedback."));
            await Until(() => app.Right.Feedback.Text.Length == 0);
            if (elapsed.Elapsed < TimeSpan.FromSeconds(2.9) || elapsed.Elapsed > TimeSpan.FromSeconds(6))
                throw new InvalidOperationException("Pane feedback must expire after approximately three seconds.");
            await Check(() => app.Left.Feedback.Text == "2 items copied.",
                "An older timeout cannot clear newer feedback or another pane's message");
            await Until(() => app.Left.Feedback.Text.Length == 0);
            if (replacement.Elapsed < TimeSpan.FromSeconds(2.9) || replacement.Elapsed > TimeSpan.FromSeconds(6))
                throw new InvalidOperationException("Replacement feedback must have its own three-second lifetime.");
            await Check(() => app.Left.Status.Text == count
                && app.Notification.Text == "Persistent error feedback.",
                "Transient feedback expires without changing the item count or clearing persistent errors");
        }

        async Task Transfers(string fixture)
        {
            string source = Path.Combine(fixture, "transfer-source");
            string destination = Path.Combine(fixture, "transfer-destination");
            string nested = Path.Combine(source, "folder with spaces");
            string text = Path.Combine(source, "file.txt");
            string folderTarget = Path.Combine(destination, "target folder");
            Directory.CreateDirectory(nested);
            Directory.CreateDirectory(folderTarget);
            await File.WriteAllTextAsync(Path.Combine(nested, "child.txt"), "nested content");
            await File.WriteAllTextAsync(text, "file content");
            await File.WriteAllTextAsync(Path.Combine(destination, "not a folder.txt"), "keep");
            await Ui(() =>
            {
                app.Left.SetViewMode(Models.ExplorerViewMode.Details);
                app.Right.SetViewMode(Models.ExplorerViewMode.Details);
                app.Left.Navigate(source);
                app.Right.Navigate(destination);
            });
            await Ready(app.Left);
            await Ready(app.Right);
            await Ui(() =>
            {
                app.Left.Focus();
                app.Left.Grid.Navigate(GridNavigation.First);
                app.Left.Grid.Navigate(GridNavigation.Next, KeyModifiers.Shift);
                if (app.Left.SelectedEntries.Length != 2)
                    throw new InvalidOperationException("File commands must include the full multi-selection.");
                var menu = app.Left.ContextMenu.GetCommands();
                if (menu.Any(c => c.Id is FileContextMenu.Open or FileContextMenu.NewTab)
                    || !new[] { FileContextMenu.Copy, FileContextMenu.Cut, FileContextMenu.CopyPaths }.All(id => menu.Any(c => c.Id == id))
                    || !app.Left.ContextMenu.GetShellPaths().Order().SequenceEqual(new[] { nested, text }.Order()))
                    throw new InvalidOperationException("Multi-selection menus must retain every source and omit single-folder commands.");
                app.Right.SelectPath(Path.Combine(destination, "not a folder.txt"));
                if (app.Transfers.QueryDrop(app.Right, app.Right.Grid.Selection.Focused, FileTransferEffect.Copy) != FileTransferEffect.None)
                    throw new InvalidOperationException("File rows must reject file drops.");
                app.Right.SelectPath(folderTarget);
                if (app.Transfers.Drop(app.Right, app.Right.Grid.Selection.Focused, [nested, text], FileTransferEffect.Copy)
                    != FileTransferEffect.Copy)
                    throw new InvalidOperationException("A folder-row drop must complete a Shell copy.");
            });
            await Ready(app.Left);
            await Ready(app.Right);
            await Check(() => File.ReadAllText(Path.Combine(folderTarget, "folder with spaces", "child.txt")) == "nested content"
                && File.ReadAllText(Path.Combine(folderTarget, "file.txt")) == "file content"
                && File.Exists(text) && Directory.Exists(nested), "Folder-row copy retains sources and copies nested content");
            await Ui(() =>
            {
                if (app.Transfers.Drop(app.Right, null, [text], FileTransferEffect.Move) != FileTransferEffect.Move)
                    throw new InvalidOperationException("An empty-area drop must move into the pane folder.");
            });
            await Ready(app.Left);
            await Ready(app.Right);
            await Check(() => !File.Exists(text) && File.ReadAllText(Path.Combine(destination, "file.txt")) == "file content"
                && app.Left.VisibleCount == 1 && app.Right.VisibleCount == 3, "Move refreshes both source and destination panes");
            await Ui(() =>
            {
                app.Left.ShowFind();
                foreach (var key in new[] { new UiKeyEvent(0x43, KeyModifiers.Control, 0),
                    new UiKeyEvent(0x58, KeyModifiers.Control, 0), new UiKeyEvent(0x56, KeyModifiers.Control, 0),
                    new UiKeyEvent(0x43, KeyModifiers.Control | KeyModifiers.Shift, 0) })
                    if (app.Window.KeyHandler?.Invoke(key) == true)
                        throw new InvalidOperationException("File clipboard shortcuts must not intercept native text editing.");
                app.Left.HideFind();
                app.Right.Navigate(folderTarget);
                if (app.Transfers.QueryDrop(app.Right, null, FileTransferEffect.Copy) != FileTransferEffect.None)
                    throw new InvalidOperationException("A pane with obsolete rows must reject drops.");
            });
            await Ready(app.Left);
            await Ready(app.Right);
        }

        async Task NavigationMenuChecks()
        {
            string path = app.Left.Model.Active.Path;
            string alpha = Path.Combine(path, "alpha");
            nint peer = 0;
            int requests = 0;
            ulong target = 0;
            await Ui(() =>
            {
                app.Sidebar.View.OnContextMenu(id =>
                {
                    requests++;
                    var commands = app.Sidebar.GetContextCommands(id);
                    var paths = app.Sidebar.GetContextShellPaths();
                    if (paths.SequenceEqual([alpha]))
                    {
                        target = id;
                        if (!commands.Any(c => c.Id == FileContextMenu.NewTab) ||
                            !commands.Any(c => c.Id == FileContextMenu.Copy) ||
                            !commands.Any(c => c.Id == FileContextMenu.Paste))
                            throw new InvalidOperationException("Navigation folders must share the Details menu commands.");
                    }
                    return [];
                }, _ => throw new InvalidOperationException("The menu probe must not execute a command."));
                app.Sidebar.FocusFilter();
                SendMessageTextW(GetFocus(), 0xC2, 1, "alpha");
            });
            await Ui(() =>
            {
                // Retained list names precede the declarative name assignment to their owner.
                peer = FindWindowExW(GetAncestor(GetFocus(), 1), 0, "Xui.Control.1", " items");
                if (peer == 0) throw new InvalidOperationException("Navigation menu peer is unavailable.");
                var bounds = app.Sidebar.View.Items.GetBounds();
                for (int y = 14; y < bounds.Height && target == 0; y += 28)
                {
                    var point = new NativePoint { X = 30, Y = (int)(y * GetDpiForWindow(peer) / 96) };
                    if (!ClientToScreen(peer, ref point)) throw new InvalidOperationException("Cannot locate the navigation row.");
                    SendMessageW(peer, 0x7B, (nuint)peer, (point.Y << 16) | (point.X & 0xffff));
                }
                if (target == 0 || app.Left.Model.Active.Path != path || app.Left.IsLoading)
                    throw new InvalidOperationException("Right-click must target a navigation folder without opening it.");
                int previous = requests;
                SendMessageW(peer, 0x7B, (nuint)peer, -1);
                if (requests != previous + 1 || !app.Sidebar.GetContextShellPaths().SequenceEqual([alpha]))
                    throw new InvalidOperationException("Keyboard menus must use the focused navigation row.");
                app.Left.SelectPath(Path.Combine(path, "beta"));
                app.Sidebar.InvokeContextCommand(FileContextMenu.Bookmark);
                if (!app.State.Bookmarks.Contains(alpha))
                    throw new InvalidOperationException("Navigation commands must retain the clicked path, not the Details selection.");
                app.ToggleBookmark(alpha);
                app.Sidebar.GetContextCommands(target);
                app.Sidebar.InvokeContextCommand(FileContextMenu.NewTab);
                app.Sidebar.BindContextMenu();
            });
            await Ready(app.Left);
            await Check(() => app.Left.Model.Active.Path == alpha, "Navigation menu opens its folder in a new tab");
            await Ui(() =>
            {
                app.Left.CloseTab();
                app.Sidebar.FocusFilter();
                SendMessageTextW(GetFocus(), 0xC2, 1, "");
                app.Left.Focus();
            });
            await Ready(app.Left);
        }

        async Task CaptionCheck(FilePaneView pane)
        {
            nint hwnd = 0;
            await Ui(() =>
            {
                pane.Focus();
                hwnd = GetAncestor(GetFocus(), 2);
                string path = pane.Model.Active.Path;
                string name = Path.GetFileName(Path.TrimEndingDirectorySeparator(path));
                var title = new System.Text.StringBuilder(32768);
                if (GetWindowTextW(hwnd, title, title.Capacity) == 0 ||
                    title.ToString() != $"{(name.Length == 0 ? path : name)} ({path}) - FileExplorer.xui")
                    throw new InvalidOperationException("The HWND caption must identify the active folder and full path.");
            });
            await Until(() => SendMessageW(hwnd, 0x7f, 0, 0) != 0 && SendMessageW(hwnd, 0x7f, 1, 0) != 0);
        }

        async Task NavigationAnimationChecks()
        {
            var reveal = app.Sidebar.Presentation;
            nint owner = 0, search = 0;
            float width = 0;
            await Ui(() =>
            {
                app.Sidebar.FocusFilter();
                owner = GetAncestor(GetFocus(), 2);
                search = GetFocus();
                width = app.Sidebar.View.GetBounds().Width;
                reveal.Duration = 800;
                app.Sidebar.Toggle();
                if (!app.Left.FilesFocused || reveal.Open)
                    throw new InvalidOperationException("Closing navigation returns focus immediately.");
            });
            bool motion = false;
            await Until(() =>
            {
                SendMessageW(owner, 0x800C, 0, 0);
                var host = reveal.GetBounds();
                if (Math.Abs(host.Width - width * reveal.Progress) > 0.1 ||
                    Math.Abs(app.Left.Root.GetBounds().X - host.Width) > 0.1 ||
                    Math.Abs(app.Window.TitlebarTabs.GetBounds().X - Math.Max(44, host.Width)) > 0.1 ||
                    app.Sidebar.View.GetBounds().Width != width)
                    throw new InvalidOperationException("Navigation, files, and title tabs must follow the same expanding clip.");
                motion |= reveal.Progress > 0 && reveal.Progress < 1;
                return !reveal.Animating;
            });
            await Ui(() =>
            {
                app.Sidebar.FocusFilter();
                if (!app.Sidebar.IsOpen || GetFocus() != search)
                    throw new InvalidOperationException("Navigation reuses its native search editor on immediate reopen.");
                if (motion && reveal.Progress != 0)
                    throw new InvalidOperationException("Navigation opening starts from the collapsed layout.");
            });
            await Until(() => !reveal.Animating && reveal.Progress == 1);
            await Ui(() => { reveal.Duration = 180; app.Left.Focus(); });
        }

        async Task NavigationGroupChecks()
        {
            var view = app.Sidebar.View;
            uint duration = 0;
            nint search = 0, items = 0, owner = 0;
            bool motion = false;
            await Ui(() =>
            {
                duration = view.Duration;
                view.Duration = 1200;
                view.Search.Focus();
                search = GetFocus();
                owner = GetAncestor(search, 2);
                if (!SystemParametersInfoW(0x1042, 0, out int enabled, 0))
                    throw new InvalidOperationException("Read navigation group motion policy.");
                motion = enabled != 0;
                if (!PostMessageW(search, 0x100, 0x09, 0) || !PostMessageW(search, 0x101, 0x09, 0))
                    throw new InvalidOperationException("Send native Tab from navigation search to its items.");
            });
            await Until(() => !view.Search.Focused);
            await Ui(() =>
            {
                items = GetFocus();
                if (items == 0 || items == search || GetAncestor(items, 2) != owner)
                    throw new InvalidOperationException("Navigation group acceptance must focus the owned item peer.");
                SendMessageW(items, 0x100, 0x24, 0);
                SendMessageW(owner, 0x800C, 0, 0);
                SendMessageW(items, 0x100, 0x25, 0);
                SendMessageW(owner, 0x800C, 0, 0);
                if (view.Animating != motion)
                    throw new InvalidOperationException(
                        $"Explorer's real first navigation group must animate native collapse: active={view.Animating}, systemMotion={motion}.");
            });
            await Until(() => !view.Animating);
            await Ui(() =>
            {
                SendMessageW(items, 0x100, 0x27, 0);
                SendMessageW(owner, 0x800C, 0, 0);
                if (view.Animating != motion)
                    throw new InvalidOperationException("Native group expansion must use the configured Explorer motion policy.");
                SendMessageW(items, 0x100, 0x25, 0);
                SendMessageW(items, 0x100, 0x27, 0);
                SendMessageW(owner, 0x800C, 0, 0);
            });
            await Until(() => !view.Animating);
            await Ui(() =>
            {
                view.Duration = 0;
                SendMessageW(items, 0x100, 0x25, 0);
                SendMessageW(items, 0x100, 0x27, 0);
                SendMessageW(owner, 0x800C, 0, 0);
                if (view.Animating)
                    throw new InvalidOperationException("Immediate navigation group changes must not retain motion.");
                view.Search.Focus();
                if (GetFocus() != search)
                    throw new InvalidOperationException("Navigation group motion must retain Explorer's native search editor.");
                view.Duration = duration;
                app.Left.Focus();
            });
        }

        async Task PaneAnimationChecks()
        {
            var split = app.Panes;
            var entryElapsed = new Stopwatch();
            long inputReadyMs = 0, firstSampleMs = -1, firstUpdateMs = -1;
            long inputObservedMs = 0, motionCapturedMs = 0, motionObservedMs = 0;
            var samples = new List<string>();
            long startingTicks = 0, startingPanePaints = 0;
            nint owner = 0, input = 0;
            float fullLeft = 0, fullRight = 0, extent = 0;
            bool motion = false;
            await Ui(() =>
            {
                if (!SystemParametersInfoW(0x1042, 0, out int enabled, 0))
                    throw new InvalidOperationException("Could not read pane motion policy.");
                motion = enabled != 0;
                split.TransitionDuration = 0;
                if (!app.SecondPaneVisible) app.ToggleSplit();
                app.Right.Focus();
                owner = GetAncestor(GetFocus(), 2);
                fullLeft = app.Left.Root.GetBounds().Width;
                fullRight = app.Right.Root.GetBounds().Width;
                extent = split.GetBounds().Width;
            });
            await Ready(app.Right);
            await Ui(() =>
            {
                app.ToggleSplit();
                split.TransitionDuration = 800;
                startingTicks = (long)SendMessageW(owner, 0x803C, 34, 0);
                startingPanePaints = (long)SendMessageW(owner, 0x803C, 35, 0);
                entryElapsed.Restart();
                app.ToggleSplit();
                app.Right.ShowFind();
                input = GetFocus();
                SendMessageW(input, 0x102, 'p', 1);
                if (!app.Right.FindInput.Focused || app.Right.FindInput.Text != "p")
                    throw new InvalidOperationException("The incoming pane accepts native input immediately.");
                inputReadyMs = entryElapsed.ElapsedMilliseconds;
            });
            inputObservedMs = entryElapsed.ElapsedMilliseconds;
            int intermediate = 0;
            await Ui(() =>
            {
                motionCapturedMs = entryElapsed.ElapsedMilliseconds;
            });
            motionObservedMs = entryElapsed.ElapsedMilliseconds;
            void Geometry()
            {
                SendMessageW(owner, 0x800C, 0, 0);
                var first = app.Left.Root.GetBounds();
                var second = app.Right.Root.GetBounds();
                var area = split.GetBounds();
                if (Math.Abs(first.Width - (extent + (fullLeft - extent) * split.Progress)) > 0.1 ||
                    Math.Abs(second.X - (area.X + extent - fullRight * split.Progress)) > 0.1 ||
                    second.Width != fullRight)
                    throw new InvalidOperationException("Pane entry must retain the secondary width and coordinate the primary edge.");
            }
            await Until(() =>
            {
                bool firstSample = firstSampleMs < 0;
                if (firstSample) firstSampleMs = entryElapsed.ElapsedMilliseconds;
                Geometry();
                if (firstSample) firstUpdateMs = entryElapsed.ElapsedMilliseconds;
                samples.Add($"{entryElapsed.ElapsedMilliseconds}ms:p={split.Progress:F4}," +
                    $"ticks={(long)SendMessageW(owner, 0x803C, 34, 0) - startingTicks}");
                if (split.Progress > 0 && split.Progress < 1) intermediate++;
                return !split.Animating;
            });
            await Ui(() =>
            {
                long paintedFrames = (long)SendMessageW(owner, 0x803C, 35, 0) - startingPanePaints;
                if ((motion && paintedFrames == 0) || GetFocus() != input || app.Right.FindInput.Text != "p")
                    throw new InvalidOperationException(
                        $"Pane motion must preserve input identity and show intermediate layout: " +
                        $"motion={motion}, frames={intermediate}, paintedFrames={paintedFrames}, focus={GetFocus()}, editor={input}, " +
                        $"text='{app.Right.FindInput.Text}', progress={split.Progress}, elapsed={entryElapsed.ElapsedMilliseconds}ms, " +
                        $"inputReady={inputReadyMs}ms, inputObserved={inputObservedMs}ms, motionCaptured={motionCapturedMs}ms, " +
                        $"motionObserved={motionObservedMs}ms, firstSample={firstSampleMs}ms, firstUpdate={firstUpdateMs}ms; " +
                        $"samples=[{string.Join("; ", samples)}].");
                Console.WriteLine($"Pane entry samples={intermediate}, paintedFrames={paintedFrames}, inputReady={inputReadyMs}ms, " +
                    $"inputObserved={inputObservedMs}ms, motionCaptured={motionCapturedMs}ms, motionObserved={motionObservedMs}ms, " +
                    $"firstSample={firstSampleMs}ms, firstUpdate={firstUpdateMs}ms, complete={entryElapsed.ElapsedMilliseconds}ms.");
                app.Right.HideFind();
                app.ClosePane(app.Right);
                if (app.SecondPaneVisible || !app.Left.FilesFocused)
                    throw new InvalidOperationException("Closing the pane changes logical visibility and returns focus immediately.");
            });
            await Until(() =>
            {
                SendMessageW(owner, 0x800C, 0, 0);
                if (split.Animating) Geometry();
                return !split.Animating;
            });
            await Ui(() =>
            {
                if (app.Right.Root.GetBounds().Width != 0 || app.Window.TitlebarSecondaryTabs.GetBounds().Width != 0)
                    throw new InvalidOperationException("Closed pane and secondary title tabs release their geometry.");
                split.TransitionDuration = 180;
            });
            await Ready(app.Left);
        }

        async Task RevealChecks(FilePaneView pane, bool animate = true)
        {
            await Ui(pane.HideFind);
            await Until(() => !pane.FindReveal.Animating && pane.FindBounds.Height == 0);
            float combinedHeight = 0;
            nint inputPeer = 0;
            nint owner = 0;
            bool motion = false;
            long startingRevealPaints = 0;
            long closeStarted = 0, closeCompleted = 0;
            void Geometry()
            {
                // A timer sample can precede its posted layout update in the UI queue.
                SendMessageW(owner, 0x800C, 0, 0);
                var files = pane.Grid.GetBounds();
                var reveal = pane.FindBounds;
                if (Math.Abs(files.Height + reveal.Height - combinedHeight) > 0.1f ||
                    Math.Abs(files.Y + files.Height - reveal.Y) > 0.1f ||
                    Math.Abs(reveal.Height - 56 * pane.FindReveal.Progress) > 0.1f)
                    throw new InvalidOperationException(
                        $"Find and files must share an edge and divide one constant extent throughout motion. " +
                        $"Files={files}, reveal={reveal}, combined={combinedHeight}, progress={pane.FindReveal.Progress}.");
                if (pane.FindContentBounds.Height != 56 || pane.FindInput.GetBounds().Height != 44)
                    throw new InvalidOperationException("Expanding Reveal must keep the native input at its full size.");
            }
            async Task Settled(bool open)
            {
                int intermediate = 0;
                await Until(() =>
                {
                    Geometry();
                    if (pane.FindReveal.Progress > 0 && pane.FindReveal.Progress < 1) intermediate++;
                    return !pane.FindReveal.Animating && pane.FindReveal.Progress == (open ? 1 : 0);
                });
                await Ui(() =>
                {
                    long paintedFrames = (long)SendMessageW(owner, 0x803C, 36, 0) - startingRevealPaints;
                    if (motion && paintedFrames == 0)
                        throw new InvalidOperationException(
                            $"Reveal must paint intermediate coordinated layout frames: samples={intermediate}, paintedFrames={paintedFrames}.");
                });
                await Ui(Geometry);
            }
            await Ui(() =>
            {
                if (pane.FindReveal.Layout != RevealLayout.Expand ||
                    pane.FindReveal.Direction != RevealDirection.Bottom || pane.FindReveal.Duration != 180)
                    throw new InvalidOperationException("Find must use the configured bottom expanding Reveal.");
                combinedHeight = pane.Grid.GetBounds().Height;
                owner = GetAncestor(GetFocus(), 2);
                startingRevealPaints = (long)SendMessageW(owner, 0x803C, 36, 0);
                pane.FindReveal.Duration = animate ? 1000u : 0u;
                pane.ShowFind();
                motion = pane.FindReveal.Animating;
                if (motion && (pane.FindBounds.Height != 0 || pane.Grid.GetBounds().Height != combinedHeight))
                    throw new InvalidOperationException("Opening Find must not resize the file list before its first motion frame.");
                if (!motion && pane.FindReveal.Progress != 1)
                    throw new InvalidOperationException("Disabled motion must open Find immediately.");
                if (!pane.FindInput.Focused || !pane.FindReveal.Open)
                    throw new InvalidOperationException("Reveal must focus the native input immediately when opening.");
                owner = GetAncestor(GetFocus(), 2);
                Geometry();
                inputPeer = GetFocus();
                SendMessageW(GetFocus(), 0x102, 's', 1);
                if (pane.FindInput.Text != "s")
                    throw new InvalidOperationException("Reveal must preserve native text during opening.");
            });
            await Settled(true);
            await Until(() => FindFits(pane));
            await Ui(() =>
            {
                closeStarted = Stopwatch.GetTimestamp();
                pane.HideFind();
                closeCompleted = Stopwatch.GetTimestamp();
                if (pane.FindReveal.Open || !pane.FilesFocused || pane.FindInput.Text != "")
                    throw new InvalidOperationException("Closing Find clears its query and returns focus immediately.");
                if (pane.FindBounds.Height != (motion ? 56 : 0))
                    throw new InvalidOperationException("Closing Find must not snap the layout at the start of motion.");
                Geometry();
            });
            if (motion)
                await Until(() =>
                {
                    Geometry();
                    if (!pane.FindReveal.Animating)
                        throw new InvalidOperationException("Closing Reveal completed without an observable reversal frame.");
                    return pane.FindReveal.Progress < 0.8f && pane.FindReveal.Progress > 0;
                });
            await Ui(() =>
            {
                long reverseStarted = Stopwatch.GetTimestamp();
                startingRevealPaints = (long)SendMessageW(owner, 0x803C, 36, 0);
                pane.ShowFind();
                long reverseCompleted = Stopwatch.GetTimestamp();
                // Reversal samples the current clock, not the previous rendered frame.
                double minimum = Math.Pow(Math.Clamp(1 - Stopwatch.GetElapsedTime(closeStarted, reverseCompleted).TotalMilliseconds / 1000, 0, 1), 3);
                double maximum = Math.Pow(Math.Clamp(1 - Stopwatch.GetElapsedTime(closeCompleted, reverseStarted).TotalMilliseconds / 1000, 0, 1), 3);
                float progress = pane.FindReveal.Progress;
                if (motion && (progress < minimum - 0.001 || progress > maximum + 0.001))
                    throw new InvalidOperationException($"Reversal must preserve the sampled closing position: {progress} outside [{minimum}, {maximum}].");
                if (!pane.FindReveal.Open || !pane.FindInput.Focused || GetFocus() != inputPeer)
                    throw new InvalidOperationException("Reversing Reveal must reuse its native editor.");
                Geometry();
                SendMessageW(GetFocus(), 0x102, 'r', 1);
                if (pane.FindInput.Text != "r")
                    throw new InvalidOperationException("The native editor must accept text immediately after reversal.");
            });
            await Settled(true);
            await Until(() => FindFits(pane));
            await Check(() => pane.FindInput.Text == "r" && pane.FindReveal.Progress == 1,
                "Reveal completes reversal without losing editor text");
            await Ui(pane.HideFind);
            await Settled(false);
            await Ready(pane);
            await Until(() => !pane.FindReveal.Animating && pane.FindBounds.Height == 0);
            await Ui(() => pane.FindReveal.Duration = 180);
        }

        async Task TypeToFindChecks(FilePaneView pane)
        {
            await Ui(() =>
            {
                pane.HideFind();
                pane.Focus();
                if (!PostMessageW(GetFocus(), 0x100, 0x53, 1))
                    throw new InvalidOperationException("Could not post the first typing key.");
            });
            await Until(() => pane.FindInput.Focused && pane.FilterQuery == "s" && !pane.IsFiltering);
            await Check(() => pane.Model.Active.FindOpen && pane.FindInput.Text == "s",
                "Typing in either file view opens Find without losing the first character");
            await Ui(() =>
            {
                if (!PostMessageW(GetFocus(), 0x100, 0x4d, 1))
                    throw new InvalidOperationException("Could not post the next typing key.");
            });
            await Until(() => pane.FilterQuery == "sm" && !pane.IsFiltering);
            await Ui(() =>
            {
                SendMessageW(GetFocus(), 0x102, 0x00e9, 1);
            });
            await Until(() => pane.FilterQuery == "sm\u00e9" && !pane.IsFiltering);
            await Ui(pane.HideFind);
            await Ready(pane);
            await Ui(() =>
            {
                pane.Focus();
                if (!PostMessageW(GetFocus(), 0x100, 0x25, 1))
                    throw new InvalidOperationException("Could not post a file-navigation key.");
            });
            await Task.Delay(50);
            await Check(() => !pane.Model.Active.FindOpen && pane.FilesFocused,
                "File-navigation keys do not start Find");
        }

        Task Ui(Action action)
        {
            var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!app.Application.Post(() =>
            {
                try { action(); completion.SetResult(); }
                catch (Exception error) { completion.SetException(error); }
            })) completion.SetException(new InvalidOperationException("The explorer window closed during smoke checks."));
            return completion.Task.WaitAsync(TimeSpan.FromSeconds(20));
        }

        void Shortcut(uint key, KeyModifiers modifiers = KeyModifiers.None)
        {
            if (app.Window.KeyHandler?.Invoke(new(key, modifiers, 0)) != true)
                throw new InvalidOperationException($"Shortcut {key:X}/{modifiers} was not handled.");
        }

        static void MouseTravel(Control target, NavigationDirection direction, Action? beforeClick = null)
        {
            target.Focus();
            nint peer = GetFocus();
            beforeClick?.Invoke();
            uint button = direction == NavigationDirection.Back ? 1u : 2u;
            nuint flags = (nuint)((button << 16) | (button == 1 ? 0x20u : 0x40u));
            if (SendMessageW(peer, 0x20B, flags, (12 << 16) | 12) != 1 ||
                SendMessageW(peer, 0x20C, (nuint)(button << 16), (12 << 16) | 12) != 1)
                throw new InvalidOperationException("Mouse navigation messages were not consumed.");
        }

        static bool FindFits(FilePaneView pane)
        {
            var bar = pane.FindBounds;
            var input = pane.FindInput.GetBounds();
            var close = pane.CloseFindButton.GetBounds();
            var files = pane.Grid.GetBounds();
            return !pane.FindReveal.Animating && pane.FindReveal.Progress == 1
                && bar.Height == 56 && input.Height == 44 && close.Height == 44
                && input.Width > 0 && input.X >= bar.X && input.Y >= bar.Y
                && close.X >= input.X + input.Width + 8 && close.Y == input.Y
                && close.X + close.Width <= bar.X + bar.Width
                && input.Y + input.Height <= bar.Y + bar.Height
                && close.Y + close.Height <= bar.Y + bar.Height
                && bar.Y >= files.Y + files.Height
                && pane.CloseFindButton.Icon == ButtonIcon.Close;
        }

        async Task Check(Func<bool> predicate, string name)
        {
            await Ui(() => { if (!predicate()) throw new InvalidOperationException(name); });
        }

        async Task Until(Func<bool> predicate)
        {
            var deadline = DateTime.UtcNow.AddSeconds(20);
            while (true)
            {
                bool ready = false;
                await Ui(() => ready = predicate());
                if (ready) return;
                if (DateTime.UtcNow > deadline) throw new TimeoutException("The explorer did not finish pending work.");
                await Task.Delay(20);
            }
        }

        Task Ready(FilePaneView pane) => Until(() => !pane.IsLoading && !pane.IsFiltering);
    }
}
