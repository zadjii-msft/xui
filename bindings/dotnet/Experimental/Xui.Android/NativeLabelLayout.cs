using Android.Text;
using Android.Widget;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

internal sealed class NativeLabelLayout
{
    private readonly TextView view;
    private readonly int minimumLines;
    private readonly int maximumLines;
    private readonly int minimumHeight;
    private readonly int maximumHeight;
    private readonly bool horizontal;
    private readonly TextUtils.TruncateAt? ellipsize;

    internal NativeLabelLayout(TextView view)
    {
        this.view = view;
        minimumLines = view.MinLines;
        maximumLines = view.MaxLines;
        minimumHeight = view.MinHeight;
        maximumHeight = view.MaxHeight;
        ellipsize = view.Ellipsize;
        if (OperatingSystem.IsAndroidVersionAtLeast(29)) horizontal = view.IsHorizontallyScrollable;
        else
        {
            using var baseline = view.Context!.ObtainStyledAttributes(null,
                [global::Android.Resource.Attribute.ScrollHorizontally], global::Android.Resource.Attribute.TextViewStyle, 0);
            horizontal = baseline.GetBoolean(0, false);
        }
    }

    internal void Apply(LabelTextLayout? layout)
    {
        if (layout is null)
        {
            if (minimumLines >= 0) view.SetMinLines(minimumLines); else view.SetMinHeight(minimumHeight);
            if (maximumLines >= 0) view.SetMaxLines(maximumLines); else view.SetMaxHeight(maximumHeight);
            view.SetHorizontallyScrolling(horizontal);
            view.Ellipsize = ellipsize;
            return;
        }
        view.SetMinLines(0);
        view.SetMaxLines(layout.MaximumLines == 0 ? int.MaxValue : (int)layout.MaximumLines);
        view.SetHorizontallyScrolling(!layout.Wrapping);
        view.Ellipsize = layout.Overflow == TextOverflow.CharacterEllipsis ? TextUtils.TruncateAt.End : null;
    }
}
