using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunLineMovement(VisualStyle style)
    {
        using var window = new Window("Designer source line movement", 850, 650, visualStyle: style);
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
                Check("A\rB", new(0, 0), true, "B\rA", new(2, 2));
                Check("A\rB", new(0, 2), true, "B\rA", new(2, 3));
                Check("A\rB", new(2, 3), false, "B\rA", new(0, 1));
                Check("A\rB\r", new(0, 2), true, "B\rA\r", new(2, 4));
                Check("A\rB\r", new(2, 4), false, "B\rA\r", new(0, 2));
                Check("A\rB", new(1, 1), true, "B\rA", new(3, 3));
                Check("A\rB", new(3, 3), false, "B\rA", new(1, 1));
                Check("one\r two\rthree", new(5, 7), true, "one\rthree\r two", new(11, 13));
                Check("one\r two\rthree", new(5, 7), false, " two\rone\rthree", new(1, 3));
                Check("A\rBB\rCCC\rD", new(2, 8), true, "A\rD\rBB\rCCC", new(4, 10));
                Check("A\rBB\rCCC\rD", new(2, 9), true, "A\rD\rBB\rCCC", new(4, 10));
                Check("A\rBB\rCCC\rD", new(2, 8), false, "BB\rCCC\rA\rD", new(0, 6));
                Check("A\rBB\rCCC\rD", new(2, 9), false, "BB\rCCC\rA\rD", new(0, 7));
                Check("A\r", new(2, 2), false, "\rA", new(0, 0));
                Check("A\r", new(0, 1), true, "\rA", new(1, 2));
                Check("\rA", new(1, 2), false, "A\r", new(0, 1));
                Check("A\r\rB", new(2, 2), true, "A\rB\r", new(4, 4));
                Check("A\r\rB", new(2, 3), false, "\rA\rB", new(0, 1));
                Check("A\r\U0001F680", new(2, 4), false, "\U0001F680\rA", new(0, 2));
                Check("A\r\U0001F680", new(0, 1), true, "\U0001F680\rA", new(3, 4));
                Check(" \tA\r\t B", new(0, 3), true, "\t B\r \tA", new(4, 7));
                string longLine = new('a', 65534);
                Check(longLine + "\rB", new(0, 1), true, "B\r" + longLine, new(2, 3));
                Require(reports.Count == 0, "Valid line moves do not report errors.");

                foreach (string source in new[] { "", "A", "A\rB", "\r" })
                {
                    Boundary(source, new(0, 0), false);
                    Boundary(source, new((ulong)source.Length, (ulong)source.Length), true);
                    Boundary(source, new(0, (ulong)source.Length), false);
                }
                Boundary("A\rB", new(0, 3), true);
                SetSource("A\rB", new(0, 1));
                editor.ReadOnly = true;
                Require(!lines.CanMove(false) && !lines.CanMove(true), "Read-only source disables both movement commands.");
                int before = reports.Count;
                lines.Move(true);
                Require(editor.Text == "A\rB" && editor.Selection == new TextSelection(0, 1) && changes == 0 &&
                    reports.Count == before + 1 && reports[^1].Contains("read-only", StringComparison.Ordinal),
                    "A read-only move reports an explicit refusal without changing source or selection.");
                editor.ReadOnly = false;
                foreach (var modifiers in new[] { KeyModifiers.None, KeyModifiers.Shift, KeyModifiers.Control,
                    KeyModifiers.Control | KeyModifiers.Alt, KeyModifiers.Control | KeyModifiers.Shift | KeyModifiers.Alt })
                    Require(!lines.HandleKey(new(0x26, modifiers, editor.Id)) &&
                        !lines.HandleKey(new(0x28, modifiers, editor.Id)), "Other navigation keys retain native behavior.");
                other.Focus();
                Require(!lines.HandleKey(new(0x26, KeyModifiers.Alt, other.Id)) &&
                    !lines.HandleKey(new(0x28, KeyModifiers.Alt, other.Id)) && other.Text == "Untouched" && editor.Text == "A\rB",
                    "Line movement does not intercept other native fields.");
                lines.Move(true);
                Require(editor.Text == "B\rA" && editor.Selection == new TextSelection(2, 3) && editor.Focused,
                    "Direct palette-style movement restores source focus and the mapped selection.");

                SetSource("A\rB", new(0, 0));
                editor.ReplaceRange(new(2, 3), editor.Text, "A");
                editor.Selection = new(0, 1);
                changes = 0;
                lines.Move(true);
                Require(editor.Text == "A\rA" && editor.Selection == new TextSelection(2, 3) && changes == 0,
                    "Identical lines move the selection without a source change.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "A\rB", "An identical-line move does not add an undo entry.");

                SetSource("A\rB", new(0, 0));
                editor.ReplaceRange(new(0, 1), editor.Text, "Q");
                editor.Selection = new(0, 0);
                lines.Move(false);
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "A\rB", "A boundary no-op leaves the preceding undo entry intact.");
                editor.Selection = new(0, 0);
                lines.Move(true);
                lines.Move(false);
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "B\rA", "A repeated move has its own undo entry.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "A\rB", "The first move has a separate native undo entry.");
                Require(window.CallbackStatus == 0, "Line movement has no native callback failures.");
                complete = true;
            }
            catch (Exception error) { failure = error; }
            finally { window.Close(); }
        })) throw new InvalidOperationException("The source line movement window rejected its action.");
        window.Run();
        if (failure is not null) throw new InvalidOperationException($"{style}: source line movement failed.", failure);
        Require(complete, $"Source line movement completed in {style}.");

        void SetSource(string source, TextSelection selection)
        {
            if (editor.Text != source) editor.ReplaceRange(new(0, (ulong)editor.Text.Length), editor.Text, source);
            editor.Selection = selection;
            changes = 0;
        }
        void Check(string source, TextSelection selection, bool down, string expected, TextSelection mapped)
        {
            SetSource(source, selection);
            Require(lines.CanMove(down), "The selected lines can move in this direction.");
            Require(lines.HandleKey(new(down ? 0x28u : 0x26u, KeyModifiers.Alt, editor.Id)), "The focused source handles the line movement shortcut.");
            Require(editor.Text == expected && editor.Selection == mapped && changes == 1 && editor.Focused,
                $"Line movement preserves exact text and maps the selection ({selection}, down={down}).");
            editor.Command(TextCommand.Undo);
            Require(editor.Text == source, "One native undo restores the source before the move.");
            editor.Command(TextCommand.Redo);
            Require(editor.Text == expected, "One native redo restores the moved lines.");
        }
        void Boundary(string source, TextSelection selection, bool down)
        {
            SetSource(source, selection);
            Require(!lines.CanMove(down), "The command is disabled at a document boundary.");
            int before = reports.Count;
            lines.Move(down);
            Require(editor.Text == source && editor.Selection == selection && changes == 0 && reports.Count == before + 1 &&
                reports[^1].Contains("already at", StringComparison.Ordinal), "A boundary move reports a no-op without changing source or selection.");
        }
    }
}
