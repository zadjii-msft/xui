using System.Globalization;
using System.Text;

namespace Xui.Designer;

internal sealed class DesignerSourceSearch
{
    private readonly MultilineText editor;
    private readonly Action navigated;
    private readonly Action<string> report;
    private readonly int maximumLength;
    private bool matchCase, wholeWord;

    internal DesignerSourceSearchLayout Layout { get; }
    internal Element View => Layout.Root;

    internal DesignerSourceSearch(Window window, MultilineText editor, Action navigated, Action<string> report, int maximumLength)
    {
        this.editor = editor;
        this.navigated = navigated;
        this.report = report;
        this.maximumLength = maximumLength;
        Layout = new DesignerSourceSearchLayout(window, editor, attach: false);
        Layout.Query.Event += value => { if (value.Kind == EventKind.Change) Refresh(); };
        editor.Event += value => { if (value.Kind == EventKind.Change) Refresh(); };
        Layout.MatchCase.Changed += value => { matchCase = value; Refresh(); };
        Layout.WholeWord.Changed += value => { wholeWord = value; Refresh(); };
        Layout.Next.Click += () => Move();
        Layout.Previous.Click += () => Move(reverse: true);
        Layout.ReplaceToggle.Click += () => SetReplaceOpen(!Layout.ReplaceOpen);
        Layout.Replace.Click += Replace;
        Layout.ReplaceAll.Click += ReplaceAll;
        Layout.Close.Click += Close;
    }

    internal bool HandleKey(UiKeyEvent key)
    {
        if (key.Modifiers == KeyModifiers.Control && key.VirtualKey == 'F')
        {
            Layout.FindOpen = true;
            Layout.Query.Focus();
            Refresh();
            return true;
        }
        if (key.Modifiers == KeyModifiers.Control && key.VirtualKey == 'H')
        {
            Layout.FindOpen = true;
            SetReplaceOpen(true);
            return true;
        }
        if (key.VirtualKey == 0x72 && key.Modifiers is KeyModifiers.None or KeyModifiers.Shift)
        {
            Layout.FindOpen = true;
            Move(key.Modifiers == KeyModifiers.Shift);
            return true;
        }
        if (Layout.FindOpen && key.VirtualKey == 0x1B && key.Modifiers == KeyModifiers.None)
        {
            Close();
            return true;
        }
        if (Layout.FindOpen && Layout.ReplaceOpen && Layout.Replacement.Focused &&
            key.VirtualKey == 0x0D && key.Modifiers == KeyModifiers.None)
        {
            Replace();
            return true;
        }
        if (!Layout.FindOpen || !Layout.Query.Focused) return false;
        if (key.VirtualKey == 0x0D && key.Modifiers is KeyModifiers.None or KeyModifiers.Shift)
        {
            Move(key.Modifiers == KeyModifiers.Shift);
            return true;
        }
        return false;
    }

    private void Close()
    {
        Layout.FindOpen = false;
        editor.Focus();
    }

    private void SetReplaceOpen(bool open)
    {
        Layout.ReplaceOpen = open;
        if (open) Layout.Replacement.Focus();
        else Layout.Query.Focus();
        Refresh();
    }

    internal void Refresh()
    {
        var matches = FindMatches(editor.Text, Layout.Query.Text, matchCase, wholeWord);
        Layout.Next.Enabled = Layout.Previous.Enabled = matches.Count > 0;
        Layout.Replace.Enabled = Layout.ReplaceAll.Enabled = matches.Count > 0 && !editor.ReadOnly;
        Layout.Status.Text = Layout.Query.Text.Length == 0 ? "Enter text to find."
            : matches.Count == 1 ? "1 match" : $"{matches.Count} matches";
    }

    internal void Move(bool reverse = false)
    {
        string query = Layout.Query.Text;
        var matches = FindMatches(editor.Text, query, matchCase, wholeWord);
        if (matches.Count == 0)
        {
            Refresh();
            Layout.Status.Text = query.Length == 0 ? "Enter text to find in source." : "No matches in the current source.";
            return;
        }
        var selection = editor.Selection;
        int index;
        if (reverse)
        {
            index = matches.Count - 1;
            for (int i = matches.Count - 1; i >= 0; i--)
                if (matches[i].Start < selection.Start) { index = i; break; }
        }
        else
        {
            index = 0;
            for (int i = 0; i < matches.Count; i++)
                if (matches[i].Start >= selection.End) { index = i; break; }
        }
        editor.Selection = matches[index];
        navigated();
        editor.Focus();
        Layout.Status.Text = $"Match {index + 1} of {matches.Count}";
    }

    internal void Replace()
    {
        if (!CanReplace()) return;
        string source = editor.Text;
        var matches = FindMatches(source, Layout.Query.Text, matchCase, wholeWord);
        if (matches.Count == 0) { Move(); return; }
        var selection = editor.Selection;
        if (!matches.Contains(selection))
        {
            Move();
            Layout.Status.Text = "Match selected. Choose Replace to change it.";
            return;
        }
        string replacement = Layout.Replacement.Text;
        if (source.AsSpan((int)selection.Start, (int)(selection.End - selection.Start)).SequenceEqual(replacement))
        {
            Move();
            Layout.Status.Text = "The replacement is unchanged. No source edit was applied.";
            return;
        }
        if (!ApplyReplacement(source, selection, replacement)) return;
        Move();
        Layout.Status.Text = $"Replaced 1 match. {Layout.Status.Text}";
    }

    internal void ReplaceAll()
    {
        if (!CanReplace()) return;
        string source = editor.Text;
        var matches = FindMatches(source, Layout.Query.Text, matchCase, wholeWord);
        if (matches.Count == 0) { Move(); return; }
        string replacement = Layout.Replacement.Text;
        long resultLength = source.Length + (long)matches.Count * (replacement.Length - Layout.Query.Text.Length);
        if (resultLength > maximumLength)
        {
            ReportError($"Replacement exceeds the source limit of {maximumLength} UTF-16 code units.");
            return;
        }
        int start = (int)matches[0].Start, end = (int)matches[^1].End, offset = start;
        var text = new StringBuilder((int)resultLength - start - (source.Length - end));
        int changed = 0;
        foreach (var match in matches)
        {
            text.Append(source, offset, (int)match.Start - offset);
            text.Append(replacement);
            if (!source.AsSpan((int)match.Start, (int)(match.End - match.Start)).SequenceEqual(replacement)) changed++;
            offset = (int)match.End;
        }
        if (changed == 0)
        {
            Layout.Status.Text = "The replacement is unchanged. No source edit was applied.";
            return;
        }
        if (!ApplyReplacement(source, new((ulong)start, (ulong)end), text.ToString())) return;
        editor.Focus();
        Layout.Status.Text = changed == 1 ? "Replaced 1 match." : $"Replaced {changed} matches.";
    }

    private bool CanReplace()
    {
        if (!editor.ReadOnly) return true;
        ReportError("The source editor is read-only. No replacement was applied.");
        return false;
    }

    private bool ApplyReplacement(string source, TextSelection range, string replacement)
    {
        if (source.Length - (long)(range.End - range.Start) + replacement.Length > maximumLength)
        {
            ReportError($"Replacement exceeds the source limit of {maximumLength} UTF-16 code units.");
            return false;
        }
        try
        {
            editor.ReplaceRange(range, source, replacement);
        }
        catch (XuiException error)
        {
            ReportError($"Native editor rejected the replacement: {error.Message}");
            return false;
        }
        Refresh();
        return true;
    }

    private void ReportError(string message)
    {
        Layout.Status.Text = message;
        report(message);
    }

    internal static IReadOnlyList<TextSelection> FindMatches(string source, string query, bool matchCase, bool wholeWord = false)
    {
        if (query.Length == 0) return [];
        var comparison = matchCase ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;
        var matches = new List<TextSelection>();
        int offset = 0;
        while (offset <= source.Length - query.Length)
        {
            int index = source.IndexOf(query, offset, comparison);
            if (index < 0) break;
            int end = index + query.Length;
            if (SplitsScalar(source, index) || SplitsScalar(source, end) ||
                (wholeWord && (IsWordBefore(source, index) || IsWordAt(source, end))))
            {
                offset = index + 1;
                continue;
            }
            offset = end;
            matches.Add(new((ulong)index, (ulong)offset));
        }
        return matches;
    }

    private static bool SplitsScalar(string source, int offset) =>
        offset > 0 && offset < source.Length && char.IsHighSurrogate(source[offset - 1]) && char.IsLowSurrogate(source[offset]);

    private static bool IsWordBefore(string source, int offset)
    {
        if (offset == 0) return false;
        int index = offset - 1;
        if (SplitsScalar(source, index)) index--;
        return IsWordAt(source, index);
    }

    private static bool IsWordAt(string source, int index) => index < source.Length &&
        char.GetUnicodeCategory(source, index) is UnicodeCategory.UppercaseLetter or UnicodeCategory.LowercaseLetter or
            UnicodeCategory.TitlecaseLetter or UnicodeCategory.ModifierLetter or UnicodeCategory.OtherLetter or
            UnicodeCategory.DecimalDigitNumber or UnicodeCategory.LetterNumber or UnicodeCategory.ConnectorPunctuation or
            UnicodeCategory.NonSpacingMark or UnicodeCategory.SpacingCombiningMark or UnicodeCategory.EnclosingMark or UnicodeCategory.Format;
}
