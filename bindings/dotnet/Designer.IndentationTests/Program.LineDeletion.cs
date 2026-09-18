using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunLineDeletion(VisualStyle style)
    {
        using var window = new Window("Designer source line deletion", 850, 650, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetMaximumLength(65536);
        var other = window.MultilineText("Other").SetDocument("Untouched");
        var reports = new List<string>();
        var lines = new DesignerSourceLines(editor, reports.Add, 65536);
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
                Check("Alpha", new(2, 2), "", 0);
                Check("Alpha", new(1, 4), "", 0);
                Check("Alpha", new(5, 5), "", 0);
                Check("A\rB", new(0, 0), "B", 0);
                Check("A\rB", new(0, 2), "B", 0);
                Check("A\rB", new(1, 1), "B", 0);
                Check("A\rB", new(2, 2), "A", 1);
                Check("A\rB", new(3, 3), "A", 1);
                Check("A\rB", new(0, 3), "", 0);
                Check("A\rB\rC", new(2, 3), "A\rC", 2);
                Check("A\rB\rC", new(0, 4), "C", 0);
                Check("A\rB\rC", new(2, 5), "A", 1);
                Check("A\rB\r", new(2, 3), "A\r", 2);
                Check("A\rB\r", new(4, 4), "A\rB", 3);
                Check("A\r\rB", new(2, 2), "A\rB", 2);
                Check("\rA", new(0, 0), "A", 0);
                Check("\rA", new(1, 2), "", 0);
                Check("\r", new(0, 0), "", 0);
                Check("\r", new(1, 1), "", 0);
                Check("\U0001F680\rX", new(0, 2), "X", 0);
                Check("\U0001F680\rX", new(3, 4), "\U0001F680", 2);
                Check("\t  A\r  B\r\tC", new(6, 7), "\t  A\r\tC", 5);
                Check(new string('a', 65536), new(100, 100), "", 0);
                Require(reports.Count == 0, "Valid deletions do not report errors.");

                SetSource("A\rB", new(0, 1));
                editor.ReadOnly = true;
                Require(!lines.CanDelete, "Read-only source disables line deletion.");
                int before = reports.Count;
                Require(lines.HandleKey(new('K', KeyModifiers.Control | KeyModifiers.Shift, editor.Id)) &&
                    editor.Text == "A\rB" && editor.Selection == new TextSelection(0, 1) && changes == 0 &&
                    reports.Count == before + 1 && reports[^1].Contains("read-only", StringComparison.Ordinal),
                    "A read-only shortcut reports an explicit refusal without changing source or selection.");
                editor.ReadOnly = false;
                foreach (var modifiers in new[] { KeyModifiers.None, KeyModifiers.Shift, KeyModifiers.Control,
                    KeyModifiers.Alt, KeyModifiers.Control | KeyModifiers.Shift | KeyModifiers.Alt })
                    Require(!lines.HandleKey(new('K', modifiers, editor.Id)), "Other K shortcuts retain native behavior.");
                other.Focus();
                Require(!lines.HandleKey(new('K', KeyModifiers.Control | KeyModifiers.Shift, other.Id)) &&
                    other.Text == "Untouched" && editor.Text == "A\rB", "Line deletion does not intercept another native field.");
                lines.Delete();
                Require(editor.Text == "B" && editor.Selection == new TextSelection(0, 0) && editor.Focused,
                    "Direct palette-style deletion uses the retained source selection and restores source focus.");
                lines.Delete();
                Require(editor.Text == "" && !lines.CanDelete, "Deleting the last line disables further deletion.");
                changes = 0;
                before = reports.Count;
                lines.Delete();
                Require(editor.Text == "" && editor.Selection == new TextSelection(0, 0) && changes == 0 &&
                    reports.Count == before + 1 && reports[^1].Contains("empty", StringComparison.Ordinal),
                    "An empty source reports a no-op without a native edit.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "B", "The empty-source no-op does not create an undo entry.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "A\rB", "Successive deletions retain separate native undo entries.");
                Require(window.CallbackStatus == 0, "Line deletion has no native callback failures.");
                complete = true;
            }
            catch (Exception error) { failure = error; }
            finally { window.Close(); }
        })) throw new InvalidOperationException("The source line deletion window rejected its action.");
        window.Run();
        if (failure is not null) throw new InvalidOperationException($"{style}: source line deletion failed.", failure);
        Require(complete, $"Source line deletion completed in {style}.");

        void SetSource(string source, TextSelection selection)
        {
            if (editor.Text != source) editor.ReplaceRange(new(0, (ulong)editor.Text.Length), editor.Text, source);
            editor.Selection = selection;
            changes = 0;
        }
        void Check(string source, TextSelection selection, string expected, ulong caret)
        {
            SetSource(source, selection);
            Require(lines.CanDelete, "Nonempty editable source permits line deletion.");
            Require(lines.HandleKey(new('K', KeyModifiers.Control | KeyModifiers.Shift, editor.Id)), "The focused source handles line deletion.");
            Require(editor.Text == expected && editor.Selection == new TextSelection(caret, caret) && changes == 1 && editor.Focused,
                "Deletion removes exactly the selected lines and restores the expected native caret.");
            editor.Command(TextCommand.Undo);
            Require(editor.Text == source, "One native undo restores every deleted character.");
            editor.Command(TextCommand.Redo);
            Require(editor.Text == expected, "One native redo restores the line deletion.");
        }
    }
}
