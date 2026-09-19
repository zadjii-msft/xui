using System.ComponentModel;
using System.Diagnostics;
using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed partial class ExplorerApplication : IDisposable
{
    private readonly ExplorerWindows windows;
    private readonly AppStateStore store;
    private readonly Label notification;
    private readonly SplitView split;
    private readonly bool smoke;
    private readonly bool stateWritable;
    private FilePaneView? active;
    private bool splitOpen;
    private bool rightInitialized;
    private bool disposed;
    private string? iconPath;

    public ExplorerApplication(ExplorerWindows windows, Application application, PreviewController preview, string initialPath, bool smoke = false)
    {
        this.windows = windows;
        Application = application;
        Preview = preview;
        this.smoke = smoke;
        initialPath = FileSystemService.ResolvePath(initialPath, Environment.CurrentDirectory);
        Window = application.CreateWindow("XUI / Files", 1320, 840, customTitlebar: true, visualStyle: VisualStyle.WinUI);
        try
        {
            Work = new(Window);
            Files = new();
            store = smoke ? new(Path.Combine(Environment.CurrentDirectory, ".file-explorer-smoke-state", "state.json")) : new();
            string startupMessage = "";
            try
            {
                State = store.Load();
                stateWritable = true;
            }
            catch (Exception error) when (UiWork.IsExpected(error))
            {
                State = new();
                startupMessage = $"Saved state was not loaded: {error.Message} Bookmarks will not be saved until you repair state.json and restart.";
            }
            Left = new(this, 1, initialPath, Window.TitlebarTabs);
            Right = new(this, 2, initialPath, Window.TitlebarSecondaryTabs);
            active = Left;
            Sidebar = new(this);
            Palettes = new(this);
            var layout = new ExplorerLayout(Window, Sidebar.Presentation, Left.Root, Right.Root, startupMessage);
            notification = layout.Notification;
            Window.IconErrorHandler = error => Report($"Cannot load the folder window icon: {error}");
            split = layout.Panes;
            split.Event += e =>
            {
                if (e.Kind != EventKind.View) return;
                Window.TitlebarSecondaryTabs.Enabled = e.Value != 0;
                if (e.Value == 0 && ReferenceEquals(Active, Right) && Left.Model.Tabs.Count != 0) Left.Focus();
            };
            Window.TitlebarSecondaryTabs.Visible(true).SetEnabled(false);
            Window.TitlebarLeading.SetText("Navigation").SetAutomationId("navigation-toggle")
                .Help("Show or collapse navigation");
            Window.TitlebarLeading.SetStyle(ExplorerStyles.IconButton);
            Window.TitlebarLeading.Click += Sidebar.Toggle;
            Window.SetTitlebarLayout(Left.Root, Right.Root);
            Commands = CreateCommands();
            Transfers = new(this);
            Transfers.Bind(Left);
            Transfers.Bind(Right);
            customizationEditor = new(this);
            try { ValidateCustomization(State.Customization); }
            catch (InvalidDataException error)
            {
                startupMessage = $"Keyboard preferences were not applied: {error.Message}";
                ignoreSavedBindings = true;
                stateWritable = false;
                Report(startupMessage);
            }
            ApplyCustomization();
            Window.KeyHandler = HandleKey;
            Window.NavigationHandler = HandleNavigation;
            Window.TabDragHandler = HandleTabDrag;
            Window.Closed += _ =>
            {
                CloseRequested = true;
                drag = null;
                Left.Cancel();
                Right.Cancel();
                Work.Dispose();
                if (!Application.Post(Dispose))
                    throw new InvalidOperationException("The application rejected Explorer retirement.");
            };
            Sidebar.Refresh();
            UpdateTitle();
        }
        catch
        {
            Work?.Dispose();
            Sidebar?.Dispose();
            Left?.Cancel();
            Right?.Cancel();
            Left?.DisposeSources();
            Right?.DisposeSources();
            Window.Dispose();
            throw;
        }
    }

    public Window Window { get; }
    public Application Application { get; }
    public UiWork Work { get; }
    public FileSystemService Files { get; }
    public ExplorerState State { get; }
    public FilePaneView Left { get; }
    public FilePaneView Right { get; }
    public FilePaneView Active => active ?? Left;
    public PaletteController Palettes { get; }
    public PreviewController Preview { get; }
    public NavigationSidebar Sidebar { get; }
    public IReadOnlyList<ExplorerCommand> Commands { get; }
    public FileTransfers Transfers { get; }
    public bool SecondPaneVisible => split.Expanded;
    internal SplitView Panes => split;
    internal Label Notification => notification;
    internal int FileOpenCount { get; private set; }
    internal string? NewWindowPath { get; private set; }
    internal bool CloseRequested { get; private set; }
    internal bool IsDisposed => disposed;

    public void Run(ExplorerSmokeMode smokeMode = ExplorerSmokeMode.Full)
    {
        Left.Navigate(Left.Model.Active.Path);
        Task? smokeTask = smoke ? ExplorerSmoke.Start(this, smokeMode) : null;
        try { Application.Show(Window); Application.Run(); }
        finally
        {
            Work.Dispose();
            smokeTask?.GetAwaiter().GetResult();
        }
    }

    public void Activate(FilePaneView pane)
    {
        if (pane.Model.Tabs.Count == 0) return;
        if (ReferenceEquals(active, pane)) return;
        active?.AddressBar.Cancel();
        keySequence.Reset();
        active = pane;
        Sidebar.Refresh();
        RefreshCommandAvailability();
        UpdateTitle();
    }

    public void LocationChanged(FilePaneView pane, bool recordRecent = true)
    {
        if (ReferenceEquals(pane, Active)) keySequence.Reset();
        RefreshCommandAvailability();
        if (recordRecent)
        {
            State.AddRecent(pane.Model.Active.Path);
            SaveState();
        }
        if (ReferenceEquals(pane, Active))
        {
            Sidebar.Refresh();
            UpdateTitle();
        }
    }

    private void UpdateTitle()
    {
        string path = Active.Model.Active.Path;
        string name = Path.GetFileName(Path.TrimEndingDirectorySeparator(path));
        Window.SetTitle($"{(name.Length == 0 ? path : name)} ({path}) - FileExplorer.xui");
        if (!string.Equals(iconPath, path, StringComparison.Ordinal))
        {
            try { Window.SetIconSource(path); iconPath = path; }
            catch (Exception error) when (error is XuiException or ArgumentException)
            {
                Report($"Cannot load the folder window icon: {error.Message}");
            }
        }
    }

    public void ToggleSplit()
    {
        splitOpen = !splitOpen;
        if (splitOpen) Window.TitlebarSecondaryTabs.Visible(true);
        split.SecondVisible = splitOpen;
        Window.TitlebarSecondaryTabs.Enabled = splitOpen;
        if (splitOpen)
        {
            InitializeRight();
            if (split.Expanded) Right.Focus();
            else Report("Widen the window to show both file panes.");
        }
        else
        {
            Right.Cancel();
            Left.Focus();
        }
    }

    private void InitializeRight()
    {
        if (rightInitialized) { Right.Refresh(); return; }
        rightInitialized = true;
        Right.ResetTabs(Active.Model.Active.Path, Active.Model.Active.Partition);
        Right.Navigate(Active.Model.Active.Path);
    }

    public void DuplicateInNewPane(FilePaneView source, ExplorerTab tab)
    {
        source.CaptureViewport();
        var destination = ReferenceEquals(source, Left) ? Right : Left;
        if (destination.Model.Tabs.Count >= ExplorerPane.TabLimit)
        {
            Report($"A pane can contain at most {ExplorerPane.TabLimit} tabs.");
            return;
        }
        if (!splitOpen)
        {
            splitOpen = true;
            Window.TitlebarSecondaryTabs.Visible(true);
            split.SecondVisible = true;
            Window.TitlebarSecondaryTabs.Enabled = true;
        }
        if (!rightInitialized && ReferenceEquals(destination, Right))
        {
            rightInitialized = true;
            Right.StartWithDuplicate(tab);
        }
        else destination.DuplicateTab(tab);
        if (split.Expanded) destination.Focus();
        else Report("Widen the window to show both file panes.");
    }

    public void ClosePane(FilePaneView pane)
    {
        if (!splitOpen || !rightInitialized)
        {
            CloseRequested = true;
            if (!smoke) Window.Close();
            return;
        }
        if (ReferenceEquals(pane, Left)) Left.ReplaceTabsFrom(Right);
        Right.ResetTabs(Left.Model.Active.Path);
        splitOpen = false;
        rightInitialized = false;
        split.SecondVisible = false;
        Window.TitlebarSecondaryTabs.Enabled = false;
        Left.Focus();
    }

    public void NewWindow(string path, ExplorerTab? source = null)
    {
        NewWindowPath = path;
        ExplorerApplication? created = null;
        try
        {
            created = windows.Create(path);
            created.State.Customization = State.Customization.Clone();
            created.ApplyCustomization();
            created.Left.Model.Active.SetPartition((source ?? Active.Model.Active).Partition);
            created.Left.Navigate(created.Left.Model.Active.Path);
            Application.Show(created.Window);
        }
        catch (Exception error) when (error is XuiException or InvalidOperationException || UiWork.IsExpected(error))
        {
            if (created is not null) RetireWindow(created);
            Report($"Cannot open another explorer window: {error.Message}");
        }
    }

    public FilePaneView? OtherPane(FilePaneView from, bool show)
    {
        if (show && !splitOpen)
        {
            splitOpen = true;
            Window.TitlebarSecondaryTabs.Visible(true);
            split.SecondVisible = true;
            Window.TitlebarSecondaryTabs.Enabled = true;
            InitializeRight();
        }
        if (!split.Expanded)
        {
            Report("Widen the window to show both file panes.");
            return null;
        }
        return ReferenceEquals(from, Left) ? Right : Left;
    }

    public void Open(FileEntry entry, FilePaneView pane)
    {
        if (entry.IsDirectory) { pane.Navigate(entry.FullPath); return; }
        FileOpenCount++;
        if (smoke) return;
        try
        {
            Process.Start(new ProcessStartInfo(entry.FullPath) { UseShellExecute = true });
        }
        catch (Exception error) when (error is Win32Exception or InvalidOperationException || UiWork.IsExpected(error))
        {
            Report($"Cannot open {entry.Name}: {error.Message}");
        }
    }

    public void Report(string message) => notification.SetText(message).Visible(true);

    private void Bookmark() => ToggleBookmark(Active.Model.Active.Path);

    public void ToggleBookmark(string path)
    {
        State.ToggleBookmark(path);
        SaveState();
        Sidebar.Refresh();
    }

    private void SaveState()
    {
        if (!stateWritable || smoke) return;
        try { store.Save(State); }
        catch (Exception error) when (UiWork.IsExpected(error)) { Report($"Cannot save bookmarks and recents: {error.Message}"); }
    }

    private void ToggleTheme()
    {
        var settings = State.Customization.Clone();
        settings.Theme = settings.Theme == "light" ? "dark" : "light";
        try { SetCustomization(settings); }
        catch (Exception error) when (UiWork.IsExpected(error)) { Report(error.Message); }
    }

    private IReadOnlyList<ExplorerCommand> CreateCommands() =>
    [
        new("Commands", "Ctrl+Shift+P", () => Palettes.ShowCommands(), Id: "commands"),
        new("Customize Explorer", "", ShowCustomization, Id: "customization"),
        new("Move tab left", "Ctrl+Shift+PageUp", () => Active.MoveTab(Active.Model.Active.Id, -1), Id: "tab.move-left"),
        new("Move tab right", "Ctrl+Shift+PageDown", () => Active.MoveTab(Active.Model.Active.Id, 1), Id: "tab.move-right"),
        new("New tab", "Ctrl+T", () => Active.NewTab(), Id: "new-tab"),
        new("Close tab", "Ctrl+W", () => Active.CloseTab(), Id: "close-tab"),
        new("Duplicate tab", "", () => Active.DuplicateTab(Active.Model.Active),
            () => Active.Model.Tabs.Count < ExplorerPane.TabLimit, Id: "duplicate-tab"),
        new("Duplicate tab to new window", "Ctrl+N", () => NewWindow(Active.Model.Active.Path, Active.Model.Active), Id: "duplicate-tab-to-new-window"),
        new("Duplicate in new pane", "", () => DuplicateInNewPane(Active, Active.Model.Active), Id: "duplicate-in-new-pane"),
        new("Close all tabs", "Ctrl+Shift+W", () => ClosePane(Active), Id: "close-all-tabs"),
        new("Next tab", "Ctrl+Tab", () => Active.CycleTab(1), Id: "next-tab"),
        new("Previous tab", "Ctrl+Shift+Tab", () => Active.CycleTab(-1), Id: "previous-tab"),
        new("Toggle split panes", "Ctrl+\\", ToggleSplit, Id: "toggle-split-panes"),
        new("Focus other pane", "F6", () => OtherPane(Active, show: true)?.Focus(), Id: "focus-other-pane"),
        new("Go to folder", "Ctrl+L", () => Active.AddressBar.ShowNavigation(), Id: "go-to-folder"),
        new("Back", "Alt+Left", () => Active.MoveHistory(-1), () => Active.Model.Active.CanBack, Id: "back"),
        new("Forward", "Alt+Right", () => Active.MoveHistory(1), () => Active.Model.Active.CanForward, Id: "forward"),
        new("Up to parent folder", "Alt+Up", () => Active.Up(), Id: "up-to-parent-folder"),
        new("Refresh folder", "F5", () => Active.Refresh(), Id: "refresh-folder"),
        new("Preview selected item", "Space", () => Preview.ShowSelected(Active),
            () => Preview.CanPreview(Active), Id: "preview-selected-item"),
        new("Copy files", "Ctrl+C", () => Transfers.Copy(Active, cut: false),
            () => Active.HasSelection && !Transfers.Busy, Id: "copy-files"),
        new("Cut files", "Ctrl+X", () => Transfers.Copy(Active, cut: true),
            () => Active.HasSelection && !Transfers.Busy, Id: "cut-files"),
        new("Paste files into this folder", "Ctrl+V", () => Transfers.Paste(Active),
            () => Active.HasCurrentRows && !Transfers.Busy, Id: "paste-files-into-this-folder"),
        new("Copy file paths", "Ctrl+Shift+C", () => Transfers.CopyPaths(Active),
            () => Active.HasSelection && !Transfers.Busy, Id: "copy-file-paths"),
        new("Use XL Icons view", "", () => Active.SetViewMode(ExplorerViewMode.ExtraLargeIcons), Id: "use-xl-icons-view"),
        new("Use L Icons view", "", () => Active.SetViewMode(ExplorerViewMode.LargeIcons), Id: "use-l-icons-view"),
        new("Use M Icons view", "", () => Active.SetViewMode(ExplorerViewMode.MediumIcons), Id: "use-m-icons-view"),
        new("Use List view", "", () => Active.SetViewMode(ExplorerViewMode.List), Id: "use-list-view"),
        new("Use Tree view", "", () => Active.SetViewMode(ExplorerViewMode.Tree), Id: "use-tree-view"),
        new("Use Details view", "", () => Active.SetViewMode(ExplorerViewMode.Details), Id: "use-details-view"),
        new("Use Columns view", "", () => Active.SetViewMode(ExplorerViewMode.Columns), Id: "use-columns-view"),
        new("Find in this folder", "Ctrl+F", () => Active.ShowFind(), Id: "find-in-this-folder"),
        new("Clear folder filter", "Escape", () => Active.HideFind(),
            () => Active.Model.Active.FindOpen, Id: "clear-folder-filter"),
        new("Filter navigation", "Alt+F", Sidebar.FocusFilter, Id: "filter-navigation"),
        new("Toggle navigation pane", "", Sidebar.Toggle, Id: "toggle-navigation-pane"),
        new("Add or remove folder bookmark", "Ctrl+D", Bookmark, Id: "add-or-remove-folder-bookmark"),
        new("Open selected folder in new tab", "", () => { if (Active.SelectedEntry is { IsDirectory: true } e) Active.NewTab(e.FullPath); },
            () => Active.SelectedEntry is { IsDirectory: true }, Id: "open-selected-folder-in-new-tab"),
        new("Open selected item in other pane", "Ctrl+Enter", () =>
            { if (Active.SelectedEntry is { } e && OtherPane(Active, show: true) is { } target) Open(e, target); },
            () => Active.SelectedEntry is not null, Id: "open-selected-item-in-other-pane"),
        new("Toggle light / dark theme", "Ctrl+F6", ToggleTheme, Id: "toggle-light-dark-theme"),
        new("Close window", "Alt+F4", Window.Close, Id: "close-window")
    ];

    private bool HandleNavigation(UiNavigationEvent navigation)
    {
        if (Palettes.IsOpen) return true;
        var pane = Active;
        if (navigation.Position is { } point)
        {
            if (Right.Model.Tabs.Count != 0 && split.Expanded
                && (Contains(Right.Root.GetBounds(), point) || Contains(Right.Tabs.GetBounds(), point))) pane = Right;
            else if (Left.Model.Tabs.Count != 0
                && (Contains(Left.Root.GetBounds(), point) || Contains(Left.Tabs.GetBounds(), point))) pane = Left;
        }
        if (ReferenceEquals(pane, Right) && !split.Expanded && Left.Model.Tabs.Count != 0) pane = Left;
        if (pane.Model.Tabs.Count == 0) return true;
        Activate(pane);
        pane.MoveHistory(navigation.Direction == NavigationDirection.Back ? -1 : 1);
        return true;

        static bool Contains(ElementBounds bounds, NavigationPoint point) =>
            point.X >= bounds.X && point.X < bounds.X + bounds.Width &&
            point.Y >= bounds.Y && point.Y < bounds.Y + bounds.Height;
    }

    private bool HandleKey(UiKeyEvent key)
    {
        if (Active.Model.Tabs.Count == 0) return false;
        uint vk = key.VirtualKey;
        var modifiers = key.Modifiers;
        if (Palettes.HandleKey(vk, modifiers)) return true;
        if (Active.AddressBar.HandleKey(key)) return true;
        if (Active.HandleFindKey(key)) return true;
        if (HandleCustomizationKey(key)) return true;
        if (key.IsTextInput && Active.FilesFocused) Active.ShowFind();
        return false;
    }

    public void Dispose()
    {
        if (disposed) return;
        Sidebar.Dispose();
        Left.Cancel();
        Right.Cancel();
        Work.Dispose();
        Left.DisposeSources();
        Right.DisposeSources();
        Window.Dispose();
        disposed = true;
        drag = null;
        windows.Forget(this);
    }
}
