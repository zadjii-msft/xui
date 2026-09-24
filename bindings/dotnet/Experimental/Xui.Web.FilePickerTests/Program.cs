using Microsoft.JSInterop;
using Xui.Experimental.Portable;
using Xui.Experimental.Web;

int assertions = 0;
void Check(bool value, string message)
{
    if (!value) throw new InvalidOperationException(message);
    assertions++;
}
void Throws<T>(Action action) where T : Exception
{
    try { action(); }
    catch (T) { assertions++; return; }
    throw new InvalidOperationException($"Expected {typeof(T).Name}.");
}
void Canceled(Task task)
{
    Throws<OperationCanceledException>(() => task.GetAwaiter().GetResult());
    Check(task.IsCanceled, "External token cancellation is a canceled task.");
}
var dispatcher = new Dispatcher();
var module = new Module();
var errors = new List<Exception>();
using var picker = new BrowserFilePicker(module, dispatcher, errors.Add);
Check(picker.GetAvailability(ServiceCapability.OpenFile) == CapabilityAvailability.RequiresUserGesture, "Browser open requires a user gesture.");
Check(picker.GetAvailability(ServiceCapability.SaveFile) == CapabilityAvailability.Unsupported, "Save unsupported.");
Throws<ArgumentNullException>(() => picker.OpenAsync(null!));
Throws<ArgumentOutOfRangeException>(() => picker.GetAvailability((ServiceCapability)99));
dispatcher.Access = false;
Throws<InvalidOperationException>(() => picker.OpenAsync(new(3)));
dispatcher.Access = true;
Canceled(picker.OpenAsync(new(3), new CancellationToken(true)));
Check(module.Creates == 0, "Pre-cancel never creates a JS handle.");
var success = picker.OpenAsync(new(3));
var handle = module.Last!;
Check(handle.Shows == 1 && !success.IsCompleted, "Native show occurs synchronously before OpenAsync returns.");
Check(picker.OpenAsync(new(3)).Result.Status == OperationStatus.Failed, "Concurrent selection rejected.");
handle.Receive("completed", "../opaque.txt", 3);
using (var file = success.Result.Value)
{
    Check(file.DisplayName == "../opaque.txt", "Metadata is not a path.");
    Throws<NotSupportedException>(() => file.Content.ReadByte());
    var bytes = new byte[3];
    Check(await file.Content.ReadAsync(bytes) == 3 && bytes.SequenceEqual(new byte[] { 1, 2, 3 }), "Real managed interop stream consumes bounded chunks.");
    Check(await file.Content.ReadAsync(bytes) == 0 && handle.Reads == 2, "Exact budget includes EOF probe.");
}
Check(handle.Releases == 1 && handle.Disposals == 1, "Caller releases selected JS File and object handle once.");
foreach (var (status, expected) in new[]
{
    ("cancelled", OperationStatus.Cancelled), ("denied", OperationStatus.Denied),
    ("unsupported", OperationStatus.Unsupported), ("failed", OperationStatus.Failed), ("oversized", OperationStatus.Failed)
})
{
    var task = picker.OpenAsync(new(3));
    handle = module.Last!;
    handle.Receive(status, error: "native failure");
    Check(task.Result.Status == expected && !task.IsCanceled, "Native result category is distinct from external cancellation.");
    Check(handle.Releases == 1 && handle.Disposals == 1, "Unsuccessful selection releases JS resources.");
}
var tooLarge = picker.OpenAsync(new(2));
handle = module.Last!;
handle.Receive("completed", "oversized.txt", 3);
Check(tooLarge.Result.Error is FileSelectionTooLargeException && handle.Releases == 1, "Managed boundary rejects lying or oversized metadata.");
using (var cancel = new CancellationTokenSource())
{
    var task = picker.OpenAsync(new(3), cancel.Token);
    handle = module.Last!;
    var receiver = handle.Receiver!;
    cancel.Cancel();
    Canceled(task);
    Check(handle.Releases == 1 && handle.Disposals == 1, "Cancellation releases native listeners and interop handles.");
    receiver.Receive("completed", "late.txt", 3, "");
    Check(handle.Releases == 1, "Late managed callback cannot resurrect or double-release a selected file.");
}
using (var cancel = new CancellationTokenSource())
{
    var task = picker.OpenAsync(new(3), cancel.Token);
    handle = module.Last!;
    dispatcher.Access = false;
    cancel.Cancel();
    Canceled(task);
    Check(handle.Releases == 0, "Off-thread cancellation queues JS cleanup instead of invoking off-thread.");
    dispatcher.Access = true;
    dispatcher.Drain();
    Check(handle.Releases == 1, "Queued cleanup releases the JS reference.");
}
using (var cancel = new CancellationTokenSource())
{
    var task = picker.OpenAsync(new(3), cancel.Token);
    handle = module.Last!;
    handle.ReleaseError = new JSException("release failed");
    cancel.Cancel();
    Canceled(task);
    Check(errors.Count == 1 && errors[0].Message == "release failed" && handle.Disposals == 1,
        "Late cleanup failure is reported, not hidden behind a canceled task.");
}
var brokenShow = picker.OpenAsync(new(3));
module.Last!.Receive("unknown");
Check(brokenShow.Result.Status == OperationStatus.Failed, "Unknown protocol result is not success.");
var disposed = picker.OpenAsync(new(3));
handle = module.Last!;
picker.Dispose();
Canceled(disposed);
Check(handle.Releases == 1 && handle.Disposals == 1 && module.Disposals == 0,
    "Picker disposes pending resources but never the caller-owned module.");
Throws<ObjectDisposedException>(() => picker.OpenAsync(new(3)));
Console.WriteLine($"Browser file picker interop: {assertions} assertions passed. No browser picker UI or user file was accessed.");

sealed class Dispatcher : IUiDispatcher
{
    public bool Access { get; set; } = true;
    private readonly Queue<Action> queue = new();
    public bool CheckAccess() => Access;
    public void Post(Action action) => queue.Enqueue(action);
    public void Drain() { while (queue.TryDequeue(out var action)) action(); }
}
sealed class Module : IJSInProcessObjectReference
{
    public Handle? Last { get; private set; }
    public int Creates { get; private set; }
    public int Disposals { get; private set; }
    public TValue Invoke<TValue>(string identifier, params object?[]? args)
    {
        if (identifier != "createFilePicker") throw new InvalidOperationException(identifier);
        Creates++;
        Last = new Handle(((DotNetObjectReference<BrowserFilePicker.Selection>)args![0]!).Value);
        return (TValue)(object)Last;
    }
    public ValueTask<TValue> InvokeAsync<TValue>(string identifier, object?[]? args) => throw new NotSupportedException();
    public ValueTask<TValue> InvokeAsync<TValue>(string identifier, CancellationToken cancellationToken, object?[]? args) => throw new NotSupportedException();
    public void Dispose() => Disposals++;
    public ValueTask DisposeAsync() { Dispose(); return ValueTask.CompletedTask; }
}
sealed class Handle(BrowserFilePicker.Selection receiver) : IJSInProcessObjectReference
{
    public BrowserFilePicker.Selection? Receiver { get; } = receiver;
    public int Shows { get; private set; }
    public int Reads { get; private set; }
    public int Releases { get; private set; }
    public int Disposals { get; private set; }
    public Exception? ReleaseError { get; set; }
    private int position;
    public void Receive(string status, string name = "", long length = 0, string error = "") => Receiver!.Receive(status, name, length, error);
    public TValue Invoke<TValue>(string identifier, params object?[]? args)
    {
        if (identifier == "show") Shows++;
        else if (identifier == "release") { Releases++; if (ReleaseError is not null) throw ReleaseError; }
        else throw new InvalidOperationException(identifier);
        return default!;
    }
    public ValueTask<TValue> InvokeAsync<TValue>(string identifier, object?[]? args) => InvokeAsync<TValue>(identifier, default, args);
    public ValueTask<TValue> InvokeAsync<TValue>(string identifier, CancellationToken cancellationToken, object?[]? args)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (identifier != "read") throw new InvalidOperationException(identifier);
        Reads++;
        int count = Math.Min((int)args![0]!, 3 - position);
        byte[] bytes = Enumerable.Range(position + 1, count).Select(n => (byte)n).ToArray();
        position += count;
        return ValueTask.FromResult((TValue)(object)bytes);
    }
    public void Dispose() => Disposals++;
    public ValueTask DisposeAsync() { Dispose(); return ValueTask.CompletedTask; }
}
