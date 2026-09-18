using Xui;
using Xui.Designer;
using Xui.Generator;

internal static partial class Program
{
    private static void RunSiblingInsertion(VisualStyle style)
    {
        const string original = """
            component SiblingInsertion { view { VStack() {
                Text("First", id: "first");
                Button("Middle", ref: Anchor);
                Text("Last");
                Grid("Cells", columns: [new(global::Xui.TrackSizing.Star, 1), new(global::Xui.TrackSizing.Star, 1)]) {
                    Text("Cell", column: 0);
                }
                ScrollView("Fixed") { Text("Only"); }
                HStack() { Text("Nested"); }
            } } }
            """;
        using var window = new Window("Designer sibling insertion", 1200, 1000, visualStyle: style);
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
        Console.WriteLine($"Designer sibling insertion assertions ({style}): {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(90));
            string initial = "", inserted = "";
            try
            {
                await Ui(workspace.SourceChanged);
                await Ready();
                await Ui(() =>
                {
                    initial = editor.Text;
                    Select(workspace.Document!.Root!.Children[1]);
                    inspector.PaletteLayout.PaletteFilter.Text = "label";
                    inspector.FilterPalette();
                    Require(inspector.Template == ControlTemplate.Text, "Sibling actions use the filtered palette selection.");
                    inspector.PaletteLayout.InsertBefore.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    inserted = editor.Text;
                    var children = workspace.Document!.Root!.Children;
                    Require(children.Count == 7 && children[1].Kind == "Text" && children[2].Kind == "Button" &&
                        children[1].Arguments[0].Value == "\"Text\"" && changes == 1,
                        "Insert before creates one compiled source edit at the exact sibling position.");
                    Require(ReferenceEquals(workspace.Hierarchy.Selection, children[1]) &&
                        editor.Selection == new TextSelection((ulong)children[1].Span.Start, (ulong)children[1].Span.End) && editor.Focused,
                        "The inserted sibling becomes the current hierarchy and source selection.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "One native Undo restores the source before sibling insertion.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == inserted, "Native Redo restores the complete insertion.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Select(workspace.Document!.Root!.Children[1]);
                    inspector.PaletteLayout.InsertAfter.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    var children = workspace.Document!.Root!.Children;
                    Require(children[1].Kind == "Button" && children[2].Arguments[0].Value == "\"Text\"" &&
                        children[3].Arguments[0].Value == "\"Last\"", "Insert after preserves the anchor and following sibling order.");
                    Require(editor.Text.Contains("id: \"first\"", StringComparison.Ordinal) &&
                        editor.Text.Contains("ref: Anchor", StringComparison.Ordinal), "Existing native identities remain unchanged.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Select(workspace.Document!.Root!.Children[5].Children[0]);
                    inspector.PaletteLayout.InsertAfter.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    Require(workspace.Document!.Root!.Children.Count == 6 &&
                        workspace.Document.Root.Children[5].Children.Count == 2, "A nested selection inserts into its immediate HStack parent.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Select(workspace.Document!.Root!.Children[3].Children[0]);
                    inspector.PaletteLayout.Row.Text = "0";
                    inspector.PaletteLayout.Column.Text = "1";
                    inspector.PaletteLayout.InsertBefore.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    var children = workspace.Document!.Root!.Children[3].Children;
                    Require(children.Count == 2 && children[0].Arguments.Single(argument => argument.Name == "column").Value == "1" &&
                        children[1].Arguments[0].Value == "\"Cell\"", "Grid siblings use explicit cell placement without changing the anchor.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Select(workspace.Document!.Root!.Children[3].Children[0]);
                    inspector.PaletteLayout.Column.Text = "0";
                    inspector.PaletteLayout.InsertAfter.Invoke();
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial && inspector.Layout.Feedback.Text.Contains("overlaps", StringComparison.Ordinal),
                        "An occupied Grid cell refuses insertion without a source change.");
                    inspector.PaletteLayout.Column.Text = "-1";
                    inspector.PaletteLayout.InsertBefore.Invoke();
                    Require(!workspace.IsBusy && editor.Text == initial &&
                        inspector.Layout.Feedback.Text.Contains("non-negative", StringComparison.Ordinal),
                        "Invalid Grid coordinates are rejected before compilation.");
                    Select(workspace.Document!.Root!);
                    Disabled(inspector.PaletteLayout.InsertBefore);
                    Disabled(inspector.PaletteLayout.InsertAfter);
                    workspace.InsertSibling(after: false);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial && inspector.Layout.Feedback.Text.Contains("root", StringComparison.Ordinal),
                        "A direct sibling command also refuses the component root.");
                    Select(workspace.Document!.Root!.Children[4].Children[0]);
                    Disabled(inspector.PaletteLayout.InsertBefore);
                    workspace.InsertSibling(after: true);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial && inspector.Layout.Feedback.Text.Contains("exactly", StringComparison.Ordinal),
                        "A fixed-child container refuses extra siblings through the model.");
                    Select(workspace.Document!.Root!.Children[1]);
                    inspector.PaletteLayout.PaletteFilter.Text = "no-such-template";
                    inspector.FilterPalette();
                    Disabled(inspector.PaletteLayout.InsertBefore);
                    Disabled(inspector.PaletteLayout.InsertAfter);
                    workspace.InsertSibling(after: true);
                    Require(!workspace.IsBusy && editor.Text == initial &&
                        inspector.Layout.Feedback.Text.Contains("Choose a control", StringComparison.Ordinal),
                        "Empty palette results cannot silently insert a default template.");
                    inspector.PaletteLayout.PaletteFilter.Text = "label";
                    inspector.FilterPalette();
                    inspector.PaletteLayout.InsertBefore.Invoke();
                    editor.ReplaceRange(new(0, 0), editor.Text, "// concurrent typing\r");
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == "// concurrent typing\r" + initial && workspace.Document!.Root!.Children.Count == 6,
                        "Source typing cancels an in-flight sibling insertion before it can commit.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() => Require(editor.Text == initial && errors.Count == 0 && window.CallbackStatus == 0,
                    "The preceding undo history survives cancellation without runtime failures."));
            }
            finally { window.Post(window.Close); }

            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The insertion test window rejected an action.");
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

        void Disabled(Button button)
        {
            bool rejected = false;
            try { button.Invoke(); }
            catch (XuiException error) when (error.Message.Contains("disabled", StringComparison.Ordinal)) { rejected = true; }
            Require(rejected, "Unavailable sibling actions are disabled native buttons.");
        }

        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException($"{style}: {message}");
            assertions++;
        }
    }
}
