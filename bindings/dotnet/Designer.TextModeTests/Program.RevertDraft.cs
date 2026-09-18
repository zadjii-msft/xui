using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunRevertDraft(VisualStyle style)
    {
        const string original = """
            component RevertDrafts { state bool Active = true; view { VStack() {
                Button("Keep", help: "Keep help", size: ( 0x78, 40f ), padding: 8, enabled: true);
                Toggle("Expression", checked: Active);
            } } }
            """;
        using var window = new Window("Designer revert property drafts", 1200, 1000, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument(original);
        var errors = new List<string>();
        using var workspace = new DesignerWorkspace(window, editor, errors.Add);
        var inspector = workspace.Inspector;
        int changes = 0, assertions = 0;
        editor.Event += value =>
        {
            if (value.Kind != EventKind.Change) return;
            changes++;
            workspace.SourceChanged();
        };
        window.SetContent(window.Stack(Axis.Horizontal).Padding(10).Spacing(10)
            .Add(editor, 1).Add(workspace.Hierarchy.Layout.Root, 1).Add(inspector.Layout.Root, 1));
        var driver = Task.Run(Drive);
        window.Run();
        driver.GetAwaiter().GetResult();
        Console.WriteLine($"Designer draft revert assertions ({style}): {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60));
            string initial = "", prior = "", applied = "";
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
                    Select(0, "help");
                    inspector.Value.Text = "not a valid literal";
                    inspector.Layout.ArgumentFilter.Text = "size";
                    inspector.FilterArguments();
                    inspector.Layout.AuthoredOnly.Invoke();
                    inspector.Layout.RevertDraft.Focus();
                });
                await Ui(() =>
                {
                    var button = inspector.Layout.RevertDraft.GetBounds();
                    var panel = inspector.Layout.Root.GetBounds();
                    Require(button.Width >= 100 && button.Y >= panel.Y && button.Y + button.Height <= panel.Y + panel.Height,
                        "The native Revert draft button is visible through inspector scrolling.");
                    var selection = editor.Selection;
                    Require(workspace.CanRevertPropertyDraft, "An invalid raw draft remains revertible.");
                    inspector.Layout.RevertDraft.Invoke();
                    Require(inspector.Value.Text == "\"Keep help\"" && inspector.Argument == "help" && inspector.Value.Focused &&
                        editor.Text == prior && editor.Selection == selection && changes == 1 && !workspace.IsBusy,
                        "Revert restores the authored literal and editor focus without changing source, selection, or undo.");
                    Require(inspector.Layout.ArgumentFilter.Text == "size" &&
                        inspector.Layout.ArgumentFilterStatus.Text == "1 matching property. The current property stays available.",
                        "Revert preserves both property filters and the current property outside their results.");
                    inspector.Layout.Apply.Invoke();
                    Require(changes == 1 && !workspace.IsBusy &&
                        inspector.Layout.Feedback.Text.Contains("unchanged", StringComparison.Ordinal),
                        "Apply after a revert is a source no-op.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "Revert leaves the previous source undo entry intact.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                await Ui(() =>
                {
                    Select(0, "help");
                    inspector.Layout.TextMode.Invoke();
                    inspector.Value.Text = "Different \"text\"\rDraft";
                    inspector.Layout.RevertDraft.Invoke();
                    Require(inspector.IsTextMode && inspector.Value.Text == "Keep help" && inspector.Value.Focused,
                        "Text-mode reversion restores decoded text and keeps text mode active.");
                    inspector.Layout.TextMode.Invoke();
                    Require(!inspector.IsTextMode && inspector.Value.Text == "\"Keep help\"",
                        "The native text-mode toggle remains synchronized after reversion.");
                    Select(0, "size");
                    inspector.Layout.DimensionMode.Invoke();
                    inspector.Layout.DimensionWidth.Text = "Width + 1";
                    inspector.Layout.DimensionHeight.Text = "-1";
                    inspector.Layout.RevertDraft.Invoke();
                    Require(inspector.IsDimensionMode && inspector.Layout.DimensionsOpen &&
                        inspector.Layout.DimensionWidth.Text == "0x78" && inspector.Layout.DimensionHeight.Text == "40f" &&
                        inspector.Layout.DimensionWidth.Focused,
                        "Invalid dimension drafts revert to exact authored field spelling without leaving the mode.");
                    inspector.Layout.DimensionMode.Invoke();
                    Require(!inspector.IsDimensionMode && inspector.Value.Text == "( 0x78, 40f )",
                        "Reverted dimensions retain the original tuple whitespace.");
                    Select(0, "enabled");
                    inspector.Layout.BooleanMode.Invoke();
                    inspector.Layout.BooleanValue.Invoke();
                    inspector.Layout.RevertDraft.Invoke();
                    Require(inspector.IsBooleanMode && inspector.Layout.BooleanValue.Focused &&
                        inspector.TryReadLiteral(out string boolean, out _) && boolean == "true" &&
                        inspector.Layout.BooleanValue.Text == "Value: true",
                        "Boolean reversion restores the authored toggle value, caption, and active focus.");
                    inspector.Layout.BooleanMode.Invoke();
                    Require(!inspector.IsBooleanMode && inspector.Value.Text == "true", "The native boolean-mode toggle remains synchronized.");
                    Select(0, "padding");
                    inspector.Layout.InsetsMode.Invoke();
                    inspector.Layout.InsetLeft.Text = "24";
                    inspector.Layout.InsetTop.Text = "99999";
                    inspector.Layout.InsetRight.Text = "Bad";
                    inspector.Layout.InsetBottom.Text = "-1";
                    inspector.Layout.RevertDraft.Invoke();
                    Require(inspector.IsInsetsMode && inspector.Layout.InsetsOpen && inspector.Layout.InsetLeft.Focused &&
                        inspector.Layout.InsetLeft.Text == "8" && inspector.Layout.InsetTop.Text == "8" &&
                        inspector.Layout.InsetRight.Text == "8" && inspector.Layout.InsetBottom.Text == "8",
                        "All four inset fields revert together, including invalid input.");
                    inspector.Layout.InsetsMode.Invoke();
                    Require(!inspector.IsInsetsMode && inspector.Value.Text == "8", "Uniform inset syntax survives reversion.");
                    Select(0, "id");
                    inspector.Value.Text = "\"unapplied-id\"";
                    inspector.Layout.RevertDraft.Invoke();
                    Require(inspector.Value.Text == "" && inspector.Value.Focused &&
                        inspector.Layout.Feedback.Text.Contains("not set", StringComparison.Ordinal),
                        "Revert clears the draft for an unset property without adding or removing an argument.");
                    Select(1, "checked");
                    Require(!workspace.CanRevertPropertyDraft, "An authored expression has no editable draft to revert.");
                    workspace.RevertPropertyDraft();
                    Require(inspector.Value.ReadOnly && inspector.Value.Text == "Active" &&
                        inspector.Layout.Feedback.Text.Contains("editable literal", StringComparison.Ordinal),
                        "Direct reversion of an expression reports a refusal and preserves its source.");
                    Require(editor.Text == prior && changes == 3, "All draft reversions leave the source and its undo history unchanged.");
                    Select(0, "help");
                    inspector.Value.Text = "\"Applied help\"";
                    inspector.Layout.Apply.Invoke();
                    Require(!workspace.CanRevertPropertyDraft && workspace.IsBusy, "Pending visual edits disable draft reversion.");
                    workspace.RevertPropertyDraft();
                    Require(inspector.Layout.Feedback.Text.Contains("Wait", StringComparison.Ordinal),
                        "Direct reversion during compilation reports a busy refusal.");
                });
                await Ready();
                await Ui(() =>
                {
                    applied = editor.Text;
                    Require(applied.Contains("help: \"Applied help\"", StringComparison.Ordinal), "Property editing still compiles after draft reversion.");
                    Select(0, "help");
                    inspector.Value.Text = "\"Discard this\"";
                    inspector.Layout.RevertDraft.Invoke();
                    Require(inspector.Value.Text == "\"Applied help\"" && editor.Text == applied,
                        "Revert uses the latest authored value, not an earlier source revision.");
                    inspector.Value.Text = "\"Stale draft\"";
                    editor.ReplaceRange(new(0, 0), editor.Text, "!");
                    string invalid = editor.Text;
                    Require(!workspace.CanRevertPropertyDraft, "Stale source disables draft reversion.");
                    workspace.RevertPropertyDraft();
                    Require(editor.Text == invalid && inspector.Value.ReadOnly &&
                        inspector.Layout.Feedback.Text.Contains("stale", StringComparison.Ordinal),
                        "Revert cannot republish a stale draft as current.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == applied, "Undo recovers the current authored value after invalid source.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == prior, "One further undo removes only the actual property edit.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() => Require(editor.Text == initial && errors.Count == 0 && window.CallbackStatus == 0,
                    "The original source undo entry remains available without native callback errors."));
            }
            finally { window.Post(window.Close); }

            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The draft revert test window rejected an action.");
                await done.Task.WaitAsync(deadline.Token);
            }

            async Task Ready()
            {
                while (true)
                {
                    bool ready = false;
                    await Ui(() => ready = workspace.IsCurrent && !workspace.IsBusy);
                    if (ready) return;
                    await Task.Delay(15, deadline.Token);
                }
            }
        }

        void Select(int index, string argument)
        {
            var node = workspace.Document!.Root!.Children[index];
            editor.Selection = new((ulong)node.Span.Start, (ulong)node.Span.Start);
            workspace.SelectFromCaret();
            inspector.ChooseArgument(argument);
        }

        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException($"{style}: {message}");
            assertions++;
        }
    }
}
