using System.Text.Json;
using PortableLayout;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void GridLayoutChecks()
    {
        using var corpus = JsonDocument.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Fixtures", "GridLayoutScenarios.json")));
        Assert(corpus.RootElement.GetProperty("version").GetInt32() == 1, "Grid corpus version.");
        GridTrack[] ReadTracks(JsonElement definitions) => definitions.EnumerateArray().Select(track => new GridTrack(
            Enum.Parse<TrackSizing>(track.GetProperty("sizing").GetString()!),
            track.TryGetProperty("value", out var value) ? value.GetSingle() : 1,
            track.TryGetProperty("minimum", out var minimum) ? minimum.GetSingle() : 0,
            track.TryGetProperty("maximum", out var maximum) ? maximum.GetSingle() : float.MaxValue)).ToArray();
        foreach (var item in corpus.RootElement.GetProperty("tracks").EnumerateArray())
        {
            var definitions = ReadTracks(item.GetProperty("definitions"));
            var demands = item.GetProperty("demands").EnumerateArray().Select(demand => new GridDemand(
                demand.GetProperty("start").GetInt32(), demand.GetProperty("span").GetInt32(), demand.GetProperty("desired").GetSingle())).ToArray();
            var offered = new MeasureConstraint(Enum.Parse<MeasureMode>(item.GetProperty("mode").GetString()!), item.GetProperty("available").GetSingle(),
                item.TryGetProperty("unboundedContext", out var context) && context.GetBoolean());
            var actual = GridLayoutMath.ResolveTracks(definitions, demands, offered);
            Assert(actual.SequenceEqual(item.GetProperty("expected").EnumerateArray().Select(value => value.GetSingle())), item.GetProperty("name").GetString() + " literal track result.");
        }
        foreach (var item in corpus.RootElement.GetProperty("arrangements").EnumerateArray())
        {
            var cells = item.GetProperty("cells").EnumerateArray().Select(value =>
                new GridPlacement(value[0].GetUInt32(), value[1].GetUInt32(), value[2].GetUInt32(), value[3].GetUInt32())).ToArray();
            var desired = item.GetProperty("desired").EnumerateArray().Select(value => new Size(value[0].GetSingle(), value[1].GetSingle())).ToArray();
            var allocation = item.GetProperty("allocation");
            bool nestedStack = item.TryGetProperty("nestedStack", out var stack);
            StackLayoutResult? measuredStack = null;
            float[] stackDesired = nestedStack ? stack.GetProperty("desired").EnumerateArray().Select(v => v.GetSingle()).ToArray() : [];
            float[] weights = nestedStack ? stack.GetProperty("weights").EnumerateArray().Select(v => v.GetSingle()).ToArray() : [];
            float spacing = nestedStack ? stack.GetProperty("spacing").GetSingle() : 0;
            Size MeasureNested(int index, MeasureConstraint _, MeasureConstraint height)
            {
                if (!nestedStack) return desired[index];
                measuredStack = LayoutMath.AllocateStack(height, spacing, stackDesired, weights);
                return new(desired[index].Width, measuredStack.Extent);
            }
            var actual = GridLayoutMath.Arrange(ReadTracks(item.GetProperty("rows")), ReadTracks(item.GetProperty("columns")),
                cells, new(allocation[0].GetSingle(), allocation[1].GetSingle()), item.GetProperty("unboundedWidth").GetBoolean(),
                item.GetProperty("unboundedHeight").GetBoolean(), MeasureNested);
            var expectedCells = item.GetProperty("expectedCells").EnumerateArray().Select(value =>
                new Rect(value[0].GetSingle(), value[1].GetSingle(), value[2].GetSingle(), value[3].GetSingle()));
            Assert(actual.Rows.SequenceEqual(item.GetProperty("expectedRows").EnumerateArray().Select(value => value.GetSingle())) &&
                actual.Cells.SequenceEqual(expectedCells), item.GetProperty("name").GetString() + " literal arranged geometry.");
            Assert(actual.CellContexts.SequenceEqual(item.GetProperty("expectedContexts").EnumerateArray()
                .Select(value => new GridCellContext(value[0].GetBoolean(), value[1].GetBoolean()))), "Literal per-cell context boundaries.");
            if (nestedStack)
            {
                var expected = item.GetProperty("expectedNestedSlots").EnumerateArray().Select(value => new LayoutSlot(value[0].GetSingle(), value[1].GetSingle())).ToArray();
                var arrangedStack = LayoutMath.AllocateStack(MeasureConstraint.Exactly(actual.Cells[0].Height, actual.CellContexts[0].HeightUnbounded),
                    spacing, stackDesired, weights);
                Assert(measuredStack!.Slots.SequenceEqual(expected) && arrangedStack.Slots.SequenceEqual(expected), "Fixed versus mixed cell context controls nested flex without losing finite clipping.");
            }
        }
        var auto = new GridTrack(TrackSizing.Automatic, 1);
        var star = new GridTrack(TrackSizing.Star, 1);
        int calls = 0;
        var clipped = GridLayoutMath.Measure([auto], [new(TrackSizing.Fixed, 280)], [new(0, 0)],
            MeasureConstraint.AtMost(100), MeasureConstraint.Unspecified, (_, width, _) =>
            {
                calls++;
                if (calls == 2) Assert(width == MeasureConstraint.Exactly(100), "Row intrinsic height uses the actual parent-clipped cell width.");
                return new(280, width.Size <= 100 ? 80 : 40);
            });
        Assert(calls == 2 && clipped.Size == new Size(100, 80) && clipped.Cells[0] == new Rect(0, 0, 100, 80), "Narrow parent preserves the correctly remeasured caption height.");
        var pressure = GridLayoutMath.Measure([new(TrackSizing.Fixed, 80), auto],
            [new(TrackSizing.Fixed, 100), auto], [new(0, 0), new(1, 1)], MeasureConstraint.Exactly(50), MeasureConstraint.Exactly(40),
            (_, _, _) => new(100, 100));
        Assert(pressure.Cells[0] == new Rect(0, 0, 50, 40) && pressure.Cells[1] == new Rect(50, 40, 0, 0), "Under pressure even zero-sized cell origins stay inside the parent.");
        var rowSpan = GridLayoutMath.Measure([new(TrackSizing.Fixed, 20), auto], [star], [new(0, 0, 2, 1)],
            MeasureConstraint.Exactly(100), MeasureConstraint.Unspecified, (_, _, _) => new(100, 100));
        Assert(rowSpan.Rows.SequenceEqual([20f, 80f]) && rowSpan.Cells[0] == new Rect(0, 0, 100, 100), "Row spans distribute only their deficit to nonfixed tracks.");
        GridTrack[] scrollRows = [new(TrackSizing.Star, 1), new(TrackSizing.Star, 3)];
        GridPlacement[] scrollCells = [new(0, 0), new(1, 0)];
        Size ScrollMeasure(int index, MeasureConstraint _, MeasureConstraint __) => new(100, index == 0 ? 40 : 80);
        var measuredScroll = GridLayoutMath.Measure(scrollRows, [star], scrollCells, MeasureConstraint.Exactly(100),
            MeasureConstraint.Unspecified, ScrollMeasure);
        var arrangedScroll = GridLayoutMath.Arrange(scrollRows, [star], scrollCells, new Size(100, 200),
            measuredScroll.WidthUnbounded, measuredScroll.HeightUnbounded, ScrollMeasure);
        Assert(measuredScroll.HeightUnbounded && arrangedScroll.Rows.SequenceEqual([40f, 80f]) &&
            arrangedScroll.Cells[1] == new Rect(0, 40, 100, 80), "Finite scroll arrangement retains unbounded star-as-auto context.");
        var boundedRows = GridLayoutMath.Arrange(scrollRows, [star], scrollCells, new Size(100, 200), false, false, ScrollMeasure);
        Assert(boundedRows.Rows.SequenceEqual([50f, 150f]), "Bounded arrangement still allocates weighted star shares.");
        var clippedNatural = GridLayoutMath.Arrange([auto], [new(TrackSizing.Fixed, 280)], [new(0, 0)],
            new Size(100, 200), true, true, (_, width, _) => new(280, width.Size == 100 ? 80 : 40));
        Assert(clippedNatural.Cells[0] == new Rect(0, 0, 100, 80), "Unbounded context still measures row height at actual finite arrangement width.");
        var independentAxes = GridLayoutMath.Arrange([auto], [new(TrackSizing.Fixed, 200)], [new(0, 0)],
            new Size(200, 100), true, true, (_, width, height) =>
            {
                Assert(!width.IsUnbounded && height.IsUnbounded, "An all-fixed column clears only horizontal context.");
                var horizontal = LayoutMath.AllocateStack(width, 8, [32, 48], [1, 3]);
                var vertical = LayoutMath.AllocateStack(height, 8, [10, 20], [1, 1]);
                Assert(horizontal.Slots.SequenceEqual([new LayoutSlot(0, 48), new(56, 144)]) &&
                    vertical.Slots.SequenceEqual([new LayoutSlot(0, 10), new(18, 20)]), "Nested horizontal and vertical stacks inherit independent budgets.");
                var definite = LayoutMath.ConstrainMeasure(height, AxisConstraints.Fixed(80));
                Assert(!definite.IsUnbounded && LayoutMath.AllocateStack(definite, 8, [10, 20], [1, 1]).Slots[0].Length == 36,
                    "An explicit child length ends inherited intrinsic context.");
                return new(horizontal.Extent, vertical.Extent);
            });
        Assert(independentAxes.CellContexts[0] == new GridCellContext(false, true), "Cell context metadata is axis-specific.");
        Throws<NotSupportedException>(() => ((IList<GridCellContext>)independentAxes.CellContexts)[0] = default);
        Throws<ArgumentException>(() => GridLayoutMath.ResolveTracks([], [], MeasureConstraint.Unspecified));
        Throws<ArgumentException>(() => GridLayoutMath.ResolveTracks(Enumerable.Repeat(star, 257).ToArray(), [], MeasureConstraint.Unspecified));
        foreach (var invalid in new[]
        {
            new GridTrack((TrackSizing)99), new GridTrack(TrackSizing.Star, 0), new GridTrack(TrackSizing.Fixed, -1),
            new GridTrack(TrackSizing.Automatic, float.NaN), new GridTrack(TrackSizing.Automatic, 1, 30, 20),
            new GridTrack(TrackSizing.Star, 1, 0, float.PositiveInfinity)
        })
            Throws<ArgumentException>(() => GridLayoutMath.ResolveTracks([invalid], [], MeasureConstraint.Unspecified));
        foreach (var invalid in new[] { new GridDemand(-1, 1, 10), new GridDemand(0, 0, 10), new GridDemand(0, 2, 10), new GridDemand(0, 1, float.NaN) })
            Throws<ArgumentException>(() => GridLayoutMath.ResolveTracks([auto], [invalid], MeasureConstraint.Unspecified));
        Throws<ArgumentOutOfRangeException>(() => GridLayoutMath.Measure([auto], [auto], [new(0, 0, 0, 1)],
            MeasureConstraint.Unspecified, MeasureConstraint.Unspecified, (_, _, _) => new(1, 1)));
        Throws<ArgumentOutOfRangeException>(() => GridLayoutMath.Measure([auto], [auto], [new(0, 0)],
            MeasureConstraint.Unspecified, MeasureConstraint.Unspecified, (_, _, _) => new(float.NaN, 1)));
        Throws<OverflowException>(() => GridLayoutMath.Measure([auto], [new(TrackSizing.Fixed, float.MaxValue), new(TrackSizing.Fixed, float.MaxValue)],
            [], MeasureConstraint.Unspecified, MeasureConstraint.Unspecified, (_, _, _) => new(1, 1)));
        for (int width = 0; width < 80; width++)
        {
            var result = GridLayoutMath.Measure([auto, star], [new(TrackSizing.Fixed, 25), auto, star],
                [new(0, 0, 1, 2), new(1, 1, 1, 2)], MeasureConstraint.Exactly(width / 3f), MeasureConstraint.Exactly(30),
                (_, _, _) => new(120, 50));
            Assert(result.Cells.All(cell => cell.X >= 0 && cell.Y >= 0 && cell.Width >= 0 && cell.Height >= 0 &&
                (double)cell.X + cell.Width <= result.Size.Width && (double)cell.Y + cell.Height <= result.Size.Height), "Fractional grid placement stays finite and inside parent bounds.");
        }

        using var host = new Host(new Dispatcher());
        var demo = new GridSizingShowcase(host);
        Assert(demo.Layout.Kind == ElementKind.Grid && demo.Input.Cell == new GridPlacement(0, 1, 1, 2), "Generated grid retains explicit fixed cell metadata.");
        Assert(demo.Sidebar.Cell == new GridPlacement(0, 0, 2, 1) && demo.Layout.Rows.Count == 3 && demo.Layout.Columns.Count == 3, "Generated row/column spans and track arrays.");
        var backend = new MutationBackend();
        host.Attach(backend);
        var children = demo.Layout.Children.ToArray();
        var input = backend.Find("grid-input");
        input.Events.Change("Retained grid draft");
        backend.Find("grid-compact").Events.Click();
        Assert(demo.Layout.Columns[0].Value == 40 && demo.Layout.Children.SequenceEqual(children) &&
            ReferenceEquals(input, backend.Find("grid-input")) && demo.Draft == "Retained grid draft", "Atomic track changes preserve child controls, editor identity, and draft.");
        backend.Find("grid-toggle-note").Events.Click();
        Assert(demo.Layout.Children.SequenceEqual(children) && demo.NoteView.Children.Count == 1, "An ordinary keyed stack can mutate inside a fixed grid cell.");
        backend.Find("grid-toggle-note").Events.Click();
        Assert(demo.NoteView.Children.Count == 0 && demo.Layout.Children.SequenceEqual(children), "Nested keyed removal leaves static grid placement untouched.");
        var previousRows = demo.Layout.Rows.ToArray();
        var previousColumns = demo.Layout.Columns.ToArray();
        Throws<ArgumentOutOfRangeException>(() => demo.Layout.SetTracks([auto], [auto]));
        Assert(host.IsAttached && demo.Layout.Rows.SequenceEqual(previousRows) && demo.Layout.Columns.SequenceEqual(previousColumns), "Shrinking tracks past an existing cell rejects before either axis changes.");
        Throws<ArgumentException>(() => demo.Layout.SetTracks([new(TrackSizing.Star, 0)], previousColumns));
        Throws<InvalidOperationException>(() => demo.Layout.Add(demo.Input));
        var defensive = previousColumns.ToArray();
        demo.Layout.SetTracks(previousRows, defensive);
        defensive[0] = new(TrackSizing.Fixed, 999);
        Assert(demo.Layout.Columns[0].Value == 40, "Grid retains a defensive track snapshot, not caller-owned arrays.");
        var earlier = demo.Columns;
        backend.Failure = "update";
        Throws<ApplicationException>(() => demo.Columns =
        [
            new(TrackSizing.Fixed, 80), new(TrackSizing.Automatic, 1), new(TrackSizing.Star, 1)
        ]);
        Assert(!host.IsAttached && demo.Layout.Columns[0].Value == 80, "Native track update failure retains the committed grid snapshot.");
        demo.Columns = earlier;
        Assert(demo.Layout.Columns[0].Value == 40, "A previously cached track array still restores the model after a native failure.");
        var recovery = new MutationBackend();
        host.Attach(recovery);
        Assert(demo.Input.Text == "Retained grid draft", "Grid reattachment retains application input.");

        using var invalidHost = new Host(new Dispatcher());
        using (var build = invalidHost.BeginBuild())
        {
            var root = invalidHost.Stack(Axis.Vertical);
            var grid = invalidHost.Grid("Validation");
            var label = invalidHost.Label("Child");
            Throws<ArgumentOutOfRangeException>(() => grid.Add(label, rowSpan: 0));
            Throws<ArgumentOutOfRangeException>(() => grid.Add(label, uint.MaxValue));
            Assert(label.Parent is null && label.Cell is null, "Invalid cell placement does not partially adopt its child.");
            grid.Add(label);
            root.Add(grid);
            invalidHost.SetContent(root);
            build.Complete();
        }
        Console.WriteLine("Grid layout: 10 literal track cases, 5 arrangement cases, width-first measurement, bounded geometry, spans, and retained placement passed.");
    }
}
