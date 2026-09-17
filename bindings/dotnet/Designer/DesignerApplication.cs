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
    private readonly ComboBox templates;
    private readonly Task compiler;
    private readonly string recoveryPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Xui", "Designer", "Drafts", $"{Guid.NewGuid():N}.xui");
    private CancellationTokenSource? revision;
    private string savedSource = "";
    private string loadedPath = "";
    private long version;
    private bool live = true, light, disposed;
    private int templateIndex;
    private int smokeStage;
    private Exception? smokeError;

    internal DesignerApplication(string? initialPath)
    {
        try
        {
            editor = window.MultilineText("XUI source").SetMaximumLength(MaximumLength);
            editor.SetControlStyleValues(StylePart.Text, new PartStyleValues { FontFamily = "Consolas", FontSize = 14 });
            diagnostics = window.MultilineText("Compiler diagnostics").SetReadOnly(true).SetMaximumLength(MaximumLength);
            preview = new PreviewHost(window, (value, message, success) =>
                window.Post(() => OnPreview(value, message, success)));
            workspace = new DesignerWorkspace(window, editor, ShowError);
            templates = window.ComboBox("New document template", false).SetAutomationId("designer-templates");
            templates.SetItems(DesignerTemplates.All.Select((template, index) => new Choice((ulong)index + 1, template.Name)).ToArray(), 1);
            templates.Event += e => { if (e.Kind == EventKind.Selection) templateIndex = checked((int)e.Value - 1); };
            view = new DesignerLayout(window, editor, diagnostics, workspace.Hierarchy.Layout.Root,
                workspace.Inspector.Layout.Root, preview.View, templates);
            using var stream = typeof(DesignerApplication).Assembly.GetManifestResourceStream("Designer.Starter.xui")
                ?? throw new InvalidOperationException("The starter component is missing.");
            using var reader = new StreamReader(stream);
            editor.Text = initialPath is null ? reader.ReadToEnd() : ReadSource(initialPath);
            savedSource = Normalize(editor.Text);
            if (initialPath is not null)
            {
                loadedPath = Path.GetFullPath(initialPath);
                view.Path.Text = loadedPath;
                savedSource = Normalize(editor.Text);
            }
            editor.Event += OnEditorEvent;
            view.Open.Click += Open;
            view.Save.Click += Save;
            view.New.Click += NewDocument;
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

    internal void Run(bool smoke, bool builderSmoke = false)
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
        Task? builder = builderSmoke ? Task.Run(() => DesignerBuilderSmoke.Run(window, editor, diagnostics, workspace, view)) : null;
        window.Post(() => { workspace.SourceChanged(); Schedule(immediate: true); });
        window.Run();
        builder?.GetAwaiter().GetResult();
        if (smokeError is not null) throw smokeError;
    }

    private static string Normalize(string text) => text.Replace("\r\n", "\n").Replace('\r', '\n');
    private bool Dirty => Normalize(editor.Text) != savedSource;

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
            if (!Dirty)
            {
                File.Delete(recoveryPath);
                return;
            }
            Directory.CreateDirectory(Path.GetDirectoryName(recoveryPath)!);
            WriteSource(recoveryPath, Normalize(editor.Text));
        }
        catch (Exception error) when (FileError(error))
        {
            ShowError($"Could not save recovery draft: {error.Message}");
        }
    }

    private void Schedule(bool immediate = false)
    {
        workspace.SourceChanged();
        revision?.Cancel();
        version++;
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
                        if (edit.Version != version || disposed) return;
                        diagnostics.Text = Limit(result.Diagnostics);
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
        if (Dirty) { ShowError("Save the current source before opening another file."); return; }
        try
        {
            var path = Path.GetFullPath(view.Path.Text);
            var source = ReadSource(path);
            editor.Text = source;
            loadedPath = path;
            savedSource = Normalize(editor.Text);
            view.Path.Text = path;
            Schedule();
        }
        catch (Exception error) when (FileError(error)) { ShowError($"Open failed: {error.Message}"); }
    }

    private void NewDocument()
    {
        if (Dirty) { ShowError("Save the current source before creating a new document."); return; }
        if (templateIndex < 0 || templateIndex >= DesignerTemplates.All.Count)
        {
            ShowError("Select a document template first.");
            return;
        }
        editor.Text = DesignerTemplates.All[templateIndex].Source;
        loadedPath = "";
        savedSource = "";
        view.Path.Text = "";
        workspace.SourceChanged();
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
            if (string.IsNullOrWhiteSpace(view.Path.Text))
                throw new ArgumentException("Enter a .xui file path before saving.");
            var path = Path.GetFullPath(view.Path.Text);
            RequireExtension(path);
            var source = Normalize(editor.Text);
            bool exists = File.Exists(path);
            if (exists)
            {
                if (!StringComparer.OrdinalIgnoreCase.Equals(path, loadedPath))
                    throw new IOException("This file already exists. Choose a new path to avoid overwriting it.");
                if (Normalize(ReadSource(path)) != savedSource)
                    throw new IOException("The file changed on disk. Save to a different path to keep both versions.");
            }
            WriteSource(path, source, overwrite: exists);
            loadedPath = path;
            savedSource = source;
            view.Path.Text = path;
            window.SetTitle("XUI Designer");
            view.Status.Text = $"Saved {path}";
            File.Delete(recoveryPath);
        }
        catch (Exception error) when (FileError(error)) { ShowError($"Save failed: {error.Message}"); }
    }

    private static string ReadSource(string path)
    {
        RequireExtension(path);
        using var reader = new StreamReader(path, new UTF8Encoding(true, true), detectEncodingFromByteOrderMarks: false);
        var buffer = new char[MaximumLength + 1];
        int count = reader.ReadBlock(buffer, 0, buffer.Length);
        if (count > MaximumLength) throw new InvalidDataException("Source exceeds 65,536 UTF-16 code units.");
        var source = new string(buffer, 0, count);
        if (source.Contains('\0')) throw new InvalidDataException("Source contains a NUL character.");
        _ = new UTF8Encoding(false, true).GetByteCount(source);
        return source;
    }

    private static void RequireExtension(string path)
    {
        if (!Path.GetExtension(path).Equals(".xui", StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("Choose a file with the .xui extension.");
    }

    private static void WriteSource(string path, string source, bool overwrite = true)
    {
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temporary, source, new UTF8Encoding(false, true));
            File.Move(temporary, path, overwrite);
        }
        finally { File.Delete(temporary); }
    }

    private static bool FileError(Exception error) => error is IOException or InvalidDataException or UnauthorizedAccessException
        or ArgumentException or NotSupportedException or System.Security.SecurityException;
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
            Require(Normalize(editor.Text) == external && diagnostics.Text.Contains("65,536"), "Open rejects oversized source without losing the document.");
            File.WriteAllBytes(other, [0xEF, 0xBB, 0xBF, 0xFF]);
            Open();
            Require(Normalize(editor.Text) == external && diagnostics.Text.StartsWith("Open failed:"), "Open rejects invalid UTF-8.");
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
        view.Status.Text = "Error. See diagnostics.";
        Console.Error.WriteLine(message);
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
