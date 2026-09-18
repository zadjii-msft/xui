namespace Xui.FileExplorer.Models;

internal sealed class ExplorerTabJoin
{
    private readonly int sourceIndex;
    private readonly ExplorerTab? targetActive;
    internal ExplorerPane Source { get; }
    internal ExplorerPane Target { get; }
    internal ExplorerTab Tab { get; }
    internal bool Active { get; private set; } = true;

    private ExplorerTabJoin(ExplorerPane source, ExplorerPane target, ExplorerTab tab, int index)
    {
        Source = source;
        Target = target;
        Tab = tab;
        sourceIndex = index;
        targetActive = target.Tabs.Count == 0 ? null : target.Active;
    }

    internal static ExplorerTabJoin? Join(ExplorerPane source, ExplorerPane target, ulong id, int index)
    {
        if (ReferenceEquals(source, target) || !source.CanTransferTab(id, target, index)) return null;
        int original = source.Tabs.FindIndex(tab => tab.Id == id);
        var join = new ExplorerTabJoin(source, target, source.Tabs[original], original);
        return source.TransferTab(id, target, index) ? join : null;
    }

    internal bool ContainsTab => Active && Target.Tabs.Any(tab => ReferenceEquals(tab, Tab));
    internal bool IsUnchangedSlot(int index)
    {
        if (!ContainsTab) return false;
        int current = Target.Tabs.IndexOf(Tab);
        return index == current || index == current + 1;
    }

    internal bool Reorder(int index)
        => ContainsTab && (IsUnchangedSlot(index) || Target.TransferTab(Tab.Id, Target, index));

    internal bool Leave()
    {
        if (!ContainsTab || !Target.CanTransferTab(Tab.Id, Source, Math.Min(sourceIndex, Source.Tabs.Count)))
            return false;
        bool restoreSelection = ReferenceEquals(Target.Active, Tab);
        Target.TransferTab(Tab.Id, Source, Math.Min(sourceIndex, Source.Tabs.Count));
        if (restoreSelection && targetActive is not null && Target.Tabs.Contains(targetActive))
            Target.SelectTab(targetActive.Id);
        Active = false;
        return true;
    }

    internal bool RecoverTo(ExplorerPane destination)
    {
        if (!Active) return false;
        if (destination.Tabs.Contains(Tab))
        {
            destination.SelectTab(Tab.Id);
            Active = false;
            return true;
        }
        int index = Math.Min(sourceIndex, destination.Tabs.Count);
        if (ContainsTab)
        {
            if (!Target.CanTransferTab(Tab.Id, destination, index)) return false;
            bool restoreSelection = ReferenceEquals(Target.Active, Tab);
            Target.TransferTab(Tab.Id, destination, index);
            if (restoreSelection && targetActive is not null && Target.Tabs.Contains(targetActive))
                Target.SelectTab(targetActive.Id);
        }
        else
        {
            // The native host or its live pane may be gone; the lease retains the actual tab.
            var retained = new ExplorerPane([Tab], Tab.Id);
            if (!retained.TransferTab(Tab.Id, destination, index)) return false;
        }
        Active = false;
        return true;
    }

    internal bool Commit()
    {
        if (!ContainsTab) return false;
        Active = false;
        return true;
    }
}
