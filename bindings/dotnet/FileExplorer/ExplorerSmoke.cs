namespace Xui.FileExplorer;

internal static class ExplorerSmoke
{
    public static Task Start(ExplorerApplication app)
    {
        return Run();
        async Task Run()
        {
            string fixture = Path.Combine(Path.GetTempPath(), $"xui-explorer-ui-{Guid.NewGuid():N}");
            try
            {
                Directory.CreateDirectory(Path.Combine(fixture, "alpha", "child"));
                Directory.CreateDirectory(Path.Combine(fixture, "beta"));
                await File.WriteAllTextAsync(Path.Combine(fixture, "small.txt"), "abc");
                await File.WriteAllTextAsync(Path.Combine(fixture, "large.txt"), new string('x', 4000));
                await Until(() => !app.Left.IsLoading && !app.Left.IsFiltering);
                await Ui(() => app.Left.Navigate(fixture));
                await Ready(app.Left);
                await Check(() => app.Left.VisibleCount == 4, "Folder rows");
                await Check(() => app.Left.Model.Active.Path == fixture, "Committed address");
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
                await Ui(app.Left.Focus);

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
                await Ui(() => Shortcut(0x09, KeyModifiers.Control | KeyModifiers.Shift));
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Filter == "small" && app.Left.VisibleCount == 1, "Restored tab filter");
                await Check(() => FindFits(app.Left) && app.Left.FindInput.Text == "small", "Restored Find has a complete input row");
                await Ui(app.Left.CloseFindButton.Invoke);
                await Ready(app.Left);
                await Check(() => !app.Left.Model.Active.FindOpen && app.Left.FindBounds.Height == 0
                    && app.Left.VisibleCount == 4 && app.Left.Grid.GetBounds().Height == unfilteredHeight
                    && app.Left.Grid.Focused, "X clears Find, restores row space, and returns focus to the files");

                await Ui(() => app.Left.Navigate(Path.Combine(fixture, "missing")));
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Path == fixture && app.Left.VisibleCount == 4 && app.Left.Error is not null,
                    "Failed navigation preserves committed view");

                await Ui(() => Shortcut(0x4c, KeyModifiers.Control));
                await Until(() => !app.Palettes.Pending);
                await Check(() => app.Palettes.IsOpen && app.Palettes.QueryText == fixture && app.Palettes.ResultCount == 4,
                    "Navigation palette opens at CWD");
                ElementBounds firstPaletteBounds = default;
                await Ui(() => firstPaletteBounds = app.Palettes.Bounds);
                await Check(() => Math.Abs(firstPaletteBounds.X + firstPaletteBounds.Width / 2
                    - (app.Left.Root.GetBounds().X + app.Left.Root.GetBounds().Width) / 2) < 1
                    && app.Palettes.StatusHeight == 0, "Palette centers in the window without a redundant result-count footer");
                await Ui(() =>
                {
                    foreach (var (query, count) in new[] { ("al", 2), ("alp", 1), ("AL", 2), ("nothing-matches", 0), ("", 4) })
                    {
                        app.Palettes.EditQuery(Path.Combine(fixture, query));
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
                await Check(() => app.Palettes.QueryText == fixture, "Palette history works without a Back button");
                await Ui(() => Shortcut(0x27, KeyModifiers.Alt));
                await Until(() => !app.Palettes.Pending);
                await Check(() => app.Palettes.QueryText == Path.Combine(fixture, "alpha") + Path.DirectorySeparatorChar,
                    "Palette history works without a Forward button");
                await Ui(() => Shortcut(0x0d));
                await Ready(app.Left);
                await Check(() => app.Left.Model.Active.Path == Path.Combine(fixture, "alpha", "child") && !app.Palettes.IsOpen,
                    "Enter navigates selected folder");

                await Ui(() =>
                {
                    app.Palettes.ShowNavigation(app.Left);
                    app.Palettes.EditQuery(Path.Combine(fixture, "alpha"));
                    app.Palettes.EditQuery(Path.Combine(fixture, "beta"));
                });
                await Until(() => !app.Palettes.Pending);
                await Check(() => app.Palettes.QueryText == Path.Combine(fixture, "beta") && app.Palettes.ResultCount == 0,
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
                    if (emptyMenu.Length != 1 || emptyMenu[0].Id != FileContextMenu.Refresh
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
                await Ui(() =>
                {
                    app.Report("Explorer smoke passed.");
                    Console.WriteLine("Explorer smoke passed: navigation, completion, panes, tabs, filtering, sorting, and commands.");
                    app.Window.Close();
                });
            }
            catch (Exception error)
            {
                if (!app.Window.Post(() => throw new InvalidOperationException("Explorer UI smoke failed.", error))) throw;
            }
            finally
            {
                if (Directory.Exists(fixture)) Directory.Delete(fixture, recursive: true);
            }
        }

        Task Ui(Action action)
        {
            var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!app.Window.Post(() =>
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
