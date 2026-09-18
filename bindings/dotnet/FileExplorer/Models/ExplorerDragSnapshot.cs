namespace Xui.FileExplorer.Models;

// Only model references cross windows. Native controls and asynchronous work remain with their owners.
internal sealed class ExplorerDragSnapshot
{
    private readonly ExplorerTab[] left, right;
    private readonly ulong leftActive, rightActive;

    internal ExplorerDragSnapshot(ExplorerPane left, ExplorerPane right)
    {
        this.left = left.Tabs.ToArray();
        this.right = right.Tabs.ToArray();
        leftActive = this.left.Length == 0 ? 0 : left.Active.Id;
        rightActive = this.right.Length == 0 ? 0 : right.Active.Id;
    }

    internal bool CanRestore(params ExplorerPane[] panes)
    {
        var expected = left.Concat(right).ToArray();
        var current = panes.SelectMany(pane => pane.Tabs).ToArray();
        return expected.Length == current.Length
            && expected.All(tab => current.Count(item => ReferenceEquals(tab, item)) == 1);
    }

    internal (ExplorerPane Left, ExplorerPane Right) Restore()
        => (new(left, leftActive), new(right, rightActive));

    internal static ExplorerPane Without(ExplorerPane pane, ulong id)
    {
        var tabs = pane.Tabs.Where(tab => tab.Id != id).ToArray();
        ulong selected = tabs.Length == 0 ? 0
            : tabs.Any(tab => ReferenceEquals(tab, pane.Active)) ? pane.Active.Id : tabs[0].Id;
        return new(tabs, selected);
    }

    internal static ExplorerPane Empty() => new([], 0);
}
