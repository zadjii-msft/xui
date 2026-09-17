namespace Xui;

public sealed unsafe partial class MultilineText
{
    /// <summary>
    /// Replaces a UTF-16 range as one native undo action. expectedText must match
    /// the complete current document, including CR paragraph separators.
    /// Returns the collapsed selection after the replacement.
    /// </summary>
    public TextSelection ReplaceRange(TextSelection range, string expectedText, string replacement)
    {
        Window.Guard();
        ArgumentNullException.ThrowIfNull(expectedText);
        ArgumentNullException.ThrowIfNull(replacement);
        using var pins = new Window.Pins();
        ulong start, end;
        Window.Check(Native.DocumentReplaceRange(Handle, range.Start, range.End,
            pins.Text(expectedText), pins.Text(replacement), &start, &end));
        return new(start, end);
    }
}
