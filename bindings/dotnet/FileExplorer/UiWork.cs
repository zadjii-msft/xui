namespace Xui.FileExplorer;

internal sealed class UiWork(Window window) : IDisposable
{
    private readonly CancellationTokenSource lifetime = new();
    public CancellationToken Lifetime => lifetime.Token;

    public void Start<T>(Func<CancellationToken, Task<T>> operation, CancellationToken cancellation,
        Action<T> complete, Action<Exception> failed)
    {
        _ = Execute();
        async Task Execute()
        {
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(Lifetime, cancellation);
            try
            {
                var result = await operation(linked.Token).ConfigureAwait(false);
                if (!linked.IsCancellationRequested)
                    window.Post(() => { if (!Lifetime.IsCancellationRequested && !cancellation.IsCancellationRequested) complete(result); });
            }
            catch (OperationCanceledException) when (linked.IsCancellationRequested) { }
            catch (Exception error) when (IsExpected(error))
            {
                if (!linked.IsCancellationRequested)
                    window.Post(() => { if (!Lifetime.IsCancellationRequested && !cancellation.IsCancellationRequested) failed(error); });
            }
            catch (Exception error)
            {
                // Unexpected worker failures must reach the binding's callback error boundary.
                window.Post(() => throw new InvalidOperationException("Explorer background work failed.", error));
            }
        }
    }

    internal static bool IsExpected(Exception error) => error is IOException or InvalidDataException or UnauthorizedAccessException
        or ArgumentException or NotSupportedException or System.Security.SecurityException;

    public void Dispose() => lifetime.Cancel();
}
