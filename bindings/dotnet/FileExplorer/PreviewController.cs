using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class PreviewController : IDisposable
{
    private readonly ExplorerApplication app;
    private readonly PreviewLayout layout;
    private readonly PreviewMetadataLayout metadata;
    private readonly Image metadataIcon;
    private readonly InlineStatus status;
    private readonly FilePreviewService service = new();
    private CancellationTokenSource request = new();
    private FilePaneView? pane;
    private FileEntry? entry;
    private ulong tab;

    public PreviewController(ExplorerApplication app)
    {
        this.app = app;
        var body = app.Window.Grid("Preview content");
        Text = app.Window.MultilineText("File contents");
        Text.SetReadOnly(true).SetMaximumLength(FilePreviewService.MaximumTextLength);
        Text.SetControlStyle(ExplorerStyles.PreviewText);
        Text.SetAutomationId("preview-text").Visible(false);
        Image = app.Window.Image("Image preview");
        Image.SetControlStyle(ExplorerStyles.PreviewImage);
        Image.SetAutomationId("preview-image").Visible(false);
        body.Add(Text);
        body.Add(Image);
        metadataIcon = app.Window.Image("File icon");
        metadataIcon.SetAutomationId("preview-file-icon").SetControlStyle(ExplorerStyles.PreviewImage).PreferredSize(160, 160);
        metadata = new(app.Window, metadataIcon, attach: false);
        body.Add(metadata.Root);
        ShowMetadata(false);
        status = app.Window.InlineStatus("Preview status");
        status.SetAutomationId("preview-status");
        status.SetDismissible(false);
        layout = new(app.Window, body, status, attach: false);
        layout.Open.SetStyle(ExplorerStyles.IconButton);
        layout.Close.SetStyle(ExplorerStyles.IconButton);
        layout.Close.Click += Dismiss;
        layout.Open.Click += Open;
        layout.Root.Event += e =>
        {
            if (e.Kind != EventKind.Dismiss) return;
            CancelRequest();
            Image.Unload();
            metadataIcon.Unload();
            Text.Text = "";
            pane = null;
            entry = null;
        };
    }

    public bool IsOpen => layout.Root.IsOpen;
    internal bool Pending { get; private set; }
    internal string Message { get; private set; } = "";
    internal MultilineText Text { get; }
    internal Image Image { get; }
    internal Button CloseButton => layout.Close;
    internal Button OpenButton => layout.Open;
    internal ElementBounds Bounds => layout.Root.GetBounds();
    internal Image MetadataIcon => metadataIcon;
    internal string MetadataName => metadata.Name.Text;
    internal string MetadataKind => metadata.Kind.Text;
    internal string MetadataSize => metadata.Size.Text;
    internal ElementBounds MetadataNameBounds => metadata.Name.GetBounds();
    internal bool StatusVisible { get; private set; }

    public bool CanPreview(FilePaneView target) => target.HasCurrentRows && target.SelectedEntries.Length == 1;

    public void ShowSelected(FilePaneView target)
    {
        if (CanPreview(target)) Show(target, target.SelectedEntries[0]);
        else app.Report("Select one item to preview.");
    }

    public void Show(FilePaneView target, FileEntry selected)
    {
        if (!CanPreview(target) || target.SelectedEntries[0].FullPath != selected.FullPath)
        {
            app.Report("The selected item changed. Select one item to preview.");
            return;
        }
        Dismiss();
        app.Palettes.Dismiss();
        pane = target;
        entry = selected;
        tab = target.Model.Active.Id;
        layout.Title.Text = selected.Name;
        layout.Root.SetName($"Preview: {selected.Name}");
        Text.SetName($"Contents of {selected.Name}").Visible(false);
        Image.SetName($"Preview of {selected.Name}").Visible(false);
        ShowMetadata(false);
        layout.Open.Text = selected.IsDirectory ? "Open folder" : "Open";
        layout.Open.Enabled = false;
        SetMessage("Loading preview...");
        Pending = true;
        target.Focus();
        layout.Root.Show(target.IsColumns ? target.Columns.Column(target.Columns.ActiveColumn) : target.Grid);
        layout.Close.Focus();
        app.Work.Start(token => service.LoadAsync(selected, token), request.Token, result =>
        {
            if (!IsCurrent()) { Dismiss(); return; }
            Pending = false;
            layout.Open.Enabled = true;
            SetMessage(result.Message);
            if (result.Kind == FilePreviewKind.Image)
                Image.Source(selected.FullPath, 1024, 1024).Visible(true);
            else if (result.Metadata is { } file)
            {
                metadata.Name.Text = file.Name;
                metadata.Kind.Text = $"File Type: {(file.IsDirectory ? "File folder" : file.Kind)}";
                metadata.Size.Text = file.IsDirectory ? "Size: Not calculated"
                    : $"Size: {FileRows.FormatSize(file.Size)} ({file.Size:N0} bytes)";
                metadata.Modified.Text = $"Date Modified: {file.ModifiedUtc.ToLocalTime():g}";
                metadataIcon.SetName($"Icon for {file.Name}");
                metadataIcon.ShellSource(file.FullPath, 256, 256);
                ShowMetadata(true);
            }
            else
            {
                Text.Text = result.Text;
                Text.Visible(true);
            }
        }, error =>
        {
            if (!IsCurrent()) { Dismiss(); return; }
            Pending = false;
            layout.Open.Enabled = true;
            SetMessage($"Cannot preview this item: {error.Message}", StatusSeverity.Error);
        });
    }

    private bool IsCurrent() => IsOpen && pane is not null && pane.Model.Active.Id == tab
        && CanPreview(pane) && pane.SelectedEntries[0].FullPath == entry?.FullPath;

    private void SetMessage(string message, StatusSeverity severity = StatusSeverity.Information)
    {
        // InlineStatus has a smaller text limit than filesystem error messages.
        int length = Math.Min(message.Length, 4093);
        if (length > 0 && char.IsHighSurrogate(message[length - 1])) length--;
        Message = message.Length <= 4096 ? message : message[..length] + "...";
        status.SetMessage(Message, severity);
        StatusVisible = Message.Length != 0;
        status.Visible(StatusVisible);
    }

    private void ShowMetadata(bool visible)
    {
        metadataIcon.Visible(visible);
        foreach (var label in new[] { metadata.Name, metadata.Kind, metadata.Size, metadata.Modified })
            label.Visible(visible);
    }

    internal void Open()
    {
        if (Pending || !IsCurrent() || pane is not { } target || entry is not { } selected) return;
        Dismiss();
        app.Open(selected, target);
    }

    public bool HandleKey(UiKeyEvent key)
    {
        if (key.VirtualKey == 0x1b) { Dismiss(); return true; }
        // Space opens only. Do not let held-key repeats activate the focused Close button.
        if (key.VirtualKey == 0x20) return true;
        return false;
    }

    public void Dismiss()
    {
        if (IsOpen) layout.Root.Dismiss();
    }

    public void DismissFor(FilePaneView target)
    {
        if (ReferenceEquals(pane, target)) Dismiss();
    }

    private void CancelRequest()
    {
        request.Cancel();
        request.Dispose();
        request = new();
        Pending = false;
    }

    public void Dispose()
    {
        request.Cancel();
        request.Dispose();
    }
}
