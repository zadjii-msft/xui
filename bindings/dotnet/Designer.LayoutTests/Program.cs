using System.Diagnostics;
using Xui;
using Xui.Designer;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        try
        {
            using var window = new Window("Designer layout smoke", 1440, 960, visualStyle: VisualStyle.WinUI);
            var editor = window.MultilineText("Source");
            var diagnostics = window.MultilineText("Diagnostics");
            var diagnosticLayout = new DesignerDiagnosticsLayout(window, diagnostics, attach: false);
            var tree = window.TreeView("Hierarchy");
            var arguments = window.ComboBox("Arguments", false);
            var value = window.MultilineText("Literal value");
            var palette = window.ComboBox("Palette", false);
            var templates = window.ComboBox("Templates", false);
            var hierarchy = new DesignerHierarchyLayout(window, tree, attach: false);
            var inspector = new DesignerInspectorLayout(window, arguments, value, palette, attach: false);
            var preview = window.Label("Layout fixture preview");
            var layout = new DesignerLayout(window, editor, diagnosticLayout.Root, hierarchy.Root, inspector.Root, preview, templates);
            editor.Text = "Native editor layout fixture";
            int assertions = 0;
            Exception? failure = null;
            var test = Task.Run(async () =>
            {
                try
                {
                    var elapsed = Stopwatch.StartNew();
                    bool ready = false;
                    while (!ready)
                    {
                        await Ui(() => ready = editor.GetBounds().Height >= 150);
                        if (elapsed.Elapsed > TimeSpan.FromSeconds(15)) throw new TimeoutException("Designer layout did not become usable.");
                        await Task.Delay(30);
                    }
                    await Ui(() =>
                    {
                        Require(editor.GetBounds().Width >= 150, "Source editor width");
                        Require(tree.GetBounds().Width >= 100 && tree.GetBounds().Height >= 150, "Native tree bounds");
                        Require(value.GetBounds().Width >= 100 && value.GetBounds().Height >= 60, "Native inspector bounds");
                        Require(diagnostics.GetBounds().Height >= 60, "Diagnostics bounds");
                        Require(diagnosticLayout.Root.GetBounds().Height == 140, "Diagnostics do not consume the flexible workspace");
                        Require(tree.GetBounds().X < editor.GetBounds().X &&
                            editor.GetBounds().X < preview.GetBounds().X &&
                            preview.GetBounds().X < value.GetBounds().X, "Hierarchy, source, preview and inspector order");
                        Require(layout.Save.GetBounds().Width >= 40 && layout.Undo.GetBounds().Width >= 40, "File and undo toolbar bounds");
                        editor.Focus();
                        editor.Selection = new(7, 13);
                        Require(editor.Selection == new TextSelection(7, 13), "Native source selection");
                        window.SetTheme(Theme.Light);
                    });
                    await Task.Delay(80);
                    await Ui(() =>
                    {
                        Require(editor.Text == "Native editor layout fixture" && editor.Selection == new TextSelection(7, 13),
                            "Theme changes preserve native source and selection");
                    });
                }
                catch (Exception error) { failure = error; }
                finally { window.Post(window.Close); }

                Task Ui(Action action)
                {
                    var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                    if (!window.Post(() =>
                    {
                        try { action(); done.SetResult(); }
                        catch (Exception error) { done.SetException(error); }
                    })) throw new InvalidOperationException("Designer layout window closed during smoke.");
                    return done.Task.WaitAsync(TimeSpan.FromSeconds(15));
                }
                void Require(bool condition, string message)
                {
                    if (!condition) throw new InvalidOperationException("Designer layout smoke: " + message);
                    assertions++;
                }
            });
            window.Run();
            test.GetAwaiter().GetResult();
            if (failure is not null) throw failure;
            Console.WriteLine($"Designer native layout assertions: {assertions} passed.");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
