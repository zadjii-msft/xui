using Xui;
using Xui.Designer;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        try { Run(); return 0; }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }

    private static void Run()
    {
        using var window = new Window("Designer discard confirmation smoke", 800, 600);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source");
        var anchor = window.Button("New document");
        window.SetContent(window.Stack().Padding(20).Add(anchor).Add(editor));
        long version = 1;
        int accepted = 0, assertions = 0;
        var errors = new List<string>();
        var dialog = new DesignerDiscardDialog(window, () => version, () => editor.Text, errors.Add);
        anchor.Click += () => dialog.Show(anchor, "create a new document", () => accepted++);
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        var driver = Task.Run(async () =>
        {
            try
            {
                await Ui(() => editor.Text = "Original");
                await Task.Delay(60, timeout.Token);
                await Ui(() =>
                {
                    editor.ReplaceRange(new(8, 8), editor.Text, " edited");
                    anchor.Invoke();
                });
                await Ui(() =>
                {
                    Check(dialog.IsPending && dialog.Layout.Message.GetBounds().Width > 100, "The native prompt has usable declarative content.");
                    dialog.View.Cancel();
                    Check(!dialog.IsPending && accepted == 0 && editor.Text == "Original edited", "Cancel preserves source without invoking the continuation.");
                    editor.Command(TextCommand.Undo);
                    Check(editor.Text == "Original", "Cancel preserves native undo history.");
                    anchor.Invoke();
                    dialog.View.Accept();
                    Check(accepted == 0 && dialog.IsPending, "Accept defers the continuation until the dialog closes.");
                });
                await Ui(() =>
                {
                    Check(accepted == 1 && !dialog.IsPending, "An unchanged document continues exactly once.");
                    anchor.Invoke();
                    version++;
                    dialog.View.Accept();
                });
                await Ui(() =>
                {
                    Check(accepted == 1 && errors.Count == 1, "A changed revision invalidates discard approval.");
                    anchor.Invoke();
                    editor.Text = "Changed without a revision";
                    dialog.View.Accept();
                });
                await Ui(() =>
                {
                    Check(accepted == 1 && errors.Count == 2 && editor.Text == "Changed without a revision",
                        "Exact source comparison rejects a stale snapshot even without a revision event.");
                    anchor.Invoke();
                    dialog.Show(anchor, "open a different file", () => accepted += 100);
                    Check(errors.Count == 3, "A second request cannot replace the action awaiting approval.");
                    dialog.View.Accept();
                });
                await Ui(() =>
                {
                    Check(accepted == 2, "Approval applies only to the original pending action.");
                    anchor.Invoke();
                    dialog.View.Cancel();
                    anchor.Invoke();
                    dialog.View.Accept();
                    version++;
                });
                await Ui(() =>
                {
                    Check(accepted == 2 && errors.Count == 4 && !dialog.IsPending,
                        "A revision change after acceptance still prevents the deferred action.");
                    anchor.Invoke();
                    dialog.View.Cancel();
                    Check(!dialog.IsPending, "A new prompt remains usable after stale approval is rejected.");
                });
                Console.WriteLine($"Designer discard UI assertions: {assertions} passed.");
            }
            finally { window.Post(window.Close); }
        });
        window.Run();
        driver.GetAwaiter().GetResult();

        void Check(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
            assertions++;
        }
        async Task Ui(Action action)
        {
            var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!window.Post(() =>
            {
                try { action(); done.SetResult(); }
                catch (Exception error) { done.SetException(error); }
            })) throw new InvalidOperationException("The discard test window rejected a posted action.");
            await done.Task.WaitAsync(timeout.Token);
        }
    }
}
