namespace Xui.Designer;

internal sealed class DesignerFileActions
{
    private readonly Window window;
    private readonly MultilineText editor;
    private readonly TextInput path;
    private readonly DesignerDocumentStore document;
    private readonly Func<long> version;
    private readonly Action<string> replaced, status, report;
    private bool pickerActive;

    internal DesignerDiscardDialog Discard { get; }

    internal DesignerFileActions(Window window, MultilineText editor, TextInput path, DesignerDocumentStore document,
        Func<long> version, Action<string> replaced, Action<string> status, Action<string> report)
    {
        this.window = window;
        this.editor = editor;
        this.path = path;
        this.document = document;
        this.version = version;
        this.replaced = replaced;
        this.status = status;
        this.report = report;
        Discard = new(window, version, () => editor.Text, report);
    }

    internal void New(Control anchor, DesignerTemplate template) =>
        Replace(anchor, "New document", $"create a new {template.Name} document", approved =>
        {
            document.UpdateSource(editor.Text);
            document.New(template.Source, discardChanges: approved);
            replaced($"New {template.Name}. Choose a file path before saving.");
        });

    internal void OpenPath(Control anchor)
    {
        string selected = path.Text;
        Replace(anchor, "Open", "open another file", approved => Open(selected, approved));
    }

    internal void OpenChooser(Control anchor) =>
        Replace(anchor, "Open", "open another file", approved =>
        {
            Choose(save: false, selected => Open(selected, approved));
        });

    internal void Save()
    {
        if (string.IsNullOrWhiteSpace(path.Text)) { SaveAs(); return; }
        Run("Save", () => { if (CanStart()) SaveTo(path.Text); });
    }

    internal void SaveAs() => Run("Save", () =>
    {
        if (!CanStart()) return;
        Choose(save: true, SaveTo);
    });

    private void Replace(Control anchor, string operation, string description, Action<bool> action) =>
        Run(operation, () =>
        {
            if (!CanStart()) return;
            document.UpdateSource(editor.Text);
            if (document.IsDirty)
                Discard.Show(anchor, description, () => Run(operation, () => action(true)));
            else action(false);
        });

    private bool CanStart()
    {
        if (!pickerActive && !Discard.IsPending) return true;
        report("Finish or cancel the current file action first.");
        return false;
    }

    private void Open(string selected, bool approved)
    {
        document.UpdateSource(editor.Text);
        document.Open(selected, discardChanges: approved);
        replaced($"Opened {document.FilePath}");
    }

    private void SaveTo(string selected)
    {
        string? previousPath = document.FilePath;
        document.UpdateSource(editor.Text);
        try
        {
            document.Save(selected);
            path.Text = document.FilePath!;
            status($"Saved {document.FilePath}");
        }
        catch
        {
            if (document.FilePath != previousPath) path.Text = document.FilePath ?? "";
            throw;
        }
    }

    private void Choose(bool save, Action<string> apply)
    {
        string? suggestion = string.IsNullOrWhiteSpace(path.Text) ? document.FilePath : Path.GetFullPath(path.Text);
        var options = new FileDialogOptions
        {
            Title = save ? "Save component as a new file" : "Open component",
            Filters = [new("XUI components", "*.xui"), new("All files", "*.*")],
            DefaultExtension = "xui",
            InitialDirectory = suggestion is null ? "" : Path.GetDirectoryName(suggestion) ?? "",
            SuggestedName = suggestion is null ? (save ? "Untitled.xui" : "") : Path.GetFileName(suggestion)
        };
        long expectedVersion = version();
        string expectedSource = editor.Text;
        pickerActive = true;
        bool posted = false;
        try
        {
            string? selected = save ? window.ShowSaveFileDialog(options) : window.ShowOpenFileDialog(options);
            posted = window.Post(() =>
            {
                try
                {
                    Run(save ? "Save" : "Open", () =>
                    {
                        if (selected is null)
                            status($"{(save ? "Save" : "Open")} canceled. The current document was kept.");
                        else if (version() != expectedVersion || editor.Text != expectedSource)
                            throw new InvalidOperationException("The source changed while the file chooser was open. Try the action again.");
                        else apply(selected);
                    });
                }
                finally { pickerActive = false; }
            });
            if (!posted) Console.WriteLine("File action canceled because the designer closed.");
        }
        finally { if (!posted) pickerActive = false; }
    }

    private void Run(string operation, Action action)
    {
        try { action(); }
        catch (XuiException error) when (error.Status == 11)
        {
            Console.Error.WriteLine($"{operation} canceled because the designer closed: {error.Message}");
        }
        catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException or
            ArgumentException or NotSupportedException or InvalidOperationException or System.Security.SecurityException or XuiException)
        {
            report($"{operation} failed: {error.Message}");
        }
    }
}
