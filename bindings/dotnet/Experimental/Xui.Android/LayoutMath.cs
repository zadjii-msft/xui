namespace Xui.Experimental.Android;

internal static class LayoutMath
{
    // Android reserves the upper measured-dimension bits for state.
    internal const int MaxDimension = 0x00ffffff;

    internal static int Pixels(float dip, float density)
    {
        if (!float.IsFinite(dip) || dip < 0) throw new ArgumentOutOfRangeException(nameof(dip));
        if (!float.IsFinite(density) || density <= 0) throw new ArgumentOutOfRangeException(nameof(density));
        return (int)Math.Min(MaxDimension, Math.Round((double)dip * density, MidpointRounding.AwayFromZero));
    }

    internal static int Sum(int a, int b) => (int)Math.Min(MaxDimension, (long)a + b);

    internal static int[] Allocate(int available, int spacing, ReadOnlySpan<int> desired, ReadOnlySpan<float> weights)
    {
        if (available < 0 || spacing < 0 || desired.Length != weights.Length)
            throw new ArgumentException("Invalid stack allocation.");
        var result = new int[desired.Length];
        long gaps = (long)spacing * Math.Max(0, desired.Length - 1);
        int remaining = (int)Math.Max(0, available - gaps);
        double totalWeight = 0;
        for (int i = 0; i < weights.Length; i++)
        {
            if (desired[i] < 0 || !float.IsFinite(weights[i]) || weights[i] < 0)
                throw new ArgumentException("Invalid child allocation.");
            if (weights[i] > 0) totalWeight += weights[i];
            else
            {
                result[i] = Math.Min(desired[i], remaining);
                remaining -= result[i];
            }
        }
        double accumulated = 0;
        int distributed = 0;
        for (int i = 0; i < weights.Length; i++)
        {
            if (weights[i] == 0) continue;
            accumulated += weights[i];
            int end = (int)Math.Round(remaining * accumulated / totalWeight);
            result[i] = end - distributed;
            distributed = end;
        }
        return result;
    }
}
