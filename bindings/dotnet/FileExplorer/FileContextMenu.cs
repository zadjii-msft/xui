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
    private readonly ContextActionsController customization = new(app, pane);
    internal ContextActionsController Customization => customization;

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
            result.Add(new(Preview, "Preview", ShortcutHint: ShortcutHint("preview-selected-item")));
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
            result.Add(new(Copy, "Copy files", Enabled: ready, ShortcutHint: ShortcutHint("copy-files")));
            result.Add(new(Cut, "Cut files", Enabled: ready, ShortcutHint: ShortcutHint("cut-files")));
            result.Add(new(CopyPaths, "Copy paths", Enabled: ready, ShortcutHint: ShortcutHint("copy-file-paths")));
        }
        result.Add(new(Paste, target is { IsDirectory: true } ? "Paste into this folder" : "Paste",
            Enabled: ready, ShortcutHint: ShortcutHint("paste-files-into-this-folder")));
        result.Add(new(Refresh, "Refresh", ShortcutHint: ShortcutHint("refresh-folder")));
        commands = result.ToArray();
        var selectedPaths = (string[])paths.Clone();
        var selectedTab = tab;
        var selectedMode = mode;
        var selectedDirectory = pane.Model.Active.Path;
        bool selectionBound = pane.SelectedEntries.Select(entry => entry.FullPath).SequenceEqual(selectedPaths, StringComparer.OrdinalIgnoreCase);
        return customization.Capture(commands, selectedPaths,
            () => pane.HasCurrentRows && selectedTab == pane.Model.Active.Id && selectedMode == pane.Model.Active.ViewMode &&
                selectedDirectory == pane.Model.Active.Path &&
                (!selectionBound || pane.SelectedEntries.Select(entry => entry.FullPath).SequenceEqual(selectedPaths, StringComparer.OrdinalIgnoreCase)),
            Invoke);
    }

    public string[] GetShellPaths() => paths.Length <= 256 ? (string[])paths.Clone() : [];

    private string ShortcutHint(string identity) =>
        app.ShortcutHint(app.Commands.Single(command => command.StableId == identity));

    public void Invoke(ulong id)
    {
        if (customization.Invoke(id)) return;
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
        if (id is Copy or Cut or CopyPaths or Paste && !app.Transfers.CanTransfer(pane))
        {
            app.Report("File transfer actions are not available while this pane is busy.");
            return;
        }
        if (id == Paste && !Directory.Exists(destination))
        {
            app.Report("The paste destination is no longer available.");
            return;
        }
        if (id is not (Refresh or Paste) && paths.Any(path => !File.Exists(path) && !Directory.Exists(path)))
        {
            app.Report("A selected path is no longer available.");
            return;
        }
        if (id is Open or NewTab or OtherPane or Bookmark && target is { IsDirectory: true } folder &&
            !Directory.Exists(folder.FullPath))
        {
            app.Report("The selected folder is no longer available.");
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
