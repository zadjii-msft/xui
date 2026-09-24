namespace Xui.Experimental.Portable;

public readonly record struct AxisConstraints(float? Length = null, float Minimum = 0, float? Maximum = null)
{
    public static AxisConstraints Auto => default;
    public static AxisConstraints Fixed(float length) => new(length);
    public static implicit operator AxisConstraints(float length) => Fixed(length);

    internal void Validate()
    {
        Values.Length(Minimum);
        if (Maximum is float maximum && Values.Length(maximum) < Minimum)
            throw new ArgumentException("An axis maximum cannot be smaller than its minimum.");
        if (Length is float length && (Values.Length(length) < Minimum || length > (Maximum ?? float.MaxValue)))
            throw new ArgumentException("A fixed axis length must be within its minimum and maximum.");
    }
}

public enum MeasureMode { Unspecified, AtMost, Exactly }
public readonly record struct MeasureConstraint(MeasureMode Mode, float Size = 0, bool UnboundedContext = false)
{
    public MeasureConstraint(MeasureMode mode, float size) : this(mode, size, false) { }
    public void Deconstruct(out MeasureMode mode, out float size) { mode = Mode; size = Size; }
    public bool IsUnbounded => Mode == MeasureMode.Unspecified || UnboundedContext;
    public static MeasureConstraint Unspecified => new(MeasureMode.Unspecified);
    public static MeasureConstraint AtMost(float size, bool unboundedContext = false) => new(MeasureMode.AtMost, size, unboundedContext);
    public static MeasureConstraint Exactly(float size, bool unboundedContext = false) => new(MeasureMode.Exactly, size, unboundedContext);

    internal void Validate()
    {
        if (!Enum.IsDefined(Mode)) throw new ArgumentOutOfRangeException(nameof(Mode));
        Values.Length(Size);
        if (Mode == MeasureMode.Unspecified && Size != 0)
            throw new ArgumentException("An unspecified measure constraint must have size zero.");
    }
}

public readonly record struct LayoutSlot(float Offset, float Length);
public sealed class StackLayoutResult
{
    public float Extent { get; }
    public IReadOnlyList<LayoutSlot> Slots { get; }
    internal StackLayoutResult(float extent, LayoutSlot[] slots)
    {
        Extent = extent;
        Slots = Array.AsReadOnly(slots);
    }
}

public static class LayoutMath
{
    public static MeasureConstraint ConstrainMeasure(MeasureConstraint offered, AxisConstraints? constraints,
        float? fixedLength = null, float? preferredLength = null)
    {
        offered.Validate();
        constraints?.Validate();
        if (fixedLength.HasValue) Values.Length(fixedLength.Value);
        if (preferredLength.HasValue) Values.Length(preferredLength.Value);
        bool definite = constraints.HasValue ? constraints.Value.Length.HasValue : fixedLength.HasValue || preferredLength.HasValue;
        bool unbounded = offered.IsUnbounded && !definite;
        float? exact = constraints.HasValue ? constraints.Value.Length : fixedLength;
        if (!constraints.HasValue && !exact.HasValue && offered.Mode != MeasureMode.Exactly)
            exact = preferredLength;
        if (exact is float length)
            return MeasureConstraint.Exactly(offered.Mode == MeasureMode.Unspecified ? length : Math.Min(length, offered.Size));
        if (constraints?.Maximum is float maximum)
            return offered.Mode == MeasureMode.Unspecified
                ? MeasureConstraint.AtMost(maximum, unbounded)
                : new(offered.Mode, Math.Min(offered.Size, maximum), unbounded);
        return offered with { UnboundedContext = offered.Mode != MeasureMode.Unspecified && unbounded };
    }

    public static float MeasureAxis(float natural, MeasureConstraint offered, AxisConstraints? constraints,
        float? fixedLength = null, float? preferredLength = null)
    {
        Values.Length(natural);
        var effective = ConstrainMeasure(offered, constraints, fixedLength, preferredLength);
        if (effective.Mode == MeasureMode.Exactly) return effective.Size;
        float desired = Math.Clamp(natural, constraints?.Minimum ?? 0, constraints?.Maximum ?? float.MaxValue);
        return effective.Mode == MeasureMode.Unspecified ? desired : Math.Min(desired, effective.Size);
    }

    public static float ArrangeAxis(float allocation, AxisConstraints? constraints, float? fixedLength = null) =>
        MeasureAxis(0, MeasureConstraint.Exactly(allocation), constraints, fixedLength);

    public static StackLayoutResult AllocateStack(MeasureConstraint offered, float spacing,
        IReadOnlyList<float> desired, IReadOnlyList<float> weights)
    {
        offered.Validate();
        Values.Length(spacing);
        ArgumentNullException.ThrowIfNull(desired);
        ArgumentNullException.ThrowIfNull(weights);
        if (desired.Count != weights.Count) throw new ArgumentException("Every stack child requires one flex weight.");
        var sizes = new float[desired.Count];
        double natural = (double)spacing * Math.Max(0, sizes.Length - 1);
        double totalWeight = 0;
        for (int i = 0; i < sizes.Length; i++)
        {
            natural += Values.Length(desired[i]);
            totalWeight += Values.Length(weights[i]);
        }
        float extent = offered.Mode == MeasureMode.Unspecified
            ? Finite(natural)
            : offered.Mode == MeasureMode.Exactly || (!offered.IsUnbounded && totalWeight > 0) ? offered.Size : (float)Math.Min(natural, offered.Size);
        if (offered.IsUnbounded)
        {
            for (int i = 0; i < sizes.Length; i++) sizes[i] = desired[i];
        }
        else
        {
            double remaining = Math.Max(0, (double)extent - (double)spacing * Math.Max(0, sizes.Length - 1));
            for (int i = 0; i < sizes.Length; i++)
                if (weights[i] == 0)
                {
                    sizes[i] = FloorLength(Math.Min(desired[i], remaining));
                    remaining -= sizes[i];
                }
            double used = 0;
            for (int i = 0; i < sizes.Length; i++)
                if (weights[i] > 0)
                {
                    sizes[i] = FloorLength(Math.Min(remaining - used, remaining * weights[i] / totalWeight));
                    used += sizes[i];
                }
        }
        var slots = new LayoutSlot[sizes.Length];
        double cursor = 0;
        for (int i = 0; i < slots.Length; i++)
        {
            float offset = FloorLength(Math.Min(cursor, extent));
            float length = Math.Min(sizes[i], FloorLength((double)extent - offset));
            slots[i] = new(offset, length);
            cursor = Math.Min(extent, (double)offset + length + spacing);
        }
        return new(extent, slots);
    }

    private static float Finite(double value)
    {
        if (value > float.MaxValue) throw new OverflowException("The unbounded layout extent exceeds finite float coordinates.");
        return (float)value;
    }

    private static float FloorLength(double value)
    {
        if (value <= 0) return 0;
        float result = (float)value;
        return result > value ? MathF.BitDecrement(result) : result;
    }
}
