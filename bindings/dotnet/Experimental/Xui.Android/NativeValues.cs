using System.Globalization;
using Android.Content;
using Android.Graphics;
using Android.Graphics.Drawables;
using Android.Views;
using Android.Views.Accessibility;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

internal sealed class NativeChoice : global::Android.Widget.CheckBox
{
    private CheckState state;
    private Drawable? normalIndicator;
    private LayerDrawable? mixedIndicator;
    private readonly List<Drawable> ownedDrawables = [];
    internal bool ThreeState { get; set; }
    internal Func<CheckState, bool>? Changed { get; set; }

    internal NativeChoice(Context context) : base(context) { }

    internal void SetState(CheckState value)
    {
        if (!Enum.IsDefined(value)) throw new ArgumentOutOfRangeException(nameof(value));
        state = value;
        Checked = value == CheckState.Checked;
        if (value == CheckState.Indeterminate)
        {
            EnsureMixedIndicator();
            SetButtonDrawable(mixedIndicator);
        }
        else if (normalIndicator is not null) SetButtonDrawable(normalIndicator);
        RefreshDescription();
    }

    internal void RefreshDescription()
    {
        bool mixed = state == CheckState.Indeterminate;
        if (OperatingSystem.IsAndroidVersionAtLeast(30))
        {
            StateDescription = mixed ? "Mixed" : null;
            ContentDescription = null;
        }
        else ContentDescription = mixed ? $"{Text}, Mixed" : null;
    }

    public override void Toggle()
    {
        for (View? view = this; view is not null; view = view.Parent as View)
            if (!view.Enabled || view.Visibility != ViewStates.Visible) return;
        var previous = state;
        var next = state switch
        {
            CheckState.Unchecked => CheckState.Checked,
            CheckState.Checked when ThreeState => CheckState.Indeterminate,
            _ => CheckState.Unchecked
        };
        SetState(next);
        if (!(Changed?.Invoke(next) ?? false)) SetState(previous);
    }

    public override void OnInitializeAccessibilityNodeInfo(AccessibilityNodeInfo? info)
    {
        base.OnInitializeAccessibilityNodeInfo(info);
        if (info is not null && OperatingSystem.IsAndroidVersionAtLeast(36))
            info.CheckedState = state switch
            {
                CheckState.Checked => global::Android.Views.Accessibility.CheckedState.True,
                CheckState.Indeterminate => global::Android.Views.Accessibility.CheckedState.Partial,
                _ => global::Android.Views.Accessibility.CheckedState.False
            };
    }

    private void EnsureMixedIndicator()
    {
        if (mixedIndicator is not null) return;
        normalIndicator = ButtonDrawable ?? throw new InvalidOperationException("The native checkbox has no button drawable.");
        float density = Resources!.DisplayMetrics!.Density;
        int side = Math.Max(normalIndicator.IntrinsicWidth, LayoutMath.Pixels(20, density));
        int stroke = Math.Max(1, LayoutMath.Pixels(2, density));
        int padding = Math.Max(0, (side - LayoutMath.Pixels(20, density)) / 2);
        var border = new GradientDrawable();
        ownedDrawables.Add(border);
        border.SetShape(ShapeType.Rectangle);
        border.SetColor(Color.Transparent);
        border.SetCornerRadius(LayoutMath.Pixels(2, density));
        border.SetStroke(stroke, Color.Black);
        var mark = new GradientDrawable();
        ownedDrawables.Add(mark);
        mark.SetShape(ShapeType.Rectangle);
        mark.SetColor(Color.Black);
        mixedIndicator = new LayerDrawable([border, mark]);
        ownedDrawables.Add(mixedIndicator);
        mixedIndicator.SetLayerSize(0, side - 2 * padding, side - 2 * padding);
        mixedIndicator.SetLayerInset(0, padding, padding, padding, padding);
        int inset = padding + LayoutMath.Pixels(4, density);
        mixedIndicator.SetLayerSize(1, side - 2 * inset, stroke);
        mixedIndicator.SetLayerInset(1, inset, (side - stroke) / 2, inset, (side - stroke + 1) / 2);
        mixedIndicator.SetTintList(TextColors);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            Changed = null;
            if (Handle != IntPtr.Zero) SetButtonDrawable((Drawable?)null);
            foreach (var drawable in ownedDrawables.AsEnumerable().Reverse()) drawable.Dispose();
            ownedDrawables.Clear();
            mixedIndicator = null;
            normalIndicator = null;
        }
        base.Dispose(disposing);
    }
}

internal sealed class NativeProgress(Context context)
    : global::Android.Widget.ProgressBar(context, null, global::Android.Resource.Attribute.ProgressBarStyleHorizontal)
{
    internal const string MinimumKey = "Xui.Android.Progress.Minimum";
    internal const string MaximumKey = "Xui.Android.Progress.Maximum";
    internal const string ValueKey = "Xui.Android.Progress.Value";
    internal const string IndeterminateKey = "Xui.Android.Progress.Indeterminate";
    private NumericRange range = new(0, 100);
    private double value;

    internal void SetSnapshot(NumericRange nextRange, double nextValue, ProgressState state)
    {
        range = nextRange;
        value = nextValue;
        Min = 0;
        Max = 10000;
        Progress = (int)Math.Round((value - range.Minimum) / (range.Maximum - range.Minimum) * Max,
            MidpointRounding.AwayFromZero);
        Indeterminate = state == ProgressState.Indeterminate;
    }

    public override void OnInitializeAccessibilityNodeInfo(AccessibilityNodeInfo? info)
    {
        base.OnInitializeAccessibilityNodeInfo(info);
        if (info is null) return;
        var extras = info.Extras ?? throw new InvalidOperationException("Android did not provide accessibility extras.");
        extras.PutDouble(MinimumKey, range.Minimum);
        extras.PutDouble(MaximumKey, range.Maximum);
        extras.PutBoolean(IndeterminateKey, Indeterminate);
        if (Indeterminate)
        {
            extras.Remove(ValueKey);
            info.SetRangeInfo(null);
            if (OperatingSystem.IsAndroidVersionAtLeast(30)) info.StateDescription = "In progress";
            else info.ContentDescription = $"{ContentDescription}, In progress";
        }
        else
        {
            extras.PutDouble(ValueKey, value);
            float minimum = (float)range.Minimum;
            float maximum = (float)range.Maximum;
            float current = (float)value;
            if (float.IsFinite(minimum) && float.IsFinite(maximum) && float.IsFinite(current) &&
                minimum < maximum && current >= minimum && current <= maximum)
            {
                using var nativeRange = OperatingSystem.IsAndroidVersionAtLeast(33)
                    ? new AccessibilityNodeInfo.RangeInfo((int)RangeType.Float, minimum, maximum, current)
                    : AccessibilityNodeInfo.RangeInfo.Obtain(RangeType.Float, minimum, maximum, current);
                info.SetRangeInfo(nativeRange);
            }
            else info.SetRangeInfo(null);
            string description = string.Create(CultureInfo.InvariantCulture, $"{value:R} in range {range.Minimum:R} to {range.Maximum:R}");
            if (OperatingSystem.IsAndroidVersionAtLeast(30)) info.StateDescription = description;
            else info.ContentDescription = $"{ContentDescription}, {description}";
        }
    }
}
