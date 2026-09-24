namespace Xui.Experimental.Portable;

public readonly record struct StoredValue(bool Exists, ReadOnlyMemory<byte> Data);

/// <summary>Small application-owned documents, not a database or a secure secret store.</summary>
public interface IApplicationStorage
{
    Task<OperationResult<StoredValue>> ReadAsync(string key, CancellationToken cancellationToken = default);
    Task<OperationResult<bool>> WriteAsync(string key, ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default);
    Task<OperationResult<bool>> DeleteAsync(string key, CancellationToken cancellationToken = default);
}

public static class StorageKeys
{
    public static void Validate(string key)
    {
        ArgumentNullException.ThrowIfNull(key);
        if (key.Length is < 1 or > 128 || key.Any(character =>
            character is not (>= 'a' and <= 'z' or >= '0' and <= '9' or '-' or '_')))
            throw new ArgumentException("Storage keys must contain 1-128 lowercase ASCII letters, digits, hyphens, or underscores.", nameof(key));
    }
}

/// <summary>Bounded native application storage using same-directory atomic replacement.</summary>
/// <remarks>
/// The application selects a trusted private directory. Values are not encrypted.
/// Cancellation before commit prevents replacement; cancellation cannot undo a completed rename.
/// This implementation is unavailable in browsers; use their platform storage adapter.
/// </remarks>
public sealed class DirectoryApplicationStorage : IApplicationStorage
{
    private readonly string directory;
    private readonly int maximumBytes;

    public DirectoryApplicationStorage(string directory, int maximumBytes = 1024 * 1024)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(directory);
        if (!Path.IsPathFullyQualified(directory)) throw new ArgumentException("Storage requires an absolute private directory.", nameof(directory));
        if (maximumBytes <= 0) throw new ArgumentOutOfRangeException(nameof(maximumBytes));
        this.directory = Path.GetFullPath(directory);
        if (HasFileAncestor(this.directory))
            throw new ArgumentException("The storage location or one of its ancestors is a file.", nameof(directory));
        this.maximumBytes = maximumBytes;
    }

    public Task<OperationResult<StoredValue>> ReadAsync(string key, CancellationToken cancellationToken = default)
    {
        string path = FilePath(key);
        cancellationToken.ThrowIfCancellationRequested();
        if (OperatingSystem.IsBrowser()) return Task.FromResult(OperationResult<StoredValue>.Unsupported());
        return ReadCoreAsync(path, cancellationToken);
    }

    private async Task<OperationResult<StoredValue>> ReadCoreAsync(string path, CancellationToken cancellationToken)
    {
        try
        {
            await using var stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                FileShare.Read | FileShare.Delete, 4096, FileOptions.Asynchronous | FileOptions.SequentialScan);
            if (stream.Length > maximumBytes) throw new IOException("The stored document exceeds the configured size limit.");
            var data = new byte[checked((int)stream.Length)];
            await stream.ReadExactlyAsync(data, cancellationToken).ConfigureAwait(false);
            if (stream.ReadByte() != -1) throw new IOException("The stored document changed size while being read.");
            return OperationResult<StoredValue>.Completed(new(true, data));
        }
        catch (FileNotFoundException) { return OperationResult<StoredValue>.Completed(new(false, ReadOnlyMemory<byte>.Empty)); }
        catch (DirectoryNotFoundException error)
        {
            return HasFileAncestor(directory) ? OperationResult<StoredValue>.Failed(error) :
                OperationResult<StoredValue>.Completed(new(false, ReadOnlyMemory<byte>.Empty));
        }
        catch (UnauthorizedAccessException) { return OperationResult<StoredValue>.Denied(); }
        catch (IOException error) { return OperationResult<StoredValue>.Failed(error); }
    }

    public Task<OperationResult<bool>> WriteAsync(string key, ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
    {
        string path = FilePath(key);
        if (data.Length > maximumBytes) throw new ArgumentOutOfRangeException(nameof(data), "The document exceeds the configured size limit.");
        cancellationToken.ThrowIfCancellationRequested();
        if (OperatingSystem.IsBrowser()) return Task.FromResult(OperationResult<bool>.Unsupported());
        // Do not retain memory the caller can change during an asynchronous write.
        return WriteCoreAsync(path, data.ToArray(), cancellationToken);
    }

    private async Task<OperationResult<bool>> WriteCoreAsync(string path, byte[] data, CancellationToken cancellationToken)
    {
        string temporary = Path.Combine(directory, $".pending-{Guid.NewGuid():N}");
        Exception? failure = null;
        try
        {
            Directory.CreateDirectory(directory);
            await using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write,
                FileShare.None, 4096, FileOptions.Asynchronous | FileOptions.WriteThrough))
            {
                await stream.WriteAsync(data, cancellationToken).ConfigureAwait(false);
                await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
                stream.Flush(flushToDisk: true);
            }
            cancellationToken.ThrowIfCancellationRequested();
            File.Move(temporary, path, overwrite: true);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or OperationCanceledException)
        {
            failure = error;
        }
        try
        {
            File.Delete(temporary);
        }
        catch (DirectoryNotFoundException) { }
        catch (Exception cleanup) when (cleanup is IOException or UnauthorizedAccessException)
        {
            return OperationResult<bool>.Failed(failure is null ? cleanup : new AggregateException(failure, cleanup));
        }
        if (failure is OperationCanceledException) System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failure).Throw();
        if (failure is UnauthorizedAccessException) return OperationResult<bool>.Denied();
        return failure is null ? OperationResult<bool>.Completed(true) : OperationResult<bool>.Failed(failure);
    }

    public Task<OperationResult<bool>> DeleteAsync(string key, CancellationToken cancellationToken = default)
    {
        string path = FilePath(key);
        cancellationToken.ThrowIfCancellationRequested();
        if (OperatingSystem.IsBrowser()) return Task.FromResult(OperationResult<bool>.Unsupported());
        try
        {
            File.Delete(path);
            return Task.FromResult(OperationResult<bool>.Completed(true));
        }
        catch (DirectoryNotFoundException error)
        {
            return Task.FromResult(HasFileAncestor(directory) ?
                OperationResult<bool>.Failed(error) : OperationResult<bool>.Completed(true));
        }
        catch (UnauthorizedAccessException) { return Task.FromResult(OperationResult<bool>.Denied()); }
        catch (IOException error) { return Task.FromResult(OperationResult<bool>.Failed(error)); }
    }

    private string FilePath(string key)
    {
        StorageKeys.Validate(key);
        return Path.Combine(directory, key + ".data");
    }

    private static bool HasFileAncestor(string path)
    {
        for (string? current = path; current is not null; current = Path.GetDirectoryName(current))
        {
            if (File.Exists(current)) return true;
            if (Directory.Exists(current)) return false;
        }
        return false;
    }
}
