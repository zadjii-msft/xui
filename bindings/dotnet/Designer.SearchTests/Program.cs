using System.Globalization;
using Xui;
using Xui.Designer;

internal static partial class Program
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
            Require(DesignerSourceSearch.FindMatches("name names _name name2 name", "name", true, true)
                .SequenceEqual([new TextSelection(0, 4), new TextSelection(23, 27)]),
                "Whole-word matching excludes longer identifiers, digits, and underscores.");
            Require(DesignerSourceSearch.FindMatches("a\u0301 a a\u203F \U00010400a a\U00010400", "a", true, true)
                .SequenceEqual([new TextSelection(3, 4)]),
                "Whole-word matching respects combining marks, connector punctuation, and supplementary letters.");
            Require(DesignerSourceSearch.FindMatches("xa a a", "a a", true, true).SequenceEqual([new TextSelection(3, 6)]),
                "A rejected word boundary does not hide a later overlapping candidate with valid boundaries.");
            Require(DesignerSourceSearch.FindMatches("a\u20DD a\u200C", "a", true, true).Count == 0,
                "Enclosing marks and format characters remain part of a word.");
            Require(DesignerSourceSearch.FindMatches("\U0001F680", "\uD83D", true).Count == 0 &&
                DesignerSourceSearch.FindMatches("\U0001F680", "\uDE80", true).Count == 0,
                "A match cannot split a supplementary Unicode scalar.");
            var culture = CultureInfo.CurrentCulture;
            try
            {
                CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("tr-TR");
                Require(DesignerSourceSearch.FindMatches("I i", "i", false).Count == 2, "Case-insensitive matching is ordinal, not culture-sensitive.");
            }
            finally { CultureInfo.CurrentCulture = culture; }
            Run();
            RunReplacement();
            RunSelectedText(VisualStyle.Classic);
            RunSelectedText(VisualStyle.WinUI);
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
        var search = new DesignerSourceSearch(window, editor, () => navigations++,
            message => throw new InvalidOperationException(message), 65536);
        editor.Event += value => { if (value.Kind == EventKind.Change) changes++; };
        window.SetContent(window.Stack().Add(search.View, 1));
        Exception? failure = null;
        bool completed = false;
        window.Post(() =>
        {
            try
            {
                Require(editor.GetBounds().Height >= 300, "The composed search toolbar preserves native source space.");
                float fullHeight = editor.GetBounds().Height;
                Require(!search.Layout.FindOpen && !search.HandleKey(new(0x1B, KeyModifiers.None, 0)),
                    "Find starts collapsed and does not consume Escape while closed.");
                Require(search.HandleKey(new('F', KeyModifiers.Control, 0)) && search.Layout.FindOpen && search.Layout.Query.Focused,
                    "Ctrl+F reveals and focuses the native query field.");
                Require(editor.GetBounds().Height == fullHeight - 82, "Find reserves only its compact panel height.");
                search.Layout.Query.Text = "alpha";
                search.Refresh();
                Require(search.Layout.Status.Text == "3 matches", "The query counts exact native source matches.");
                editor.Selection = new(0, 0);
                search.Layout.Next.Invoke();
                Require(editor.Selection == new TextSelection(0, 5) && editor.Focused && navigations == 1,
                    "Next selects the first match, focuses source, and notifies hierarchy synchronization.");
                Require(search.HandleKey(new(0x1B, KeyModifiers.None, 0)) && !search.Layout.FindOpen && editor.Focused,
                    "Escape also collapses Find after match navigation focuses source.");
                Require(editor.GetBounds().Height == fullHeight && editor.Selection == new TextSelection(0, 5),
                    $"Closing Find restores source space without changing the match selection (height {editor.GetBounds().Height}/{fullHeight}, selection {editor.Selection}).");
                search.HandleKey(new('F', KeyModifiers.Control, 0));
                Require(search.Layout.Query.Text == "alpha", "Reopening Find preserves the query.");
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
                Require(search.HandleKey(new(0x1B, KeyModifiers.None, 0)) && !search.Layout.FindOpen && editor.Focused,
                    "Escape from the query closes Find and returns to source.");
                Require(!search.HandleKey(new(0x0D, KeyModifiers.None, 0)) &&
                    !search.HandleKey(new(0x72, KeyModifiers.Alt, 0)), "Unregistered shortcuts and source Enter remain native.");
                search.Layout.Query.Text = "Alpha";
                search.HandleKey(new('F', KeyModifiers.Control, 0));
                Require(search.HandleKey(new(0x0D, KeyModifiers.None, 0)) && editor.Focused,
                    "Enter in the query navigates to source.");
                search.Layout.Close.Invoke();
                Require(!search.Layout.FindOpen && editor.Focused, "The close button collapses Find and restores source focus.");
                search.HandleKey(new(0x72, KeyModifiers.None, 0));
                Require(search.Layout.FindOpen && editor.Focused, "F3 can reveal Find and navigate the retained query.");
                search.Layout.MatchCase.Focus();
                Require(search.HandleKey(new(0x1B, KeyModifiers.None, 0)) && !search.Layout.FindOpen && editor.Focused,
                    "Escape closes Find from its case toggle.");
                Require(editor.Text == source, "Opening, navigating, and closing Find leave source untouched.");
                completed = true;
            }
            catch (Exception error) { failure = error; }
            finally { window.Close(); }
        });
        window.Run();
        if (failure is not null) throw new InvalidOperationException("Source search UI smoke failed.", failure);
        Require(completed, "The source search fixture completed.");
    }

    private static void RunReplacement()
    {
        using var window = new Window("Designer source replacement", 800, 600);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetMaximumLength(65536);
        var errors = new List<string>();
        var search = new DesignerSourceSearch(window, editor, () => { }, errors.Add, 65536);
        window.SetContent(window.Stack().Add(search.View, 1));
        int changes = 0;
        editor.Event += value => { if (value.Kind == EventKind.Change) changes++; };
        Exception? failure = null;
        bool completed = false;
        window.Post(() =>
        {
            try
            {
                float fullHeight = editor.GetBounds().Height;
                Require(search.HandleKey(new('H', KeyModifiers.Control, 0)) && search.Layout.FindOpen &&
                    search.Layout.ReplaceOpen && search.Layout.Replacement.Focused,
                    "Ctrl+H opens replacement and focuses the native replacement field.");
                Require(editor.GetBounds().Height == fullHeight - 122 &&
                    search.Layout.Replacement.GetBounds().Height >= 28,
                    "The replacement panel reserves a compact extra row.");

                SetSource("Alpha alpha\r\U0001F680 Alpha", new(0, 0));
                search.Layout.Query.Text = "alpha";
                search.Layout.Replacement.Text = "Beta";
                search.Refresh();
                search.Layout.Replace.Invoke();
                Require(editor.Text == "Alpha alpha\r\U0001F680 Alpha" && editor.Selection == new TextSelection(0, 5) && changes == 0,
                    "Replace first selects a match instead of changing arbitrary selected source.");
                search.Layout.Replace.Invoke();
                Require(editor.Text == "Beta alpha\r\U0001F680 Alpha" && editor.Selection == new TextSelection(5, 10) && changes == 1,
                    "Replace changes only the selected match and advances to the next match.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "Alpha alpha\r\U0001F680 Alpha", "One native undo restores a single replacement.");
                editor.Command(TextCommand.Redo);
                Require(editor.Text == "Beta alpha\r\U0001F680 Alpha", "One native redo restores a single replacement.");

                SetSource("one\r\U0001F680 ONE one2 one", new(0, 0));
                search.Layout.Query.Text = "one";
                search.Layout.Replacement.Text = "$1\\literal";
                search.Layout.WholeWord.Invoke();
                Require(search.Layout.Status.Text == "3 matches", "Whole-word matching is wired to the replacement query.");
                search.Layout.ReplaceAll.Invoke();
                Require(editor.Text == "$1\\literal\r\U0001F680 $1\\literal one2 $1\\literal" && changes == 1,
                    "Replace All preserves gaps, native CR, Unicode, and literal replacement punctuation in one native edit.");
                Require(search.Layout.Status.Text == "Replaced 3 matches.", "Replace All reports its exact change count.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "one\r\U0001F680 ONE one2 one", "One undo restores all replacements together.");
                editor.Command(TextCommand.Redo);
                Require(editor.Text == "$1\\literal\r\U0001F680 $1\\literal one2 $1\\literal",
                    "One redo restores all replacements together.");

                search.Layout.WholeWord.Invoke();
                SetSource("xx", new(0, 0));
                search.Layout.Query.Text = "x";
                search.Layout.Replacement.Text = "xx";
                search.ReplaceAll();
                Require(editor.Text == "xxxx" && changes == 1 && search.Layout.Status.Text == "Replaced 2 matches.",
                    "Replace All uses one snapshot and does not search its own inserted text.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "xx", "Undo restores a replacement that contains the query.");
                search.Layout.Replacement.Text = "";
                changes = 0;
                search.ReplaceAll();
                Require(editor.Text == "" && changes == 1, "An empty replacement deletes all matched text in one operation.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "xx", "Undo restores deleted matches.");

                SetSource("x x", new(0, 1));
                search.Layout.Replacement.Text = "x";
                search.Replace();
                search.ReplaceAll();
                Require(editor.Text == "x x" && changes == 0 &&
                    search.Layout.Status.Text.Contains("unchanged", StringComparison.Ordinal),
                    "Identical replacements do not create source edits or undo entries.");
                editor.ReplaceRange(new(0, 0), editor.Text, "// ");
                search.ReplaceAll();
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "x x", "A no-op replacement preserves the preceding native undo action.");

                search.Layout.Replacement.Text = "y";
                editor.Selection = new(0, 1);
                editor.ReplaceRange(new(0, 1), editor.Text, "z");
                changes = 0;
                search.Replace();
                Require(editor.Text == "z x" && editor.Selection == new TextSelection(2, 3) && changes == 0,
                    "Replace reads the current source and refuses an old match selection after source changes.");
                search.Layout.Replacement.Focus();
                Require(search.HandleKey(new(0x0D, KeyModifiers.None, 0)) && editor.Text == "z y" && changes == 1,
                    "Enter in the replacement field replaces the selected current match.");

                SetSource("x X", new(0, 0));
                search.Layout.MatchCase.Invoke();
                search.ReplaceAll();
                Require(editor.Text == "y X" && changes == 1, "Case-sensitive replacement leaves differently cased text unchanged.");
                search.Layout.MatchCase.Invoke();
                SetSource("x X", new(0, 0));
                search.Layout.Replacement.Text = "x";
                search.ReplaceAll();
                Require(editor.Text == "x x" && search.Layout.Status.Text == "Replaced 1 match.",
                    "The replacement count excludes matches whose text was already identical.");

                search.Layout.Query.Text = "";
                search.Layout.Replacement.Text = "unused";
                changes = 0;
                var selection = editor.Selection;
                search.ReplaceAll();
                Require(changes == 0 && editor.Selection == selection,
                    "An empty query cannot edit the document or move its selection.");
                search.Layout.Query.Text = "absent";
                search.Replace();
                Require(changes == 0 && editor.Selection == selection, "An absent query cannot edit the document.");

                SetSource("x x", new(0, 1));
                search.Layout.Query.Text = "x";
                search.Layout.Replacement.Text = "long";
                editor.ReadOnly = true;
                search.Refresh();
                search.ReplaceAll();
                Require(editor.Text == "x x" && changes == 0 && errors.Count == 1,
                    "Direct replacement of read-only source reports an error without a mutation.");
                editor.ReadOnly = false;
                editor.SetMaximumLength(4);
                search.Replace();
                Require(editor.Text == "x x" && editor.Selection == new TextSelection(0, 1) &&
                    changes == 0 && errors.Count == 2, "Native length rejection preserves source, selection, and undo.");
                search.ReplaceAll();
                Require(editor.Text == "x x" && changes == 0 && errors.Count == 3,
                    "Native length rejection is atomic for Replace All.");
                editor.SetMaximumLength(65536);
                SetSource(new string('x', 65536), new(0, 1));
                search.Layout.Replacement.Text = "xx";
                search.ReplaceAll();
                Require(editor.Text.Length == 65536 && changes == 0 && errors.Count == 4 &&
                    editor.Selection == new TextSelection(0, 1),
                    "Oversized dense replacement is rejected before building a large candidate.");
                search.Replace();
                Require(editor.Text.Length == 65536 && changes == 0 && errors.Count == 5,
                    "A single replacement also respects the Designer source limit.");
                search.Layout.Replacement.Text = "";
                search.ReplaceAll();
                Require(editor.Text == "" && changes == 1, "Maximum-length dense deletion is one native edit.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text.Length == 65536, "One undo restores the maximum-length document.");

                search.Layout.Replacement.Text = "retained";
                search.Layout.Close.Invoke();
                Require(!search.Layout.FindOpen && editor.Focused && editor.GetBounds().Height == fullHeight,
                    "Closing replacement returns focus and the full source area.");
                search.HandleKey(new('H', KeyModifiers.Control, 0));
                Require(search.Layout.Replacement.Text == "retained", "Reopening retains the replacement text.");
                search.Layout.ReplaceToggle.Invoke();
                Require(!search.Layout.ReplaceOpen && search.Layout.Query.Focused &&
                    editor.GetBounds().Height == fullHeight - 82, "The toggle hides only replacement and restores query focus.");
                completed = true;
            }
            catch (Exception error) { failure = error; }
            finally { window.Close(); }
        });
        window.Run();
        if (failure is not null) throw new InvalidOperationException("Source replacement UI smoke failed.", failure);
        Require(completed, "The source replacement fixture completed.");

        void SetSource(string source, TextSelection selection)
        {
            if (editor.Text != source) editor.ReplaceRange(new(0, (ulong)editor.Text.Length), editor.Text, source);
            editor.Selection = selection;
            changes = 0;
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }
}
