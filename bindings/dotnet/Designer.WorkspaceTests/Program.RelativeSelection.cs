using Xui;
using Xui.Designer;
using Xui.Generator;

internal static partial class Program
{
    private static void RunRelativeSelection(VisualStyle style)
    {
        const string fixture = """
            component Relatives { view { VStack() {
                Text("ROCKET");
                HStack() {
                    Button("First");
                    ScrollView("Nested") { VStack() { Text("Deep"); } }
                    Button("Last");
                }
                SplitView("Panes") { Text("Left"); Text("Right"); }
                VStack() { }
            } } }
            """;
        using var window = new Window("Designer relative selection", 1200, 1000, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument(fixture.Replace("ROCKET", "\U0001F680", StringComparison.Ordinal));
        var errors = new List<string>();
        using var workspace = new DesignerWorkspace(window, editor, errors.Add);
        var tree = workspace.Hierarchy;
        window.SetContent(window.Stack(Axis.Horizontal).Add(editor, 1).Add(tree.Layout.Root, 1)
            .Add(workspace.Inspector.Layout.Root, 1));
        int assertions = 0, selections = 0, changes = 0;
        bool observeChanges = true;
        editor.Event += e =>
        {
            if (e.Kind != EventKind.Change) return;
            changes++;
            if (observeChanges) workspace.SourceChanged();
        };
        workspace.SelectionChanged += () => selections++;
        var driver = Task.Run(Drive);
        window.Run();
        driver.GetAwaiter().GetResult();
        Console.WriteLine($"Designer relative selection assertions ({style}): {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60));
            string initial = "";
            try
            {
                await Ui(() =>
                {
                    Require(Enum.GetValues<DesignerSelectionTarget>().All(target => !workspace.CanSelectRelative(target)),
                        "No navigation is available before source publication.");
                    workspace.SourceChanged();
                });
                await Ready();
                await Ui(() =>
                {
                    initial = editor.Text;
                    var root = workspace.Document!.Root!;
                    Choose(root);
                    Refused(DesignerSelectionTarget.Parent);
                    Refused(DesignerSelectionTarget.Root);
                    Refused(DesignerSelectionTarget.PreviousSibling);
                    Refused(DesignerSelectionTarget.NextSibling);
                    Navigate(DesignerSelectionTarget.FirstChild, root.Children[0]);
                    Refused(DesignerSelectionTarget.FirstChild);
                    Refused(DesignerSelectionTarget.PreviousSibling);
                    Navigate(DesignerSelectionTarget.NextSibling, root.Children[1]);
                    Navigate(DesignerSelectionTarget.FirstChild, root.Children[1].Children[0]);
                    Navigate(DesignerSelectionTarget.NextSibling, root.Children[1].Children[1]);
                    Navigate(DesignerSelectionTarget.FirstChild, root.Children[1].Children[1].Children[0]);
                    Navigate(DesignerSelectionTarget.FirstChild, root.Children[1].Children[1].Children[0].Children[0]);
                    Navigate(DesignerSelectionTarget.Parent, root.Children[1].Children[1].Children[0]);
                    Navigate(DesignerSelectionTarget.Root, root);
                    tree.Tree.Expand(tree.Key(root), expanded: false);
                    Navigate(DesignerSelectionTarget.FirstChild, root.Children[0]);
                    Choose(root.Children[1].Children[2]);
                    Refused(DesignerSelectionTarget.NextSibling);
                    Navigate(DesignerSelectionTarget.PreviousSibling, root.Children[1].Children[1]);
                    Choose(root.Children[2].Children[0]);
                    Navigate(DesignerSelectionTarget.NextSibling, root.Children[2].Children[1]);
                    Refused(DesignerSelectionTarget.NextSibling);
                    Navigate(DesignerSelectionTarget.PreviousSibling, root.Children[2].Children[0]);
                    Choose(root.Children[3]);
                    Refused(DesignerSelectionTarget.FirstChild);
                    Refused(DesignerSelectionTarget.NextSibling);
                    Require(editor.Text == initial && changes == 0, "All relative navigation leaves native source unchanged.");
                    editor.ReplaceRange(new(0, 0), editor.Text, "// Prior typing\r");
                });
                await Ready();
                await Ui(() =>
                {
                    Choose(workspace.Document!.Root!.Children[1].Children[0]);
                    tree.Layout.Query.Text = "Button";
                    tree.RefreshSearch();
                    workspace.Inspector.Layout.ArgumentFilter.Text = "value";
                    Navigate(DesignerSelectionTarget.Parent, workspace.Document.Root.Children[1]);
                    Require(tree.Layout.Query.Text == "Button" && workspace.Inspector.Layout.ArgumentFilter.Text == "value",
                        "Navigation keeps search filters while switching the active inspector node.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "Selection navigation preserves the preceding native undo operation.");
                    Choose(workspace.Document!.Root!.Children[0]);
                    workspace.Wrap(ControlTemplate.VStack);
                    Require(workspace.IsBusy, "The real compiled wrapper starts a pending visual edit.");
                    Unavailable("Pending compilation");
                });
                await Ready();
                await Ui(() => editor.Command(TextCommand.Undo));
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "Navigation refusal does not add an undo entry during compilation.");
                    observeChanges = false;
                    editor.ReplaceRange(new(0, 0), editor.Text, "// Unpublished\r");
                    Require(workspace.IsCurrent, "The test retains the old model to exercise an exact-snapshot mismatch.");
                    Unavailable("An unmatched source snapshot");
                    observeChanges = true;
                    workspace.SourceChanged();
                });
                await Ready();
                await Ui(() => editor.Command(TextCommand.Undo));
                await Ready();
                await Ui(() =>
                {
                    editor.ReplaceRange(new(0, 0), editor.Text, "!");
                    Unavailable("Source invalidation");
                });
                await Until(() => tree.Layout.Status.Text.StartsWith("Invalid source", StringComparison.Ordinal));
                await Ui(() =>
                {
                    Unavailable("Invalid source");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Choose(workspace.Document!.Root!.Children[1].Children[0]);
                    Navigate(DesignerSelectionTarget.Root, workspace.Document.Root);
                    Require(editor.Text == initial && errors.Count == 0 && window.CallbackStatus == 0,
                        "Navigation recovers against new revision keys without native callback failures.");
                    workspace.Dispose();
                    Require(Enum.GetValues<DesignerSelectionTarget>().All(target => !workspace.CanSelectRelative(target)),
                        "Disposal disables every relative selection command.");
                });
            }
            finally { window.Post(window.Close); }

            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The relative selection window rejected an action.");
                await done.Task.WaitAsync(deadline.Token);
            }

            Task Ready() => Until(() => workspace.IsCurrent && !workspace.IsBusy);

            async Task Until(Func<bool> condition)
            {
                while (true)
                {
                    bool ready = false;
                    await Ui(() => ready = condition());
                    if (ready) return;
                    await Task.Delay(15, deadline.Token);
                }
            }
        }

        void Choose(XuiSourceNode node)
        {
            editor.Selection = new((ulong)node.Span.Start, (ulong)node.Span.Start);
            workspace.SelectFromCaret();
        }

        void Navigate(DesignerSelectionTarget target, XuiSourceNode expected)
        {
            Require(workspace.CanSelectRelative(target), $"{target} is available.");
            int before = selections;
            workspace.SelectRelative(target);
            Require(ReferenceEquals(tree.Selection, expected) && tree.Tree.Selection.Focused == tree.Key(expected) &&
                editor.Selection == new TextSelection((ulong)expected.Span.Start, (ulong)expected.Span.End) &&
                tree.Tree.Focused && selections == before + 1,
                $"{target} reveals the current node, exact UTF-16 source range, and emits one notification.");
        }

        void Refused(DesignerSelectionTarget target)
        {
            var selection = editor.Selection;
            int before = selections;
            Require(!workspace.CanSelectRelative(target), $"{target} is unavailable at this boundary.");
            workspace.SelectRelative(target);
            Require(editor.Selection == selection && selections == before &&
                workspace.Inspector.Layout.Feedback.Text.Contains("no control", StringComparison.Ordinal),
                "A boundary refusal reports the reason without wrapping or selecting another control.");
        }

        void Unavailable(string reason)
        {
            var selection = editor.Selection;
            int before = selections;
            foreach (var target in Enum.GetValues<DesignerSelectionTarget>())
            {
                Require(!workspace.CanSelectRelative(target), $"{reason} disables {target}.");
                workspace.SelectRelative(target);
            }
            Require(editor.Selection == selection && selections == before &&
                workspace.Inspector.Layout.Feedback.Text.Contains("current source", StringComparison.Ordinal),
                $"{reason} refuses direct calls without changing selection.");
        }

        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException($"{style}: {message}");
            assertions++;
        }
    }
}
