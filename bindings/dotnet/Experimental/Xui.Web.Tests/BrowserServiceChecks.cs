using System.Text.Json;
using Microsoft.JSInterop;
using Xui.Experimental.Portable;
using Xui.Experimental.Web;

internal static class BrowserServiceChecks
{
    public static async Task<int> Run()
    {
        int assertions = 0;
        void Check(bool value)
        {
            if (!value) throw new InvalidOperationException("Browser service assertion failed.");
            assertions++;
        }
        using var module = new ServiceModule();
        var services = new BrowserPlatformServices(module);
        module.Response = """{"status":"Completed","hasValue":true,"value":"test text"}""";
        var completed = await services.ReadClipboardAsync().WaitAsync(TimeSpan.FromSeconds(5));
        Check(completed.Status == OperationStatus.Completed && completed.Value == "test text");
        module.Response = """{"status":"Completed","hasValue":true,"value":""}""";
        var empty = await services.ReadClipboardAsync().WaitAsync(TimeSpan.FromSeconds(5));
        Check(empty.Status == OperationStatus.Completed && empty.Value == "");
        foreach (string response in new[]
        {
            "null", "{}", """{"status":"Unexpected"}""", """{"status":"Completed","hasValue":false}""",
            """{"status":"Completed","hasValue":true,"value":null}""", """{"status":"Failed","error":""}""",
            """{"status":"Completed","hasValue":true,"value":42}"""
        })
        {
            module.Response = response;
            var result = await services.ReadClipboardAsync().WaitAsync(TimeSpan.FromSeconds(5));
            Check(result.Status == OperationStatus.Failed && result.Error is not null);
        }
        module.ThrowInterop = true;
        var failure = await services.ReadClipboardAsync().WaitAsync(TimeSpan.FromSeconds(5));
        Check(failure.Status == OperationStatus.Failed && failure.Error is JSException);
        module.ThrowInterop = false;
        using var canceled = new CancellationTokenSource();
        canceled.Cancel();
        int before = module.Calls;
        var pending = services.ReadClipboardAsync(canceled.Token);
        try { await pending.WaitAsync(TimeSpan.FromSeconds(5)); }
        catch (OperationCanceledException) when (pending.IsCanceled) { assertions++; }
        Check(pending.IsCanceled && module.Calls == before);
        Check(services.GetAvailability(ServiceCapability.OpenFile) == CapabilityAvailability.Unsupported && module.Calls == before);
        return assertions;
    }

    private sealed class ServiceModule : IJSInProcessObjectReference
    {
        public string Response { get; set; } = "{}";
        public int Calls { get; private set; }
        public bool ThrowInterop { get; set; }
        public TValue Invoke<TValue>(string identifier, params object?[]? args) =>
            throw new InvalidOperationException("This test must not invoke synchronous browser APIs.");
        public ValueTask<TValue> InvokeAsync<TValue>(string identifier, object?[]? args) =>
            InvokeAsync<TValue>(identifier, CancellationToken.None, args);
        public ValueTask<TValue> InvokeAsync<TValue>(string identifier, CancellationToken cancellationToken, object?[]? args)
        {
            Calls++;
            cancellationToken.ThrowIfCancellationRequested();
            if (ThrowInterop) throw new JSException("Intentional interop failure.");
            return ValueTask.FromResult(JsonSerializer.Deserialize<TValue>(Response)!);
        }
        public void Dispose() { }
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
}
