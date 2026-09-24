using Android.Content;
using Android.Views;
using Android.Widget;
using Xui.Experimental.Portable;
using Size = Xui.Experimental.Portable.Size;

namespace Xui.Experimental.Android;

internal sealed class NativePageLayout : FrameLayout, IContextMeasure, IMutableNativeLayout
{
    private readonly PageView model;
    private readonly List<AndroidPeer> children = [];
    private readonly NativeMeasureContext measureContext = new();
    private ulong? selected;

    internal NativePageLayout(Context context, PageView model) : base(context)
    {
        this.model = model;
        ContentDescription = model.Name;
        Focusable = true;
        FocusableInTouchMode = true;
        DescendantFocusability = DescendantFocusability.BeforeDescendants;
    }

    public void Add(AndroidPeer child) => Insert(children.Count, child);
    public void Insert(int index, AndroidPeer child)
    {
        AddView(child.View, index, new LayoutParams(LayoutParams.MatchParent, LayoutParams.MatchParent));
        children.Insert(index, child);
        ApplyPages();
    }
    public void Remove(AndroidPeer child)
    {
        RemoveView(child.View);
        children.Remove(child);
    }
    public void Move(AndroidPeer child, int index)
    {
        int previous = children.IndexOf(child);
        if (previous < 0) throw new ArgumentException("The native page is not a child of this container.", nameof(child));
        if (previous == index) return;
        DetachViewFromParent(previous);
        AttachViewToParent(child.View, index, child.View.LayoutParameters);
        children.RemoveAt(previous);
        children.Insert(index, child);
        RequestLayout();
    }

    internal void ValidatePages(IReadOnlyList<PageEntry> pages, ulong? next)
    {
        if (selected != next && children.Any(child => child.HasComposingDescendant()))
            throw new InvalidOperationException("Finish native composition before switching retained pages.");
        if (selected != next && HasFocus && (!Focusable || !FocusableInTouchMode))
            throw new InvalidOperationException("The retained page host cannot safely receive focus from its outgoing page.");
    }

    internal void ValidateVisibility(bool visible)
    {
        if (!visible && (HasFocus || children.Any(child => child.HasComposingDescendant())))
            throw new InvalidOperationException("Move native focus and finish composition before hiding the page pane.");
    }

    internal void ValidateMutation()
    {
        if (children.Any(child => child.HasComposingDescendant()))
            throw new InvalidOperationException("Finish native composition before structurally changing retained pages.");
    }

    internal void ApplyPages()
    {
        bool repair = selected != model.Selected && HasFocus;
        selected = model.Selected;
        foreach (var child in children)
        {
            int index = -1;
            for (int i = 0; i < model.Children.Count; i++)
                if (ReferenceEquals(model.Children[i], child.Element)) { index = i; break; }
            bool active = model.Visible && index >= 0 && index < model.Pages.Count &&
                model.Pages[index].Enabled && model.Pages[index].Id == selected;
            child.View.Visibility = active && child.OwnVisible ? ViewStates.Visible : ViewStates.Gone;
            child.View.ImportantForAccessibility = active
                ? child.View.VirtualItem is null ? ImportantForAccessibility.No : ImportantForAccessibility.Yes
                : ImportantForAccessibility.NoHideDescendants;
            child.UpdateEnabled();
        }
        if (repair && model.Visible && !RequestFocus())
            throw new InvalidOperationException("The retained native page host could not repair outgoing page focus.");
        RequestLayout();
    }

    public void MeasureWith(MeasureConstraint width, MeasureConstraint height) => measureContext.Measure(this, width, height);
    protected override void OnMeasure(int widthMeasureSpec, int heightMeasureSpec)
    {
        var (width, height) = measureContext.Read(widthMeasureSpec, heightMeasureSpec);
        int naturalWidth = 0, naturalHeight = 0;
        foreach (var child in children)
        {
            if (child.View.Visibility == ViewStates.Gone) continue;
            child.View.MeasureWith(width, height);
            naturalWidth = Math.Max(naturalWidth, child.View.MeasuredWidth);
            naturalHeight = Math.Max(naturalHeight, child.View.MeasuredHeight);
        }
        SetMeasuredDimension(NativeMeasure.Allocation(Xui.Experimental.Portable.LayoutMath.MeasureAxis(naturalWidth, width, null)),
            NativeMeasure.Allocation(Xui.Experimental.Portable.LayoutMath.MeasureAxis(naturalHeight, height, null)));
    }
    protected override void OnLayout(bool changed, int left, int top, int right, int bottom)
    {
        foreach (var child in children)
            if (child.View.Visibility != ViewStates.Gone) child.View.Layout(0, 0, child.View.MeasuredWidth, child.View.MeasuredHeight);
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing) children.Clear();
        base.Dispose(disposing);
    }
}
