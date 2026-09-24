using System.Collections.Concurrent;
using Microsoft.JSInterop;
using Xui.Experimental.Web;

internal static class BrowserErrorChecks
{
    public static int Run()
    {
        int assertions = 0;
        void Check(bool condition)
        {
            if (!condition) throw new InvalidOperationException("Browser error reporter assertion failed.");
            assertions++;
        }
        var originalContext = SynchronizationContext.Current;
        var originalError = Console.Error;
        var context = new ErrorContext();
        using var output = new StringWriter();
        using var module = new ErrorModule();
        try
        {
            Console.SetError(output);
            SynchronizationContext.SetSynchronizationContext(context);
            using var reporter = new BrowserErrorReporter(module, "errors");
            reporter.Report(new InvalidOperationException("UI failure"));
            Check(module.Calls == 1 && output.ToString().Contains("UI failure"));
            Check(reporter.LastError?.Message == "UI failure");
            Task.Run(() => reporter.Report(new InvalidOperationException("worker failure"))).GetAwaiter().GetResult();
            Check(module.Calls == 1);
            context.Drain();
            Check(module.Calls == 2 && module.Message.Contains("worker failure"));
            module.Fail = true;
            reporter.Report(new InvalidOperationException("render failure"));
            Check(reporter.LastError is AggregateException aggregate && aggregate.InnerExceptions.Count == 2);
            Check(output.ToString().Contains("Intentional DOM reporter failure"));
            module.Fail = false;
            Console.SetError(new BrokenWriter());
            reporter.Report(new InvalidOperationException("console failure"));
            Check(reporter.LastError is AggregateException logFailure && logFailure.InnerExceptions[1] is IOException);
            Check(module.Message.Contains("Console sink unavailable."));
            Console.SetError(output);
            context.Reject = true;
            Task.Run(() => reporter.Report(new InvalidOperationException("dispatch failure"))).GetAwaiter().GetResult();
            Check(reporter.LastError is AggregateException rejected && rejected.InnerExceptions[1].Message == "Error dispatcher unavailable.");
            Check(output.ToString().Contains("Error dispatcher unavailable."));
            int before = module.Calls;
            reporter.Dispose();
            reporter.Report(new InvalidOperationException("late failure"));
            Check(module.Calls == before && output.ToString().Contains("late failure"));
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(originalContext);
            Console.SetError(originalError);
        }
        return assertions;
    }

    private sealed class ErrorContext : SynchronizationContext
    {
        private readonly ConcurrentQueue<Action> actions = new();
        public bool Reject { get; set; }
        public override void Post(SendOrPostCallback callback, object? state)
        {
            if (Reject) throw new InvalidOperationException("Error dispatcher unavailable.");
            actions.Enqueue(() => callback(state));
        }
        public void Drain() { while (actions.TryDequeue(out var action)) action(); }
    }
    private sealed class BrokenWriter : StringWriter
    {
        public override void WriteLine(string? value) => throw new IOException("Console sink unavailable.");
    }
    private sealed class ErrorModule : IJSInProcessObjectReference
    {
        public int Calls { get; private set; }
        public string Message { get; private set; } = "";
        public bool Fail { get; set; }
        public TValue Invoke<TValue>(string identifier, params object?[]? args)
        {
            Calls++;
            if (Fail) throw new JSException("Intentional DOM reporter failure");
            Message = (string)args![1]!;
            return default!;
        }
        public ValueTask<TValue> InvokeAsync<TValue>(string identifier, object?[]? args) => throw new NotSupportedException();
        public ValueTask<TValue> InvokeAsync<TValue>(string identifier, CancellationToken cancellationToken, object?[]? args) => throw new NotSupportedException();
        public void Dispose() { }
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
}
