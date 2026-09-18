using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class FilePaneView
{
    private readonly ExplorerApplication app;
    private readonly Window window;
    private Dictionary<string, ulong> identities = new(StringComparer.OrdinalIgnoreCase);
    private readonly Button back, forward, up;
    private readonly FilePaneLayout layout;
    private readonly ViewMenuLayout viewMenu;
    private readonly Stack findHost;
    private readonly Button closeFind;
    private readonly Label status;
    private readonly TextInput find;
    private CancellationTokenSource navigation = new();
    private CancellationTokenSource filtering = new();
    private CancellationTokenSource feedback = new();
    private ulong nextIdentity = 1;
    private ulong displayedTab;
    private FileRows rows;
    private bool rendering;
    private string? error;
    private readonly List<ColumnPresentation> columnViews = [];
    private bool focusColumnsAfterRender;
    private bool viewEntryPending;

    private sealed record ColumnPresentation(ExplorerColumn Model, string Query, int Sort, bool Descending,
        FileRows Rows, ImmutableSource Source);

    public FilePaneView(ExplorerApplication app, int number, string path, TabStrip tabs)
    {
        this.app = app;
        window = app.Window;
        Model = new(path);
        Tabs = tabs;
        Tabs.SetAutomationId($"pane-{number}-tabs");
        Tabs.NewTabButtonVisible = true;
        Tabs.Duration = 180;
        Tabs.NewTabButton.SetStyle(ExplorerStyles.IconButton);
        TabMenu = new(app, this);
        Tabs.OnContextMenu(TabMenu.GetCommands, TabMenu.Invoke);
        layout = new(window, number, path, attach: false);
        layout.ViewReveal.Duration = 180;
        foreach (var button in new[] { layout.Back, layout.Forward, layout.Up, layout.Refresh, layout.Commands })
            button.SetStyle(ExplorerStyles.IconButton);
        Root = layout.Root;
        BackButton = back = layout.Back;
        forward = layout.Forward;
        up = layout.Up;
        Address = layout.Address;
        WireButton(back, () => MoveHistory(-1));
        WireButton(forward, () => MoveHistory(1));
        WireButton(up, Up);
        WireButton(Address, () => app.Palettes.ShowNavigation(this));
        WireButton(layout.Refresh, Refresh);
        WireButton(layout.Commands, () => app.Palettes.ShowCommands());
        Grid = layout.Files;
        Columns = window.MillerColumns($"Columns in pane {number}");
        Columns.SetAutomationId($"pane-{number}-columns");
        Columns.Visible(false);
        layout.ContentHost.Add(Columns);
        viewMenu = new(window, number, attach: false);
        WireButton(layout.ViewMode, ShowViewMenu);
        WireButton(viewMenu.Details, () => ChooseView(ExplorerViewMode.Details));
        WireButton(viewMenu.Columns, () => ChooseView(ExplorerViewMode.Columns));
        ContextMenu = new(app, this);
        Grid.OnContextMenu(ContextMenu.GetCommands, ContextMenu.Invoke, ContextMenu.GetShellPaths,
            ShellMenuPresentation.Xui);
        for (uint i = 0; i < ExplorerTab.ColumnLimit; i++)
        {
            var list = Columns.Column(i);
            list.FocusEntered += Activate;
            list.OnContextMenu(ContextMenu.GetCommands, ContextMenu.Invoke, ContextMenu.GetShellPaths,
                ShellMenuPresentation.Xui);
        }
        rows = new([], Identify);
        find = layout.Find;
        closeFind = layout.CloseFind;
        WireButton(closeFind, HideFind);
        findHost = layout.FindHost;
        status = layout.Status;

        Tabs.Event += e =>
        {
            if (rendering) return;
            Activate();
            if (e.Kind == EventKind.Selection) SelectTab(e.Value);
            else if (e.Kind == EventKind.Click) Focus();
            else if (e.Kind == EventKind.Cancel) CloseTab(e.Value);
            else if (e.Kind == EventKind.Action) { NewTab(); Focus(); }
        };
        Tabs.FocusEntered += Activate;
        Grid.FocusEntered += Activate;
        Address.FocusEntered += Activate;
        find.FocusEntered += Activate;
        find.Changed += text =>
        {
            if (rendering) return;
            SaveViewport();
            Model.Active.Filter = text;
            ApplyFilter();
        };
        find.Submitted += Focus;
        Columns.SelectionChanged += e => SelectColumn(e.Column, e.Key);
        Columns.ItemActivated += e =>
        {
            if (rendering || !IsCurrentColumn(e.Column)) return;
            Activate();
            if (columnViews[(int)e.Column].Rows.Entry(e.Key.Id) is { IsDirectory: false } entry)
                app.Open(entry, this);
        };
        Grid.Event += e =>
        {
            if (rendering || IsColumns) return;
            Activate();
            if (e.Kind == EventKind.Click && rows.Entry(e.Value) is { } item)
                app.Open(item, this);
            else if (e.Kind == EventKind.Selection)
                Model.Active.SelectedPath = Grid.Selection.Focused is { } key ? rows.Entry(key.Id)?.FullPath : null;
            else if (e.Kind == EventKind.View)
            {
                SaveViewport();
                Model.Active.SortColumn = checked((int)(e.Value & 0xffffffff));
                Model.Active.SortDescending = (e.Value >> 32) != 0;
                ApplyFilter();
            }
        };
        UpdateTabs();
    }

    public ExplorerPane Model { get; private set; }
    public Grid Root { get; }
    public TabStrip Tabs { get; }
    internal TabContextMenu TabMenu { get; }
    public Button Address { get; }
    public Button BackButton { get; }
    public DataGrid Grid { get; }
    public MillerColumns Columns { get; }
    public Button ViewModeButton => layout.ViewMode;
    internal Popup ViewMenu => viewMenu.Root;
    internal Button DetailsOption => viewMenu.Details;
    internal Button ColumnsOption => viewMenu.Columns;
    internal Label Feedback => layout.Feedback;
    internal Label Status => status;
    internal Stack Footer => layout.Footer;
    public bool IsColumns => Model.Active.ViewMode == ExplorerViewMode.Columns;
    public bool FilesFocused => IsColumns
        ? Columns.ColumnCount != 0 && Columns.Column(Columns.ActiveColumn).Focused
        : Grid.Focused;
    public FileContextMenu ContextMenu { get; }
    public TextInput FindInput => find;
    public Button CloseFindButton => closeFind;
    public Reveal FindReveal => layout.FindReveal;
    internal Reveal ViewReveal => layout.ViewReveal;
    public ElementBounds FindBounds => FindReveal.GetBounds();
    public ElementBounds FindContentBounds => findHost.GetBounds();
    public bool IsLoading { get; private set; }
    public bool IsFiltering { get; private set; }
    public string? Error => error;
    public int VisibleCount => checked((int)rows.Count);
    public bool HasCurrentRows => !IsLoading && !IsFiltering && displayedTab == Model.Active.Id;
    public string TransferDirectory => IsColumns && IsCurrentColumn(Columns.ActiveColumn)
        ? columnViews[(int)Columns.ActiveColumn].Model.Snapshot.Path : Model.Active.Path;
    public FileEntry? Entry(ItemKey key) => rows.Entry(key.Id);
    public bool HasSelection => HasCurrentRows &&
        (IsColumns ? SelectedEntry is not null :
            Enumerable.Range(0, VisibleCount).Any(index => Grid.Contains(rows.Key((ulong)index))));
    public FileEntry[] SelectedEntries => HasCurrentRows
        ? IsColumns ? SelectedEntry is { } entry ? [entry] : []
        : Enumerable.Range(0, VisibleCount).Where(index => Grid.Contains(rows.Key((ulong)index)))
            .Select(index => rows.EntryAt((ulong)index)).ToArray()
        : [];
    public FileEntry? SelectedEntry
    {
        get
        {
            if (!IsColumns) return Grid.Selection.Focused is { } key ? rows.Entry(key.Id) : null;
            uint index = Columns.ActiveColumn;
            return IsCurrentColumn(index) && Columns.Column(index).Selection.Focused is { } selected
                && Columns.Column(index).Contains(selected)
                ? columnViews[(int)index].Rows.Entry(selected.Id) : null;
        }
    }

    private void WireButton(Button button, Action action)
    {
        button.Click += () => { Activate(); action(); };
        button.FocusEntered += Activate;
    }

    private ulong Identify(string path)
    {
        if (!identities.TryGetValue(path, out ulong id)) identities.Add(path, id = nextIdentity++);
        return id;
    }

    public void Activate() => app.Activate(this);
    public void Focus()
    {
        if (Model.Tabs.Count == 0) return;
        Activate();
        if (IsColumns && Columns.ColumnCount != 0) Columns.FocusColumn(Columns.ActiveColumn);
        else if (!IsColumns) Grid.Focus();
        else focusColumnsAfterRender = true;
    }
    public void SelectPath(string path)
    {
        if (IsColumns) SelectColumnPath(Columns.ActiveColumn, path);
        else if (rows.KeyForPath(path) is { } key) Grid.Select(key);
    }

    public void SelectColumnPath(uint column, string path)
    {
        if (column < columnViews.Count && columnViews[(int)column].Rows.KeyForPath(path) is { } key)
            Columns.Column(column).Select(key);
    }

    public void SetViewMode(ExplorerViewMode mode)
    {
        if (Model.Active.ViewMode == mode) return;
        SaveViewport();
        Cancel();
        error = null;
        Model.Active.SetViewMode(mode);
        viewEntryPending = Model.Active.HasSnapshot;
        focusColumnsAfterRender = mode == ExplorerViewMode.Columns;
        Render();
        if (!Model.Active.HasSnapshot) Navigate(Model.Active.Path);
        Focus();
    }

    private void StartViewEntry()
    {
        if (!viewEntryPending) return;
        viewEntryPending = false;
        bool restoreFocus = FilesFocused;
        uint duration = ViewReveal.Duration;
        ViewReveal.Duration = 0;
        ViewReveal.Open = false;
        ViewReveal.Direction = IsColumns ? RevealDirection.Right : RevealDirection.Left;
        ViewReveal.Duration = duration;
        ViewReveal.Open = true;
        if (restoreFocus) Focus();
    }

    private void SettleViewEntry()
    {
        if (!ViewReveal.Animating) return;
        uint duration = ViewReveal.Duration;
        ViewReveal.Duration = 0;
        ViewReveal.Duration = duration;
    }

    private void ShowViewMenu()
    {
        viewMenu.Details.Text = IsColumns ? "Details" : "Details (current)";
        viewMenu.Columns.Text = IsColumns ? "Columns (current)" : "Columns";
        viewMenu.Root.Show(layout.ViewMode);
        (IsColumns ? viewMenu.Columns : viewMenu.Details).Focus();
    }

    private void ChooseView(ExplorerViewMode mode)
    {
        viewMenu.Root.Dismiss();
        SetViewMode(mode);
        Focus();
    }

    public void ShowFeedback(string message)
    {
        feedback.Cancel();
        feedback.Dispose();
        feedback = new();
        layout.Feedback.Text = message;
        app.Work.Start(async token =>
        {
            await Task.Delay(TimeSpan.FromSeconds(3), token).ConfigureAwait(false);
            return true;
        }, feedback.Token, _ => layout.Feedback.Text = "",
            failure => app.Report($"Cannot clear pane feedback: {failure.Message}"));
    }

    private void SelectColumn(uint column, ItemKey key)
    {
        if (rendering || !IsCurrentColumn(column)) return;
        Activate();
        SaveViewport();
        Model.Active.ActiveColumn = (int)column;
        if (columnViews[(int)column].Rows.Entry(key.Id) is not { } entry)
        {
            Cancel();
            error = null;
            UpdateStatus(checked((int)rows.Count), Model.Active.Entries.Count, Model.Active.Filter);
            return;
        }
        if (entry.IsDirectory)
        {
            if (column + 1 >= ExplorerTab.ColumnLimit)
            {
                Cancel();
                error = $"A path can contain at most {ExplorerTab.ColumnLimit} columns. Open the folder directly to start a new path.";
                status.Text = error;
                return;
            }
            Navigate(entry.FullPath, parentColumn: (int)column);
        }
        else
        {
            Cancel();
            error = null;
            bool truncated = (int)column != Model.Active.Columns.Count - 1;
            Model.Active.SelectColumnLeaf((int)column, entry.FullPath);
            if (truncated)
            {
                Render();
                app.LocationChanged(this);
            }
            else ApplyFilter();
        }
    }

    private bool IsCurrentColumn(uint column) => IsColumns && displayedTab == Model.Active.Id
        && column < columnViews.Count && column < Model.Active.Columns.Count
        && ReferenceEquals(columnViews[(int)column].Model, Model.Active.Columns[(int)column]);

    public void Navigate(string path, int historyDelta = 0, int? parentColumn = null)
    {
        viewEntryPending = false;
        SettleViewEntry();
        SaveViewport();
        navigation.Cancel();
        navigation.Dispose();
        navigation = new();
        filtering.Cancel();
        IsFiltering = false;
        var tab = Model.Active;
        IsLoading = true;
        error = null;
        status.Text = $"Opening {path}...";
        app.Work.Start(token => app.Files.ReadDirectoryAsync(path, tab.Path, token), navigation.Token, snapshot =>
        {
            if (!ReferenceEquals(tab, Model.Active)) return;
            if (parentColumn is { } parent) tab.CommitColumn(parent, snapshot);
            else if (historyDelta == 0) tab.Commit(snapshot);
            else tab.CommitHistory(snapshot, historyDelta);
            IsLoading = false;
            Render();
            app.LocationChanged(this);
        }, failure =>
        {
            IsLoading = false;
            error = $"Cannot open {path}: {failure.Message}";
            status.Text = error;
            UpdateNavigation();
        });
    }

    public void MoveHistory(int delta)
    {
        if (Model.Active.TryGetHistory(delta, out string path)) Navigate(path, delta);
    }

    public void Up()
    {
        string? parent = Directory.GetParent(Model.Active.Path)?.FullName;
        if (parent is not null) Navigate(parent);
    }

    public void Refresh() => Navigate(Model.Active.Path);

    public void NewTab(string? path = null)
    {
        SaveViewport();
        try { Model.AddTab(path ?? Model.Active.Path); }
        catch (InvalidOperationException failure) { app.Report(failure.Message); return; }
        SwitchTab();
    }

    public void SelectTab(ulong id)
    {
        if (id == Model.Active.Id) return;
        SaveViewport();
        if (Model.SelectTab(id)) SwitchTab();
    }

    public void CloseTab(ulong? id = null)
        => CloseTabs([id ?? Model.Active.Id]);

    public void CloseTabs(IEnumerable<ulong> ids)
    {
        var closing = ids.ToHashSet();
        if (Model.Tabs.All(tab => closing.Contains(tab.Id))) { app.ClosePane(this); return; }
        SaveViewport();
        ulong old = Model.Active.Id;
        foreach (ulong id in closing) Model.CloseTab(id);
        if (old != Model.Active.Id) SwitchTab();
        else UpdateTabs();
    }

    public void MoveTab(ulong id, int delta)
    {
        if (Model.MoveTab(id, delta)) UpdateTabs();
    }

    public void DuplicateTab(ExplorerTab source)
    {
        SaveViewport();
        try { Model.DuplicateTab(source); }
        catch (InvalidOperationException failure) { app.Report(failure.Message); return; }
        SwitchTab();
        Focus();
    }

    internal void CaptureViewport() => SaveViewport();

    internal void SetTransferredModel(ExplorerPane model)
    {
        CancelForTransfer();
        ClearColumns();
        Model = model;
        displayedTab = 0;
        error = null;
    }

    internal void CancelForTransfer()
    {
        Cancel();
        feedback.Cancel();
        layout.Feedback.Text = "";
    }

    internal void RenderTransferredModel()
    {
        bool visible = Model.Tabs.Count != 0;
        SetPaneControlsVisible(visible);
        if (!visible)
        {
            Grid.Visible(false);
            Columns.Visible(false);
            SetFindVisible(false);
            UpdateTabs();
            return;
        }
        Render();
        if (!Model.Active.HasSnapshot) Navigate(Model.Active.Path);
    }

    private void SetPaneControlsVisible(bool visible)
    {
        foreach (var control in new Control[] { back, forward, up, Address, layout.Refresh,
            layout.Commands, layout.ViewMode, layout.Feedback, status })
            control.Visible(visible);
    }

    internal void ReplaceTabsFrom(FilePaneView source)
    {
        source.SaveViewport();
        Cancel();
        Model.ReplaceTabs(source.Model);
        SwitchTab();
    }

    internal void ResetTabs(string path)
    {
        Cancel();
        ClearColumns();
        Model.ResetTabs(path);
        SetPaneControlsVisible(true);
        UpdateTabs();
    }

    internal void StartWithDuplicate(ExplorerTab source)
    {
        Cancel();
        Model.ResetTabs(source);
        SwitchTab();
    }

    public void CycleTab(int delta)
    {
        SaveViewport();
        Model.CycleTab(delta);
        SwitchTab();
    }

    private void SwitchTab()
    {
        SetPaneControlsVisible(true);
        bool filesHadFocus = Grid.Focused || Enumerable.Range(0, columnViews.Count)
            .Any(i => Columns.Column((uint)i).Focused);
        Cancel();
        if (displayedTab != Model.Active.Id && columnViews.Count > 0) ClearColumns();
        error = null;
        Render();
        app.LocationChanged(this, recordRecent: false);
        if (!IsColumns || !Model.Active.HasSnapshot || Model.Active.Columns.Count == 0) Navigate(Model.Active.Path);
        if (filesHadFocus) Focus();
    }

    public void ShowFind()
    {
        Model.Active.FindOpen = true;
        SetFindVisible(true);
        find.Focus(selectAll: true);
    }

    public void SetFilter(string query)
    {
        SaveViewport();
        Model.Active.Filter = query;
        rendering = true;
        try { find.Text = query; }
        finally { rendering = false; }
        ApplyFilter();
    }

    public void HideFind()
    {
        Model.Active.FindOpen = false;
        SetFilter("");
        SetFindVisible(false);
        Focus();
    }

    public bool HandleFindKey(UiKeyEvent key)
    {
        if (!Model.Active.FindOpen || !find.Focused || (key.Modifiers & KeyModifiers.Alt) != 0) return false;
        GridNavigation? direction = key.VirtualKey switch
        {
            0x26 => GridNavigation.Previous,
            0x28 => GridNavigation.Next,
            0x21 => GridNavigation.PagePrevious,
            0x22 => GridNavigation.PageNext,
            0x24 when (key.Modifiers & KeyModifiers.Control) != 0 => GridNavigation.First,
            0x23 when (key.Modifiers & KeyModifiers.Control) != 0 => GridNavigation.Last,
            _ => null
        };
        if (direction is null) return false;
        // Ctrl+Home/End selects the edge; unmodified Home/End still edit the query.
        var modifiers = key.VirtualKey is 0x24 or 0x23 ? key.Modifiers & ~KeyModifiers.Control : key.Modifiers;
        if (IsColumns && Columns.ColumnCount != 0)
        {
            uint last = Columns.ColumnCount - 1;
            var list = Columns.Column(last);
            int count = checked((int)rows.Count);
            if (count == 0) return true;
            int index = list.Selection.Focused is { } selected && list.Contains(selected)
                && rows.Find(selected) is { } found ? (int)found : -1;
            int page = Math.Max(1, (int)(list.GetBounds().Height / 40));
            int target = direction.Value switch
            {
                GridNavigation.First => 0,
                GridNavigation.Last => count - 1,
                GridNavigation.PagePrevious => Math.Max(0, index - page),
                GridNavigation.PageNext => Math.Min(count - 1, index + page),
                GridNavigation.Previous => Math.Max(0, index - 1),
                _ => Math.Min(count - 1, index + 1)
            };
            list.Select(rows.Key((ulong)target));
        }
        else Grid.Navigate(direction.Value, modifiers);
        return true;
    }

    private void SaveViewport()
    {
        if (Model.Tabs.Count == 0) return;
        if (displayedTab != Model.Active.Id) return;
        if (IsColumns)
        {
            for (int i = 0; i < columnViews.Count && i < Columns.ColumnCount; i++)
            {
                var column = columnViews[i];
                column.Model.ScrollOffset = Columns.Column((uint)i).Offset;
                column.Model.SelectedPath = Columns.Column((uint)i).Selection.Focused is { } key
                    && Columns.Column((uint)i).Contains(key)
                    ? column.Rows.Entry(key.Id)?.FullPath : null;
            }
            Model.Active.ActiveColumn = (int)Columns.ActiveColumn;
            if (Model.Active.Columns.Count > 0)
            {
                Model.Active.SelectedPath = Model.Active.Columns[^1].SelectedPath;
                Model.Active.ScrollOffset = Model.Active.Columns[^1].ScrollOffset;
            }
            return;
        }
        Model.Active.ScrollOffset = Grid.Offset;
        Model.Active.SelectedPath = SelectedEntry?.FullPath;
    }

    private void Render()
    {
        var retained = new Dictionary<string, ulong>(StringComparer.OrdinalIgnoreCase);
        var allEntries = IsColumns ? Model.Active.Columns.SelectMany(c => c.Snapshot.Entries) : Model.Active.Entries;
        foreach (var entry in allEntries) retained[entry.FullPath] = Identify(entry.FullPath);
        identities = retained;
        rendering = true;
        try
        {
            find.Text = Model.Active.Filter;
            SetFindVisible(Model.Active.FindOpen);
            UpdateTabs();
            UpdateNavigation();
            Grid.SetSort((uint)Model.Active.SortColumn, Model.Active.SortDescending);
            Grid.Visible(!IsColumns);
            Columns.Visible(IsColumns);
            layout.ViewMode.Help(IsColumns ? "Current view: Columns. Choose a view." : "Current view: Details. Choose a view.");
            if (!IsColumns) ClearColumns();
            else
            {
                int retainedColumns = 0;
                while (retainedColumns < columnViews.Count && retainedColumns < Model.Active.Columns.Count
                    && ReferenceEquals(columnViews[retainedColumns].Model, Model.Active.Columns[retainedColumns]))
                    retainedColumns++;
                if (retainedColumns < columnViews.Count)
                {
                    bool restoreFocus = FilesFocused;
                    Columns.SetColumns(columnViews.Take(retainedColumns).Select(c =>
                        new MillerColumn(TabName(c.Model.Snapshot.Path), c.Source,
                            c.Rows.KeyForPath(c.Model.SelectedPath))).ToArray());
                    foreach (var obsolete in columnViews.Skip(retainedColumns)) obsolete.Source.Dispose();
                    columnViews.RemoveRange(retainedColumns, columnViews.Count - retainedColumns);
                    if (restoreFocus) focusColumnsAfterRender = true;
                }
            }
        }
        finally { rendering = false; }
        ApplyFilter();
    }

    private void SetFindVisible(bool visible) => layout.FindOpen = visible;

    private void ApplyFilter()
    {
        SettleViewEntry();
        if (IsColumns) { ApplyColumnFilter(); return; }
        filtering.Cancel();
        filtering.Dispose();
        filtering = new();
        IsFiltering = true;
        var tab = Model.Active;
        var entries = tab.Entries;
        string query = tab.Filter;
        int column = tab.SortColumn;
        bool descending = tab.SortDescending;
        app.Work.Start(token => Task.Run(() =>
        {
            token.ThrowIfCancellationRequested();
            var result = FileSystemService.FilterAndSort(entries, query, column, descending);
            token.ThrowIfCancellationRequested();
            return result;
        }, token), filtering.Token, result =>
        {
            if (!ReferenceEquals(tab, Model.Active)) return;
            IsFiltering = false;
            rows = new(result, Identify);
            rendering = true;
            try
            {
                using var source = window.ImmutableSource(rows);
                Grid.SetSource(source);
                displayedTab = tab.Id;
                if (rows.KeyForPath(tab.SelectedPath) is { } key) Grid.Select(key);
                Grid.Offset = tab.ScrollOffset;
            }
            finally { rendering = false; }
            StartViewEntry();
            UpdateStatus(result.Count, entries.Count, query);
        }, failure =>
        {
            IsFiltering = false;
            viewEntryPending = false;
            status.Text = $"Cannot filter this folder: {failure.Message}";
        });
    }

    private void UpdateStatus(int count, int total, string query)
        => status.Text = error ?? (IsLoading ? "Opening folder..." : query.Length == 0
            ? $"{count:N0} items" : $"{count:N0} of {total:N0} items match \"{query}\"");

    private void ApplyColumnFilter()
    {
        filtering.Cancel();
        filtering.Dispose();
        filtering = new();
        IsFiltering = true;
        var tab = Model.Active;
        var chain = tab.Columns.ToArray();
        string query = tab.Filter;
        int sort = tab.SortColumn;
        bool descending = tab.SortDescending;
        var reusable = chain.Select((column, i) => columnViews.FirstOrDefault(view =>
            ReferenceEquals(view.Model, column) && view.Query == (i == chain.Length - 1 ? query : "")
            && view.Sort == sort && view.Descending == descending)).ToArray();
        app.Work.Start(token => Task.Run(() =>
        {
            var results = new IReadOnlyList<FileEntry>?[chain.Length];
            for (int i = 0; i < chain.Length; i++)
            {
                token.ThrowIfCancellationRequested();
                if (reusable[i] is null)
                    results[i] = FileSystemService.FilterAndSort(chain[i].Snapshot.Entries,
                        i == chain.Length - 1 ? query : "", sort, descending);
            }
            return results;
        }, token), filtering.Token, results =>
        {
            if (!ReferenceEquals(tab, Model.Active) || !IsColumns) return;
            IsFiltering = false;
            var next = new List<ColumnPresentation>();
            try
            {
                for (int i = 0; i < chain.Length; i++)
                {
                    if (reusable[i] is { } existing) next.Add(existing);
                    else
                    {
                        var sourceRows = new FileRows(results[i]!, Identify);
                        next.Add(new(chain[i], i == chain.Length - 1 ? query : "", sort, descending,
                            sourceRows, window.ImmutableSource(sourceRows)));
                    }
                }
                rendering = true;
                Columns.SetColumns(next.Select(c => new MillerColumn(TabName(c.Model.Snapshot.Path), c.Source,
                    c.Rows.KeyForPath(c.Model.SelectedPath))).ToArray());
                for (uint i = 0; i < next.Count; i++)
                {
                    var list = Columns.Column(i);
                    list.Offset = next[(int)i].Model.ScrollOffset;
                }
                if (next.Count > 0) Columns.ActiveColumn = (uint)Math.Clamp(tab.ActiveColumn, 0, next.Count - 1);
                foreach (var old in columnViews)
                    if (!next.Contains(old)) old.Source.Dispose();
                columnViews.Clear();
                columnViews.AddRange(next);
                displayedTab = tab.Id;
                rows = next.Count == 0 ? new([], Identify) : next[^1].Rows;
                if (focusColumnsAfterRender && next.Count > 0)
                {
                    focusColumnsAfterRender = false;
                    Columns.FocusColumn(Columns.ActiveColumn);
                }
            }
            catch
            {
                foreach (var added in next)
                    if (!columnViews.Contains(added)) added.Source.Dispose();
                throw;
            }
            finally { rendering = false; }
            StartViewEntry();
            UpdateStatus(checked((int)rows.Count), tab.Entries.Count, query);
        }, failure =>
        {
            IsFiltering = false;
            viewEntryPending = false;
            error = $"Cannot filter this folder: {failure.Message}";
            status.Text = error;
        });
    }

    private void ClearColumns()
    {
        Columns.SetColumns([]);
        foreach (var column in columnViews) column.Source.Dispose();
        columnViews.Clear();
    }

    public void DisposeSources()
    {
        feedback.Cancel();
        feedback.Dispose();
        foreach (var column in columnViews) column.Source.Dispose();
        columnViews.Clear();
    }

    private void UpdateTabs()
    {
        bool prior = rendering;
        rendering = true;
        try
        {
            Tabs.SetTabItems(Model.Tabs.Select(t => new TabEntry(t.Id, TabName(t.Path),
                ButtonIcon.Folder, t.Path)).ToArray(), Model.Tabs.Count == 0 ? null : Model.Active.Id);
        }
        finally { rendering = prior; }
    }

    private static string TabName(string path)
    {
        string name = Path.GetFileName(Path.TrimEndingDirectorySeparator(path));
        return name.Length == 0 ? path : name;
    }

    private void UpdateNavigation()
    {
        Address.Text = Model.Active.Path;
        back.Enabled = Model.Active.CanBack;
        forward.Enabled = Model.Active.CanForward;
        up.Enabled = Directory.GetParent(Model.Active.Path) is not null;
    }

    internal void Report(string message) => app.Report(message);

    public void Cancel()
    {
        viewEntryPending = false;
        SettleViewEntry();
        if (viewMenu.Root.IsOpen) viewMenu.Root.Dismiss();
        navigation.Cancel();
        filtering.Cancel();
        IsLoading = false;
        IsFiltering = false;
        focusColumnsAfterRender = false;
    }
}
