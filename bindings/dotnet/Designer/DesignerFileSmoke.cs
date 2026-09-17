using System.Diagnostics;

namespace Xui.Designer;

internal sealed partial class DesignerApplication
{
    private async Task FileRecoverySmoke(string directory)
    {
        const string saved = "component Saved { view { Text(\"Saved source\"); } }";
        const string recoveredSource = "component Recovered { view { VStack() { Text(\"Recovered source\"); } } }";
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(120));
        int assertions = 0;
        string currentPath = Path.Combine(directory, "current.xui");
        string originalPath = Path.Combine(directory, "original-\u6587.xui");
        string copyPath = Path.Combine(directory, "recovered-copy.xui");
        try
        {
            Directory.CreateDirectory(directory);
            var donor = new DesignerDocumentStore(Path.Combine(directory, "Drafts"), saved);
            donor.Save(originalPath);
            donor.UpdateSource(recoveredSource);
            donor.PersistRecovery();
            await Ready();
            await Ui(() => { templates.Select(1); view.New.Invoke(); });
            await Ready();
            await Check(() => document.IsUntitled && view.Path.Text.Length == 0 &&
                Normalize(editor.Text) == Normalize(DesignerTemplates.Get("blank").Source),
                "New creates the selected template without a destination path.");
            await Ui(() => { view.Path.Text = currentPath; view.Save.Invoke(); });
            await Check(() => !Dirty && document.FilePath == currentPath &&
                File.ReadAllText(currentPath) == Normalize(editor.Text), "Save updates file identity and clean state.");
            await Ui(() =>
            {
                string source = editor.Text;
                editor.ReplaceRange(new((ulong)source.Length, (ulong)source.Length), source, "\r// Unsaved edit");
            });
            await Ready();
            await Check(() => Dirty && File.Exists(document.RecoveryPath) &&
                File.Exists(Path.ChangeExtension(document.RecoveryPath, ".json")),
                "Native source edits automatically create a recovery source and identity sidecar.");
            string dirty = "";
            bool preservedDiagnostics = false;
            await Ui(() =>
            {
                dirty = editor.Text;
                string previousDiagnostics = diagnostics.Text;
                view.New.Invoke();
                preservedDiagnostics = diagnostics.Text == previousDiagnostics;
            });
            await Check(() => editor.Text == dirty && preservedDiagnostics &&
                view.FileStatus.Text.StartsWith("New document failed:", StringComparison.Ordinal),
                "New preserves edits and compiler diagnostics while it reports a persistent file error.");
            await Ui(view.Undo.Invoke);
            await Ready();
            await Check(() => !Dirty && !File.Exists(document.RecoveryPath) &&
                !File.Exists(Path.ChangeExtension(document.RecoveryPath, ".json")),
                "Undo to the saved source removes the current instance's recovery files.");

            await Ui(view.Recovery.Invoke);
            await Until(() => recovery.SelectedId == donor.RecoveryId && recovery.Layout.SourcePreview.GetBounds().Width > 100);
            await Check(() => recovery.Layout.SourcePreview.Text.Contains("Recovered source", StringComparison.Ordinal),
                "The application recovery button opens the native draft picker.");
            await Ui(recovery.View.Accept);
            await Ready();
            await Check(() => Normalize(editor.Text) == recoveredSource && Dirty && view.Path.Text == originalPath &&
                workspace.Document!.Source == editor.Text, "Recover replaces the native document and refreshes the matching hierarchy.");
            await Check(() => File.Exists(donor.RecoveryPath) && File.Exists(document.RecoveryPath),
                "Recover keeps the original draft and creates a separate draft for this instance.");
            await Ui(view.Undo.Invoke);
            await Check(() => Normalize(editor.Text) == recoveredSource,
                "Recovery is a document boundary rather than an undoable visual replacement.");
            await Ui(view.Recovery.Invoke);
            await Ui(recovery.View.Accept);
            await Check(() => Normalize(editor.Text) == recoveredSource && Dirty,
                "A dirty recovered document cannot be replaced from the recovery picker.");
            await Ui(recovery.View.Cancel);

            File.AppendAllText(originalPath, "\n// External edit");
            await Ui(view.Save.Invoke);
            await Until(() => preview.AppliedVersion == version);
            await Check(() => Dirty && File.ReadAllText(originalPath).EndsWith("// External edit", StringComparison.Ordinal) &&
                view.FileStatus.Text.Contains("changed on disk", StringComparison.Ordinal),
                "A successful preview does not hide an external-file conflict or overwrite that file.");
            await Ui(() => { view.Path.Text = copyPath; view.Save.Invoke(); });
            await Check(() => !Dirty && document.FilePath == copyPath && File.ReadAllText(copyPath) == recoveredSource &&
                File.Exists(donor.RecoveryPath) && !File.Exists(document.RecoveryPath),
                "Save to a new path keeps both versions and removes only this instance's draft.");
            await Ui(() => { view.Path.Text = currentPath; view.Open.Invoke(); });
            await Ready();
            await Check(() => !Dirty && document.FilePath == currentPath &&
                Normalize(editor.Text) == File.ReadAllText(currentPath),
                "Open restores a saved document after the recovery round trip.");
            string invalidPath = Path.Combine(directory, "invalid.xui");
            const string invalidSource = "component Invalid { view { Text(\"unfinished); } }";
            File.WriteAllText(invalidPath, invalidSource);
            await Ui(() => { view.Path.Text = invalidPath; view.Open.Invoke(); });
            await Until(() => workspace.Hierarchy.Layout.Status.Text.StartsWith("Invalid source", StringComparison.Ordinal));
            await Check(() => !Dirty && editor.Text == invalidSource && !workspace.IsCurrent &&
                workspace.Hierarchy.Layout.Status.Text.Contains("read-only", StringComparison.Ordinal),
                "Open permits invalid syntax and marks the stale hierarchy read-only.");
            await Ui(() =>
            {
                string source = editor.Text;
                editor.ReplaceRange(new(0, (ulong)source.Length), source, saved);
            });
            await Ready();
            await Check(() => Dirty && editor.Text == saved, "Native editing repairs an invalid file and refreshes its hierarchy.");
            Console.WriteLine($"Designer application file/recovery assertions: {assertions} passed.");
        }
        finally { window.Post(window.Close); }

        async Task Ui(Action action)
        {
            var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!window.Post(() =>
            {
                try { action(); done.SetResult(); }
                catch (Exception error) { done.SetException(error); }
            })) throw new InvalidOperationException("The designer closed during file/recovery smoke.");
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
                if (elapsed.Elapsed > TimeSpan.FromSeconds(30))
                    throw new TimeoutException("Designer file/recovery smoke condition timed out.");
                await Task.Delay(30, timeout.Token);
            }
        }
        Task Ready() => Until(() => workspace.IsCurrent && !workspace.IsBusy && workspace.Document!.Source == editor.Text);
        async Task Check(Func<bool> condition, string message)
        {
            await Ui(() => { if (!condition()) throw new InvalidOperationException("File/recovery smoke: " + message); });
            assertions++;
        }
    }
}
