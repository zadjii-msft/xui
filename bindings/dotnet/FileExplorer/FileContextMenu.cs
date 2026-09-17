using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class FileContextMenu(ExplorerApplication app, FilePaneView pane)
{
    internal const ulong Open = 1, NewTab = 2, OtherPane = 3, Bookmark = 4, Refresh = 5;
    internal const ulong Copy = 6, Cut = 7, Paste = 8, CopyPaths = 9;
    internal const ulong Preview = 10;
    private FileEntry? target;
    private string[] paths = [];
    private string destination = "";
    private Command[] commands = [];
    private ulong tab;
    private ExplorerViewMode mode;

    public Command[] GetCommands()
        => GetCommands(pane.SelectedEntries);

    internal Command[] GetCommands(FileEntry[] entries)
    {
        pane.Activate();
        tab = pane.Model.Active.Id;
        mode = pane.Model.Active.ViewMode;
        target = entries.Length == 1 ? entries[0] : null;
        paths = entries.Select(entry => entry.FullPath).ToArray();
        destination = target is { IsDirectory: true } ? target.FullPath : pane.TransferDirectory;
        List<Command> result = [];
        if (target is not null)
            result.Add(new(Preview, "Preview", ShortcutHint: "Space"));
        if (target is { IsDirectory: true })
        {
            bool saved = app.State.Bookmarks.Contains(target.FullPath, StringComparer.OrdinalIgnoreCase);
            result.AddRange([
                new(Open, "Open in this pane"),
                new(NewTab, "Open in new tab"),
                new(OtherPane, "Open in other pane"),
                new(Bookmark, saved ? "Remove bookmark" : "Bookmark folder")
            ]);
        }
        bool ready = app.Transfers.CanTransfer(pane);
        if (paths.Length > 0)
        {
            result.Add(new(Copy, "Copy files", Enabled: ready, ShortcutHint: "Ctrl+C"));
            result.Add(new(Cut, "Cut files", Enabled: ready, ShortcutHint: "Ctrl+X"));
            result.Add(new(CopyPaths, "Copy paths", Enabled: ready, ShortcutHint: "Ctrl+Shift+C"));
        }
        result.Add(new(Paste, target is { IsDirectory: true } ? "Paste into this folder" : "Paste",
            Enabled: ready, ShortcutHint: "Ctrl+V"));
        result.Add(new(Refresh, "Refresh", ShortcutHint: "F5"));
        return commands = result.ToArray();
    }

    public string[] GetShellPaths() => paths.Length <= 256 ? paths : [];

    public void Invoke(ulong id)
    {
        if (tab != pane.Model.Active.Id || mode != pane.Model.Active.ViewMode)
        {
            app.Report("The menu is no longer available in this tab or view.");
            return;
        }
        if (!commands.Any(command => command.Id == id && command.Enabled))
        {
            app.Report("That command is not available for this item.");
            return;
        }
        pane.Activate();
        if (id == Refresh) { pane.Refresh(); return; }
        if (id == Copy) { app.Transfers.Copy(pane, paths, cut: false); return; }
        if (id == Cut) { app.Transfers.Copy(pane, paths, cut: true); return; }
        if (id == CopyPaths) { app.Transfers.CopyPaths(pane, paths); return; }
        if (id == Paste) { app.Transfers.Paste(pane, destination); return; }
        if (target is not { } entry)
        {
            app.Report("The selected item is no longer available.");
            return;
        }
        switch (id)
        {
            case Preview:
                app.Preview.Show(pane, entry);
                break;
            case Open:
                app.Open(entry, pane);
                pane.Focus();
                break;
            case NewTab:
                pane.NewTab(entry.FullPath);
                pane.Focus();
                break;
            case OtherPane:
                if (app.OtherPane(pane, show: true) is { } other)
                {
                    other.Navigate(entry.FullPath);
                    other.Focus();
                }
                break;
            case Bookmark:
                app.ToggleBookmark(entry.FullPath);
                break;
        }
    }
}
