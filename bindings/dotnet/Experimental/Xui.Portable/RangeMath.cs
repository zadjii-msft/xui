namespace Xui.Experimental.Portable;

public enum RangeKey { Decrease, Increase, PageDecrease, PageIncrease, Minimum, Maximum }

public static class RangeMath
{
    public static void Validate(NumericRange range)
    {
        if (!double.IsFinite(range.Minimum) || !double.IsFinite(range.Maximum) || range.Minimum >= range.Maximum ||
            !double.IsFinite(range.Maximum - range.Minimum) ||
            !double.IsFinite(range.SmallStep) || range.SmallStep <= 0 ||
            !double.IsFinite(range.LargeStep) || range.LargeStep <= 0)
            throw new ArgumentOutOfRangeException(nameof(range), "A range requires finite increasing bounds and positive finite steps.");
    }

    public static void ValidateValue(NumericRange range, double value)
    {
        Validate(range);
        if (!double.IsFinite(value) || value < range.Minimum || value > range.Maximum)
            throw new ArgumentOutOfRangeException(nameof(value), "The value must be finite and within its range.");
    }

    public static double Step(NumericRange range, double value, RangeKey key)
    {
        ValidateValue(range, value);
        return key switch
        {
            RangeKey.Decrease => Math.Max(range.Minimum, value - range.SmallStep),
            RangeKey.Increase => Math.Min(range.Maximum, value + range.SmallStep),
            RangeKey.PageDecrease => Math.Max(range.Minimum, value - range.LargeStep),
            RangeKey.PageIncrease => Math.Min(range.Maximum, value + range.LargeStep),
            RangeKey.Minimum => range.Minimum,
            RangeKey.Maximum => range.Maximum,
            _ => throw new ArgumentOutOfRangeException(nameof(key))
        };
    }

    public static double Snap(NumericRange range, double fraction)
    {
        Validate(range);
        if (!double.IsFinite(fraction)) throw new ArgumentOutOfRangeException(nameof(fraction));
        fraction = Math.Clamp(fraction, 0, 1);
        double raw = range.Minimum + fraction * (range.Maximum - range.Minimum);
        double steps = (raw - range.Minimum) / range.SmallStep;
        double snapped = double.IsFinite(steps)
            ? range.Minimum + Math.Round(steps, MidpointRounding.AwayFromZero) * range.SmallStep
            : raw;
        return Math.Clamp(snapped, range.Minimum, range.Maximum);
    }
}
