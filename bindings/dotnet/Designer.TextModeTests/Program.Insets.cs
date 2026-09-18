using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunInsets(VisualStyle style)
    {
        const string original = """
            component InsetModes { view { VStack(padding: 8) {
                Button("Uniform", padding: 0x8, borderThickness: ( 1, 2f, 3, 4 ), help: "Keep");
                Text("Commented", padding: (1, /*keep*/ 2, 3, 4));
            } } }
            """;
        using var window = new Window("Designer inset editing", 1200, 1000, visualStyle: style);
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
        Console.WriteLine($"Designer inset UI assertions ({style}): {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60));
            try
            {
                await Ui(workspace.SourceChanged);
                await Ready();
                string initial = "", prior = "", changed = "";
                await Ui(() =>
                {
                    initial = editor.Text;
                    editor.ReplaceRange(new((ulong)editor.Text.Length, (ulong)editor.Text.Length), editor.Text, "\r// Prior typing");
                    prior = editor.Text;
                });
                await Ready();
                await Ui(() =>
                {
                    Select(0, "padding");
                    Require(!inspector.IsInsetsMode && !inspector.Value.ReadOnly, "Raw source remains the initial mode.");
                    inspector.Layout.InsetsMode.Invoke();
                    Require(inspector.IsInsetsMode && inspector.Layout.InsetsOpen && inspector.Value.ReadOnly &&
                        inspector.Layout.InsetLeft.Text == "0x8" && inspector.Layout.InsetTop.Text == "0x8" &&
                        inspector.Layout.InsetRight.Text == "0x8" && inspector.Layout.InsetBottom.Text == "0x8",
                        "Uniform padding populates all four native fields without changing spelling.");
                });
                await Ui(() =>
                {
                    inspector.FocusValue();
                    Require(inspector.Layout.InsetLeft.Focused && inspector.Value.GetBounds().Height == 0 &&
                        inspector.Layout.InsetLeft.GetBounds().Height >= 32 &&
                        inspector.Layout.InsetTop.GetBounds().X > inspector.Layout.InsetLeft.GetBounds().X &&
                        inspector.Layout.InsetRight.GetBounds().Y > inspector.Layout.InsetLeft.GetBounds().Y &&
                        inspector.Layout.InsetBottom.GetBounds().X > inspector.Layout.InsetRight.GetBounds().X,
                        "Four native fields replace the raw editor in two rows, and property focus targets Left.");
                    inspector.Layout.Apply.Invoke();
                    Require(editor.Text == prior && changes == 1 && !workspace.IsBusy &&
                        inspector.Layout.Feedback.Text.Contains("unchanged", StringComparison.Ordinal),
                        "A no-op keeps the exact scalar and creates no source edit.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "No-op inset editing preserves the previous native undo unit.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                await Ui(() =>
                {
                    Select(0, "padding");
                    inspector.Layout.InsetsMode.Invoke();
                    inspector.Layout.InsetTop.Text = "12f";
                    inspector.Layout.InsetsMode.Invoke();
                    Require(!inspector.IsInsetsMode && inspector.Value.Text == "(0x8, 12f, 0x8, 0x8)" && editor.Text == prior,
                        "Returning to raw mode preserves the unapplied four-sided draft.");
                    inspector.Layout.InsetsMode.Invoke();
                    Require(inspector.Layout.InsetTop.Text == "12f", "Mode reentry uses the current raw draft.");
                    inspector.Layout.InsetBottom.Text = "32769";
                    inspector.Layout.Apply.Invoke();
                    Require(editor.Text == prior && !workspace.IsBusy && inspector.Layout.Feedback.Text.Contains("bottom", StringComparison.Ordinal),
                        "Out-of-range insets identify the field without changing source.");
                    inspector.Layout.InsetsMode.Invoke();
                    Require(inspector.IsInsetsMode && inspector.Layout.InsetsOpen && inspector.Layout.InsetBottom.Text == "32769",
                        "Failed raw conversion preserves the active invalid draft.");
                    inspector.Layout.InsetBottom.Text = "16";
                    inspector.Layout.Apply.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    changed = editor.Text;
                    Require(changed.Contains("padding: (0x8, 12f, 0x8, 16)", StringComparison.Ordinal) &&
                        changed.Contains("help: \"Keep\"", StringComparison.Ordinal) && !inspector.IsInsetsMode && changes == 4,
                        "Applying four sides compiles one native edit, preserves other arguments, and clears the mode.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == prior, "One undo restores exact original padding syntax.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == changed, "Redo restores the inset edit.");
                    Select(0, "borderThickness");
                    inspector.Layout.InsetsMode.Invoke();
                    inspector.Layout.InsetRight.Text = "6";
                    inspector.Layout.Apply.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text.Contains("borderThickness: ( 1, 2f, 6, 4 )", StringComparison.Ordinal),
                        "Border thickness uses the same editor and retains tuple whitespace.");
                    Select(1, "padding");
                    Require(!inspector.IsInsetsMode && !inspector.Value.ReadOnly &&
                        inspector.Layout.ArgumentHelp.Text.Contains("Comments", StringComparison.Ordinal),
                        "Commented tuples remain in raw mode.");
                    Select(-1, "padding");
                    Require(!inspector.Layout.InsetsArgument && !inspector.IsInsetsMode,
                        "Stack structural padding remains a uniform scalar, not a four-sided style.");
                    Select(0, "padding");
                    inspector.Value.Text = "(Width, 2, 3, 4)";
                    inspector.Layout.InsetsMode.Invoke();
                    Require(!inspector.IsInsetsMode && inspector.Value.Text == "(Width, 2, 3, 4)",
                        "An expression draft cannot enter inset mode or lose its text.");
                    Select(0, "padding");
                    inspector.Layout.InsetsMode.Invoke();
                    inspector.Layout.Reset.Invoke();
                });
                await Ready();
                string latest = "";
                await Ui(() =>
                {
                    Require(workspace.Document!.Root!.Children[0].Arguments.All(argument => argument.Name != "padding") &&
                        !inspector.IsInsetsMode, "Reset removes authored padding through the existing guarded transaction.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    latest = editor.Text;
                    Require(latest.Contains("padding: (0x8, 12f, 0x8, 16)", StringComparison.Ordinal), "Undo restores the reset inset.");
                    Select(0, "padding");
                    inspector.Layout.InsetsMode.Invoke();
                    inspector.Layout.InsetLeft.Text = "20";
                    editor.ReplaceRange(new(0, 0), editor.Text, "!");
                    string invalid = editor.Text;
                    workspace.ApplyProperty();
                    Require(editor.Text == invalid && !inspector.IsInsetsMode && !inspector.Layout.InsetsOpen && inspector.Value.ReadOnly,
                        "New source invalidates inset drafts and refuses stale application.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() => Require(editor.Text == latest && errors.Count == 0 && window.CallbackStatus == 0,
                    "Undo recovers the document without native callback errors."));
            }
            finally { window.Post(window.Close); }

            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The inset test window rejected an action.");
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
            var node = index < 0 ? workspace.Document!.Root! : workspace.Document!.Root!.Children[index];
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
