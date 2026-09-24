using Microsoft.JSInterop;

namespace Xui.Experimental.Web;

/// <summary>A nonthrowing, thread-safe fatal-error boundary. The caller owns the imported DOM module.</summary>
public sealed class BrowserErrorReporter : IDisposable
{
    private readonly IJSInProcessObjectReference module;
    private readonly string errorId;
    private readonly SynchronizationContext context = SynchronizationContext.Current ?? new SynchronizationContext();
    private readonly int thread = Environment.CurrentManagedThreadId;
    private Exception? lastError;
    private int disposed;

    public BrowserErrorReporter(IJSInProcessObjectReference module, string errorId)
    {
        this.module = module ?? throw new ArgumentNullException(nameof(module));
        ArgumentException.ThrowIfNullOrWhiteSpace(errorId);
        this.errorId = errorId;
    }

    public Exception? LastError => Volatile.Read(ref lastError);

    public void Report(Exception error)
    {
        var report = Log(error ?? new ArgumentNullException(nameof(error)));
        if (Volatile.Read(ref disposed) != 0) return;
        if (Environment.CurrentManagedThreadId == thread) Render(report);
        else
        {
            try { context.Post(_ => Render(report), null); }
            catch (Exception dispatchError) { Log(new AggregateException(report, dispatchError)); }
        }
    }

    private void Render(Exception error)
    {
        if (Volatile.Read(ref disposed) != 0) return;
        try
        {
            if (Environment.CurrentManagedThreadId != thread)
                throw new InvalidOperationException("The browser error dispatcher did not deliver to the UI thread.");
            module.InvokeVoid("reportError", errorId, error.ToString(), false);
        }
        catch (Exception renderingError) { Log(new AggregateException(error, renderingError)); }
    }

    private Exception Log(Exception error)
    {
        try
        {
            string message = error.ToString().Replace("\r", "\\r").Replace("\n", "\\n");
            Console.Error.WriteLine($"XUI browser error: {message}");
        }
        catch (Exception loggingError)
        {
            // Keep reporting failures observable even when the console itself is unavailable.
            error = new AggregateException(error, loggingError);
        }
        Volatile.Write(ref lastError, error);
        return error;
    }

    public void Dispose() => Interlocked.Exchange(ref disposed, 1);
}
