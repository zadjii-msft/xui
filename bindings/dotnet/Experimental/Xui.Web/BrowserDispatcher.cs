using Xui.Experimental.Portable;

namespace Xui.Experimental.Web;

public sealed class BrowserDispatcher : IUiDispatcher
{
    private readonly int thread = Environment.CurrentManagedThreadId;
    private readonly SynchronizationContext context = SynchronizationContext.Current ?? new SynchronizationContext();
    private readonly Action<Exception> reportError;

    public BrowserDispatcher(Action<Exception> reportError)
    {
        this.reportError = reportError ?? throw new ArgumentNullException(nameof(reportError));
    }

    public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;

    public void Post(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        context.Post(_ =>
        {
            try
            {
                // Host.DispatchAsync must run its guard to complete its task even after bad delivery.
                action();
            }
            catch (Exception error) { reportError(error); }
        }, null);
    }
}
