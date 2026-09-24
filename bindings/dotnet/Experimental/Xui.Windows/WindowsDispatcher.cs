using Xui.Experimental.Portable;

namespace Xui.Experimental.Windows;

/// <summary>A window-owned UI queue with explicit cancellation when the native window closes.</summary>
public sealed class WindowsDispatcher : ICancellableUiDispatcher, IDisposable
{
    private readonly int thread = Environment.CurrentManagedThreadId;
    private readonly object gate = new();
    private readonly Queue<(Action Run, Action<Exception> Canceled)> pending = [];
    private bool scheduled;
    private bool stopped;
    internal Window Window { get; }

    public WindowsDispatcher(Window window)
    {
        ArgumentNullException.ThrowIfNull(window);
        window.VerifyAccess();
        if (window.State is WindowState.Closed or WindowState.Closing)
            throw new InvalidOperationException("The dispatcher requires a live window.");
        Window = window;
        window.Closed += OnClosed;
    }

    public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;

    internal void VerifyAccess()
    {
        if (!CheckAccess()) throw new InvalidOperationException("Windows controls require the creating UI thread.");
    }

    public void Post(Action action) => Post(action, error => Console.Error.WriteLine($"Xui.Windows dispatch canceled: {error}"));

    public void Post(Action action, Action<Exception> canceled)
    {
        ArgumentNullException.ThrowIfNull(action);
        ArgumentNullException.ThrowIfNull(canceled);
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(stopped, this);
            if (!scheduled)
            {
                if (!Window.PostUnscoped(Drain, Dispose))
                    throw new InvalidOperationException("The Windows UI dispatcher is unavailable.");
                scheduled = true;
            }
            pending.Enqueue((action, canceled));
        }
    }

    private void Drain()
    {
        VerifyAccess();
        List<Exception>? failures = null;
        while (true)
        {
            (Action Run, Action<Exception> Canceled) action;
            bool cancel;
            lock (gate)
            {
                if (!pending.TryDequeue(out action))
                {
                    scheduled = false;
                    break;
                }
                cancel = stopped;
            }
            try
            {
                if (cancel) action.Canceled(new ObjectDisposedException(nameof(WindowsDispatcher), "The Windows UI dispatcher has closed."));
                else action.Run();
            }
            catch (Exception error) { (failures ??= []).Add(error); }
        }
        if (failures is not null) throw new AggregateException("Windows UI callbacks failed.", failures);
    }

    private void OnClosed(WindowClosedEventArgs _) => Dispose();

    /// <summary>Rejects new work and cancels accepted actions without executing application work.</summary>
    public void Dispose()
    {
        VerifyAccess();
        lock (gate)
        {
            if (stopped) return;
            stopped = true;
        }
        Window.Closed -= OnClosed;
        Drain();
    }
}
