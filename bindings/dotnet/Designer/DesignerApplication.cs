using System.Text;
using System.Threading.Channels;

namespace Xui.Designer;

internal sealed partial class DesignerApplication : IDisposable
{
    private const int MaximumLength = 65536;
    private readonly Window window = new("XUI Designer", 1440, 960, visualStyle: VisualStyle.WinUI);
    private readonly CancellationTokenSource lifetime = new();
    private readonly Channel<(long Version, string Source)> edits = Channel.CreateBounded<(long, string)>(
        new BoundedChannelOptions(1) { FullMode = BoundedChannelFullMode.DropOldest, SingleReader = true });
    private readonly MultilineText editor;
    private readonly MultilineText diagnostics;
    private readonly DesignerLayout view;
    private readonly PreviewHost preview;
    private readonly DesignerWorkspace workspace;
    private readonly DesignerDiagnosticNavigator diagnosticNavigator;
    private readonly ComboBox templates;
    private readonly Task compiler;
    private readonly DesignerDocumentStore document;
    private readonly DesignerRecoveryDialog recovery;
    private CancellationTokenSource? revision;
    private long version;
    private bool live = true, light, disposed;
    private int templateIndex;
    private int smokeStage;
    private Exception? smokeError;

    internal DesignerApplication(string? initialPath, string? recoveryDirectory = null)
    {
        try
        {
            editor = window.MultilineText("XUI source").SetMaximumLength(MaximumLength);
            editor.SetControlStyleValues(StylePart.Text, new PartStyleValues { FontFamily = "Consolas", FontSize = 14 });
            diagnostics = window.MultilineText("Compiler diagnostics").SetReadOnly(true).SetMaximumLength(MaximumLength);
            preview = new PreviewHost(window, (value, message, success) =>
                window.Post(() => OnPreview(value, message, success)));
            workspace = new DesignerWorkspace(window, editor, ShowError);
            diagnosticNavigator = new DesignerDiagnosticNavigator(window, editor, diagnostics,
                () => version, ReportNavigation, workspace.SelectFromCaret);
            templates = window.ComboBox("New document template", false).SetAutomationId("designer-templates");
            templates.SetItems(DesignerTemplates.All.Select((template, index) => new Choice((ulong)index + 1, template.Name)).ToArray(), 1);
            templates.Event += e => { if (e.Kind == EventKind.Selection) templateIndex = checked((int)e.Value - 1); };
            view = new DesignerLayout(window, editor, diagnosticNavigator.View, workspace.Hierarchy.Layout.Root,
                workspace.Inspector.Layout.Root, preview.View, templates);
            document = new DesignerDocumentStore(recoveryDirectory ?? Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Xui", "Designer", "Drafts"),
                DesignerTemplates.Get("counter").Source);
            if (initialPath is not null) document.Open(initialPath);
            editor.Text = document.Source;
            document.UpdateSource(editor.Text);
            view.Path.Text = document.FilePath ?? "";
            recovery = new DesignerRecoveryDialog(window, document, RecoveredDocument, ReportFileError);
            SetFileStatus(document.FilePath is { } path ? $"Opened {path}" : "Untitled example. Choose a file path before saving.");
            editor.Event += OnEditorEvent;
            view.Open.Click += Open;
            view.Save.Click += Save;
            view.New.Click += NewDocument;
            view.Recovery.Click += () => { document.UpdateSource(editor.Text); recovery.Show(view.Recovery); };
            view.Undo.Click += () => SourceCommand(TextCommand.Undo);
            view.Redo.Click += () => SourceCommand(TextCommand.Redo);
            view.Render.Click += () => Schedule(immediate: true);
            view.Live.Changed += value => { live = value; Schedule(); };
            view.Light.Changed += value => { light = value; window.SetTheme(value ? Theme.Light : Theme.Dark); Schedule(immediate: true); };
            window.KeyHandler = key =>
            {
                if (key.Modifiers == KeyModifiers.Control && key.VirtualKey == 'S') { Save(); return true; }
                if (key.Modifiers == KeyModifiers.Control && key.VirtualKey == 0x0D) { Schedule(immediate: true); return true; }
                if (key.Modifiers == (KeyModifiers.Control | KeyModifiers.Shift) && key.VirtualKey == 'L')
                { workspace.SelectFromCaret(); return true; }
                if (diagnosticNavigator.HandleKey(key)) return true;
                return workspace.HandleHierarchyKey(key);
            };
            compiler = Task.Run(CompileEdits);
        }
        catch
        {
            lifetime.Cancel();
            workspace?.Dispose();
            preview?.Dispose();
            window.Dispose();
            lifetime.Dispose();
            throw;
        }
    }

    internal void Run(bool smoke, bool builderSmoke = false, string? fileSmokeDirectory = null)
    {
        if (smoke)
        {
            smokeStage = 1;
            _ = Task.Run(async () =>
            {
                try
                {
                    await Task.Delay(TimeSpan.FromSeconds(45), lifetime.Token);
                    window.Post(() => { smokeError = new TimeoutException("Designer smoke test timed out."); window.Close(); });
                }
                catch (OperationCanceledException) when (lifetime.IsCancellationRequested) { }
            });
        }
        if (builderSmoke) smokeStage = -1;
        Task? driver = builderSmoke ? Task.Run(() => DesignerBuilderSmoke.Run(window, editor, diagnostics, workspace, view))
            : fileSmokeDirectory is not null ? Task.Run(() => FileRecoverySmoke(fileSmokeDirectory)) : null;
        window.Post(() => { workspace.SourceChanged(); Schedule(immediate: true); });
        window.Run();
        driver?.GetAwaiter().GetResult();
        if (smokeError is not null) throw smokeError;
    }

    private static string Normalize(string text) => text.Replace("\r\n", "\n").Replace('\r', '\n');
    private bool Dirty
    {
        get { document.UpdateSource(editor.Text); return document.IsDirty; }
    }

    private void OnEditorEvent(UiEvent e)
    {
        if (e.Kind != EventKind.Change) return;
        workspace.SourceChanged();
        PersistDraft();
        Schedule();
    }

    private void PersistDraft()
    {
        if (smokeStage != 0) return;
        try
        {
            document.UpdateSource(editor.Text);
            document.PersistRecovery();
        }
        catch (Exception error) when (FileError(error))
        {
            ReportFileError($"Could not save recovery draft: {error.Message}");
        }
    }

    private void Schedule(bool immediate = false)
    {
        workspace.SourceChanged();
        revision?.Cancel();
        version++;
        diagnosticNavigator.Invalidate();
        preview.Supersede(version);
        window.SetTitle(Dirty ? "XUI Designer - unsaved changes" : "XUI Designer");
        if (!live && !immediate)
        {
            view.Status.Text = "Live preview paused. Select Render to compile.";
            return;
        }
        view.Status.Text = "Compiling...";
        if (!edits.Writer.TryWrite((version, editor.Text)))
            throw new InvalidOperationException("The compiler queue is closed.");
    }

    private async Task CompileEdits()
    {
        try
        {
            await foreach (var edit in edits.Reader.ReadAllAsync(lifetime.Token))
            {
                using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
                // Cancellation is requested on the UI thread, but compilation stays serial.
                var accepted = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    if (edit.Version != version) { accepted.SetResult(false); return; }
                    revision = cancellation;
                    accepted.SetResult(true);
                })) return;
                if (!await accepted.Task.WaitAsync(lifetime.Token)) continue;
                try
                {
                    await Task.Delay(300, cancellation.Token);
                    var result = PreviewCompiler.Compile(edit.Source, cancellation.Token);
                    window.Post(() =>
                    {
                        if (edit.Version != version || disposed || edit.Source != editor.Text) return;
                        diagnostics.Text = Limit(result.Diagnostics);
                        diagnosticNavigator.Publish(edit.Version, edit.Source);
                        if (!result.Success)
                        {
                            view.Status.Text = "Source has errors. The last valid preview is unchanged.";
                            SmokeCompileError();
                            return;
                        }
                        preview.Publish(edit.Version, result.Assembly!, light ? Theme.Light : Theme.Dark);
                    });
                }
                catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { }
                finally
                {
                    // Clear the UI-owned token before this iteration disposes it.
                    var cleared = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                    if (window.Post(() => { if (ReferenceEquals(revision, cancellation)) revision = null; cleared.SetResult(); }))
                        await cleared.Task.WaitAsync(lifetime.Token);
                }
            }
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested) { }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            window.Post(() => ShowError($"Compiler worker failed: {error.Message}"));
        }
    }

    private void OnPreview(long value, string message, bool success)
    {
        if (value != version || disposed) return;
        view.Status.Text = message;
        if (!success)
        {
            if (!message.StartsWith("Preview closed.", StringComparison.Ordinal)) ShowError(message);
            if (smokeStage == 3 && message.StartsWith("Preview construction failed:", StringComparison.Ordinal))
            {
                smokeStage = 4;
                editor.Text = "component Fixed { view { Text(\"Recovered preview\"); } }";
                Schedule(immediate: true);
            }
            return;
        }
        if (smokeStage == 1)
        {
            if (editor.GetBounds().Height < 100 || diagnostics.GetBounds().Width < 100)
            {
                smokeError = new InvalidOperationException("The source editor or diagnostics pane has no usable layout.");
                window.Close();
                return;
            }
            smokeStage = 2;
            editor.Text = "component Broken { view { VStack() { Text(\"missing terminator\") } } }";
            Schedule(immediate: true);
        }
        else if (smokeStage == 4)
        {
            smokeStage = 5;
            FileSmoke();
            window.Close();
        }
    }

    private void SmokeCompileError()
    {
        if (smokeStage != 2) return;
        if (!diagnostics.Text.Contains("XUI001", StringComparison.Ordinal))
        {
            smokeError = new InvalidOperationException("The designer did not display the XUI diagnostic.");
            window.Close();
            return;
        }
        smokeStage = 3;
        editor.Text = "component Throws { state int Value = int.Parse(\"bad\"); view { Text($\"{Value}\"); } }";
        Schedule(immediate: true);
    }

    private void Open()
    {
        try
        {
            document.UpdateSource(editor.Text);
            document.Open(view.Path.Text);
            editor.Text = document.Source;
            document.UpdateSource(editor.Text);
            view.Path.Text = document.FilePath!;
            SetFileStatus($"Opened {document.FilePath}");
            Schedule();
        }
        catch (Exception error) when (FileError(error)) { ReportFileError($"Open failed: {error.Message}"); }
    }

    private void NewDocument()
    {
        if (templateIndex < 0 || templateIndex >= DesignerTemplates.All.Count)
        {
            ReportFileError("Select a document template first.");
            return;
        }
        try
        {
            document.UpdateSource(editor.Text);
            var template = DesignerTemplates.All[templateIndex];
            document.New(template.Source);
            editor.Text = document.Source;
            document.UpdateSource(editor.Text);
            view.Path.Text = "";
            SetFileStatus($"New {template.Name}. Choose a file path before saving.");
            Schedule();
        }
        catch (Exception error) when (FileError(error)) { ReportFileError($"New document failed: {error.Message}"); }
    }

    private void RecoveredDocument()
    {
        editor.Text = document.Source;
        document.UpdateSource(editor.Text);
        view.Path.Text = document.FilePath ?? "";
        SetFileStatus("Recovered a copy. The original recovery draft remains available.");
        PersistDraft();
        Schedule();
    }

    private void SourceCommand(TextCommand command)
    {
        editor.Focus();
        editor.Command(command);
    }

    private void Save()
    {
        try
        {
            document.UpdateSource(editor.Text);
            document.Save(view.Path.Text);
            view.Path.Text = document.FilePath!;
            window.SetTitle("XUI Designer");
            SetFileStatus($"Saved {document.FilePath}");
        }
        catch (Exception error) when (FileError(error))
        {
            window.SetTitle(Dirty ? "XUI Designer - unsaved changes" : "XUI Designer");
            ReportFileError($"Save failed: {error.Message}");
        }
    }

    private static bool FileError(Exception error) => error is IOException or InvalidDataException or UnauthorizedAccessException
        or ArgumentException or NotSupportedException or InvalidOperationException or System.Security.SecurityException;
    private static string Limit(string message)
    {
        message = Encoding.UTF8.GetString(Encoding.UTF8.GetBytes(message)).Replace("\0", "\\0", StringComparison.Ordinal);
        if (message.Length <= MaximumLength) return message;
        int end = MaximumLength - 32;
        if (char.IsHighSurrogate(message[end - 1])) end--;
        return message[..end] + "\nDiagnostics truncated.";
    }

    private void FileSmoke()
    {
        var directory = Path.Combine(Path.GetTempPath(), "XuiDesignerSmoke-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        var first = Path.Combine(directory, "first.xui");
        var other = Path.Combine(directory, "other.xui");
        try
        {
            view.Path.Text = first;
            Save();
            Require(File.ReadAllText(first) == Normalize(editor.Text) && !Dirty, "Save writes the source and clears dirty state.");
            const string external = "component External { view { Text(\"External source\"); } }";
            File.WriteAllText(other, external);
            editor.Text = "component Edited { view { Text(\"Unsaved source\"); } }";
            view.Path.Text = other;
            Open();
            Require(editor.Text.Contains("Unsaved source"), "Open keeps unsaved edits.");
            Save();
            Require(File.ReadAllText(other) == external, "Save does not overwrite another document.");
            view.Path.Text = first;
            File.WriteAllText(first, external);
            Save();
            Require(File.ReadAllText(first) == external, "Save detects external changes.");
            File.Delete(first);
            Save();
            Require(!Dirty && File.ReadAllText(first).Contains("Unsaved source"), "Save can recreate a deleted destination.");
            view.Path.Text = other;
            Open();
            Require(!Dirty && Normalize(editor.Text) == external, "Open loads a clean document.");
            File.WriteAllText(other, new string('x', MaximumLength + 1));
            Open();
            Require(Normalize(editor.Text) == external && view.FileStatus.Text.Contains("65,536"), "Open rejects oversized source without losing the document.");
            File.WriteAllBytes(other, [0xEF, 0xBB, 0xBF, 0xFF]);
            Open();
            Require(Normalize(editor.Text) == external && view.FileStatus.Text.StartsWith("Open failed:"), "Open rejects invalid UTF-8.");
            File.WriteAllText(other, external, new UTF8Encoding(true));
            Open();
            Require(Normalize(editor.Text) == external, "Open accepts the UTF-8 byte-order mark.");
            Require(!Limit("bad\0message\uD800").Contains('\0'), "Diagnostic text is safe for native documents.");
        }
        finally
        {
            File.Delete(first);
            File.Delete(other);
            Directory.Delete(directory);
        }

        static void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException("Designer file smoke: " + message);
        }
    }
    private void ShowError(string message)
    {
        diagnostics.Text = Limit(message);
        diagnosticNavigator.Invalidate();
        view.Status.Text = "Error. See diagnostics.";
        Console.Error.WriteLine(message);
    }

    private void ReportNavigation(string message) => view.Status.Text = Limit(message);

    private void ReportFileError(string message)
    {
        SetFileStatus(message);
        Console.Error.WriteLine(message);
    }

    private void SetFileStatus(string message)
    {
        string safe = Limit(message);
        int length = Math.Min(safe.Length, 512);
        if (length > 0 && char.IsHighSurrogate(safe[length - 1])) length--;
        view.FileStatus.Text = safe[..length] + (length < safe.Length ? "..." : "");
    }

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        if (Dirty) PersistDraft();
        lifetime.Cancel();
        edits.Writer.TryComplete();
        compiler.GetAwaiter().GetResult();
        workspace.Dispose();
        preview.Dispose();
        window.Dispose();
        lifetime.Dispose();
    }
}
