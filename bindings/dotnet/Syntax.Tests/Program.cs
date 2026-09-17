using Xui;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        try
        {
            Require(MultilineText.SyntaxHighlightingAvailable, "Build native XUI with Lsh 0.3.0 before running this fixture.");
            using var window = new Window("Syntax highlighting", 800, 600);
            window.SetShowActivated(false);
            var editor = window.MultilineText("XUI source").SetSyntaxLanguage("xui");
            const string source = "component Demo {\rstate string Caption = \"\U0001f30d\";\rview { Text(Caption); }\r}";
            editor.Text = source;
            editor.SetControlStyleValues(StylePart.Text, new PartStyleValues { FontFamily = "Consolas" });
            var root = window.Stack();
            root.Add(editor);
            window.SetContent(root);
            int changes = 0;
            editor.Event += e => { if (e.Kind == EventKind.Change) changes++; };
            Exception? failure = null;
            bool completed = false;
            window.Post(() =>
            {
                try
                {
                    var selection = new TextSelection(0, 9);
                    editor.Selection = selection;
                    editor.SetSyntaxPath("Example.CS");
                    Require(editor.Text == source && editor.Selection == selection && changes == 0, "Language changes preserve source and selection.");
                    editor.SetSyntaxLanguage("xui");
                    editor.ReplaceRange(new(0, 0), editor.Text, "// comment\r");
                    Require(changes == 1 && editor.Text.StartsWith("// comment\r", StringComparison.Ordinal), "One edit produces one callback.");
                    editor.SetSyntaxLanguage("cpp");
                    editor.Command(TextCommand.Undo);
                    Require(editor.Text == source && changes == 2, "Syntax presentation preserves native undo.");
                    editor.Command(TextCommand.Redo);
                    Require(editor.Text.StartsWith("// comment\r", StringComparison.Ordinal) && changes == 3, "Syntax presentation preserves native redo.");
                    editor.SetSyntaxPath("notes.txt");
                    editor.SetSyntaxLanguage("xui");
                    try
                    {
                        editor.SetSyntaxLanguage("missing-language");
                        throw new InvalidOperationException("Unknown language did not report an error.");
                    }
                    catch (XuiException error) { Require(error.Status == 1, "Unknown language uses invalid-argument status."); }
                    window.SetTheme(Theme.Light);
                    window.SetTheme(Theme.Dark);
                    Require(changes == 3, "Theme changes do not edit text.");
                    completed = true;
                }
                catch (Exception error) { failure = error; }
                finally { window.Close(); }
            });
            window.Run();
            if (failure is not null) throw failure;
            Require(completed, "Native fixture did not complete.");
            Console.WriteLine("Managed LSH integration and native editing checks passed.");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }

    private static void Require(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
    }
}
