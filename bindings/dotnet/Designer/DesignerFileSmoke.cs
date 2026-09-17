using System.Diagnostics;

namespace Xui.Designer;

internal sealed partial class DesignerApplication
{
    private DesignerFileDialogProbe? fileSmokeClosedOwner;
    private async Task FileRecoverySmoke(string directory, bool closeOnly)
    {
        const string saved = "component Saved { view { Text(\"Saved source\"); } }";
        const string recoveredSource = "component Recovered { view { VStack() { Text(\"Recovered source\"); } } }";
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(240));
        int assertions = 0;
        string currentPath = Path.Combine(directory, "current.xui");
        string originalPath = Path.Combine(directory, "original-\u6587.xui");
        string copyPath = Path.Combine(directory, "recovered-copy.xui");
        DesignerFileDialogProbe? pendingChooser = null;
        try
        {
            Directory.CreateDirectory(directory);
            var donor = new DesignerDocumentStore(Path.Combine(directory, "Drafts"), saved);
            donor.Save(originalPath);
            donor.UpdateSource(recoveredSource);
            donor.PersistRecovery();
            await Ready();
            if (closeOnly)
            {
                await Until(() => preview.AppliedVersion == version);
                await CloseOwner();
                Console.WriteLine($"Designer file-owner close assertions: {assertions} passed.");
                return;
            }
            await Ui(() => { templates.Select(1); view.New.Invoke(); });
            await Ready();
            await Check(() => document.IsUntitled && view.Path.Text.Length == 0 &&
                Normalize(editor.Text) == Normalize(DesignerTemplates.Get("blank").Source),
                "New creates the selected template without a destination path.");
            await Ui(() => { view.Path.Text = currentPath; Key('S', KeyModifiers.Control); });
            await Check(() => !Dirty && document.FilePath == currentPath &&
                File.ReadAllText(currentPath) == Normalize(editor.Text), "Save updates file identity and clean state.");
            await Ui(() =>
            {
                if (editor.GetBounds() is { Width: <= 0 } or { Height: <= 0 })
                    throw new InvalidOperationException($"The source editor has no usable bounds: {editor.GetBounds()}.");
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
                fileActions.Discard.IsPending && File.Exists(document.RecoveryPath),
                "New preserves edits, recovery, and compiler diagnostics while it requests explicit discard approval.");
            await Ui(fileActions.Discard.View.Cancel);
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
            await Ui(() =>
            {
                editor.Selection = new(1, 4);
                editor.Focus();
                using var chooser = new DesignerFileDialogProbe(window, accept: false);
                Key('O', KeyModifiers.Control);
                chooser.Check();
            }, modal: true);
            await Check(() => !Dirty && document.FilePath == currentPath && editor.Selection == new TextSelection(1, 4),
                "Canceling the actual native Open chooser preserves file identity and source selection.");
            await Ui(() =>
            {
                view.Path.Text = copyPath;
                using var chooser = new DesignerFileDialogProbe(window, accept: true);
                view.OpenPicker.Invoke();
                chooser.Check();
            }, modal: true);
            await Ready();
            await Check(() => !Dirty && document.FilePath == copyPath && editor.Text == recoveredSource,
                "The actual native Open chooser loads the selected file and matching hierarchy.");
            string chooserCopy = Path.Combine(directory, "chooser-\u6587\U0001F600.xui");
            await Ui(() =>
            {
                view.Path.Text = chooserCopy;
                using var chooser = new DesignerFileDialogProbe(window, accept: false);
                Key('S', KeyModifiers.Control | KeyModifiers.Shift);
                chooser.Check();
            }, modal: true);
            await Check(() => !File.Exists(chooserCopy) && document.FilePath == copyPath,
                "Canceling native Save As creates no file and preserves the current identity.");
            await Ui(() =>
            {
                using var chooser = new DesignerFileDialogProbe(window, accept: true);
                view.SaveAs.Invoke();
                chooser.Check();
            }, modal: true);
            await Check(() => !Dirty && document.FilePath == chooserCopy && File.ReadAllText(chooserCopy) == recoveredSource,
                "Native Save As preserves an exact Unicode path and saves through the document store.");
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
            const string diagnosticSource = "component Diagnostic { view { Text(MissingValue); } }";
            await Ui(() =>
            {
                string source = editor.Text;
                editor.ReplaceRange(new(0, (ulong)source.Length), source, diagnosticSource);
            });
            await Until(() => diagnosticNavigator.IsCurrent && diagnostics.Text.Contains("CS0103", StringComparison.Ordinal));
            await Ui(diagnosticNavigator.Layout.Next.Invoke);
            await Check(() => editor.Selection.Start == (ulong)diagnosticSource.IndexOf("MissingValue", StringComparison.Ordinal) &&
                workspace.Hierarchy.Selection?.Kind == "Text" && diagnostics.GetBounds().Height >= 60,
                "The integrated diagnostic toolbar selects the exact compiler location and matching hierarchy control.");
            bool stalePreserved = false;
            await Ui(() =>
            {
                string source = editor.Text;
                editor.ReplaceRange(new(0, 0), source, "// shifted\r");
                var selection = editor.Selection;
                diagnosticNavigator.Move();
                stalePreserved = editor.Selection == selection && !diagnosticNavigator.IsCurrent;
            });
            await Check(() => stalePreserved, "Source changes invalidate diagnostic navigation before another compile result arrives.");
            await Ui(() => Key('N', KeyModifiers.Control));
            await Check(() => fileActions.Discard.IsPending && Dirty, "Dirty New waits for explicit native discard approval.");
            await Ui(fileActions.Discard.View.Accept);
            await Ready();
            await Check(() => !Dirty && document.IsUntitled && !File.Exists(document.RecoveryPath),
                "Approved New replaces the document and removes only its own recovery draft.");
            await Ui(() =>
            {
                string source = editor.Text;
                editor.ReplaceRange(new((ulong)source.Length, (ulong)source.Length), source, "\r// Keep this edit");
            });
            await Ready();
            await Ui(() =>
            {
                view.Path.Text = currentPath;
                view.OpenPicker.Invoke();
                pendingChooser = new DesignerFileDialogProbe(window, accept: false);
                fileActions.Discard.View.Accept();
            });
            await Ui(() => { pendingChooser!.Check(); pendingChooser.Dispose(); pendingChooser = null; }, modal: true);
            await Check(() => Dirty && document.IsUntitled && editor.Text.EndsWith("// Keep this edit", StringComparison.Ordinal) &&
                File.Exists(document.RecoveryPath), "Canceling the chooser after discard approval still preserves source and recovery.");
            await Ui(() =>
            {
                view.Path.Text = Path.Combine(directory, "missing-file.xui");
                view.Open.Invoke();
            });
            await Ui(fileActions.Discard.View.Accept);
            await Check(() => Dirty && document.IsUntitled && editor.Text.EndsWith("// Keep this edit", StringComparison.Ordinal) &&
                File.Exists(document.RecoveryPath) && view.FileStatus.Text.StartsWith("Open failed:", StringComparison.Ordinal),
                "An approved Open that cannot read its destination keeps the original source and recovery draft.");
            await Ui(view.Undo.Invoke);
            await Ready();
            await Check(() => !Dirty && !File.Exists(document.RecoveryPath),
                "Canceling confirmation and the native chooser preserves the previous source undo operation.");
            await Ui(view.Redo.Invoke);
            await Ready();
            await Check(() => Dirty && editor.Text.EndsWith("// Keep this edit", StringComparison.Ordinal),
                "The canceled file action also preserves native redo.");
            await Ui(() =>
            {
                view.Path.Text = currentPath;
                view.OpenPicker.Invoke();
                pendingChooser = new DesignerFileDialogProbe(window, accept: true);
                fileActions.Discard.View.Accept();
            });
            await Ui(() => { pendingChooser!.Check(); pendingChooser.Dispose(); pendingChooser = null; }, modal: true);
            await Ready();
            await Check(() => !Dirty && document.FilePath == currentPath && !File.Exists(document.RecoveryPath),
                "Approved dirty Open closes its confirmation before the real chooser and replaces the selected document.");
            await Ui(view.Undo.Invoke);
            await Check(() => Normalize(editor.Text) == File.ReadAllText(currentPath),
                "Approved Open starts a new native undo history instead of restoring discarded edits.");
            await Ui(() =>
            {
                view.Path.Text = "";
                using var chooser = new DesignerFileDialogProbe(window, accept: false);
                Key('S', KeyModifiers.Control);
                chooser.Check();
            }, modal: true);
            await Check(() => document.FilePath == currentPath && !Dirty,
                "Save with an empty path uses the actual native destination chooser without changing identity on cancel.");
            await Ui(() =>
            {
                view.Path.Text = Path.Combine(directory, "missing-directory", "missing.xui");
                using var chooser = new DesignerFileDialogProbe(window, accept: false);
                view.OpenPicker.Invoke();
            }, modal: true);
            await Check(() => document.FilePath == currentPath && !Dirty &&
                view.FileStatus.Text.StartsWith("Open failed:", StringComparison.Ordinal),
                "An invalid native chooser directory reports an error rather than a cancellation or document replacement.");
            string staleDestination = Path.Combine(directory, "stale-choice.xui");
            await Ui(() =>
            {
                view.Path.Text = staleDestination;
                using var chooser = new DesignerFileDialogProbe(window, accept: true, () => version++);
                view.SaveAs.Invoke();
                chooser.Check();
            }, modal: true);
            await Check(() => !File.Exists(staleDestination) && document.FilePath == currentPath &&
                view.FileStatus.Text.Contains("source changed", StringComparison.Ordinal),
                "A revision change inside the actual native chooser rejects its selected save destination.");
            await Ui(view.Render.Invoke);
            await Until(() => preview.AppliedVersion == version);
            Console.WriteLine($"Designer application file/recovery assertions: {assertions} passed.");
        }
        finally { window.Post(() => { pendingChooser?.Dispose(); window.Close(); }); }

        Task CloseOwner() => Ui(() =>
        {
            string closeDestination = Path.Combine(directory, "closed-owner.xui");
            string? previousPath = document.FilePath;
            string previousSource = document.Source;
            view.Path.Text = closeDestination;
            using var chooser = new DesignerFileDialogProbe(window, accept: false, () =>
            {
                Console.WriteLine("Requesting native chooser owner close.");
                window.Close();
                Console.WriteLine("Native chooser owner close returned.");
            }, postResult: false);
            view.SaveAs.Invoke();
            chooser.Check();
            if (window.Post(() => throw new InvalidOperationException("A closed owner accepted a late UI callback.")))
                throw new InvalidOperationException("The closed designer accepted a posted result.");
            fileSmokeClosedOwner = chooser;
            if (File.Exists(closeDestination) || document.FilePath != previousPath || document.Source != previousSource)
                throw new InvalidOperationException("Closing the file chooser owner changed the document or wrote a file.");
            assertions++;
        }, modal: true);

        void Key(uint key, KeyModifiers modifiers)
        {
            if (window.KeyHandler?.Invoke(new(key, modifiers, 0)) != true)
                throw new InvalidOperationException("The designer did not handle its file shortcut.");
        }

        async Task Ui(Action action, bool modal = false)
        {
            var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!window.Post(() =>
            {
                try { action(); done.SetResult(); }
                catch (Exception error) { done.SetException(error); }
            })) throw new InvalidOperationException("The designer closed during file/recovery smoke.");
            await done.Task.WaitAsync(TimeSpan.FromSeconds(modal ? 70 : 30), timeout.Token);
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
