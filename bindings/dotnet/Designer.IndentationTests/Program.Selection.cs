using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunSelectionIndentation(VisualStyle style)
    {
        using var window = new Window("Designer selected indentation", 800, 600, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetLineNumbers();
        var errors = new List<string>();
        var indentation = new DesignerSourceIndentation(editor, errors.Add);
        window.SetContent(window.Stack().Add(editor, 1));
        int changes = 0;
        editor.Event += e => { if (e.Kind == EventKind.Change) changes++; };
        Exception? failure = null;
        window.Post(() =>
        {
            try
            {
                editor.Focus();
                Check("Alpha", new(1, 4), false, "    Alpha", new(5, 8));
                Check("Alpha\rBeta", new(0, 6), false, "    Alpha\rBeta", new(4, 10));
                Check("Alpha\rBeta", new(2, 8), false, "    Alpha\r    Beta", new(6, 16));
                Check("Alpha\r\rBeta", new(0, 11), false, "    Alpha\r    \r    Beta", new(4, 23));
                Check("Alpha\r", new(0, 6), false, "    Alpha\r", new(4, 10));
                Check(" \tAlpha\r  Beta", new(0, 14), true, "\tAlpha\rBeta", new(0, 11));
                Check("\tAlpha\r    Beta", new(1, 15), true, "Alpha\rBeta", new(0, 10));
                Check("    Alpha", new(1, 3), true, "Alpha", new(0, 0));
                Check("\U0001F680\r  Beta", new(0, 8), false, "    \U0001F680\r      Beta", new(4, 16));
                Set("Alpha\rBeta", new(0, 10));
                Require(indentation.HandleKey(new(9, KeyModifiers.Shift, 0)) &&
                    editor.Text == "Alpha\rBeta" && editor.Selection == new TextSelection(0, 10) && changes == 0,
                    "Unindenting unindented selected lines is a handled no-op.");
                Set("Alpha\rBeta", new(0, 10));
                editor.SetMaximumLength(12);
                Require(indentation.HandleKey(new(9, KeyModifiers.None, 0)) && changes == 0 &&
                    editor.Text == "Alpha\rBeta" && editor.Selection == new TextSelection(0, 10) &&
                    errors.Count == 1 && editor.Focused, "Oversized selected indentation fails atomically without traversing focus.");
            }
            catch (Exception error) { failure = error; }
            finally { window.Close(); }
        });
        window.Run();
        if (failure is not null) throw new InvalidOperationException($"{style}: selected indentation failed.", failure);

        void Set(string source, TextSelection selection)
        {
            if (editor.Text != source) editor.ReplaceRange(new(0, (ulong)editor.Text.Length), editor.Text, source);
            editor.Selection = selection;
            changes = 0;
        }
        void Check(string source, TextSelection selection, bool unindent, string expected, TextSelection mapped)
        {
            Set(source, selection);
            Require(indentation.HandleKey(new(9, unindent ? KeyModifiers.Shift : KeyModifiers.None, 0)),
                "Selected indentation consumes the key.");
            Require(editor.Text == expected && editor.Selection == mapped && changes == 1 && editor.Focused && errors.Count == 0,
                $"Selected indentation preserves characters and focus: {editor.Selection}, expected {mapped}.");
            editor.Command(TextCommand.Undo);
            Require(editor.Text == source, "One Undo restores all selected lines.");
            editor.Command(TextCommand.Redo);
            Require(editor.Text == expected, "One Redo restores all selected lines.");
        }
    }
}
