using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunPropertySearch(VisualStyle style)
    {
        const string original = """
            component PropertySearch { state bool Active = true; view { VStack() {
                Text("Label", fontSize: 16, padding: 4, help: "Keep help");
                Button("Button", size: (120, 40), enabled: true, padding: 8);
                Toggle("Check", checked: Active);
            } } }
            """;
        using var window = new Window("Designer property search", 1200, 1000, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument(original);
        var errors = new List<string>();
        using var workspace = new DesignerWorkspace(window, editor, errors.Add);
        var inspector = workspace.Inspector;
        var arguments = (ComboBox)inspector.Layout.Arguments;
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
        Console.WriteLine($"Designer property search assertions ({style}): {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60));
            string initial = "", changed = "";
            int before = 0;
            try
            {
                await Ui(workspace.SourceChanged);
                await Ready();
                await Ui(() =>
                {
                    initial = editor.Text;
                    Select(0, "value");
                    var label = workspace.Document!.Root!.Children[0];
                    Require(DesignerInspector.FindArguments(label, " FoNt \t SI ", false).SequenceEqual(new[] { "fontSize" }),
                        "Property queries use ordinal case-insensitive matching for every term.");
                    Require(DesignerInspector.FindArguments(label, "", true).SequenceEqual(new[] { "fontSize", "help", "padding", "value" }),
                        "Authored-only results include exactly the authored argument names in sorted order.");
                    Require(DesignerInspector.FindArguments(null, "", false).Length == 0 &&
                        DesignerInspector.FindArguments(label, "font padding", false).Length == 0,
                        "Terms cannot match across properties, and no selection has no results.");
                    inspector.Layout.TextMode.Invoke();
                    inspector.Value.Text = "Draft label";
                    var selection = editor.Selection;
                    inspector.Layout.ArgumentFilter.Focus();
                    NativeQuery("FoNt sI");
                    Require(inspector.Layout.ArgumentFilterStatus.Text.StartsWith("1 matching property.", StringComparison.Ordinal) &&
                        inspector.Layout.ArgumentFilter.Focused &&
                        inspector.Argument == "value" && inspector.Layout.ArgumentLabel.Text == "Editing: value" &&
                        inspector.IsTextMode && inspector.Value.Text == "Draft label" && editor.Text == initial && editor.Selection == selection && changes == 0,
                        "Actual native query input preserves the active text draft and source selection.");
                    Require(inspector.Layout.ArgumentFilter.GetBounds().Width >= 100 &&
                        inspector.Layout.ClearArgumentFilter.GetBounds().Width == 32,
                        "The property query and clear button have usable native bounds.");
                    arguments.Select(Key("fontSize"));
                    Require(inspector.Argument == "fontSize" && inspector.Value.Text == "16" && !inspector.IsTextMode,
                        "A native filtered choice resolves its original property key rather than its result index.");
                    inspector.Value.Text = "22";
                    Query("missing-property");
                    Require(inspector.Layout.ArgumentFilterStatus.Text == "No properties match. The current property stays available." &&
                        inspector.Value.Text == "22" && inspector.Argument == "fontSize",
                        "No matches keep the active property and raw draft available.");
                    arguments.Select(Key("fontSize"));
                    Require(inspector.Value.Text == "22", "Reselecting the pinned current property does not discard its draft.");
                    bool rejected = false;
                    try { arguments.Select(Key("padding")); }
                    catch (XuiException) { rejected = true; }
                    Require(rejected && inspector.Argument == "fontSize", "A hidden non-current property is absent from native choices.");
                    Query("font");
                    inspector.Layout.AuthoredOnly.Invoke();
                    Require(inspector.Layout.ArgumentFilterStatus.Text == "1 matching property." && inspector.Value.Text == "22",
                        "Authored-only combines with the query without replacing a draft.");
                    inspector.Layout.ClearArgumentFilter.Invoke();
                    Require(inspector.Layout.ArgumentFilter.Text == "" && inspector.Layout.ArgumentFilter.Focused &&
                        inspector.Value.Text == "22" && inspector.Layout.ArgumentFilterStatus.Text ==
                            $"{DesignerInspector.FindArguments(label, "", false).Length} matching properties.",
                        "Clear restores all supported properties and focuses the query without changing the draft.");
                    before = changes;
                    inspector.Layout.Apply.Invoke();
                    Query("font");
                    inspector.Layout.AuthoredOnly.Invoke();
                    Require(workspace.IsBusy && editor.Text == initial, "Filters remain available during a pending visual edit without applying its source early.");
                });
                await Ready();
                await Ui(() =>
                {
                    changed = editor.Text;
                    Require(changed.Contains("fontSize: 22", StringComparison.Ordinal) &&
                        changed.Contains("help: \"Keep help\"", StringComparison.Ordinal) && changes == before + 1 &&
                        inspector.Layout.ArgumentFilter.Text == "font",
                        "A draft preserved through filtering compiles into one source edit and retains the query.");
                    Select(0, "fontSize");
                    Require(inspector.Layout.ArgumentFilterStatus.Text == "1 matching property.",
                        "The authored-only choice remains active across source revisions.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "One native undo restores the complete pre-filter source.");
                    editor.Command(TextCommand.Redo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == changed, "Redo restores the compiled property edit.");
                    Select(0, "padding");
                    inspector.Layout.InsetsMode.Invoke();
                    inspector.Layout.InsetTop.Text = "12";
                    Query("font");
                    Require(inspector.IsInsetsMode && inspector.Layout.InsetTop.Text == "12" && inspector.Argument == "padding",
                        "An inset draft remains active outside the property filter.");
                    inspector.Layout.ClearArgumentFilter.Invoke();
                    Require(inspector.IsInsetsMode && inspector.Layout.InsetTop.Text == "12",
                        "Clear preserves all structured fields, not only raw text.");
                    Select(1, "size");
                    inspector.Layout.DimensionMode.Invoke();
                    inspector.Layout.DimensionWidth.Text = "240";
                    Query("enabled");
                    Require(inspector.IsDimensionMode && inspector.Layout.DimensionWidth.Text == "240" && inspector.Argument == "size",
                        "Dimension drafts remain active while filtering for another property.");
                    Select(1, "enabled");
                    inspector.Layout.BooleanMode.Invoke();
                    inspector.Layout.BooleanValue.Invoke();
                    Query("padding");
                    Require(inspector.IsBooleanMode && inspector.TryReadLiteral(out var boolean, out _) && boolean == "false",
                        "Boolean drafts remain active while filtering.");
                    Select(2, "checked");
                    Query("checked");
                    inspector.Layout.AuthoredOnly.Invoke();
                    Require(inspector.Value.ReadOnly && inspector.Layout.ArgumentFilterStatus.Text == "1 matching property." &&
                        inspector.Value.Text == "Active", "Authored expressions stay discoverable and read-only.");
                    editor.ReplaceRange(new(0, 0), editor.Text, "!");
                    string invalid = editor.Text;
                    Query("padding");
                    workspace.ApplyProperty();
                    Require(inspector.Value.ReadOnly && editor.Text == invalid && inspector.Layout.ArgumentFilter.Text == "padding",
                        "Filtering stale source never enables property application.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Select(0, "fontSize");
                    Require(editor.Text == changed && inspector.Layout.ArgumentFilter.Text == "padding" &&
                        inspector.Layout.ArgumentFilterStatus.Text.StartsWith("1 matching property.", StringComparison.Ordinal) &&
                        errors.Count == 0 && window.CallbackStatus == 0,
                        "Undo restores current source, retained filters, and safe native callbacks.");
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
                })) throw new InvalidOperationException("The property search test window rejected an action.");
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

        void Query(string text)
        {
            inspector.Layout.ArgumentFilter.Text = text;
            inspector.FilterArguments();
        }

        ulong Key(string name)
        {
            var names = DesignerInspector.FindArguments(workspace.Hierarchy.Selection, "", false);
            int index = Array.IndexOf(names, name);
            if (index < 0) throw new InvalidOperationException("Unknown test argument: " + name);
            return (ulong)index + 1;
        }

        void Select(int index, string name)
        {
            var node = workspace.Document!.Root!.Children[index];
            editor.Selection = new((ulong)node.Span.Start, (ulong)node.Span.Start);
            workspace.SelectFromCaret();
            inspector.ChooseArgument(name);
        }

        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException($"{style}: {message}");
            assertions++;
        }
    }
}
