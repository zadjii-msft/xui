using Xui.Experimental.Portable;

namespace Xui.Experimental.Windows;

/// <summary>A window-owned native single-file picker. Dispose before releasing its dispatcher.</summary>
/// <remarks>Cancellation rejects delivery but cannot dismiss an already-open native modal dialog.</remarks>
public sealed class WindowsFilePicker : IFilePicker, IDisposable
{
    private readonly IWindowsFilePickerBridge bridge;
    private readonly Action<Exception> reportError;
    private FileSelectionRequest? pending;
    private bool disposed;

    public WindowsFilePicker(WindowsDispatcher dispatcher, Action<Exception> reportError)
        : this(new WindowsFilePickerBridge(dispatcher), reportError) { }

    internal WindowsFilePicker(IWindowsFilePickerBridge bridge, Action<Exception> reportError)
    {
        this.bridge = bridge ?? throw new ArgumentNullException(nameof(bridge));
        this.reportError = reportError ?? throw new ArgumentNullException(nameof(reportError));
        VerifyAccess();
    }

    private void VerifyAccess()
    {
        if (!bridge.CheckAccess()) throw new InvalidOperationException("File pickers require their creating UI thread.");
    }

    public CapabilityAvailability GetAvailability(ServiceCapability capability)
    {
        PlatformServicePolicy.ValidateCapability(capability);
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        return capability == ServiceCapability.OpenFile && bridge.IsAvailable
            ? CapabilityAvailability.Available : CapabilityAvailability.Unsupported;
    }

    public Task<OperationResult<PickedFile>> OpenAsync(FileSelectionOptions options, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(options);
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<OperationResult<PickedFile>>(cancellationToken);
        if (pending is not null)
            return Task.FromResult(OperationResult<PickedFile>.Failed(new InvalidOperationException("A native picker is already pending.")));
        if (!bridge.IsAvailable)
            return Task.FromResult(OperationResult<PickedFile>.Failed(new ObjectDisposedException("The picker window is unavailable.")));
        var request = new FileSelectionRequest(cancellationToken, reportError);
        pending = request;
        void Finish(OperationResult<PickedFile> result)
        {
            try { request.TryComplete(result); }
            finally
            {
                if (ReferenceEquals(pending, request)) pending = null;
                request.Dispose();
            }
        }
        try
        {
            bridge.Post(() =>
            {
                Stream? stream = null;
                OperationResult<PickedFile> result;
                try
                {
                    if (request.Task.IsCompleted || disposed)
                    {
                        Finish(OperationResult<PickedFile>.Cancelled());
                        return;
                    }
                    if (!bridge.IsAvailable) throw new ObjectDisposedException("The picker window is unavailable.");
                    string? path = bridge.ChooseFile();
                    if (path is null || request.Task.IsCompleted || disposed)
                    {
                        Finish(OperationResult<PickedFile>.Cancelled());
                        return;
                    }
                    if (!bridge.IsAvailable) throw new ObjectDisposedException("The picker window closed during selection.");
                    stream = bridge.OpenRead(path);
                    var file = new PickedFile(Path.GetFileName(path), stream, options, stream.CanSeek ? stream.Length : null);
                    stream = null;
                    result = OperationResult<PickedFile>.Completed(file);
                }
                catch (UnauthorizedAccessException) { result = OperationResult<PickedFile>.Denied(); }
                catch (Exception error) { result = OperationResult<PickedFile>.Failed(error); }
                if (stream is not null)
                {
                    try { stream.Dispose(); }
                    catch (Exception cleanup)
                    {
                        result = OperationResult<PickedFile>.Failed(result.Error is null
                            ? cleanup : new AggregateException(result.Error, cleanup));
                    }
                }
                Finish(result);
            }, error => Finish(OperationResult<PickedFile>.Failed(error)));
        }
        catch (Exception error) { Finish(OperationResult<PickedFile>.Failed(error)); }
        return request.Task;
    }

    public void Dispose()
    {
        VerifyAccess();
        if (disposed) return;
        disposed = true;
        pending?.Dispose();
    }
}

internal interface IWindowsFilePickerBridge
{
    bool CheckAccess();
    bool IsAvailable { get; }
    void Post(Action action, Action<Exception> canceled);
    string? ChooseFile();
    Stream OpenRead(string path);
}

internal sealed class WindowsFilePickerBridge : IWindowsFilePickerBridge
{
    private readonly WindowsDispatcher dispatcher;

    public WindowsFilePickerBridge(WindowsDispatcher dispatcher)
    {
        this.dispatcher = dispatcher ?? throw new ArgumentNullException(nameof(dispatcher));
    }

    public bool CheckAccess() => dispatcher.CheckAccess();
    public bool IsAvailable => dispatcher.Window.State is not (WindowState.Closed or WindowState.Closing);
    public void Post(Action action, Action<Exception> canceled) => dispatcher.Post(action, canceled);
    public string? ChooseFile() => dispatcher.Window.ShowOpenFileDialog(new FileDialogOptions());
    public Stream OpenRead(string path) => new FileStream(path, FileMode.Open, FileAccess.Read,
        FileShare.Read | FileShare.Delete, 4096, FileOptions.Asynchronous | FileOptions.SequentialScan);
}
