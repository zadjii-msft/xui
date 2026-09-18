using Xui.FileExplorer.Models;

internal static class HoverJoinTests
{
    internal static int Run(string path)
    {
        int assertions = 0;
        void Check(bool condition)
        {
            ++assertions;
            if (!condition) throw new InvalidOperationException($"Hover join assertion {assertions} failed.");
        }
        ExplorerPane Pane(int count)
        {
            var pane = ExplorerDragSnapshot.Empty();
            for (int i = 0; i < count; ++i) pane.AddTab(path);
            return pane;
        }
        for (int count = 1; count < ExplorerPane.TabLimit; ++count)
        for (int slot = 0; slot <= count; ++slot)
        {
            var source = Pane(1);
            var target = Pane(count);
            var tab = source.Active;
            var selected = target.Active;
            var original = target.Tabs.ToArray();
            var join = ExplorerTabJoin.Join(source, target, tab.Id, slot);
            Check(join is not null && source.Tabs.Count == 0 && ReferenceEquals(target.Tabs[slot], tab));
            target.SelectTab(selected.Id);
            var unchanged = target.Tabs.ToArray();
            Check(join!.IsUnchangedSlot(slot) && join.IsUnchangedSlot(slot + 1));
            Check(join.Reorder(slot) && join.Reorder(slot + 1));
            Check(target.Tabs.SequenceEqual(unchanged) && ReferenceEquals(target.Active, selected));
            Check(join!.Reorder(target.Tabs.Count));
            Check(ReferenceEquals(target.Tabs[^1], tab));
            Check(join.Reorder(0) && ReferenceEquals(target.Tabs[0], tab));
            Check(join.Leave() && ReferenceEquals(source.Active, tab));
            Check(target.Tabs.SequenceEqual(original) && ReferenceEquals(target.Active, selected));
            Check(!join.Leave() && !join.Commit() && !join.Reorder(0));
        }

        var left = Pane(3);
        var right = Pane(2);
        var leftOrder = left.Tabs.ToArray();
        var rightOrder = right.Tabs.ToArray();
        var snapshot = new ExplorerDragSnapshot(left, right);
        foreach (var originalSource in new[] { left, right })
        {
            var tab = originalSource.Tabs[1];
            var moving = new ExplorerPane([tab], tab.Id);
            var remainingLeft = originalSource == left ? ExplorerDragSnapshot.Without(left, tab.Id) : left;
            var remainingRight = originalSource == right ? ExplorerDragSnapshot.Without(right, tab.Id) : right;
            foreach (var target in new[] { Pane(2), Pane(3), remainingLeft, remainingRight })
            {
                var targetOrder = target.Tabs.ToArray();
                var selected = target.Active;
                var join = ExplorerTabJoin.Join(moving, target, tab.Id, 0)!;
                Check(join.Reorder(target.Tabs.Count));
                Check(join.Leave() && target.Tabs.SequenceEqual(targetOrder)
                    && ReferenceEquals(target.Active, selected));
                Check(snapshot.CanRestore(moving, remainingLeft, remainingRight));
            }
            var restored = snapshot.Restore();
            Check(restored.Left.Tabs.SequenceEqual(leftOrder) && restored.Right.Tabs.SequenceEqual(rightOrder));
        }

        var origin = Pane(1);
        var destination = Pane(3);
        var dragged = origin.Active;
        var previous = destination.Active;
        var lease = ExplorerTabJoin.Join(origin, destination, dragged.Id, 1)!;
        var added = destination.AddTab(path);
        destination.MoveTab(destination.Tabs[0].Id, 1);
        var concurrent = destination.Tabs.Where(tab => tab != dragged).ToArray();
        Check(lease.Leave() && destination.Tabs.SequenceEqual(concurrent));
        Check(ReferenceEquals(destination.Active, added));
        Check(ReferenceEquals(origin.Active, dragged));

        lease = ExplorerTabJoin.Join(origin, destination, dragged.Id, 2)!;
        destination.CloseTab(previous.Id);
        destination.SelectTab(dragged.Id);
        concurrent = destination.Tabs.Where(tab => tab != dragged).ToArray();
        Check(lease.Leave() && destination.Tabs.SequenceEqual(concurrent));
        Check(destination.Tabs.Contains(destination.Active) && destination.Active != previous);

        lease = ExplorerTabJoin.Join(origin, destination, dragged.Id, 0)!;
        Check(lease.Commit() && origin.Tabs.Count == 0 && destination.Tabs[0] == dragged);
        Check(!lease.Leave() && !lease.Reorder(1) && !lease.Commit());
        var other = Pane(1);
        lease = ExplorerTabJoin.Join(destination, other, dragged.Id, 1)!;
        Check(lease.Leave() && destination.Tabs[0] == dragged);

        var full = Pane(ExplorerPane.TabLimit);
        var fullOrder = full.Tabs.ToArray();
        var sourceOrder = destination.Tabs.ToArray();
        Check(ExplorerTabJoin.Join(destination, full, dragged.Id, 0) is null);
        Check(full.Tabs.SequenceEqual(fullOrder) && destination.Tabs.SequenceEqual(sourceOrder));
        Check(ExplorerTabJoin.Join(destination, destination, dragged.Id, 0) is null);
        Check(ExplorerTabJoin.Join(destination, other, ulong.MaxValue, 0) is null);
        Check(ExplorerTabJoin.Join(destination, other, dragged.Id, -1) is null);
        Check(ExplorerTabJoin.Join(destination, other, dragged.Id, other.Tabs.Count + 1) is null);
        lease = ExplorerTabJoin.Join(destination, other, dragged.Id, 0)!;
        var joinedOrder = other.Tabs.ToArray();
        Check(!lease.Reorder(-1) && !lease.Reorder(other.Tabs.Count + 1));
        Check(other.Tabs.SequenceEqual(joinedOrder));
        while (destination.Tabs.Count < ExplorerPane.TabLimit) destination.AddTab(path);
        Check(!lease.Leave() && lease.ContainsTab && other.Tabs.SequenceEqual(joinedOrder));
        destination.CloseTab(destination.Active.Id);
        Check(lease.Leave());

        origin = Pane(1);
        destination = Pane(1);
        dragged = origin.Active;
        dragged.Filter = "retained";
        lease = ExplorerTabJoin.Join(origin, destination, dragged.Id, 0)!;
        destination.ResetTabs(path);
        var replacement = destination.Active;
        Check(!lease.ContainsTab && lease.RecoverTo(origin));
        Check(origin.Active == dragged && dragged.Filter == "retained");
        Check(destination.Tabs.Count == 1 && destination.Active == replacement);
        Check(!lease.RecoverTo(Pane(0)));

        lease = ExplorerTabJoin.Join(origin, destination, dragged.Id, 0)!;
        var newSourceModel = Pane(1);
        Check(lease.RecoverTo(newSourceModel) && newSourceModel.Active == dragged);
        Check(origin.Tabs.Count == 0 && !destination.Tabs.Contains(dragged));

        lease = ExplorerTabJoin.Join(newSourceModel, destination, dragged.Id, 0)!;
        var collisionPane = new ExplorerPane([new ExplorerTab(dragged.Id, path)], dragged.Id);
        Check(!lease.RecoverTo(collisionPane) && lease.ContainsTab);
        var recovery = Pane(0);
        Check(lease.RecoverTo(recovery) && recovery.Active == dragged);
        Check(!destination.Tabs.Contains(dragged));
        return assertions;
    }
}
