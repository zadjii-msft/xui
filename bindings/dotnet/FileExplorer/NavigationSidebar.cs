using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class NavigationSidebar
{
    private readonly ExplorerApplication app;
    private readonly Dictionary<string, ulong> identities = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<ulong, string> paths = [];
    private ulong nextId = 1;
    private bool updating;

    public NavigationSidebar(ExplorerApplication app)
    {
        this.app = app;
        View = new SidebarLayout(app.Window, attach: false).Root;
        foreach (var items in new[] { View.Items, View.HeaderItems, View.FooterItems })
            items.SetControlStyle(ExplorerStyles.NavigationItems);
        View.Event += e =>
        {
            if (!updating && e.Kind is EventKind.Selection or EventKind.Click && paths.TryGetValue(e.Value, out string? path))
                app.Active.Navigate(path);
        };
    }

    public NavigationView View { get; }
    public bool IsOpen { get; private set; } = true;

    public void Toggle()
    {
        IsOpen = !IsOpen;
        View.Visible(IsOpen);
    }
    public void FocusFilter()
    {
        if (!IsOpen) Toggle();
        View.Search.Focus(selectAll: true);
    }

    public void Refresh()
    {
        var items = new List<NavigationEntry>();
        paths.Clear();
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

        updating = true;
        try { View.SetItems(items.ToArray()); }
        finally { updating = false; }
        return;

        ulong Header(string text, ButtonIcon icon)
        {
            ulong id = Identify($"section:{text}");
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
