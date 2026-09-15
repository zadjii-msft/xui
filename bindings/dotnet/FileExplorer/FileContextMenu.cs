using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class FileContextMenu(ExplorerApplication app, FilePaneView pane)
{
    internal const ulong Open = 1, NewTab = 2, OtherPane = 3, Bookmark = 4, Refresh = 5;
    private FileEntry? target;
    private Command[] commands = [];
    private ulong tab;
    private ExplorerViewMode mode;

    public Command[] GetCommands()
    {
        pane.Activate();
        tab = pane.Model.Active.Id;
        mode = pane.Model.Active.ViewMode;
        target = pane.SelectedEntry;
        if (target is null || !target.IsDirectory) return commands = [new(Refresh, "Refresh", ShortcutHint: "F5")];
        bool saved = app.State.Bookmarks.Contains(target.FullPath, StringComparer.OrdinalIgnoreCase);
        return commands =
        [
            new(Open, "Open in this pane"),
            new(NewTab, "Open in new tab"),
            new(OtherPane, "Open in other pane"),
            new(Bookmark, saved ? "Remove bookmark" : "Bookmark folder"),
            new(Refresh, "Refresh", ShortcutHint: "F5")
        ];
    }

    public string[] GetShellPaths() => target is null ? [] : [target.FullPath];

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
        if (target is not { } entry)
        {
            app.Report("The selected item is no longer available.");
            return;
        }
        switch (id)
        {
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
