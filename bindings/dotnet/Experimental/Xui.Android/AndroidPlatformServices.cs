using Android.App;
using Android.Content;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

public sealed class AndroidPlatformServices : IPlatformServices, IDisposable
{
    private readonly IUiDispatcher dispatcher;
    private readonly object gate = new();
    private readonly HashSet<Action<Exception>> pending = [];
    private IAndroidServiceBridge? bridge;

    public AndroidPlatformServices(Activity activity, IUiDispatcher dispatcher)
        : this(new ActivityServiceBridge(activity), dispatcher) { }

    internal AndroidPlatformServices(IAndroidServiceBridge bridge, IUiDispatcher dispatcher)
    {
        ArgumentNullException.ThrowIfNull(bridge);
        ArgumentNullException.ThrowIfNull(dispatcher);
        this.bridge = bridge;
        this.dispatcher = dispatcher;
        VerifyThread();
        VerifyAlive();
    }

    private void VerifyThread()
    {
        if (!dispatcher.CheckAccess()) throw new InvalidOperationException("Android service lifetime and availability require the UI thread.");
    }

    private IAndroidServiceBridge VerifyAlive()
    {
        var current = bridge;
        ObjectDisposedException.ThrowIf(current is null || !current.IsAlive, this);
        return current;
    }

    public CapabilityAvailability GetAvailability(ServiceCapability capability)
    {
        PlatformServicePolicy.ValidateCapability(capability);
        VerifyThread();
        VerifyAlive();
        return capability is ServiceCapability.Clipboard or ServiceCapability.OpenUri
            ? CapabilityAvailability.Available : CapabilityAvailability.Unsupported;
    }

    public Task<OperationResult<string>> ReadClipboardAsync(CancellationToken cancellationToken = default) =>
        Execute(current => current.HasFocus ? current.ReadClipboard() : OperationResult<string>.Denied(), cancellationToken);

    public Task<OperationResult<bool>> WriteClipboardAsync(string text, CancellationToken cancellationToken = default)
    {
        PlatformServicePolicy.ValidateClipboardText(text);
        return Execute(current => current.HasFocus ? current.WriteClipboard(text) : OperationResult<bool>.Denied(), cancellationToken);
    }

    public Task<OperationResult<bool>> OpenUriAsync(Uri uri, CancellationToken cancellationToken = default)
    {
        PlatformServicePolicy.ValidateLaunchUri(uri);
        return Execute(current => current.HasFocus ? current.OpenUri(uri) : OperationResult<bool>.Denied(), cancellationToken);
    }

    private Task<OperationResult<T>> Execute<T>(Func<IAndroidServiceBridge, OperationResult<T>> operation, CancellationToken cancellationToken)
    {
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<OperationResult<T>>(cancellationToken);
        var completion = new TaskCompletionSource<OperationResult<T>>(TaskCreationOptions.RunContinuationsAsynchronously);
        void Fail(Exception error) => completion.TrySetException(error);
        lock (gate)
        {
            if (bridge is null) return Task.FromException<OperationResult<T>>(new ObjectDisposedException(nameof(AndroidPlatformServices)));
            pending.Add(Fail);
        }
        var registration = cancellationToken.Register(() => completion.TrySetCanceled(cancellationToken));
        void Run()
        {
            if (completion.Task.IsCompleted) return;
            try
            {
                VerifyThread();
                cancellationToken.ThrowIfCancellationRequested();
                completion.TrySetResult(operation(VerifyAlive()));
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                completion.TrySetCanceled(cancellationToken);
            }
            catch (Exception error) { completion.TrySetException(error); }
        }
        try
        {
            if (dispatcher.CheckAccess()) Run();
            else dispatcher.Post(Run);
        }
        catch (Exception error) { completion.TrySetException(error); }
        return Finish();

        async Task<OperationResult<T>> Finish()
        {
            try { return await completion.Task.ConfigureAwait(false); }
            finally
            {
                registration.Dispose();
                lock (gate) pending.Remove(Fail);
            }
        }
    }

    public void Dispose()
    {
        VerifyThread();
        lock (gate)
        {
            if (bridge is null) return;
            bridge = null;
            var error = new ObjectDisposedException(nameof(AndroidPlatformServices));
            foreach (var fail in pending) fail(error);
            pending.Clear();
        }
    }

    private sealed class ActivityServiceBridge : IAndroidServiceBridge
    {
        private readonly WeakReference<Activity> activity;
        internal ActivityServiceBridge(Activity owner)
        {
            ArgumentNullException.ThrowIfNull(owner);
            activity = new(owner);
        }
        public bool IsAlive => activity.TryGetTarget(out var owner) && !owner.IsDestroyed && !owner.IsFinishing;
        public bool HasFocus => Owner.HasWindowFocus;
        private Activity Owner => activity.TryGetTarget(out var owner) && !owner.IsDestroyed && !owner.IsFinishing
            ? owner : throw new ObjectDisposedException(nameof(Activity));

        public OperationResult<string> ReadClipboard()
        {
            try
            {
                var manager = Owner.GetSystemService(Context.ClipboardService) as ClipboardManager
                    ?? throw new InvalidOperationException("Android clipboard service is unavailable.");
                using var clip = manager.PrimaryClip;
                if (clip is null || clip.ItemCount == 0)
                    return OperationResult<string>.Failed(new InvalidOperationException("Android returned no readable clipboard data."));
                string? text = clip.GetItemAt(0)?.Text?.ToString();
                return text is null ? OperationResult<string>.Unsupported() : OperationResult<string>.Completed(text);
            }
            catch (Java.Lang.SecurityException) { return OperationResult<string>.Denied(); }
            catch (Java.Lang.Exception error) { return OperationResult<string>.Failed(error); }
        }

        public OperationResult<bool> WriteClipboard(string text)
        {
            try
            {
                var manager = Owner.GetSystemService(Context.ClipboardService) as ClipboardManager
                    ?? throw new InvalidOperationException("Android clipboard service is unavailable.");
                using var clip = ClipData.NewPlainText("XUI", text)
                    ?? throw new InvalidOperationException("Android could not create plain-text clipboard data.");
                manager.PrimaryClip = clip;
                return OperationResult<bool>.Completed(true);
            }
            catch (Java.Lang.SecurityException) { return OperationResult<bool>.Denied(); }
            catch (Java.Lang.Exception error) { return OperationResult<bool>.Failed(error); }
        }

        public OperationResult<bool> OpenUri(Uri uri)
        {
            try
            {
                using var address = global::Android.Net.Uri.Parse(uri.AbsoluteUri)
                    ?? throw new InvalidOperationException("Android could not parse the launch URI.");
                using var intent = new Intent(Intent.ActionView, address);
                Owner.StartActivity(intent);
                return OperationResult<bool>.Completed(true);
            }
            catch (ActivityNotFoundException) { return OperationResult<bool>.Unsupported(); }
            catch (Java.Lang.SecurityException) { return OperationResult<bool>.Denied(); }
            catch (Java.Lang.Exception error) { return OperationResult<bool>.Failed(error); }
        }
    }
}

internal interface IAndroidServiceBridge
{
    bool IsAlive { get; }
    bool HasFocus { get; }
    OperationResult<string> ReadClipboard();
    OperationResult<bool> WriteClipboard(string text);
    OperationResult<bool> OpenUri(Uri uri);
}
