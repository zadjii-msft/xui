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
    private readonly PreviewMetadataLayout metadata;
    private readonly Image metadataIcon;
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
        Window = application.CreateWindow($"Preview: {target.Name}", 800, 600, visualStyle: VisualStyle.WinUI);
        work = new(Window);
        var body = Window.Grid("Preview content");
        Text = Window.MultilineText("File contents");
        Text.SetReadOnly(true).SetMaximumLength(FilePreviewService.MaximumTextLength);
        Text.SetControlStyle(ExplorerStyles.PreviewText);
        Text.SetAutomationId("preview-text").Visible(false);
        Image = Window.Image("Image preview");
        Image.SetControlStyle(ExplorerStyles.PreviewImage);
        Image.SetAutomationId("preview-image").Visible(false);
        body.Add(Text);
        body.Add(Image);
        metadataIcon = Window.Image("File icon");
        metadataIcon.SetAutomationId("preview-file-icon").SetControlStyle(ExplorerStyles.PreviewImage).PreferredSize(160, 160);
        metadata = new(Window, metadataIcon, attach: false);
        body.Add(metadata.Root);
        ShowMetadata(false);
        status = Window.InlineStatus("Preview status");
        status.SetAutomationId("preview-status");
        status.SetDismissible(false);
        layout = new(Window, body, status);
        layout.Open.SetStyle(ExplorerStyles.IconButton);
        layout.Close.SetStyle(ExplorerStyles.IconButton);
        layout.Close.Click += Dismiss;
        layout.Open.Click += Open;
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
    internal Button CloseButton => layout.Close;
    internal Button OpenButton => layout.Open;
    internal ElementBounds Bounds => layout.Root.GetBounds();
    internal Image MetadataIcon => metadataIcon;
    internal string MetadataName => metadata.Name.Text;
    internal string MetadataKind => metadata.Kind.Text;
    internal string MetadataSize => metadata.Size.Text;
    internal ElementBounds MetadataNameBounds => metadata.Name.GetBounds();
    internal bool StatusVisible { get; private set; }

    public void Show()
    {
        var selected = Target;
        layout.Title.Text = selected.Name;
        Text.SetName($"Contents of {selected.Name}").Visible(false);
        Image.SetName($"Preview of {selected.Name}").Visible(false);
        ShowMetadata(false);
        layout.Open.Text = selected.IsDirectory ? "Open folder" : "Open";
        layout.Open.Enabled = false;
        SetMessage("Loading preview...");
        Pending = true;
        application.Show(Window);
        IsOpen = true;
        layout.Close.Focus();
        work.Start(token => service.LoadAsync(selected, token), request.Token, result =>
        {
            if (!IsOpen) return;
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
            if (!IsOpen) return;
            Pending = false;
            layout.Open.Enabled = true;
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
        // Space opens only. Do not let held-key repeats activate the focused Close button.
        if (key.VirtualKey == 0x20 && key.Modifiers == KeyModifiers.None
            && (key.TargetId == layout.Close.Id || key.TargetId == layout.Open.Id)) return true;
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
