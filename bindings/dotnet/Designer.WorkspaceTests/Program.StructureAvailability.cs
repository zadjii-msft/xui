using Xui;
using Xui.Designer;
using Xui.Generator;

internal static partial class Program
{
    private static void RunStructureAvailability(VisualStyle style)
    {
        const string original = """
            component StructureChoices { view { VStack() {
                Text("First");
                HStack() { Button("Only child"); }
                SplitView("Panes") { Text("Left"); Text("Right"); }
                ScrollView("Scroll") { VStack() { Text("Nested"); } }
                Grid("Cells", columns: [new(), new()]) { Button("Cell"); }
                VStack() { }
                Text("Last");
            } } }
            """;
        using var window = new Window("Designer structural availability", 1200, 1000, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument(original);
        var errors = new List<string>();
        using var workspace = new DesignerWorkspace(window, editor, errors.Add);
        var inspector = workspace.Inspector;
        int assertions = 0;
        editor.Event += value => { if (value.Kind == EventKind.Change) workspace.SourceChanged(); };
        window.SetContent(window.Stack(Axis.Horizontal).Add(editor, 1)
            .Add(workspace.Hierarchy.Layout.Root, 1).Add(inspector.Layout.Root, 1));
        var driver = Task.Run(Drive);
        window.Run();
        driver.GetAwaiter().GetResult();
        Console.WriteLine($"Designer structure availability assertions ({style}): {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60));
            string initial = "";
            try
            {
                await Ui(() =>
                {
                    Require(!workspace.CanEditSelection && !inspector.CanWrap, "Commands start unavailable without a current selection.");
                    workspace.SourceChanged();
                });
                await Ready();
                await Ui(() =>
                {
                    initial = editor.Text;
                    var root = workspace.Document!.Root!;
                    Check(root, false, false, false, false);
                    Disabled(inspector.Layout.Delete);
                    Disabled(inspector.Layout.Duplicate);
                    Check(root.Children[0], true, false, true, false);
                    Disabled(inspector.Layout.Up);
                    Check(root.Children[1], true, true, true, true);
                    Check(root.Children[1].Children[0], true, false, false, false);
                    Check(root.Children[2], true, true, true, false);
                    Check(root.Children[2].Children[0], false, false, true, false);
                    Disabled(inspector.Layout.Delete);
                    Check(root.Children[2].Children[1], false, true, false, false);
                    Check(root.Children[3].Children[0], false, false, false, true);
                    Check(root.Children[4].Children[0], true, false, false, false);
                    Check(root.Children[5], true, true, true, false);
                    Check(root.Children[6], true, true, false, false);
                    Disabled(inspector.Layout.Down);
                    Check(root.Children[0], true, false, true, false);
                    workspace.Move(1);
                    Require(workspace.IsBusy && !workspace.CanEditSelection && !inspector.CanDeleteOrDuplicate &&
                        !inspector.CanMoveUp && !inspector.CanMoveDown && !inspector.CanWrap && !inspector.CanUnwrap,
                        "Compilation disables every shared structural capability.");
                });
                await Ready();
                await Ui(() =>
                {
                    Require(workspace.CanEditSelection && workspace.Document!.Root!.Children[1].Kind == "Text",
                        "Capabilities recover after the same compiled move used by commands.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "The structural move retains one native source undo operation.");
                    Check(workspace.Document!.Root!.Children[4].Children[0], true, false, false, false);
                    inspector.Layout.Row.Text = "0";
                    inspector.Layout.Column.Text = "1";
                    workspace.Duplicate();
                });
                await Ready();
                await Ui(() =>
                {
                    Require(workspace.Document!.Root!.Children[4].Children.Count == 2 &&
                        workspace.Hierarchy.Selection!.Arguments.Any(argument => argument.Name == "column" && argument.Value == "1"),
                        "Command-accessible duplication uses the existing Grid coordinate fields and compiler checks.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "Grid duplication preserves exact source undo.");
                    editor.ReplaceRange(new(0, 0), editor.Text, "!");
                    Require(!workspace.CanEditSelection && !inspector.CanDeleteOrDuplicate && !inspector.CanMoveUp &&
                        !inspector.CanMoveDown && !inspector.CanWrap && !inspector.CanUnwrap,
                        "Source typing immediately invalidates all shared structural capabilities.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() => Require(editor.Text == initial && workspace.CanEditSelection && errors.Count == 0 && window.CallbackStatus == 0,
                    "Undo restores command availability without native callback failures."));
            }
            finally { window.Post(window.Close); }

            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The structure test window rejected an action.");
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

        void Check(XuiSourceNode node, bool remove, bool up, bool down, bool unwrap)
        {
            editor.Selection = new((ulong)node.Span.Start, (ulong)node.Span.Start);
            workspace.SelectFromCaret();
            Require(workspace.CanEditSelection && inspector.CanDeleteOrDuplicate == remove &&
                inspector.CanMoveUp == up && inspector.CanMoveDown == down && inspector.CanWrap && inspector.CanUnwrap == unwrap,
                $"Shared structural capabilities match {node.Kind} at {node.Span.Start}.");
        }

        void Disabled(Button button)
        {
            bool refused = false;
            try { button.Invoke(); }
            catch (XuiException error) when (error.Message.Contains("disabled", StringComparison.Ordinal)) { refused = true; }
            Require(refused, "The matching native inspector button also rejects the unavailable action.");
        }

        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException($"{style}: {message}");
            assertions++;
        }
    }
}
