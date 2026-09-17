using Xui;
using Xui.Designer;

internal static class Program
{
    private static int assertions;
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    [STAThread]
    private static int Main()
    {
        string root = Path.Combine(Path.GetTempPath(), "XuiDesignerRecovery-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            Run(root);
            Console.WriteLine($"Designer recovery UI assertions: {assertions} passed.");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    private static void Run(string root)
    {
        const string initial = """component Initial { view { Text("Initial"); } }""";
        const string edited = """component Restored { view { Text("Recovered"); } }""";
        var draft = new DesignerDocumentStore(root, initial);
        draft.UpdateSource(edited);
        draft.PersistRecovery();
        var document = new DesignerDocumentStore(root, initial);
        var errors = new List<string>();
        int recovered = 0;
        using var window = new Window("Designer recovery UI smoke", 800, 760);
        window.SetShowActivated(false);
        var anchor = window.Button("Recovery drafts");
        var dialog = new DesignerRecoveryDialog(window, document, () => recovered++, errors.Add);
        anchor.Click += () => dialog.Show(anchor);
        window.SetContent(window.Stack().Padding(20).Add(anchor));
        Guid corruptId = Guid.NewGuid();
        string corruptPath = Path.Combine(root, corruptId.ToString("N") + ".xui");
        Exception? failure = null;
        bool completed = false;
        var driver = Task.Run(async () =>
        {
            try
            {
                for (int step = 0; step <= 6; step++)
                {
                    var next = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                    int current = step;
                    if (!window.Post(() =>
                    {
                        try { Advance(current); next.SetResult(); }
                        catch (Exception error) { next.SetException(error); window.Close(); }
                    })) throw new InvalidOperationException("The recovery test window rejected a posted action.");
                    await next.Task.WaitAsync(TimeSpan.FromSeconds(10));
                    await Task.Delay(60);
                }
            }
            catch (Exception error)
            {
                failure = error;
                window.Post(window.Close);
            }
        });
        window.Run();
        driver.GetAwaiter().GetResult();
        if (failure is not null) throw new InvalidOperationException("Recovery UI smoke failed.", failure);
        Require(completed, "The recovery fixture completed every native action.");

        void Advance(int step)
        {
            switch (step)
            {
                case 0:
                    anchor.Invoke();
                    break;
                case 1:
                    Require(dialog.SelectedId == draft.RecoveryId, "The dialog selects an available recovery draft.");
                    Require(dialog.Layout.SourcePreview.GetBounds().Width > 100, "The declarative recovery content has usable native bounds.");
                    dialog.View.Accept();
                    Require(recovered == 1 && document.Source == edited && document.IsDirty, "Accept recovers a copy through the document model.");
                    Require(File.Exists(draft.RecoveryPath), "Recovery does not delete another instance's draft.");
                    break;
                case 2:
                    dialog.Show(anchor);
                    dialog.View.Accept();
                    Require(recovered == 1 && document.Source == edited, "Dirty source prevents recovery through the primary action.");
                    dialog.DeleteSelected();
                    Require(File.Exists(draft.RecoveryPath), "Deletion requires explicit confirmation.");
                    dialog.View.Cancel();
                    document.New(initial, discardChanges: true);
                    File.WriteAllText(corruptPath, initial);
                    File.WriteAllText(Path.ChangeExtension(corruptPath, ".json"), "{broken");
                    dialog.Show(anchor);
                    break;
                case 3:
                    int index = document.ListRecovery().ToList().FindIndex(entry => entry.Id == corruptId);
                    Require(index >= 0, "Corrupt drafts remain discoverable.");
                    dialog.Choices.Select((ulong)index + 1);
                    Require(dialog.SelectedId == corruptId, "The native combo selection updates the selected recovery identity.");
                    dialog.View.Accept();
                    Require(recovered == 1 && document.Source == initial, "A corrupt draft cannot replace the source.");
                    dialog.Layout.ConfirmDelete.Invoke();
                    dialog.Layout.Delete.Invoke();
                    Require(!File.Exists(corruptPath) && !File.Exists(Path.ChangeExtension(corruptPath, ".json")), "Confirmed deletion removes the selected draft and sidecar.");
                    Require(File.Exists(draft.RecoveryPath), "Deletion leaves the other recovery source unchanged.");
                    dialog.DeleteSelected();
                    Require(File.Exists(draft.RecoveryPath), "Refresh resets deletion approval before selecting another draft.");
                    dialog.View.Cancel();
                    break;
                case 4:
                    dialog.Show(anchor);
                    break;
                case 5:
                    File.Delete(draft.RecoveryPath);
                    dialog.View.Accept();
                    Require(errors.Count == 1, "A draft removed after selection reports an explicit error.");
                    Require(recovered == 1 && document.Source == initial && !document.IsDirty, "A failed recovery preserves the current document.");
                    dialog.Show(anchor);
                    break;
                case 6:
                    Require(dialog.SelectedId is null, "An empty catalog has no selected draft.");
                    dialog.View.Accept();
                    Require(recovered == 1, "An empty catalog cannot accept recovery.");
                    dialog.View.Cancel();
                    completed = true;
                    window.Close();
                    break;
            }
        }
    }
}
