namespace Xui.FileExplorer.Models;

public sealed class ExplorerTab
{
    public const int HistoryLimit = 128;
    private readonly List<string> history = [];
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
    public IReadOnlyList<FileEntry> Entries { get; private set; } = Array.Empty<FileEntry>();
    public bool CanBack => historyIndex > 0;
    public bool CanForward => historyIndex >= 0 && historyIndex < history.Count - 1;

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
