using Xui;
using Xui.Designer;
using Xui.Generator;

internal static class Program
{
    private static int assertions;
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    [STAThread]
    private static int Main()
    {
        try
        {
            Run();
            Console.WriteLine($"Designer grouping UI assertions: {assertions} passed.");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }

    private static void Run()
    {
        const string original = """component Grouping { view { VStack() { Text("Hello"); Text("Second"); } } }""";
        const string protectedSource = """component Protected { view { VStack(padding: 8) { Text("Keep"); } } }""";
        using var window = new Window("Designer grouping UI smoke", 1200, 1000);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument(original);
        var errors = new List<string>();
        using var workspace = new DesignerWorkspace(window, editor, errors.Add);
        editor.Event += value => { if (value.Kind == EventKind.Change) workspace.SourceChanged(); };
        window.SetContent(window.Stack(Axis.Horizontal).Padding(10).Spacing(10)
            .Add(editor, 1).Add(workspace.Hierarchy.Layout.Root, 1).Add(workspace.Inspector.Layout.Root, 1));
        Exception? failure = null;
        var driver = Task.Run(async () =>
        {
            try
            {
                await OnUi(workspace.SourceChanged);
                await Ready();
                await OnUi(() =>
                {
                    Select(workspace.Document!.Root!.Children[0]);
                    workspace.Inspector.Layout.WrapVertical.Invoke();
                });
                await Ready();
                string wrapped = "";
                await OnUi(() =>
                {
                    wrapped = editor.Text;
                    Require(workspace.Document!.Root!.Children[0].Kind == "VStack", "The native Wrap button groups the selected child.");
                    Require(workspace.Document.Root.Children[0].Children.Single().Kind == "Text", "Wrapping preserves the selected subtree.");
                    Require(wrapped.Contains('\r') && !wrapped.Contains('\n'), "One-line source gains only native CR separators.");
                    Require(editor.Text.Substring((int)editor.Selection.Start, (int)(editor.Selection.End - editor.Selection.Start)).StartsWith("VStack", StringComparison.Ordinal),
                        "The native selection covers the resulting wrapper.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await OnUi(() =>
                {
                    Require(editor.Text == original, "One native Undo restores the complete original source.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                await OnUi(() =>
                {
                    Require(editor.Text == wrapped, "Native Redo restores the exact grouping edit.");
                    Select(workspace.Document!.Root!.Children[0]);
                    workspace.Inspector.Layout.Unwrap.Invoke();
                });
                await Ready();
                await OnUi(() =>
                {
                    Require(workspace.Document!.Root!.Children[0].Kind == "Text", "The native Unwrap button restores the child at its original level.");
                    Select(workspace.Document.Root);
                    workspace.Inspector.Layout.WrapScroll.Invoke();
                });
                await Ready();
                await OnUi(() =>
                {
                    Require(workspace.Document!.Root!.Kind == "ScrollView" && workspace.Document.Root.Children.Single().Kind == "VStack",
                        "The view root can be wrapped in a native scroll container.");
                    workspace.Inspector.Layout.Unwrap.Invoke();
                });
                await Ready();
                await OnUi(() =>
                {
                    Require(workspace.Document!.Root!.Kind == "VStack", "Unwrap retains the required single view root.");
                    Select(workspace.Document.Root.Children[0]);
                    workspace.Hierarchy.Tree.Focus();
                    Require(workspace.HandleHierarchyKey(new('G', KeyModifiers.Control, 0)), "Ctrl+G is handled when the native hierarchy has focus.");
                });
                await Ready();
                await OnUi(() =>
                {
                    Require(workspace.Document!.Root!.Children[0].Kind == "VStack", "The grouping shortcut uses the same validated native edit.");
                    workspace.Hierarchy.Tree.Focus();
                    Require(workspace.HandleHierarchyKey(new('G', KeyModifiers.Control | KeyModifiers.Shift, 0)), "Ctrl+Shift+G unwraps the selected hierarchy node.");
                });
                await Ready();
                await OnUi(() =>
                {
                    Require(workspace.Document!.Root!.Children[0].Kind == "Text", "The ungroup shortcut restores the child.");
                    workspace.Inspector.Layout.WrapHorizontal.Invoke();
                });
                await Ready();
                await OnUi(() =>
                {
                    Require(workspace.Document!.Root!.Children[0].Kind == "HStack", "The horizontal grouping button uses the same source transaction.");
                    Require(!workspace.HandleHierarchyKey(new('G', KeyModifiers.Control, 0)), "Grouping shortcuts do not run when the source editor has focus.");
                    editor.Text = protectedSource;
                    workspace.SourceChanged();
                });
                await Ready();
                await OnUi(() => workspace.Inspector.Layout.Unwrap.Invoke());
                await Ready();
                await OnUi(() =>
                {
                    Require(editor.Text == protectedSource, "Unwrap does not discard authored wrapper properties.");
                    Require(workspace.Inspector.Layout.Feedback.Text.Contains("padding", StringComparison.Ordinal), "The refusal identifies the authored property.");
                    editor.Text = "invalid source";
                    workspace.SourceChanged();
                    workspace.Wrap(ControlTemplate.HStack);
                    Require(editor.Text == "invalid source", "A stale hierarchy cannot replace newly typed source.");
                    Require(errors.Count == 0, "Expected edit refusals do not produce runtime failures.");
                });
            }
            catch (Exception error) { failure = error; }
            finally { window.Post(window.Close); }
        });
        window.Run();
        driver.GetAwaiter().GetResult();
        if (failure is not null) throw new InvalidOperationException("Grouping UI smoke failed.", failure);

        void Select(XuiSourceNode node)
        {
            editor.Selection = new((ulong)node.Span.Start, (ulong)node.Span.Start);
            workspace.SelectFromCaret();
        }

        async Task OnUi(Action action)
        {
            var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!window.Post(() =>
            {
                try { action(); done.SetResult(); }
                catch (Exception error) { done.SetException(error); }
            })) throw new InvalidOperationException("The grouping test window rejected a posted action.");
            await done.Task.WaitAsync(TimeSpan.FromSeconds(15));
        }

        async Task Ready()
        {
            var ready = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            await OnUi(() =>
            {
                workspace.Changed += Check;
                Check();
                void Check()
                {
                    if (!workspace.IsCurrent || workspace.IsBusy) return;
                    workspace.Changed -= Check;
                    ready.TrySetResult();
                }
            });
            await ready.Task.WaitAsync(TimeSpan.FromSeconds(15));
        }
    }
}
