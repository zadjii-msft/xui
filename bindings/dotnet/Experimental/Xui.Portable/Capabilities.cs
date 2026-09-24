namespace Xui.Experimental.Portable;

public enum ServiceCapability
{
    Clipboard, OpenUri, OpenFile, SaveFile, ApplicationStorage
}

public enum CapabilityAvailability { Available, RequiresUserGesture, Unsupported }
public enum OperationStatus { Completed, Cancelled, Denied, Unsupported, Failed }

/// <summary>An explicit result from a platform service; no empty-value success fallback.</summary>
public sealed class OperationResult<T>
{
    private readonly T? value;
    public OperationStatus Status { get; }
    public T Value => Status == OperationStatus.Completed ? value! :
        throw new InvalidOperationException($"The platform operation did not complete: {Status}.", Error);
    public Exception? Error { get; }
    private OperationResult(OperationStatus status, T? value, Exception? error)
    {
        Status = status;
        this.value = value;
        Error = error;
    }
    public static OperationResult<T> Completed(T value)
    {
        ArgumentNullException.ThrowIfNull(value);
        return new(OperationStatus.Completed, value, null);
    }
    public static OperationResult<T> Cancelled() => new(OperationStatus.Cancelled, default, null);
    public static OperationResult<T> Denied() => new(OperationStatus.Denied, default, null);
    public static OperationResult<T> Unsupported() => new(OperationStatus.Unsupported, default, null);
    public static OperationResult<T> Failed(Exception error)
    {
        ArgumentNullException.ThrowIfNull(error);
        return new(OperationStatus.Failed, default, error);
    }
}

/// <summary>Platform-specific services with explicit capability and permission outcomes.</summary>
/// <remarks>
/// An external cancellation token cancels the task. OperationStatus.Cancelled represents
/// cancellation by native UI, not a failure. Cancelling an in-flight side effect cannot undo it.
/// Availability is advisory; permissions and user activation are checked for each operation.
/// </remarks>
public interface IPlatformServices
{
    CapabilityAvailability GetAvailability(ServiceCapability capability);
    Task<OperationResult<string>> ReadClipboardAsync(CancellationToken cancellationToken = default);
    Task<OperationResult<bool>> WriteClipboardAsync(string text, CancellationToken cancellationToken = default);
    Task<OperationResult<bool>> OpenUriAsync(Uri uri, CancellationToken cancellationToken = default);
}

public static class PlatformServicePolicy
{
    public static void ValidateCapability(ServiceCapability capability)
    {
        if (!Enum.IsDefined(capability)) throw new ArgumentOutOfRangeException(nameof(capability));
    }

    public static void ValidateClipboardText(string text) => Values.Text(text);

    public static void ValidateLaunchUri(Uri uri)
    {
        ArgumentNullException.ThrowIfNull(uri);
        if (!uri.IsAbsoluteUri || uri.OriginalString.Any(char.IsControl) ||
            uri.Scheme is not ("http" or "https" or "mailto" or "tel"))
            throw new ArgumentException("Only absolute HTTP, HTTPS, mailto, and tel URIs can be launched.", nameof(uri));
        if ((uri.Scheme is "http" or "https") && uri.UserInfo.Length != 0)
            throw new ArgumentException("Launch URIs must not contain HTTP credentials.", nameof(uri));
    }
}
