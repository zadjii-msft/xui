namespace Xui.Designer;

internal sealed class DesignerSourceSearch
{
    private readonly MultilineText editor;
    private readonly Action navigated;
    private bool matchCase;

    internal DesignerSourceSearchLayout Layout { get; }
    internal Element View => Layout.Root;

    internal DesignerSourceSearch(Window window, MultilineText editor, Action navigated)
    {
        this.editor = editor;
        this.navigated = navigated;
        Layout = new DesignerSourceSearchLayout(window, editor, attach: false);
        Layout.Query.Event += value => { if (value.Kind == EventKind.Change) Refresh(); };
        editor.Event += value => { if (value.Kind == EventKind.Change) Refresh(); };
        Layout.MatchCase.Changed += value => { matchCase = value; Refresh(); };
        Layout.Next.Click += () => Move();
        Layout.Previous.Click += () => Move(reverse: true);
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
        if (!Layout.Query.Focused) return false;
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

    internal void Refresh()
    {
        var matches = FindMatches(editor.Text, Layout.Query.Text, matchCase);
        Layout.Next.Enabled = Layout.Previous.Enabled = matches.Count > 0;
        Layout.Status.Text = Layout.Query.Text.Length == 0 ? "Enter text to find."
            : matches.Count == 1 ? "1 match" : $"{matches.Count} matches";
    }

    internal void Move(bool reverse = false)
    {
        string query = Layout.Query.Text;
        var matches = FindMatches(editor.Text, query, matchCase);
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

    internal static IReadOnlyList<TextSelection> FindMatches(string source, string query, bool matchCase)
    {
        if (query.Length == 0) return [];
        var comparison = matchCase ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;
        var matches = new List<TextSelection>();
        int offset = 0;
        while (offset <= source.Length - query.Length)
        {
            int index = source.IndexOf(query, offset, comparison);
            if (index < 0) break;
            offset = index + query.Length;
            matches.Add(new((ulong)index, (ulong)offset));
        }
        return matches;
    }
}
