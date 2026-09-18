using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunComments(VisualStyle style)
    {
        using var window = new Window("Designer line comments", 800, 600, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetMaximumLength(65536);
        var other = window.MultilineText("Other document").SetDocument("Other");
        var messages = new List<string>();
        var comments = new DesignerSourceComments(editor, messages.Add, 65536);
        window.KeyHandler = comments.HandleKey;
        window.SetContent(window.Stack().Add(editor, 1).Add(other, 1));
        int changes = 0;
        editor.Event += value => { if (value.Kind == EventKind.Change) changes++; };
        Exception? failure = null;
        bool completed = false;
        if (!window.Post(() =>
        {
            try
            {
                editor.Focus();
                CheckEdit("Alpha", new(2, 2), "// Alpha", new(5, 5));
                CheckEdit("  Alpha", new(0, 0), "  // Alpha", new(0, 0));
                CheckEdit("  Alpha", new(2, 7), "  // Alpha", new(5, 10));
                CheckEdit("\tAlpha\r  Beta", new(1, 13), "\t// Alpha\r  // Beta", new(4, 19));
                CheckEdit("Alpha\rBeta", new(0, 6), "// Alpha\rBeta", new(3, 9));
                CheckEdit("Alpha\rBeta", new(0, 7), "// Alpha\r// Beta", new(3, 13));
                CheckEdit("// Alpha", new(3, 8), "Alpha", new(0, 5));
                CheckEdit("// Alpha", new(1, 2), "Alpha", new(0, 0));
                CheckEdit(" //Alpha", new(4, 8), " Alpha", new(2, 6));
                CheckEdit("\U0001F680\r  Beta", new(0, 2), "// \U0001F680\r  Beta", new(3, 5));
                CheckEdit("// \U0001F680", new(3, 5), "\U0001F680", new(0, 2));
                CheckAll("  Alpha\r\r \t\r\tBeta", "  // Alpha\r\r \t\r\t// Beta", 0);
                CheckAll(" Alpha\r // Beta", " // Alpha\r // // Beta", 0);
                CheckAll(" // Alpha\r // // Beta", " Alpha\r // Beta", 0);
                CheckAll("//\tAlpha\r  // Beta\r//", "\tAlpha\r  Beta\r", 0);
                CheckAll("Alpha\r", "// Alpha\r", 3);
                CheckAll("Alpha\r\rBeta", "// Alpha\r\r// Beta", 3);
                CheckAll(new string('a', 65533), "// " + new string('a', 65533), 3);
                Require(messages.Count == 0, "Valid line comment edits have no error messages.");

                foreach (string blank in new[] { "", " \t", "\r", " \r\t\r" })
                {
                    SetSource(blank, new(0, (ulong)blank.Length));
                    int before = messages.Count;
                    comments.Toggle();
                    Require(editor.Text == blank && editor.Selection == new TextSelection(0, (ulong)blank.Length) &&
                        changes == 0 && messages.Count == before + 1 && messages[^1].Contains("whitespace", StringComparison.Ordinal),
                        "An all-blank selection reports an explicit no-op without changing text or selection.");
                }
                SetSource("Alpha", new(0, 5));
                editor.ReplaceRange(new(0, 5), editor.Text, " ");
                comments.Toggle();
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "Alpha", "A blank-line no-op preserves the previous native undo operation.");

                string maximum = new('a', 65534);
                SetSource(maximum, new(1, 1));
                int errorCount = messages.Count;
                comments.Toggle();
                Require(editor.Text == maximum && editor.Selection == new TextSelection(1, 1) && changes == 0 &&
                    messages.Count == errorCount + 1 && messages[^1].Contains("source limit", StringComparison.Ordinal),
                    "An oversized comment is rejected atomically before a native replacement.");

                SetSource("Alpha", new(1, 3));
                editor.SetMaximumLength(7);
                errorCount = messages.Count;
                comments.Toggle();
                Require(editor.Text == "Alpha" && editor.Selection == new TextSelection(1, 3) && changes == 0 &&
                    messages.Count == errorCount + 1 && messages[^1].Contains("Native editor rejected", StringComparison.Ordinal),
                    "A stricter native length limit also rejects the complete edit without partial text.");
                editor.SetMaximumLength(65536);
                editor.ReadOnly = true;
                errorCount = messages.Count;
                Require(comments.HandleKey(new(0xBF, KeyModifiers.Control, editor.Id)) && editor.Text == "Alpha" &&
                    editor.Selection == new TextSelection(1, 3) && changes == 0 && messages.Count == errorCount + 1 &&
                    messages[^1].Contains("read-only", StringComparison.Ordinal),
                    "A source comment shortcut reports a read-only refusal without changing selection.");
                editor.ReadOnly = false;
                foreach (var modifiers in new[] { KeyModifiers.None, KeyModifiers.Shift, KeyModifiers.Control | KeyModifiers.Shift, KeyModifiers.Alt })
                    Require(!comments.HandleKey(new(0xBF, modifiers, editor.Id)), "Other modifier combinations retain native behavior.");
                other.Focus();
                Require(!comments.HandleKey(new(0xBF, KeyModifiers.Control, other.Id)) && other.Text == "Other" &&
                    editor.Text == "Alpha" && changes == 0, "The comment shortcut does not consume input in another field.");
                comments.Toggle();
                Require(editor.Text == "// Alpha" && editor.Selection == new TextSelection(4, 6) && editor.Focused,
                    "A direct command targets the retained source selection and returns focus to source.");
                Require(window.CallbackStatus == 0, "Native comment editing does not cause callback failures.");
                completed = true;
            }
            catch (Exception error) { failure = error; }
            finally { window.Close(); }
        })) throw new InvalidOperationException("The comment test window rejected its action.");
        window.Run();
        if (failure is not null) throw new InvalidOperationException($"{style}: source comments UI smoke failed.", failure);
        Require(completed, "The native line-comment fixture completed.");

        void SetSource(string source, TextSelection selection)
        {
            if (editor.Text != source)
                editor.ReplaceRange(new(0, (ulong)editor.Text.Length), editor.Text, source);
            editor.Selection = selection;
            changes = 0;
        }

        void CheckAll(string source, string expected, ulong expectedStart) =>
            CheckEdit(source, new(0, (ulong)source.Length), expected, new(expectedStart, (ulong)expected.Length));

        void CheckEdit(string source, TextSelection selection, string expected, TextSelection expectedSelection)
        {
            SetSource(source, selection);
            Require(comments.HandleKey(new(0xBF, KeyModifiers.Control, editor.Id)), "Ctrl+/ is handled in source.");
            Require(editor.Text == expected, "Line comments preserve indentation, blank lines, and surrounding source.");
            Require(editor.Selection == expectedSelection, $"UTF-16 selection anchors map through comment prefixes: {editor.Selection}, expected {expectedSelection}.");
            Require(changes == 1 && editor.Focused, "A line-comment operation emits one native change and keeps source focus.");
            editor.Command(TextCommand.Undo);
            Require(editor.Text == source, "One Undo restores exact pre-comment source.");
            editor.Command(TextCommand.Redo);
            Require(editor.Text == expected, "One Redo restores the complete comment edit.");
        }
    }
}
