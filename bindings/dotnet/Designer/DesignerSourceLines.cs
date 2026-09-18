namespace Xui.Designer;

internal sealed class DesignerSourceLines(MultilineText editor, Action<string> report, int maximumLength)
{
    internal bool HandleKey(UiKeyEvent key)
    {
        if (!editor.Focused) return false;
        if (key.VirtualKey == 0x28 && key.Modifiers == (KeyModifiers.Alt | KeyModifiers.Shift))
        { Duplicate(); return true; }
        if (key.VirtualKey is 0x26 or 0x28 && key.Modifiers == KeyModifiers.Alt)
        { Move(down: key.VirtualKey == 0x28); return true; }
        if (key.VirtualKey == 'K' && key.Modifiers == (KeyModifiers.Control | KeyModifiers.Shift))
        { Delete(); return true; }
        return false;
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

    internal bool CanMove(bool down)
    {
        if (editor.ReadOnly) return false;
        string source = editor.Text;
        var (first, last) = SelectedLineBounds(source, editor.Selection);
        return down ? last < source.Length : first > 0;
    }

    internal bool CanDelete => !editor.ReadOnly && editor.Text.Length > 0;

    internal void Delete()
    {
        if (editor.ReadOnly)
        {
            report("Source is read-only. No lines were deleted.");
            return;
        }
        string source = editor.Text;
        if (source.Length == 0)
        {
            report("Source is empty. No lines were deleted.");
            return;
        }
        var (first, last) = SelectedLineBounds(source, editor.Selection);
        if (last < source.Length) last++;
        else if (first > 0) first--;
        try
        {
            editor.ReplaceRange(new((ulong)first, (ulong)last), source, "");
            editor.Selection = new((ulong)first, (ulong)first);
            editor.Focus();
        }
        catch (XuiException error) { report($"Native editor rejected the line deletion: {error.Message}"); }
    }

    internal void Move(bool down)
    {
        if (editor.ReadOnly)
        {
            report("Source is read-only. No lines moved.");
            return;
        }
        string source = editor.Text;
        var selection = editor.Selection;
        var (first, last) = SelectedLineBounds(source, selection);
        if (down ? last == source.Length : first == 0)
        {
            report($"The selected lines are already at the {(down ? "end" : "start")} of the source. No lines moved.");
            return;
        }
        int rangeStart = first, rangeEnd = last, delta;
        string replacement;
        if (down)
        {
            rangeEnd = source.IndexOf('\r', last + 1);
            if (rangeEnd < 0) rangeEnd = source.Length;
            replacement = source[(last + 1)..rangeEnd] + "\r" + source[first..last];
            delta = rangeEnd - last;
        }
        else
        {
            rangeStart = first == 1 ? 0 : source.LastIndexOf('\r', first - 2) + 1;
            replacement = source[first..last] + "\r" + source[rangeStart..(first - 1)];
            delta = rangeStart - first;
        }
        var mapped = new TextSelection((ulong)((int)selection.Start + delta),
            (ulong)Math.Min((int)selection.End + delta, source.Length));
        try
        {
            if (!source.AsSpan(rangeStart, rangeEnd - rangeStart).SequenceEqual(replacement))
                editor.ReplaceRange(new((ulong)rangeStart, (ulong)rangeEnd), source, replacement);
            editor.Selection = mapped;
            editor.Focus();
        }
        catch (XuiException error) { report($"Native editor rejected the line move: {error.Message}"); }
    }
}
