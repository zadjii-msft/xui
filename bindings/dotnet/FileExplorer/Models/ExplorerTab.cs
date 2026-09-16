namespace Xui.FileExplorer.Models;

public enum ExplorerViewMode { Details, Columns }

public sealed class ExplorerColumn(DirectorySnapshot snapshot)
{
    public DirectorySnapshot Snapshot { get; } = snapshot;
    public string? SelectedPath { get; set; }
    public double ScrollOffset { get; set; }
}

public sealed class ExplorerTab
{
    public const int HistoryLimit = 128;
    public const int ColumnLimit = 32;
    private readonly List<string> history = [];
    private readonly List<ExplorerColumn> columns = [];
    private int historyIndex = -1;

    public ExplorerTab(ulong id, string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        _ = NormalizePath(path);
        Id = id;
        Path = path;
    }

    public ulong Id { get; }
    public string Path { get; private set; }
    public string Filter { get; set; } = "";
    public bool FindOpen { get; set; }
    public int SortColumn { get; set; }
    public bool SortDescending { get; set; }
    public string? SelectedPath { get; set; }
    public double ScrollOffset { get; set; }
    public ExplorerViewMode ViewMode { get; private set; }
    public IReadOnlyList<ExplorerColumn> Columns => columns;
    public int ActiveColumn { get; set; }
    public IReadOnlyList<FileEntry> Entries { get; private set; } = Array.Empty<FileEntry>();
    public bool HasSnapshot => historyIndex >= 0;
    public bool CanBack => historyIndex > 0;
    public bool CanForward => historyIndex >= 0 && historyIndex < history.Count - 1;

    internal ExplorerTab Duplicate(ulong id)
    {
        var copy = new ExplorerTab(id, Path)
        {
            Filter = Filter, FindOpen = FindOpen, SortColumn = SortColumn, SortDescending = SortDescending,
            SelectedPath = SelectedPath, ScrollOffset = ScrollOffset, ViewMode = ViewMode,
            ActiveColumn = ActiveColumn, Entries = Entries, historyIndex = historyIndex
        };
        copy.history.AddRange(history);
        copy.columns.AddRange(columns.Select(column => new ExplorerColumn(column.Snapshot)
        {
            SelectedPath = column.SelectedPath, ScrollOffset = column.ScrollOffset
        }));
        return copy;
    }

    public void SetViewMode(ExplorerViewMode mode)
    {
        if (!Enum.IsDefined(mode)) throw new ArgumentOutOfRangeException(nameof(mode));
        if (ViewMode == mode) return;
        ViewMode = mode;
        ResetColumns();
    }

    private void ResetColumns()
    {
        columns.Clear();
        ActiveColumn = 0;
        if (ViewMode == ExplorerViewMode.Columns)
            columns.Add(new(new(Path, Entries)) { SelectedPath = SelectedPath, ScrollOffset = ScrollOffset });
    }

    public void CommitColumn(int parent, DirectorySnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        ArgumentException.ThrowIfNullOrWhiteSpace(snapshot.Path);
        if (ViewMode != ExplorerViewMode.Columns || parent < 0 || parent >= columns.Count)
            throw new ArgumentOutOfRangeException(nameof(parent));
        if (parent + 1 >= ColumnLimit)
            throw new InvalidOperationException($"A path can contain at most {ColumnLimit} columns. Open the folder directly to start a new path.");
        if (!columns[parent].Snapshot.Entries.Any(e => e.IsDirectory && PathsEqual(e.FullPath, snapshot.Path)))
            throw new InvalidOperationException("The folder is not a child of this column.");
        var ancestors = columns.Take(parent + 1).ToArray();
        Commit(snapshot);
        columns.Clear();
        columns.AddRange(ancestors);
        columns[parent].SelectedPath = snapshot.Path;
        columns.Add(new(new(Path, Entries)));
        ActiveColumn = parent;
    }

    public void SelectColumnLeaf(int index, string? path)
    {
        if (ViewMode != ExplorerViewMode.Columns || index < 0 || index >= columns.Count)
            throw new ArgumentOutOfRangeException(nameof(index));
        if (path is not null && !columns[index].Snapshot.Entries.Any(e => !e.IsDirectory && PathsEqual(e.FullPath, path)))
            throw new InvalidOperationException("The file is not a leaf in this column.");
        var retained = columns.Take(index + 1).ToArray();
        if (index != columns.Count - 1) Commit(retained[index].Snapshot);
        columns.Clear();
        columns.AddRange(retained);
        columns[index].SelectedPath = path;
        SelectedPath = path;
        ActiveColumn = index;
    }

    public void Commit(DirectorySnapshot snapshot)
    {
        var entries = ValidateAndCopy(snapshot);
        if (historyIndex < 0 || !PathsEqual(history[historyIndex], snapshot.Path))
        {
            if (historyIndex < history.Count - 1)
                history.RemoveRange(historyIndex + 1, history.Count - historyIndex - 1);
            history.Add(snapshot.Path);
            if (history.Count > HistoryLimit)
                history.RemoveAt(0);
            historyIndex = history.Count - 1;
        }
        Apply(snapshot.Path, entries);
    }

    public bool TryGetHistory(int delta, out string path)
    {
        var index = (long)historyIndex + delta;
        if (historyIndex < 0 || index < 0 || index >= history.Count)
        {
            path = "";
            return false;
        }
        path = history[(int)index];
        return true;
    }

    public void CommitHistory(DirectorySnapshot snapshot, int delta)
    {
        var entries = ValidateAndCopy(snapshot);
        if (!TryGetHistory(delta, out var target) ||
            !PathsEqual(target, snapshot.Path))
            throw new InvalidOperationException("The directory does not match the requested history entry.");
        historyIndex += delta;
        Apply(snapshot.Path, entries);
    }

    private static IReadOnlyList<FileEntry> ValidateAndCopy(DirectorySnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        ArgumentException.ThrowIfNullOrWhiteSpace(snapshot.Path);
        _ = NormalizePath(snapshot.Path);
        ArgumentNullException.ThrowIfNull(snapshot.Entries);
        if (snapshot.Entries.Any(entry => entry is null))
            throw new ArgumentException("Directory entries must not contain null.", nameof(snapshot));
        return Array.AsReadOnly(snapshot.Entries.ToArray());
    }

    private void Apply(string path, IReadOnlyList<FileEntry> entries)
    {
        var sameDirectory = PathsEqual(Path, path);
        Path = path;
        Entries = entries;
        if (!sameDirectory)
        {
            SelectedPath = null;
            ScrollOffset = 0;
        }
        ResetColumns();
    }

    private static bool PathsEqual(string left, string right)
        => StringComparer.OrdinalIgnoreCase.Equals(NormalizePath(left), NormalizePath(right));

    private static string NormalizePath(string path)
        => System.IO.Path.TrimEndingDirectorySeparator(System.IO.Path.GetFullPath(path));
}

public sealed class ExplorerPane
{
    public const int TabLimit = 32;
    private ulong nextId;

    public ExplorerPane(string path)
    {
        Active = AddTab(path);
    }

    public List<ExplorerTab> Tabs { get; } = [];
    public ExplorerTab Active { get; private set; }

    public ExplorerTab AddTab(string path)
    {
        if (Tabs.Count >= TabLimit)
            throw new InvalidOperationException($"A pane can contain at most {TabLimit} tabs.");
        var tab = new ExplorerTab(checked(++nextId), path);
        Tabs.Add(tab);
        Active = tab;
        return tab;
    }

    public bool SelectTab(ulong id)
    {
        var tab = Tabs.Find(tab => tab.Id == id);
        if (tab is null)
            return false;
        Active = tab;
        return true;
    }

    public ExplorerTab DuplicateTab(ExplorerTab source)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (Tabs.Count >= TabLimit)
            throw new InvalidOperationException($"A pane can contain at most {TabLimit} tabs.");
        var copy = source.Duplicate(checked(++nextId));
        int index = Tabs.IndexOf(source);
        Tabs.Insert(index < 0 ? Tabs.Count : index + 1, copy);
        return Active = copy;
    }

    public bool MoveTab(ulong id, int delta)
    {
        if (delta is not (-1 or 1)) throw new ArgumentOutOfRangeException(nameof(delta));
        int index = Tabs.FindIndex(tab => tab.Id == id);
        int destination = index + delta;
        if (index < 0 || destination < 0 || destination >= Tabs.Count) return false;
        (Tabs[index], Tabs[destination]) = (Tabs[destination], Tabs[index]);
        return true;
    }

    public void ReplaceTabs(ExplorerPane source)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (ReferenceEquals(this, source)) throw new ArgumentException("The source pane must be different.", nameof(source));
        int active = source.Tabs.IndexOf(source.Active);
        var copies = source.Tabs.Select(tab => tab.Duplicate(checked(++nextId))).ToArray();
        Tabs.Clear();
        Tabs.AddRange(copies);
        Active = Tabs[active];
    }

    public void ResetTabs(string path)
    {
        var tab = new ExplorerTab(checked(++nextId), path);
        Tabs.Clear();
        Tabs.Add(tab);
        Active = tab;
    }

    public void ResetTabs(ExplorerTab source)
    {
        ArgumentNullException.ThrowIfNull(source);
        var tab = source.Duplicate(checked(++nextId));
        Tabs.Clear();
        Tabs.Add(tab);
        Active = tab;
    }

    public bool CloseTab(ulong id)
    {
        var index = Tabs.FindIndex(tab => tab.Id == id);
        if (index < 0 || Tabs.Count <= 1)
            return false;
        var wasActive = ReferenceEquals(Tabs[index], Active);
        Tabs.RemoveAt(index);
        if (wasActive)
            Active = Tabs[Math.Min(index, Tabs.Count - 1)];
        return true;
    }

    public void CycleTab(int delta)
    {
        var index = Tabs.IndexOf(Active);
        var next = ((long)index + delta) % Tabs.Count;
        if (next < 0)
            next += Tabs.Count;
        Active = Tabs[(int)next];
    }
}
