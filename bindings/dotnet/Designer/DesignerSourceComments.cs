using System.Text;

namespace Xui.Designer;

internal sealed class DesignerSourceComments(MultilineText editor, Action<string> report, int maximumLength)
{
    private readonly record struct Change(int Start, int Length, string Text);

    internal bool HandleKey(UiKeyEvent key)
    {
        if (key.VirtualKey != 0xBF || key.Modifiers != KeyModifiers.Control || !editor.Focused) return false;
        Toggle();
        return true;
    }

    internal void Toggle()
    {
        if (editor.ReadOnly)
        {
            report("Source is read-only. No line comments changed.");
            return;
        }
        string source = editor.Text;
        var selection = editor.Selection;
        int start = checked((int)selection.Start), end = checked((int)selection.End);
        int first = start == 0 ? 0 : source.LastIndexOf('\r', start - 1) + 1;
        int last = source.IndexOf('\r', end > start ? end - 1 : end);
        if (last < 0) last = source.Length;
        var prefixes = new List<int>();
        bool uncomment = true;
        for (int line = first; line <= last;)
        {
            int lineEnd = source.IndexOf('\r', line);
            if (lineEnd < 0 || lineEnd > last) lineEnd = last;
            int prefix = line;
            while (prefix < lineEnd && source[prefix] is ' ' or '\t') prefix++;
            if (prefix < lineEnd)
            {
                prefixes.Add(prefix);
                uncomment &= source.AsSpan(prefix, lineEnd - prefix).StartsWith("//", StringComparison.Ordinal);
            }
            if (lineEnd == last) break;
            line = lineEnd + 1;
        }
        if (prefixes.Count == 0)
        {
            report("The selected lines contain only whitespace. No line comments changed.");
            return;
        }
        var changes = prefixes.Select(prefix => uncomment
            ? new Change(prefix, prefix + 2 < source.Length && source[prefix + 2] == ' ' ? 3 : 2, "")
            : new Change(prefix, 0, "// ")).ToArray();
        int delta = changes.Sum(change => change.Text.Length - change.Length);
        if ((long)source.Length + delta > maximumLength)
        {
            report($"Line comments would exceed the source limit of {maximumLength} UTF-16 code units. No source changed.");
            return;
        }
        var replacement = new StringBuilder(last - first + delta);
        int cursor = first;
        foreach (var change in changes)
        {
            replacement.Append(source, cursor, change.Start - cursor).Append(change.Text);
            cursor = change.Start + change.Length;
        }
        replacement.Append(source, cursor, last - cursor);
        var mapped = new TextSelection((ulong)Map(start, changes), (ulong)Map(end, changes));
        try
        {
            editor.ReplaceRange(new((ulong)first, (ulong)last), source, replacement.ToString());
            editor.Selection = mapped;
            editor.Focus();
        }
        catch (XuiException error) { report($"Native editor rejected the line-comment edit: {error.Message}"); }
    }

    private static int Map(int position, IReadOnlyList<Change> changes)
    {
        int delta = 0;
        foreach (var change in changes)
        {
            if (position < change.Start) break;
            if (position < change.Start + change.Length) return change.Start + delta;
            delta += change.Text.Length - change.Length;
        }
        return position + delta;
    }
}
