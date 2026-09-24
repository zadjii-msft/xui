#if DEBUG
using Microsoft.JSInterop;
using PortableDemo;
using Xui.Experimental.Portable;
using Xui.Experimental.Web;

internal sealed class BrowserVirtualListFixture : IDisposable
{
    private readonly Host host;
    private readonly BrowserErrorReporter reporter;
    private readonly VirtualList view;
    private readonly IJSInProcessObjectReference module;
    private bool disposed;
    private int emptyAttachments;
    private IVirtualViewportLease? lease;
    private DomBackend? backend;
    internal List<object> PerformanceDiagnostics { get; } = [];
    internal BrowserVirtualListFixture(IJSInProcessObjectReference module)
    {
        this.module = module;
        reporter = new BrowserErrorReporter(module, "errors");
        host = new Host(new BrowserDispatcher(reporter.Report));
        try
        {
            view = VirtualList.CreateForViewport(host);
            Attach();
        }
        catch
        {
            try { host.Dispose(); }
            finally { reporter.Dispose(); }
            throw;
        }
    }
    private void Attach()
    {
        if (view.RowsView.Children.Count != 0 || view.Controller.Mounted.Count != 0)
            throw new InvalidOperationException("Prepare an empty virtual tree before native attachment.");
        emptyAttachments++;
        backend = new DomBackend(module, "app", "errors");
        host.Attach(backend);
        lease = host.BeginVirtualViewport(view.Viewport, view.Controller.RequestedCount,
            view.Controller.RowHeight, view.Controller.RequestedSourceVersion, view.OnViewportRequested);
        view.AttachViewport(lease, host.SetVirtualItemInfo);
    }
    internal void Cycle()
    {
        view.CaptureEditingState();
        host.Detach();
        view.PrepareForViewportAttachment();
        Attach();
    }
    internal object State() => new
    {
        attached = host.IsAttached, count = view.Controller.Count, mounted = view.Controller.Mounted.Count,
        ready = view.Ready, emptyAttachments, error = reporter.LastError?.Message
    };
    internal async Task<VirtualListPerformanceResult> MeasurePerformance(bool profile = false)
    {
        if (!view.Ready) throw new InvalidOperationException("Wait for the initial committed viewport.");
        var samples = new List<double>();
        PerformanceDiagnostics.Clear();
        backend!.Diagnostics.Enabled = profile;
        try
        {
            foreach (float offset in VirtualListPerformance.RequestOffsets(view.Controller.Count, view.Controller.RowHeight, view.Controller.ViewportHeight))
            {
                var completed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                void Observe(VirtualViewportRequest request)
                {
                    if (request.Requested.Offset == offset)
                    {
                        backend.Diagnostics.Mark("controller-pruned-event");
                        completed.TrySetResult();
                    }
                }
                view.ViewportCommitted += Observe;
                try
                {
                    long start = System.Diagnostics.Stopwatch.GetTimestamp();
                    if (profile) backend.Diagnostics.Start();
                    lease!.RequestOffset(offset);
                    await completed.Task.WaitAsync(TimeSpan.FromSeconds(10));
                    backend.Diagnostics.Mark("await-completed");
                    samples.Add(System.Diagnostics.Stopwatch.GetElapsedTime(start).TotalMilliseconds);
                    if (profile) PerformanceDiagnostics.Add(backend.Diagnostics.Snapshot());
                }
                finally { view.ViewportCommitted -= Observe; }
            }
            return VirtualListPerformance.Evaluate(samples);
        }
        finally { backend.Diagnostics.Enabled = false; }
    }
    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        List<Exception> errors = [];
        foreach (var action in new Action[]
        {
            () => { if (host.IsAttached) view.CaptureEditingState(); }, host.Dispose, reporter.Dispose
        })
        {
            try { action(); }
            catch (Exception error) { errors.Add(error); }
        }
        if (errors.Count != 0) throw new AggregateException("Virtual test fixture cleanup failed.", errors);
    }
}
#endif
