namespace Xui.Experimental.Portable;

public enum TrackSizing : uint { Fixed, Automatic, Star }
public readonly record struct GridTrack(TrackSizing Sizing = TrackSizing.Star, float Value = 1,
    float Minimum = 0, float Maximum = float.MaxValue);
public readonly record struct GridPlacement(uint Row, uint Column, uint RowSpan = 1, uint ColumnSpan = 1)
{
    internal void Validate(int rows, int columns)
    {
        if (Row >= rows || Column >= columns || RowSpan == 0 || ColumnSpan == 0 ||
            RowSpan > (uint)rows - Row || ColumnSpan > (uint)columns - Column)
            throw new ArgumentOutOfRangeException(nameof(GridPlacement), "The grid cell must fit inside its row and column tracks.");
    }
}

public sealed class Grid : Element
{
    private readonly string name;
    private (IReadOnlyList<GridTrack> Rows, IReadOnlyList<GridTrack> Columns) tracks =
        (Array.AsReadOnly(new[] { new GridTrack(TrackSizing.Star, 1) }), Array.AsReadOnly(new[] { new GridTrack(TrackSizing.Star, 1) }));
    public string Name => Read(name);
    public IReadOnlyList<GridTrack> Rows => Read(tracks).Rows;
    public IReadOnlyList<GridTrack> Columns => Read(tracks).Columns;

    internal Grid(Host host, string name) : base(host, ElementKind.Grid) { this.name = Values.Text(name); }

    public Grid SetTracks(ReadOnlySpan<GridTrack> rows, ReadOnlySpan<GridTrack> columns)
    {
        VerifyAccess();
        Owner.VerifyElementMutation(this);
        var nextRows = rows.ToArray();
        var nextColumns = columns.ToArray();
        GridLayoutMath.ValidateTracks(nextRows);
        GridLayoutMath.ValidateTracks(nextColumns);
        foreach (var child in Children) child.Cell!.Value.Validate(nextRows.Length, nextColumns.Length);
        if (tracks.Rows.SequenceEqual(nextRows) && tracks.Columns.SequenceEqual(nextColumns)) return this;
        Set(ref tracks, (Array.AsReadOnly(nextRows), Array.AsReadOnly(nextColumns)), ElementProperty.Tracks);
        return this;
    }

    public Grid Add(Element child, uint row = 0, uint column = 0, uint rowSpan = 1, uint columnSpan = 1)
    {
        VerifyAccess();
        var placement = new GridPlacement(row, column, rowSpan, columnSpan);
        placement.Validate(tracks.Rows.Count, tracks.Columns.Count);
        AddChild(child, 0);
        child.SetGridPlacement(placement);
        return this;
    }
}
