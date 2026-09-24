namespace Xui.Experimental.Portable;

/// <summary>One selected file, read incrementally with an explicit byte budget.</summary>
public sealed class FileSelectionOptions
{
    public long MaximumBytes { get; }

    public FileSelectionOptions(long maximumBytes)
    {
        if (maximumBytes <= 0) throw new ArgumentOutOfRangeException(nameof(maximumBytes));
        MaximumBytes = maximumBytes;
    }
}

/// <summary>A platform-native, single-file open operation. This contract does not save files.</summary>
/// <remarks>
/// Call from the platform UI thread and, where required, directly from a user gesture.
/// External cancellation cancels the task; native picker dismissal returns Cancelled.
/// SaveFile is Unsupported. A completed result transfers ownership to the caller.
/// </remarks>
public interface IFilePicker : IDisposable
{
    CapabilityAvailability GetAvailability(ServiceCapability capability);
    Task<OperationResult<PickedFile>> OpenAsync(FileSelectionOptions options, CancellationToken cancellationToken = default);
}

/// <summary>An owned, bounded read stream and an untrusted display label, never a filesystem path.</summary>
/// <remarks>
/// The constructor takes ownership of content only when it succeeds. Length is advisory;
/// the stream enforces MaximumBytes even when the supplied length is stale or unknown.
/// Dispose this object or Content after reading. Do not use DisplayName as a storage key or path.
/// Reads are sequential; simultaneous reads are rejected.
/// </remarks>
public sealed class PickedFile : IDisposable, IAsyncDisposable
{
    public string DisplayName { get; }
    public long? Length { get; }
    public long MaximumBytes { get; }
    public Stream Content { get; }

    public PickedFile(string displayName, Stream content, FileSelectionOptions options, long? length = null)
    {
        ArgumentNullException.ThrowIfNull(displayName);
        ArgumentNullException.ThrowIfNull(content);
        ArgumentNullException.ThrowIfNull(options);
        if (length < 0) throw new ArgumentOutOfRangeException(nameof(length));
        if (!content.CanRead) throw new ArgumentException("The selected file must provide a readable stream.", nameof(content));
        if (length > options.MaximumBytes) throw new FileSelectionTooLargeException(options.MaximumBytes);
        DisplayName = displayName;
        Length = length;
        MaximumBytes = options.MaximumBytes;
        Content = new FileSelectionReadStream(content, MaximumBytes);
    }

    public void Dispose() => Content.Dispose();
    public ValueTask DisposeAsync() => Content.DisposeAsync();
}

public sealed class FileSelectionTooLargeException : IOException
{
    public long MaximumBytes { get; }

    public FileSelectionTooLargeException(long maximumBytes)
        : base($"The selected file exceeds the {maximumBytes}-byte read limit.")
    {
        MaximumBytes = maximumBytes;
    }
}
