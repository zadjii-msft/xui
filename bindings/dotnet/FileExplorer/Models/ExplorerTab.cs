namespace Xui.FileExplorer.Models;

public enum ExplorerViewMode { Details, Columns, List, Tree, MediumIcons, LargeIcons, ExtraLargeIcons }
public enum ExplorerPartition { FoldersFirst, FilesFirst, Mixed }

public sealed class ExplorerColumn(DirectorySnapshot snapshot)
{
    public DirectorySnapshot Snapshot { get; } = snapshot;
    public string Filter { get; set; } = "";
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
    public ExplorerPartition Partition { get; private set; }
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
            ActiveColumn = ActiveColumn, Entries = Entries, historyIndex = historyIndex, Partition = Partition
        };
        copy.history.AddRange(history);
        copy.columns.AddRange(columns.Select(column => new ExplorerColumn(column.Snapshot)
        {
            Filter = column.Filter, SelectedPath = column.SelectedPath, ScrollOffset = column.ScrollOffset
        }));
        return copy;
    }

    public void SetViewMode(ExplorerViewMode mode)
    {
        if (!Enum.IsDefined(mode)) throw new ArgumentOutOfRangeException(nameof(mode));
        if (ViewMode == mode) return;
        if (ViewMode == ExplorerViewMode.Columns && columns.Count > 0) Filter = columns[^1].Filter;
        ViewMode = mode;
        ResetColumns();
    }

    public void SetPartition(ExplorerPartition partition)
    {
        if (!Enum.IsDefined(partition)) throw new ArgumentOutOfRangeException(nameof(partition));
        Partition = partition;
    }

    private void ResetColumns()
    {
        columns.Clear();
        ActiveColumn = 0;
        if (ViewMode == ExplorerViewMode.Columns)
            columns.Add(new(new(Path, Entries)) { Filter = Filter, SelectedPath = SelectedPath, ScrollOffset = ScrollOffset });
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
        if (sameDirectory && ViewMode == ExplorerViewMode.Columns && columns.Count > 0)
            Filter = columns[^1].Filter;
        Path = path;
        Entries = entries;
        if (!sameDirectory)
        {
            Filter = "";
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
    private static long nextId;
    private ExplorerTab? active;

    internal ExplorerPane(IEnumerable<ExplorerTab> tabs, ulong activeId)
    {
        var items = tabs.ToArray();
        if (items.Length > TabLimit || items.Select(tab => tab.Id).Distinct().Count() != items.Length)
            throw new ArgumentException("The pane contains too many tabs or duplicate tab identities.", nameof(tabs));
        active = items.SingleOrDefault(tab => tab.Id == activeId);
        if (items.Length != 0 && active is null)
            throw new ArgumentException("The active tab is not in the pane.", nameof(activeId));
        Tabs.AddRange(items);
    }

    public ExplorerPane(string path)
    {
        Active = AddTab(path);
    }

    public List<ExplorerTab> Tabs { get; } = [];
    public ExplorerTab Active
    {
        get => active ?? throw new InvalidOperationException("The pane has no tabs.");
        private set => active = value;
    }

    private static ulong AllocateId()
    {
        long id = Interlocked.Increment(ref nextId);
        if (id <= 0) throw new InvalidOperationException("Tab identities are exhausted.");
        return (ulong)id;
    }

    public ExplorerTab AddTab(string path)
    {
        if (Tabs.Count >= TabLimit)
            throw new InvalidOperationException($"A pane can contain at most {TabLimit} tabs.");
        var tab = new ExplorerTab(AllocateId(), path);
        tab.SetPartition(active?.Partition ?? ExplorerPartition.FoldersFirst);
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
        var copy = source.Duplicate(AllocateId());
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

    public bool CanTransferTab(ulong id, ExplorerPane target, int index)
    {
        ArgumentNullException.ThrowIfNull(target);
        return index >= 0 && index <= target.Tabs.Count && Tabs.Any(tab => tab.Id == id)
            && (ReferenceEquals(this, target) ||
                (target.Tabs.Count < TabLimit && target.Tabs.All(tab => tab.Id != id)));
    }

    // The native strip reports an insertion slot before the source is removed.
    public bool TransferTab(ulong id, ExplorerPane target, int index)
    {
        if (!CanTransferTab(id, target, index)) return false;
        int sourceIndex = Tabs.FindIndex(tab => tab.Id == id);
        var tab = Tabs[sourceIndex];
        if (ReferenceEquals(this, target) && index > sourceIndex) --index;
        Tabs.RemoveAt(sourceIndex);
        if (ReferenceEquals(active, tab))
            active = Tabs.Count == 0 ? null : Tabs[Math.Min(sourceIndex, Tabs.Count - 1)];
        target.Tabs.Insert(index, tab);
        target.active = tab;
        return true;
    }

    public void ReplaceTabs(ExplorerPane source)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (ReferenceEquals(this, source)) throw new ArgumentException("The source pane must be different.", nameof(source));
        int active = source.Tabs.IndexOf(source.Active);
        var copies = source.Tabs.Select(tab => tab.Duplicate(AllocateId())).ToArray();
        Tabs.Clear();
        Tabs.AddRange(copies);
        Active = Tabs[active];
    }

    public void ResetTabs(string path, ExplorerPartition partition = ExplorerPartition.FoldersFirst)
    {
        var tab = new ExplorerTab(AllocateId(), path);
        tab.SetPartition(partition);
        Tabs.Clear();
        Tabs.Add(tab);
        Active = tab;
    }

    public void ResetTabs(ExplorerTab source)
    {
        ArgumentNullException.ThrowIfNull(source);
        var tab = source.Duplicate(AllocateId());
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
