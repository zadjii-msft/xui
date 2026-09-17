using System.Runtime.InteropServices;
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
            Run();
            RunKeyboardRouting();
            Console.WriteLine($"Designer source indentation assertions: {assertions} passed.");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }

    private static void Run()
    {
        using var window = new Window("Designer source indentation", 800, 600);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source");
        var other = window.MultilineText("Other document");
        var errors = new List<string>();
        var indentation = new DesignerSourceIndentation(editor, errors.Add);
        window.KeyHandler = indentation.HandleKey;
        window.SetContent(window.Stack().Add(editor, 1).Add(other, 1));
        int changes = 0;
        editor.Event += value => { if (value.Kind == EventKind.Change) changes++; };
        Exception? failure = null;
        bool completed = false;
        window.Post(() =>
        {
            try
            {
                editor.Focus();
                CheckEdit("    Text();", new(11, 11), 0x0D, KeyModifiers.None, "    Text();\r    ", 16);
                CheckEdit("\t  Text();", new(10, 10), 0x0D, KeyModifiers.None, "\t  Text();\r\t  ", 14);
                CheckEdit("    ", new(4, 4), 0x0D, KeyModifiers.None, "    \r    ", 9);
                CheckEdit("    Text();", new(2, 2), 0x0D, KeyModifiers.None, "  \r    Text();", 5);
                CheckEdit("    Text();", new(7, 7), 0x0D, KeyModifiers.None, "    Tex\r    t();", 12);
                CheckEdit("    first\r  second", new(4, 12), 0x0D, KeyModifiers.None, "    \r    second", 9);
                CheckEdit("head\r  \U0001F680", new(9, 9), 0x0D, KeyModifiers.None, "head\r  \U0001F680\r  ", 12);
                CheckEdit("", new(0, 0), 0x09, KeyModifiers.None, "    ", 4);
                CheckEdit("Text();", new(0, 0), 0x09, KeyModifiers.None, "    Text();", 4);
                CheckEdit("    Text();", new(4, 4), 0x09, KeyModifiers.None, "        Text();", 8);
                CheckEdit("    Text();", new(2, 2), 0x09, KeyModifiers.None, "        Text();", 6);
                CheckEdit("head\r  Text();", new(7, 7), 0x09, KeyModifiers.None, "head\r      Text();", 11);
                CheckEdit("        Text();", new(8, 8), 0x09, KeyModifiers.Shift, "    Text();", 4);
                CheckEdit("    Text();", new(0, 0), 0x09, KeyModifiers.Shift, "Text();", 0);
                CheckEdit("    Text();", new(2, 2), 0x09, KeyModifiers.Shift, "Text();", 0);
                CheckEdit("  Text();", new(2, 2), 0x09, KeyModifiers.Shift, "Text();", 0);
                CheckEdit("\t  Text();", new(3, 3), 0x09, KeyModifiers.Shift, "  Text();", 2);
                CheckEdit("  \tText();", new(3, 3), 0x09, KeyModifiers.Shift, "\tText();", 1);
                CheckEdit("head\r    Text();", new(9, 9), 0x09, KeyModifiers.Shift, "head\rText();", 5);

                SetSource("Text();", new(0, 0));
                Require(indentation.HandleKey(new(0x09, KeyModifiers.Shift, 0)) && changes == 0 &&
                    editor.Selection == new TextSelection(0, 0), "Unindent at column zero is a handled no-op.");
                Unhandled("Text();", new(7, 7), 0x0D, KeyModifiers.None);
                Unhandled("    Text();", new(0, 0), 0x0D, KeyModifiers.None);
                Unhandled("    Text();", new(6, 6), 0x09, KeyModifiers.None);
                Unhandled("    Text();", new(6, 6), 0x09, KeyModifiers.Shift);
                Unhandled("    Text();", new(0, 4), 0x09, KeyModifiers.None);
                Unhandled("    Text();", new(0, 4), 0x09, KeyModifiers.Shift);
                Unhandled("    Text();", new(11, 11), 0x0D, KeyModifiers.Control);
                Unhandled("    Text();", new(11, 11), 0x0D, KeyModifiers.Shift);
                Unhandled("    Text();", new(4, 4), 0x09, KeyModifiers.Control);
                Unhandled("    Text();", new(4, 4), 0x09, KeyModifiers.Alt);

                other.Focus();
                Unhandled("    Text();", new(11, 11), 0x0D, KeyModifiers.None);
                Unhandled("    Text();", new(4, 4), 0x09, KeyModifiers.None);
                editor.Focus();
                editor.ReadOnly = true;
                Unhandled("    Text();", new(11, 11), 0x0D, KeyModifiers.None);
                Unhandled("    Text();", new(4, 4), 0x09, KeyModifiers.Shift);
                editor.ReadOnly = false;

                SetSource("    ", new(4, 4));
                editor.ReplaceRange(new(4, 4), editor.Text, "Text();");
                Require(indentation.HandleKey(new(0x0D, KeyModifiers.None, 0)), "Enter handles a preceding native edit.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "    Text();", "Undo removes the newline and indentation together.");
                editor.Command(TextCommand.Undo);
                Require(editor.Text == "    ", "The preceding native undo action survives.");
                editor.Command(TextCommand.Redo);
                editor.Command(TextCommand.Redo);
                Require(editor.Text == "    Text();\r    ", "Redo restores both native edits.");

                SetSource("    Text();", new(11, 11));
                editor.SetMaximumLength(12);
                Require(indentation.HandleKey(new(0x0D, KeyModifiers.None, 0)), "An oversized Enter is consumed.");
                Require(editor.Text == "    Text();" && changes == 0 && errors.Count == 1 &&
                    editor.Selection == new TextSelection(11, 11), "A length error is explicit and leaves text and selection intact.");
                editor.Selection = new(4, 4);
                Require(indentation.HandleKey(new(0x09, KeyModifiers.None, 0)), "An oversized Tab is consumed.");
                Require(editor.Text == "    Text();" && changes == 0 && errors.Count == 2,
                    "A rejected Tab does not change the document or traverse focus.");
                Require(editor.Focused, "Indentation preserves native editor focus.");
                completed = true;
            }
            catch (Exception error) { failure = error; }
            finally { window.Close(); }
        });
        window.Run();
        if (failure is not null) throw new InvalidOperationException("Source indentation UI smoke failed.", failure);
        Require(completed, "The source indentation fixture completed.");

        void SetSource(string source, TextSelection selection)
        {
            if (editor.Text != source)
                editor.ReplaceRange(new(0, (ulong)editor.Text.Length), editor.Text, source);
            editor.Selection = selection;
            changes = 0;
        }

        void CheckEdit(string source, TextSelection selection, uint key, KeyModifiers modifiers, string expected, ulong caret)
        {
            SetSource(source, selection);
            Require(indentation.HandleKey(new(key, modifiers, 0)), "The indentation key is handled.");
            Require(errors.Count == 0, $"The native editor accepts the edit: {string.Join("; ", errors)}");
            Require(editor.Text == expected, $"The indentation edit has the expected text for key {key} at {selection}.");
            Require(editor.Selection == new TextSelection(caret, caret), $"The caret follows the indentation edit: {editor.Selection}, expected {caret}.");
            Require(changes == 1 && errors.Count == 0, "Each indentation edit emits exactly one native change without an error.");
            editor.Command(TextCommand.Undo);
            Require(editor.Text == source, "One native Undo restores the complete source.");
            editor.Command(TextCommand.Redo);
            Require(editor.Text == expected, "One native Redo restores the complete indentation edit.");
        }

        void Unhandled(string source, TextSelection selection, uint key, KeyModifiers modifiers)
        {
            SetSource(source, selection);
            Require(!indentation.HandleKey(new(key, modifiers, 0)), "Unrelated input keeps the existing key behavior.");
            Require(editor.Text == source && editor.Selection == selection && changes == 0,
                "Unhandled keys leave native text, selection, and undo unchanged.");
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    private static void RunKeyboardRouting()
    {
        using var window = new Window("Designer indentation keyboard routing", 800, 600);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument("    Text();");
        var other = window.MultilineText("Other document").SetDocument("Other");
        var errors = new List<string>();
        var indentation = new DesignerSourceIndentation(editor, errors.Add);
        window.SetContent(window.Stack().Add(editor, 1).Add(other, 1));
        int stage = 0, changes = 0;
        bool markerSeen = false, completed = false;
        nint target = 0;
        Exception? failure = null;
        editor.Event += value => { if (value.Kind == EventKind.Change) changes++; };
        window.KeyHandler = key =>
        {
            if (key.VirtualKey != 0x87) return indentation.HandleKey(key);
            try
            {
                // A second marker runs after any WM_CHAR queued by TranslateMessage.
                if (!markerSeen)
                {
                    markerSeen = true;
                    PostKey(0x87);
                    return true;
                }
                if (stage == 0)
                {
                    Require(editor.Text == "    Text();\r    " && editor.Selection == new TextSelection(16, 16) &&
                        changes == 1, "A queued Enter produces exactly one indented newline through the native key router.");
                    editor.Command(TextCommand.Undo);
                    editor.Selection = new(0, 0);
                    stage++;
                    StartKey(0x09);
                }
                else if (stage == 1)
                {
                    Require(editor.Text == "        Text();" && editor.Selection == new TextSelection(4, 4) &&
                        changes == 1 && editor.Focused, "A queued leading Tab indents instead of traversing focus.");
                    editor.Command(TextCommand.Undo);
                    other.Focus();
                    other.Selection = new(5, 5);
                    target = GetFocus();
                    stage++;
                    StartKey(0x0D);
                }
                else
                {
                    Require(editor.Text == "    Text();" && changes == 0 && other.Text == "Other\r",
                        "Enter in another native document keeps its ordinary behavior.");
                    Require(errors.Count == 0, "Queued keyboard edits do not report native errors.");
                    completed = true;
                    window.Close();
                }
            }
            catch (Exception error) { failure = error; window.Close(); }
            return true;
        };
        window.Post(() =>
        {
            try
            {
                editor.Focus();
                editor.Selection = new(11, 11);
                target = GetFocus();
                Require(target != 0, "The test owns a focused native document.");
                StartKey(0x0D);
            }
            catch (Exception error) { failure = error; window.Close(); }
        });
        using var cancellation = new CancellationTokenSource();
        var timeout = Task.Run(async () =>
        {
            try
            {
                await Task.Delay(TimeSpan.FromSeconds(15), cancellation.Token);
                window.Post(() => { failure = new TimeoutException("The indentation keyboard fixture timed out."); window.Close(); });
            }
            catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { }
        });
        window.Run();
        cancellation.Cancel();
        timeout.GetAwaiter().GetResult();
        if (failure is not null) throw new InvalidOperationException("Source indentation keyboard routing failed.", failure);
        Require(completed, "The native keyboard routing fixture completed.");

        void StartKey(uint key)
        {
            changes = 0;
            markerSeen = false;
            PostKey(key);
            PostKey(0x87);
        }

        void PostKey(uint key) =>
            Require(PostMessageW(target, 0x0100, key, 1), "The test posts a key to its own native document.");
    }

    [DllImport("user32.dll")]
    private static extern nint GetFocus();

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessageW(nint window, uint message, nuint wParam, nint lParam);
}
