using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class BreadcrumbAddressBar : IDisposable
{
    private readonly ExplorerApplication app;
    private readonly FilePaneView pane;
    private readonly ContentHost pathHost;
    private readonly Reveal pathPresentation;
    private readonly Popup folders;
    private readonly ItemsView folderList;
    private readonly Label message;
    private readonly List<(Button Name, Button Children, string Path)> buttons = [];
    private readonly string automationId;
    private CancellationTokenSource query = new();
    private FileRows rows = new([], _ => 0);
    private IReadOnlyList<BreadcrumbPart> parts = [];
    private bool disposed;
    private int revision;
    private string path = "";

    public BreadcrumbAddressBar(ExplorerApplication app, FilePaneView pane, int number)
    {
        this.app = app;
        this.pane = pane;
        var window = app.Window;
        string id = automationId = $"pane-{number}-address";
        pathHost = window.CreateContentHost();
        var layout = new BreadcrumbAddressLayout(window, id, pathHost, attach: false);
        Root = layout.Root.AutoSize(false);
        pathPresentation = layout.PathPresentation;
        Display = layout.Display;
        AncestorsButton = layout.Ancestors.SetStyle(ExplorerStyles.IconButton);
        AncestorsButton.Click += ShowAncestors;
        AncestorsButton.FocusEntered += pane.Activate;

        var menu = new BreadcrumbMenuLayout(window, id, attach: false);
        folderList = menu.Folders.ItemSize(280, 32).SetSingleClickActivation(true);
        message = menu.Message;
        folders = menu.Root;
        folders.Event += e =>
        {
            if (e.Kind == EventKind.Dismiss) CancelQuery();
        };
        folderList.Event += e =>
        {
            if (e.Kind == EventKind.Click && !Pending && rows.Entry(e.Value) is { } entry)
                Navigate(entry.FullPath);
        };
    }

    public Grid Root { get; }
    public Reveal Display { get; }
    public Button AncestorsButton { get; }
    internal Button TrailingSpace { get; private set; } = null!;
    internal bool MenuOpen => folders.IsOpen;
    internal bool Pending { get; private set; }
    internal int FolderCount => checked((int)rows.Count);
    internal string MenuMessage => message.Text;
    internal IReadOnlyList<(Button Name, Button Children, string Path)> Segments => buttons;

    public void SetPath(string value)
    {
        if (path == value) return;
        Cancel();
        path = value;
        parts = BreadcrumbPath.Create(path);
        Rebuild();
    }

    private void Rebuild()
    {
        revision++;
        buttons.Clear();
        var update = pathHost.BeginUpdate();
        try
        {
            update.Commit(BuildPath());
        }
        catch
        {
            update.Dispose();
            throw;
        }
    }

    private Element BuildPath()
    {
        int currentRevision = revision;
        void Dispatch(Action action)
        {
            // Retained candidates cannot replace themselves from a scoped callback.
            if (!app.Application.Post(() =>
                {
                    if (!disposed && !app.CloseRequested && currentRevision == revision) action();
                }) && !app.CloseRequested)
                throw new InvalidOperationException("Cannot dispatch an address-bar action.");
        }
        Element? tail = null;
        TrailingSpace = new BreadcrumbSpaceLayout(app.Window, automationId, attach: false).Root
            .SetStyle(ExplorerStyles.IconButton).AutoSize(false);
        TrailingSpace.FocusEntered += () => Dispatch(pane.Activate);
        TrailingSpace.Click += () => Dispatch(ShowNavigation);
        // Bound retained controls; the ancestor menu still contains the complete path.
        foreach (var part in parts.TakeLast(64).Reverse())
        {
            var segment = new BreadcrumbSegmentLayout(app.Window, automationId, parts.Count - 1 - buttons.Count,
                part.Name, part.Path, part == parts[^1], attach: false);
            var name = segment.Name.MaximumSize(200, 36);
            var children = segment.Children.SetStyle(ExplorerStyles.IconButton);
            name.FocusEntered += () => Dispatch(pane.Activate);
            children.FocusEntered += () => Dispatch(pane.Activate);
            name.Click += () => Dispatch(() =>
            {
                if (part == parts[^1]) ShowNavigation();
                else Navigate(part.Path);
            });
            children.Click += () => Dispatch(() => ShowChildren(part.Path));
            buttons.Insert(0, (name, children, part.Path));
            if (tail is null)
            {
                tail = segment.Root.Add(TrailingSpace, 1);
            }
            else
            {
                tail = app.Window.AdaptiveLayout("Collapsible path segment", segment.Root, tail)
                    .AutoSize(true).SetContentSized(true)
                    .SetCompactNavigation(CompactNavigation.Overlay).SetNavigationOpen(false);
                tail.SetControlStyle(ExplorerStyles.BreadcrumbLayout);
            }
        }
        return tail!;
    }

    public void ShowNavigation()
    {
        pane.Activate();
        DismissMenu();
        app.Palettes.ShowNavigation(pane);
    }

    private void Navigate(string target)
    {
        pane.Activate();
        DismissMenu();
        pane.Navigate(target);
        pane.Focus();
    }

    public void ShowAncestors()
    {
        OpenMenu(AncestorsButton);
        ShowRows(parts.Select(part => new FileEntry(part.Path, part.Name, true, 0, default)).ToArray(),
            "Choose a location in the current path.");
    }

    internal void ShowChildren(string parent)
    {
        OpenMenu(pathPresentation);
        Pending = true;
        ShowRows([], "Loading folders...");
        app.Work.Start(token => app.Files.ReadDirectoryAsync(parent, parent, token), query.Token, snapshot =>
        {
            Pending = false;
            var entries = snapshot.Entries.Where(entry => entry.IsDirectory).ToArray();
            ShowRows(entries, entries.Length == 0 ? "No subfolders." : "Choose a folder.");
        }, error =>
        {
            Pending = false;
            ShowRows([], $"Cannot list folders: {error.Message}");
            pane.Report($"Cannot list folders in {parent}: {error.Message}");
        });
    }

    private void OpenMenu(Control anchor)
    {
        pane.Activate();
        app.Palettes.Dismiss();
        DismissMenu();
        query.Dispose();
        query = new();
        folders.Show(anchor);
        folderList.Focus();
    }

    private void ShowRows(IReadOnlyList<FileEntry> entries, string status)
    {
        ulong id = 0;
        rows = new(entries, _ => ++id, suggestions: true);
        using var source = app.Window.ImmutableSource(rows);
        folderList.SetSource(source);
        folderList.Enabled = true;
        if (rows.Count != 0) folderList.Select(rows.Key(0));
        message.Text = status;
        message.Help(status);
    }

    private void CancelQuery()
    {
        query.Cancel();
        Pending = false;
    }

    public void DismissMenu()
    {
        CancelQuery();
        if (!app.CloseRequested && folders.IsOpen) folders.Dismiss();
    }

    public bool HandleKey(UiKeyEvent key)
    {
        if (folders.IsOpen)
        {
            if (key.VirtualKey == 0x1b)
            {
                DismissMenu();
                return true;
            }
            // Let the native list handle arrows and Enter, not pane navigation shortcuts.
            if (key.Modifiers == KeyModifiers.Alt || key.Modifiers == KeyModifiers.Control)
                DismissMenu();
        }
        if (key.Modifiers == KeyModifiers.None && key.VirtualKey is 0x25 or 0x27 or 0x28)
        {
            var visible = buttons.SelectMany(button => new[] { button.Name, button.Children })
                .Where(button => button.GetBounds().Width > 0).ToArray();
            int index = Array.FindIndex(visible, button => button.Id == key.TargetId);
            if (index >= 0 && key.VirtualKey is 0x25 or 0x27)
            {
                visible[Math.Clamp(index + (key.VirtualKey == 0x25 ? -1 : 1), 0, visible.Length - 1)].Focus();
                return true;
            }
            if (index >= 0 && key.VirtualKey == 0x28)
            {
                var segment = buttons.Single(button => button.Name.Id == key.TargetId || button.Children.Id == key.TargetId);
                ShowChildren(segment.Path);
                return true;
            }
        }
        return false;
    }

    public void Cancel() => DismissMenu();

    public void SetVisible(bool visible)
    {
        if (!visible) Cancel();
        Display.Visible(visible);
    }

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        query.Cancel();
        query.Dispose();
    }
}
