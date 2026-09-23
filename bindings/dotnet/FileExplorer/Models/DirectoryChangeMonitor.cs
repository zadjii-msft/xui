namespace Xui.FileExplorer.Models;

internal sealed class DirectoryChangeMonitor : IDisposable
{
    private readonly object gate = new();
    private readonly List<FileSystemWatcher> watchers = [];
    private readonly Timer timer;
    private readonly Action<Exception?> changed;
    private Exception? error;
    private bool pending, disposed;

    public DirectoryChangeMonitor(IEnumerable<string> paths, bool recursive, Action<Exception?> changed)
    {
        this.changed = changed;
        timer = new(_ => Deliver(), null, Timeout.Infinite, Timeout.Infinite);
        try
        {
            foreach (string path in paths.Distinct(StringComparer.OrdinalIgnoreCase))
            {
                var watcher = new FileSystemWatcher(path)
                {
                    IncludeSubdirectories = recursive,
                    NotifyFilter = NotifyFilters.FileName | NotifyFilters.DirectoryName |
                        NotifyFilters.LastWrite | NotifyFilters.Size
                };
                watchers.Add(watcher);
                watcher.Created += OnChange;
                watcher.Deleted += OnChange;
                watcher.Changed += OnChange;
                watcher.Renamed += OnChange;
                watcher.Error += (_, e) => Signal(e.GetException());
                watcher.EnableRaisingEvents = true;
            }
        }
        catch
        {
            Dispose();
            throw;
        }
    }

    private void OnChange(object sender, FileSystemEventArgs e) => Signal(null);

    private void Signal(Exception? failure)
    {
        lock (gate)
        {
            if (disposed) return;
            error ??= failure;
            if (pending) return;
            pending = true;
            // Bound the wait even while a writer continuously changes the directory.
            timer.Change(100, Timeout.Infinite);
        }
    }

    private void Deliver()
    {
        Exception? failure;
        lock (gate)
        {
            if (disposed) return;
            pending = false;
            failure = error;
            error = null;
        }
        changed(failure);
    }

    public void Dispose()
    {
        lock (gate)
        {
            if (disposed) return;
            disposed = true;
            timer.Dispose();
        }
        foreach (var watcher in watchers) watcher.Dispose();
        watchers.Clear();
    }
}
