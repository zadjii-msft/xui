using System.Globalization;
using Microsoft.JSInterop;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Web;

internal sealed class DomVirtualViewport : ISettledVirtualViewportLease
{
    private readonly IJSInProcessObjectReference native;
    private readonly DotNetObjectReference<DomVirtualViewport> reference;
    private Action<VirtualViewportRequest>? requested;
    private readonly Action reflow;
    private readonly Action beginLayout;
    private readonly Action endLayout;
    private readonly Action flushLayout;
    private readonly int thread = Environment.CurrentManagedThreadId;
    private bool disposed;
    internal bool IsDisposed => disposed;

    internal DomVirtualViewport(IJSInProcessObjectReference surface, int id, int count, float rowHeight,
        long sourceVersion, Action<VirtualViewportRequest> requested, Action beginLayout, Action endLayout, Action reflow, Action flushLayout)
    {
        ArgumentNullException.ThrowIfNull(requested);
        VirtualizationMath.Extent(count, rowHeight);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(sourceVersion);
        this.requested = requested;
        this.reflow = reflow;
        this.beginLayout = beginLayout;
        this.endLayout = endLayout;
        this.flushLayout = flushLayout;
        reference = DotNetObjectReference.Create(this);
        try { native = surface.Invoke<IJSInProcessObjectReference>("beginVirtualViewport", id, count, rowHeight, Format(sourceVersion), reference); }
        catch { reference.Dispose(); throw; }
    }

    private static string Format(long value) => value.ToString(CultureInfo.InvariantCulture);
    private void Verify()
    {
        if (Environment.CurrentManagedThreadId != thread) throw new InvalidOperationException("Virtual viewport operations require the browser UI thread.");
        ObjectDisposedException.ThrowIf(disposed, this);
    }

    [JSInvokable]
    public void Request(string epoch, string committedVersion, string requestedVersion,
        VirtualViewportRect committed, VirtualViewportRect next, bool blocked)
    {
        if (disposed) return;
        Verify();
        var request = new VirtualViewportRequest(long.Parse(epoch, CultureInfo.InvariantCulture),
            long.Parse(committedVersion, CultureInfo.InvariantCulture), long.Parse(requestedVersion, CultureInfo.InvariantCulture),
            committed, next, blocked);
        request.Validate();
        beginLayout();
        try { requested!(request); }
        finally { endLayout(); }
    }

    public void SetExtent(int itemCount, long sourceVersion)
    {
        Verify();
        native.InvokeVoid("setExtent", itemCount, Format(sourceVersion));
    }
    public void RequestOffset(float offset)
    {
        Verify();
        native.InvokeVoid("requestOffset", offset);
    }
    public VirtualViewportUpdateResult TryBeginUpdate(long expectedEpoch)
    {
        Verify();
        var result = native.Invoke<string>("tryBeginUpdate", Format(expectedEpoch)) switch
        {
            "Ready" => VirtualViewportUpdateResult.Ready,
            "Superseded" => VirtualViewportUpdateResult.Superseded,
            "Blocked" => VirtualViewportUpdateResult.Blocked,
            _ => throw new InvalidOperationException("Invalid native viewport update result.")
        };
        return result;
    }
    public VirtualViewportCommitResult TryCommit(long expectedEpoch)
    {
        Verify();
        reflow();
        return native.Invoke<string>("tryCommit", Format(expectedEpoch)) switch
        {
            "Committed" => VirtualViewportCommitResult.Committed,
            "Superseded" => VirtualViewportCommitResult.Superseded,
            "Blocked" => VirtualViewportCommitResult.Blocked,
            _ => throw new InvalidOperationException("Invalid native viewport commit result.")
        };
    }
    public void Cancel(long expectedEpoch)
    {
        Verify();
        native.InvokeVoid("cancel", Format(expectedEpoch));
    }
    public void FlushCommitted(long expectedEpoch)
    {
        Verify();
        native.InvokeVoid("validateFlush", Format(expectedEpoch));
        flushLayout();
        native.InvokeVoid("flushCommitted", Format(expectedEpoch));
    }
    public void Dispose()
    {
        if (disposed) return;
        Verify();
        disposed = true;
        requested = null;
        List<Exception> errors = [];
        foreach (var action in new Action[] { () => native.InvokeVoid("dispose"), reference.Dispose, native.Dispose })
        {
            try { action(); }
            catch (Exception error) { errors.Add(error); }
        }
        if (errors.Count != 0) throw new AggregateException("Virtual viewport cleanup failed.", errors);
    }
}
