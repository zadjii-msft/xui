using Android.Views;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

internal interface IContextMeasure
{
    void MeasureWith(MeasureConstraint width, MeasureConstraint height);
}

internal sealed class NativeMeasureContext
{
    private (MeasureConstraint Width, MeasureConstraint Height)? active;
    private (MeasureConstraint Width, MeasureConstraint Height)? previous;

    internal void Measure(View view, MeasureConstraint width, MeasureConstraint height)
    {
        var next = (NativeMeasure.PixelOffer(width), NativeMeasure.PixelOffer(height));
        // Android's spec cache does not include semantic unbounded-context flags.
        if (previous != next) view.ForceLayout();
        active = next;
        try { view.Measure(NativeMeasure.Spec(next.Item1), NativeMeasure.Spec(next.Item2)); }
        finally { active = null; }
    }

    internal (MeasureConstraint Width, MeasureConstraint Height) Read(int widthSpec, int heightSpec)
    {
        var result = active ?? (NativeMeasure.Offer(widthSpec), NativeMeasure.Offer(heightSpec));
        previous = result;
        return result;
    }
}

internal static class NativeMeasure
{
    internal static int Allocation(float value)
    {
        if (!float.IsFinite(value) || value < 0) throw new ArgumentOutOfRangeException(nameof(value));
        // Allocation budgets must not overflow; authored dp lengths use Pixels instead.
        return (int)Math.Min(LayoutMath.MaxDimension, Math.Floor(value));
    }

    internal static MeasureConstraint PixelOffer(MeasureConstraint value) => value.Mode == MeasureMode.Unspecified
        ? MeasureConstraint.Unspecified : value with { Size = Allocation(value.Size) };

    internal static MeasureConstraint Offer(int spec) => View.MeasureSpec.GetMode(spec) switch
    {
        MeasureSpecMode.Exactly => MeasureConstraint.Exactly(Math.Min(LayoutMath.MaxDimension, View.MeasureSpec.GetSize(spec))),
        MeasureSpecMode.AtMost => MeasureConstraint.AtMost(Math.Min(LayoutMath.MaxDimension, View.MeasureSpec.GetSize(spec))),
        MeasureSpecMode.Unspecified => MeasureConstraint.Unspecified,
        _ => throw new ArgumentOutOfRangeException(nameof(spec), "Unknown Android measure mode.")
    };

    internal static int Spec(MeasureConstraint offer) => View.MeasureSpec.MakeMeasureSpec(
        offer.Mode == MeasureMode.Unspecified ? 0 : Allocation(offer.Size),
        offer.Mode switch
        {
            MeasureMode.Exactly => MeasureSpecMode.Exactly,
            MeasureMode.AtMost => MeasureSpecMode.AtMost,
            MeasureMode.Unspecified => MeasureSpecMode.Unspecified,
            _ => throw new ArgumentOutOfRangeException(nameof(offer), "Unknown portable measure mode.")
        });

    internal static void Content(View view, MeasureConstraint width, MeasureConstraint height)
    {
        if (view is IContextMeasure contextual) contextual.MeasureWith(width, height);
        else view.Measure(Spec(width), Spec(height));
    }

    internal static MeasureConstraint Inset(MeasureConstraint offer, int padding) =>
        offer.Mode == MeasureMode.Unspecified ? offer : offer with { Size = Math.Max(0, offer.Size - padding) };

    internal static MeasureConstraint Intrinsic(MeasureConstraint offer) => offer.Mode == MeasureMode.Unspecified
        ? offer : MeasureConstraint.AtMost(offer.Size, offer.IsUnbounded);

    internal static AxisConstraints? Scale(AxisConstraints? constraints, float density) => constraints is { } value
        ? new AxisConstraints(Length(value.Length, density), LayoutMath.Pixels(value.Minimum, density), Length(value.Maximum, density))
        : null;

    internal static float? Length(float? value, float density) => value is float length ? LayoutMath.Pixels(length, density) : null;
}
