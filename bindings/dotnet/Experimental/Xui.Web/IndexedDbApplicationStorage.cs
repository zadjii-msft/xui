using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.JSInterop;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Web;

/// <summary>Bounded application-owned IndexedDB documents. Use and dispose on the browser UI thread.</summary>
public sealed class IndexedDbApplicationStorage : IApplicationStorage, IDisposable
{
    private readonly IJSInProcessObjectReference storage;
    private readonly int maximumBytes;
    private readonly int thread = Environment.CurrentManagedThreadId;
    private int nextOperation;
    private bool disposed;

    public IndexedDbApplicationStorage(IJSInProcessObjectReference module, string databaseName, int maximumBytes = 1024 * 1024)
    {
        ArgumentNullException.ThrowIfNull(module);
        StorageKeys.Validate(databaseName);
        if (maximumBytes <= 0) throw new ArgumentOutOfRangeException(nameof(maximumBytes));
        this.maximumBytes = maximumBytes;
        storage = module.Invoke<IJSInProcessObjectReference>("createStorage", databaseName, maximumBytes);
    }

    public async Task<OperationResult<StoredValue>> ReadAsync(string key, CancellationToken cancellationToken = default)
    {
        var result = await Run("read", key, null, cancellationToken);
        if (result.Status == "Completed")
        {
            if (result.Exists is not bool exists || result.Data is null ||
                result.Data.Length > maximumBytes || (!exists && result.Data.Length != 0))
                return OperationResult<StoredValue>.Failed(new InvalidOperationException("Invalid IndexedDB read response."));
            return OperationResult<StoredValue>.Completed(new(exists, result.Data));
        }
        return Failure<StoredValue>(result);
    }

    public async Task<OperationResult<bool>> WriteAsync(string key, ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
    {
        if (data.Length > maximumBytes) throw new ArgumentOutOfRangeException(nameof(data), "The document exceeds the configured size limit.");
        var result = await Run("write", key, data.ToArray(), cancellationToken);
        return result.Status == "Completed" ? OperationResult<bool>.Completed(true) : Failure<bool>(result);
    }

    public async Task<OperationResult<bool>> DeleteAsync(string key, CancellationToken cancellationToken = default)
    {
        var result = await Run("delete", key, null, cancellationToken);
        return result.Status == "Completed" ? OperationResult<bool>.Completed(true) : Failure<bool>(result);
    }

    private async Task<StorageResponse> Run(string operation, string key, byte[]? data, CancellationToken cancellationToken)
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        StorageKeys.Validate(key);
        cancellationToken.ThrowIfCancellationRequested();
        int id = checked(++nextOperation);
        try
        {
            var pending = storage.InvokeAsync<StorageResponse>("run", id, operation, key, data);
            using var registration = cancellationToken.Register(() => { if (!disposed) storage.InvokeVoid("cancel", id); });
            var response = await pending;
            if (response?.Status == "Cancelled" && cancellationToken.IsCancellationRequested)
                throw new OperationCanceledException(cancellationToken);
            return response ?? new() { Status = "Failed", Error = "Missing IndexedDB response." };
        }
        catch (JSException error) { return new() { Status = "Failed", Error = error.Message }; }
        catch (JsonException error) { return new() { Status = "Failed", Error = error.Message }; }
    }

    private static OperationResult<T> Failure<T>(StorageResponse response) => response.Status switch
    {
        "Denied" => OperationResult<T>.Denied(),
        "Unsupported" => OperationResult<T>.Unsupported(),
        _ => OperationResult<T>.Failed(new InvalidOperationException(response.Error ?? $"Invalid IndexedDB status: {response.Status}."))
    };

    private void VerifyAccess()
    {
        if (Environment.CurrentManagedThreadId != thread)
            throw new InvalidOperationException("IndexedDB storage requires the browser UI thread.");
    }

    public void Dispose()
    {
        VerifyAccess();
        if (disposed) return;
        disposed = true;
        try { storage.InvokeVoid("close"); }
        finally { storage.Dispose(); }
    }

    private sealed class StorageResponse
    {
        public StorageResponse() { }
        [JsonPropertyName("status")] public string? Status { get; set; }
        [JsonPropertyName("exists")] public bool? Exists { get; set; }
        [JsonPropertyName("data")] public byte[]? Data { get; set; }
        [JsonPropertyName("error")] public string? Error { get; set; }
    }
}
