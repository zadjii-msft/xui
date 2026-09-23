using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed partial class FilePaneView
{
    private DirectoryChangeMonitor? directoryWatch;
    private string[] watchedPaths = [];
    private bool watchingTree, refreshPending, refreshQueued;
    private int watchGeneration;

    internal void UpdateDirectoryWatch()
    {
        if (app.CloseRequested || app.Work.Lifetime.IsCancellationRequested || Model.Tabs.Count == 0 ||
            (ReferenceEquals(this, app.Right) && !app.SecondPaneVisible) || !Model.Active.HasSnapshot)
        {
            StopDirectoryWatch();
            return;
        }
        string[] paths = IsColumns
            ? Model.Active.Columns.Select(column => column.Snapshot.Path).ToArray()
            : [Model.Active.Path];
        if (directoryWatch is not null && watchingTree == IsTree &&
            watchedPaths.SequenceEqual(paths, StringComparer.OrdinalIgnoreCase)) return;
        StopDirectoryWatch();
        int generation = watchGeneration;
        try
        {
            directoryWatch = new(paths, IsTree, failure =>
            {
                window.Post(() =>
                {
                    if (generation != watchGeneration || app.Work.Lifetime.IsCancellationRequested) return;
                    if (failure is not null)
                        app.Report($"Cannot monitor this folder: {failure.Message} Refresh the folder to retry.");
                    RequestDirectoryRefresh();
                });
            });
            watchedPaths = paths;
            watchingTree = IsTree;
            // Reconcile changes made between the previous scan and watcher registration.
            RequestDirectoryRefresh();
        }
        catch (Exception failure) when (UiWork.IsExpected(failure))
        {
            app.Report($"Cannot monitor this folder: {failure.Message} Use Refresh to update its contents.");
        }
    }

    private void StopDirectoryWatch()
    {
        ++watchGeneration;
        directoryWatch?.Dispose();
        directoryWatch = null;
        watchedPaths = [];
        refreshPending = refreshQueued = false;
    }

    internal void RequestDirectoryRefresh()
    {
        if (app.CloseRequested || app.Work.Lifetime.IsCancellationRequested || Model.Tabs.Count == 0 ||
            (ReferenceEquals(this, app.Right) && !app.SecondPaneVisible)) return;
        refreshPending = true;
        QueueDirectoryRefresh();
    }

    private void QueueDirectoryRefresh()
    {
        if (refreshQueued || !refreshPending) return;
        refreshQueued = true;
        int generation = watchGeneration;
        app.Work.Start(async token => { await Task.Delay(100, token).ConfigureAwait(false); return true; },
            app.Work.Lifetime, _ =>
            {
                if (generation != watchGeneration) return;
                refreshQueued = false;
                if (IsLoading || IsFiltering || app.Transfers.Busy)
                {
                    QueueDirectoryRefresh();
                    return;
                }
                refreshPending = false;
                ReloadWatchedDirectories();
            }, failure => app.Report($"Cannot refresh this folder: {failure.Message}"));
    }

    private void ReloadWatchedDirectories()
    {
        SaveViewport();
        navigation.Cancel();
        navigation.Dispose();
        navigation = new();
        var tab = Model.Active;
        string[] paths = IsColumns
            ? tab.Columns.Select(column => column.Snapshot.Path).ToArray() : [tab.Path];
        IsLoading = true;
        tree.Cancel();
        RefreshCommandAvailability();
        app.Work.Start(async token =>
        {
            var snapshots = new List<DirectorySnapshot>();
            foreach (string path in paths)
            {
                if (snapshots.Count > 0 && !snapshots[^1].Entries.Any(entry => entry.IsDirectory &&
                    string.Equals(entry.FullPath, path, StringComparison.OrdinalIgnoreCase))) break;
                snapshots.Add(await app.Files.ReadDirectoryAsync(path, path, token).ConfigureAwait(false));
            }
            return snapshots;
        }, navigation.Token, snapshots =>
        {
            if (!ReferenceEquals(tab, Model.Active)) return;
            tab.RefreshSnapshots(snapshots);
            IsLoading = false;
            error = null;
            Render(preserveSelection: true);
            app.LocationChanged(this, recordRecent: false);
        }, failure =>
        {
            IsLoading = false;
            error = $"Cannot refresh {tab.Path}: {failure.Message}";
            status.Text = error;
            app.Report(error);
            UpdateNavigation();
        });
    }
}
