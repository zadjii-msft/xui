using Xui;
using Xui.Designer;

internal static partial class Program
{
    [STAThread]
    private static int Main()
    {
        try { Run(); RunDimensions(); RunBooleans(VisualStyle.Classic); RunBooleans(VisualStyle.WinUI); return 0; }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }

    private static void Run()
    {
        const string original = """"component TextModes { state string Title = "Expression"; view { VStack() { Text(@"C:\folder ""quoted""", fontSize: 16, wrapping: true); Text("Line1\r\nLine2"); Text("""Raw \ path"""); Text(Title, ref: Caption); Text("\0"); } } }"""";
        var parsed = VisualDocument.Parse(original);
        if (!parsed.Success) throw new InvalidOperationException(string.Join("; ", parsed.Diagnostics.Select(value => value.Message)));
        using var window = new Window("Designer string text mode", 1200, 1000);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument(original);
        var errors = new List<string>();
        using var workspace = new DesignerWorkspace(window, editor, errors.Add);
        int changes = 0, assertions = 0;
        editor.Event += value =>
        {
            if (value.Kind != EventKind.Change) return;
            changes++;
            workspace.SourceChanged();
        };
        window.SetContent(window.Stack(Axis.Horizontal).Padding(10).Spacing(10)
            .Add(editor, 1).Add(workspace.Hierarchy.Layout.Root, 1).Add(workspace.Inspector.Layout.Root, 1));
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!window.Post(() => started.SetResult()))
            throw new InvalidOperationException("The text mode test window rejected its startup action.");
        Exception? failure = null;
        var driver = Task.Run(async () =>
        {
            try
            {
                await started.Task.WaitAsync(TimeSpan.FromSeconds(30));
                await Ui(workspace.SourceChanged);
                await Ready();
                string prior = "";
                await Ui(() =>
                {
                    editor.ReplaceRange(new((ulong)editor.Text.Length, (ulong)editor.Text.Length), editor.Text, "\r// Prior typing");
                    prior = editor.Text;
                });
                await Ready();
                await Ui(() =>
                {
                    Select(0);
                    Require(!workspace.Inspector.IsTextMode && workspace.Inspector.Value.Text.StartsWith("@\"", StringComparison.Ordinal),
                        "Raw literal source remains the default.");
                    workspace.Inspector.Layout.TextMode.Invoke();
                    Require(workspace.Inspector.IsTextMode && workspace.Inspector.Value.Text == "C:\\folder \"quoted\"",
                        "Text mode decodes a verbatim literal in the native editor.");
                    workspace.Inspector.Layout.Apply.Invoke();
                    Require(editor.Text == prior && changes == 1 && !workspace.IsBusy &&
                        workspace.Inspector.Layout.Feedback.Text.Contains("unchanged", StringComparison.Ordinal),
                        "An unchanged decoded value creates no source edit or validation transaction.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == original, "No-op text Apply preserves the previous native typing undo unit.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                const string changedText = "Hello \"UI\"\rNext \U0001F600";
                int beforeApply = 0;
                string changedSource = "";
                await Ui(() =>
                {
                    Select(0);
                    workspace.Inspector.Layout.TextMode.Invoke();
                    workspace.Inspector.Value.Text = changedText;
                    beforeApply = changes;
                    workspace.Inspector.Layout.Apply.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    changedSource = editor.Text;
                    var expression = workspace.Document!.Root!.Children[0].Arguments.Single(argument => argument.Name == "value").Value;
                    Require(DesignerLiteralCodec.TryDecodeText(expression, out string decoded, out _) && decoded == changedText,
                        "Text Apply encodes quotes, native paragraphs, and complete Unicode scalars. " + workspace.Inspector.Layout.Feedback.Text);
                    Require(changes == beforeApply + 1 && !workspace.Inspector.IsTextMode,
                        "A text edit emits one native source change and returns the refreshed inspector to raw mode.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == prior, "One native Undo restores the exact original verbatim spelling.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == changedSource, "Native Redo restores the encoded text edit.");
                    Select(0);
                    workspace.Inspector.Layout.TextMode.Invoke();
                    workspace.Inspector.Value.Text = "Draft \"quotes\"";
                    workspace.Inspector.Layout.TextMode.Invoke();
                    Require(!workspace.Inspector.IsTextMode &&
                        DesignerLiteralCodec.TryDecodeText(workspace.Inspector.Value.Text, out string draft, out _) && draft == "Draft \"quotes\"" &&
                        editor.Text == changedSource, "Switching back to raw mode preserves the unapplied property draft.");
                    workspace.Inspector.Value.Text = "42";
                    workspace.Inspector.Layout.TextMode.Invoke();
                    Require(!workspace.Inspector.IsTextMode && workspace.Inspector.Value.Text == "42" &&
                        workspace.Inspector.Layout.Feedback.Text.Contains("string literal", StringComparison.Ordinal),
                        "An invalid raw draft cannot enter text mode and is not discarded.");
                    Select(1);
                    workspace.Inspector.Layout.TextMode.Invoke();
                    Require(workspace.Inspector.Value.Text == "Line1\rLine2", "CRLF string values use native paragraphs in text mode.");
                    int before = changes;
                    workspace.Inspector.Layout.Apply.Invoke();
                    Require(editor.Text == changedSource && changes == before, "Unchanged native paragraphs preserve the original CRLF escape spelling.");
                    Select(2);
                    workspace.Inspector.Layout.TextMode.Invoke();
                    Require(workspace.Inspector.Value.Text == "Raw \\ path", "Raw strings decode without changing their backslashes.");
                    workspace.Inspector.Layout.Apply.Invoke();
                    Require(editor.Text == changedSource && changes == before, "An unchanged raw string retains its exact delimiter spelling.");
                    Select(3);
                    Require(!workspace.Inspector.IsTextMode && workspace.Inspector.Value.ReadOnly &&
                        workspace.Inspector.Value.Text == "Title", "Expression-backed arguments remain read-only.");
                    Select(4);
                    Require(!workspace.Inspector.IsTextMode && workspace.Inspector.Value.Text == "\"\\0\"" &&
                        workspace.Inspector.Layout.ArgumentHelp.Text.Contains("NUL", StringComparison.Ordinal),
                        "Native-inexpressible string values remain in raw mode.");
                    Select(0);
                    workspace.Inspector.Layout.TextMode.Invoke();
                    workspace.Inspector.Value.Text = new string('\\', 40000);
                    workspace.Inspector.Layout.Apply.Invoke();
                    Require(!workspace.IsBusy && editor.Text == changedSource &&
                        workspace.Inspector.Layout.Feedback.Text.Contains("exceeds", StringComparison.Ordinal),
                        "An encoded literal beyond the source limit fails explicitly without editing source.");
                    editor.ReplaceRange(new(0, 0), editor.Text, "!");
                    string invalid = editor.Text;
                    workspace.ApplyProperty();
                    Require(editor.Text == invalid && workspace.Inspector.Value.ReadOnly && !workspace.Inspector.IsTextMode,
                        "A source change retires text mode and refuses a stale property edit.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                int beforeReset = 0;
                string resetSource = "";
                await Ui(() =>
                {
                    Select(0);
                    workspace.Inspector.ChooseArgument("fontSize");
                    beforeReset = changes;
                    workspace.Inspector.Layout.Reset.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    resetSource = editor.Text;
                    var node = workspace.Document!.Root!.Children[0];
                    Require(node.Arguments.All(argument => argument.Name != "fontSize") &&
                        node.Arguments.Any(argument => argument.Name == "wrapping" && argument.Value == "true"),
                        "Reset removes only the chosen named literal argument.");
                    Require(changes == beforeReset + 1, "A property reset emits exactly one native source change.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == changedSource, "One native Undo restores the exact named argument and separator.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == resetSource, "Native Redo restores the property reset.");
                    Select(0);
                    workspace.ResetProperty();
                    Require(editor.Text == resetSource && workspace.Inspector.Layout.Feedback.Text.Contains("Positional", StringComparison.Ordinal),
                        "The reset controller protects positional operands.");
                    workspace.Inspector.ChooseArgument("fontSize");
                    workspace.ResetProperty();
                    Require(editor.Text == resetSource && workspace.Inspector.Layout.Feedback.Text.Contains("not set", StringComparison.Ordinal),
                        "An absent property cannot create a reset edit.");
                    Select(3);
                    workspace.Inspector.ChooseArgument("ref");
                    workspace.ResetProperty();
                    Require(editor.Text == resetSource && workspace.Inspector.Layout.Feedback.Text.Contains("Expressions are read-only", StringComparison.Ordinal),
                        "Named expression-backed source remains unchanged by Reset.");
                    Require(errors.Count == 0, "Expected mode and edit refusals do not produce runtime failures.");
                });
            }
            catch (Exception error) { failure = error; }
            finally { window.Post(window.Close); }
        });
        window.Run();
        driver.GetAwaiter().GetResult();
        if (failure is not null) throw new InvalidOperationException("Text mode UI smoke failed.", failure);
        Console.WriteLine($"Designer text mode UI assertions: {assertions} passed.");

        void Select(int index)
        {
            var node = workspace.Document!.Root!.Children[index];
            editor.Selection = new((ulong)node.Span.Start, (ulong)node.Span.Start);
            workspace.SelectFromCaret();
            workspace.Inspector.ChooseArgument("value");
        }

        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
            assertions++;
        }

        async Task Ui(Action action)
        {
            var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!window.Post(() =>
            {
                try { action(); done.SetResult(); }
                catch (Exception error) { done.SetException(error); }
            })) throw new InvalidOperationException("The text mode test window rejected an action.");
            await done.Task.WaitAsync(TimeSpan.FromSeconds(15));
        }

        async Task Ready()
        {
            var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            await Ui(() =>
            {
                workspace.Changed += Check;
                Check();
                void Check()
                {
                    if (!workspace.IsCurrent || workspace.IsBusy) return;
                    workspace.Changed -= Check;
                    done.TrySetResult();
                }
            });
            await done.Task.WaitAsync(TimeSpan.FromSeconds(30));
        }
    }
}
