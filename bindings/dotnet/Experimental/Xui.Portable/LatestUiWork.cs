namespace Xui.Experimental.Portable;

/// <summary>Cancels superseded work and applies only the newest result on the host UI thread.</summary>
/// <remarks>Application-owned; dispose before its host. Observe each returned task, including cancellation.</remarks>
public sealed class LatestUiWork : IDisposable
{
    private readonly Host host;
    private readonly int thread = Environment.CurrentManagedThreadId;
    private UiWorkScope? current;
    private bool disposed;
    private bool replacing;

    public LatestUiWork(Host host)
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
        if (replacing) throw new InvalidOperationException("Latest-result work cannot be replaced reentrantly.");
        replacing = true;
        try
        {
            var previous = current;
            current = null;
            previous?.Dispose();
            ObjectDisposedException.ThrowIf(disposed, this);
            current = new UiWorkScope(host);
            return current.RunAsync(produce, apply, cancellationToken);
        }
        finally { replacing = false; }
    }

    public void Dispose()
    {
        if (Environment.CurrentManagedThreadId != thread)
            throw new InvalidOperationException("Dispose latest-result work on its creating UI thread.");
        if (disposed) return;
        disposed = true;
        var previous = current;
        current = null;
        previous?.Dispose();
    }
}
