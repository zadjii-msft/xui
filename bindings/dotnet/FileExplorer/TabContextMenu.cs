using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class TabContextMenu(ExplorerApplication app, FilePaneView pane)
{
    internal const ulong ShiftLeft = 1, ShiftRight = 2, Duplicate = 3, NewWindow = 4, NewPane = 5,
        CopyPath = 6, Close = 7, CloseOthers = 8, CloseLeft = 9, CloseRight = 10, CloseAll = 11;
    private ExplorerTab? target;
    private ulong[] order = [];
    private string path = "";
    private Command[] commands = [];

    public Command[] GetCommands(ulong id)
    {
        target = pane.Model.Tabs.Find(tab => tab.Id == id);
        if (target is null) throw new InvalidOperationException("The tab is no longer available.");
        path = target.Path;
        order = pane.Model.Tabs.Select(tab => tab.Id).ToArray();
        int index = Array.IndexOf(order, id);
        var other = ReferenceEquals(pane, app.Left) ? app.Right : app.Left;
        return commands =
        [
            new(ShiftLeft, "Shift tab left", Enabled: index > 0, ShortcutHint: "Ctrl+Shift+PageUp"),
            new(ShiftRight, "Shift tab right", Enabled: index < order.Length - 1, ShortcutHint: "Ctrl+Shift+PageDown"),
            new(0, "", Kind: CommandKind.Separator),
            new(Duplicate, "Duplicate tab", Enabled: order.Length < ExplorerPane.TabLimit),
            new(NewWindow, "Duplicate tab to new window", ShortcutHint: "Ctrl+N"),
            new(NewPane, "Duplicate in new pane", Enabled: other.Model.Tabs.Count < ExplorerPane.TabLimit),
            new(CopyPath, "Copy path", Enabled: !app.Transfers.Busy),
            new(0, "", Kind: CommandKind.Separator),
            new(Close, "Close tab", ShortcutHint: "Ctrl+W / Ctrl+F4"),
            new(CloseOthers, "Close other tabs", Enabled: order.Length > 1),
            new(CloseLeft, "Close left tabs", Enabled: index > 0),
            new(CloseRight, "Close right tabs", Enabled: index < order.Length - 1),
            new(CloseAll, "Close all tabs", ShortcutHint: "Ctrl+Shift+W")
        ];
    }

    public void Invoke(ulong id)
    {
        if (target is null || target.Path != path ||
            !order.SequenceEqual(pane.Model.Tabs.Select(tab => tab.Id)) ||
            !pane.Model.Tabs.Contains(target))
        {
            app.Report("The tab menu is no longer available. Open it again.");
            return;
        }
        if (!commands.Any(command => command.Id == id && command.Enabled && command.Kind == CommandKind.Action)
            || (id == CopyPath && app.Transfers.Busy))
        {
            app.Report("That command is not available for this tab.");
            return;
        }
        pane.Activate();
        int index = Array.IndexOf(order, target.Id);
        switch (id)
        {
            case ShiftLeft: pane.MoveTab(target.Id, -1); break;
            case ShiftRight: pane.MoveTab(target.Id, 1); break;
            case Duplicate: pane.DuplicateTab(target); break;
            case NewWindow: app.NewWindow(path); break;
            case NewPane: app.DuplicateInNewPane(pane, target); break;
            case CopyPath: app.Transfers.CopyPaths(pane, [path]); break;
            case Close: pane.CloseTab(target.Id); break;
            case CloseOthers: pane.CloseTabs(order.Where(tab => tab != target.Id)); break;
            case CloseLeft: pane.CloseTabs(order.Take(index)); break;
            case CloseRight: pane.CloseTabs(order.Skip(index + 1)); break;
            case CloseAll: app.ClosePane(pane); break;
        }
    }
}
