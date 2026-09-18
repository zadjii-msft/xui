using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunDuplication(VisualStyle style)
    {
        using var window = new Window("Designer source duplication", 850, 650, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetMaximumLength(65536);
        var other = window.MultilineText("Other").SetDocument("Untouched");
        var errors = new List<string>();
        var lines = new DesignerSourceLines(editor, errors.Add, 65536);
        window.KeyHandler = lines.HandleKey;
        window.SetContent(window.Stack().Add(editor, 1).Add(other, 1));
        int changes = 0;
        editor.Event += e => { if (e.Kind == EventKind.Change) changes++; };
        Exception? failure = null;
        bool complete = false;
        if (!window.Post(() =>
        {
            try
            {
                editor.Focus();
                Check("", new(0, 0), "\r", new(1, 1));
                Check("Alpha", new(2, 2), "Alpha\rAlpha", new(8, 8));
                Check("Alpha", new(1, 4), "Alpha\rAlpha", new(7, 10));
                Check("Alpha", new(5, 5), "Alpha\rAlpha", new(11, 11));
                Check("Alpha\rBeta", new(2, 2), "Alpha\rAlpha\rBeta", new(8, 8));
                Check("Alpha\rBeta", new(0, 6), "Alpha\rAlpha\rBeta", new(6, 12));
                Check("Alpha\rBeta", new(1, 7), "Alpha\rBeta\rAlpha\rBeta", new(12, 18));
                Check("Alpha\rBeta", new(6, 6), "Alpha\rBeta\rBeta", new(11, 11));
                Check("Alpha\rBeta", new(0, 10), "Alpha\rBeta\rAlpha\rBeta", new(11, 21));
                Check("Alpha\r", new(6, 6), "Alpha\r\r", new(7, 7));
                Check("Alpha\r", new(0, 6), "Alpha\rAlpha\r", new(6, 12));
                Check("A\r\rB", new(2, 2), "A\r\r\rB", new(3, 3));
                Check("A\r\rB", new(2, 3), "A\r\r\rB", new(3, 4));
                Check("\t  A\r  B\rC", new(1, 8), "\t  A\r  B\r\t  A\r  B\rC", new(10, 17));
                Check("\U0001F680\rX", new(0, 2), "\U0001F680\r\U0001F680\rX", new(3, 5));
                Check("A\r\U0001F680", new(2, 4), "A\r\U0001F680\r\U0001F680", new(5, 7));
                Check(" \t ", new(1, 1), " \t \r \t ", new(5, 5));
                string exact = new string('a', 32767) + "\r";
                Check(exact, new(1, 1), exact + exact, new(32769, 32769));
                Require(errors.Count == 0, "Valid duplication has no error messages.");

                string oversized = new('a', 32768);
                SetSource(oversized, new(1, 1));
                int before = errors.Count;
                lines.Duplicate();
                Require(editor.Text == oversized && editor.Selection == new TextSelection(1, 1) && changes == 0 &&
                    errors.Count == before + 1 && errors[^1].Contains("source limit", StringComparison.Ordinal),
                    "A duplication one code unit over the source limit is refused atomically.");
                SetSource("Alpha", new(1, 3));
                editor.SetMaximumLength(10);
                before = errors.Count;
                lines.Duplicate();
                Require(editor.Text == "Alpha" && editor.Selection == new TextSelection(1, 3) && changes == 0 &&
                    errors.Count == before + 1 && errors[^1].Contains("Native editor rejected", StringComparison.Ordinal),
                    "A stricter native limit rejects the insertion without partial changes.");
                editor.SetMaximumLength(65536);
                editor.ReadOnly = true;
                before = errors.Count;
                Require(lines.HandleKey(new(0x28, KeyModifiers.Alt | KeyModifiers.Shift, editor.Id)) &&
                    editor.Text == "Alpha" && changes == 0 && errors.Count == before + 1 &&
                    errors[^1].Contains("read-only", StringComparison.Ordinal),
                    "The source shortcut reports a read-only refusal.");
                editor.ReadOnly = false;
                foreach (var modifiers in new[] { KeyModifiers.None, KeyModifiers.Shift, KeyModifiers.Alt,
                    KeyModifiers.Control, KeyModifiers.Alt | KeyModifiers.Control | KeyModifiers.Shift })
                    Require(!lines.HandleKey(new(0x28, modifiers, editor.Id)), "Other Down combinations retain native behavior.");
                Require(!lines.HandleKey(new(0x26, KeyModifiers.Alt | KeyModifiers.Shift, editor.Id)),
                    "The action does not intercept an unregistered Up shortcut.");
                other.Focus();
                Require(!lines.HandleKey(new(0x28, KeyModifiers.Alt | KeyModifiers.Shift, other.Id)) &&
                    other.Text == "Untouched" && editor.Text == "Alpha", "The shortcut leaves other native fields alone.");
                lines.Duplicate();
                Require(editor.Text == "Alpha\rAlpha" && editor.Selection == new TextSelection(7, 9) && editor.Focused,
                    "Palette-style direct dispatch targets the retained source selection and restores source focus.");
                SetSource("A\rB", new(0, 0));
                editor.ReplaceRange(new(0, 0), editor.Text, "//\r");
                editor.Selection = new(3, 3);
                lines.Duplicate();
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "//\rA\rB", "The first undo removes only the duplicate.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "A\rB", "The next undo restores the preceding source edit.");
                Require(window.CallbackStatus == 0, "Native duplication has no callback failures.");
                complete = true;
            }
            catch (Exception error) { failure = error; }
            finally { window.Close(); }
        })) throw new InvalidOperationException("The source duplication window rejected its action.");
        window.Run();
        if (failure is not null) throw new InvalidOperationException($"{style}: source duplication failed.", failure);
        Require(complete, $"Source duplication completed in {style}.");

        void SetSource(string source, TextSelection selection)
        {
            if (editor.Text != source) editor.ReplaceRange(new(0, (ulong)editor.Text.Length), editor.Text, source);
            editor.Selection = selection;
            changes = 0;
        }
        void Check(string source, TextSelection selection, string expected, TextSelection mapped)
        {
            SetSource(source, selection);
            Require(lines.HandleKey(new(0x28, KeyModifiers.Alt | KeyModifiers.Shift, editor.Id)), "The source duplication shortcut is handled.");
            Require(editor.Text == expected && editor.Selection == mapped && changes == 1 && editor.Focused,
                "Duplication preserves exact lines and maps the selected characters or caret into the copy.");
            editor.Command(TextCommand.Undo);
            Require(editor.Text == source, "One native undo restores the exact source.");
            editor.Command(TextCommand.Redo);
            Require(editor.Text == expected, "One native redo restores the duplicate.");
        }
    }
}
