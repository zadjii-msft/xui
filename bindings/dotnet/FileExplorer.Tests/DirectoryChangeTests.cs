using Xui.FileExplorer.Models;

internal static class DirectoryChangeTests
{
    internal static async Task<int> Run(string fixture)
    {
        int assertions = 0;
        void Check(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
            assertions++;
        }
        string root = Path.Combine(fixture, "watch");
        string child = Path.Combine(root, "child");
        Directory.CreateDirectory(child);
        using var signals = new SemaphoreSlim(0);
        var failures = new System.Collections.Concurrent.ConcurrentQueue<Exception>();
        int deliveries = 0;
        using var monitor = new DirectoryChangeMonitor([root, root], true, failure =>
        {
            if (failure is not null) failures.Enqueue(failure);
            Interlocked.Increment(ref deliveries);
            signals.Release();
        });
        async Task Changed(Action action)
        {
            await Task.Delay(150);
            while (signals.Wait(0)) { }
            action();
            Check(await signals.WaitAsync(TimeSpan.FromSeconds(5)), "A filesystem mutation did not notify the directory monitor.");
            Check(failures.IsEmpty, "The directory monitor reported an unexpected failure.");
        }
        string file = Path.Combine(child, "item.txt");
        await Changed(() => File.WriteAllText(file, "one"));
        await Changed(() => File.AppendAllText(file, "two"));
        string renamed = Path.Combine(child, "renamed.txt");
        await Changed(() => File.Move(file, renamed));
        await Changed(() => File.Delete(renamed));
        await Changed(() => Directory.Delete(child));
        monitor.Dispose();
        await Task.Delay(200);
        int before = Volatile.Read(ref deliveries);
        File.WriteAllText(Path.Combine(root, "after-dispose.txt"), "no callback");
        await Task.Delay(250);
        Check(Volatile.Read(ref deliveries) == before, "A disposed monitor observed a new change.");
        try
        {
            using var missing = new DirectoryChangeMonitor([Path.Combine(root, "missing")], false, _ => { });
            throw new InvalidOperationException("A missing watch directory was silently accepted.");
        }
        catch (ArgumentException) { assertions++; }

        FileEntry Folder(string parent, string name) => new(Path.Combine(parent, name), name, true, 0, DateTime.UnixEpoch);
        var folder = Folder(root, "child");
        var leaf = new FileEntry(Path.Combine(child, "leaf.txt"), "leaf.txt", false, 1, DateTime.UnixEpoch);
        var tab = new ExplorerTab(1, root);
        tab.Commit(new(root, [folder]));
        tab.SetViewMode(ExplorerViewMode.Columns);
        tab.CommitColumn(0, new(child, [leaf]));
        var first = tab.Columns[0];
        var second = tab.Columns[1];
        first.Filter = "chi";
        second.Filter = "leaf";
        second.ScrollOffset = 120;
        second.SelectedPath = leaf.FullPath;
        tab.ActiveColumn = 1;
        tab.RefreshSnapshots([new(root, [folder]), new(child, [])]);
        Check(ReferenceEquals(first, tab.Columns[0]) && ReferenceEquals(second, tab.Columns[1]),
            "Automatic refresh replaced retained column models.");
        Check(first.Filter == "chi" && second.Filter == "leaf" && second.ScrollOffset == 120 && tab.ActiveColumn == 1,
            "Automatic refresh lost a column's query, offset, or active state.");
        Check(second.SelectedPath is null && tab.Entries.Count == 0, "Deleted column rows or selections survived refresh.");
        Check(tab.TryGetHistory(-1, out var previous) && previous == root, "Automatic refresh added a history entry.");
        tab.RefreshSnapshots([new(root, [])]);
        Check(tab.Columns.Count == 1 && tab.Path == root && tab.ActiveColumn == 0 && first.SelectedPath is null,
            "Deleting an open folder did not remove its descendant columns.");
        Check(tab.Filter == "chi", "Pruning columns lost the surviving folder's query.");
        tab.SetViewMode(ExplorerViewMode.Details);
        tab.RefreshSnapshots([new(root, [folder])]);
        Check(tab.Entries.Count == 1 && tab.Filter == "chi", "Flat refresh lost the filter or failed to replace rows.");
        return assertions;
    }
}
