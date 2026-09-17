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
            var searchLayout = new DesignerSourceSearchLayout(window, editor, attach: false);
            var diagnostics = window.MultilineText("Diagnostics").SetReadOnly(true);
            var diagnosticLayout = new DesignerDiagnosticsLayout(window, diagnostics, attach: false);
            var tree = window.TreeView("Hierarchy");
            var arguments = window.ComboBox("Arguments", false);
            var value = window.MultilineText("Literal value");
            var palette = window.ComboBox("Palette", false);
            var templates = window.ComboBox("Templates", false);
            var hierarchy = new DesignerHierarchyLayout(window, tree, attach: false);
            var inspector = new DesignerInspectorLayout(window, arguments, value, palette, attach: false);
            var preview = window.Label("Layout fixture preview");
            var layout = new DesignerLayout(window, searchLayout.Root, diagnosticLayout.Root, hierarchy.Root, inspector.Root, preview, templates, window);
            editor.Text = "Native editor layout fixture";
            int assertions = 0;
            float collapsedHeight = 0;
            float collapsedBottom = 0;
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
                        Require(!searchLayout.FindOpen && !layout.OutputExpanded, "Find and output start collapsed");
                        Require(tree.GetBounds().Width >= 100 && tree.GetBounds().Height >= 150, "Native tree bounds");
                        Require(value.GetBounds().Width >= 100 && value.GetBounds().Height >= 60, "Native inspector bounds");
                        Require(tree.GetBounds().X < editor.GetBounds().X &&
                            editor.GetBounds().X < preview.GetBounds().X &&
                            preview.GetBounds().X < value.GetBounds().X, "Hierarchy, source, preview and inspector order");
                        Require(layout.Save.GetBounds().Width == 36 && layout.Undo.GetBounds().Width == 36 &&
                            layout.Save.Icon == ButtonIcon.Save && layout.OpenPicker.Icon == ButtonIcon.Folder &&
                            layout.Undo.Icon == ButtonIcon.Undo && layout.Redo.Icon == ButtonIcon.Redo,
                            "Common toolbar actions use compact native icon buttons");
                        Require(layout.Save.Text == "Save" && layout.Undo.Text == "Undo",
                            "Icon buttons retain descriptive accessible names");
                        Require(layout.Commands.Text == "Commands" && layout.Commands.GetBounds().Width >= 80 &&
                            layout.Commands.GetBounds().X + layout.Commands.GetBounds().Width <=
                            layout.ToolbarHost.GetBounds().X + layout.ToolbarHost.GetBounds().Width,
                            "Command discovery has a visible, named toolbar action");
                        Require(layout.GoToLine.Text == "Go to line" && layout.GoToLine.GetBounds().Width >= 80 &&
                            layout.GoToLine.GetBounds().Y + layout.GoToLine.GetBounds().Height <= editor.GetBounds().Y,
                            "Source location navigation has a visible, named action above the editor");
                        foreach (var panel in new[] { layout.HierarchyPanel, layout.InspectorPanel, layout.OutputPanel })
                        {
                            var style = panel.GetControlStyleValues(StylePart.Root, effective: true);
                            Require(style.Background == new ThemeColor(0xF3F3F3, 0x272727) &&
                                style.BorderBrush == new ThemeColor(0xCCCCCC, 0x454545) && style.BorderThickness == new Insets(1),
                                "Workspace panels have theme-aware backgrounds and borders");
                        }
                        Require(layout.ToolbarHost.GetBounds().Y + layout.ToolbarHost.GetBounds().Height <= layout.Workspace.GetBounds().Y,
                            "The grouped toolbar occupies a separate band above the workspace");
                        Require(layout.OutputToggle.Text == "Expand output" && layout.OutputToggle.Icon == ButtonIcon.ChevronUp &&
                            layout.OutputToggle.GetBounds().Width == 32,
                            "The bottom status row exposes an accessible up-chevron button");
                        var disclosure = layout.OutputToggle.EffectiveStyleValues;
                        Require(disclosure.Background == layout.OutputPanel.GetControlStyleValues(StylePart.Root, effective: true).Background &&
                            disclosure.BorderThickness == new Insets(0),
                            "The output disclosure has no contrasting button background or border");
                        Require(layout.OutputToggle.GetBounds().X < layout.Status.GetBounds().X &&
                            layout.OutputToggle.GetBounds().X >= layout.OutputPanel.GetBounds().X &&
                            layout.OutputToggle.GetBounds().X <= layout.OutputPanel.GetBounds().X + 10,
                            "The output toggle is left-aligned in the panel header");
                        diagnostics.Text = "Diagnostics published while output is collapsed.";
                        collapsedHeight = editor.GetBounds().Height;
                        collapsedBottom = layout.OutputToggle.GetBounds().Y;
                        editor.Focus();
                        editor.Selection = new(7, 13);
                        Require(editor.Selection == new TextSelection(7, 13), "Native source selection");
                        editor.ReplaceRange(new(0, 0), editor.Text, "Test ");
                        editor.Selection = new(7, 13);
                        layout.OutputToggle.Invoke();
                        searchLayout.FindOpen = true;
                        window.SetTheme(Theme.Light);
                    });
                    await Task.Delay(80);
                    await Ui(() =>
                    {
                        Require(layout.OutputExpanded && layout.OutputToggle.Text == "Collapse output" &&
                            layout.OutputToggle.Icon == ButtonIcon.ChevronDown,
                            "The output button changes to an accessible down-chevron");
                        Require(diagnostics.GetBounds().Height >= 60, "Expanded diagnostics have usable bounds");
                        Require(diagnostics.Text == "Diagnostics published while output is collapsed.",
                            "Output reveals diagnostics published while collapsed");
                        Require(layout.Output.GetBounds().Height == 170,
                            $"Output has a bounded expanded height (actual {layout.Output.GetBounds().Height}, panel {layout.OutputPanel.GetBounds()}, header {layout.OutputHeader.GetBounds()})");
                        Require(diagnostics.GetBounds().Y + diagnostics.GetBounds().Height <=
                            layout.Output.GetBounds().Y + layout.Output.GetBounds().Height,
                            "Diagnostics stay inside the output pane below its header");
                        Require(searchLayout.Query.GetBounds().Width >= 80 && searchLayout.Close.GetBounds().Width == 32,
                            "The self-contained Find panel fits the source pane");
                        Require(editor.GetBounds().Height <= collapsedHeight - 240,
                            "Expanded Find and output take space from the flexible workspace");
                        Require(layout.OutputToggle.GetBounds().Y == collapsedBottom - 170 &&
                            layout.OutputHeader.GetBounds().Y + layout.OutputHeader.GetBounds().Height <= layout.Output.GetBounds().Y,
                            "The output toggle moves upward into the header above expanded contents");
                        Require(editor.Text == "Test Native editor layout fixture" && editor.Selection == new TextSelection(7, 13),
                            "Theme and panel changes preserve native source and selection");
                        diagnostics.Focus();
                        diagnostics.Selection = new(0, 11);
                        layout.OutputToggle.Invoke();
                        searchLayout.FindOpen = false;
                    });
                    await Task.Delay(80);
                    await Ui(() =>
                    {
                        Require(!layout.OutputExpanded && layout.OutputToggle.Focused,
                            "Collapsing output moves focus out of the hidden diagnostics");
                        Require(editor.GetBounds().Height == collapsedHeight, "Collapsing both panels restores all source space");
                        Require(layout.OutputToggle.GetBounds().Y == collapsedBottom, "The collapsed status row stays at the bottom");
                        Require(diagnostics.Text == "Diagnostics published while output is collapsed." &&
                            diagnostics.Selection == new TextSelection(0, 11),
                            "Collapsing output preserves diagnostic text and selection");
                        Require(window.Style == VisualStyle.WinUI && layout.StyleToggle.Text == "Style: WinUI",
                            "The style toolbar button identifies the initial WinUI style");
                        layout.StyleToggle.Invoke();
                    });
                    await Task.Delay(80);
                    await Ui(() =>
                    {
                        Require(window.Style == VisualStyle.Classic && layout.StyleToggle.Text == "Style: Classic",
                            "The toolbar button switches the live window to Classic");
                        Require(layout.OutputToggle.Icon == ButtonIcon.ChevronUp &&
                            layout.OutputToggle.EffectiveStyleValues.BorderThickness == new Insets(0),
                            "Classic retains the borderless output chevron");
                        Require(editor.Text == "Test Native editor layout fixture" && editor.Selection == new TextSelection(7, 13),
                            "Classic style preserves native source text and selection");
                        Require(!layout.OutputExpanded && !searchLayout.FindOpen && editor.GetBounds().Height >= 150,
                            "Classic style preserves collapsed panels and usable source space");
                        layout.StyleToggle.Invoke();
                    });
                    await Task.Delay(80);
                    await Ui(() =>
                    {
                        Require(window.Style == VisualStyle.WinUI && layout.StyleToggle.Text == "Style: WinUI",
                            "The toolbar button switches back to WinUI");
                        Require(editor.GetBounds().Height == collapsedHeight && layout.OutputToggle.GetBounds().Y == collapsedBottom,
                            "Switching back to WinUI restores the original layout");
                        editor.Command(TextCommand.Undo);
                        Require(editor.Text == "Native editor layout fixture", "Panel and style changes preserve native editor undo");
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
