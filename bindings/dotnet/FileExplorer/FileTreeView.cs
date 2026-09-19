using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class FileTreeView
{
    private readonly ExplorerApplication app;
    private readonly Func<string, ulong> identify;
    private readonly Dictionary<ulong, FileEntry> entries = [];
    private readonly Dictionary<TreeRequest, string> requests = [];
    private CancellationTokenSource loading = new();
    private string query = "";
    private int sort;
    private bool descending;
    private ExplorerPartition partition;
    private string? restorePath;
    private string? restoreFolder;
    private double restoreOffset;

    public FileTreeView(ExplorerApplication app, int number, Func<string, ulong> identify)
    {
        this.app = app;
        this.identify = identify;
        View = app.Window.TreeView($"Tree in pane {number}");
        View.SetAutomationId($"pane-{number}-tree");
        View.SetControlStyle(ExplorerStyles.FileTree);
        View.SetColumns(FileRows.Columns);
        View.OnRequest(LoadChildren);
    }

    public TreeView View { get; }
    public bool Restoring { get; private set; }
    public bool Updating { get; private set; }
    internal int PendingCount => requests.Count;
    public FileEntry? Entry(ItemKey key) => entries.GetValueOrDefault(key.Id);
    public FileEntry[] SelectedEntries => entries.Where(pair =>
        View.Contains(Key(pair.Key, pair.Value))).Select(pair => pair.Value).ToArray();
    public void StopRestoring() => Restoring = false;

    private static ItemKey Key(ulong id, FileEntry entry) => new(id, entry.IsDirectory ? 1UL : 0);

    public void SetRows(IReadOnlyList<FileEntry> roots, ExplorerTab owner)
    {
        Cancel();
        entries.Clear();
        query = owner.Filter;
        sort = owner.SortColumn;
        descending = owner.SortDescending;
        partition = owner.Partition;
        restorePath = owner.SelectedPath;
        restoreFolder = null;
        restoreOffset = owner.ScrollOffset;
        Restoring = true;
        using var source = app.Window.ImmutableSource(Rows(roots));
        View.SetSource(source);
        Restore(roots);
    }

    private FileRows Rows(IReadOnlyList<FileEntry> items)
    {
        foreach (var entry in items) entries[identify(entry.FullPath)] = entry;
        return new(items, identify, tree: true);
    }

    public void SelectPath(string path)
    {
        var entry = entries.FirstOrDefault(pair => string.Equals(pair.Value.FullPath, path, StringComparison.OrdinalIgnoreCase));
        if (entry.Value is not null) View.Select(Key(entry.Key, entry.Value));
    }

    private void Restore(IReadOnlyList<FileEntry> items)
    {
        if (!Restoring) return;
        var selected = items.FirstOrDefault(entry =>
            string.Equals(entry.FullPath, restorePath, StringComparison.OrdinalIgnoreCase));
        if (selected is not null)
        {
            Updating = true;
            try { View.Select(Key(identify(selected.FullPath), selected)); }
            finally { Updating = false; }
        }
        else if (restorePath is not null && items.FirstOrDefault(entry => entry.IsDirectory &&
            restorePath.StartsWith(Path.TrimEndingDirectorySeparator(entry.FullPath) + Path.DirectorySeparatorChar,
                StringComparison.OrdinalIgnoreCase)) is { } ancestor)
        {
            restoreFolder = ancestor.FullPath;
            View.Expand(Key(identify(ancestor.FullPath), ancestor));
            if (requests.Count != 0) return;
        }
        Restoring = false;
        View.Offset = restoreOffset;
    }

    private void LoadChildren(TreeRequest request)
    {
        var key = request.Node;
        if (Entry(key) is not { IsDirectory: true } folder)
        {
            using (request)
            using (var empty = app.Window.ImmutableSource(new FileRows([], identify, tree: true)))
                request.Complete(empty, "This folder is no longer available.");
            return;
        }
        requests.Add(request, folder.FullPath);
        string filter = query;
        int column = sort;
        bool reverse = descending;
        var order = partition;
        app.Work.Start(async token =>
        {
            var snapshot = await app.Files.ReadDirectoryAsync(folder.FullPath, folder.FullPath, token).ConfigureAwait(false);
            token.ThrowIfCancellationRequested();
            return FileSystemService.FilterAndSort(snapshot.Entries, filter, column, reverse, order);
        }, loading.Token, children => Complete(request, children, ""), failure =>
            Complete(request, [], $"Cannot open {folder.FullPath}: {failure.Message}"));
    }

    private void Complete(TreeRequest request, IReadOnlyList<FileEntry> children, string error)
    {
        bool restore = requests.Remove(request, out string? folder) && folder == restoreFolder;
        using (request)
        {
            try
            {
                // A collapsed branch can finish after the native request was canceled.
                _ = request.Node;
                using var source = app.Window.ImmutableSource(new FileRows(children, identify, tree: true));
                Updating = true;
                try { request.Complete(source, error); }
                finally { Updating = false; }
            }
            catch (XuiException failure) when (failure.Status == 11)
            {
                if (restore) Restoring = false;
                return;
            }
        }
        foreach (var entry in children) entries[identify(entry.FullPath)] = entry;
        if (error.Length != 0)
        {
            if (restore) Restoring = false;
            app.Report(error);
        }
        else if (restore) Restore(children);
    }

    public void Cancel()
    {
        loading.Cancel();
        loading.Dispose();
        loading = new();
        Updating = true;
        try { foreach (var request in requests.Keys) request.Dispose(); }
        finally { Updating = false; }
        requests.Clear();
        Restoring = false;
    }

    public void Clear()
    {
        Cancel();
        entries.Clear();
        using var source = app.Window.ImmutableSource(new FileRows([], identify, tree: true));
        View.SetSource(source);
    }

    public void Dispose()
    {
        Cancel();
        loading.Dispose();
    }
}
