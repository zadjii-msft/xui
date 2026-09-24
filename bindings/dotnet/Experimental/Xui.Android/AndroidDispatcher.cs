using Android.OS;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

public sealed class AndroidDispatcher : IUiDispatcher
{
    private readonly Handler handler;
    private readonly int uiThreadId;
    private long accessChecks;
    internal bool TraceAccessChecks { get; set; }
    internal long AccessChecks => Interlocked.Read(ref accessChecks);

    public AndroidDispatcher()
    {
        var main = Looper.MainLooper ?? throw new InvalidOperationException("Android has no main looper.");
        if (Looper.MyLooper() != main) throw new InvalidOperationException("Create the dispatcher on the Android UI thread.");
        uiThreadId = System.Environment.CurrentManagedThreadId;
        handler = new Handler(main);
    }

    public bool CheckAccess()
    {
        if (TraceAccessChecks) Interlocked.Increment(ref accessChecks);
        return System.Environment.CurrentManagedThreadId == uiThreadId;
    }

    public void Post(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        if (!handler.Post(action)) throw new InvalidOperationException("The Android UI queue rejected the callback.");
    }
}
