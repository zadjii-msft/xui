using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Xui.Experimental.Portable;

namespace PortableDemo;

public sealed record WorkspaceStudioState
{
    public WorkspaceStudioSession Session { get; init; } = WorkspaceStudioSession.Seed();
    public long CatalogVersion { get; init; } = 1;
    public long DocumentOpenVersion { get; init; }
    public int VisibleDocuments { get; init; } = StudioCatalog.Count;
    public bool Analyzing { get; init; }
    public StudioAnalysis? Analysis { get; init; }
    public string Status { get; init; } = "Local workspace. No files are read, saved, or uploaded.";
    public string Error { get; init; } = "";
    public bool CanAnalyze => !Analyzing && Session.ActiveDraft is { CanAnalyze: true };
}

public sealed class WorkspaceStudioController : IDisposable
{
    private readonly Host host;
    private readonly IStudioAnalysisService service;
    private readonly Action<Exception> reportUnhandled;
    private readonly LatestUiWork work;
    private readonly int thread = Environment.CurrentManagedThreadId;
    private WorkspaceStudioState state;
    private ImmutableArray<string> visibleKeys;
    private Action<WorkspaceStudioState>? render;
    private CancellationToken lifetime;
    private CancellationTokenSource? current;
    private long generation;
    private volatile bool disposed;

    public WorkspaceStudioState State { get { Verify(); return state; } }
    public ImmutableArray<string> VisibleKeys { get { Verify(); return visibleKeys; } }
    public Task LastOperation { get; private set; } = Task.CompletedTask;

    public WorkspaceStudioController(Host host, IStudioAnalysisService service, Action<Exception> reportUnhandled,
        WorkspaceStudioSession? session = null)
    {
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(service);
        ArgumentNullException.ThrowIfNull(reportUnhandled);
        host.VerifyMutation();
        this.host = host;
        this.service = service;
        this.reportUnhandled = reportUnhandled;
        work = new(host);
        var initial = session ?? WorkspaceStudioSession.Seed();
        visibleKeys = initial.VisibleKeys();
        state = new()
        {
            Session = initial, VisibleDocuments = visibleKeys.Length,
            Status = initial.AnalysisInterrupted ? "Analysis interrupted. Local drafts restored; analyze again when ready." :
                session is null ? "Local workspace. No files are read, saved, or uploaded." : "Workspace restored. Local drafts only."
        };
    }
    public void Attach(ComponentLifetime owner, Action<WorkspaceStudioState> refresh)
    {
        Verify();
        ArgumentNullException.ThrowIfNull(owner);
        ArgumentNullException.ThrowIfNull(refresh);
        if (render is not null) throw new InvalidOperationException("Studio controller is already attached.");
        owner.Own(this);
        lifetime = owner.Token;
        render = refresh;
        refresh(state);
    }
    public WorkspaceStudioSession CaptureSession()
    {
        Verify();
        return state.Session.Interrupted(state.Session.AnalysisInterrupted || state.Analyzing || !LastOperation.IsCompleted);
    }
    public void Navigate(StudioSection section) => Change(state.Session.Navigate(section), "Workspace section changed.", invalidateAnalysis: false);
    public void SetQuery(string query) => Change(state.Session.WithQuery(query), "Catalog filter updated.", invalidateAnalysis: false);
    public void SetCategory(StudioCategory category) => Change(state.Session.WithCategory(category), "Catalog category updated.", invalidateAnalysis: false);
    public void SelectDocument(string key) => Change(state.Session.SelectDocument(key), "Document selected.", invalidateAnalysis: false);
    public void OpenDocument(string key)
    {
        Verify();
        _ = StudioCatalog.Get(key);
        if (!state.Session.CanOpenDocument(key))
        {
            Publish(state with { Error = "Close a tab before opening another document. Local drafts are kept." });
            return;
        }
        Change(state.Session.OpenDocument(key), "Document opened. Drafts stay local.",
            invalidateAnalysis: key != state.Session.ActiveDocument, openPane: true);
    }
    public void ActivateTab(string key) => Change(state.Session.ActivateTab(key), "Document tab selected.", invalidateAnalysis: key != state.Session.ActiveDocument);
    public void OpenOperations()
    {
        Verify();
        if (!state.Session.CanOpenDocument(StudioTabs.OperationsKey))
        {
            Publish(state with { Error = "Close a tab before opening Operations. Local drafts are kept." });
            return;
        }
        Change(state.Session.OpenOperations(), "Operations dashboard opened. Metrics are computed only on request.",
            invalidateAnalysis: state.Session.ActiveDocument != StudioTabs.OperationsKey, openPane: true);
    }
    public void MoveTab(string key, int destination) => Change(state.Session.MoveTab(key, destination), "Document tabs reordered.", invalidateAnalysis: false);
    public void CloseTab(string key) => Change(state.Session.CloseTab(key), "Tab closed. Any local draft is kept in Drafts.", invalidateAnalysis: key == state.Session.ActiveDocument);
    public void SetTitle(string key, string value) => Change(state.Session.Edit(state.Session.Document(key).WithTitle(value)),
        "Title edited. No file was saved.", invalidateAnalysis: key == state.Session.ActiveDocument);
    public void SetBody(string key, string value) => Change(state.Session.Edit(state.Session.Document(key).WithBody(value)),
        "Document edited. No file was saved.", invalidateAnalysis: key == state.Session.ActiveDocument);
    public void RevertDocument(string key) => Change(state.Session.Revert(key), "Document reverted to its generated sample.",
        invalidateAnalysis: key == state.Session.ActiveDocument);
    private void Change(WorkspaceStudioSession next, string status, bool invalidateAnalysis, bool openPane = false)
    {
        Verify();
        var before = state;
        var oldKeys = visibleKeys;
        bool filterChanged = next.Query != before.Session.Query || next.Category != before.Session.Category ||
            (next.Section == StudioSection.Drafts) != (before.Session.Section == StudioSection.Drafts);
        bool changedDraftKeys = !next.Drafts.Select(draft => draft.Key).SequenceEqual(before.Session.Drafts.Select(draft => draft.Key));
        bool changedTitles = !next.Drafts.Select(draft => (draft.Key, draft.Title))
            .SequenceEqual(before.Session.Drafts.Select(draft => (draft.Key, draft.Title)));
        bool recalculate = filterChanged || (next.Section == StudioSection.Drafts && changedDraftKeys) ||
            (!string.IsNullOrWhiteSpace(next.Query) && changedTitles);
        var keys = recalculate ? next.VisibleKeys() : visibleKeys;
        bool changedSource = !keys.SequenceEqual(visibleKeys);
        var candidate = before with
        {
            Session = next, Status = status, Error = "",
            VisibleDocuments = keys.Length,
            CatalogVersion = changedSource ? checked(before.CatalogVersion + 1) : before.CatalogVersion,
            DocumentOpenVersion = openPane ? checked(before.DocumentOpenVersion + 1) : before.DocumentOpenVersion,
            Analyzing = invalidateAnalysis ? false : before.Analyzing,
            Analysis = invalidateAnalysis ? null : before.Analysis
        };
        visibleKeys = keys;
        try { Publish(candidate); }
        catch (KeyedUpdateException error) when (!error.ModelCommitted)
        {
            visibleKeys = oldKeys;
            throw;
        }
        finally
        {
            if (invalidateAnalysis && ReferenceEquals(state, candidate)) InvalidateWork();
        }
    }
    public void Analyze()
    {
        Verify();
        var draft = state.Session.ActiveDraft ?? throw new InvalidOperationException("Open a document before analyzing.");
        if (!draft.CanAnalyze) throw new InvalidOperationException(draft.Validation);
        if (render is null) throw new InvalidOperationException("Attach studio before starting analysis.");
        InvalidateWork();
        var request = CancellationTokenSource.CreateLinkedTokenSource(lifetime);
        current = request;
        long operationGeneration = generation;
        try { Publish(state with { Analyzing = true, Analysis = null, Status = "Analyzing the active local draft...", Error = "" }); }
        catch { current = null; request.Dispose(); throw; }
        Task<StudioAnalysis>? production = null;
        var delivery = work.RunAsync(token => production = service.AnalyzeAsync(draft, token), result =>
        {
            if (generation != operationGeneration) return;
            if (result != StudioAnalysis.Calculate(draft))
                throw new InvalidOperationException("Analysis did not match the requested local draft.");
            current = null;
            try
            {
                Publish(state with
                {
                    Analyzing = false, Analysis = result, Session = state.Session.Interrupted(false),
                    Status = "Analysis complete. No content left this workspace."
                });
            }
            catch
            {
                reportUnhandled(new InvalidOperationException("Studio could not display its analysis result."));
                throw;
            }
        }, request.Token);
        LastOperation = Task.WhenAll(LastOperation, Observe(delivery, () => production, request, operationGeneration));
    }
    public void CancelAnalysis()
    {
        Verify();
        if (!state.Analyzing || current is null) throw new InvalidOperationException("No active analysis.");
        InvalidateWork();
        Publish(state with { Analyzing = false, Analysis = null, Status = "Analysis canceled. Local draft kept.", Error = "" });
    }
    private void InvalidateWork()
    {
        generation = checked(generation + 1);
        var previous = current;
        current = null;
        previous?.Cancel();
    }
    private async Task Observe(Task delivery, Func<Task<StudioAnalysis>?> production,
        CancellationTokenSource request, long operationGeneration)
    {
        bool canceled = false, failed = false;
        try
        {
            try { await delivery.ConfigureAwait(false); }
            catch (OperationCanceledException) when (request.IsCancellationRequested) { canceled = true; }
            catch { failed = true; }
            if (canceled && production() is { } pending)
            {
                try { await pending.ConfigureAwait(false); }
                catch (OperationCanceledException) when (request.IsCancellationRequested) { }
                catch { failed = true; }
            }
            if (!failed || disposed || lifetime.IsCancellationRequested) return;
            try
            {
                await host.DispatchAsync(() =>
                {
                    if (disposed || lifetime.IsCancellationRequested || generation != operationGeneration) return;
                    current = null;
                    Publish(state with
                    {
                        Analyzing = false, Analysis = null, Status = "Analysis failed. Local draft kept.",
                        Error = "Local analysis could not complete. Document content is not included in diagnostics."
                    });
                }).ConfigureAwait(false);
            }
            catch
            {
                if (!disposed && !lifetime.IsCancellationRequested)
                    reportUnhandled(new InvalidOperationException("Studio could not deliver its analysis outcome."));
            }
        }
        finally { Interlocked.CompareExchange(ref current, null, request); request.Dispose(); }
    }
    private void Publish(WorkspaceStudioState next)
    {
        var before = state;
        state = next;
        try { render?.Invoke(next); }
        catch (KeyedUpdateException error) when (!error.ModelCommitted) { state = before; throw; }
    }
    private void Verify() { host.VerifyMutation(); ObjectDisposedException.ThrowIf(disposed, this); }
    public void Dispose()
    {
        if (Environment.CurrentManagedThreadId != thread) throw new InvalidOperationException("Dispose studio on its owning UI thread.");
        if (disposed) return;
        disposed = true;
        render = null;
        var failures = new List<Exception>();
        try { InvalidateWork(); } catch (Exception error) { failures.Add(error); }
        try { work.Dispose(); } catch (Exception error) { failures.Add(error); }
        if (failures.Count != 0) throw new InvalidOperationException("Studio cancellation cleanup failed.");
    }
}
