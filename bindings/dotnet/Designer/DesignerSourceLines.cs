namespace Xui.Designer;

internal sealed class DesignerSourceLines(MultilineText editor, Action<string> report, int maximumLength)
{
    internal bool HandleKey(UiKeyEvent key)
    {
        if (key.VirtualKey != 0x28 || key.Modifiers != (KeyModifiers.Alt | KeyModifiers.Shift) || !editor.Focused) return false;
        Duplicate();
        return true;
    }

    internal static (int First, int Last) SelectedLineBounds(string source, TextSelection selection)
    {
        int start = checked((int)selection.Start), end = checked((int)selection.End);
        int first = start == 0 ? 0 : source.LastIndexOf('\r', start - 1) + 1;
        int last = source.IndexOf('\r', end > start ? end - 1 : end);
        return (first, last < 0 ? source.Length : last);
    }

    internal void Duplicate()
    {
        if (editor.ReadOnly)
        {
            report("Source is read-only. No lines were duplicated.");
            return;
        }
        string source = editor.Text;
        var selection = editor.Selection;
        var (first, last) = SelectedLineBounds(source, selection);
        bool terminated = last < source.Length;
        int insertion = terminated ? last + 1 : last;
        int length = insertion - first + (terminated ? 0 : 1);
        if ((long)source.Length + length > maximumLength)
        {
            report($"Line duplication would exceed the source limit of {maximumLength} UTF-16 code units. No source changed.");
            return;
        }
        string replacement = terminated ? source[first..insertion] : "\r" + source[first..insertion];
        ulong delta = (ulong)length;
        var mapped = new TextSelection(selection.Start + delta, selection.End + delta);
        try
        {
            editor.ReplaceRange(new((ulong)insertion, (ulong)insertion), source, replacement);
            editor.Selection = mapped;
            editor.Focus();
        }
        catch (XuiException error) { report($"Native editor rejected the line duplication: {error.Message}"); }
    }
}
