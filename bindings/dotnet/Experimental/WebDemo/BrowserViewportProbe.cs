#if DEBUG
using System.Globalization;
using Microsoft.JSInterop;
using Xui.Experimental.Portable;
using Xui.Experimental.Web;

internal sealed class BrowserViewportProbe : IDisposable
{
    private readonly Host host;
    private readonly IVirtualViewportLease lease;
    private readonly List<VirtualViewportRequest> requests = [];

    internal BrowserViewportProbe(IJSInProcessObjectReference module)
    {
        host = new Host(new BrowserDispatcher(error => module.InvokeVoid("reportError", "errors", error.ToString())));
        try
        {
            ScrollView viewport;
            using (var build = host.BeginBuild())
            {
                var root = host.Stack(Axis.Vertical);
                viewport = host.ScrollView(host.Stack(Axis.Vertical), "Viewport protocol probe");
                root.Add(viewport, 1);
                host.SetContent(root);
                build.Complete();
            }
            host.Attach(new DomBackend(module, "app", "errors"));
            lease = host.BeginVirtualViewport(viewport, 0, 64, 9007199254740993L, request => requests.Add(request));
            if (requests.Count != 0) throw new InvalidOperationException("Viewport callbacks ran synchronously.");
        }
        catch { host.Dispose(); throw; }
    }

    internal object Snapshot() => new
    {
        requests = requests.Select(request => new
        {
            epoch = request.Epoch.ToString(CultureInfo.InvariantCulture),
            committedSourceVersion = request.CommittedSourceVersion.ToString(CultureInfo.InvariantCulture),
            requestedSourceVersion = request.RequestedSourceVersion.ToString(CultureInfo.InvariantCulture),
            request.Committed, request.Requested, request.IsBlocked
        }).ToArray()
    };

    internal object Commit()
    {
        long epoch = requests.Last().Epoch;
        var begin = lease.TryBeginUpdate(epoch);
        if (begin != VirtualViewportUpdateResult.Ready) throw new InvalidOperationException($"Probe could not reserve: {begin}.");
        var result = lease.TryCommit(epoch);
        if (lease is not ISettledVirtualViewportLease settled) throw new NotSupportedException("Missing native settle capability.");
        settled.FlushCommitted(epoch);
        return new { result = result.ToString() };
    }

    internal void MaximumSource() => lease.SetExtent(0, long.MaxValue);
    internal void Close() => lease.Dispose();
    public void Dispose() => host.Dispose();
}
#endif
