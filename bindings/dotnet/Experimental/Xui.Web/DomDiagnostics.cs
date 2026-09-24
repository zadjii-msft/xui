#if DEBUG
using System.Diagnostics;
using Microsoft.JSInterop;

namespace Xui.Experimental.Web;

public sealed class DomDiagnostics
{
    private readonly Dictionary<string, (int Count, double Milliseconds)> calls = [];
    private readonly Dictionary<string, double> marks = [];
    private long started;
    private long allocated;
    private int collections;
    public bool Enabled { get; set; }
    public void Start()
    {
        calls.Clear();
        marks.Clear();
        started = Stopwatch.GetTimestamp();
        allocated = GC.GetAllocatedBytesForCurrentThread();
        collections = GC.CollectionCount(0);
    }
    public void Mark(string name)
    {
        if (Enabled) marks[name] = Stopwatch.GetElapsedTime(started).TotalMilliseconds;
    }
    internal void Record(string name, long start)
    {
        var previous = calls.GetValueOrDefault(name);
        calls[name] = (previous.Count + 1, previous.Milliseconds + Stopwatch.GetElapsedTime(start).TotalMilliseconds);
    }
    public object Snapshot() => new
    {
        calls = calls.ToDictionary(entry => entry.Key, entry => new { count = entry.Value.Count, milliseconds = entry.Value.Milliseconds }),
        marks = new Dictionary<string, double>(marks),
        allocatedBytes = GC.GetAllocatedBytesForCurrentThread() - allocated,
        generation0Collections = GC.CollectionCount(0) - collections
    };
}

internal sealed class DiagnosticJsReference(IJSInProcessObjectReference inner, DomDiagnostics diagnostics) : IJSInProcessObjectReference
{
    public TValue Invoke<TValue>(string identifier, params object?[]? args)
    {
        long start = diagnostics.Enabled ? Stopwatch.GetTimestamp() : 0;
        try
        {
            var result = inner.Invoke<TValue>(identifier, args);
            return result is IJSInProcessObjectReference reference
                ? (TValue)(object)new DiagnosticJsReference(reference, diagnostics) : result;
        }
        finally { if (diagnostics.Enabled) diagnostics.Record(identifier, start); }
    }
    public ValueTask<TValue> InvokeAsync<TValue>(string identifier, object?[]? args) => inner.InvokeAsync<TValue>(identifier, args);
    public ValueTask<TValue> InvokeAsync<TValue>(string identifier, CancellationToken cancellationToken, object?[]? args) =>
        inner.InvokeAsync<TValue>(identifier, cancellationToken, args);
    public void Dispose() => inner.Dispose();
    public ValueTask DisposeAsync() => inner.DisposeAsync();
}
#endif
