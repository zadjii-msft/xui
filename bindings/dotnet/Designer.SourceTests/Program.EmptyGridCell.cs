using Xui.Designer;

internal static partial class Program
{
    private static void TestEmptyGridCell()
    {
        var empty = Parse("""component Empty { view { Grid("Default") { } } }""");
        Found(empty, new(0, 0));
        for (int occupied = 0; occupied < 64; occupied++)
        {
            string children = string.Join(" ", Enumerable.Range(0, 6).Where(index => (occupied & (1 << index)) != 0)
                .Select(index => $"Text(\"{index}\", row: {index / 3}, column: {index % 3});"));
            var grid = Grid(2, 3, children);
            if (occupied == 63) Missing(grid, "no empty");
            else
            {
                int first = Enumerable.Range(0, 6).First(index => (occupied & (1 << index)) == 0);
                Found(grid, new(first / 3, first % 3));
            }
        }
        Found(Grid(3, 4, """Text("Left", rowSpan: 2, columnSpan: 2); Text("Top", column: 2, columnSpan: 2);"""), new(1, 2));
        Found(Grid(3, 4, """
            Text("Left", rowSpan: 2, columnSpan: 2);
            Text("Top", column: 2, columnSpan: 2);
            Text("Middle", row: 1, column: 2, columnSpan: 2);
            """), new(2, 0));
        Found(Grid(5, 1, """Text("First"); Text("Tall", row: 1, rowSpan: 2);"""), new(3, 0));
        Found(Grid(1024, 1024, """Text("Tall", rowSpan: 1024, columnSpan: 1023);"""), new(0, 1023));
        Missing(Grid(1024, 1024, """Text("Full", rowSpan: 1024, columnSpan: 1024);"""), "no empty");
        Missing(Grid(2, 2, """Text("A", columnSpan: 2); Text("B", column: 1);"""), "overlaps");
        Missing(Grid(2, 2, """Text("Outside", row: 2);"""), "outside");
        Missing(Grid(2, 2, """Text("Outside", columnSpan: 2147483647);"""), "outside");
        Missing(Grid(0, 0, ""), "no empty");
        Missing(Parse("""component Unknown { param global::Xui.GridTrack[] Tracks; view { Grid("G", rows: Tracks) { } } }"""), "unknown lengths");
        Missing(Parse("""component Dynamic { param int Row; view { Grid("G") { Text("X", row: Row); } } }"""), "expressions");
        Missing(Parse("""component Spread { param global::Xui.GridTrack[] Tracks; view { Grid("G", rows: [..Tracks]) { } } }"""), "unknown lengths");
        Missing(Parse("""component Wrong { view { VStack() { } } }"""), "Select a Grid");
        Assert(!empty.TryFindEmptyGridCell(Guid.Empty, 0, out _, out var error) && error!.Contains("stale", StringComparison.Ordinal),
            "Cell discovery rejects stale model revisions.");
        Assert(!empty.TryFindEmptyGridCell(empty.Revision, 999, out _, out error) && error!.Contains("node", StringComparison.Ordinal),
            "Cell discovery rejects nonexistent node identities.");
        using var cancelled = new CancellationTokenSource();
        cancelled.Cancel();
        Throws<OperationCanceledException>(() => empty.TryFindEmptyGridCell(empty.Revision, 0, out _, out _, cancelled.Token),
            "Cell discovery honors cancellation.");
        var insert = Grid(2, 2, """Text("First", columnSpan: 2);""");
        Assert(insert.TryFindEmptyGridCell(insert.Revision, 0, out var placement, out _), "A sparse Grid has a candidate.");
        var result = Parse(Apply(insert, insert.InsertControl(insert.Revision, 0, 1, ControlTemplate.Button, placement)));
        Assert(result.Root!.Children[1].Arguments.Single(argument => argument.Name == "row").Value == "1",
            "A discovered placement passes the real insertion compiler and Grid validation.");

        VisualDocument Grid(int rows, int columns, string children) => Parse(
            $"component Cells {{ view {{ Grid(\"Cells\", rows: [{Tracks(rows)}], columns: [{Tracks(columns)}]) {{ {children} }} }} }}");
        string Tracks(int count) => string.Join(",", Enumerable.Repeat("new()", count));
        void Found(VisualDocument document, GridPlacement expected)
        {
            string source = document.Source;
            Assert(document.TryFindEmptyGridCell(document.Revision, document.Root!.Id, out var cell, out var error) &&
                cell == expected && error is null, $"Row-major discovery must return {expected}.");
            Assert(document.Source == source, "Cell discovery never rewrites source.");
        }
        void Missing(VisualDocument document, string reason)
        {
            Assert(!document.TryFindEmptyGridCell(document.Revision, document.Root!.Id, out _, out var error) &&
                error?.Contains(reason, StringComparison.OrdinalIgnoreCase) == true, "Cell discovery reports: " + reason);
        }
    }
}
