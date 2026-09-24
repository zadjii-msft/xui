using Android.Content;
using Android.Views;
using Android.Widget;
using Xui.Experimental.Portable;
using Axis = Xui.Experimental.Portable.Axis;
using NativeScrollView = Android.Widget.ScrollView;
using Stack = Xui.Experimental.Portable.Stack;
using PortableMath = Xui.Experimental.Portable.LayoutMath;

namespace Xui.Experimental.Android;

internal sealed class ElementFrame : FrameLayout, IContextMeasure
{
    private readonly Element element;
    private readonly float density;
    private readonly NativeMeasureContext measureContext = new();
    internal View Content { get; }

    internal ElementFrame(Context context, Element element, View content) : base(context)
    {
        this.element = element;
        density = context.Resources!.DisplayMetrics!.Density;
        Content = content;
    }

    public void MeasureWith(MeasureConstraint width, MeasureConstraint height) => measureContext.Measure(this, width, height);

    protected override void OnMeasure(int widthMeasureSpec, int heightMeasureSpec)
    {
        var fixedSize = element.FixedSize;
        var preferred = element.PreferredSize;
        var (width, height) = measureContext.Read(widthMeasureSpec, heightMeasureSpec);
        var widthConstraints = NativeMeasure.Scale(element.WidthConstraints, density);
        var heightConstraints = NativeMeasure.Scale(element.HeightConstraints, density);
        float? fixedWidth = NativeMeasure.Length(fixedSize?.Width, density);
        float? fixedHeight = NativeMeasure.Length(fixedSize?.Height, density);
        float? preferredWidth = NativeMeasure.Length(preferred?.Width, density);
        float? preferredHeight = NativeMeasure.Length(preferred?.Height, density);
        var contentWidth = PortableMath.ConstrainMeasure(width, widthConstraints, fixedWidth, preferredWidth);
        var contentHeight = PortableMath.ConstrainMeasure(height, heightConstraints, fixedHeight, preferredHeight);
        NativeMeasure.Content(Content, contentWidth, contentHeight);
        int measuredWidth = NativeMeasure.Allocation(PortableMath.MeasureAxis(Content.MeasuredWidth, width, widthConstraints, fixedWidth, preferredWidth));
        if (Content.MeasuredWidth != measuredWidth)
            NativeMeasure.Content(Content, MeasureConstraint.Exactly(measuredWidth, contentWidth.IsUnbounded), contentHeight);
        int measuredHeight = NativeMeasure.Allocation(PortableMath.MeasureAxis(Content.MeasuredHeight, height, heightConstraints, fixedHeight, preferredHeight));
        if (Content.MeasuredWidth != measuredWidth || Content.MeasuredHeight != measuredHeight)
            NativeMeasure.Content(Content, MeasureConstraint.Exactly(measuredWidth, contentWidth.IsUnbounded),
                MeasureConstraint.Exactly(measuredHeight, contentHeight.IsUnbounded));
        SetMeasuredDimension(measuredWidth, measuredHeight);
    }

    protected override void OnLayout(bool changed, int left, int top, int right, int bottom)
    {
        if (Content is EnabledScrollView { HasVirtualViewport: true } scroll)
        {
            scroll.ArrangeVirtualViewport(MeasuredWidth, MeasuredHeight);
            Content.Layout(0, 0, Content.MeasuredWidth, Content.MeasuredHeight);
        }
        else Content.Layout(0, 0, MeasuredWidth, MeasuredHeight);
    }

    internal VirtualItemInfo? VirtualItem { get; set; }

    public override void OnInitializeAccessibilityNodeInfo(global::Android.Views.Accessibility.AccessibilityNodeInfo? info)
    {
        base.OnInitializeAccessibilityNodeInfo(info);
        if (info is null || VirtualItem is not { } item) return;
        using var metadata = OperatingSystem.IsAndroidVersionAtLeast(33)
            ? new global::Android.Views.Accessibility.AccessibilityNodeInfo.CollectionItemInfo(item.Index, 1, 0, 1, false, false)
            : global::Android.Views.Accessibility.AccessibilityNodeInfo.CollectionItemInfo.Obtain(item.Index, 1, 0, 1, false, false);
        info.SetCollectionItemInfo(metadata);
        info.Extras?.PutString("Xui.Virtual.Key", item.Key);
        info.Extras?.PutLong("Xui.Virtual.SourceVersion", item.SourceVersion);
    }
}

internal sealed partial class EnabledScrollView(Context context) : NativeScrollView(context), IContextMeasure
{
    public override bool DispatchTouchEvent(MotionEvent? e)
    {
        if (!Enabled) return false;
        if (e?.ActionMasked == MotionEventActions.Down) virtualLease?.StopAnimation();
        return base.DispatchTouchEvent(e);
    }
    public override bool OnGenericMotionEvent(MotionEvent? e)
    {
        if (!Enabled) return false;
        if (virtualLease is not null && e is { Action: MotionEventActions.Scroll })
        {
            virtualLease.RequestNativeOffset(virtualLease.IntentOffset - e.GetAxisValue(global::Android.Views.Axis.Vscroll) *
                48 * Resources!.DisplayMetrics!.Density);
            return true;
        }
        return base.OnGenericMotionEvent(e);
    }
    public override bool DispatchKeyEvent(KeyEvent? e) => Enabled && base.DispatchKeyEvent(e);
}

internal interface IMutableNativeLayout
{
    void Add(AndroidPeer child);
    void Insert(int index, AndroidPeer child);
    void Remove(AndroidPeer child);
    void Move(AndroidPeer child, int index);
}

internal sealed class StackLayout : LinearLayout, IContextMeasure, IMutableNativeLayout
{
    private readonly Stack element;
    private readonly float density;
    private readonly List<AndroidPeer> children = [];
    private readonly NativeMeasureContext measureContext = new();
    private (AndroidPeer Peer, int Main)[] arranged = [];
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

    public void Add(AndroidPeer child) => Insert(children.Count, child);

    public void Insert(int index, AndroidPeer child)
    {
        AddView(child.View, index);
        children.Insert(index, child);
    }

    public void Remove(AndroidPeer child)
    {
        if (!children.Contains(child)) throw new ArgumentException("The native stack does not contain this child.", nameof(child));
        RemoveView(child.View);
        children.Remove(child);
    }

    public void Move(AndroidPeer child, int index)
    {
        int previous = children.IndexOf(child);
        if (previous < 0) throw new ArgumentException("The native stack does not contain this child.", nameof(child));
        if (index < 0 || index >= children.Count) throw new ArgumentOutOfRangeException(nameof(index));
        if (previous == index) return;
        var view = child.View;
        var layout = view.LayoutParameters;
        // Reorder the native child array without a window detach or editor/IME restart.
        DetachViewFromParent(previous);
        AttachViewToParent(view, index, layout);
        children.RemoveAt(previous);
        children.Insert(index, child);
        RequestLayout();
        Invalidate();
    }

    public void MeasureWith(MeasureConstraint width, MeasureConstraint height) => measureContext.Measure(this, width, height);

    protected override void OnMeasure(int widthMeasureSpec, int heightMeasureSpec)
    {
        var (width, height) = measureContext.Read(widthMeasureSpec, heightMeasureSpec);
        var mainOffer = Vertical ? height : width;
        var crossOffer = Vertical ? width : height;
        int padding = LayoutMath.Sum(PaddingPixels, PaddingPixels);
        var mainContent = NativeMeasure.Inset(mainOffer, padding);
        var crossContent = NativeMeasure.Inset(crossOffer, padding);
        var visible = children.Where(child => child.View.Visibility != ViewStates.Gone).ToArray();
        var desired = new float[visible.Length];
        var weights = new float[visible.Length];
        int cross = 0;
        for (int i = 0; i < visible.Length; i++)
        {
            var child = visible[i];
            MeasureChild(child.View, NativeMeasure.Intrinsic(mainContent),
                crossContent.Mode == MeasureMode.Exactly ? crossContent : NativeMeasure.Intrinsic(crossContent));
            desired[i] = Vertical ? child.View.MeasuredHeight : child.View.MeasuredWidth;
            weights[i] = child.Element.Flex;
            cross = Math.Max(cross, Vertical ? child.View.MeasuredWidth : child.View.MeasuredHeight);
        }
        var natural = PortableMath.AllocateStack(mainContent, Spacing, desired, weights);
        int main = NativeMeasure.Allocation(PortableMath.MeasureAxis(
            LayoutMath.Sum(NativeMeasure.Allocation(natural.Extent), padding), mainOffer, null));
        cross = NativeMeasure.Allocation(PortableMath.MeasureAxis(LayoutMath.Sum(cross, padding), crossOffer, null));
        var allocation = PortableMath.AllocateStack(
            MeasureConstraint.Exactly(Math.Max(0, main - padding), mainContent.IsUnbounded), Spacing, desired, weights);
        if (!Vertical && crossOffer.Mode != MeasureMode.Exactly)
        {
            // Wrapped native text needs its allocated width before it can report row height.
            cross = 0;
            for (int i = 0; i < visible.Length; i++)
            {
                MeasureChild(visible[i].View,
                    MeasureConstraint.Exactly(NativeMeasure.Allocation(allocation.Slots[i].Length), mainContent.IsUnbounded),
                    NativeMeasure.Intrinsic(crossContent));
                cross = Math.Max(cross, visible[i].View.MeasuredHeight);
            }
            cross = NativeMeasure.Allocation(PortableMath.MeasureAxis(LayoutMath.Sum(cross, padding), crossOffer, null));
        }
        arranged = new (AndroidPeer, int)[visible.Length];
        for (int i = 0; i < visible.Length; i++)
        {
            MeasureChild(visible[i].View,
                MeasureConstraint.Exactly(NativeMeasure.Allocation(allocation.Slots[i].Length), mainContent.IsUnbounded),
                MeasureConstraint.Exactly(Math.Max(0, cross - padding), crossContent.IsUnbounded));
            arranged[i] = (visible[i], NativeMeasure.Allocation(allocation.Slots[i].Offset));
        }
        SetMeasuredDimension(Vertical ? cross : main, Vertical ? main : cross);
    }

    private void MeasureChild(ElementFrame child, MeasureConstraint main, MeasureConstraint cross)
    {
        child.MeasureWith(Vertical ? cross : main, Vertical ? main : cross);
    }

    protected override void OnLayout(bool changed, int left, int top, int right, int bottom)
    {
        int mainPadding = Math.Min(PaddingPixels, Vertical ? MeasuredHeight : MeasuredWidth);
        int cross = Math.Min(PaddingPixels, Vertical ? MeasuredWidth : MeasuredHeight);
        foreach (var entry in arranged)
        {
            var view = entry.Peer.View;
            if (view.Visibility == ViewStates.Gone) continue;
            int cursor = LayoutMath.Sum(mainPadding, entry.Main);
            int x = Vertical ? cross : cursor;
            int y = Vertical ? cursor : cross;
            view.Layout(x, y, x + view.MeasuredWidth, y + view.MeasuredHeight);
        }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            children.Clear();
            arranged = [];
        }
        base.Dispose(disposing);
    }
}
