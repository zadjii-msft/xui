using System;
using System.Threading;
using System.Threading.Tasks;
using Xui.Experimental.Portable;

namespace PortableDemo;

public sealed class StudioOperationsController : IDisposable
{
    private readonly Host host;
    private readonly WorkspaceStudioController workspace;
    private readonly IStudioOperationsService service;
    private readonly Action<Exception> reportUnhandled;
    private readonly UiWorkScope work;
    private readonly int thread = Environment.CurrentManagedThreadId;
    private WorkspaceStudioState source;
    private StudioOperationsState state = new();
    private Action<StudioOperationsState>? render;
    private CancellationToken lifetime;
    private CancellationTokenSource? active;
    private volatile bool disposed;

    public StudioOperationsState State { get { Verify(); return state; } }
    public Task LastOperation { get; private set; } = Task.CompletedTask;

    public StudioOperationsController(Host host, WorkspaceStudioController workspace, IStudioOperationsService service,
        Action<Exception> reportUnhandled)
    {
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(workspace);
        ArgumentNullException.ThrowIfNull(service);
        ArgumentNullException.ThrowIfNull(reportUnhandled);
        host.VerifyMutation();
        this.host = host;
        this.workspace = workspace;
        this.service = service;
        this.reportUnhandled = reportUnhandled;
        source = workspace.State;
        work = new(host);
    }
    public void Attach(ComponentLifetime owner, Action<StudioOperationsState> refresh)
    {
        Verify();
        if (render is not null) throw new InvalidOperationException("Operations controller is already attached.");
        owner.Own(this);
        lifetime = owner.Token;
        render = refresh;
        refresh(state);
    }
    public void UpdateWorkspace(WorkspaceStudioState next)
    {
        Verify();
        bool changed = next.CatalogVersion != source.CatalogVersion ||
            !System.Linq.Enumerable.SequenceEqual(next.Session.Drafts, source.Session.Drafts) ||
            !System.Linq.Enumerable.SequenceEqual(next.Session.OpenTabs, source.Session.OpenTabs);
        source = next;
        if (changed) Publish(state with { Revision = checked(state.Revision + 1) });
    }
    public void SetScope(StudioOperationsScope scope)
    {
        VerifyIdle();
        if (!Enum.IsDefined(scope)) throw new ArgumentOutOfRangeException(nameof(scope));
        Publish(state with { Scope = scope, Status = "Scope selected. Scan to compute a new local snapshot.", Error = "" });
    }
    public void Scan()
    {
        VerifyIdle();
        if (render is null) throw new InvalidOperationException("Attach operations before scanning.");
        var request = StudioOperations.Capture(workspace.State.Session, workspace.VisibleKeys, state.Scope, state.Revision);
        var cancellation = CancellationTokenSource.CreateLinkedTokenSource(lifetime);
        active = cancellation;
        try { Publish(state with { Busy = true, CancelRequested = false, Status = "Scanning captured local documents...", Error = "" }); }
        catch { active = null; cancellation.Dispose(); throw; }
        Task<StudioOperationsReport>? production = null;
        var delivery = work.RunAsync(token => production = service.ScanAsync(request, token), report =>
        {
            ValidateReport(request, report);
            active = null;
            try
            {
                Publish(state with
                {
                    Busy = false, CancelRequested = false, Report = report,
                    Status = "Local operations snapshot complete. No cloud metrics or user files were accessed."
                });
            }
            catch
            {
                reportUnhandled(new InvalidOperationException("Studio could not present its operations snapshot."));
                throw;
            }
        }, cancellation.Token);
        LastOperation = Observe(delivery, () => production, cancellation);
    }
    private static void ValidateReport(StudioOperationsRequest request, StudioOperationsReport report)
    {
        ArgumentNullException.ThrowIfNull(report);
        if (report.Revision != request.Revision || report.Scope != request.Scope || report.Documents != request.Keys.Length ||
            report.ModifiedDocuments < 0 || report.ModifiedDocuments > report.Documents ||
            report.OpenDocuments < 0 || report.OpenDocuments > report.Documents || report.Characters < 0 || report.Words < 0 ||
            report.ChecklistItems < 0 || report.EngineeringDocuments < 0 || report.DesignDocuments < 0 || report.ResearchDocuments < 0 ||
            (long)report.EngineeringDocuments + report.DesignDocuments + report.ResearchDocuments != report.Documents ||
            report.Workload.IsDefault || report.Workload.Length != Math.Min(8, report.Documents))
            throw new InvalidOperationException("Operations service returned an invalid snapshot.");
        var keys = System.Linq.Enumerable.ToHashSet(request.Keys, StringComparer.Ordinal);
        var seen = new System.Collections.Generic.HashSet<string>(StringComparer.Ordinal);
        foreach (var item in report.Workload)
            if (item is null || !keys.Contains(item.Key) || !seen.Add(item.Key) || item.Words < 0 || item.ChecklistItems < 0 ||
                StudioCatalog.Get(item.Key).Category != item.Category || string.IsNullOrWhiteSpace(item.Title) ||
                item.Title.Length > 1024 || item.Title.Contains('\0'))
                throw new InvalidOperationException("Operations service returned an invalid workload row.");
    }
    public void Cancel()
    {
        Verify();
        if (active is null || !state.CanCancel) throw new InvalidOperationException("No cancellable operations scan.");
        Publish(state with { CancelRequested = true, Status = "Cancel requested. Waiting for local scan to settle..." });
        active.Cancel();
    }
    public void BrowseCategory(StudioCategory category)
    {
        VerifyIdle();
        workspace.SetCategory(category);
        workspace.Navigate(StudioSection.Library);
    }
    public void OpenDocument(string key)
    {
        VerifyIdle();
        workspace.OpenDocument(key);
    }
    private async Task Observe(Task delivery, Func<Task<StudioOperationsReport>?> production, CancellationTokenSource cancellation)
    {
        bool canceled = false, failed = false;
        try
        {
            try { await delivery.ConfigureAwait(false); }
            catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { canceled = true; }
            catch { failed = true; }
            if (canceled && production() is { } pending)
            {
                try { await pending.ConfigureAwait(false); }
                catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { }
                catch { failed = true; }
            }
            if (disposed || lifetime.IsCancellationRequested)
            {
                if (failed) reportUnhandled(new InvalidOperationException("Local operations scan failed after its page retired."));
                return;
            }
            if (!canceled && !failed) return;
            try
            {
                await host.DispatchAsync(() =>
                {
                    if (disposed || lifetime.IsCancellationRequested) return;
                    active = null;
                    Publish(state with
                    {
                        Busy = false, CancelRequested = false,
                        Status = failed ? "Local operations scan failed." : "Local operations scan canceled. Drafts kept.",
                        Error = failed ? "The scan could not complete. Document content is not included in diagnostics." : ""
                    });
                }).ConfigureAwait(false);
            }
            catch
            {
                if (!disposed && !lifetime.IsCancellationRequested)
                    reportUnhandled(new InvalidOperationException("Studio could not deliver its operations outcome."));
            }
        }
        finally { Interlocked.CompareExchange(ref active, null, cancellation); cancellation.Dispose(); }
    }
    private void Publish(StudioOperationsState next)
    {
        var previous = state;
        state = next;
        try { render?.Invoke(next); }
        catch (KeyedUpdateException error) when (!error.ModelCommitted) { state = previous; throw; }
    }
    private void Verify() { host.VerifyMutation(); ObjectDisposedException.ThrowIf(disposed, this); }
    private void VerifyIdle() { Verify(); if (state.Busy) throw new InvalidOperationException("Wait for the current operations scan."); }
    public void Dispose()
    {
        if (Environment.CurrentManagedThreadId != thread) throw new InvalidOperationException("Dispose operations on its owning UI thread.");
        if (disposed) return;
        disposed = true;
        render = null;
        bool failed = false;
        try { active?.Cancel(); } catch (Exception) { failed = true; }
        try { work.Dispose(); } catch (Exception) { failed = true; }
        if (failed) throw new InvalidOperationException("Studio operations cancellation cleanup failed.");
    }
}
