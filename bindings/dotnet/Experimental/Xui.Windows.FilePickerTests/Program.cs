using Xui.Experimental.Portable;
using Xui.Experimental.Windows;

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
    Check(task.IsCanceled, "External cancellation is a canceled task.");
}
var errors = new List<Exception>();
var bridge = new Bridge();
using var picker = new WindowsFilePicker(bridge, errors.Add);
Check(picker.GetAvailability(ServiceCapability.OpenFile) == CapabilityAvailability.Available, "Native open capability.");
Check(picker.GetAvailability(ServiceCapability.SaveFile) == CapabilityAvailability.Unsupported, "Save is explicitly unsupported.");
Throws<ArgumentOutOfRangeException>(() => picker.GetAvailability((ServiceCapability)999));
Throws<ArgumentNullException>(() => picker.OpenAsync(null!));
bridge.Access = false;
Throws<InvalidOperationException>(() => picker.OpenAsync(new(3)));
bridge.Access = true;
using (var cancel = new CancellationTokenSource())
{
    cancel.Cancel();
    Canceled(picker.OpenAsync(new(3), cancel.Token));
    Check(bridge.Posts == 0, "Pre-cancel does not queue a picker.");
}
var success = picker.OpenAsync(new(3));
Check(!success.IsCompleted && bridge.Choices == 0, "Dialog opens only after the unscoped dispatcher callback.");
Check(picker.OpenAsync(new(3)).Result.Status == OperationStatus.Failed, "Concurrent request is rejected.");
bridge.Drain();
using (var file = success.Result.Value)
{
    Check(file.DisplayName == "synthetic.txt" && file.Length == 3, "Native path is reduced to a display label.");
    byte[] buffer = new byte[3];
    Check(file.Content.Read(buffer) == 3 && buffer.SequenceEqual(new byte[] { 1, 2, 3 }), "Stream is readable and bounded.");
}
Check(bridge.LastStream!.Disposals == 1, "Successful stream belongs to caller.");
bridge.Path = null;
var dismiss = picker.OpenAsync(new(3));
bridge.Drain();
Check(dismiss.Result.Status == OperationStatus.Cancelled && !dismiss.IsCanceled, "Native dismissal is distinct from token cancellation.");
bridge.Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "xui-picker-fixture", "synthetic.txt");
using (var cancel = new CancellationTokenSource())
{
    var canceled = picker.OpenAsync(new(3), cancel.Token);
    cancel.Cancel();
    Canceled(canceled);
    int choices = bridge.Choices;
    Check(picker.OpenAsync(new(3)).Result.Status == OperationStatus.Failed, "Canceled request retains pending slot until dispatched cleanup.");
    bridge.Drain();
    Check(bridge.Choices == choices, "Canceled queued request never opens UI.");
}
using (var cancel = new CancellationTokenSource())
{
    bridge.DuringChoice = cancel.Cancel;
    int opens = bridge.Opens;
    var canceled = picker.OpenAsync(new(3), cancel.Token);
    bridge.Drain();
    Canceled(canceled);
    Check(bridge.Opens == opens, "Cancellation during modal selection never opens the selected file.");
    bridge.DuringChoice = null;
}
using (var cancel = new CancellationTokenSource())
{
    bridge.DuringOpen = cancel.Cancel;
    var canceled = picker.OpenAsync(new(3), cancel.Token);
    bridge.Drain();
    Canceled(canceled);
    Check(bridge.LastStream!.Disposals == 1, "Cancellation racing with file opening releases the late stream.");
    bridge.DuringOpen = null;
}
bridge.DuringChoice = () => Check(picker.OpenAsync(new(3)).Result.Status == OperationStatus.Failed,
    "Modal reentrant selection is rejected.");
var reentrant = picker.OpenAsync(new(3));
bridge.Drain();
reentrant.Result.Value.Dispose();
bridge.DuringChoice = null;
foreach (var error in new Exception[] { new UnauthorizedAccessException(), new IOException("provider"), new InvalidOperationException("dialog") })
{
    bridge.OpenError = error;
    var failed = picker.OpenAsync(new(3));
    bridge.Drain();
    Check(failed.Result.Status == (error is UnauthorizedAccessException ? OperationStatus.Denied : OperationStatus.Failed),
        "Native errors preserve their explicit result category.");
}
bridge.OpenError = null;
var oversized = picker.OpenAsync(new(2));
bridge.Drain();
Check(oversized.Result.Error is FileSelectionTooLargeException && bridge.LastStream!.Disposals == 1,
    "Known oversize failure closes its opened stream.");
bridge.Reject = true;
Check(picker.OpenAsync(new(3)).Result.Status == OperationStatus.Failed, "Dispatcher rejection is explicit.");
bridge.Reject = false;
var closed = picker.OpenAsync(new(3));
bridge.CancelQueue();
Check(closed.Result.Status == OperationStatus.Failed, "Window-close queue cancellation cannot strand the task.");
var disposed = picker.OpenAsync(new(3));
picker.Dispose();
Canceled(disposed);
bridge.Drain();
Throws<ObjectDisposedException>(() => picker.OpenAsync(new(3)));
Check(errors.Count == 0, "Expected protocol results do not silently log unexpected errors.");
Console.WriteLine($"Windows file picker protocol: {assertions} assertions passed. No OS picker, HWND, or user file was accessed.");

sealed class Bridge : IWindowsFilePickerBridge
{
    public bool Access { get; set; } = true;
    public bool IsAvailable { get; set; } = true;
    public bool Reject { get; set; }
    public int Posts { get; private set; }
    public int Choices { get; private set; }
    public int Opens { get; private set; }
    public string? Path { get; set; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "xui-picker-fixture", "synthetic.txt");
    public Action? DuringChoice { get; set; }
    public Action? DuringOpen { get; set; }
    public Exception? OpenError { get; set; }
    public Tracked? LastStream { get; private set; }
    private (Action Run, Action<Exception> Cancel)? queued;
    public bool CheckAccess() => Access;
    public void Post(Action action, Action<Exception> canceled)
    {
        if (Reject) throw new ObjectDisposedException("dispatcher");
        Posts++;
        queued = (action, canceled);
    }
    public void Drain()
    {
        var item = queued!.Value;
        queued = null;
        item.Run();
    }
    public void CancelQueue()
    {
        var item = queued!.Value;
        queued = null;
        item.Cancel(new ObjectDisposedException("window"));
    }
    public string? ChooseFile() { Choices++; DuringChoice?.Invoke(); return Path; }
    public Stream OpenRead(string path)
    {
        Opens++;
        if (OpenError is not null) throw OpenError;
        LastStream = new Tracked();
        DuringOpen?.Invoke();
        return LastStream;
    }
}
sealed class Tracked : MemoryStream
{
    public int Disposals { get; private set; }
    public Tracked() : base([1, 2, 3]) { }
    protected override void Dispose(bool disposing)
    {
        if (disposing) Disposals++;
        base.Dispose(disposing);
    }
}
