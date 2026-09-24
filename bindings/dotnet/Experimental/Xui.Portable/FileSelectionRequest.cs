namespace Xui.Experimental.Portable;

/// <summary>One-shot ownership transfer for asynchronous native picker callbacks.</summary>
/// <remarks>
/// A platform adapter must also release its own dialog/listeners/permission handles.
/// Canceling this request does not claim to dismiss operating-system UI.
/// Every completed file passed to TryComplete is transferred or disposed, including late results.
/// Cleanup failures after cancellation are sent to the required error reporter.
/// </remarks>
public sealed class FileSelectionRequest : IDisposable
{
    private readonly TaskCompletionSource<OperationResult<PickedFile>> completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly Action<Exception> reportCleanupError;
    private readonly CancellationToken cancellationToken;
    private readonly CancellationTokenRegistration registration;
    public Task<OperationResult<PickedFile>> Task => completion.Task;

    public FileSelectionRequest(CancellationToken cancellationToken, Action<Exception> reportCleanupError)
    {
        ArgumentNullException.ThrowIfNull(reportCleanupError);
        this.reportCleanupError = reportCleanupError;
        this.cancellationToken = cancellationToken;
        registration = cancellationToken.UnsafeRegister(static state => ((FileSelectionRequest)state!).Cancel(), this);
        if (completion.Task.IsCompleted) registration.Unregister();
    }

    private void Cancel()
    {
        completion.TrySetCanceled(cancellationToken);
        registration.Unregister();
    }

    public bool TryComplete(OperationResult<PickedFile> result)
    {
        ArgumentNullException.ThrowIfNull(result);
        if (completion.TrySetResult(result))
        {
            registration.Unregister();
            return true;
        }
        if (result.Status == OperationStatus.Completed)
        {
            if (completion.Task.IsCompletedSuccessfully &&
                completion.Task.Result.Status == OperationStatus.Completed &&
                ReferenceEquals(completion.Task.Result.Value, result.Value)) return false;
            try { result.Value.Dispose(); }
            catch (Exception error) { reportCleanupError(error); }
        }
        else if (result.Status == OperationStatus.Failed &&
            (!completion.Task.IsCompletedSuccessfully || !ReferenceEquals(completion.Task.Result, result)))
        {
            reportCleanupError(result.Error!);
        }
        return false;
    }

    public void Dispose()
    {
        completion.TrySetCanceled(cancellationToken.IsCancellationRequested ? cancellationToken : new CancellationToken(canceled: true));
        registration.Unregister();
    }
}
