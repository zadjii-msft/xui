namespace Xui.Experimental.Portable;

/// <summary>Owns cancellable background work and guarded UI delivery for one application scope.</summary>
/// <remarks>
/// Create, start work, and dispose on the host UI thread. Dispose this scope before its host.
/// Returned tasks must be observed. Producers retain responsibility for their resources and
/// must honor cancellation; cancellation prevents delivery even if a producer finishes late.
/// </remarks>
public sealed class UiWorkScope : IDisposable
{
    private readonly Host host;
    private readonly int thread = Environment.CurrentManagedThreadId;
    private readonly CancellationTokenSource lifetime = new();
    private bool disposed;

    public UiWorkScope(Host host)
    {
        ArgumentNullException.ThrowIfNull(host);
        host.VerifyMutation();
        this.host = host;
    }

    public Task RunAsync<T>(Func<CancellationToken, Task<T>> produce, Action<T> apply,
        CancellationToken cancellationToken = default)
    {
        host.VerifyMutation();
        ObjectDisposedException.ThrowIf(disposed, this);
        ArgumentNullException.ThrowIfNull(produce);
        ArgumentNullException.ThrowIfNull(apply);
        return RunCoreAsync(produce, apply, cancellationToken);
    }

    private async Task RunCoreAsync<T>(Func<CancellationToken, Task<T>> produce, Action<T> apply,
        CancellationToken cancellationToken)
    {
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token, cancellationToken);
        var token = linked.Token;
        token.ThrowIfCancellationRequested();
        var production = produce(token) ?? throw new InvalidOperationException("The producer did not return a task.");
        T result = await production.WaitAsync(token).ConfigureAwait(false);
        token.ThrowIfCancellationRequested();
        await host.DispatchAsync(() =>
        {
            token.ThrowIfCancellationRequested();
            ObjectDisposedException.ThrowIf(disposed, this);
            apply(result);
        }).WaitAsync(token).ConfigureAwait(false);
    }

    public void Dispose()
    {
        if (Environment.CurrentManagedThreadId != thread)
            throw new InvalidOperationException("Dispose application work on its creating UI thread.");
        if (disposed) return;
        disposed = true;
        try { lifetime.Cancel(); }
        finally { lifetime.Dispose(); }
    }
}
