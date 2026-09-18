using Xui;
using Xui.Designer;

internal static partial class Program
{
    [STAThread]
    private static int Main()
    {
        try
        {
            RunHierarchySearch(VisualStyle.Classic);
            RunHierarchySearch(VisualStyle.WinUI);
            RunSiblingInsertion(VisualStyle.Classic);
            RunSiblingInsertion(VisualStyle.WinUI);
            RunGridCellSearch(VisualStyle.Classic);
            RunGridCellSearch(VisualStyle.WinUI);
            RunPropertySearch(VisualStyle.Classic);
            RunPropertySearch(VisualStyle.WinUI);
            RunPropertySource(VisualStyle.Classic);
            RunPropertySource(VisualStyle.WinUI);
            RunStructureAvailability(VisualStyle.Classic);
            RunStructureAvailability(VisualStyle.WinUI);
            using var window = new Window("Designer workspace smoke", 1440, 960, visualStyle: VisualStyle.WinUI);
            var editor = window.MultilineText("XUI source").SetMaximumLength(65536);
            var diagnostics = window.MultilineText("Compiler diagnostics").SetReadOnly(true);
            using var workspace = new DesignerWorkspace(window, editor, error => diagnostics.Text = error);
            var templates = window.ComboBox("Document template", false);
            var view = new DesignerLayout(window, editor, diagnostics, workspace.Hierarchy.Layout.Root,
                workspace.Inspector.Layout.Root, window.Label("Workspace smoke does not execute authored previews."), templates, window);
            editor.Event += e => { if (e.Kind == EventKind.Change) workspace.SourceChanged(); };
            view.Undo.Click += () => { editor.Focus(); editor.Command(TextCommand.Undo); };
            view.Redo.Click += () => { editor.Focus(); editor.Command(TextCommand.Redo); };
            var smoke = Task.Run(() => DesignerBuilderSmoke.Run(window, editor, diagnostics, workspace, view));
            window.Run();
            smoke.GetAwaiter().GetResult();
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
