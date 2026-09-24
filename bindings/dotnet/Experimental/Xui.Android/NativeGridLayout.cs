using Android.Content;
using Android.Views;
using Xui.Experimental.Portable;
using Grid = Xui.Experimental.Portable.Grid;
using Size = Xui.Experimental.Portable.Size;

namespace Xui.Experimental.Android;

internal sealed class NativeGridLayout : ViewGroup, IContextMeasure
{
    private readonly Grid element;
    private readonly float density;
    private readonly NativeMeasureContext measureContext = new();
    private readonly List<AndroidPeer> children = [];
    private GridLayoutResult? layout;

    internal NativeGridLayout(Context context, Grid element) : base(context)
    {
        this.element = element;
        density = context.Resources!.DisplayMetrics!.Density;
        ImportantForAccessibility = ImportantForAccessibility.No;
    }

    internal void Add(AndroidPeer child)
    {
        if (child.Element.Cell is null) throw new ArgumentException("The native grid child has no cell placement.", nameof(child));
        AddView(child.View, new LayoutParams(LayoutParams.MatchParent, LayoutParams.MatchParent));
        children.Add(child);
    }

    public void MeasureWith(MeasureConstraint width, MeasureConstraint height) => measureContext.Measure(this, width, height);

    private GridTrack[] Tracks(IReadOnlyList<GridTrack> tracks) => tracks.Select(track => new GridTrack(
        track.Sizing, track.Sizing == TrackSizing.Fixed ? LayoutMath.Pixels(track.Value, density) : track.Value,
        LayoutMath.Pixels(track.Minimum, density), LayoutMath.Pixels(track.Maximum, density))).ToArray();

    protected override void OnMeasure(int widthMeasureSpec, int heightMeasureSpec)
    {
        var (width, height) = measureContext.Read(widthMeasureSpec, heightMeasureSpec);
        var rows = Tracks(element.Rows);
        var columns = Tracks(element.Columns);
        var cells = children.Select(child => child.Element.Cell
            ?? throw new InvalidOperationException("A native grid cell lost its placement.")).ToArray();
        Size MeasureChild(int index, MeasureConstraint childWidth, MeasureConstraint childHeight)
        {
            var child = children[index].View;
            if (child.Visibility == ViewStates.Gone) return new(0, 0);
            child.MeasureWith(childWidth, childHeight);
            return new(child.MeasuredWidth, child.MeasuredHeight);
        }
        var measured = GridLayoutMath.Measure(rows, columns, cells, width, height, MeasureChild);
        int measuredWidth = NativeMeasure.Allocation(measured.Size.Width);
        int measuredHeight = NativeMeasure.Allocation(measured.Size.Height);
        layout = GridLayoutMath.Arrange(rows, columns, cells, new(measuredWidth, measuredHeight),
            measured.WidthUnbounded, measured.HeightUnbounded, MeasureChild);
        for (int i = 0; i < children.Count; i++)
        {
            if (children[i].View.Visibility == ViewStates.Gone) continue;
            var cell = layout.Cells[i];
            var context = layout.CellContexts[i];
            children[i].View.MeasureWith(
                MeasureConstraint.Exactly(NativeMeasure.Allocation(cell.Width), context.WidthUnbounded),
                MeasureConstraint.Exactly(NativeMeasure.Allocation(cell.Height), context.HeightUnbounded));
        }
        SetMeasuredDimension(measuredWidth, measuredHeight);
    }

    protected override void OnLayout(bool changed, int left, int top, int right, int bottom)
    {
        if (layout is null) throw new InvalidOperationException("The native grid must be measured before layout.");
        for (int i = 0; i < children.Count; i++)
        {
            var child = children[i].View;
            if (child.Visibility == ViewStates.Gone) continue;
            var cell = layout.Cells[i];
            int x = NativeMeasure.Allocation(cell.X);
            int y = NativeMeasure.Allocation(cell.Y);
            child.Layout(x, y, x + child.MeasuredWidth, y + child.MeasuredHeight);
        }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            children.Clear();
            layout = null;
        }
        base.Dispose(disposing);
    }
}
