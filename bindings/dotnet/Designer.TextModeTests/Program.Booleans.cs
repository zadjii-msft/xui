using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunBooleans(VisualStyle style)
    {
        const string original = """
            component BooleanModes { state bool Active = true; view { VStack() {
                Button("Flag", enabled: true, visible: false, help: "Keep help");
                Toggle("Choice", checked: false);
                Button("Expression", enabled: Active);
                Button("Commented", enabled: /* keep */ true);
            } } }
            """;
        using var window = new Window("Designer boolean editing", 1200, 1000, visualStyle: style);
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
        Console.WriteLine($"Designer boolean UI assertions ({style}): {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60));
            string initial = "", prior = "", changed = "";
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
                    Select(0, "enabled");
                    Require(!inspector.IsBooleanMode && !inspector.Value.ReadOnly, "Boolean properties start as editable raw literals.");
                    inspector.Layout.BooleanMode.Invoke();
                    Require(inspector.IsBooleanMode && inspector.Layout.BooleanOpen && inspector.Value.ReadOnly &&
                        inspector.Layout.BooleanValue.Text == "Value: true", "Boolean mode exposes the authored value through a native toggle.");
                });
                await Ui(() =>
                {
                    inspector.FocusValue();
                    Require(inspector.Layout.BooleanValue.Focused && inspector.Layout.BooleanValue.GetBounds().Height >= 24 &&
                        inspector.Value.GetBounds().Height == 0, "The active boolean toggle replaces the raw editor and receives property focus.");
                    inspector.Layout.Apply.Invoke();
                    Require(editor.Text == prior && changes == 1 && !workspace.IsBusy &&
                        inspector.Layout.Feedback.Text.Contains("unchanged", StringComparison.Ordinal), "An unchanged boolean creates no source or undo edit.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "A no-op boolean preserves the preceding native undo operation.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                await Ui(() =>
                {
                    Select(0, "enabled");
                    inspector.Layout.BooleanMode.Invoke();
                    inspector.Layout.BooleanValue.Invoke();
                    Require(inspector.Layout.BooleanValue.Text == "Value: false" && editor.Text == prior,
                        "The native toggle changes a draft without editing source.");
                    inspector.Layout.BooleanMode.Invoke();
                    Require(!inspector.IsBooleanMode && !inspector.Value.ReadOnly && inspector.Value.Text == "false",
                        "Returning to raw mode preserves the boolean draft.");
                    inspector.Layout.BooleanMode.Invoke();
                    Require(inspector.Layout.BooleanValue.Text == "Value: false", "Reentering boolean mode reads the current raw draft.");
                    inspector.Layout.BooleanValue.Invoke();
                    inspector.Layout.Apply.Invoke();
                    Require(editor.Text == prior && changes == 3 && !workspace.IsBusy,
                        "Toggling back to the authored value remains an exact no-op.");
                    inspector.Layout.BooleanValue.Invoke();
                    inspector.Layout.Apply.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    changed = editor.Text;
                    Require(changed.Contains("enabled: false, visible: false, help: \"Keep help\"", StringComparison.Ordinal) &&
                        changes == 4 && !inspector.IsBooleanMode, "Apply compiles one native source edit and preserves other arguments.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == prior, "One native undo restores the exact boolean source.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == changed, "Native redo restores the applied boolean.");
                    Select(1, "checked");
                    inspector.Layout.BooleanMode.Invoke();
                    Require(inspector.Layout.BooleanValue.Text == "Value: false", "False authored values initialize an unchecked draft.");
                    inspector.Layout.BooleanValue.Invoke();
                    inspector.Layout.Apply.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text.Contains("checked: true", StringComparison.Ordinal), "The same editor handles literal checked properties.");
                    Select(2, "enabled");
                    Require(inspector.Value.ReadOnly && !inspector.Layout.BooleanArgument && !inspector.IsBooleanMode,
                        "Expressions remain read-only and do not appear as boolean literals.");
                    Select(3, "enabled");
                    Require(inspector.Value.Text == "true", "Source comments outside the literal do not enter its editable value span.");
                    inspector.Layout.BooleanMode.Invoke();
                    inspector.Layout.BooleanValue.Invoke();
                    inspector.Layout.Apply.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text.Contains("enabled: /* keep */ false", StringComparison.Ordinal),
                        "Boolean edits preserve authored comments outside the literal span.");
                    Select(0, "enabled");
                    inspector.Value.Text = "/* draft */ false";
                    inspector.Layout.BooleanMode.Invoke();
                    Require(!inspector.IsBooleanMode && inspector.Value.Text == "/* draft */ false" &&
                        inspector.Layout.Feedback.Text.Contains("comments", StringComparison.Ordinal),
                        "A raw draft with comments stays intact and cannot enter boolean mode.");
                    Select(0, "enabled");
                    inspector.Value.Text = "Active";
                    inspector.Layout.BooleanMode.Invoke();
                    Require(!inspector.IsBooleanMode && inspector.Value.Text == "Active" &&
                        inspector.Layout.Feedback.Text.Contains("true or false", StringComparison.Ordinal),
                        "A failed raw-to-boolean conversion preserves the unconverted draft.");
                    Select(0, "enabled");
                    inspector.Layout.BooleanMode.Invoke();
                    inspector.Layout.Reset.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    Require(workspace.Document!.Root!.Children[0].Arguments.All(argument => argument.Name != "enabled") &&
                        !inspector.IsBooleanMode, "Reset from boolean mode removes the named argument through the guarded edit workflow.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text.Contains("enabled: false", StringComparison.Ordinal), "Native undo restores a boolean reset.");
                    changed = editor.Text;
                    Select(0, "enabled");
                    inspector.Layout.BooleanMode.Invoke();
                    inspector.Layout.BooleanValue.Invoke();
                    editor.ReplaceRange(new(0, 0), editor.Text, "!");
                    string invalid = editor.Text;
                    workspace.ApplyProperty();
                    Require(editor.Text == invalid && !inspector.IsBooleanMode && !inspector.Layout.BooleanOpen && inspector.Value.ReadOnly,
                        "A source revision clears boolean mode and rejects stale drafts.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() => Require(editor.Text == changed && errors.Count == 0 && window.CallbackStatus == 0,
                    "Undo recovers valid source without runtime or callback failures."));
            }
            finally { window.Post(window.Close); }

            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The boolean test window rejected an action.");
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
