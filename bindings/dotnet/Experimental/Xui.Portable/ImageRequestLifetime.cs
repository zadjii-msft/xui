namespace Xui.Experimental.Portable;

/// <summary>UI-thread attachment ownership for one image request and its eventual decoded resource.</summary>
/// <remarks>
/// Begin on source replacement; dispose before native peer/module teardown. Native decoding
/// may continue after cancellation. Deliver every late resource to its request's TryOwn method
/// on the original UI thread so it is released, not published into a replacement attachment.
/// This object does not decode, marshal callbacks, join workers, or promise that a codec stopped.
/// </remarks>
public sealed class ImageRequestLifetime : IDisposable
{
    private readonly IUiDispatcher dispatcher;
    private readonly Action<Exception> reportCleanupError;
    private ImageRequest? current;
    private bool disposed;
    private bool changing;

    public ImageRequestLifetime(IUiDispatcher dispatcher, Action<Exception> reportCleanupError)
    {
        this.dispatcher = dispatcher ?? throw new ArgumentNullException(nameof(dispatcher));
        this.reportCleanupError = reportCleanupError ?? throw new ArgumentNullException(nameof(reportCleanupError));
        VerifyAccess();
    }

    internal void VerifyAccess()
    {
        if (!dispatcher.CheckAccess()) throw new InvalidOperationException("Image requests require their owning UI thread.");
    }

    public ImageRequest Begin()
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (changing) throw new InvalidOperationException("Image request replacement cannot reenter.");
        changing = true;
        try
        {
            var previous = current;
            current = null;
            previous?.Retire();
            ObjectDisposedException.ThrowIf(disposed, this);
            return current = new ImageRequest(this);
        }
        finally { changing = false; }
    }

    internal bool IsCurrent(ImageRequest request) => !disposed && ReferenceEquals(current, request);
    internal void ReportCleanup(Exception error) => reportCleanupError(error);

    public void Dispose()
    {
        VerifyAccess();
        if (disposed) return;
        disposed = true;
        var previous = current;
        current = null;
        previous?.Retire();
    }
}

/// <summary>A single generation. TryOwn transfers a successful resource or disposes a late one.</summary>
public sealed class ImageRequest
{
    private readonly ImageRequestLifetime owner;
    private readonly CancellationTokenSource cancellation = new();
    private IDisposable? resource;
    private WeakReference<IDisposable>? accepted;
    private WeakReference<IDisposable>? lastRejected;
    private bool completed, retired;
    public CancellationToken Token { get; }
    public bool IsCurrent { get { owner.VerifyAccess(); return owner.IsCurrent(this) && !retired; } }

    internal ImageRequest(ImageRequestLifetime owner)
    {
        this.owner = owner;
        Token = cancellation.Token;
    }

    public bool TryOwn(IDisposable value)
    {
        ArgumentNullException.ThrowIfNull(value);
        owner.VerifyAccess();
        if (!completed && IsCurrent)
        {
            completed = true;
            resource = value;
            accepted = new(value);
            return true;
        }
        if (accepted is not null && accepted.TryGetTarget(out var published) && ReferenceEquals(published, value)) return false;
        if (lastRejected is not null && lastRejected.TryGetTarget(out var rejected) && ReferenceEquals(rejected, value)) return false;
        lastRejected = new(value);
        try { value.Dispose(); }
        catch (Exception error) { owner.ReportCleanup(error); }
        return false;
    }

    internal void Retire()
    {
        if (retired) return;
        retired = true;
        List<Exception>? failures = null;
        try { cancellation.Cancel(); }
        catch (Exception error) { (failures ??= []).Add(error); }
        finally { cancellation.Dispose(); }
        var previous = resource;
        resource = null;
        try { previous?.Dispose(); }
        catch (Exception error) { (failures ??= []).Add(error); }
        if (failures is not null) throw new AggregateException("Image request cleanup failed.", failures);
    }
}
