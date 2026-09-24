using Xui.Experimental.Android;
using Xui.Experimental.Portable;

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
void Canceled(Task task) { Throws<OperationCanceledException>(() => task.GetAwaiter().GetResult()); Check(task.IsCanceled, "Task cancellation."); }
var errors = new List<Exception>();
var bridge = new Bridge();
using var picker = new FileSelectionAndroidProtocol(bridge, errors.Add);
Check(picker.GetAvailability(ServiceCapability.OpenFile) == CapabilityAvailability.Available, "SAF open capability.");
Check(picker.GetAvailability(ServiceCapability.SaveFile) == CapabilityAvailability.Unsupported, "No save capability.");
Throws<ArgumentNullException>(() => picker.OpenAsync(null!));
Throws<ArgumentOutOfRangeException>(() => picker.GetAvailability((ServiceCapability)99));
bridge.Access = false;
Throws<InvalidOperationException>(() => picker.OpenAsync(new(3)));
bridge.Access = true;
Canceled(picker.OpenAsync(new(3), new CancellationToken(true)));
Check(bridge.Launches == 0, "Pre-cancel never starts an Activity.");
Check(!picker.HandleResult(_ => throw new Exception()), "Unsolicited results are ignored without reading data.");
var pending = picker.OpenAsync(new(3));
Check(!picker.HandleResult(_ => throw new Exception()), "Result before actual launch cannot consume a pending request.");
bridge.Drain();
Check(!pending.IsCompleted && bridge.Launches == 1, "Launch waits for native Activity result.");
using var stream = new Tracked();
Check(picker.HandleResult(options => OperationResult<PickedFile>.Completed(new("../../opaque", stream, options, 3))), "Expected result consumed.");
using (var file = pending.Result.Value)
    Check(file.DisplayName == "../../opaque" && file.Content.ReadByte() == 1, "Metadata is not a filesystem path.");
Check(stream.Disposals == 1, "Caller owns stream.");
using (var cancel = new CancellationTokenSource())
{
    var task = picker.OpenAsync(new(3), cancel.Token);
    bridge.Drain();
    cancel.Cancel();
    Canceled(task);
    Check(picker.OpenAsync(new(3)).Result.Status == OperationStatus.Failed, "Canceled native request reserves slot until its result.");
    int reads = 0;
    picker.HandleResult(_ => { reads++; throw new Exception(); });
    Check(reads == 0, "Late canceled content URI is never queried or read.");
}
using (var cancel = new CancellationTokenSource())
{
    var task = picker.OpenAsync(new(3), cancel.Token);
    cancel.Cancel();
    bridge.Drain();
    Canceled(task);
    Check(!picker.HandleResult(_ => throw new Exception()), "Canceled unlaunched request releases its slot.");
}
using (var cancel = new CancellationTokenSource())
{
    var task = picker.OpenAsync(new(3), cancel.Token);
    bridge.Drain();
    var late = new Tracked();
    picker.HandleResult(options =>
    {
        cancel.Cancel();
        return OperationResult<PickedFile>.Completed(new("late", late, options));
    });
    Canceled(task);
    Check(late.Disposals == 1, "Result construction cancellation releases newly-created stream.");
}
foreach (var status in new[] { OperationStatus.Cancelled, OperationStatus.Denied, OperationStatus.Unsupported, OperationStatus.Failed })
{
    var task = picker.OpenAsync(new(3));
    bridge.Drain();
    picker.HandleResult(_ => status switch
    {
        OperationStatus.Cancelled => OperationResult<PickedFile>.Cancelled(),
        OperationStatus.Denied => throw new UnauthorizedAccessException(),
        OperationStatus.Unsupported => OperationResult<PickedFile>.Unsupported(),
        _ => throw new IOException("provider unavailable")
    });
    Check(task.Result.Status == status && !task.IsCanceled, "Native result and error categories remain explicit.");
}
bridge.LaunchError = new UnauthorizedAccessException();
var denied = picker.OpenAsync(new(3));
bridge.Drain();
Check(denied.Result.Status == OperationStatus.Denied, "Security denial launching SAF is explicit.");
bridge.LaunchError = null;
bridge.LaunchError = new NotSupportedException("No handler");
var unsupported = picker.OpenAsync(new(3));
bridge.Drain();
Check(unsupported.Result.Status == OperationStatus.Unsupported, "Missing document picker is explicitly unsupported.");
bridge.LaunchError = null;
var rejected = picker.OpenAsync(new(3));
bridge.Cancel();
Check(rejected.Result.Status == OperationStatus.Failed, "Rejected dispatcher callback completes.");
bridge.IsAvailable = false;
Check(picker.OpenAsync(new(3)).Result.Status == OperationStatus.Failed, "Missing focused Activity fails before launch.");
bridge.IsAvailable = true;
var destroyed = picker.OpenAsync(new(3));
bridge.Drain();
picker.Dispose();
Canceled(destroyed);
Check(picker.HandleResult(_ => throw new Exception()), "Disposed Activity consumes its late result without opening content.");
Throws<ObjectDisposedException>(() => picker.OpenAsync(new(3)));
Check(errors.Count == 0, "No unexpected cleanup failures.");
Console.WriteLine($"Android file picker protocol: {assertions} assertions passed. No Activity, picker, URI provider or user file was accessed.");

sealed class Bridge : IAndroidFilePickerBridge
{
    public bool Access { get; set; } = true;
    public bool IsAvailable { get; set; } = true;
    public int Launches { get; private set; }
    public Exception? LaunchError { get; set; }
    private (Action Run, Action<Exception> Cancel)? queued;
    public bool CheckAccess() => Access;
    public void Post(Action action, Action<Exception> canceled) => queued = (action, canceled);
    public void Drain() { var next = queued!.Value; queued = null; next.Run(); }
    public void Cancel() { var next = queued!.Value; queued = null; next.Cancel(new ObjectDisposedException("Activity")); }
    public void Launch() { Launches++; if (LaunchError is not null) throw LaunchError; }
}
sealed class Tracked : MemoryStream
{
    public int Disposals { get; private set; }
    public Tracked() : base([1, 2, 3]) { }
    protected override void Dispose(bool disposing) { if (disposing) Disposals++; base.Dispose(disposing); }
}
