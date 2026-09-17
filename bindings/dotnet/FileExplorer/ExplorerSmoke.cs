using System.Runtime.InteropServices;
using System.Diagnostics;
using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal static class ExplorerSmoke
{
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern nint GetFocus();
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
    private static extern uint GetWindowThreadProcessId(nint window, out uint process);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern bool SetWindowPos(nint window, nint after, int x, int y, int width, int height, uint flags);
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

    public static Task Start(ExplorerApplication app)
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
                await Check(() => app.Window.Style == VisualStyle.WinUI, "Explorer uses the WinUI visual style");
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
                await CaptionCheck(app.Left);
                await NavigationMenuChecks();
                await TypeToFindChecks(app.Left);
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
                await Until(() => app.Sidebar.View.GetBounds().Width == 0);
                await Check(() => app.Left.Root.GetBounds().X == 0 && app.Window.TitlebarTabs.GetBounds().X == 44
                    && app.Window.TitlebarLeading.GetBounds().X == 0 && app.Sidebar.View.Search.GetBounds().Width == 0,
                    "Hidden navigation has no rail and leaves the hamburger before the first tab");
                await Ui(() => Shortcut(0x46, KeyModifiers.Alt));
                await Until(() => app.Sidebar.View.GetBounds().Width > 0 && app.Sidebar.View.Search.GetBounds().Width > 0);
                await Check(() => app.Sidebar.IsOpen && app.Window.TitlebarTabs.GetBounds().X == app.Left.Root.GetBounds().X,
                    "Alt+F restores navigation and aligned tabs");
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
                await Check(() => FindFits(app.Left) && app.Left.FindInput.Text == "small", "Restored Find has a complete input row");
                await Ui(app.Left.CloseFindButton.Invoke);
                await Ready(app.Left);
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

                await Ui(() => Shortcut(0x4c, KeyModifiers.Control));
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
                    if (!app.Palettes.Pending || app.Palettes.ResultCount != 4)
                        throw new InvalidOperationException("A new folder must keep the old rows until its scan completes.");
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
                await Check(() => app.Palettes.QueryText == fixture + Path.DirectorySeparatorChar, "Palette history works without a Back button");
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
                await Check(() => FindFits(app.Right) && app.Right.VisibleCount == 1
                    && app.Left.Model.Active.Filter == "" && app.Left.FindBounds.Height == 0,
                    "Second-pane Find fits its split without changing the first pane");
                await Ui(() => Shortcut(0x1b));
                await Ready(app.Right);
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
                string many = Path.Combine(fixture, "many");
                Directory.CreateDirectory(many);
                for (int i = 0; i < 80; i++) await File.WriteAllTextAsync(Path.Combine(many, $"item-{i:D3}.txt"), "row");
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
                await ColumnsChecks();
                await Transfers(fixture);
                await FeedbackChecks();
                await TabMenuChecks(fixture);
                await DetachedLifetimeChecks();
                Console.WriteLine("Explorer smoke passed: navigation, completion, panes, tabs, tab menus, filtering, sorting, columns, commands, detached previews, opener-first lifetime, native copy, image reuse, and file transfers.");
            }
            catch (Exception error)
            {
                if (!app.Application.Post(() => throw new InvalidOperationException("Explorer UI smoke failed.", error))) throw;
            }
            finally
            {
                if (Directory.Exists(fixture)) Directory.Delete(fixture, recursive: true);
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
                await Until(() => app.Preview.IsOpen && !app.Preview.Pending);
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
                    await Until(() => app.Preview.IsOpen && !app.Preview.Pending);
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
                await Check(() => app.Preview.Message.Contains("Cannot preview"), "Deleted files produce an explicit error");
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
                nint opener = 0, textHost = 0, imageHost = 0;
                await Ui(() =>
                {
                    app.Left.Focus();
                    opener = GetAncestor(GetFocus(), 2);
                    text = Show("survivor.txt");
                    image = Show("survivor.bmp");
                    twin = Show("survivor.bmp");
                    folder = Show("alpha");
                    text.Text.Focus();
                    textHost = GetAncestor(GetFocus(), 2);
                    image.CloseButton.Focus();
                    imageHost = GetAncestor(GetFocus(), 2);
                    uint thread = GetWindowThreadProcessId(opener, out uint process);
                    foreach (nint host in new[] { textHost, imageHost })
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
                        return app.Preview.Current!;
                    }
                });
                await Until(() => !text.Pending && !folder.Pending
                    && image.Image.Status == ImageStatus.Ready && twin.Image.Status == ImageStatus.Ready);
                await Ui(twin.Dismiss);
                await Until(() => twin.IsDisposed);
                await Check(() => image.Image.Status == ImageStatus.Ready && image.IsOpen,
                    "Retiring one image preview does not invalidate another window's cached pixels");
                await Ui(app.Window.Close);
                await Until(() => app.IsDisposed);
                await Check(() => !IsWindow(opener) && IsWindow(textHost) && IsWindow(imageHost)
                    && text.IsOpen && image.IsOpen && folder.IsOpen, "Previews survive native opener destruction and managed disposal");
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
                await Ui(() => pane.SelectColumnPath(0, alpha));
                await Ready(pane);
                await Check(() => pane.Columns.ColumnCount == 2 && pane.Model.Active.Path == alpha
                    && pane.Columns.ActiveColumn == 0, "Single selection drills while retaining its ancestor and focus");
                await Ui(() => pane.SelectColumnPath(1, Path.Combine(alpha, "child")));
                await Ready(pane);
                await Check(() => pane.Columns.ColumnCount == 3, "Nested selection appends a column");
                double columnWidth = 0, horizontalOffset = 0;
                await Ui(() =>
                {
                    columnWidth = pane.Columns.ColumnWidth;
                    pane.Columns.ColumnWidth = 2000;
                });
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
                await Ui(() => { pane.ShowFind(); pane.SetFilter("child"); });
                await Ready(pane);
                await Check(() => pane.VisibleCount == 1 && pane.Columns.ColumnCount == 2
                    && pane.Model.Active.Columns[0].Snapshot.Entries.Count == 5
                    && pane.Model.Active.Columns[1].SelectedPath is null,
                    "Find filters the rightmost folder without removing siblings or selecting its focus-only row");
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
                    if (!pane.FindInput.Focused || pane.FindInput.Text != "" || pane.Model.Active.Filter != ""
                        || pane.Model.Active.Path != Path.Combine(alpha, "child"))
                        throw new InvalidOperationException(
                            $"Find Down opens the folder and clears its filter without moving input focus: " +
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
                await Check(() => pane.IsColumns && pane.Columns.ColumnCount == 3 && pane.Model.Active.Filter == "child",
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

        async Task TypeToFindChecks(FilePaneView pane)
        {
            await Ui(() =>
            {
                pane.HideFind();
                pane.Focus();
                if (!PostMessageW(GetFocus(), 0x100, 0x53, 1))
                    throw new InvalidOperationException("Could not post the first typing key.");
            });
            await Until(() => pane.FindInput.Focused && pane.Model.Active.Filter == "s" && !pane.IsFiltering);
            await Check(() => pane.Model.Active.FindOpen && pane.FindInput.Text == "s",
                "Typing in either file view opens Find without losing the first character");
            await Ui(() =>
            {
                if (!PostMessageW(GetFocus(), 0x100, 0x4d, 1))
                    throw new InvalidOperationException("Could not post the next typing key.");
            });
            await Until(() => pane.Model.Active.Filter == "sm" && !pane.IsFiltering);
            await Ui(() =>
            {
                SendMessageW(GetFocus(), 0x102, 0x00e9, 1);
            });
            await Until(() => pane.Model.Active.Filter == "sm\u00e9" && !pane.IsFiltering);
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
            return bar.Height == 56 && input.Height == 44 && close.Height == 44
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
