namespace Xui.FileExplorer;

internal sealed class FileTransfers(ExplorerApplication app)
{
    public bool Busy { get; private set; }

    public bool CanTransfer(FilePaneView pane) =>
        !Busy && !app.Palettes.IsOpen && pane.HasCurrentRows;

    public void Bind(FilePaneView pane)
    {
        pane.Grid.OnFileDrag(() => CanTransfer(pane) ? pane.SelectedEntries.Select(e => e.FullPath).ToArray() : [],
            _ => RefreshPanes());
        pane.Grid.OnFileDrop((key, effect) => QueryDrop(pane, key, effect),
            (key, paths, effect) => Drop(pane, key, paths, effect));
    }

    internal FileTransferEffect QueryDrop(FilePaneView pane, ItemKey? key, FileTransferEffect effect) =>
        DropDestination(pane, key) is not null ? effect : FileTransferEffect.None;

    internal FileTransferEffect Drop(FilePaneView pane, ItemKey? key, string[] paths, FileTransferEffect effect)
    {
        if (DropDestination(pane, key) is not { } destination) return FileTransferEffect.None;
        pane.Activate();
        return Transfer(pane, paths, destination, effect) ? effect : FileTransferEffect.None;
    }

    private string? DropDestination(FilePaneView pane, ItemKey? key)
    {
        if (!CanTransfer(pane) || pane.IsColumns) return null;
        return key is { } row ? pane.Entry(row) is { IsDirectory: true } folder ? folder.FullPath : null
            : pane.Model.Active.Path;
    }

    public void Copy(FilePaneView pane, bool cut)
    {
        if (!CanTransfer(pane)) return;
        Copy(pane, pane.SelectedEntries.Select(e => e.FullPath).ToArray(), cut);
    }

    public void Copy(FilePaneView pane, string[] paths, bool cut)
    {
        if (Busy) return;
        if (paths.Length == 0) { app.Report("Select files or folders first."); return; }
        try
        {
            app.Window.SetFileClipboard(paths, cut ? FileTransferEffect.Move : FileTransferEffect.Copy);
            ReportCopied(pane, paths.Length, cut);
        }
        catch (Exception error) when (IsExpected(error))
        {
            app.Report($"Cannot {(cut ? "cut" : "copy")} these items: {error.Message}");
        }
    }

    internal void ReportCopied(FilePaneView pane, int count, bool cut)
    {
        string items = count == 1 ? "1 item" : $"{count:N0} items";
        pane.ShowFeedback(cut ? $"{items} cut. Paste to move." : $"{items} copied.");
    }

    public void CopyPaths(FilePaneView pane)
    {
        if (!CanTransfer(pane)) return;
        CopyPaths(pane, pane.SelectedEntries.Select(e => e.FullPath).ToArray());
    }

    public void CopyPaths(FilePaneView pane, string[] paths)
    {
        if (Busy) return;
        if (paths.Length == 0) { app.Report("Select files or folders first."); return; }
        try
        {
            app.Window.SetClipboardText(string.Join(Environment.NewLine, paths.Select(path => $"\"{path}\"")));
            pane.ShowFeedback(paths.Length == 1 ? "1 path copied." : $"{paths.Length:N0} paths copied.");
        }
        catch (Exception error) when (IsExpected(error))
        {
            app.Report($"Cannot copy these paths: {error.Message}");
        }
    }

    public void Paste(FilePaneView pane, string? destination = null)
    {
        if (!CanTransfer(pane)) return;
        destination ??= pane.TransferDirectory;
        Busy = true;
        try
        {
            pane.ShowFeedback("Pasting...");
            bool? result = app.Window.PasteFiles(destination);
            if (result == false) app.Report("Paste canceled or incomplete. Some items may already have transferred.");
            else pane.ShowFeedback(result == true ? "Paste completed." : "The clipboard does not contain files or folders.");
        }
        catch (Exception error) when (IsExpected(error))
        {
            app.Report($"Cannot paste into {destination}: {error.Message}");
        }
        finally
        {
            Busy = false;
            RefreshPanes();
        }
    }

    internal bool Transfer(FilePaneView pane, string[] paths, string destination, FileTransferEffect effect)
    {
        if (Busy) return false;
        Busy = true;
        try
        {
            pane.ShowFeedback(effect == FileTransferEffect.Move ? "Moving..." : "Copying...");
            bool complete = app.Window.TransferFiles(paths, destination, effect);
            if (complete) pane.ShowFeedback("Transfer completed.");
            else app.Report("Transfer canceled or incomplete. Some items may already have transferred.");
            return complete;
        }
        catch (Exception error) when (IsExpected(error))
        {
            app.Report($"Cannot transfer items into {destination}: {error.Message}");
            return false;
        }
        finally
        {
            Busy = false;
            RefreshPanes();
        }
    }

    private void RefreshPanes()
    {
        if (app.Work.Lifetime.IsCancellationRequested) return;
        if (app.Left.HasCurrentRows) app.Left.Refresh();
        if (app.SecondPaneVisible && app.Right.HasCurrentRows) app.Right.Refresh();
    }

    private static bool IsExpected(Exception error) => error is XuiException || UiWork.IsExpected(error);
}
