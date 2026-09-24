using System.Text.Json;
using Microsoft.JSInterop;
using Xui.Experimental.Portable;
using Xui.Experimental.Web;

internal static class BrowserStorageChecks
{
    public static async Task<int> Run()
    {
        int assertions = 0;
        void Check(bool value)
        {
            if (!value) throw new InvalidOperationException("Browser storage assertion failed.");
            assertions++;
        }
        using var module = new StorageModule();
        using var storage = new IndexedDbApplicationStorage(module, "test-store", 3);
        foreach (string response in new[]
        {
            "null", "{}", """{"status":"Completed","exists":true}""",
            """{"status":"Completed","exists":false,"data":"AQ=="}""",
            """{"status":"Completed","exists":true,"data":"AQIDBA=="}""",
            """{"status":"Completed","exists":"invalid","data":""}"""
        })
        {
            module.Response = response;
            var result = await storage.ReadAsync("document").WaitAsync(TimeSpan.FromSeconds(5));
            Check(result.Status == OperationStatus.Failed && result.Error is not null);
        }
        module.Response = """{"status":"Completed","exists":true,"data":""}""";
        var empty = await storage.ReadAsync("document");
        Check(empty.Status == OperationStatus.Completed && empty.Value.Exists && empty.Value.Data.IsEmpty);
        module.Response = """{"status":"Completed","exists":false,"data":""}""";
        var missing = await storage.ReadAsync("document");
        Check(missing.Status == OperationStatus.Completed && !missing.Value.Exists && missing.Value.Data.IsEmpty);
        foreach (var (json, status) in new[] { ("Denied", OperationStatus.Denied), ("Unsupported", OperationStatus.Unsupported) })
        {
            module.Response = $"{{\"status\":\"{json}\"}}";
            Check((await storage.DeleteAsync("document")).Status == status);
        }
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        int before = module.Calls;
        var cancelled = storage.ReadAsync("document", cancellation.Token);
        try { await cancelled.WaitAsync(TimeSpan.FromSeconds(5)); }
        catch (OperationCanceledException) when (cancelled.IsCanceled) { assertions++; }
        Check(cancelled.IsCanceled && module.Calls == before);
        storage.Dispose();
        Check(module.Closed);
        var afterDispose = storage.ReadAsync("document");
        try { await afterDispose; }
        catch (ObjectDisposedException) { assertions++; }
        Check(afterDispose.IsFaulted && module.Calls == before);
        return assertions;
    }

    private sealed class StorageModule : IJSInProcessObjectReference
    {
        public string Response { get; set; } = "{}";
        public int Calls { get; private set; }
        public bool Closed { get; private set; }
        public TValue Invoke<TValue>(string identifier, params object?[]? args)
        {
            if (identifier == "createStorage" && this is TValue reference) return reference;
            if (identifier == "close") { Closed = true; return default!; }
            throw new InvalidOperationException($"Unexpected storage invocation: {identifier}.");
        }
        public ValueTask<TValue> InvokeAsync<TValue>(string identifier, object?[]? args) =>
            InvokeAsync<TValue>(identifier, CancellationToken.None, args);
        public ValueTask<TValue> InvokeAsync<TValue>(string identifier, CancellationToken cancellationToken, object?[]? args)
        {
            Calls++;
            return ValueTask.FromResult(JsonSerializer.Deserialize<TValue>(Response)!);
        }
        public void Dispose() { }
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
}
