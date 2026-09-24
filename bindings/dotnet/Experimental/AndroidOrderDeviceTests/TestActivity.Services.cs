using Android.Util;
using Xui.Experimental.Android;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private async Task ServiceChecks()
    {
        int before = assertions;
        Assert(!HasWindowFocus, "Native service denial checks run before this Activity receives window focus.");
        using (var native = new AndroidPlatformServices(this, new AndroidDispatcher()))
        {
            Assert(native.GetAvailability(ServiceCapability.Clipboard) == CapabilityAvailability.Available &&
                native.GetAvailability(ServiceCapability.OpenUri) == CapabilityAvailability.Available,
                "Native clipboard and URI availability are explicit and advisory.");
            Assert((await native.ReadClipboardAsync()).Status == OperationStatus.Denied &&
                (await native.WriteClipboardAsync("must not reach the native clipboard")).Status == OperationStatus.Denied &&
                (await native.OpenUriAsync(new Uri("https://example.invalid/xui-test"))).Status == OperationStatus.Denied,
                "An unfocused Activity rejects native services before any clipboard access or external launch.");
        }

        var dispatcher = new ServiceDispatcher();
        var bridge = new ServiceBridge();
        using var services = new AndroidPlatformServices(bridge, dispatcher);
        Assert(new[] { ServiceCapability.OpenFile, ServiceCapability.SaveFile, ServiceCapability.ApplicationStorage }
            .All(capability => services.GetAvailability(capability) == CapabilityAvailability.Unsupported),
            "Unimplemented Android file and storage services stay unsupported.");
        bool rejected = false;
        try { services.GetAvailability((ServiceCapability)99); }
        catch (ArgumentOutOfRangeException) { rejected = true; }
        Assert(rejected, "Unknown service capabilities are rejected.");
        foreach (string value in new[] { "file:///tmp/private", "javascript:alert(1)", "https://user:secret@example.test/" })
        {
            rejected = false;
            try { await services.OpenUriAsync(new Uri(value)); }
            catch (ArgumentException) { rejected = true; }
            Assert(rejected && bridge.Calls == 0, "Unsupported URI policy fails before the native bridge.");
        }
        rejected = false;
        try { await services.WriteClipboardAsync("bad\0text"); }
        catch (ArgumentException) { rejected = true; }
        Assert(rejected && bridge.Calls == 0, "Invalid clipboard text fails before the native bridge.");

        Assert((await services.ReadClipboardAsync()).Value == "fake clipboard", "The isolated clipboard bridge returns exact text.");
        Assert((await services.WriteClipboardAsync("")).Value && (await services.ReadClipboardAsync()).Value == "",
            "Explicitly empty clipboard text roundtrips through the isolated bridge as completed.");
        string text = "  Ada \u674e  ";
        Assert((await services.WriteClipboardAsync(text)).Value && bridge.Written == text,
            "The isolated clipboard bridge preserves whitespace and Unicode.");
        var uri = new Uri("https://example.test/path");
        Assert((await services.OpenUriAsync(uri)).Value && bridge.Launched == uri,
            "The isolated launch bridge receives the validated URI.");
        int calls = bridge.Calls;
        bridge.HasFocus = false;
        Assert((await services.ReadClipboardAsync()).Status == OperationStatus.Denied &&
            (await services.WriteClipboardAsync("ignored")).Status == OperationStatus.Denied &&
            (await services.OpenUriAsync(uri)).Status == OperationStatus.Denied && bridge.Calls == calls,
            "Focus loss rejects every service without invoking its bridge.");
        bridge.HasFocus = true;

        using var cancellation = new CancellationTokenSource();
        dispatcher.OnUiThread = false;
        Task<OperationResult<string>> canceled = services.ReadClipboardAsync(cancellation.Token);
        cancellation.Cancel();
        dispatcher.OnUiThread = true;
        dispatcher.Drain();
        bool wasCanceled = false;
        try { await canceled; }
        catch (OperationCanceledException) { wasCanceled = true; }
        Assert(wasCanceled && bridge.Calls == calls, "Cancellation before UI delivery prevents clipboard access.");

        dispatcher.OnUiThread = false;
        var pending = services.WriteClipboardAsync("late");
        dispatcher.OnUiThread = true;
        services.Dispose();
        bool disposed = false;
        try { await pending; }
        catch (ObjectDisposedException) { disposed = true; }
        dispatcher.Drain();
        Assert(disposed && bridge.Calls == calls, "Service disposal faults accepted work without waiting for the UI queue.");
        disposed = false;
        try { await services.ReadClipboardAsync(); }
        catch (ObjectDisposedException) { disposed = true; }
        Assert(disposed, "Disposed services cannot access a later Activity.");
        services.Dispose();

        using var failed = new AndroidPlatformServices(bridge, dispatcher);
        bridge.IsAlive = false;
        disposed = false;
        try { await failed.OpenUriAsync(uri); }
        catch (ObjectDisposedException) { disposed = true; }
        Assert(disposed && bridge.Calls == calls, "An expired Activity prevents external operations.");
        Log.Info("Xui.Android.Orders", $"Platform services: {assertions - before} assertions; no real clipboard read/write or URI launch performed.");
    }

    private sealed class ServiceDispatcher : IUiDispatcher
    {
        private readonly Queue<Action> queue = new();
        internal bool OnUiThread { get; set; } = true;
        public bool CheckAccess() => OnUiThread;
        public void Post(Action action) => queue.Enqueue(action);
        internal void Drain()
        {
            while (queue.TryDequeue(out var action)) action();
        }
    }

    private sealed class ServiceBridge : IAndroidServiceBridge
    {
        public bool IsAlive { get; set; } = true;
        public bool HasFocus { get; set; } = true;
        internal int Calls { get; private set; }
        internal string ReadText { get; set; } = "fake clipboard";
        internal string? Written { get; private set; }
        internal Uri? Launched { get; private set; }
        public OperationResult<string> ReadClipboard() { Calls++; return OperationResult<string>.Completed(ReadText); }
        public OperationResult<bool> WriteClipboard(string text) { Calls++; Written = text; ReadText = text; return OperationResult<bool>.Completed(true); }
        public OperationResult<bool> OpenUri(Uri uri) { Calls++; Launched = uri; return OperationResult<bool>.Completed(true); }
    }
}
