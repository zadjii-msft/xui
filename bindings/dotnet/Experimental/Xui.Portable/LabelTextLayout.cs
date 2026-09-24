namespace Xui.Experimental.Portable;

public enum TextOverflow { Clip, CharacterEllipsis }

/// <summary>Explicit native label layout; it never rewrites the source or accessible text.</summary>
public sealed record LabelTextLayout
{
    public bool Wrapping { get; }
    public uint MaximumLines { get; }
    public TextOverflow Overflow { get; }

    private LabelTextLayout(bool wrapping, uint maximumLines, TextOverflow overflow)
    {
        Wrapping = wrapping;
        MaximumLines = maximumLines;
        Overflow = overflow;
    }

    public static LabelTextLayout SingleLine(TextOverflow overflow = TextOverflow.Clip)
    {
        if (!Enum.IsDefined(overflow)) throw new ArgumentOutOfRangeException(nameof(overflow));
        return new(false, 1, overflow);
    }

    /// <summary>Uses native line breaking and clips after the requested line count; zero is uncapped.</summary>
    public static LabelTextLayout Wrap(uint maximumLines = 0)
    {
        if (maximumLines > 32)
            throw new ArgumentOutOfRangeException(nameof(maximumLines), "Portable wrapping supports zero (uncapped) or 1 through 32 lines.");
        return new(true, maximumLines, TextOverflow.Clip);
    }

    public void ValidateText(string text)
    {
        Values.Text(text);
        if (!Wrapping && text.AsSpan().IndexOfAny("\r\n\u0085\u2028\u2029") >= 0)
            throw new ArgumentException("Explicit single-line labels cannot contain CR, LF, NEL, line separator, or paragraph separator.", nameof(text));
    }
}

/// <summary>Validates an explicit label layout without changing native state.</summary>
public interface ITextLayoutPeer : IElementPeer
{
    void ValidateTextLayout(LabelTextLayout? layout);
}
