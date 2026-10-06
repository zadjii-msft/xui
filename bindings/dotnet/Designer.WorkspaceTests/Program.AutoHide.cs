using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunAutoHideNavigation(VisualStyle style)
    {
        using var window = new Window("Designer auto-hide commands", 1440, 960, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument("""component Sample { view { VStack() { Text("Child"); } } }""");
        var errors = new List<string>();
        using var workspace = new DesignerWorkspace(window, editor, errors.Add);
        var view = new DesignerLayout(window, editor, window.MultilineText("Diagnostics"), workspace.Hierarchy.Layout.Root,
            workspace.Inspector.Layout.Root, window.Label("Preview"), window.ComboBox("Templates", false), window);
        workspace.Hierarchy.RevealPane = view.RevealHierarchy;
        workspace.Inspector.RevealPane = view.RevealInspector;
        editor.Event += e => { if (e.Kind == EventKind.Change) workspace.SourceChanged(); };
        int assertions = 0;
        var driver = Task.Run(Drive);
        window.Run();
        driver.GetAwaiter().GetResult();
        Console.WriteLine($"Designer auto-hide command assertions ({style}): {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(30));
            string original = "";
            try
            {
                await Ui(workspace.SourceChanged);
                await Until(() => workspace.IsCurrent);
                await Ui(() =>
                {
                    original = editor.Text;
                    SelectChild();
                    view.HierarchyPin.Invoke();
                    view.InspectorPin.Invoke();
                });
                await Until(() => !view.HierarchyPinned && !view.InspectorPinned &&
                    !view.HierarchyDock.NavigationOpen && !view.InspectorDock.NavigationOpen);
                await Ui(() =>
                {
                    editor.Focus();
                    editor.ReplaceRange(new(0, 0), editor.Text, "// Formatting\r");
                });
                await Until(() => workspace.IsCurrent);
                await Ui(() =>
                {
                    SelectChild();
                    Require(!view.HierarchyDock.NavigationOpen && !view.InspectorDock.NavigationOpen,
                        "Parsing and source-caret selection do not expand hidden panes.");
                    workspace.Hierarchy.FocusSearch();
                    Require(view.HierarchyDock.NavigationOpen && workspace.Hierarchy.Layout.Query.Focused,
                        "The hierarchy search command reveals its pane before native focus.");
                    editor.Focus();
                    workspace.SelectRelative(DesignerSelectionTarget.Parent);
                    Require(view.HierarchyDock.NavigationOpen && workspace.Hierarchy.Tree.Focused &&
                        workspace.Hierarchy.Selection?.Kind == "VStack",
                        "Relative selection reveals the hidden hierarchy and focuses the selected tree.");
                    editor.Focus();
                    SelectChild();
                    workspace.Inspector.FocusSearch();
                    Require(view.InspectorDock.NavigationOpen && workspace.Inspector.Layout.ArgumentFilter.Focused,
                        "The property search command reveals its hidden pane.");
                    workspace.Inspector.Value.Text = "\"Unapplied draft\"";
                    editor.Focus();
                    workspace.Inspector.FocusValue();
                    Require(view.InspectorDock.NavigationOpen && workspace.Inspector.Value.Focused &&
                        workspace.Inspector.Value.Text == "\"Unapplied draft\"",
                        "The property focus command reveals the pane without resetting its draft.");
                    editor.Focus();
                    editor.Command(TextCommand.Undo);
                });
                await Until(() => workspace.IsCurrent);
                await Ui(() =>
                {
                    Require(editor.Text == original, "Auto-hide command navigation preserves the native source undo stack.");
                    Require(!view.HierarchyDock.NavigationOpen && !view.InspectorDock.NavigationOpen && errors.Count == 0,
                        "Background source updates keep both panes closed without focus errors.");
                });
            }
            finally { window.Post(window.Close); }

            void SelectChild()
            {
                var child = workspace.Document!.Root!.Children[0];
                editor.Selection = new((ulong)child.Span.Start, (ulong)child.Span.Start);
                workspace.SelectFromCaret();
            }
            void Require(bool condition, string message)
            {
                if (!condition) throw new InvalidOperationException(message);
                assertions++;
            }
            Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The auto-hide test window closed unexpectedly.");
                return done.Task.WaitAsync(deadline.Token);
            }
            async Task Until(Func<bool> predicate)
            {
                bool ready = false;
                while (!ready)
                {
                    await Ui(() => ready = predicate());
                    if (!ready) await Task.Delay(20, deadline.Token);
                }
            }
        }
    }
}
