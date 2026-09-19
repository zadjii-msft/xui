using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class NavigationSidebar : IDisposable
{
    private readonly ExplorerApplication app;
    private readonly SidebarLayout layout;
    private readonly Dictionary<string, ulong> identities = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<ulong, string> paths = [];
    private readonly Dictionary<ulong, ExplorerCommand> actionCommands = [];
    private ulong nextId = 1;
    private bool updating;
    private CancellationTokenSource? hover;
    private ulong hoveredId;
    private FileContextMenu? contextMenu;

    public NavigationSidebar(ExplorerApplication app)
    {
        this.app = app;
        layout = new SidebarLayout(app.Window, attach: false);
        View = layout.Navigation;
        View.Search.SetControlStyle(ExplorerStyles.NavigationFilter);
        View.SetHoverDelay(1000);
        BindContextMenu();
        foreach (var items in new[] { View.Items, View.HeaderItems, View.FooterItems })
            items.SetControlStyle(ExplorerStyles.NavigationItems);
        View.Event += e =>
        {
            if (!updating && e.Kind == EventKind.Click && actionCommands.TryGetValue(e.Value, out var command))
            {
                app.ExecuteCommand(command);
                return;
            }
            if (e.Kind == EventKind.Preview) HoverChanged(e.Value);
            else if (e.Kind == EventKind.Request) LoadHover(e.Value);
            if (!updating && e.Kind is EventKind.Selection or EventKind.Click && paths.TryGetValue(e.Value, out string? path))
                app.Active.Navigate(path);
        };
    }

    public NavigationView View { get; }
    public Reveal Presentation => layout.Root;
    public bool IsOpen { get; private set; } = true;
    public void Dispose() => CancelHover();

    internal void BindContextMenu() => View.OnContextMenu(GetContextCommands, InvokeContextCommand,
        GetContextShellPaths, ShellMenuPresentation.Xui);

    internal string[] GetContextShellPaths() => contextMenu?.GetShellPaths() ?? [];

    internal void InvokeContextCommand(ulong id)
    {
        if (contextMenu is { } menu) menu.Invoke(id);
        else app.Report("The navigation menu is no longer available.");
    }

    internal Command[] GetContextCommands(ulong id)
    {
        CancelHover();
        contextMenu = null;
        if (!paths.TryGetValue(id, out string? path)) return [];
        contextMenu = new(app, app.Active);
        return contextMenu.GetCommands([new(path, FolderName(path), true, 0, DateTime.MinValue)]);
    }

    public void Toggle()
    {
        var settings = app.State.Customization.Clone();
        settings.ShowSidebar = !IsOpen;
        try { app.SetCustomization(settings); }
        catch (Exception error) when (UiWork.IsExpected(error)) { app.Report(error.Message); }
    }
    public void FocusFilter()
    {
        if (!IsOpen) Toggle();
        View.Search.Focus(selectAll: true);
    }

    public void Refresh()
    {
        CancelHover();
        var items = new List<NavigationEntry>();
        var headers = new Dictionary<ulong, string>();
        paths.Clear();
        actionCommands.Clear();
        ulong recents = Header("Recents", ButtonIcon.History);
        foreach (string path in app.State.Recents.Distinct(StringComparer.OrdinalIgnoreCase)) AddPath(path, recents, "recent");
        if (app.State.Recents.Count == 0) Empty("No recent folders", recents);
        ulong bookmarks = Header("Bookmarks", ButtonIcon.Bookmark);
        var saved = app.State.Bookmarks.Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        foreach (string path in saved.Take(3800)) AddPath(path, bookmarks, "bookmark");
        if (saved.Length > 3800) Empty($"{saved.Length - 3800} more bookmarks in state.json", bookmarks);
        if (app.State.Bookmarks.Count == 0) Empty("Bookmark this folder with Ctrl+D", bookmarks);
        ulong storage = Header("Storage", ButtonIcon.Drive);
        foreach (var drive in DriveInfo.GetDrives()) AddPath(drive.Name, storage, "drive");
        ulong places = Header("Places", ButtonIcon.Home);
        foreach (var (name, path) in Places())
            if (!string.IsNullOrEmpty(path)) AddPath(path, places, "place", name);

        ulong tree = Header("Current folder", ButtonIcon.Folder);
        var current = app.Active.Model.Active;
        var ancestors = new List<string>();
        string? ancestor = current.Path;
        while (ancestor is not null)
        {
            ancestors.Add(ancestor);
            ancestor = Directory.GetParent(ancestor)?.FullName;
        }
        ancestors.Reverse();
        ulong parent = tree;
        // NavigationView supports 64 levels. Keep the current end of exceptionally deep paths.
        foreach (string path in ancestors.TakeLast(50)) parent = AddPath(path, parent, "tree");
        var children = current.Entries.Where(e => e.IsDirectory).OrderBy(e => e.Name, StringComparer.OrdinalIgnoreCase).ToArray();
        int available = Math.Max(0, 4000 - items.Count);
        foreach (var child in children.Take(available)) AddPath(child.FullPath, parent, "tree", child.Name);
        if (children.Length > available) Empty("More folders are available in the file pane", parent);

        var byId = items.ToDictionary(item => item.Id);
        ulong Root(NavigationEntry item)
        {
            while (item.Parent != 0) item = byId[item.Parent];
            return item.Id;
        }
        var groups = items.GroupBy(Root).ToDictionary(group => group.Key, group => group.ToArray());
        var ordered = new List<NavigationEntry>();
        foreach (string id in app.State.Customization.SidebarCommands)
        {
            var command = app.Commands.FirstOrDefault(command => command.StableId == id);
            if (command is null) continue;
            ulong key = Identify("command:" + id);
            actionCommands[key] = command;
            ordered.Add(new(key, command.Name, Keywords: app.ShortcutHint(command), Enabled: command.Enabled, Icon: ButtonIcon.More));
        }
        foreach (string section in app.State.Customization.SidebarSections)
        {
            var header = headers.FirstOrDefault(pair => pair.Value == section);
            if (header.Key != 0) ordered.AddRange(groups[header.Key]);
        }
        updating = true;
        try { View.SetItems(ordered.ToArray()); }
        finally { updating = false; }
        return;

        ulong Header(string text, ButtonIcon icon)
        {
            ulong id = Identify($"section:{text}");
            headers[id] = text switch { "Current folder" => "tree", _ => text.ToLowerInvariant() };
            items.Add(new(id, text, Selectable: false, Icon: icon));
            return id;
        }

        void Empty(string text, ulong owner) => items.Add(new(Identify($"empty:{owner}"), text, owner, Selectable: false));

        ulong AddPath(string path, ulong owner, string category, string? name = null)
        {
            ulong id = Identify($"{category}:{path}");
            paths[id] = path;
            string label = name ?? Path.GetFileName(Path.TrimEndingDirectorySeparator(path));
            if (string.IsNullOrEmpty(label)) label = path;
            items.Add(new(id, label, owner, Keywords: path, Icon: ButtonIcon.Folder, ImagePath: path));
            return id;
        }
    }

    internal void ApplyCustomization()
    {
        CancelHover();
        bool wasOpen = IsOpen;
        IsOpen = app.State.Customization.ShowSidebar;
        layout.NavigationOpen = IsOpen;
        Presentation.Duration = app.State.Customization.Animations ? 180u : 0u;
        View.Search.SetControlStyle(ExplorerPresentation.NavigationFilter(app.State.Customization));
        foreach (var items in new[] { View.Items, View.HeaderItems, View.FooterItems })
            items.SetControlStyle(ExplorerPresentation.NavigationItems(app.State.Customization));
        if (wasOpen && !IsOpen) app.Active.Focus();
        Refresh();
    }

    private void CancelHover()
    {
        hover?.Cancel();
        hover?.Dispose();
        hover = null;
        hoveredId = 0;
    }

    private void HoverChanged(ulong id)
    {
        CancelHover();
        if (!paths.TryGetValue(id, out string? path))
        {
            if (id != 0) View.SetHoverHelp(id, "");
            return;
        }
        hoveredId = id;
        View.SetHoverHelp(id, $"{FolderName(path)}\n{path}\n\nReading folder details...");
    }

    private void LoadHover(ulong id)
    {
        if (id != hoveredId || hover is not null || !paths.TryGetValue(id, out string? path)) return;
        hover = new();
        app.Work.Start(token => FolderMetadataReader.ReadAsync(path, token), hover.Token,
            metadata => { if (hoveredId == id) View.SetHoverHelp(id, metadata.HelpText()); },
            error => { if (hoveredId == id) View.SetHoverHelp(id, $"{FolderName(path)}\n{path}\n\nFolder details unavailable: {error.Message}"); });
    }

    private static string FolderName(string path)
    {
        string name = Path.GetFileName(Path.TrimEndingDirectorySeparator(path));
        return string.IsNullOrEmpty(name) ? path : name;
    }

    private ulong Identify(string value)
    {
        if (!identities.TryGetValue(value, out ulong id)) identities.Add(value, id = nextId++);
        return id;
    }

    private static IEnumerable<(string Name, string Path)> Places()
    {
        string home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        yield return ("Home", home);
        yield return ("Desktop", Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory));
        yield return ("Documents", Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments));
        yield return ("Downloads", Path.Combine(home, "Downloads"));
        yield return ("Pictures", Environment.GetFolderPath(Environment.SpecialFolder.MyPictures));
        yield return ("Music", Environment.GetFolderPath(Environment.SpecialFolder.MyMusic));
        yield return ("Videos", Environment.GetFolderPath(Environment.SpecialFolder.MyVideos));
    }
}
