using System.ComponentModel;
using System.Diagnostics;
using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class PreviewSession : IDisposable
{
    private readonly Application application;
    private readonly UiWork work;
    private readonly bool smoke;
    private readonly Action opened;
    private readonly PreviewLayout layout;
    private readonly Grid body;
    private readonly PreviewMetadataLayout metadata;
    private readonly VectorCanvas metadataIcon;
    private readonly InlineStatus status;
    private readonly FilePreviewService service = new();
    private readonly CancellationTokenSource request = new();
    private bool disposed;

    public PreviewSession(Application application, FileEntry target, bool smoke, Action opened)
    {
        this.application = application;
        this.smoke = smoke;
        this.opened = opened;
        Target = target;
        Window = application.CreateWindow($"Preview: {target.Name}", 800, 600,
            customTitlebar: true, visualStyle: VisualStyle.WinUI);
        Window.SetFileTypeIcon(directory: target.IsDirectory);
        Window.TitlebarTabs.Visible(false);
        Window.TitlebarSecondaryTabs.Visible(false);
        OpenButton = Window.TitlebarLeading;
        CloseButton = Window.TitlebarClose;
        OpenButton.SetText(target.IsDirectory ? "Open folder" : "Open")
            .SetIcon(ButtonIcon.Open).SetAutomationId("preview-open").Help("Open in the associated application");
        OpenButton.SetStyle(ExplorerStyles.IconButton);
        OpenButton.Click += Open;
        work = new(Window);
        body = Window.Grid("Preview content");
        Text = Window.MultilineText("File contents");
        Text.SetReadOnly(true).SetMaximumLength(FilePreviewService.MaximumTextLength);
        Text.SetControlStyle(ExplorerStyles.PreviewText);
        Text.SetAutomationId("preview-text").Visible(false);
        Image = Window.Image("Image preview");
        Image.SetControlStyle(ExplorerStyles.PreviewImage);
        Image.SetAutomationId("preview-image").Visible(false);
        body.Add(Text);
        body.Add(Image);
        metadataIcon = Window.VectorCanvas("Generic file icon");
        metadataIcon.SetAutomationId("preview-file-icon").PreferredSize(160, 160);
        metadataIcon.SetScene(target.IsDirectory
            ? [new(1, [new(12, 42), new(60, 42), new(72, 55), new(148, 55), new(148, 130), new(12, 130)],
                "Generic folder", Closed: true, Fill: new(0.84f, 0.65f, 0.24f), Stroke: new(0.35f, 0.3f, 0.16f), StrokeWidth: 2)]
            : [new(1, [new(35, 12), new(100, 12), new(130, 42), new(130, 148), new(35, 148)],
                "Generic file", Closed: true, Fill: new(0.7f, 0.76f, 0.82f), Stroke: new(0.3f, 0.38f, 0.46f), StrokeWidth: 2),
               new(2, [new(100, 12), new(100, 42), new(130, 42)], Stroke: new(0.3f, 0.38f, 0.46f), StrokeWidth: 2),
               new(3, [new(53, 72), new(111, 72)], Stroke: new(0.3f, 0.38f, 0.46f), StrokeWidth: 3),
               new(4, [new(53, 92), new(111, 92)], Stroke: new(0.3f, 0.38f, 0.46f), StrokeWidth: 3)]);
        metadata = new(Window, metadataIcon, attach: false);
        body.Add(metadata.Root);
        ShowMetadata(false);
        status = Window.InlineStatus("Preview status");
        status.SetAutomationId("preview-status");
        status.SetDismissible(false);
        layout = new(Window, body, status);
        Window.KeyHandler = HandleKey;
        Window.Closed += e =>
        {
            IsOpen = false;
            Pending = false;
            request.Cancel();
            work.Dispose();
            if (!application.Post(Dispose))
                throw new InvalidOperationException("The application rejected preview retirement.");
        };
    }

    public Window Window { get; }
    public FileEntry Target { get; }
    public bool IsOpen { get; private set; }
    internal bool IsDisposed => disposed;
    internal bool Pending { get; private set; }
    internal string Message { get; private set; } = "";
    internal MultilineText Text { get; }
    internal Image Image { get; }
    internal Button CloseButton { get; }
    internal Button OpenButton { get; }
    internal ElementBounds Bounds => layout.Root.GetBounds();
    internal ElementBounds BodyBounds => body.GetBounds();
    internal ElementBounds StatusBounds => status.GetBounds();
    internal VectorCanvas MetadataIcon => metadataIcon;
    internal string MetadataName => metadata.Name.Text;
    internal string MetadataKind => metadata.Kind.Text;
    internal string MetadataSize => metadata.Size.Text;
    internal ElementBounds MetadataNameBounds => metadata.Name.GetBounds();
    internal bool StatusVisible { get; private set; }

    public void Show()
    {
        var selected = Target;
        metadata.Name.Text = selected.Name;
        metadata.Kind.Text = $"File Type: {(selected.IsDirectory ? "File folder" : selected.Kind)}";
        metadata.Size.Text = selected.IsDirectory ? "Size: Not calculated"
            : $"Size: {FileRows.FormatSize(selected.Size)} ({selected.Size:N0} bytes)";
        metadata.Modified.Text = $"Date Modified: {selected.ModifiedUtc.ToLocalTime():g}";
        Text.SetName($"Contents of {selected.Name}").Visible(false);
        Image.SetName($"Preview of {selected.Name}").Visible(false);
        ShowMetadata(false);
        OpenButton.Enabled = false;
        SetMessage("Loading preview...");
        Pending = true;
        application.Show(Window);
        IsOpen = true;
        work.Start(token => service.LoadAsync(selected, token), request.Token, result =>
        {
            if (!IsOpen) return;
            Pending = false;
            OpenButton.Enabled = true;
            if (!result.Restricted && !selected.IsDirectory)
                Window.SetFileTypeIcon(Path.GetExtension(selected.Name));
            SetMessage(result.Message);
            if (result.Kind == FilePreviewKind.Image)
                Image.Source(selected.FullPath, 1024, 1024).Visible(true);
            else if (result.Metadata is not null)
            {
                ShowMetadata(true);
            }
            else
            {
                Text.Text = result.Text;
                Text.Visible(true);
                if (result.Kind == FilePreviewKind.Text && MultilineText.SyntaxHighlightingAvailable)
                {
                    try { Text.SetSyntaxPath(selected.Name); }
                    catch (XuiException error)
                    {
                        SetMessage($"Syntax highlighting failed: {error.Message}", StatusSeverity.Error);
                    }
                }
            }
        }, error =>
        {
            if (!IsOpen) return;
            Pending = false;
            OpenButton.Enabled = true;
            SetMessage($"Cannot preview this item: {error.Message}", StatusSeverity.Error);
        });
    }

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
        if (Pending || !IsOpen) return;
        try
        {
            if (!smoke)
            {
                using var process = Process.Start(new ProcessStartInfo(Target.FullPath) { UseShellExecute = true });
            }
            opened();
            Dismiss();
        }
        catch (Exception error) when (error is Win32Exception or InvalidOperationException
            || UiWork.IsExpected(error))
        {
            SetMessage($"Cannot open this item: {error.Message}", StatusSeverity.Error);
        }
    }

    public bool HandleKey(UiKeyEvent key)
    {
        if (key.VirtualKey == 0x1b) { Dismiss(); return true; }
        // Held Space from Explorer must not activate the initial caption focus.
        if (key.VirtualKey == 0x20 && key.Modifiers == KeyModifiers.None
            && (key.TargetId == CloseButton.Id || key.TargetId == OpenButton.Id)) return true;
        return false;
    }

    public void Dismiss()
    {
        if (!IsOpen) return;
        request.Cancel();
        Pending = false;
        work.Dispose();
        Window.Close();
    }

    public void Dispose()
    {
        if (disposed) return;
        work.Dispose();
        request.Cancel();
        request.Dispose();
        Window.Dispose();
        disposed = true;
    }
}
