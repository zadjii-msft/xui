namespace Xui.Experimental.Portable;

public readonly record struct GridDemand(int Start, int Span, float Desired);
public readonly record struct Rect(float X, float Y, float Width, float Height);
public readonly record struct GridCellContext(bool WidthUnbounded, bool HeightUnbounded);

public sealed class GridLayoutResult
{
    public Size Size { get; }
    public IReadOnlyList<float> Rows { get; }
    public IReadOnlyList<float> Columns { get; }
    public IReadOnlyList<Rect> Cells { get; }
    public IReadOnlyList<GridCellContext> CellContexts { get; }
    public bool WidthUnbounded { get; }
    public bool HeightUnbounded { get; }
    internal GridLayoutResult(Size size, float[] rows, float[] columns, Rect[] cells, GridCellContext[] cellContexts, bool widthUnbounded, bool heightUnbounded)
    {
        Size = size;
        Rows = Array.AsReadOnly(rows);
        Columns = Array.AsReadOnly(columns);
        Cells = Array.AsReadOnly(cells);
        CellContexts = Array.AsReadOnly(cellContexts);
        WidthUnbounded = widthUnbounded;
        HeightUnbounded = heightUnbounded;
    }
}

public static class GridLayoutMath
{
    internal static void ValidateTracks(IReadOnlyList<GridTrack> tracks)
    {
        if (tracks.Count is < 1 or > 256) throw new ArgumentException("A grid requires 1 to 256 tracks per axis.");
        foreach (var track in tracks)
        {
            if (!Enum.IsDefined(track.Sizing)) throw new ArgumentOutOfRangeException(nameof(tracks), "Unknown grid track sizing.");
            Values.Length(track.Value);
            Values.Length(track.Minimum);
            Values.Length(track.Maximum);
            if (track.Minimum > track.Maximum || (track.Sizing == TrackSizing.Star && track.Value == 0))
                throw new ArgumentException("Grid tracks require ordered bounds and positive star weights.");
        }
    }

    public static float[] ResolveTracks(IReadOnlyList<GridTrack> definitions, IReadOnlyList<GridDemand> demands, MeasureConstraint offered)
    {
        ArgumentNullException.ThrowIfNull(definitions);
        ArgumentNullException.ThrowIfNull(demands);
        offered.Validate();
        var tracks = definitions.ToArray();
        ValidateTracks(tracks);
        var intrinsic = tracks.Select(track => (double)(track.Sizing == TrackSizing.Fixed
            ? Math.Clamp(track.Value, track.Minimum, track.Maximum) : track.Minimum)).ToArray();
        var ordered = demands.ToArray();
        foreach (var demand in ordered)
        {
            Values.Length(demand.Desired);
            if (demand.Start < 0 || demand.Span <= 0 || demand.Start >= tracks.Length || demand.Span > tracks.Length - demand.Start)
                throw new ArgumentOutOfRangeException(nameof(demands), "A grid demand must fit inside the tracks.");
        }
        foreach (var demand in ordered.OrderBy(demand => demand.Span))
        {
            double covered = 0;
            for (int i = demand.Start; i < demand.Start + demand.Span; i++) covered += intrinsic[i];
            var eligible = Enumerable.Range(demand.Start, demand.Span).Where(i => tracks[i].Sizing != TrackSizing.Fixed).ToArray();
            Grow(intrinsic, tracks, eligible, Math.Max(0, demand.Desired - covered), weighted: false);
        }
        var result = intrinsic;
        if (!offered.IsUnbounded)
        {
            result = intrinsic.Select((size, i) => tracks[i].Sizing == TrackSizing.Star ? tracks[i].Minimum : size).ToArray();
            Grow(result, tracks, Enumerable.Range(0, tracks.Length).Where(i => tracks[i].Sizing == TrackSizing.Star).ToArray(),
                Math.Max(0, offered.Size - result.Sum()), weighted: true);
        }
        return result.Select(size => (float)size).ToArray();
    }

    private static void Grow(double[] sizes, GridTrack[] tracks, int[] eligible, double remaining, bool weighted)
    {
        for (int pass = 0; pass < tracks.Length && remaining > 0; pass++)
        {
            var active = eligible.Where(i => sizes[i] < tracks[i].Maximum).ToArray();
            if (active.Length == 0) break;
            double weight = active.Sum(i => weighted ? (double)tracks[i].Value : 1);
            double consumed = 0;
            foreach (int i in active)
            {
                double addition = Math.Min(tracks[i].Maximum - sizes[i], remaining * (weighted ? tracks[i].Value : 1) / weight);
                sizes[i] += addition;
                consumed += addition;
            }
            if (consumed <= 0) break;
            remaining = Math.Max(0, remaining - consumed);
        }
    }

    public static GridLayoutResult Measure(IReadOnlyList<GridTrack> rows, IReadOnlyList<GridTrack> columns,
        IReadOnlyList<GridPlacement> cells, MeasureConstraint width, MeasureConstraint height,
        Func<int, MeasureConstraint, MeasureConstraint, Size> measureChild) =>
        Compute(rows, columns, cells, width, height, null, measureChild);

    public static GridLayoutResult Arrange(IReadOnlyList<GridTrack> rows, IReadOnlyList<GridTrack> columns,
        IReadOnlyList<GridPlacement> cells, Size allocation, bool widthWasUnbounded, bool heightWasUnbounded,
        Func<int, MeasureConstraint, MeasureConstraint, Size> measureChild)
    {
        Values.Size(allocation.Width, allocation.Height);
        return Compute(rows, columns, cells,
            MeasureConstraint.Exactly(allocation.Width, widthWasUnbounded),
            MeasureConstraint.Exactly(allocation.Height, heightWasUnbounded),
            allocation, measureChild);
    }

    private static GridLayoutResult Compute(IReadOnlyList<GridTrack> rows, IReadOnlyList<GridTrack> columns,
        IReadOnlyList<GridPlacement> cells, MeasureConstraint width, MeasureConstraint height, Size? allocation,
        Func<int, MeasureConstraint, MeasureConstraint, Size> measureChild)
    {
        ArgumentNullException.ThrowIfNull(rows);
        ArgumentNullException.ThrowIfNull(columns);
        ArgumentNullException.ThrowIfNull(cells);
        ArgumentNullException.ThrowIfNull(measureChild);
        width.Validate();
        height.Validate();
        var rowDefinitions = rows.ToArray();
        var columnDefinitions = columns.ToArray();
        var placements = cells.ToArray();
        ValidateTracks(rowDefinitions);
        ValidateTracks(columnDefinitions);
        foreach (var cell in placements) cell.Validate(rowDefinitions.Length, columnDefinitions.Length);
        var columnDemands = new GridDemand[placements.Length];
        var rowDemands = new GridDemand[placements.Length];
        var contexts = placements.Select(cell => new GridCellContext(
            width.IsUnbounded && !AllFixed(columnDefinitions, cell.Column, cell.ColumnSpan),
            height.IsUnbounded && !AllFixed(rowDefinitions, cell.Row, cell.RowSpan))).ToArray();
        MeasureConstraint Intrinsic(MeasureConstraint offer, bool context) => offer.Mode == MeasureMode.Unspecified
            ? offer : MeasureConstraint.AtMost(offer.Size, context);
        MeasureConstraint HeightOffer(int index) => height.IsUnbounded && !contexts[index].HeightUnbounded
            ? FixedOffer(rowDefinitions, placements[index].Row, placements[index].RowSpan, height)
            : Intrinsic(height, contexts[index].HeightUnbounded);
        Size MeasureChild(int index, MeasureConstraint w)
        {
            var desired = measureChild(index, w, HeightOffer(index));
            Values.Size(desired.Width, desired.Height);
            return desired;
        }
        for (int i = 0; i < placements.Length; i++)
        {
            var offer = width.IsUnbounded && !contexts[i].WidthUnbounded
                ? FixedOffer(columnDefinitions, placements[i].Column, placements[i].ColumnSpan, width)
                : Intrinsic(width, contexts[i].WidthUnbounded);
            columnDemands[i] = new((int)placements[i].Column, (int)placements[i].ColumnSpan, MeasureChild(i, offer).Width);
        }
        var columnSizes = ResolveTracks(columnDefinitions, columnDemands, width);
        float extentWidth = allocation?.Width ?? Extent(columnSizes, width);
        for (int i = 0; i < placements.Length; i++)
        {
            var span = Span(columnSizes, (int)placements[i].Column, (int)placements[i].ColumnSpan, extentWidth);
            rowDemands[i] = new((int)placements[i].Row, (int)placements[i].RowSpan,
                MeasureChild(i, MeasureConstraint.Exactly(span.Length, contexts[i].WidthUnbounded)).Height);
        }
        var rowSizes = ResolveTracks(rowDefinitions, rowDemands, height);
        float extentHeight = allocation?.Height ?? Extent(rowSizes, height);
        var rectangles = new Rect[placements.Length];
        for (int i = 0; i < placements.Length; i++)
        {
            var x = Span(columnSizes, (int)placements[i].Column, (int)placements[i].ColumnSpan, extentWidth);
            var y = Span(rowSizes, (int)placements[i].Row, (int)placements[i].RowSpan, extentHeight);
            rectangles[i] = new(x.Offset, y.Offset, x.Length, y.Length);
        }
        return new(new(extentWidth, extentHeight), rowSizes, columnSizes, rectangles, contexts,
            width.IsUnbounded, height.IsUnbounded);
    }

    private static bool AllFixed(GridTrack[] tracks, uint start, uint count) =>
        tracks.Skip((int)start).Take((int)count).All(track => track.Sizing == TrackSizing.Fixed);

    private static MeasureConstraint FixedOffer(GridTrack[] tracks, uint start, uint count, MeasureConstraint parent)
    {
        double total = tracks.Skip((int)start).Take((int)count)
            .Sum(track => (double)Math.Clamp(track.Value, track.Minimum, track.Maximum));
        if (parent.Mode != MeasureMode.Unspecified) total = Math.Min(total, parent.Size);
        if (total > float.MaxValue) throw new OverflowException("The fixed cell span exceeds finite float coordinates.");
        return MeasureConstraint.Exactly((float)total);
    }

    private static float Extent(float[] sizes, MeasureConstraint offered)
    {
        double natural = sizes.Sum(size => (double)size);
        if (offered.Mode == MeasureMode.Exactly) return offered.Size;
        if (offered.Mode == MeasureMode.AtMost) return (float)Math.Min(natural, offered.Size);
        if (natural > float.MaxValue) throw new OverflowException("The unbounded grid exceeds finite float coordinates.");
        return (float)natural;
    }

    private static LayoutSlot Span(float[] sizes, int start, int count, float extent)
    {
        double offset = 0, length = 0;
        for (int i = 0; i < start; i++) offset += sizes[i];
        for (int i = start; i < start + count; i++) length += sizes[i];
        float position = Math.Min((float)offset, extent);
        double remaining = Math.Max(0, (double)extent - position);
        float allocation = (float)Math.Min(length, remaining);
        if (allocation > remaining) allocation = MathF.BitDecrement(allocation);
        return new(position, allocation);
    }
}
