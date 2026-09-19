using Android.Content;
using Android.Views;
using Android.Widget;
using Xui.Experimental.Portable;
using Axis = Xui.Experimental.Portable.Axis;
using NativeScrollView = Android.Widget.ScrollView;
using Stack = Xui.Experimental.Portable.Stack;

namespace Xui.Experimental.Android;

internal sealed class ElementFrame : FrameLayout
{
    private readonly Element element;
    private readonly float density;
    internal View Content { get; }

    internal ElementFrame(Context context, Element element, View content) : base(context)
    {
        this.element = element;
        density = context.Resources!.DisplayMetrics!.Density;
        Content = content;
    }

    private int Constrain(int spec, float? fixedLength, float? preferred)
    {
        var mode = MeasureSpec.GetMode(spec);
        int bound = MeasureSpec.GetSize(spec);
        if (fixedLength is float length)
        {
            int size = LayoutMath.Pixels(length, density);
            if (mode != MeasureSpecMode.Unspecified) size = Math.Min(size, bound);
            return MeasureSpec.MakeMeasureSpec(size, MeasureSpecMode.Exactly);
        }
        if (mode != MeasureSpecMode.Exactly && preferred is float wanted)
        {
            int size = LayoutMath.Pixels(wanted, density);
            if (mode != MeasureSpecMode.Unspecified) size = Math.Min(size, bound);
            return MeasureSpec.MakeMeasureSpec(size, MeasureSpecMode.Exactly);
        }
        return spec;
    }

    protected override void OnMeasure(int widthMeasureSpec, int heightMeasureSpec)
    {
        var fixedSize = element.FixedSize;
        var preferred = element.PreferredSize;
        Content.Measure(
            Constrain(widthMeasureSpec, fixedSize?.Width, preferred?.Width),
            Constrain(heightMeasureSpec, fixedSize?.Height, preferred?.Height));
        SetMeasuredDimension(Content.MeasuredWidth, Content.MeasuredHeight);
    }

    protected override void OnLayout(bool changed, int left, int top, int right, int bottom) =>
        Content.Layout(0, 0, MeasuredWidth, MeasuredHeight);
}

internal sealed class EnabledScrollView(Context context) : NativeScrollView(context)
{
    public override bool DispatchTouchEvent(MotionEvent? e) => Enabled && base.DispatchTouchEvent(e);
    public override bool OnGenericMotionEvent(MotionEvent? e) => Enabled && base.OnGenericMotionEvent(e);
    public override bool DispatchKeyEvent(KeyEvent? e) => Enabled && base.DispatchKeyEvent(e);
}

internal sealed class StackLayout : LinearLayout
{
    private readonly Stack element;
    private readonly float density;
    private readonly List<AndroidPeer> children = [];
    private bool Vertical => element.Axis == Axis.Vertical;
    private int Spacing => LayoutMath.Pixels(element.SpacingValue, density);
    private int PaddingPixels => LayoutMath.Pixels(element.PaddingValue, density);

    internal StackLayout(Context context, Stack element) : base(context)
    {
        this.element = element;
        density = context.Resources!.DisplayMetrics!.Density;
        Orientation = Vertical ? Orientation.Vertical : Orientation.Horizontal;
        ImportantForAccessibility = ImportantForAccessibility.No;
        BaselineAligned = false;
    }

    internal void Add(AndroidPeer child)
    {
        children.Add(child);
        AddView(child.View);
    }

    protected override void OnMeasure(int widthMeasureSpec, int heightMeasureSpec)
    {
        int mainSpec = Vertical ? heightMeasureSpec : widthMeasureSpec;
        int crossSpec = Vertical ? widthMeasureSpec : heightMeasureSpec;
        bool bounded = MeasureSpec.GetMode(mainSpec) != MeasureSpecMode.Unspecified;
        bool crossBounded = MeasureSpec.GetMode(crossSpec) != MeasureSpecMode.Unspecified;
        int padding = LayoutMath.Sum(PaddingPixels, PaddingPixels);
        int mainBound = Math.Max(0, MeasureSpec.GetSize(mainSpec) - padding);
        int crossBound = Math.Max(0, MeasureSpec.GetSize(crossSpec) - padding);
        var visible = children.Where(child => child.View.Visibility != ViewStates.Gone).ToArray();
        var desired = new int[visible.Length];
        var weights = new float[visible.Length];
        int cross = 0;
        for (int i = 0; i < visible.Length; i++)
        {
            var child = visible[i];
            MeasureChild(child.View, mainBound, bounded ? MeasureSpecMode.AtMost : MeasureSpecMode.Unspecified,
                crossBound, crossBounded ? MeasureSpecMode.AtMost : MeasureSpecMode.Unspecified);
            desired[i] = Vertical ? child.View.MeasuredHeight : child.View.MeasuredWidth;
            weights[i] = child.Element.Flex;
            cross = Math.Max(cross, Vertical ? child.View.MeasuredWidth : child.View.MeasuredHeight);
        }
        int naturalMain = padding;
        for (int i = 0; i < desired.Length; i++)
            naturalMain = LayoutMath.Sum(naturalMain, LayoutMath.Sum(desired[i], i == 0 ? 0 : Spacing));
        int main = MeasureSpec.GetMode(mainSpec) == MeasureSpecMode.Exactly ||
            (bounded && weights.Any(weight => weight > 0))
            ? MeasureSpec.GetSize(mainSpec)
            : bounded ? Math.Min(naturalMain, MeasureSpec.GetSize(mainSpec)) : naturalMain;
        cross = LayoutMath.Sum(cross, padding);
        if (MeasureSpec.GetMode(crossSpec) == MeasureSpecMode.Exactly) cross = MeasureSpec.GetSize(crossSpec);
        else if (crossBounded) cross = Math.Min(cross, MeasureSpec.GetSize(crossSpec));
        int[] allocation = bounded ? LayoutMath.Allocate(Math.Max(0, main - padding), Spacing, desired, weights) : desired;
        for (int i = 0; i < visible.Length; i++)
            MeasureChild(visible[i].View, allocation[i], MeasureSpecMode.Exactly,
                Math.Max(0, cross - padding), MeasureSpecMode.Exactly);
        SetMeasuredDimension(Vertical ? cross : main, Vertical ? main : cross);
    }

    private void MeasureChild(View child, int main, MeasureSpecMode mainMode, int cross, MeasureSpecMode crossMode)
    {
        int mainSpec = MeasureSpec.MakeMeasureSpec(main, mainMode);
        int crossSpec = MeasureSpec.MakeMeasureSpec(cross, crossMode);
        child.Measure(Vertical ? crossSpec : mainSpec, Vertical ? mainSpec : crossSpec);
    }

    protected override void OnLayout(bool changed, int left, int top, int right, int bottom)
    {
        int cursor = Math.Min(PaddingPixels, Vertical ? MeasuredHeight : MeasuredWidth);
        int cross = Math.Min(PaddingPixels, Vertical ? MeasuredWidth : MeasuredHeight);
        bool first = true;
        foreach (var child in children)
        {
            var view = child.View;
            if (view.Visibility == ViewStates.Gone) continue;
            if (!first) cursor = Math.Min(Vertical ? MeasuredHeight : MeasuredWidth, LayoutMath.Sum(cursor, Spacing));
            int x = Vertical ? cross : cursor;
            int y = Vertical ? cursor : cross;
            view.Layout(x, y, x + view.MeasuredWidth, y + view.MeasuredHeight);
            cursor = LayoutMath.Sum(cursor, Vertical ? view.MeasuredHeight : view.MeasuredWidth);
            first = false;
        }
    }
}
