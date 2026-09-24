namespace Xui.Experimental.Portable;

public readonly record struct VirtualRange(int Start, int End)
{
    public int Count => End - Start;
}

public readonly record struct VirtualRun(int Start, int Count, bool IsGap);

/// <summary>Fixed-pitch, half-open intervals in native logical layout units.</summary>
public static class VirtualizationMath
{
    public static float Extent(int count, float rowHeight)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        Positive(rowHeight, nameof(rowHeight));
        double extent = count * (double)rowHeight;
        if (extent > float.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(count), "The total row extent must fit finite native geometry.");
        return (float)extent;
    }

    public static float ClampOffset(int count, float rowHeight, float offset, float viewportHeight)
    {
        float extent = Extent(count, rowHeight);
        Nonnegative(offset, nameof(offset));
        Nonnegative(viewportHeight, nameof(viewportHeight));
        return (float)Math.Min(offset, Math.Max(0, (double)extent - viewportHeight));
    }

    public static VirtualRange Visible(int count, float rowHeight, float offset, float viewportHeight, int overscan)
    {
        float clamped = ClampOffset(count, rowHeight, offset, viewportHeight);
        ArgumentOutOfRangeException.ThrowIfNegative(overscan);
        if (count == 0 || viewportHeight == 0) return default;
        int first = (int)Math.Min(count, Math.Floor(clamped / (double)rowHeight));
        int last = (int)Math.Min(count, Math.Ceiling((clamped + (double)viewportHeight) / rowHeight));
        return new(first - Math.Min(first, overscan), last + Math.Min(count - last, overscan));
    }

    public static float Reveal(int count, float rowHeight, int index, float offset, float viewportHeight)
    {
        float clamped = ClampOffset(count, rowHeight, offset, viewportHeight);
        if (index < 0 || index >= count) throw new ArgumentOutOfRangeException(nameof(index));
        double top = index * (double)rowHeight;
        double target = top < clamped || rowHeight > viewportHeight
            ? top
            : top + rowHeight > clamped + (double)viewportHeight
                ? top + rowHeight - viewportHeight
                : clamped;
        return ClampOffset(count, rowHeight, (float)target, viewportHeight);
    }

    public static IReadOnlyList<VirtualRun> Runs(int count, VirtualRange visible, IEnumerable<int> pins)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        ArgumentNullException.ThrowIfNull(pins);
        if (visible.Start < 0 || visible.End < visible.Start || visible.End > count)
            throw new ArgumentOutOfRangeException(nameof(visible));
        var intervals = new List<VirtualRange>();
        if (visible.Count != 0) intervals.Add(visible);
        foreach (int pin in pins)
        {
            if (pin < 0 || pin >= count) throw new ArgumentOutOfRangeException(nameof(pins));
            intervals.Add(new(pin, pin + 1));
        }
        intervals.Sort((left, right) => left.Start.CompareTo(right.Start));
        var runs = new List<VirtualRun>();
        int cursor = 0;
        foreach (var interval in intervals)
        {
            if (interval.End <= cursor) continue;
            if (interval.Start > cursor)
                runs.Add(new(cursor, interval.Start - cursor, true));
            int start = Math.Max(cursor, interval.Start);
            if (runs.Count > 0 && !runs[^1].IsGap)
            {
                var previous = runs[^1];
                runs[^1] = new(previous.Start, interval.End - previous.Start, false);
            }
            else
                runs.Add(new(start, interval.End - start, false));
            cursor = interval.End;
        }
        if (cursor < count) runs.Add(new(cursor, count - cursor, true));
        return runs.AsReadOnly();
    }

    private static void Positive(float value, string name)
    {
        if (!float.IsFinite(value) || value <= 0)
            throw new ArgumentOutOfRangeException(name, "A row height must be finite and positive.");
    }

    private static void Nonnegative(float value, string name)
    {
        if (!float.IsFinite(value) || value < 0)
            throw new ArgumentOutOfRangeException(name, "Viewport geometry must be finite and nonnegative.");
    }
}
