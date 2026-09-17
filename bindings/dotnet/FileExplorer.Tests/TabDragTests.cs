using Xui.FileExplorer.Models;

internal static class TabDragTests
{
    internal static int Run(string path)
    {
        int assertions = 0;
        void Check(bool condition)
        {
            ++assertions;
            if (!condition) throw new InvalidOperationException($"Tab drag assertion {assertions} failed.");
        }
        void SameOrder(IEnumerable<ExplorerTab> expected, ExplorerPane pane)
            => Check(expected.SequenceEqual(pane.Tabs));
        ExplorerPane Pane(int count)
        {
            var pane = new ExplorerPane(path);
            for (int i = 1; i < count; ++i) pane.AddTab(path);
            return pane;
        }

        for (int count = 1; count <= ExplorerPane.TabLimit; ++count)
        for (int source = 0; source < count; ++source)
        for (int slot = 0; slot <= count; ++slot)
        {
            var pane = Pane(count);
            var expected = pane.Tabs.ToList();
            var moved = expected[source];
            var before = pane.Tabs.ToArray();
            var active = pane.Active;
            Check(pane.CanTransferTab(moved.Id, pane, slot));
            SameOrder(before, pane);
            Check(ReferenceEquals(active, pane.Active));
            expected.RemoveAt(source);
            expected.Insert(slot > source ? slot - 1 : slot, moved);
            Check(pane.TransferTab(moved.Id, pane, slot));
            SameOrder(expected, pane);
            Check(ReferenceEquals(moved, pane.Active));
        }

        var left = Pane(3);
        var right = Pane(2);
        var all = left.Tabs.Concat(right.Tabs).ToArray();
        Check(all.Select(tab => tab.Id).Distinct().Count() == all.Length);
        var tab = left.Tabs[1];
        string child = Path.Combine(path, "child");
        var entry = new FileEntry(Path.Combine(child, "selected.txt"), "selected.txt", false, 42, DateTime.UnixEpoch);
        tab.Commit(new(path, [new(child, "child", true, 0, DateTime.UnixEpoch)]));
        tab.SetViewMode(ExplorerViewMode.Columns);
        tab.CommitColumn(0, new(child, [entry]));
        tab.Filter = "selected";
        tab.FindOpen = true;
        tab.SortColumn = 2;
        tab.SortDescending = true;
        tab.SelectedPath = entry.FullPath;
        tab.ScrollOffset = 55;
        tab.Columns[0].ScrollOffset = 17;
        tab.Columns[1].SelectedPath = entry.FullPath;
        tab.Columns[1].ScrollOffset = 55;
        tab.ActiveColumn = 1;
        var entries = tab.Entries;
        var columns = tab.Columns.ToArray();
        left.SelectTab(tab.Id);
        var originalLeft = left.Tabs.ToArray();
        var originalRight = right.Tabs.ToArray();
        var snapshot = new ExplorerDragSnapshot(left, right);
        var selectedRight = right.Active;
        for (int i = 0; i < 3; ++i)
        {
            Check(left.CanTransferTab(tab.Id, right, 1));
            SameOrder(originalLeft, left);
            SameOrder(originalRight, right);
            Check(ReferenceEquals(left.Active, tab) && ReferenceEquals(right.Active, selectedRight));
            Check(tab.Filter == "selected" && tab.SelectedPath == entry.FullPath && tab.ScrollOffset == 55);
        }
        Check(left.TransferTab(tab.Id, right, 1));
        SameOrder(new[] { originalLeft[0], originalLeft[2] }, left);
        SameOrder(new[] { originalRight[0], tab, originalRight[1] }, right);
        Check(ReferenceEquals(left.Active, originalLeft[2]));
        Check(ReferenceEquals(right.Active, tab));
        Check(tab.Filter == "selected" && tab.FindOpen && tab.SortColumn == 2 && tab.SortDescending);
        Check(tab.SelectedPath == entry.FullPath && tab.ScrollOffset == 55);
        Check(tab.TryGetHistory(-1, out string previous) && previous == path);
        Check(ReferenceEquals(entries, tab.Entries));
        Check(tab.ViewMode == ExplorerViewMode.Columns && tab.ActiveColumn == 1);
        Check(tab.Columns.SequenceEqual(columns) && tab.Columns[0].ScrollOffset == 17);
        Check(tab.Columns[1].ScrollOffset == 55 && tab.Columns[1].SelectedPath == entry.FullPath);
        Check(snapshot.CanRestore(left, right));
        var restored = snapshot.Restore();
        SameOrder(originalLeft, restored.Left);
        SameOrder(originalRight, restored.Right);
        Check(ReferenceEquals(restored.Left.Active, tab));

        var extra = right.AddTab(path);
        Check(!snapshot.CanRestore(left, right));
        right.CloseTab(extra.Id);
        Check(snapshot.CanRestore(left, right));
        Check(!snapshot.CanRestore(left, left));

        var full = Pane(ExplorerPane.TabLimit);
        var savedLeft = left.Tabs.ToArray();
        var savedFull = full.Tabs.ToArray();
        var selectedLeft = left.Active;
        var selectedFull = full.Active;
        Check(!left.TransferTab(left.Active.Id, full, 0));
        SameOrder(savedLeft, left);
        SameOrder(savedFull, full);
        Check(ReferenceEquals(selectedLeft, left.Active) && ReferenceEquals(selectedFull, full.Active));
        foreach (int slot in new[] { -1, int.MinValue, int.MaxValue, right.Tabs.Count + 1 })
        {
            Check(!left.TransferTab(left.Active.Id, right, slot));
            SameOrder(savedLeft, left);
        }
        Check(!left.TransferTab(ulong.MaxValue, right, 0));
        SameOrder(savedLeft, left);
        var collision = new ExplorerPane([left.Active], left.Active.Id);
        Check(!left.TransferTab(left.Active.Id, collision, 0));
        SameOrder(savedLeft, left);

        foreach (uint sourceStrip in new uint[] { 0, 1 })
        {
            left = Pane(3);
            right = Pane(3);
            var source = sourceStrip == 0 ? left : right;
            source.SelectTab(source.Tabs[1].Id);
            var dragged = source.Active;
            originalLeft = left.Tabs.ToArray();
            originalRight = right.Tabs.ToArray();
            snapshot = new(left, right);
            var remainingLeft = sourceStrip == 0 ? ExplorerDragSnapshot.Without(left, dragged.Id) : left;
            var remainingRight = sourceStrip == 1 ? ExplorerDragSnapshot.Without(right, dragged.Id) : right;
            var moving = new ExplorerPane([dragged], dragged.Id);
            var empty = ExplorerDragSnapshot.Empty();
            Check(snapshot.CanRestore(moving, empty, remainingLeft, remainingRight));
            Check(ReferenceEquals(moving.Active, dragged) && moving.Active.Id == dragged.Id);
            Check(remainingLeft.Tabs.Concat(remainingRight.Tabs).All(item => !ReferenceEquals(item, dragged)));
            restored = snapshot.Restore();
            SameOrder(originalLeft, restored.Left);
            SameOrder(originalRight, restored.Right);
            Check(ReferenceEquals((sourceStrip == 0 ? restored.Left : restored.Right).Active, dragged));
            var target = Pane(1);
            Check(moving.TransferTab(dragged.Id, target, 1));
            Check(moving.Tabs.Count == 0 && ReferenceEquals(target.Active, dragged));
            Check(!snapshot.CanRestore(moving, empty, remainingLeft, remainingRight));
        }

        var lone = Pane(1);
        var destination = ExplorerDragSnapshot.Empty();
        var loneTab = lone.Active;
        Check(lone.TransferTab(loneTab.Id, destination, 0));
        Check(lone.Tabs.Count == 0 && ReferenceEquals(destination.Active, loneTab));
        Check(destination.TransferTab(loneTab.Id, lone, 0));
        Check(ReferenceEquals(lone.Active, loneTab));
        var unique = new HashSet<ulong>();
        for (int i = 0; i < 100; ++i)
        {
            var pane = Pane(1);
            Check(unique.Add(pane.Active.Id));
            pane.ResetTabs(path);
            Check(unique.Add(pane.Active.Id));
            Check(unique.Add(pane.DuplicateTab(pane.Active).Id));
            pane.ResetTabs(pane.Active);
            Check(unique.Add(pane.Active.Id));
            pane.ReplaceTabs(lone);
            Check(unique.Add(pane.Active.Id));
        }
        return assertions + HoverJoinTests.Run(path);
    }
}
