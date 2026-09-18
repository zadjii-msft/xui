using Xui;
using Xui.Designer;
using Xui.Generator;

internal static partial class Program
{
    private static void RunExpansion(VisualStyle style)
    {
        string fixture = "component Expansion { view { VStack(spacing: 8) {\r" +
            string.Join("\r", Enumerable.Range(0, 40).Select(i => $"HStack() {{ Button(\"Action {i}\"); }}")) +
            "\rScrollView(\"Nested\") { VStack() { HStack() { Text(\"Deep\"); } } }\r} } }";
        using var window = new Window("Designer hierarchy expansion", 1200, 900, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument(fixture);
        var errors = new List<string>();
        using var workspace = new DesignerWorkspace(window, editor, errors.Add);
        var hierarchy = workspace.Hierarchy;
        window.SetContent(window.Stack(Axis.Horizontal).Add(editor, 1).Add(hierarchy.Layout.Root, 1)
            .Add(workspace.Inspector.Layout.Root, 1));
        editor.Event += e => { if (e.Kind == EventKind.Change) workspace.SourceChanged(); };
        int selections = 0, assertions = 0;
        workspace.SelectionChanged += () => selections++;
        var driver = Task.Run(Drive);
        window.Run();
        driver.GetAwaiter().GetResult();
        Console.WriteLine($"Designer hierarchy expansion assertions ({style}): {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(90));
            string initial = "";
            TextSelection selection = default;
            int before = 0;
            try
            {
                await Ui(() =>
                {
                    Require(!workspace.CanExpandHierarchy && !workspace.CanCollapseHierarchy, "Expansion is unavailable before source publication.");
                    workspace.SourceChanged();
                });
                await Ready();
                await Ui(() =>
                {
                    initial = editor.Text;
                    Root();
                    workspace.Inspector.Value.Text = "23";
                    hierarchy.Layout.Query.Text = "Action";
                    hierarchy.RefreshSearch();
                    selection = editor.Selection;
                    before = selections;
                    workspace.ExpandHierarchy();
                    Require(workspace.IsExpandingHierarchy && !workspace.CanExpandHierarchy && workspace.CanCollapseHierarchy,
                        "Expansion schedules bounded work while collapse remains available.");
                });
                await Until(() => !workspace.IsExpandingHierarchy);
                await Ui(() =>
                {
                    Require(editor.Text == initial && editor.Selection == selection && selections == before &&
                        workspace.Inspector.Value.Text == "23" && hierarchy.Layout.Query.Text == "Action",
                        "Bulk expansion preserves exact source selection, property draft, search query, and selection notifications.");
                    foreach (var child in workspace.Document!.Root!.Children.Take(40))
                        Visible(child.Children[0], true);
                    var deep = workspace.Document.Root.Children[^1].Children[0].Children[0].Children[0];
                    Visible(deep, true);
                    Root();
                    selection = editor.Selection;
                    workspace.Inspector.Value.Text = "42";
                    before = selections;
                    workspace.CollapseHierarchy();
                    Require(editor.Selection == selection && selections == before && workspace.Inspector.Value.Text == "42" &&
                        hierarchy.Tree.Focused, "Collapsing the selected branch preserves drafts and focuses the hierarchy.");
                    Visible(workspace.Document.Root.Children[0], false);
                    hierarchy.Tree.Expand(hierarchy.Key(workspace.Document.Root));
                    Visible(deep, true);
                    Require(editor.Text == initial, "Reopening a collapsed branch retains nested expansion choices without source edits.");
                    editor.ReplaceRange(new(0, 0), editor.Text, "// New revision\r");
                });
                await Ready();
                await Ui(() =>
                {
                    Root();
                    workspace.ExpandHierarchy();
                    if (!window.Post(workspace.CancelHierarchyExpansion)) throw new InvalidOperationException("Cancel dispatch failed.");
                });
                await Until(() => !workspace.IsExpandingHierarchy);
                await Ui(() =>
                {
                    Require(workspace.Inspector.Layout.Feedback.Text.Contains("canceled", StringComparison.Ordinal),
                        "Explicit cancellation reports that completed branches stay open.");
                    Visible(workspace.Document!.Root!.Children[0].Children[0], true);
                    Visible(workspace.Document.Root.Children[39].Children[0], false);
                    Root();
                    workspace.ExpandHierarchy();
                    workspace.CollapseHierarchy();
                });
                await Ui(() =>
                {
                    Require(!workspace.IsExpandingHierarchy, "Collapse cancels queued expansion before it can reopen the branch.");
                    Visible(workspace.Document!.Root!.Children[0], false);
                    hierarchy.Tree.Expand(hierarchy.Key(workspace.Document.Root));
                    Root();
                    workspace.ExpandHierarchy();
                    var child = workspace.Document.Root.Children[1];
                    editor.Selection = new((ulong)child.Span.Start, (ulong)child.Span.Start);
                    workspace.SelectFromCaret();
                    Require(!workspace.IsExpandingHierarchy, "A new selection cancels queued work for the previous subtree.");
                });
                await Ui(() =>
                {
                    Visible(workspace.Document!.Root!.Children[39].Children[0], false);
                    Root();
                    workspace.ExpandHierarchy();
                    editor.ReplaceRange(new(0, 0), editor.Text, "// Cancel source\r");
                    Require(!workspace.IsExpandingHierarchy && !workspace.CanExpandHierarchy && !workspace.CanCollapseHierarchy,
                        "Source typing immediately invalidates expansion and its command availability.");
                });
                await Ready();
                await Ui(() =>
                {
                    Root();
                    Visible(workspace.Document!.Root!.Children[0].Children[0], false);
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() => editor.Command(TextCommand.Undo));
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "Expansion and cancellation add no source undo entries between actual edits.");
                    Root();
                    workspace.ExpandHierarchy();
                    workspace.Wrap(ControlTemplate.HStack);
                    Require(workspace.IsBusy && !workspace.IsExpandingHierarchy && !workspace.CanExpandHierarchy,
                        "A compiled visual edit cancels expansion and disables new expansion.");
                });
                await Ready();
                await Ui(() => editor.Command(TextCommand.Undo));
                await Ready();
                await Ui(() =>
                {
                    Root();
                    workspace.ExpandHierarchy();
                    editor.ReplaceRange(new(0, 0), editor.Text, "!");
                });
                await Until(() => hierarchy.Layout.Status.Text.StartsWith("Invalid source", StringComparison.Ordinal));
                await Ui(() =>
                {
                    Require(!workspace.CanExpandHierarchy && !workspace.CanCollapseHierarchy, "Invalid source cannot expand stale keys.");
                    workspace.ExpandHierarchy();
                    Require(!workspace.IsExpandingHierarchy && workspace.Inspector.Layout.Feedback.Text.Contains("current", StringComparison.Ordinal),
                        "A direct stale expansion reports its refusal.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    var leaf = workspace.Document!.Root!.Children[0].Children[0];
                    editor.Selection = new((ulong)leaf.Span.Start, (ulong)leaf.Span.Start);
                    workspace.SelectFromCaret();
                    Require(!workspace.CanExpandHierarchy && !workspace.CanCollapseHierarchy, "Leaf nodes have no expansion commands.");
                    Root();
                    workspace.ExpandHierarchy();
                    workspace.Dispose();
                    Require(!workspace.IsExpandingHierarchy, "Disposal cancels queued hierarchy work.");
                });
                await Ui(() => Require(editor.Text == initial && errors.Count == 0 && window.CallbackStatus == 0,
                    "Canceled callbacks cannot access retired workspace state or modify source."));
            }
            finally { window.Post(window.Close); }

            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The hierarchy expansion window rejected an action.");
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

        void Root()
        {
            var root = workspace.Document!.Root!;
            editor.Selection = new((ulong)root.Span.Start, (ulong)root.Span.Start);
            workspace.SelectFromCaret();
            workspace.Inspector.ChooseArgument("spacing");
        }

        void Visible(XuiSourceNode node, bool expected)
        {
            var previous = hierarchy.Tree.Selection.Focused;
            hierarchy.Tree.Select(hierarchy.Key(node));
            Require(expected ? hierarchy.Tree.Selection.Focused == hierarchy.Key(node) : hierarchy.Tree.Selection.Focused == previous,
                $"The native tree {(expected ? "can" : "cannot")} select the {(expected ? "expanded" : "hidden")} {node.Kind} row.");
        }

        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException($"{style}: {message}");
            assertions++;
        }
    }
}
