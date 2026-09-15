using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class FilePaneView
{
    private readonly ExplorerApplication app;
    private readonly Window window;
    private Dictionary<string, ulong> identities = new(StringComparer.OrdinalIgnoreCase);
    private readonly Button back, forward, up;
    private readonly FilePaneLayout layout;
    private readonly Stack findHost;
    private readonly Button closeFind;
    private readonly Label status;
    private readonly TextInput find;
    private CancellationTokenSource navigation = new();
    private CancellationTokenSource filtering = new();
    private ulong nextIdentity = 1;
    private ulong displayedTab;
    private FileRows rows;
    private bool rendering;
    private string? error;

    public FilePaneView(ExplorerApplication app, int number, string path, TabStrip tabs)
    {
        this.app = app;
        window = app.Window;
        Model = new(path);
        Tabs = tabs;
        Tabs.SetAutomationId($"pane-{number}-tabs");
        layout = new(window, number, path, attach: false);
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
        WireButton(layout.NewTab, () => NewTab());
        WireButton(layout.Commands, () => app.Palettes.ShowCommands());
        Grid = layout.Files;
        ContextMenu = new(app, this);
        Grid.OnContextMenu(ContextMenu.GetCommands, ContextMenu.Invoke, ContextMenu.GetShellPaths,
            ShellMenuPresentation.Xui);
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
            else if (e.Kind == EventKind.Action) NewTab();
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
        find.Submitted += () => Grid.Focus();
        Grid.Event += e =>
        {
            if (rendering) return;
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

    public ExplorerPane Model { get; }
    public Grid Root { get; }
    public TabStrip Tabs { get; }
    public Button Address { get; }
    public Button BackButton { get; }
    public DataGrid Grid { get; }
    public FileContextMenu ContextMenu { get; }
    public TextInput FindInput => find;
    public Button CloseFindButton => closeFind;
    public ElementBounds FindBounds => findHost.GetBounds();
    public bool IsLoading { get; private set; }
    public bool IsFiltering { get; private set; }
    public string? Error => error;
    public int VisibleCount => checked((int)rows.Count);
    public FileEntry? SelectedEntry => Grid.Selection.Focused is { } key ? rows.Entry(key.Id) : null;

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
    public void Focus() { Activate(); Grid.Focus(); }
    public void SelectPath(string path)
    {
        if (rows.KeyForPath(path) is { } key) Grid.Select(key);
    }

    public void Navigate(string path, int historyDelta = 0)
    {
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
            if (historyDelta == 0) tab.Commit(snapshot);
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
    {
        SaveViewport();
        ulong old = Model.Active.Id;
        if (!Model.CloseTab(id ?? old)) return;
        if (old != Model.Active.Id) SwitchTab();
        else UpdateTabs();
    }

    public void CycleTab(int delta)
    {
        SaveViewport();
        Model.CycleTab(delta);
        SwitchTab();
    }

    private void SwitchTab()
    {
        navigation.Cancel();
        filtering.Cancel();
        Render();
        app.LocationChanged(this, recordRecent: false);
        Navigate(Model.Active.Path);
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
        Grid.Focus();
    }

    private void SaveViewport()
    {
        if (displayedTab != Model.Active.Id) return;
        Model.Active.ScrollOffset = Grid.Offset;
        Model.Active.SelectedPath = SelectedEntry?.FullPath;
    }

    private void Render()
    {
        var retained = new Dictionary<string, ulong>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in Model.Active.Entries) retained.Add(entry.FullPath, Identify(entry.FullPath));
        identities = retained;
        rendering = true;
        try
        {
            find.Text = Model.Active.Filter;
            SetFindVisible(Model.Active.FindOpen);
            UpdateTabs();
            UpdateNavigation();
            Grid.SetSort((uint)Model.Active.SortColumn, Model.Active.SortDescending);
        }
        finally { rendering = false; }
        ApplyFilter();
    }

    private void SetFindVisible(bool visible) => layout.FindOpen = visible;

    private void ApplyFilter()
    {
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
            status.Text = error ?? (query.Length == 0
                ? $"{result.Count:N0} items"
                : $"{result.Count:N0} of {entries.Count:N0} items match \"{query}\"");
        }, failure =>
        {
            IsFiltering = false;
            status.Text = $"Cannot filter this folder: {failure.Message}";
        });
    }

    private void UpdateTabs()
    {
        bool prior = rendering;
        rendering = true;
        try
        {
            Tabs.SetTabs(Model.Tabs.Select(t => new Choice(t.Id, TabName(t.Path))).ToArray(), Model.Active.Id);
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

    public void Cancel()
    {
        navigation.Cancel();
        filtering.Cancel();
        IsLoading = false;
        IsFiltering = false;
    }
}
