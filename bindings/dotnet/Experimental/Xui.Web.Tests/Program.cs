using Xui.Experimental.Portable;
using Xui.Experimental.Web;

int assertions = 0;
void Assert(bool condition)
{
    if (!condition) throw new Exception("Browser dispatcher assertion failed.");
    assertions++;
}
void Fails<T>(Task task) where T : Exception
{
    try { task.WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult(); }
    catch (T) { assertions++; return; }
    throw new Exception($"Expected {typeof(T).Name}.");
}

var original = SynchronizationContext.Current;
try
{
    var context = new QueueContext();
    SynchronizationContext.SetSynchronizationContext(context);
    var errors = new List<Exception>();
    var dispatcher = new BrowserDispatcher(errors.Add);
    using var host = new Host(dispatcher);
    Assert(dispatcher.CheckAccess());
    int calls = 0;
    var success = host.DispatchAsync(() => calls++);
    Assert(!success.IsCompleted && calls == 0);
    context.Run();
    success.WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult();
    Assert(calls == 1);
    var authoredError = host.DispatchAsync(() => throw new ApplicationException("authored"));
    context.Run();
    Fails<ApplicationException>(authoredError);
    Assert(errors.Count == 0);
    var wrongThread = host.DispatchAsync(() => calls++);
    Task.Run(context.Run).GetAwaiter().GetResult();
    Fails<InvalidOperationException>(wrongThread);
    Assert(calls == 1 && errors.Count == 0);
    dispatcher.Post(() => throw new ApplicationException("raw callback"));
    context.Run();
    Assert(errors.Count == 1 && errors[0].Message == "raw callback");
    var afterDisposal = host.DispatchAsync(() => calls++);
    host.Dispose();
    context.Run();
    Fails<ObjectDisposedException>(afterDisposal);
    Assert(calls == 1);

    SynchronizationContext.SetSynchronizationContext(new RejectingContext());
    using var rejectedHost = new Host(new BrowserDispatcher(errors.Add));
    Fails<InvalidOperationException>(rejectedHost.DispatchAsync(() => calls++));
    Assert(calls == 1);

    SynchronizationContext.SetSynchronizationContext(null);
    using var fallbackHost = new Host(new BrowserDispatcher(errors.Add));
    // Desktop ThreadPool delivery is not the browser UI thread. The task must fault, not hang.
    Fails<InvalidOperationException>(fallbackHost.DispatchAsync(() => calls++));
    Assert(calls == 1);
}
finally { SynchronizationContext.SetSynchronizationContext(original); }
Console.WriteLine($"Browser dispatcher: {assertions} assertions passed.");

sealed class QueueContext : SynchronizationContext
{
    private readonly Queue<Action> queue = new();
    public override void Post(SendOrPostCallback callback, object? state) => queue.Enqueue(() => callback(state));
    public void Run() => queue.Dequeue()();
}

sealed class RejectingContext : SynchronizationContext
{
    public override void Post(SendOrPostCallback callback, object? state) =>
        throw new InvalidOperationException("Dispatch unavailable.");
}
