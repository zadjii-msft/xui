using System.Runtime.InteropServices;
using Xui;
using Xui.Designer;
using Xui.Generator;

internal static partial class Program
{
    private static void RunHierarchySearch(VisualStyle style)
    {
        const string original = """
            component SearchTree { view { VStack(ref: LayoutRoot) {
                HStack(ref: Actions) {
                    Button("Save changes", id: "save-button");
                    Button("Cancel", ref: CancelAction);
                }
                Text("Save status");
                TextInput("Search ROCKET", text: "value");
                Text("A long label with more than fifty characters before the final needle");
            } } }
            """;
        using var window = new Window("Designer hierarchy search", 1000, 800, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument(original.Replace("ROCKET", "\U0001F680", StringComparison.Ordinal));
        var errors = new List<string>();
        using var workspace = new DesignerWorkspace(window, editor, errors.Add);
        var hierarchy = workspace.Hierarchy;
        window.SetContent(window.Stack(Axis.Horizontal).Add(editor, 1).Add(hierarchy.Layout.Root.PreferredSize(220, 700)));
        window.KeyHandler = hierarchy.HandleSearchKey;
        int changes = 0, selections = 0, assertions = 0;
        editor.Event += value =>
        {
            if (value.Kind != EventKind.Change) return;
            changes++;
            workspace.SourceChanged();
        };
        workspace.SelectionChanged += () => selections++;
        var driver = Task.Run(Drive);
        window.Run();
        driver.GetAwaiter().GetResult();
        Console.WriteLine($"Designer hierarchy search assertions ({style}): {assertions} passed.");

        XuiSourceNode Save() => workspace.Document!.Root!.Children[0].Children[0];
        XuiSourceNode Cancel() => workspace.Document!.Root!.Children[0].Children[1];

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60));
            string initial = "";
            int before = 0;
            ItemKey oldKey = default;
            try
            {
                await Ui(workspace.SourceChanged);
                await Ready();
                await Ui(() =>
                {
                    initial = editor.Text;
                    before = selections;
                    hierarchy.Layout.Query.Focus();
                    NativeQuery("BuTToN save");
                    Require(hierarchy.Layout.SearchStatus.Text == "1 matching control." && selections == before && changes == 0,
                        "Native query edits match every term against one control without moving source or selection.");
                    Require(hierarchy.Layout.Query.GetBounds().Width >= 100 && hierarchy.Layout.ClearSearch.GetBounds().Width == 32,
                        "The query and clear action fit the narrow hierarchy pane.");
                    hierarchy.Tree.Expand(hierarchy.Key(workspace.Document!.Root!), expanded: false);
                    NativeKey(0x0D);
                });
                await Until(() => ReferenceEquals(hierarchy.Selection, Save()));
                await Ui(() =>
                {
                    var save = Save();
                    oldKey = hierarchy.Key(save);
                    Require(hierarchy.Tree.Selection.Focused == oldKey &&
                        editor.Selection == new TextSelection((ulong)save.Span.Start, (ulong)save.Span.End) &&
                        hierarchy.Layout.Query.Focused && hierarchy.Layout.SearchStatus.Text == "Match 1 of 1.",
                        "Native Enter reveals collapsed ancestors, selects exact source, and keeps focus in the query.");
                    Require(selections == before + 1, "One search navigation emits one workspace selection notification.");
                    NativeQuery("button");
                    NativeKey(0x0D);
                });
                await Until(() => ReferenceEquals(hierarchy.Selection, Cancel()));
                await Ui(() =>
                {
                    Require(hierarchy.Layout.SearchStatus.Text == "Match 2 of 2.", "Next visits matches in source order.");
                    Require(hierarchy.HandleSearchKey(new(0x0D, KeyModifiers.Shift, hierarchy.Layout.Query.Id)) &&
                        ReferenceEquals(hierarchy.Selection, Save()), "Shift+Enter moves to the preceding match.");
                    hierarchy.Layout.PreviousMatch.Invoke();
                    Require(ReferenceEquals(hierarchy.Selection, Cancel()), "Previous wraps from the first to the last match.");
                    hierarchy.Layout.NextMatch.Invoke();
                    Require(ReferenceEquals(hierarchy.Selection, Save()), "Next wraps from the last to the first match.");
                    Query("ref CancelAction");
                    hierarchy.MoveSearch();
                    Require(ReferenceEquals(hierarchy.Selection, Cancel()), "Search includes authored reference names.");
                    Query("id SAVE-BUTTON");
                    hierarchy.MoveSearch(reverse: true);
                    Require(ReferenceEquals(hierarchy.Selection, Save()), "Search includes case-insensitive automation IDs.");
                    Query("text \"value\"");
                    hierarchy.MoveSearch();
                    Require(hierarchy.Selection?.Kind == "TextInput", "Named argument names and values participate in matching.");
                    Query("\U0001F680");
                    hierarchy.MoveSearch();
                    Require(hierarchy.Selection?.Kind == "TextInput", "Unicode captions remain searchable.");
                    Query("final needle");
                    hierarchy.MoveSearch();
                    Require(ReferenceEquals(hierarchy.Selection, workspace.Document!.Root!.Children[3]),
                        "Search uses full values, not the truncated native row caption.");
                    var selection = editor.Selection;
                    Query("HStack Save");
                    hierarchy.MoveSearch();
                    Require(hierarchy.Layout.SearchStatus.Text == "No controls match." && editor.Selection == selection,
                        "Terms cannot match unrelated descendants or move selection when there is no result.");
                    Query("Button");
                    hierarchy.MoveSearch(reverse: true);
                    Require(ReferenceEquals(hierarchy.Selection, Cancel()), "Previous starts at the last match when selection is outside the results.");
                    hierarchy.Layout.Query.Focus();
                    NativeKey(0x1B);
                });
                await Until(() => hierarchy.Layout.Query.Text.Length == 0);
                await Ui(() =>
                {
                    Require(ReferenceEquals(hierarchy.Selection, Cancel()) && hierarchy.Layout.Query.Focused &&
                        hierarchy.Layout.SearchStatus.Text == "Enter a query.", "Escape clears only the query and preserves selection.");
                    Query("Button");
                    hierarchy.Layout.ClearSearch.Invoke();
                    Require(hierarchy.Layout.Query.Text == "" && ReferenceEquals(hierarchy.Selection, Cancel()),
                        "The accessible clear action has the same non-destructive behavior.");
                    Query("Button");
                    editor.ReplaceRange(new((ulong)editor.Text.Length, (ulong)editor.Text.Length), editor.Text, "\r// Prior typing");
                    var selection = editor.Selection;
                    hierarchy.MoveSearch();
                    Require(!workspace.IsCurrent && hierarchy.Layout.SearchStatus.Text.Contains("waits", StringComparison.Ordinal) &&
                        editor.Selection == selection, "A new source revision disables search navigation before asynchronous parsing.");
                });
                await Ready();
                await Ui(() =>
                {
                    Require(hierarchy.Layout.Query.Text == "Button" && hierarchy.Key(Save()) != oldKey,
                        "A new hierarchy revision retains the query but retires every old native key.");
                    hierarchy.MoveSearch();
                    Require(ReferenceEquals(hierarchy.Selection, Save()) && hierarchy.Tree.Selection.Focused == hierarchy.Key(Save()),
                        "Retained queries navigate only nodes from the new revision.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == initial, "Search and navigation preserve the preceding native source undo operation.");
                    editor.ReplaceRange(new(0, 0), editor.Text, "!");
                });
                await Until(() => hierarchy.Layout.Status.Text.StartsWith("Invalid source", StringComparison.Ordinal));
                await Ui(() =>
                {
                    Query("Button Save");
                    var selection = editor.Selection;
                    hierarchy.MoveSearch();
                    Require(!workspace.IsCurrent && hierarchy.Layout.Query.Text == "Button Save" && editor.Selection == selection &&
                        hierarchy.Layout.SearchStatus.Text.Contains("waits", StringComparison.Ordinal),
                        "Queries remain editable while an invalid source keeps all stale navigation disabled.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    hierarchy.MoveSearch();
                    Require(editor.Text == initial && ReferenceEquals(hierarchy.Selection, Save()) &&
                        hierarchy.Layout.SearchStatus.Text == "Match 1 of 1.", "Recovery rematches the retained query against valid source.");
                    editor.Focus();
                    Require(!hierarchy.HandleSearchKey(new(0x0D, KeyModifiers.None, editor.Id)) &&
                        !hierarchy.HandleSearchKey(new(0x1B, KeyModifiers.None, editor.Id)),
                        "Hierarchy Enter and Escape do not intercept other native fields.");
                    Require(errors.Count == 0 && window.CallbackStatus == 0, "Search has no runtime or native callback errors.");
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
                })) throw new InvalidOperationException("The hierarchy search test window rejected an action.");
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

        void Query(string value)
        {
            hierarchy.Layout.Query.Text = value;
            hierarchy.RefreshSearch();
        }

        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException($"{style}: {message}");
            assertions++;
        }
    }

    private static void NativeQuery(string text)
    {
        nint editor = GetFocus();
        if (editor == 0 || SendMessageW(editor, 0x000C, 0, text) == 0)
            throw new InvalidOperationException("The native hierarchy query rejected text.");
    }

    private static void NativeKey(uint key)
    {
        if (!PostMessageW(GetFocus(), 0x0100, key, 0)) throw new InvalidOperationException("Native hierarchy key posting failed.");
    }

    [DllImport("user32.dll")] private static extern nint GetFocus();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint SendMessageW(nint window, uint message, nuint first, string second);
    [DllImport("user32.dll")] private static extern bool PostMessageW(nint window, uint message, nuint first, nint second);
}
