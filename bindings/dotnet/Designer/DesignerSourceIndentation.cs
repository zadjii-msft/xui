namespace Xui.Designer;

internal sealed class DesignerSourceIndentation(MultilineText editor, Action<string> report)
{
    private const int IndentSize = 4;

    internal bool HandleKey(UiKeyEvent key)
    {
        bool enter = key.VirtualKey == 0x0D && key.Modifiers == KeyModifiers.None;
        bool tab = key.VirtualKey == 0x09 && key.Modifiers is KeyModifiers.None or KeyModifiers.Shift;
        if ((!enter && !tab) || !editor.Focused || editor.ReadOnly) return false;

        string source = editor.Text;
        var selection = editor.Selection;
        int start = checked((int)selection.Start);
        int lineStart = start == 0 ? 0 : source.LastIndexOf('\r', start - 1) + 1;
        int indentEnd = lineStart;
        while (indentEnd < source.Length && source[indentEnd] is ' ' or '\t') indentEnd++;

        TextSelection range;
        string replacement;
        TextSelection? caret = null;
        if (enter)
        {
            int length = Math.Min(start, indentEnd) - lineStart;
            if (length == 0) return false;
            range = selection;
            replacement = "\r" + source.Substring(lineStart, length);
        }
        else
        {
            if (selection.Start != selection.End || start > indentEnd) return false;
            if (key.Modifiers == KeyModifiers.Shift)
            {
                if (indentEnd == lineStart) return true;
                int length = 1;
                if (source[lineStart] == ' ')
                    while (length < IndentSize && lineStart + length < indentEnd && source[lineStart + length] == ' ') length++;
                range = new((ulong)lineStart, (ulong)(lineStart + length));
                replacement = "";
                ulong position = (ulong)Math.Max(lineStart, start - length);
                caret = new(position, position);
            }
            else
            {
                range = selection;
                replacement = new string(' ', IndentSize);
            }
        }

        try
        {
            editor.ReplaceRange(range, source, replacement);
            if (caret is { } position) editor.Selection = position;
        }
        catch (XuiException error)
        {
            report($"Native editor rejected the indentation edit: {error.Message}");
        }
        return true;
    }
}
