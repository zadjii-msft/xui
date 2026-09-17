using System.Diagnostics;

namespace Xui.Designer;

internal static class DesignerBuilderSmoke
{
    internal static async Task Run(Window window, MultilineText editor, MultilineText diagnostics,
        DesignerWorkspace workspace, DesignerLayout view)
    {
        const string fixture = """
            component BuilderFixture {
                state string Title = "Expression title";
                view {
                    VStack(spacing: 8) {
                        Text("First");
                        Text(Title);
                        Button("Go");
                    }
                }
            }
            """;
        int assertions = 0;
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(90));
        try
        {
            await Ui(() =>
            {
                editor.Text = fixture;
                workspace.SourceChanged();
            });
            await Ready();
            await Check(() => editor.GetBounds().Width >= 150 && editor.GetBounds().Height >= 150 &&
                workspace.Hierarchy.Tree.GetBounds().Width >= 100 && workspace.Hierarchy.Tree.GetBounds().Height >= 100 &&
                workspace.Inspector.Value.GetBounds().Width >= 100 &&
                diagnostics.GetBounds().Height >= 60, "Native source, tree, inspector and diagnostics have usable bounds.");
            await Ui(() => editor.Focus());
            await Check(() => !workspace.HandleHierarchyKey(new UiKeyEvent('D', KeyModifiers.Control, editor.Id)),
                "Hierarchy shortcuts do not intercept keys in the native source editor.");
            string original = "";
            await Ui(() =>
            {
                original = editor.Text;
                editor.Selection = new((ulong)original.IndexOf("\"First\"", StringComparison.Ordinal),
                    (ulong)original.IndexOf("\"First\"", StringComparison.Ordinal));
                workspace.Hierarchy.Layout.FromCaret.Invoke();
                workspace.Inspector.ChooseArgument("value");
                workspace.Inspector.Value.Text = "\"Changed\"";
                workspace.Inspector.Layout.Apply.Invoke();
            });
            await Until(() => !workspace.IsBusy && editor.Text.Contains("\"Changed\"", StringComparison.Ordinal));
            await Ready();
            await Check(() => workspace.Hierarchy.Selection?.Kind == "Text" &&
                editor.Selection.End > editor.Selection.Start, "Applying a property changes source and selects the resulting control.");
            await Ui(view.Undo.Invoke);
            await Until(() => editor.Text == original);
            await Ready();
            await Ui(view.Redo.Invoke);
            await Until(() => editor.Text.Contains("\"Changed\"", StringComparison.Ordinal));
            await Ready();
            await Check(() => editor.Text != original, "Native toolbar undo and redo preserve the visual edit.");

            await Ui(() =>
            {
                var root = workspace.Document!.Root!;
                var expression = root.Children[1];
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(expression));
                workspace.Inspector.ChooseArgument("value");
            });
            await Check(() => workspace.Inspector.Value.ReadOnly &&
                workspace.Inspector.Layout.ArgumentHelp.Text.Contains("Expression: read-only", StringComparison.Ordinal) &&
                editor.Selection.Start == (ulong)workspace.Document!.Root!.Children[1].Span.Start,
                "Hierarchy selection reveals the source and expressions are explicitly read-only.");

            await Ui(() =>
            {
                var root = workspace.Document!.Root!;
                workspace.Hierarchy.Tree.Expand(workspace.Hierarchy.Key(root), false);
                workspace.Hierarchy.Tree.Expand(workspace.Hierarchy.Key(root));
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(root));
                workspace.Inspector.Layout.Insert.Invoke();
            });
            await Until(() => workspace.IsCurrent && !workspace.IsBusy && workspace.Document!.Root!.Children.Count == 4);
            await Check(() => workspace.Document!.Root!.Children.Last().Kind == "Text", "Palette inserts a real compilable control.");
            await Ui(() =>
            {
                var last = workspace.Document!.Root!.Children.Last();
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(last));
                workspace.Inspector.Layout.Duplicate.Invoke();
            });
            await Until(() => workspace.IsCurrent && !workspace.IsBusy && workspace.Document!.Root!.Children.Count == 5);
            await Ui(() =>
            {
                var button = workspace.Document!.Root!.Children[2];
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(button));
                workspace.Hierarchy.Tree.Focus();
                if (!workspace.HandleHierarchyKey(new UiKeyEvent(0x26, KeyModifiers.Alt, workspace.Hierarchy.Tree.Id)))
                    throw new InvalidOperationException("The hierarchy did not handle Alt+Up.");
            });
            await Until(() => !workspace.IsBusy);
            await Ready();
            await Check(() => workspace.Hierarchy.Selection?.Id == workspace.Document!.Root!.Children[1].Id &&
                workspace.Hierarchy.Selection?.Kind == "Button",
                "Move up selects the moved control in the new revision.");
            await Ui(workspace.Inspector.Layout.Delete.Invoke);
            await Until(() => workspace.IsCurrent && !workspace.IsBusy && workspace.Document!.Root!.Children.Count == 4);

            string concurrent = "";
            await Ui(() =>
            {
                var first = workspace.Document!.Root!.Children[0];
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(first));
                workspace.Inspector.ChooseArgument("value");
                workspace.Inspector.Value.Text = "\"Must not apply\"";
                workspace.Inspector.Layout.Apply.Invoke();
                string source = editor.Text;
                editor.ReplaceRange(new((ulong)source.Length, (ulong)source.Length), source, "\r// Concurrent typing");
                concurrent = editor.Text;
            });
            await Until(() => !workspace.IsBusy);
            await Ready();
            await Check(() => editor.Text == concurrent && !editor.Text.Contains("Must not apply", StringComparison.Ordinal),
                "Typing while a visual edit compiles cancels the stale result.");
            Guid validRevision = Guid.Empty;
            await Ui(() =>
            {
                validRevision = workspace.Document!.Revision;
                string source = editor.Text;
                editor.ReplaceRange(new(0, 0), source, "!");
            });
            await Until(() => workspace.Hierarchy.Layout.Status.Text.StartsWith("Invalid source", StringComparison.Ordinal));
            await Check(() => !workspace.IsCurrent && workspace.Document!.Revision == validRevision &&
                workspace.Inspector.Value.ReadOnly, "Invalid source retains only an explicitly stale, read-only hierarchy.");
            await Ui(view.Undo.Invoke);
            await Ready();
            await Check(() => editor.Text == concurrent, "Native undo recovers from invalid source without replacing the document.");
            await Ui(() =>
            {
                editor.Text = """
                    component GridFixture {
                        view {
                            Grid("Cells", columns: [new(global::Xui.TrackSizing.Star), new(global::Xui.TrackSizing.Star)]) {
                                Text("Left", column: 0);
                            }
                        }
                    }
                    """;
                workspace.SourceChanged();
            });
            await Ready();
            await Ui(() =>
            {
                var cell = workspace.Document!.Root!.Children[0];
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(cell));
                workspace.Inspector.Layout.Row.Text = "0";
                workspace.Inspector.Layout.Column.Text = "1";
                workspace.Hierarchy.Tree.Focus();
                if (!workspace.HandleHierarchyKey(new UiKeyEvent('D', KeyModifiers.Control, workspace.Hierarchy.Tree.Id)))
                    throw new InvalidOperationException("The hierarchy did not handle Ctrl+D.");
            });
            await Until(() => workspace.IsCurrent && !workspace.IsBusy && workspace.Document!.Root!.Children.Count == 2);
            string grid = "";
            await Ui(() =>
            {
                grid = editor.Text;
                workspace.Inspector.Layout.Duplicate.Invoke();
            });
            await Until(() => !workspace.IsBusy);
            await Check(() => editor.Text == grid &&
                workspace.Inspector.Layout.Feedback.Text.Contains("overlap", StringComparison.OrdinalIgnoreCase),
                "Grid duplication uses the requested cell and refuses overlap without changing source.");
            await Ui(() =>
            {
                editor.Text = "component OneLine { view { VStack() { Text(\"Single\"); } } }";
                workspace.SourceChanged();
            });
            await Ready();
            await Ui(workspace.Inspector.Layout.Insert.Invoke);
            await Until(() => workspace.IsCurrent && !workspace.IsBusy && workspace.Document!.Root!.Children.Count == 2);
            await Check(() => editor.Text.Contains('\r') && !editor.Text.Contains('\n') &&
                editor.Selection.Start == (ulong)workspace.Document!.Root!.Children[1].Span.Start &&
                editor.Selection.End == (ulong)workspace.Document.Root.Children[1].Span.End,
                "One-line source insertion selects exact native CR offsets.");
            await Ui(() =>
            {
                editor.Text = "component UnicodeLabel { view { VStack() { Text(\"" +
                    new string('x', 45) + "\U0001F600" + new string('y', 10) + "\"); } } }";
                workspace.SourceChanged();
            });
            await Ready();
            await Ui(() =>
            {
                var text = workspace.Document!.Root!.Children[0];
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(text));
            });
            await Check(() => workspace.Inspector.Value.Text.Contains("\U0001F600", StringComparison.Ordinal),
                "Truncated native hierarchy labels preserve complete UTF-16 scalars.");
            await Ui(() =>
            {
                editor.Text = "component Nested { view { VStack() { HStack() { VStack() { Text(\"Deep\"); } } } } }";
                workspace.SourceChanged();
            });
            await Ready();
            await Ui(() =>
            {
                int caret = editor.Text.IndexOf("\"Deep\"", StringComparison.Ordinal);
                editor.Selection = new((ulong)caret, (ulong)caret);
                workspace.Hierarchy.Layout.FromCaret.Invoke();
            });
            await Check(() => workspace.Hierarchy.Selection?.Kind == "Text" &&
                workspace.Hierarchy.Tree.Selection.Focused == workspace.Hierarchy.Key(workspace.Hierarchy.Selection),
                "Native tree sources expose descendants at every depth and caret selection expands all ancestors.");
            await Ui(() => workspace.Hierarchy.Tree.Expand(workspace.Hierarchy.Key(workspace.Document!.Root!), false));
            await Check(() => workspace.Hierarchy.Selection?.Id == workspace.Document!.Root!.Id &&
                workspace.Hierarchy.Tree.Selection.Focused == workspace.Hierarchy.Key(workspace.Document.Root),
                "Collapsing an ancestor keeps the inspector synchronized with native hierarchy focus.");
            string newest = "";
            await Ui(() =>
            {
                for (int i = 0; i < 24; i++)
                {
                    string source = editor.Text;
                    editor.ReplaceRange(new((ulong)source.Length, (ulong)source.Length), source, $"\r// Burst {i}");
                }
                newest = editor.Text;
            });
            await Ready();
            await Check(() => workspace.Document!.Source == newest && workspace.IsCurrent,
                "A burst of native edits publishes only a hierarchy for the newest exact source.");
            Console.WriteLine($"Designer builder UI assertions: {assertions} passed.");
        }
        finally
        {
            if (!window.Post(window.Close)) Console.Error.WriteLine("Builder smoke could not close the designer window.");
        }

        async Task Ui(Action action)
        {
            var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!window.Post(() =>
            {
                try { action(); done.SetResult(); }
                catch (Exception error) { done.SetException(error); }
            })) throw new InvalidOperationException("Designer window closed during builder smoke.");
            await done.Task.WaitAsync(timeout.Token);
        }
        async Task Until(Func<bool> condition)
        {
            var elapsed = Stopwatch.StartNew();
            while (true)
            {
                bool satisfied = false;
                await Ui(() => satisfied = condition());
                if (satisfied) return;
                if (elapsed.Elapsed > TimeSpan.FromSeconds(25))
                {
                    string state = "";
                    await Ui(() => state = $"Current={workspace.IsCurrent}, busy={workspace.IsBusy}. " +
                        $"{workspace.Hierarchy.Layout.Status.Text} {workspace.Inspector.Layout.Feedback.Text} {diagnostics.Text}");
                    throw new TimeoutException("Builder smoke condition timed out. " + state);
                }
                await Task.Delay(30, timeout.Token);
            }
        }
        Task Ready() => Until(() => workspace.IsCurrent && !workspace.IsBusy && workspace.Document!.Source == editor.Text);
        async Task Check(Func<bool> condition, string message)
        {
            await Ui(() => { if (!condition()) throw new InvalidOperationException("Builder smoke: " + message); });
            assertions++;
        }
    }
}
