namespace Xui.Experimental.Portable;

/// <summary>A backend attachment's native font loader, separate from encoded-byte caching.</summary>
/// <remarks>
/// Implementations must verify actual registered-face identity, collisions, supported native
/// input/rendering behavior, and thread ownership before returning Completed. Metadata preflight
/// alone is not success. Expected native denial, unsupported registration, and loading failures
/// use OperationResult; external cancellation cancels the task. Invalid arguments throw before
/// loading. A late native registration must be retired on its owning context, never dropped
/// through a canceled UI-delivery task. Dispose closes the scope to new uses but cannot revoke
/// existing bound pins. Callers retire native bindings and measurement caches before final unload.
/// This interface adds no implementation or platform support claim.
/// </remarks>
public interface IFontResourceService : IDisposable
{
    FontResourceScope Scope { get; }
    Task<OperationResult<FontResource>> LoadAsync(PackagedFontSource source, CancellationToken cancellationToken = default);
}

/// <summary>A failed native retirement whose owner remains available for an explicit UI-thread cleanup retry.</summary>
public sealed class FontResourceRetirementException : Exception
{
    public FontResource Resource { get; }
    public FontResourceRetirementException(FontResource resource, Exception error)
        : base("Native font retirement failed; the resource retains its encoded owner until cleanup succeeds.", error)
    {
        Resource = resource ?? throw new ArgumentNullException(nameof(resource));
        ArgumentNullException.ThrowIfNull(error);
    }
}

/// <summary>One service load's cancellation and late-result ownership transfer.</summary>
/// <remarks>
/// Construct, complete and dispose on the attachment UI thread. Cancellation may arrive from
/// another thread, but only settles the task; native cleanup still occurs when the producer
/// completes on its owning context. Observe producer failures even after this task is canceled.
/// A completion rejected before transfer (for example, a foreign-scope handle) remains producer-owned.
/// </remarks>
public sealed class FontResourceLoad : IDisposable
{
    private readonly FontResourceScope scope;
    private readonly Action<Exception> reportLateError;
    private readonly TaskCompletionSource<OperationResult<FontResource>> completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly CancellationTokenSource lifetime;
    private readonly CancellationTokenRegistration registration;
    private bool disposed;
    public CancellationToken Token { get; }
    public Task<OperationResult<FontResource>> Task => completion.Task;

    public FontResourceLoad(FontResourceScope scope, CancellationToken cancellationToken, Action<Exception> reportLateError)
    {
        this.scope = scope ?? throw new ArgumentNullException(nameof(scope));
        this.reportLateError = reportLateError ?? throw new ArgumentNullException(nameof(reportLateError));
        scope.VerifyOpen();
        lifetime = CancellationTokenSource.CreateLinkedTokenSource(scope.Token, cancellationToken);
        Token = lifetime.Token;
        registration = Token.UnsafeRegister(static state =>
        {
            var request = (FontResourceLoad)state!;
            request.completion.TrySetCanceled(request.Token);
        }, this);
    }

    public bool TryComplete(OperationResult<FontResource> result)
    {
        ArgumentNullException.ThrowIfNull(result);
        scope.VerifyAccess();
        if (result.Status == OperationStatus.Completed)
        {
            if (!ReferenceEquals(result.Value.Scope, scope))
                throw new ArgumentException("A font load cannot transfer a resource from another attachment.", nameof(result));
            if (!completion.Task.IsCompleted && !result.Value.AcceptsNewUses)
                throw new ObjectDisposedException(nameof(FontResource), "A load cannot publish a closed font resource.");
        }
        if (completion.TrySetResult(result))
        {
            registration.Unregister();
            return true;
        }
        if (result.Status == OperationStatus.Completed)
        {
            if (completion.Task.IsCompletedSuccessfully && completion.Task.Result.Status == OperationStatus.Completed &&
                ReferenceEquals(completion.Task.Result.Value, result.Value)) return false;
            try { result.Value.Dispose(); }
            catch (Exception error) { reportLateError(new FontResourceRetirementException(result.Value, error)); }
        }
        else if (result.Status == OperationStatus.Failed &&
            (!completion.Task.IsCompletedSuccessfully || !ReferenceEquals(completion.Task.Result, result)))
            reportLateError(result.Error!);
        return false;
    }

    public void Dispose()
    {
        scope.VerifyAccess();
        if (disposed) return;
        disposed = true;
        try { lifetime.Cancel(); }
        finally { registration.Unregister(); lifetime.Dispose(); }
    }
}
