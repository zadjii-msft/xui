using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunDimensions()
    {
        const string original = """
            component DimensionModes { state float Width = 180; view { VStack() {
                Button("Fixed", size: ( 0x78, 40f ), help: "Keep help");
                TextInput("Preferred", preferredSize: (240, 60));
                Button("Expression", size: (Width, 40));
                Button("Commented", size: (120 /* keep */, 40));
            } } }
            """;
        using var window = new Window("Designer dimension editing", 1200, 1000);
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
        var driver = Task.Run(Drive);
        window.Run();
        driver.GetAwaiter().GetResult();
        Console.WriteLine($"Designer dimension UI assertions: {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60));
            try
            {
                await Ui(workspace.SourceChanged);
                await Ready();
                string initial = "", prior = "";
                await Ui(() =>
                {
                    initial = editor.Text;
                    editor.ReplaceRange(new((ulong)editor.Text.Length, (ulong)editor.Text.Length), editor.Text, "\r// Prior typing");
                    prior = editor.Text;
                });
                await Ready();
                await Ui(() =>
                {
                    Select(0, "size");
                    Require(!workspace.Inspector.IsDimensionMode && !workspace.Inspector.Value.ReadOnly,
                        "Literal source remains the default for dimension arguments.");
                    workspace.Inspector.Layout.DimensionMode.Invoke();
                    Require(workspace.Inspector.IsDimensionMode && workspace.Inspector.Layout.DimensionsOpen &&
                        workspace.Inspector.Value.ReadOnly && workspace.Inspector.Layout.DimensionWidth.Text == "0x78" &&
                        workspace.Inspector.Layout.DimensionHeight.Text == "40f",
                        "Dimension mode exposes exact width and height spelling in native fields.");
                });
                await Ui(() =>
                {
                    workspace.Inspector.FocusValue();
                    Require(workspace.Inspector.Value.GetBounds().Height == 0 &&
                        workspace.Inspector.Layout.DimensionWidth.GetBounds().Height >= 32 &&
                        workspace.Inspector.Layout.DimensionHeight.GetBounds().X > workspace.Inspector.Layout.DimensionWidth.GetBounds().X,
                        $"The dimension fields replace the raw editor area instead of stacking over it: raw={workspace.Inspector.Value.GetBounds()}, " +
                        $"width={workspace.Inspector.Layout.DimensionWidth.GetBounds()}, height={workspace.Inspector.Layout.DimensionHeight.GetBounds()}.");
                    Require(workspace.Inspector.Layout.DimensionWidth.Focused, "Property focus targets the active dimension field, not the hidden raw editor.");
                    workspace.Inspector.Layout.Apply.Invoke();
                    Require(editor.Text == prior && changes == 1 && !workspace.IsBusy &&
                        workspace.Inspector.Layout.Feedback.Text.Contains("unchanged", StringComparison.Ordinal),
                        "Unchanged dimensions preserve the exact tuple without a source edit.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "No-op dimensions preserve the preceding native undo unit.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                string changed = "";
                int before = 0;
                await Ui(() =>
                {
                    Select(0, "size");
                    workspace.Inspector.Layout.DimensionMode.Invoke();
                    workspace.Inspector.Layout.DimensionWidth.Text = "240f";
                    workspace.Inspector.Layout.DimensionMode.Invoke();
                    Require(!workspace.Inspector.IsDimensionMode && !workspace.Inspector.Value.ReadOnly &&
                        workspace.Inspector.Value.Text == "( 240f, 40f )" && editor.Text == prior,
                        "Returning to raw mode preserves an unapplied dimension draft and tuple whitespace.");
                    workspace.Inspector.Layout.DimensionMode.Invoke();
                    Require(workspace.Inspector.Layout.DimensionWidth.Text == "240f",
                        "Reentering dimension mode uses the unapplied raw draft.");
                    workspace.Inspector.Layout.DimensionWidth.Text = "Width + 1";
                    workspace.Inspector.Layout.Apply.Invoke();
                    Require(editor.Text == prior && !workspace.IsBusy && workspace.Inspector.IsDimensionMode &&
                        workspace.Inspector.Layout.Feedback.Text.Contains("numeric literal", StringComparison.Ordinal),
                        "Dimension fields reject expressions before a source transaction.");
                    workspace.Inspector.Layout.DimensionMode.Invoke();
                    Require(workspace.Inspector.IsDimensionMode && workspace.Inspector.Layout.DimensionsOpen &&
                        workspace.Inspector.Layout.DimensionWidth.Text == "Width + 1",
                        "Failed conversion retains the invalid draft and the active mode.");
                    workspace.Inspector.Layout.DimensionWidth.Text = "240f";
                    workspace.Inspector.Layout.DimensionHeight.Text = "-1";
                    workspace.Inspector.Layout.Apply.Invoke();
                    Require(editor.Text == prior && !workspace.IsBusy &&
                        workspace.Inspector.Layout.Feedback.Text.Contains("height", StringComparison.Ordinal),
                        "Negative dimensions report the offending field without editing source.");
                    workspace.Inspector.Layout.DimensionHeight.Text = "60f";
                    before = changes;
                    workspace.Inspector.Layout.Apply.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    changed = editor.Text;
                    Require(changed.Contains("size: ( 240f, 60f ), help: \"Keep help\"", StringComparison.Ordinal) &&
                        changes == before + 1 && !workspace.Inspector.IsDimensionMode,
                        "Applying dimensions compiles one source edit and preserves unrelated arguments.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == prior, "One native undo restores exact numeric spelling and source whitespace.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == changed, "Native redo restores the compiled dimension change.");
                    Select(1, "preferredSize");
                    workspace.Inspector.Layout.DimensionMode.Invoke();
                    workspace.Inspector.Layout.DimensionWidth.Text = "320";
                    workspace.Inspector.Layout.DimensionHeight.Text = "72";
                    workspace.Inspector.Layout.Apply.Invoke();
                });
                await Ready();
                string latest = "";
                await Ui(() =>
                {
                    latest = editor.Text;
                    Require(latest.Contains("preferredSize: (320, 72)", StringComparison.Ordinal),
                        "Preferred dimensions use the same compiled edit workflow as fixed size.");
                    Select(2, "size");
                    Require(!workspace.Inspector.IsDimensionMode && workspace.Inspector.Value.ReadOnly &&
                        workspace.Inspector.Value.Text == "(Width, 40)", "Expression-backed size remains read-only.");
                    Select(3, "size");
                    Require(!workspace.Inspector.IsDimensionMode && !workspace.Inspector.Value.ReadOnly &&
                        workspace.Inspector.Layout.ArgumentHelp.Text.Contains("comments", StringComparison.Ordinal),
                        "Commented literal tuples remain editable only as raw source.");
                    Select(0, "size");
                    workspace.Inspector.Value.Text = "(Width, 40)";
                    workspace.Inspector.Layout.DimensionMode.Invoke();
                    Require(!workspace.Inspector.IsDimensionMode && workspace.Inspector.Value.Text == "(Width, 40)" &&
                        editor.Text == latest, "An invalid raw draft cannot enter dimension mode or lose its text.");
                    Select(0, "size");
                    workspace.Inspector.Layout.DimensionMode.Invoke();
                    workspace.Inspector.Layout.Reset.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    Require(workspace.Document!.Root!.Children[0].Arguments.All(argument => argument.Name != "size") &&
                        !workspace.Inspector.IsDimensionMode, "Reset removes an authored size through the existing guarded source transaction.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == latest, "Undo restores a size reset from dimension mode.");
                    Select(0, "size");
                    workspace.Inspector.Layout.DimensionMode.Invoke();
                    workspace.Inspector.Layout.DimensionWidth.Text = "200";
                    editor.ReplaceRange(new(0, 0), editor.Text, "!");
                    string invalid = editor.Text;
                    workspace.ApplyProperty();
                    Require(editor.Text == invalid && !workspace.Inspector.IsDimensionMode &&
                        !workspace.Inspector.Layout.DimensionsOpen && workspace.Inspector.Value.ReadOnly,
                        "A source revision retires dimension mode and refuses stale property input.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() => Require(editor.Text == latest && errors.Count == 0,
                    "Native undo recovers source and expected field errors do not cause runtime failures."));
            }
            finally { window.Post(window.Close); }

            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The dimension test window rejected an action.");
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
            workspace.Inspector.ChooseArgument(argument);
        }

        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
            assertions++;
        }
    }
}
