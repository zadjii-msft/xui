using Xui.Experimental.Portable;

namespace PortableDemo;

public static class VirtualListPerformance
{
    public const int WarmupCount = 10;
    public const int SampleCount = 100;
    public const double P95BudgetMilliseconds = 50;

    public static IReadOnlyList<float> RequestOffsets(int itemCount, float rowHeight, float viewportHeight)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(itemCount);
        VirtualizationMath.ClampOffset(itemCount, rowHeight, 0, viewportHeight);
        var offsets = new float[WarmupCount + SampleCount];
        for (int i = 0; i < offsets.Length; i++)
        {
            long far = (5000L + (i / 2) * 7919L) % itemCount;
            int index = (int)Math.Min(itemCount - 1L, far + i % 2);
            offsets[i] = VirtualizationMath.ClampOffset(itemCount, rowHeight,
                (float)(index * (double)rowHeight), viewportHeight);
        }
        return Array.AsReadOnly(offsets);
    }

    public static VirtualListPerformanceResult Evaluate(IReadOnlyList<double> milliseconds)
    {
        ArgumentNullException.ThrowIfNull(milliseconds);
        if (milliseconds.Count != WarmupCount + SampleCount)
            throw new ArgumentException("Record exactly ten warmups followed by one hundred viewport transactions.", nameof(milliseconds));
        foreach (double value in milliseconds)
            if (!double.IsFinite(value) || value < 0)
                throw new ArgumentOutOfRangeException(nameof(milliseconds), "Every elapsed time must be finite and nonnegative.");
        double[] samples = milliseconds.Skip(WarmupCount).ToArray();
        double[] ordered = samples.Order().ToArray();
        return new(Array.AsReadOnly(samples), ordered[49], ordered[94], ordered[^1]);
    }
}

public sealed record VirtualListPerformanceResult(
    IReadOnlyList<double> SamplesMilliseconds, double P50Milliseconds, double P95Milliseconds, double MaximumMilliseconds)
{
    public bool MeetsInitialBudget => P95Milliseconds <= VirtualListPerformance.P95BudgetMilliseconds;
}
