using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunPropertySource(VisualStyle style)
    {
        const string original = """
            component PropertyLocations { state string Caption = "Changing"; view { VStack() {
                Text("ROCKET");
                Button(Caption, ref: ActionButton, help: /* keep */ @"Line 1
            Line 2", size: (120, 40), padding: 8, enabled: true,
                    visible: Caption.Length >
                        0, click: Activate);
            } }
            code csharp { void Activate() { Caption = "Clicked"; } }
            }
            """;
        using var window = new Window("Designer property source navigation", 1200, 1000, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument(original.Replace("ROCKET", "\U0001F680", StringComparison.Ordinal));
        var errors = new List<string>();
        using var workspace = new DesignerWorkspace(window, editor, errors.Add);
        var inspector = workspace.Inspector;
        int changes = 0, selections = 0, assertions = 0;
        editor.Event += value =>
        {
            if (value.Kind != EventKind.Change) return;
            changes++;
            workspace.SourceChanged();
        };
        workspace.SelectionChanged += () => selections++;
        window.SetContent(window.Stack(Axis.Horizontal).Padding(10).Spacing(10)
            .Add(editor, 1).Add(workspace.Hierarchy.Layout.Root, 1).Add(inspector.Layout.Root, 1));
        var driver = Task.Run(Drive);
        window.Run();
        driver.GetAwaiter().GetResult();
        Console.WriteLine($"Designer property source assertions ({style}): {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60));
            string initial = "", prior = "", latest = "";
            try
            {
                await Ui(workspace.SourceChanged);
                await Ready();
                await Ui(() =>
                {
                    initial = editor.Text;
                    editor.ReplaceRange(new((ulong)editor.Text.Length, (ulong)editor.Text.Length), editor.Text, "\r// Prior typing");
                    prior = editor.Text;
                });
                await Ready();
                await Ui(() =>
                {
                    Select(1, "value");
                    Require(inspector.Value.ReadOnly && workspace.CanRevealPropertySource,
                        "An authored expression is read-only in the inspector but available for source navigation.");
                });
                await Ui(() =>
                {
                    Reveal();
                    Require(SelectedText() == "Caption" && editor.Selection.Start ==
                        (ulong)(editor.Text.IndexOf("Button(Caption", StringComparison.Ordinal) + "Button(".Length),
                        "Expression navigation selects its exact UTF-16 occurrence after a supplementary Unicode character.");
                    Select(1, "help");
                    inspector.Layout.TextMode.Invoke();
                    inspector.Value.Text = "Unapplied text";
                    inspector.Layout.ArgumentFilter.Text = "enabled";
                    inspector.FilterArguments();
                    inspector.Layout.AuthoredOnly.Invoke();
                    Reveal();
                    Require(SelectedText() == "@\"Line 1\rLine 2\"" && inspector.IsTextMode && inspector.Value.Text == "Unapplied text" &&
                        inspector.Layout.ArgumentFilter.Text == "enabled" &&
                        inspector.Layout.ArgumentFilterStatus.Text == "1 matching property. The current property stays available.",
                        "Multiline value navigation excludes preceding comments and preserves the text draft and both filters.");
                    inspector.Layout.TextMode.Invoke();
                    Reveal();
                    Require(inspector.Value.Text == "\"Unapplied text\"" && SelectedText().StartsWith("@\"", StringComparison.Ordinal),
                        "Navigation uses the authored literal, not an unapplied raw draft.");
                    Select(1, "size");
                    inspector.Layout.DimensionMode.Invoke();
                    inspector.Layout.DimensionWidth.Text = "240";
                    Reveal();
                    Require(inspector.IsDimensionMode && inspector.Layout.DimensionWidth.Text == "240" && SelectedText() == "(120, 40)",
                        "Dimension drafts remain intact while their authored tuple is selected.");
                    Select(1, "padding");
                    inspector.Layout.InsetsMode.Invoke();
                    inspector.Layout.InsetTop.Text = "Not a number";
                    Reveal();
                    Require(inspector.IsInsetsMode && inspector.Layout.InsetTop.Text == "Not a number" && SelectedText() == "8",
                        "Even invalid inset drafts remain intact during source navigation.");
                    Select(1, "enabled");
                    inspector.Layout.BooleanMode.Invoke();
                    inspector.Layout.BooleanValue.Invoke();
                    Reveal();
                    Require(inspector.IsBooleanMode && inspector.TryReadLiteral(out string boolean, out _) && boolean == "false" &&
                        SelectedText() == "true", "Boolean navigation selects authored source rather than applying a draft.");
                    Select(1, "ref");
                    Reveal();
                    Require(inspector.Value.ReadOnly && SelectedText() == "ActionButton", "References can be located without enabling literal editing.");
                    Select(1, "click");
                    Reveal();
                    Require(inspector.Value.ReadOnly && SelectedText() == "Activate",
                        "Event navigation selects the authored handler reference without executing or resolving it.");
                    Select(1, "visible");
                    Reveal();
                    Require(inspector.Value.ReadOnly && SelectedText().Contains('\r') &&
                        SelectedText().StartsWith("Caption.Length >", StringComparison.Ordinal) && SelectedText().EndsWith('0'),
                        "The complete multiline expression is selected without evaluation.");
                    Select(1, "id");
                    inspector.Value.Text = "\"draft-id\"";
                    var selection = editor.Selection;
                    Require(!workspace.CanRevealPropertySource, "An unset property has no authored value to reveal.");
                    workspace.RevealPropertySource();
                    Require(editor.Selection == selection && inspector.Value.Text == "\"draft-id\"" &&
                        inspector.Layout.Feedback.Text.Contains("not set", StringComparison.Ordinal),
                        "Unset-property refusal preserves the source selection and draft.");
                    Select(0, "value");
                    Reveal();
                    Require(SelectedText() == "\"\U0001F680\"" && changes == 1 && editor.Text == prior,
                        "Supplementary Unicode literals remain whole, and all navigation leaves source unchanged.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "Property navigation creates no source undo entries.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                await Ui(() =>
                {
                    Select(1, "value");
                    Reveal();
                    editor.ReplaceRange(editor.Selection, editor.Text, "\"Direct edit\"");
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text.Contains("Button(\"Direct edit\"", StringComparison.Ordinal),
                        "Native source replacement edits exactly the revealed expression.");
                    Select(1, "size");
                    inspector.Layout.DimensionMode.Invoke();
                    inspector.Layout.DimensionWidth.Text = "240";
                    inspector.Layout.Apply.Invoke();
                    var selection = editor.Selection;
                    Require(workspace.IsBusy && !workspace.CanRevealPropertySource, "Pending visual edits disable property source navigation.");
                    workspace.RevealPropertySource();
                    Require(editor.Selection == selection && inspector.Layout.Feedback.Text.Contains("Wait", StringComparison.Ordinal),
                        "Direct navigation during compilation reports a busy refusal without moving selection.");
                });
                await Ready();
                await Ui(() =>
                {
                    latest = editor.Text;
                    Require(latest.Contains("size: (240, 40)", StringComparison.Ordinal),
                        "The component still compiles after a revealed expression is edited.");
                    Select(1, "size");
                    editor.ReplaceRange(new(0, 0), editor.Text, "!");
                    var selection = editor.Selection;
                    Require(!workspace.CanRevealPropertySource, "New source invalidates the old argument location.");
                    workspace.RevealPropertySource();
                    Require(editor.Selection == selection && inspector.Layout.Feedback.Text.Contains("stale", StringComparison.Ordinal),
                        "A stale argument span cannot move the current source selection.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() => Require(editor.Text == latest && errors.Count == 0 && window.CallbackStatus == 0,
                    "Undo restores the current source without native callback errors."));
            }
            finally { window.Post(window.Close); }

            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The property source test window rejected an action.");
                await done.Task.WaitAsync(deadline.Token);
            }

            async Task Ready()
            {
                while (true)
                {
                    bool ready = false;
                    await Ui(() =>
                    {
                        ready = workspace.IsCurrent && !workspace.IsBusy;
                        if (workspace.Hierarchy.Layout.Status.Text.StartsWith("Invalid source", StringComparison.Ordinal))
                            throw new InvalidOperationException("Property source fixture is invalid: " + inspector.Layout.Feedback.Text);
                    });
                    if (ready) return;
                    await Task.Delay(15, deadline.Token);
                }
            }
        }

        void Select(int index, string name)
        {
            var node = workspace.Document!.Root!.Children[index];
            editor.Selection = new((ulong)node.Span.Start, (ulong)node.Span.Start);
            workspace.SelectFromCaret();
            inspector.ChooseArgument(name);
        }

        void Reveal()
        {
            var node = workspace.Hierarchy.Selection!;
            var key = workspace.Hierarchy.Key(node);
            var value = node.Arguments.Single(argument => argument.Name == inspector.Argument);
            string source = editor.Text;
            int before = selections;
            workspace.RevealPropertySource();
            Require(editor.Focused && editor.Selection == new TextSelection((ulong)value.ValueSpan.Start, (ulong)value.ValueSpan.End) &&
                ReferenceEquals(workspace.Hierarchy.Selection, node) && workspace.Hierarchy.Key(node) == key &&
                selections == before + 1 && editor.Text == source,
                "Source navigation focuses the exact authored value, retains hierarchy identity, and sends one outline notification.");
        }

        string SelectedText() => editor.Text.Substring((int)editor.Selection.Start, (int)(editor.Selection.End - editor.Selection.Start));

        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException($"{style}: {message}");
            assertions++;
        }
    }
}
