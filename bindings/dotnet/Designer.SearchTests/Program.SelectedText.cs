using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunSelectedText(VisualStyle style)
    {
        const string source = "Alpha alpha\r\U0001F680 Alpha";
        using var window = new Window("Designer selected-source search", 850, 650, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetMaximumLength(65536).SetDocument(source);
        var errors = new List<string>();
        int navigations = 0, changes = 0;
        var search = new DesignerSourceSearch(window, editor, () => navigations++, errors.Add, 65536);
        editor.Event += e => { if (e.Kind == EventKind.Change) changes++; };
        window.SetContent(window.Stack().Add(search.View, 1));
        window.KeyHandler = search.HandleKey;
        Exception? failure = null;
        bool complete = false;
        window.Post(() =>
        {
            try
            {
                editor.Selection = new(0, 5);
                Require(search.CanFindSelection, "A nonempty source selection enables selection search.");
                search.FindSelection();
                Require(search.Layout.FindOpen && search.Layout.Query.Text == "Alpha" && search.Layout.Query.Focused &&
                    search.Layout.Status.Text == "3 matches" && editor.Selection == new TextSelection(0, 5) && navigations == 0,
                    "Find selected text opens the complete native query without changing source selection.");
                Require(!search.HandleKey(new(0x72, KeyModifiers.Control, search.Layout.Query.Id)),
                    "Ctrl+F3 in the query does not consume another field's keyboard input.");
                editor.Focus();
                Require(search.HandleKey(new(0x72, KeyModifiers.Control, editor.Id)) &&
                    editor.Selection == new TextSelection(6, 11) && navigations == 1 && editor.Focused,
                    "Source Ctrl+F3 skips the selected occurrence and notifies selection synchronization once.");
                Require(search.HandleKey(new(0x72, KeyModifiers.Control | KeyModifiers.Shift, editor.Id)) &&
                    editor.Selection == new TextSelection(0, 5) && search.Layout.Query.Text == "alpha",
                    "Source Ctrl+Shift+F3 uses the currently selected text and moves backwards.");
                search.FindSelection(navigate: true, reverse: true);
                Require(editor.Selection == new TextSelection(15, 20), "Previous selection search wraps at the start of source.");
                search.FindSelection(navigate: true);
                Require(editor.Selection == new TextSelection(0, 5), "Next selection search wraps at the end of source.");
                search.Layout.MatchCase.Invoke();
                search.FindSelection(navigate: true);
                Require(editor.Selection == new TextSelection(15, 20) && search.Layout.Status.Text == "Match 2 of 2",
                    "Selection search preserves the existing case-sensitive option.");
                search.Layout.MatchCase.Invoke();
                editor.Selection = new(12, 14);
                search.FindSelection(navigate: true);
                Require(search.Layout.Query.Text == "\U0001F680" && editor.Selection == new TextSelection(12, 14),
                    "Selection search retains a complete supplementary Unicode scalar.");
                Require(changes == 0 && editor.Text == source, "Selection searches never change the native source document.");
                search.HandleKey(new('H', KeyModifiers.Control, editor.Id));
                search.Layout.Replacement.Text = "Keep replacement";
                Require(!search.HandleKey(new(0x72, KeyModifiers.Control | KeyModifiers.Shift, search.Layout.Replacement.Id)),
                    "The new source shortcut does not intercept replacement-field input.");
                editor.Selection = new(0, 5);
                search.FindSelection();
                Require(search.Layout.ReplaceOpen && search.Layout.Replacement.Text == "Keep replacement",
                    "Selection search retains the replacement panel and its draft.");

                Reject(new(0, 0));
                Reject(new(0, 15));
                editor.Text = new string('a', DesignerSourceSearch.SelectionQueryLimit + 1);
                Reject(new(0, (ulong)editor.Text.Length));
                editor.Text = new string('a', DesignerSourceSearch.SelectionQueryLimit - 2) + "\U0001F680";
                editor.Selection = new(0, (ulong)editor.Text.Length);
                search.FindSelection();
                Require(search.Layout.Query.Text == editor.Text && search.Layout.Query.Text.Length == DesignerSourceSearch.SelectionQueryLimit,
                    "The exact native query limit keeps all selected text, including a final Unicode pair.");

                editor.Text = "Alpha Alphabet Alpha";
                search.Layout.WholeWord.Invoke();
                editor.Selection = new(0, 5);
                search.FindSelection(navigate: true);
                Require(search.Layout.Status.Text == "Match 2 of 2" && editor.Selection == new TextSelection(15, 20),
                    "Selection search preserves whole-word matching instead of finding a longer identifier.");
                editor.ReplaceRange(new(0, 0), editor.Text, "// ");
                editor.Selection = new(3, 8);
                search.FindSelection(navigate: true);
                Require(editor.Selection == new TextSelection(18, 23), "Selection search reads current source after an edit.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "Alpha Alphabet Alpha", "Selection search preserves the preceding native undo operation.");
                editor.ReadOnly = true;
                editor.Selection = new(0, 5);
                search.FindSelection(navigate: true);
                Require(editor.Selection == new TextSelection(15, 20), "Read-only source remains searchable.");
                Require(errors.Count == 3 && window.CallbackStatus == 0, "Only deliberate selection refusals report errors.");

                foreach (var (text, selection) in new (string, TextSelection)[]
                {
                    ("a", new(0, 0)), ("a", new(1, 0)), ("a", new(0, ulong.MaxValue)),
                    ("\U0001F680", new(0, 1)), ("\U0001F680", new(1, 2)),
                    ("a\rb", new(0, 3)), ("a\nb", new(0, 3)), ("a\0b", new(0, 3)),
                    ("a\u0085b", new(0, 3)), ("a\u2028b", new(0, 3)), ("a\u2029b", new(0, 3))
                })
                    Require(!DesignerSourceSearch.TrySelectionQuery(text, selection, out string query, out var error) &&
                        query == "" && !string.IsNullOrEmpty(error), "Invalid ranges and line boundaries return explicit errors without a partial query.");
                Require(DesignerSourceSearch.TrySelectionQuery(" \tfoo ", new(0, 6), out string exact, out _) && exact == " \tfoo ",
                    "Valid selected whitespace remains literal rather than being trimmed.");
                complete = true;
            }
            catch (Exception error) { failure = error; }
            finally { window.Close(); }
        });
        window.Run();
        if (failure is not null) throw new InvalidOperationException($"Selected-source search ({style}) failed.", failure);
        Require(complete, $"Selected-source search completed in {style}.");

        void Reject(TextSelection selection)
        {
            editor.Selection = selection;
            string query = search.Layout.Query.Text;
            int before = navigations, beforeErrors = errors.Count;
            Require(!search.CanFindSelection, "An unsupported selection disables the palette action.");
            search.FindSelection(navigate: true);
            Require(editor.Selection == selection && query == search.Layout.Query.Text && navigations == before &&
                errors.Count == beforeErrors + 1, "A direct selection-search refusal preserves query and source selection.");
        }
    }
}
