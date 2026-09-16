using System.ComponentModel;
using System.Diagnostics;
using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class ExplorerApplication : IDisposable
{
    private readonly AppStateStore store;
    private readonly Label notification;
    private readonly SplitView split;
    private readonly bool smoke;
    private readonly bool stateWritable;
    private FilePaneView? active;
    private bool splitOpen;
    private bool rightInitialized;
    private bool light;

    public ExplorerApplication(string initialPath, bool smoke = false)
    {
        this.smoke = smoke;
        initialPath = FileSystemService.ResolvePath(initialPath, Environment.CurrentDirectory);
        Window = new("XUI / Files", 1320, 840, customTitlebar: true, visualStyle: VisualStyle.WinUI);
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
        Preview = new(this);
        var layout = new ExplorerLayout(Window, Sidebar.View, Left.Root, Right.Root, startupMessage);
        notification = layout.Notification;
        split = layout.Panes;
        split.Event += e =>
        {
            if (e.Kind != EventKind.View) return;
            Window.TitlebarSecondaryTabs.Visible = e.Value != 0;
            if (e.Value == 0 && ReferenceEquals(Active, Right)) Left.Focus();
        };
        Window.TitlebarSecondaryTabs.Visible(false);
        Window.TitlebarLeading.SetText("Navigation").SetAutomationId("navigation-toggle")
            .Help("Show or collapse navigation");
        Window.TitlebarLeading.SetStyle(ExplorerStyles.IconButton);
        Window.TitlebarLeading.Click += Sidebar.Toggle;
        Window.SetTitlebarLayout(Left.Root, Right.Root);
        Commands = CreateCommands();
        Transfers = new(this);
        Transfers.Bind(Left);
        Transfers.Bind(Right);
        Window.KeyHandler = HandleKey;
        Window.NavigationHandler = HandleNavigation;
        Sidebar.Refresh();
        UpdateTitle();
    }

    public Window Window { get; }
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
    internal Label Notification => notification;
    internal int FileOpenCount { get; private set; }
    internal string? NewWindowPath { get; private set; }
    internal bool CloseRequested { get; private set; }

    public void Run()
    {
        Left.Navigate(Left.Model.Active.Path);
        Task? smokeTask = smoke ? ExplorerSmoke.Start(this) : null;
        try { Window.Run(); }
        finally
        {
            Work.Dispose();
            smokeTask?.GetAwaiter().GetResult();
        }
    }

    public void Activate(FilePaneView pane)
    {
        if (ReferenceEquals(active, pane)) return;
        Preview?.Dismiss();
        active = pane;
        Sidebar.Refresh();
        UpdateTitle();
    }

    public void LocationChanged(FilePaneView pane, bool recordRecent = true)
    {
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

    private void UpdateTitle() => Window.SetTitle($"XUI / Files - {Active.Model.Active.Path}");

    public void ToggleSplit()
    {
        splitOpen = !splitOpen;
        split.SecondVisible = splitOpen;
        Window.TitlebarSecondaryTabs.Visible(splitOpen);
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
            split.SecondVisible = true;
            Window.TitlebarSecondaryTabs.Visible(true);
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
        Window.TitlebarSecondaryTabs.Visible(false);
        Left.Focus();
    }

    public void NewWindow(string path)
    {
        NewWindowPath = path;
        if (smoke) return;
        try
        {
            string executable = Environment.ProcessPath ??
                throw new InvalidOperationException("The explorer executable path is unavailable.");
            var start = new ProcessStartInfo(executable) { UseShellExecute = false };
            if (string.Equals(Path.GetFileNameWithoutExtension(executable), "dotnet", StringComparison.OrdinalIgnoreCase))
                start.ArgumentList.Add(Environment.GetCommandLineArgs()[0]);
            start.ArgumentList.Add(path);
            using var process = Process.Start(start) ??
                throw new InvalidOperationException("The explorer process did not start.");
        }
        catch (Exception error) when (error is Win32Exception or InvalidOperationException)
        {
            Report($"Cannot open another explorer window: {error.Message}");
        }
    }

    public FilePaneView? OtherPane(FilePaneView from, bool show)
    {
        if (show && !splitOpen)
        {
            splitOpen = true;
            split.SecondVisible = true;
            Window.TitlebarSecondaryTabs.Visible(true);
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
        light = !light;
        Window.SetTheme(light ? Theme.Light : Theme.Dark);
    }

    private IReadOnlyList<ExplorerCommand> CreateCommands() =>
    [
        new("New tab", "Ctrl+T", () => Active.NewTab()),
        new("Close tab", "Ctrl+W", () => Active.CloseTab()),
        new("Duplicate tab", "", () => Active.DuplicateTab(Active.Model.Active),
            () => Active.Model.Tabs.Count < ExplorerPane.TabLimit),
        new("Duplicate tab to new window", "Ctrl+N", () => NewWindow(Active.Model.Active.Path)),
        new("Duplicate in new pane", "", () => DuplicateInNewPane(Active, Active.Model.Active)),
        new("Close all tabs", "Ctrl+Shift+W", () => ClosePane(Active)),
        new("Next tab", "Ctrl+Tab", () => Active.CycleTab(1)),
        new("Previous tab", "Ctrl+Shift+Tab", () => Active.CycleTab(-1)),
        new("Toggle split panes", "Ctrl+\\", ToggleSplit),
        new("Focus other pane", "F6", () => OtherPane(Active, show: true)?.Focus()),
        new("Go to folder", "Ctrl+L", () => Palettes.ShowNavigation(Active)),
        new("Back", "Alt+Left", () => Active.MoveHistory(-1), () => Active.Model.Active.CanBack),
        new("Forward", "Alt+Right", () => Active.MoveHistory(1), () => Active.Model.Active.CanForward),
        new("Up to parent folder", "Alt+Up", () => Active.Up()),
        new("Refresh folder", "F5", () => Active.Refresh()),
        new("Preview selected item", "Space", () => Preview.ShowSelected(Active),
            () => Preview.CanPreview(Active)),
        new("Copy files", "Ctrl+C", () => Transfers.Copy(Active, cut: false),
            () => Active.HasSelection && !Transfers.Busy),
        new("Cut files", "Ctrl+X", () => Transfers.Copy(Active, cut: true),
            () => Active.HasSelection && !Transfers.Busy),
        new("Paste files into this folder", "Ctrl+V", () => Transfers.Paste(Active),
            () => Active.HasCurrentRows && !Transfers.Busy),
        new("Copy file paths", "Ctrl+Shift+C", () => Transfers.CopyPaths(Active),
            () => Active.HasSelection && !Transfers.Busy),
        new("Use Details view", "", () => Active.SetViewMode(ExplorerViewMode.Details)),
        new("Use Columns view", "", () => Active.SetViewMode(ExplorerViewMode.Columns)),
        new("Find in this folder", "Ctrl+F", () => Active.ShowFind()),
        new("Clear folder filter", "Escape", () => Active.HideFind()),
        new("Filter navigation", "Alt+F", Sidebar.FocusFilter),
        new("Toggle navigation pane", "", Sidebar.Toggle),
        new("Add or remove folder bookmark", "Ctrl+D", Bookmark),
        new("Open selected folder in new tab", "", () => { if (Active.SelectedEntry is { IsDirectory: true } e) Active.NewTab(e.FullPath); },
            () => Active.SelectedEntry is { IsDirectory: true }),
        new("Open selected item in other pane", "Ctrl+Enter", () =>
            { if (Active.SelectedEntry is { } e && OtherPane(Active, show: true) is { } target) Open(e, target); },
            () => Active.SelectedEntry is not null),
        new("Toggle light / dark theme", "Ctrl+F6", ToggleTheme),
        new("Close window", "Alt+F4", Window.Close)
    ];

    private bool HandleNavigation(UiNavigationEvent navigation)
    {
        if (Palettes.IsOpen || Preview.IsOpen) return true;
        var pane = Active;
        if (navigation.Position is { } point)
        {
            if (split.Expanded && (Contains(Right.Root.GetBounds(), point) || Contains(Right.Tabs.GetBounds(), point))) pane = Right;
            else if (Contains(Left.Root.GetBounds(), point) || Contains(Left.Tabs.GetBounds(), point)) pane = Left;
        }
        if (ReferenceEquals(pane, Right) && !split.Expanded) pane = Left;
        Activate(pane);
        pane.MoveHistory(navigation.Direction == NavigationDirection.Back ? -1 : 1);
        return true;

        static bool Contains(ElementBounds bounds, NavigationPoint point) =>
            point.X >= bounds.X && point.X < bounds.X + bounds.Width &&
            point.Y >= bounds.Y && point.Y < bounds.Y + bounds.Height;
    }

    private bool HandleKey(UiKeyEvent key)
    {
        uint vk = key.VirtualKey;
        var modifiers = key.Modifiers;
        if (Preview.IsOpen) return Preview.HandleKey(key);
        if (Palettes.HandleKey(vk, modifiers)) return true;
        if (modifiers == (KeyModifiers.Control | KeyModifiers.Shift))
        {
            switch (vk)
            {
                case 0x21: Active.MoveTab(Active.Model.Active.Id, -1); return true;
                case 0x22: Active.MoveTab(Active.Model.Active.Id, 1); return true;
                case 0x57: ClosePane(Active); return true;
            }
        }
        if (Active.HandleFindKey(key)) return true;
        if (Active.FilesFocused)
        {
            if (modifiers == KeyModifiers.None && vk == 0x20 && Preview.CanPreview(Active))
            {
                Preview.ShowSelected(Active); return true;
            }
            if (modifiers == (KeyModifiers.Control | KeyModifiers.Shift) && vk == 0x43)
            {
                Transfers.CopyPaths(Active); return true;
            }
            if (modifiers == KeyModifiers.Control)
            {
                switch (vk)
                {
                    case 0x43:
                    case 0x2d: Transfers.Copy(Active, cut: false); return true;
                    case 0x58: Transfers.Copy(Active, cut: true); return true;
                    case 0x56: Transfers.Paste(Active); return true;
                }
            }
            if (modifiers == KeyModifiers.Shift)
            {
                switch (vk)
                {
                    case 0x2d: Transfers.Paste(Active); return true;
                }
            }
        }
        if (modifiers == (KeyModifiers.Control | KeyModifiers.Shift) && vk == 0x50)
        {
            Palettes.ShowCommands(); return true;
        }
        if (modifiers == KeyModifiers.Control)
        {
            switch (vk)
            {
                case 0x4c: Palettes.ShowNavigation(Active); return true;
                case 0x46: Active.ShowFind(); return true;
                case 0x54: Active.NewTab(); return true;
                case 0x57: Active.CloseTab(); return true;
                case 0x73: Active.CloseTab(); return true;
                case 0x4e: NewWindow(Active.Model.Active.Path); return true;
                case 0x44: Bookmark(); return true;
                case 0x09: Active.CycleTab(1); return true;
                case 0xdc: ToggleSplit(); return true;
                case 0x75: ToggleTheme(); return true;
                case 0x0d when Active.FilesFocused:
                    if (Active.SelectedEntry is { } entry && OtherPane(Active, show: true) is { } target) Open(entry, target);
                    return true;
            }
        }
        if (modifiers == (KeyModifiers.Control | KeyModifiers.Shift) && vk == 0x09)
        {
            Active.CycleTab(-1); return true;
        }
        if (modifiers == KeyModifiers.Alt)
        {
            switch (vk)
            {
                case 0x46: Sidebar.FocusFilter(); return true;
                case 0x25: Active.MoveHistory(-1); return true;
                case 0x27: Active.MoveHistory(1); return true;
                case 0x26: Active.Up(); return true;
            }
        }
        if (modifiers == KeyModifiers.None)
        {
            switch (vk)
            {
                case 0x74: Active.Refresh(); return true;
                case 0x75: OtherPane(Active, show: true)?.Focus(); return true;
                case 0x1b when Active.Model.Active.FindOpen: Active.HideFind(); return true;
            }
        }
        return false;
    }

    public void Dispose()
    {
        Left.Cancel();
        Right.Cancel();
        Preview.Dispose();
        Work.Dispose();
        Left.DisposeSources();
        Right.DisposeSources();
        Window.Dispose();
    }
}
