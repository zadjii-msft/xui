using System.Collections.Immutable;
using System.Text.Json;
using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private sealed class PendingOperations : IStudioOperationsService
    {
        public List<(StudioOperationsRequest Request, CancellationToken Token, TaskCompletionSource<StudioOperationsReport> Completion)> Pending { get; } = [];
        public Task<StudioOperationsReport> ScanAsync(StudioOperationsRequest request, CancellationToken cancellationToken)
        {
            var result = new TaskCompletionSource<StudioOperationsReport>(TaskCreationOptions.RunContinuationsAsynchronously);
            Pending.Add((request, cancellationToken, result));
            return result.Task;
        }
    }
    private static WorkspaceStudioSession OperationsDraft()
    {
        var seed = WorkspaceStudioSession.Seed();
        seed = seed.Edit(seed.Document("doc-00001").WithBody("One two\n- three four\n\n- five"));
        return seed.Edit(seed.Document("doc-00002").WithBody("Alpha beta\n- gamma")).OpenOperations();
    }
    private static void OperationsChecks()
    {
        OperationsModelChecks();
        OperationsWorkflowChecks();
        OperationsLifetimeChecks();
        OperationsFailureChecks();
        OperationsMetricsLayoutChecks();
    }
    private static void OperationsModelChecks()
    {
        var session = OperationsDraft();
        Assert(session.Version == 2 && session.OpenTabs.SequenceEqual(["doc-00001", "doc-00002", "operations"]) &&
            session.ActiveDocument == "operations" && session.ActiveDraft is null && session.ActiveCategory == "Local operations",
            "Operations is a genuine typed workspace tab, not a fabricated document identity.");
        Assert(StudioTabs.NativeId("operations") == 10001 && StudioTabs.Key(10001) == "operations" &&
            StudioCatalog.Get("doc-10000").NativeId == 10000, "Dashboard reserved identity does not collide with any catalog document.");
        var json = WorkspaceStudioSessionCodec.Serialize(session);
        var restored = WorkspaceStudioSessionCodec.Restore(json);
        Assert(WorkspaceStudioSessionCodec.Serialize(restored) == json && !json.Contains("Workload") &&
            !json.Contains("Report") && !json.Contains("Busy"), "Session v2 contains logical dashboard identity but no computed report or live task.");
        var legacy = WorkspaceStudioSessionCodec.Serialize(WorkspaceStudioSession.Seed()).Replace("\"Version\":2", "\"Version\":1");
        Assert(WorkspaceStudioSessionCodec.Restore(legacy).Version == 1 &&
            WorkspaceStudioSessionCodec.Restore(legacy).OpenOperations().Version == 2,
            "Version-one document sessions read safely and migrate explicitly when new tab kinds are introduced.");
        Throws<JsonException>(() => WorkspaceStudioSessionCodec.Restore(json.Replace("\"Version\":2", "\"Version\":1")));
        Throws<ArgumentException>(() => WorkspaceStudioSessionCodec.Restore(json.Replace("\"operations\"", "\"unknown-dashboard\"")));
        Throws<ArgumentOutOfRangeException>(() => StudioTabs.Key(10002));
        Assert(session.CloseTab("operations").ActiveDocument == "doc-00002" &&
            session.CloseTab("operations").ChangedCount == 2, "Closing Operations preserves all real document drafts.");
        var full = WorkspaceStudioSession.Seed();
        for (int i = 2; i < 16; i++) full = full.OpenDocument(StudioCatalog.Entries[i].Key);
        Throws<InvalidOperationException>(() => full.OpenOperations());
        var mixed = full.CloseTab("doc-00016").OpenOperations();
        Assert(mixed.OpenTabs.Length == 16 && mixed.OpenTabs.Count(StudioTabs.IsDocument) == 15,
            "Operations consumes one of the same sixteen retained-page slots, never an accidental seventeenth page.");
        var local = new LocalStudioOperationsService();
        var request = StudioOperations.Capture(session, ["doc-00001", "doc-00002"], StudioOperationsScope.CurrentCatalog, 1);
        var result = local.ScanAsync(request, CancellationToken.None).GetAwaiter().GetResult();
        Assert(result.Documents == 2 && result.ModifiedDocuments == 2 && result.OpenDocuments == 2 &&
            result.Characters == 46 && result.Words == 11 && result.ChecklistItems == 3 &&
            result.EngineeringDocuments == 1 && result.DesignDocuments == 1 && result.ResearchDocuments == 0,
            "Operations computes literal workload values from captured local document bodies.");
        Assert(result.Workload.Select(item => item.Key).SequenceEqual(["doc-00001", "doc-00002"]) &&
            result.Workload[0].Words == 7 && result.Workload[0].ChecklistItems == 2,
            "Actionable rows are ranked deterministically by checklist workload and words.");
        var all = local.ScanAsync(StudioOperations.Capture(session, [], StudioOperationsScope.AllDocuments, 2),
            CancellationToken.None).GetAwaiter().GetResult();
        Assert(all.Documents == 10000 && all.ModifiedDocuments == 2 && all.OpenDocuments == 2 &&
            all.EngineeringDocuments == 3334 && all.DesignDocuments == 3333 && all.ResearchDocuments == 3333 &&
            all.ChecklistItems == 29997 && all.Workload.Length == 8,
            "Real 10k local scan has literal category totals and only eight actionable result rows.");
        var empty = local.ScanAsync(StudioOperations.Capture(WorkspaceStudioSession.Seed(), [],
            StudioOperationsScope.LocalDrafts, 3), CancellationToken.None).GetAwaiter().GetResult();
        Assert(empty.Documents == 0 && empty.Words == 0 && empty.Characters == 0 && empty.Workload.Length == 0,
            "An empty selected dataset produces an actual zero-count report, not fake cloud activity.");
        Throws<ArgumentException>(() => StudioOperations.Capture(session, ["doc-00001", "doc-00001"], StudioOperationsScope.CurrentCatalog, 1));
        Throws<ArgumentOutOfRangeException>(() => StudioOperations.Capture(session, [], (StudioOperationsScope)4, 1));
        using var cancel = new CancellationTokenSource();
        cancel.Cancel();
        Throws<OperationCanceledException>(() => local.ScanAsync(request, cancel.Token).GetAwaiter().GetResult());
    }
    private static void OperationsWorkflowChecks()
    {
        using var h = new StudioHarness(OperationsDraft());
        var dashboard = h.App.OperationsPage ?? throw new InvalidOperationException("Operations page was not created.");
        Assert(h.App.DocumentTabs.Selected == 10001 && h.App.DocumentPages.Pages.Count == 3 &&
            dashboard.Snapshot.Report is null && dashboard.Snapshot.Status == "No operations snapshot yet. Scan local data when ready.",
            "Restoring dashboard tab creates a real page with no automatic scan or invented metrics.");
        var docBody = h.Backend.Find("doc-00001-body");
        Assert(!docBody.Events.Change("inactive edit"), "Inactive editor remains native-input blocked beside retained dashboard.");
        var scope = h.Backend.Find("operations-scope");
        Assert(((ISelectionControlEvents)scope.Events).SelectionChanged(3), "Dataset scope uses genuine native SingleChoice selection.");
        Assert(h.Backend.Find("operations-scan").Events.Click(), "Dashboard scan is an explicit native UI action.");
        h.Dispatcher.Until(() => dashboard.Controller.LastOperation.IsCompleted);
        dashboard.Controller.LastOperation.GetAwaiter().GetResult();
        Assert(dashboard.Snapshot.Report?.Documents == 2 && dashboard.WorkloadRows.Children.Count == 2 &&
            ((Label)h.Backend.Find("operations-documents").Element).Text == "Documents: 2" &&
            ((Label)h.Backend.Find("operations-words").Element).Text == "Words: 11" &&
            ((Label)h.Backend.Find("operations-freshness").Element).Text == "CURRENT LOCAL SNAPSHOT",
            "Native dashboard displays computed local metrics with explicit freshness.");
        var report = dashboard.Snapshot.Report;
        h.Backend.Find("operations-doc-00001-open").Events.Click();
        Assert(h.Controller.State.Session.ActiveDocument == "doc-00001" &&
            ReferenceEquals(docBody, h.Backend.Find("doc-00001-body")) && !dashboard.Lifetime.Token.IsCancellationRequested,
            "Actionable workload row opens an existing retained editor, not a copied details page.");
        docBody.Events.Change("Changed since the operations snapshot.");
        Assert(dashboard.Snapshot.Stale && ReferenceEquals(report, dashboard.Snapshot.Report),
            "Editing a source document marks retained dashboard report stale without rewriting old metrics as current.");
        h.Backend.Find("studio-tabs").SelectPage(10001);
        Assert(((Label)h.Backend.Find("operations-freshness").Element).Text == "STALE SNAPSHOT / REFRESH REQUIRED",
            "Returning to dashboard exposes stale-source status.");
        h.Backend.Find("operations-browse-design").Events.Click();
        h.Dispatcher.Drain();
        Assert(h.Controller.State.Session.Category == StudioCategory.Design &&
            h.Controller.State.Session.Section == StudioSection.Library &&
            h.Controller.State.VisibleDocuments == 3333 && h.App.DocumentPages.Selected == 10001,
            "Category workload action reuses existing library navigation/filter and preserves all workspace tabs.");
        var saved = h.App.CaptureSession();
        h.Host.Dispose();
        using var recreated = new StudioHarness(WorkspaceStudioSessionCodec.Restore(WorkspaceStudioSessionCodec.Serialize(saved)));
        Assert(recreated.App.OperationsPage?.Snapshot.Report is null &&
            recreated.Controller.State.Session.Drafts.Length == 2 && recreated.App.DocumentPages.Selected == 10001,
            "Recreated workspace retains drafts and dashboard identity, but never persists metrics as live data.");
    }
    private static void OperationsLifetimeChecks()
    {
        var service = new PendingOperations();
        using var h = new StudioHarness(OperationsDraft(), service);
        var page = h.App.OperationsPage!;
        page.Controller.SetScope(StudioOperationsScope.LocalDrafts);
        page.Controller.Scan();
        var first = service.Pending[0];
        Assert(page.Snapshot.Busy && ((Control)h.Backend.Find("operations-progress").Element).Visible &&
            !page.ScopeInput.Enabled, "Indeterminate progress corresponds to an actual pending local producer.");
        h.Controller.ActivateTab("doc-00001");
        h.Controller.SetBody("doc-00001", "Edited while captured scan is running.");
        var result = new LocalStudioOperationsService().ScanAsync(first.Request, CancellationToken.None).GetAwaiter().GetResult();
        first.Completion.SetResult(result);
        h.Dispatcher.Until(() => page.Controller.LastOperation.IsCompleted);
        Assert(!page.Snapshot.Busy && page.Snapshot.Stale && page.Snapshot.Report?.Words == 11 &&
            !page.Lifetime.Token.IsCancellationRequested, "Retained inactive dashboard may finish owned work, explicitly stale after source changes.");
        h.Controller.ActivateTab(StudioTabs.OperationsKey);
        page.Controller.Scan();
        var second = service.Pending[1];
        page.Controller.Cancel();
        Assert(second.Token.IsCancellationRequested && page.Snapshot.Busy && page.Snapshot.CancelRequested,
            "Cancel waits for captured local scan producer to settle.");
        second.Completion.SetCanceled(second.Token);
        h.Dispatcher.Until(() => page.Controller.LastOperation.IsCompleted);
        Assert(page.Snapshot.Status == "Local operations scan canceled. Drafts kept.", "Cancellation is distinct from failed or current snapshot.");
        page.Controller.Scan();
        var third = service.Pending[2];
        h.Controller.CloseTab(StudioTabs.OperationsKey);
        Task remaining = h.App.LastOperation;
        Assert(page.Lifetime.Token.IsCancellationRequested && third.Token.IsCancellationRequested &&
            h.App.OperationsPage is null && !remaining.IsCompleted,
            "Closing Operations retires its component and retains producer drainage in workspace ownership.");
        third.Completion.SetResult(new LocalStudioOperationsService().ScanAsync(third.Request, CancellationToken.None).GetAwaiter().GetResult());
        h.Dispatcher.Until(() => remaining.IsCompleted);
        remaining.GetAwaiter().GetResult();
        Assert(h.Controller.State.Session.Drafts.Length == 2 && !h.Controller.State.Session.OpenTabs.Contains("operations") &&
            h.Errors.Count == 0, "Late closed-dashboard result cannot recreate its tab or lose document drafts.");
        h.Controller.OpenOperations();
        var reopened = h.App.OperationsPage!;
        Assert(!ReferenceEquals(page, reopened) && reopened.Snapshot.Report is null,
            "Reopening dashboard creates fresh owned state rather than reviving disposed producers.");
        reopened.Controller.Scan();
        var closing = service.Pending[3];
        var session = h.App.CaptureSession();
        var pending = h.App.LastOperation;
        h.Host.Dispose();
        Assert(session.AnalysisInterrupted && closing.Token.IsCancellationRequested, "Workspace recreation captures interrupted aggregate work and cancels root-owned page operations.");
        closing.Completion.SetResult(new LocalStudioOperationsService().ScanAsync(closing.Request, CancellationToken.None).GetAwaiter().GetResult());
        h.Dispatcher.Until(() => pending.IsCompleted);
        pending.GetAwaiter().GetResult();
        Assert(h.Errors.Count == 0 && h.Backend.Peers.All(peer => peer.Disposed), "Late terminal scan leaves no native UI or stale callbacks.");
    }
    private static void OperationsFailureChecks()
    {
        var service = new PendingOperations();
        using var h = new StudioHarness(OperationsDraft(), service);
        var page = h.App.OperationsPage!;
        page.Controller.SetScope(StudioOperationsScope.LocalDrafts);
        page.Controller.Scan();
        service.Pending[0].Completion.SetException(new IOException("PRIVATE_DOCUMENT_CONTENT"));
        h.Dispatcher.Until(() => page.Controller.LastOperation.IsCompleted);
        Assert(page.Snapshot.Status == "Local operations scan failed." && page.Snapshot.Report is null &&
            !page.Snapshot.Error.Contains("PRIVATE") && h.Errors.Count == 0, "Failed producer is explicit without displaying private document diagnostics.");
        page.Controller.Scan();
        var wrong = service.Pending[1];
        var report = new LocalStudioOperationsService().ScanAsync(wrong.Request, CancellationToken.None).GetAwaiter().GetResult();
        wrong.Completion.SetResult(report with { Documents = 999 });
        h.Dispatcher.Until(() => page.Controller.LastOperation.IsCompleted);
        Assert(page.Snapshot.Report is null && page.Snapshot.Status == "Local operations scan failed.",
            "Invalid report shape cannot fabricate dataset results.");
        var rowsPeer = h.Backend.Peers.Single(peer => ReferenceEquals(peer.Element, page.WorkloadRows));
        rowsPeer.RejectMutation = true;
        page.Controller.Scan();
        var rejected = service.Pending[2];
        rejected.Completion.SetResult(new LocalStudioOperationsService().ScanAsync(rejected.Request, CancellationToken.None).GetAwaiter().GetResult());
        h.Dispatcher.Until(() => page.Controller.LastOperation.IsCompleted);
        Assert(h.Host.IsAttached && page.Snapshot.Report is null && page.Controller.State.Report is null &&
            page.WorkloadRows.Children.Count == 0 && page.Snapshot.Status == "Local operations scan failed.",
            "Precommit result-row rejection does not publish a phantom completed report.");
        rowsPeer.RejectMutation = false;
        rowsPeer.FailInsert = true;
        page.Controller.Scan();
        var committed = service.Pending[3];
        committed.Completion.SetResult(new LocalStudioOperationsService().ScanAsync(committed.Request, CancellationToken.None).GetAwaiter().GetResult());
        h.Dispatcher.Until(() => page.Controller.LastOperation.IsCompleted);
        Assert(!h.Host.IsAttached && page.Controller.State.Report?.Documents == 2 && page.WorkloadRows.Children.Count == 2,
            "Postcommit result insertion failure retains committed report and row models with explicit detachment.");
        h.App.PrepareForAttachment();
        var replacement = new CatalogBackend(h.Dispatcher);
        h.Host.Attach(replacement);
        h.App.AttachView();
        h.Dispatcher.Drain();
        Assert(((Label)replacement.Find("operations-words").Element).Text == "Words: 11" &&
            replacement.Find("operations-doc-00001-open").Element is Button,
            "Reattachment reconstructs correct report presentation without fake rollback.");
        page.Controller.Scan();
        var retired = service.Pending[4];
        h.Controller.CloseTab(StudioTabs.OperationsKey);
        var pending = h.App.LastOperation;
        int errors = h.Errors.Count;
        retired.Completion.SetException(new IOException("PRIVATE_DOCUMENT_CONTENT"));
        h.Dispatcher.Until(() => pending.IsCompleted);
        pending.GetAwaiter().GetResult();
        Assert(h.Errors.Count == errors + 1 && !h.Errors[^1].ToString().Contains("PRIVATE"),
            "Noncooperative producer failure after close is observed through sanitized host reporting.");
    }
    private static void OperationsMetricsLayoutChecks()
    {
        using var h = new StudioHarness();
        h.Controller.OpenOperations();
        var dashboard = h.App.OperationsPage!;
        dashboard.Controller.SetScope(StudioOperationsScope.AllDocuments);
        dashboard.Controller.Scan();
        h.Finish();
        var report = dashboard.Snapshot.Report;
        Assert(report is { Documents: 10000, OpenDocuments: 2, Words: 470000 },
            "Responsive metrics use the exact large local snapshot seen in native desktop and phone captures.");
        var scope = h.Backend.Find("operations-scope");
        var editor = h.Backend.Find("doc-00001-body");
        var results = dashboard.WorkloadRows.Children.ToArray();
        var metricIds = new[] { "operations-documents", "operations-drafts", "operations-open",
            "operations-words", "operations-characters", "operations-tasks" };
        var texts = metricIds.Select(id => ((Label)h.Backend.Find(id).Element).Text).ToArray();
        var wideMetrics = dashboard.MetricsView.Children.Single();
        var wideGrid = (Grid)wideMetrics.Children.Single();
        Assert(wideGrid.Columns.Count == 3 && wideGrid.Columns[1] == new GridTrack(TrackSizing.Fixed, 16) &&
            wideGrid.Rows.Count == 5 && wideGrid.Rows[1] == new GridTrack(TrackSizing.Fixed, 8),
            "Wide metrics retain two data columns with explicit horizontal and vertical gutters.");
        Assert(wideGrid.Children.Select(child => child.Cell!.Value.Column).SequenceEqual([0u, 2u, 0u, 2u, 0u, 2u]),
            "No wide metric occupies the dedicated 16-unit gutter track.");
        var oldPeers = metricIds.Select(h.Backend.Find).ToArray();
        var wideLifetime = h.Host.GetComponentLifetime(wideMetrics);
        Assert(scope.TryFocus(), "Native dataset choice focused before resize.");
        h.Backend.Resize(320, 600);
        h.Dispatcher.Drain();
        var compactMetrics = dashboard.MetricsView.Children.Single();
        var compactGrid = (Grid)compactMetrics.Children.Single();
        Assert(compactGrid.Columns.Count == 1 && compactGrid.Columns[0] == new GridTrack(TrackSizing.Star, 1) &&
            compactGrid.Rows.Count == 11 &&
            compactGrid.Children.Select(child => child.Cell!.Value.Row).SequenceEqual([0u, 2u, 4u, 6u, 8u, 10u]) &&
            compactGrid.Children.All(child => child.Cell!.Value.Column == 0),
            "Compact metrics give all six complete label/value strings their own full-width auto-height row.");
        Assert(compactGrid.Children.OfType<Label>().All(label => label.TextLayout == LabelTextLayout.Wrap()),
            "Long keys and numeric values wrap natively without a two-line clipping cap.");
        Assert(wideLifetime.Token.IsCancellationRequested && oldPeers.All(peer => peer.Disposed) &&
            metricIds.Select(id => ((Label)h.Backend.Find(id).Element).Text).SequenceEqual(texts),
            "Only the read-only metric component is replaced; native text and metric identities remain exact.");
        Assert(ReferenceEquals(scope, h.Backend.Find("operations-scope")) && scope.Focused &&
            ReferenceEquals(editor, h.Backend.Find("doc-00001-body")) &&
            results.SequenceEqual(dashboard.WorkloadRows.Children) && ReferenceEquals(report, dashboard.Snapshot.Report),
            "Narrow resize retains native choice focus, every editor, actionable result row, and captured report.");
        foreach (float width in new[] { 360f, 480f, 719f })
        {
            h.Backend.Resize(width, 600);
            h.Dispatcher.Drain();
            Assert(ReferenceEquals(compactMetrics, dashboard.MetricsView.Children.Single()),
                "Within Compact, resize does not rebuild even the read-only metrics component.");
        }
        h.Backend.Resize(720, 800);
        h.Dispatcher.Drain();
        var mediumMetrics = dashboard.MetricsView.Children.Single();
        var mediumGrid = (Grid)mediumMetrics.Children.Single();
        Assert(mediumGrid.Columns.Count == 3 && mediumGrid.Columns[1].Value == 16 &&
            metricIds.Select(id => ((Label)h.Backend.Find(id).Element).Text).SequenceEqual(texts),
            "Medium restores the guttered two-column layout without recalculating the local report.");
        h.Backend.Resize(1440, 960);
        h.Dispatcher.Drain();
        Assert(ReferenceEquals(mediumMetrics, dashboard.MetricsView.Children.Single()),
            "Medium and Expanded share one retained wide metric presentation.");
        var metricsPeer = h.Backend.Peers.Single(peer => ReferenceEquals(peer.Element, dashboard.MetricsView));
        metricsPeer.RejectMutation = true;
        h.Backend.Resize(320, 600);
        h.Dispatcher.Drain();
        Assert(h.App.LayoutPending && h.App.LayoutMode == WidthMode.Expanded &&
            ReferenceEquals(mediumMetrics, dashboard.MetricsView.Children.Single()) &&
            ReferenceEquals(report, dashboard.Snapshot.Report) && scope.Focused,
            "Native mutation veto defers the readonly layout change without changing report, input focus, or cached width mode.");
        metricsPeer.RejectMutation = false;
        h.Backend.Find("studio-layout-retry").Events.Click();
        Assert(h.App.LayoutMode == WidthMode.Compact && !h.App.LayoutPending &&
            ((Grid)dashboard.MetricsView.Children.Single().Children.Single()).Columns.Count == 1,
            "Explicit retry applies the correct full-width metrics after native preflight succeeds.");

        using var compactStart = new StudioHarness();
        compactStart.Backend.Resize(320, 600);
        compactStart.Dispatcher.Drain();
        compactStart.Controller.OpenOperations();
        Assert(((Grid)compactStart.App.OperationsPage!.MetricsView.Children.Single().Children.Single()).Columns.Count == 1,
            "Opening Operations after initial compact layout constructs only the appropriate narrow metrics group.");
        Assert(compactStart.Backend.Peers.Count(peer => !peer.Disposed && peer.Id == "operations-open") == 1,
            "There is exactly one visible native metric identity, not duplicate hidden presentations.");
    }
}
