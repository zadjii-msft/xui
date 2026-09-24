using Xui.Experimental.Portable;

internal static partial class Program
{
    private static int assertions;

    private static void Main()
    {
        ValueChecks();
        RuntimeChecks();
        GeneratedChecks();
        Console.WriteLine($"Label text layout: {assertions} assertions passed.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private static void ValueChecks()
    {
        var clip = LabelTextLayout.SingleLine();
        var ellipsis = LabelTextLayout.SingleLine(TextOverflow.CharacterEllipsis);
        var wrap = LabelTextLayout.Wrap();
        Assert(!clip.Wrapping && clip.MaximumLines == 1 && clip.Overflow == TextOverflow.Clip,
            "Single-line clipping explicitly describes one unwrapped line.");
        Assert(!ellipsis.Wrapping && ellipsis.MaximumLines == 1 && ellipsis.Overflow == TextOverflow.CharacterEllipsis,
            "Single-line character ellipsis is distinct from clipping.");
        Assert(wrap.Wrapping && wrap.MaximumLines == 0 && wrap.Overflow == TextOverflow.Clip,
            "Uncapped native wrapping never requests an ellipsis.");
        Assert(clip == LabelTextLayout.SingleLine(TextOverflow.Clip) &&
            ellipsis == LabelTextLayout.SingleLine(TextOverflow.CharacterEllipsis) &&
            clip != ellipsis && wrap != clip,
            "Atomic layout descriptors compare by value without conflating null, clipping, or wrapping.");
        foreach (uint lines in new uint[] { 0, 1, 2, 16, 32 })
        {
            var value = LabelTextLayout.Wrap(lines);
            Assert(value.Wrapping && value.MaximumLines == lines && value.Overflow == TextOverflow.Clip,
                "The entire bounded line-cap range preserves native wrapping with clipping.");
        }
        Throws<ArgumentOutOfRangeException>(() => LabelTextLayout.Wrap(33));
        Throws<ArgumentOutOfRangeException>(() => LabelTextLayout.Wrap(uint.MaxValue));
        Throws<ArgumentOutOfRangeException>(() => LabelTextLayout.SingleLine((TextOverflow)(-1)));
        Throws<ArgumentOutOfRangeException>(() => LabelTextLayout.SingleLine((TextOverflow)2));
        foreach (string text in new[]
        {
            "", "ordinary label", "  spaces\tstay  ", "\u05e9\u05dc\u05d5\u05dd 123",
            "\u0645\u0631\u062d\u0628\u0627", "\u4e2d\u6587", "e\u0301 \U0001f469\u200d\U0001f4bb",
            "No-break\u00a0space", " \u2007\u202f "
        })
        {
            clip.ValidateText(text);
            ellipsis.ValidateText(text);
            wrap.ValidateText(text);
            Assert(true, "Validation preserves empty text, spaces, tabs, bidi, combining marks, and grapheme sequences.");
        }
        foreach (string separator in new[] { "\r", "\n", "\r\n", "\u0085", "\u2028", "\u2029" })
        {
            string text = "before" + separator + "after";
            Throws<ArgumentException>(() => clip.ValidateText(text));
            Throws<ArgumentException>(() => ellipsis.ValidateText(text));
            wrap.ValidateText(text);
            LabelTextLayout.Wrap(1).ValidateText(text);
            Assert(text == "before" + separator + "after",
                "Wrapping keeps literal hard breaks even when clipping after one line; validation never normalizes text.");
        }
        foreach (var value in new[] { clip, ellipsis, wrap })
        {
            Throws<ArgumentNullException>(() => value.ValidateText(null!));
            Throws<ArgumentException>(() => value.ValidateText("embedded\0NUL"));
        }
    }
}
