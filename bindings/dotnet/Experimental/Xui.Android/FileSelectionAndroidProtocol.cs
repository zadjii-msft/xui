using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

internal interface IAndroidFilePickerBridge
{
    bool CheckAccess();
    bool IsAvailable { get; }
    void Post(Action action, Action<Exception> canceled);
    void Launch();
}

internal sealed class FileSelectionAndroidProtocol(IAndroidFilePickerBridge bridge, Action<Exception> reportError)
    : IFilePicker, IDisposable
{
    private FileSelectionRequest? pending;
    private FileSelectionOptions? options;
    private bool launched;
    private bool disposed;

    private void VerifyAccess()
    {
        if (!bridge.CheckAccess()) throw new InvalidOperationException("Android file pickers require the UI thread.");
    }

    public CapabilityAvailability GetAvailability(ServiceCapability capability)
    {
        PlatformServicePolicy.ValidateCapability(capability);
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        return capability == ServiceCapability.OpenFile && bridge.IsAvailable
            ? CapabilityAvailability.Available : CapabilityAvailability.Unsupported;
    }

    public Task<OperationResult<PickedFile>> OpenAsync(FileSelectionOptions selection, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(selection);
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<OperationResult<PickedFile>>(cancellationToken);
        if (pending is not null)
            return Task.FromResult(OperationResult<PickedFile>.Failed(new InvalidOperationException("The previous native picker has not returned.")));
        if (!bridge.IsAvailable)
            return Task.FromResult(OperationResult<PickedFile>.Failed(new InvalidOperationException("A live focused Activity is required.")));
        var request = new FileSelectionRequest(cancellationToken, reportError);
        pending = request;
        options = selection;
        try
        {
            bridge.Post(() =>
            {
                if (!ReferenceEquals(pending, request)) return;
                if (request.Task.IsCompleted || disposed) { Finish(OperationResult<PickedFile>.Cancelled()); return; }
                try
                {
                    if (!bridge.IsAvailable) throw new InvalidOperationException("The Activity is no longer available.");
                    launched = true;
                    bridge.Launch();
                }
                catch (UnauthorizedAccessException) { Finish(OperationResult<PickedFile>.Denied()); }
                catch (NotSupportedException) { Finish(OperationResult<PickedFile>.Unsupported()); }
                catch (Exception error) { Finish(OperationResult<PickedFile>.Failed(error)); }
            }, error =>
            {
                if (ReferenceEquals(pending, request)) Finish(OperationResult<PickedFile>.Failed(error));
                else reportError(error);
            });
        }
        catch (Exception error) { Finish(OperationResult<PickedFile>.Failed(error)); }
        return request.Task;
    }

    public bool HandleResult(Func<FileSelectionOptions, OperationResult<PickedFile>> receive)
    {
        ArgumentNullException.ThrowIfNull(receive);
        VerifyAccess();
        if (pending is null || !launched) return false;
        if (pending.Task.IsCompleted || disposed)
        {
            Finish(OperationResult<PickedFile>.Cancelled());
            return true;
        }
        OperationResult<PickedFile> result;
        try { result = receive(options!); }
        catch (UnauthorizedAccessException) { result = OperationResult<PickedFile>.Denied(); }
        catch (NotSupportedException) { result = OperationResult<PickedFile>.Unsupported(); }
        catch (Exception error) { result = OperationResult<PickedFile>.Failed(error); }
        Finish(result);
        return true;
    }

    private void Finish(OperationResult<PickedFile> result)
    {
        var request = pending!;
        pending = null;
        options = null;
        launched = false;
        try { request.TryComplete(result); }
        finally { request.Dispose(); }
    }

    public void Dispose()
    {
        VerifyAccess();
        if (disposed) return;
        disposed = true;
        pending?.Dispose();
        // A launched request retains its slot until the matching Activity result is consumed.
        if (!launched && pending is not null) Finish(OperationResult<PickedFile>.Cancelled());
    }
}
