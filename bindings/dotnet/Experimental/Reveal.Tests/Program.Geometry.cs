using System.Text.Json;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void GeometryChecks()
    {
        using var document = JsonDocument.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Fixtures", "RevealLayoutScenarios.json")));
        Assert(document.RootElement.GetProperty("version").GetInt32() == 1, "The literal reveal geometry corpus has a supported version.");
        var cases = document.RootElement.GetProperty("cases");
        Assert(cases.GetArrayLength() == 14, "Every literal reveal geometry case runs.");
        foreach (var test in cases.EnumerateArray())
        {
            string name = test.GetProperty("name").GetString()!;
            Size ReadSize(string property)
            {
                var value = test.GetProperty(property);
                return new(value[0].GetSingle(), value[1].GetSingle());
            }
            MeasureConstraint ReadOffer(string property)
            {
                var value = test.GetProperty(property);
                return new(Enum.Parse<MeasureMode>(value[0].GetString()!), value[1].GetSingle());
            }
            var direction = Enum.Parse<RevealDirection>(test.GetProperty("direction").GetString()!);
            var presentation = new RevealPresentation(test.GetProperty("progress").GetSingle(), test.GetProperty("animating").GetBoolean());
            var natural = ReadSize("natural");
            Assert(RevealLayoutMath.Measure(natural, ReadOffer("width"), ReadOffer("height"), direction, presentation) == ReadSize("desired"),
                $"{name}: measured demand matches the literal expected extent.");
            var result = RevealLayoutMath.Arrange(natural, ReadSize("allocation"), direction, presentation);
            Assert(result.ClipSize == ReadSize("clip"), $"{name}: actual clipping never exceeds the slot or animated extent.");
            Assert(result.ContentSize == ReadSize("content"), $"{name}: the child retains its full animation-axis allocation.");
        }
        ParentAllocationChecks();
        InvalidGeometryChecks();
        RuntimeGeometryGuards();
    }

    private static void ParentAllocationChecks()
    {
        foreach (var direction in new[] { RevealDirection.Bottom, RevealDirection.Right })
        foreach (float progress in new[] { 0f, 0.5f, 1f })
        {
            bool vertical = direction == RevealDirection.Bottom;
            var natural = vertical ? new Size(120, 100) : new Size(100, 120);
            var presentation = new RevealPresentation(progress, progress is > 0 and < 1);
            var desired = RevealLayoutMath.Measure(natural,
                vertical ? MeasureConstraint.Exactly(200) : MeasureConstraint.Unspecified,
                vertical ? MeasureConstraint.Unspecified : MeasureConstraint.Exactly(200),
                direction, presentation);
            float demand = vertical ? desired.Height : desired.Width;
            var stack = LayoutMath.AllocateStack(MeasureConstraint.Unspecified, 5, [10, demand, 20], [0, 0, 0]);
            Assert(stack.Slots[1].Length == 100 * progress && stack.Slots[2].Offset == 20 + 100 * progress &&
                stack.Extent == 40 + 100 * progress,
                $"{direction}: a nonflex stack animates demand while retaining both gaps around the zero-length closed slot.");

            var crossTracks = new[] { new GridTrack(TrackSizing.Fixed, 200) };
            foreach (var (track, expectedSlot) in new[]
            {
                (new GridTrack(TrackSizing.Fixed, 160), 160f),
                (new GridTrack(TrackSizing.Star, 1), 160f),
                (new GridTrack(TrackSizing.Automatic, 0), 100 * progress),
                (new GridTrack(TrackSizing.Automatic, 0, 80), Math.Max(80, 100 * progress))
            })
            {
                var rows = vertical ? [track] : crossTracks;
                var columns = vertical ? crossTracks : [track];
                var cells = new[] { new GridPlacement(0, 0) };
                Size Measure(int _, MeasureConstraint width, MeasureConstraint height) =>
                    RevealLayoutMath.Measure(natural, width, height, direction, presentation);
                var grid = GridLayoutMath.Measure(rows, columns, cells,
                    vertical ? MeasureConstraint.Exactly(200) : MeasureConstraint.AtMost(160),
                    vertical ? MeasureConstraint.AtMost(160) : MeasureConstraint.Exactly(200), Measure);
                var placed = GridLayoutMath.Arrange(rows, columns, cells,
                    vertical ? new(200, expectedSlot) : new(expectedSlot, 200), false, false, Measure);
                var slot = placed.Cells[0];
                var clipped = RevealLayoutMath.Arrange(natural, new(slot.Width, slot.Height), direction, presentation);
                Assert((vertical ? grid.Rows[0] : grid.Columns[0]) == expectedSlot &&
                    (vertical ? slot.Height : slot.Width) == expectedSlot,
                    $"{direction}: fixed, star, auto, and minimum tracks retain their own allocation semantics.");
                Assert((vertical ? clipped.ClipSize.Height : clipped.ClipSize.Width) == 100 * progress &&
                    (vertical ? clipped.ContentSize.Height : clipped.ContentSize.Width) == 100,
                    $"{direction}: a forced grid slot cannot expand the reveal clip past progress or shrink its retained child.");
            }
        }
    }

    private static void InvalidGeometryChecks()
    {
        var presentation = new RevealPresentation(0.5f, true);
        foreach (var invalid in new[] { new Size(-1, 10), new Size(10, float.NaN), new Size(float.PositiveInfinity, 10) })
        {
            Throws<ArgumentOutOfRangeException>(() => RevealLayoutMath.Measure(invalid, MeasureConstraint.Unspecified,
                MeasureConstraint.Unspecified, RevealDirection.Bottom, presentation));
            Throws<ArgumentOutOfRangeException>(() => RevealLayoutMath.Arrange(new(100, 100), invalid, RevealDirection.Bottom, presentation));
        }
        Throws<ArgumentOutOfRangeException>(() => RevealLayoutMath.Measure(new(100, float.MaxValue),
            MeasureConstraint.Unspecified, MeasureConstraint.Unspecified, RevealDirection.Bottom, presentation));
        Throws<ArgumentOutOfRangeException>(() => RevealLayoutMath.Arrange(new(float.MaxValue, 100), new(100, 100),
            RevealDirection.Right, presentation));
        Throws<ArgumentOutOfRangeException>(() => RevealLayoutMath.Measure(new(100, 100), MeasureConstraint.Unspecified,
            MeasureConstraint.Unspecified, (RevealDirection)2, presentation));
        foreach (float invalid in new[] { -0.1f, 1.1f, float.NaN, float.PositiveInfinity })
            Throws<InvalidOperationException>(() => RevealLayoutMath.Arrange(new(100, 100), new(100, 100),
                RevealDirection.Bottom, new(invalid, true)));
        Throws<ArgumentOutOfRangeException>(() => RevealLayoutMath.Measure(new(100, 100),
            new((MeasureMode)99, 100), MeasureConstraint.Unspecified, RevealDirection.Bottom, presentation));
        Throws<ArgumentException>(() => RevealLayoutMath.Measure(new(100, 100),
            new(MeasureMode.Unspecified, 100), MeasureConstraint.Unspecified, RevealDirection.Bottom, presentation));
    }

    private static void RuntimeGeometryGuards()
    {
        using var host = new Host(new Dispatcher());
        var (reveal, _, _) = Tree(host);
        Throws<NotSupportedException>(() => ElementExtensions.FixedSize(reveal, 240, 100));
        Throws<NotSupportedException>(() => ElementExtensions.PreferredSize(reveal, 240, 100));
        Throws<NotSupportedException>(() => reveal.SetWidth(AxisConstraints.Auto));
        Throws<NotSupportedException>(() => reveal.SetHeight(new(null, 1)));
        Throws<NotSupportedException>(() => reveal.SetConstraints(new(0), null));
        Throws<NotSupportedException>(() => reveal.SetConstraints(null, new(null, 0, 0)));
        Assert(reveal.FixedSize is null && reveal.PreferredSize is null &&
            reveal.WidthConstraints is null && reveal.HeightConstraints is null,
            "Rejected outer sizing cannot leave fields that later override animated extent.");
        reveal.SetConstraints(null, null).SetWidth(null).SetHeight(null);
        Assert(reveal.WidthConstraints is null && reveal.HeightConstraints is null,
            "Clearing inherited axis constraints remains a harmless supported no-op.");
        reveal.Content.SetWidth(AxisConstraints.Fixed(240));
        Assert(reveal.Content.WidthConstraints == AxisConstraints.Fixed(240),
            "The retained child still supports bounded sizing independently of its reveal wrapper.");
        var backend = new Backend();
        host.Attach(backend);
        Throws<NotSupportedException>(() => reveal.SetWidth(AxisConstraints.Auto));
        Assert(host.IsAttached && backend.Find(reveal).Updates.Count == 0,
            "Attached outer-constraint rejection occurs before native update or attachment failure.");

        using var buildingHost = new Host(new Dispatcher());
        using var build = buildingHost.BeginBuild();
        var root = buildingHost.Stack(Axis.Vertical);
        var child = buildingHost.Label("Bounded reveal content");
        var unattached = buildingHost.Reveal(child, "Flex guard");
        foreach (float invalid in new[] { float.NaN, float.PositiveInfinity, -1f })
            Throws<ArgumentOutOfRangeException>(() => root.Add(unattached, invalid));
        foreach (float positive in new[] { 0.5f, 1f })
            Throws<NotSupportedException>(() => root.Add(unattached, positive));
        Assert(root.Children.Count == 0 && unattached.Parent is null && unattached.Flex == 0,
            "Invalid or conflicting flex is rejected before the parent adopts the reveal.");
        root.Add(unattached, 0);
        Assert(ReferenceEquals(unattached.Parent, root) && unattached.Flex == 0,
            "A zero-flex reveal retains ordinary stack participation and spacing.");
        buildingHost.SetContent(root);
        build.Complete();
    }
}
