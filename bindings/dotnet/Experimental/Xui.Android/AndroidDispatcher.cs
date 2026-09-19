using Android.OS;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

public sealed class AndroidDispatcher : IUiDispatcher
{
    private readonly Handler handler;

    public AndroidDispatcher()
    {
        var main = Looper.MainLooper ?? throw new InvalidOperationException("Android has no main looper.");
        if (Looper.MyLooper() != main) throw new InvalidOperationException("Create the dispatcher on the Android UI thread.");
        handler = new Handler(main);
    }

    public bool CheckAccess() => Looper.MyLooper() == Looper.MainLooper;

    public void Post(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        if (!handler.Post(action)) throw new InvalidOperationException("The Android UI queue rejected the callback.");
    }
}
