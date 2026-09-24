#if DEBUG
using System.Text.Json;
using Xui.Experimental.Portable;

internal static class BrowserLayoutMathFixture
{
    internal static object Evaluate(string json)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        MeasureConstraint Offer(JsonElement item) => new(Enum.Parse<MeasureMode>(item.GetProperty("mode").GetString()!),
            item.GetProperty("available").GetSingle(), item.TryGetProperty("unboundedContext", out var context) && context.GetBoolean());
        float[] Numbers(JsonElement values) => values.EnumerateArray().Select(value => value.GetSingle()).ToArray();
        GridTrack[] Tracks(JsonElement values) => values.EnumerateArray().Select(track => new GridTrack(
            Enum.Parse<TrackSizing>(track.GetProperty("sizing").GetString()!),
            track.TryGetProperty("value", out var value) ? value.GetSingle() : 1,
            track.TryGetProperty("minimum", out var min) ? min.GetSingle() : 0,
            track.TryGetProperty("maximum", out var max) ? max.GetSingle() : float.MaxValue)).ToArray();
        return new
        {
            axes = root.GetProperty("axes").EnumerateArray().Select(item =>
            {
                var definition = item.GetProperty("axis");
                AxisConstraints? axis = definition.ValueKind == JsonValueKind.Null ? null : new(
                    definition.TryGetProperty("length", out var length) ? length.GetSingle() : null,
                    definition.TryGetProperty("minimum", out var min) ? min.GetSingle() : 0,
                    definition.TryGetProperty("maximum", out var max) ? max.GetSingle() : null);
                float? fixedLength = item.TryGetProperty("legacyFixed", out var f) ? f.GetSingle() : null;
                float? preferred = item.TryGetProperty("legacyPreferred", out var p) ? p.GetSingle() : null;
                var offer = LayoutMath.ConstrainMeasure(Offer(item), axis, fixedLength, preferred);
                return new { offerMode = offer.Mode.ToString(), offerSize = offer.Size, unbounded = offer.IsUnbounded,
                    value = LayoutMath.MeasureAxis(item.GetProperty("natural").GetSingle(), Offer(item), axis, fixedLength, preferred) };
            }).ToArray(),
            stacks = root.GetProperty("stacks").EnumerateArray().Select(item =>
            {
                var result = LayoutMath.AllocateStack(Offer(item), item.GetProperty("spacing").GetSingle(),
                    Numbers(item.GetProperty("desired")), Numbers(item.GetProperty("weights")));
                return new { extent = result.Extent, slots = result.Slots.Select(slot => new[] { slot.Offset, slot.Length }).ToArray() };
            }).ToArray(),
            tracks = root.GetProperty("tracks").EnumerateArray().Select(item =>
                GridLayoutMath.ResolveTracks(Tracks(item.GetProperty("definitions")),
                    item.GetProperty("demands").EnumerateArray().Select(demand => new GridDemand(
                        demand.GetProperty("start").GetInt32(), demand.GetProperty("span").GetInt32(),
                        demand.GetProperty("desired").GetSingle())).ToArray(), Offer(item))).ToArray(),
            arrangements = root.GetProperty("arrangements").EnumerateArray().Select(item =>
            {
                var allocation = Numbers(item.GetProperty("allocation"));
                var desired = item.GetProperty("desired").EnumerateArray().Select(value => new Size(value[0].GetSingle(), value[1].GetSingle())).ToArray();
                var cells = item.GetProperty("cells").EnumerateArray().Select(value =>
                    new GridPlacement(value[0].GetUInt32(), value[1].GetUInt32(), value[2].GetUInt32(), value[3].GetUInt32())).ToArray();
                var result = GridLayoutMath.Arrange(Tracks(item.GetProperty("rows")), Tracks(item.GetProperty("columns")),
                    cells, new(allocation[0], allocation[1]), item.GetProperty("unboundedWidth").GetBoolean(),
                    item.GetProperty("unboundedHeight").GetBoolean(), (index, _, _) => desired[index]);
                var nested = item.TryGetProperty("nestedStack", out var stack)
                    ? LayoutMath.AllocateStack(MeasureConstraint.Exactly(result.Cells[0].Height, result.CellContexts[0].HeightUnbounded),
                        stack.GetProperty("spacing").GetSingle(), Numbers(stack.GetProperty("desired")), Numbers(stack.GetProperty("weights")))
                    : null;
                return new { rows = result.Rows, cells = result.Cells.Select(cell => new[] { cell.X, cell.Y, cell.Width, cell.Height }).ToArray(),
                    contexts = result.CellContexts.Select(context => new[] { context.WidthUnbounded, context.HeightUnbounded }).ToArray(),
                    nested = nested?.Slots.Select(slot => new[] { slot.Offset, slot.Length }).ToArray() };
            }).ToArray()
        };
    }
}
#endif
