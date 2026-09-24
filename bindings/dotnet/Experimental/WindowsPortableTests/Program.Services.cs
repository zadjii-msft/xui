using System.ComponentModel;
using Xui.Experimental.Portable;
using Xui.Experimental.Windows;

internal static partial class Program
{
    private static void ServiceContracts()
    {
        var queue = new ServiceQueue();
        var bridge = new ServiceBridge();
        var services = new WindowsPlatformServices(queue, bridge);
        Check(services.GetAvailability(ServiceCapability.Clipboard) == CapabilityAvailability.Available &&
            services.GetAvailability(ServiceCapability.OpenUri) == CapabilityAvailability.Available &&
            services.GetAvailability(ServiceCapability.OpenFile) == CapabilityAvailability.Unsupported,
            "Windows service capabilities are incorrect.");
        Throws<ArgumentOutOfRangeException>(() => services.GetAvailability((ServiceCapability)999));
        Throws<ArgumentException>(() => services.WriteClipboardAsync("invalid\0text"));
        Throws<ArgumentException>(() => services.OpenUriAsync(new Uri("file:///C:/private.txt")));
        Throws<ArgumentException>(() => services.OpenUriAsync(new Uri("https://user:secret@example.test/")));
        Check(bridge.Calls == 0 && queue.Count == 0, "Invalid service requests reached native execution.");

        var write = services.WriteClipboardAsync("owned fixture");
        Check(!write.IsCompleted && bridge.Calls == 0, "A service operation bypassed UI dispatch.");
        queue.Run();
        Check(write.GetAwaiter().GetResult().Value && bridge.Text == "owned fixture", "Clipboard writes lost their result.");
        var read = services.ReadClipboardAsync();
        queue.Run();
        Check(read.GetAwaiter().GetResult().Value == "owned fixture", "Clipboard reads lost their exact text.");
        var uri = new Uri("https://example.test/portable");
        var open = services.OpenUriAsync(uri);
        queue.Run();
        Check(open.GetAwaiter().GetResult().Value && bridge.Launched == uri, "URI dispatch changed the validated target.");

        foreach (var (error, status) in new (Exception Error, OperationStatus Status)[]
        {
            (new Win32Exception(5), OperationStatus.Denied),
            (new Win32Exception(1223), OperationStatus.Cancelled),
            (new Win32Exception(1155), OperationStatus.Unsupported),
            (new NotSupportedException("format unavailable"), OperationStatus.Unsupported),
            (new IOException("native fixture failure"), OperationStatus.Failed)
        })
        {
            bridge.Failure = error;
            var failed = services.ReadClipboardAsync();
            queue.Run();
            var result = failed.GetAwaiter().GetResult();
            Check(result.Status == status, "A native service failure was misclassified.");
            Throws<InvalidOperationException>(() => _ = result.Value);
            if (status == OperationStatus.Failed) Check(ReferenceEquals(result.Error, error), "The original native failure was discarded.");
        }
        bridge.Failure = null;

        int before = bridge.Calls;
        using var canceled = new CancellationTokenSource();
        var pending = services.WriteClipboardAsync("must not run", canceled.Token);
        canceled.Cancel();
        queue.Run();
        Throws<OperationCanceledException>(() => pending.GetAwaiter().GetResult());
        Check(bridge.Calls == before, "An externally canceled operation still called the platform.");
        Throws<OperationCanceledException>(() => services.ReadClipboardAsync(canceled.Token).GetAwaiter().GetResult());
        Check(queue.Count == 0, "Pre-canceled work entered the native queue.");

        var closing = services.ReadClipboardAsync();
        var closed = new ObjectDisposedException("window");
        queue.Cancel(closed);
        var closingResult = closing.GetAwaiter().GetResult();
        Check(closingResult.Status == OperationStatus.Failed && ReferenceEquals(closingResult.Error, closed) &&
            bridge.Calls == before, "Window shutdown abandoned or executed accepted service work.");

        queue.Rejection = new InvalidOperationException("dispatcher unavailable");
        var rejected = services.ReadClipboardAsync().GetAwaiter().GetResult();
        Check(rejected.Status == OperationStatus.Failed && ReferenceEquals(rejected.Error, queue.Rejection),
            "Dispatcher rejection lost its explicit failure.");
        queue.Rejection = null;
        queue.HasAccess = false;
        var wrongThread = services.ReadClipboardAsync();
        queue.Run();
        Check(wrongThread.GetAwaiter().GetResult().Status == OperationStatus.Failed && bridge.Calls == before,
            "Wrong-thread delivery reached the native service bridge.");
        queue.HasAccess = true;

        using var inFlight = new CancellationTokenSource();
        bridge.DuringOperation = inFlight.Cancel;
        var started = services.WriteClipboardAsync("already started", inFlight.Token);
        queue.Run();
        Throws<OperationCanceledException>(() => started.GetAwaiter().GetResult());
        Check(bridge.Text == "already started" && bridge.Calls == before + 1,
            "Cancellation incorrectly claimed to roll back an in-flight native side effect.");
    }

    private sealed class ServiceQueue : ICancellableUiDispatcher
    {
        private readonly Queue<(Action Run, Action<Exception> Cancel)> actions = [];
        internal int Count => actions.Count;
        internal Exception? Rejection;
        internal bool HasAccess = true;
        public bool CheckAccess() => HasAccess;
        public void Post(Action action) => Post(action, error => throw error);
        public void Post(Action action, Action<Exception> canceled)
        {
            if (Rejection is not null) throw Rejection;
            actions.Enqueue((action, canceled));
        }
        internal void Run() { while (actions.TryDequeue(out var action)) action.Run(); }
        internal void Cancel(Exception error) { while (actions.TryDequeue(out var action)) action.Cancel(error); }
    }

    private sealed class ServiceBridge : IWindowsServiceBridge
    {
        internal int Calls;
        internal string Text = "";
        internal Uri? Launched;
        internal Exception? Failure;
        internal Action? DuringOperation;
        private void Call() { Calls++; if (Failure is not null) throw Failure; DuringOperation?.Invoke(); }
        public string ReadClipboard() { Call(); return Text; }
        public void WriteClipboard(string text) { Call(); Text = text; }
        public void OpenUri(Uri uri) { Call(); Launched = uri; }
    }
}
