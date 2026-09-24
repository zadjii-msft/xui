using System.Diagnostics;

namespace Xui.Experimental.Android;

internal enum NativePeerOperation { Create, Dispose, Insert, Remove, Move, Metadata, Update, InputConstruction, InitialProperties, Listeners }

internal sealed class NativePeerMetrics
{
    private readonly long[] ticks = new long[10];
    internal void Add(NativePeerOperation operation, long elapsed) => ticks[(int)operation] += elapsed;
    internal double[] Milliseconds() => ticks.Select(value => value * 1000.0 / Stopwatch.Frequency).ToArray();
}

internal readonly struct NativePeerTrace(NativePeerMetrics? metrics, NativePeerOperation operation) : IDisposable
{
    private readonly long started = metrics is null ? 0 : Stopwatch.GetTimestamp();
    public void Dispose()
    {
        if (metrics is not null) metrics.Add(operation, Stopwatch.GetTimestamp() - started);
    }
}
