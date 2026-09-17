using System.Globalization;
using Xui;
using Xui.Designer;

internal static class Program
{
    private static int assertions;

    [STAThread]
    private static int Main()
    {
        try
        {
            Require(DesignerSourceSearch.FindMatches("", "", false).Count == 0, "An empty query has no matches.");
            Require(DesignerSourceSearch.FindMatches("aaa", "aa", true).SequenceEqual([new TextSelection(0, 2)]),
                "Literal matches do not overlap.");
            Require(DesignerSourceSearch.FindMatches("a.b a*b", ".", true).SequenceEqual([new TextSelection(1, 2)]),
                "Query punctuation is literal, not a regular expression.");
            var dense = DesignerSourceSearch.FindMatches(new string('x', 65536), "x", true);
            Require(dense.Count == 65536 && dense[^1] == new TextSelection(65535, 65536),
                "A maximum-length designer document keeps exact end offsets for dense matches.");
            var culture = CultureInfo.CurrentCulture;
            try
            {
                CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("tr-TR");
                Require(DesignerSourceSearch.FindMatches("I i", "i", false).Count == 2, "Case-insensitive matching is ordinal, not culture-sensitive.");
            }
            finally { CultureInfo.CurrentCulture = culture; }
            Run();
            Console.WriteLine($"Designer source search assertions: {assertions} passed.");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }

    private static void Run()
    {
        const string source = "Alpha alpha\r\U0001F680 Alpha";
        using var window = new Window("Designer source search", 800, 600);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument(source);
        int navigations = 0, changes = 0;
        var search = new DesignerSourceSearch(window, editor, () => navigations++);
        editor.Event += value => { if (value.Kind == EventKind.Change) changes++; };
        window.SetContent(search.Layout.Root);
        Exception? failure = null;
        bool completed = false;
        window.Post(() =>
        {
            try
            {
                Require(editor.GetBounds().Height >= 300, "The composed search toolbar preserves native source space.");
                Require(search.HandleKey(new('F', KeyModifiers.Control, 0)) && search.Layout.Query.Focused,
                    "Ctrl+F focuses the native query field.");
                search.Layout.Query.Text = "alpha";
                search.Refresh();
                Require(search.Layout.Status.Text == "3 matches", "The query counts exact native source matches.");
                editor.Selection = new(0, 0);
                search.Layout.Next.Invoke();
                Require(editor.Selection == new TextSelection(0, 5) && editor.Focused && navigations == 1,
                    "Next selects the first match, focuses source, and notifies hierarchy synchronization.");
                search.Layout.Next.Invoke();
                Require(editor.Selection == new TextSelection(6, 11), "Next advances beyond the selected match.");
                search.HandleKey(new(0x72, KeyModifiers.None, 0));
                Require(editor.Selection == new TextSelection(15, 20), "F3 uses exact native CR and UTF-16 offsets.");
                search.HandleKey(new(0x72, KeyModifiers.None, 0));
                Require(editor.Selection == new TextSelection(0, 5), "Next wraps at the end of source.");
                search.HandleKey(new(0x72, KeyModifiers.Shift, 0));
                Require(editor.Selection == new TextSelection(15, 20), "Shift+F3 wraps backwards.");
                search.Layout.MatchCase.Invoke();
                Require(search.Layout.Status.Text == "1 match", "The case toggle refreshes the count.");
                search.Move();
                Require(editor.Selection == new TextSelection(6, 11), "Case-sensitive navigation finds only the exact spelling.");
                search.Layout.Query.Text = "\U0001F680";
                search.Move();
                Require(editor.Selection == new TextSelection(12, 14), "Search selects a complete supplementary Unicode scalar.");
                search.Layout.Query.Text = "absent";
                var selection = editor.Selection;
                search.Move();
                Require(editor.Selection == selection && search.Layout.Status.Text.Contains("No matches", StringComparison.Ordinal),
                    "A missing match reports its result without moving selection.");
                search.Layout.Query.Text = "";
                search.Move();
                Require(editor.Selection == selection && search.Layout.Status.Text.Contains("Enter text", StringComparison.Ordinal),
                    "An empty query leaves selection unchanged.");
                Require(editor.Text == source && changes == 0, "Finding source never edits the native document.");
                editor.ReplaceRange(new(0, 0), editor.Text, "// shift\r");
                search.Layout.Query.Text = "Alpha";
                editor.Selection = new(0, 0);
                search.Move();
                Require(editor.Selection == new TextSelection(9, 14), "Search recomputes against source edits without cached stale offsets.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text == source, "Search navigation preserves the preceding native undo operation.");
                search.HandleKey(new('F', KeyModifiers.Control, 0));
                Require(search.HandleKey(new(0x1B, KeyModifiers.None, 0)) && editor.Focused, "Escape from the query returns to source.");
                Require(!search.HandleKey(new(0x0D, KeyModifiers.None, 0)) &&
                    !search.HandleKey(new(0x72, KeyModifiers.Control, 0)), "Unregistered shortcuts and source Enter remain native.");
                search.Layout.Query.Text = "Alpha";
                search.HandleKey(new('F', KeyModifiers.Control, 0));
                Require(search.HandleKey(new(0x0D, KeyModifiers.None, 0)) && editor.Focused,
                    "Enter in the query navigates to source.");
                completed = true;
            }
            catch (Exception error) { failure = error; }
            finally { window.Close(); }
        });
        window.Run();
        if (failure is not null) throw new InvalidOperationException("Source search UI smoke failed.", failure);
        Require(completed, "The source search fixture completed.");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }
}
