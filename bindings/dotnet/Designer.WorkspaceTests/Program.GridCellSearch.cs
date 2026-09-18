using Xui;
using Xui.Designer;
using Xui.Generator;

internal static partial class Program
{
    private static void RunGridCellSearch(VisualStyle style)
    {
        const string original = """
            component GridSearch { param global::Xui.GridTrack[] Tracks; view { VStack() {
                Grid("Cells", columns: [new(), new()]) { Text("First", column: 0); }
                Grid("Full") { Text("Only"); }
                Grid("Unknown", rows: Tracks) { }
                Text("Not Grid");
                Grid("Outer", columns: [new(), new()]) {
                    Grid("Inner", columns: [new(), new(), new()]) { Text("A"); Text("B", column: 1); }
                }
            } } }
            """;
        using var window = new Window("Designer empty Grid cells", 1200, 1000, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument(original);
        var errors = new List<string>();
        using var workspace = new DesignerWorkspace(window, editor, errors.Add);
        var inspector = workspace.Inspector;
        window.SetContent(window.Stack(Axis.Horizontal).Padding(10).Spacing(10)
            .Add(editor, 1).Add(workspace.Hierarchy.Layout.Root, 1).Add(inspector.Layout.Root, 1));
        int changes = 0, assertions = 0;
        editor.Event += value =>
        {
            if (value.Kind != EventKind.Change) return;
            changes++;
            workspace.SourceChanged();
        };
        var driver = Task.Run(Drive);
        window.Run();
        driver.GetAwaiter().GetResult();
        Console.WriteLine($"Designer empty Grid cell assertions ({style}): {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60));
            string initial = "";
            try
            {
                await Ui(workspace.SourceChanged);
                await Ready();
                await Ui(() => inspector.ShowPalette(workspace.Hierarchy.Layout.FromCaret));
                await Ui(() => { });
                await Ui(() =>
                {
                    initial = editor.Text;
                    Select(workspace.Document!.Root!.Children[0]);
                    inspector.PaletteLayout.Row.Text = "bad";
                    inspector.PaletteLayout.Column.Text = "99";
                    var selection = editor.Selection;
                    inspector.PaletteLayout.FindCell.Invoke();
                    Require(inspector.PaletteLayout.Row.Text == "0" && inspector.PaletteLayout.Column.Text == "1" &&
                        inspector.Layout.Feedback.Text.Contains("selected Grid", StringComparison.Ordinal),
                        "The native action fills the first free cell in the selected Grid.");
                    Require(editor.Text == initial && editor.Selection == selection && changes == 0 && !workspace.IsBusy,
                        "Choosing a cell changes only draft coordinates, not source, selection, or undo.");
                    inspector.PaletteLayout.FindCell.Focus();
                });
                await Ui(() =>
                {
                    var button = inspector.PaletteLayout.FindCell.GetBounds();
                    var panel = inspector.PaletteLayout.Root.GetBounds();
                    Require(button.Width >= 100 && button.Y >= panel.Y && button.Y + button.Height <= panel.Y + panel.Height,
                        "The native helper button is visible inside the control palette flyout.");
                    Select(workspace.Document!.Root!.Children[0].Children[0]);
                    inspector.PaletteLayout.FindCell.Invoke();
                    Require(inspector.PaletteLayout.Column.Text == "1" && inspector.Layout.Feedback.Text.Contains("parent Grid", StringComparison.Ordinal),
                        "A direct child finds space in its parent Grid.");
                    Select(workspace.Document.Root.Children[4].Children[0]);
                    inspector.PaletteLayout.FindCell.Invoke();
                    Require(inspector.PaletteLayout.Column.Text == "2" && inspector.Layout.Feedback.Text.Contains("selected Grid", StringComparison.Ordinal),
                        "A selected nested Grid takes precedence over its own parent.");
                    Select(workspace.Document.Root.Children[4]);
                    inspector.PaletteLayout.FindCell.Invoke();
                    Require(inspector.PaletteLayout.Column.Text == "1", "Selecting the outer Grid searches its own cells instead.");
                    inspector.PaletteLayout.Row.Text = "7";
                    inspector.PaletteLayout.Column.Text = "8";
                    Select(workspace.Document.Root.Children[1]);
                    inspector.PaletteLayout.FindCell.Invoke();
                    Refused("no empty");
                    Select(workspace.Document.Root.Children[2]);
                    inspector.PaletteLayout.FindCell.Invoke();
                    Refused("unknown lengths");
                    Select(workspace.Document.Root.Children[3]);
                    workspace.FindEmptyGridCell();
                    Refused("Select a Grid");
                    Select(workspace.Document.Root.Children[0]);
                    inspector.PaletteLayout.FindCell.Invoke();
                    inspector.PaletteLayout.PaletteFilter.Text = "label";
                    inspector.FilterPalette();
                    inspector.PaletteLayout.Insert.Invoke();
                    workspace.FindEmptyGridCell();
                    Require(workspace.IsBusy && inspector.Layout.Feedback.Text.Contains("Wait", StringComparison.Ordinal),
                        "The helper refuses field changes during an active visual transaction.");
                });
                await Ready();
                await Ui(() =>
                {
                    var children = workspace.Document!.Root!.Children[0].Children;
                    Require(children.Count == 2 && children[1].Arguments.Single(argument => argument.Name == "column").Value == "1" &&
                        changes == 1, "The suggested coordinates feed a real compiled insertion as one native edit.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "A cell lookup does not add undo entries before the insertion.");
                    Select(workspace.Document!.Root!.Children[0]);
                    inspector.PaletteLayout.Row.Text = "7";
                    inspector.PaletteLayout.Column.Text = "8";
                    editor.ReplaceRange(new(0, 0), editor.Text, "!");
                    workspace.FindEmptyGridCell();
                    Refused("stale");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() => Require(editor.Text == initial && errors.Count == 0 && window.CallbackStatus == 0,
                    "Stale refusal and source undo preserve the document without native failures."));
            }
            finally { window.Post(window.Close); }

            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The Grid cell test window rejected an action.");
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

        void Select(XuiSourceNode node)
        {
            editor.Selection = new((ulong)node.Span.Start, (ulong)node.Span.Start);
            workspace.SelectFromCaret();
        }

        void Refused(string reason) => Require(inspector.PaletteLayout.Row.Text == "7" && inspector.PaletteLayout.Column.Text == "8" &&
            inspector.Layout.Feedback.Text.Contains(reason, StringComparison.OrdinalIgnoreCase),
            "A " + reason + " refusal preserves the existing draft coordinates.");

        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException($"{style}: {message}");
            assertions++;
        }
    }
}
