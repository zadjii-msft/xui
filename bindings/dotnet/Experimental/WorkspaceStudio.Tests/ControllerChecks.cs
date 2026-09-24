using System.Collections.Concurrent;
using System.Diagnostics;
using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private sealed class Dispatcher : IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        private readonly ConcurrentQueue<Action> queue = new();
        public bool Reject { get; set; }
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action)
        {
            if (Reject) throw new InvalidOperationException("Dispatcher unavailable.");
            queue.Enqueue(action);
        }
        public void Drain() { while (queue.TryDequeue(out var action)) action(); }
        public void Until(Func<bool> complete)
        {
            var timer = Stopwatch.StartNew();
            while (!complete())
            {
                Drain();
                if (timer.Elapsed > TimeSpan.FromSeconds(10)) throw new TimeoutException("Studio operation did not settle.");
                Thread.Sleep(1);
            }
            Drain();
        }
    }
    private sealed class AnalysisService : IStudioAnalysisService
    {
        public List<(StudioDocumentDraft Draft, CancellationToken Token, TaskCompletionSource<StudioAnalysis> Completion)> Pending { get; } = [];
        public Task<StudioAnalysis> AnalyzeAsync(StudioDocumentDraft draft, CancellationToken cancellationToken)
        {
            var completion = new TaskCompletionSource<StudioAnalysis>(TaskCreationOptions.RunContinuationsAsynchronously);
            Pending.Add((draft, cancellationToken, completion));
            return completion.Task;
        }
    }
    private static void ControllerChecks()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        Xui.Experimental.Portable.Stack root;
        using (var build = host.BeginBuild())
        {
            root = host.Stack(Axis.Vertical);
            root.Add(host.Label("Workspace controller model probe"));
            host.SetContent(root);
            build.Complete();
        }
        var errors = new List<Exception>();
        var service = new AnalysisService();
        var controller = new WorkspaceStudioController(host, service, errors.Add);
        var observed = new List<WorkspaceStudioState>();
        controller.Attach(host.GetComponentLifetime(root), observed.Add);
        Assert(controller.VisibleKeys.Length == 10000 && observed.Count == 1 && service.Pending.Count == 0,
            "Controller publishes initial plain state without allocating catalog controls or starting analysis.");
        var keys = controller.VisibleKeys;
        long version = controller.State.CatalogVersion;
        controller.SetBody("doc-00001", "One two\n- three four\n\n- five");
        Assert(controller.VisibleKeys == keys && controller.State.CatalogVersion == version &&
            controller.State.Session.ChangedCount == 1, "Typing into document does not republish an unchanged 10k catalog.");
        controller.SetQuery("doc-00001");
        Assert(controller.VisibleKeys.SequenceEqual(["doc-00001"]) && controller.State.VisibleDocuments == 1 &&
            controller.State.CatalogVersion == version + 1, "Catalog projection has a monotonic source version when actual keys change.");
        controller.SetQuery("");
        controller.Navigate(StudioSection.Drafts);
        Assert(controller.VisibleKeys.SequenceEqual(["doc-00001"]), "Drafts navigation shows changed documents only.");
        controller.CloseTab("doc-00001");
        Assert(controller.State.Session.ChangedCount == 1 && controller.State.Session.ActiveDocument == "doc-00002",
            "Controller keeps closed dirty document data.");
        controller.OpenDocument("doc-00001");
        controller.Analyze();
        Assert(controller.State.Analyzing && service.Pending.Count == 1 && !controller.State.CanAnalyze &&
            controller.CaptureSession().AnalysisInterrupted, "Owned analysis starts from immutable captured draft.");
        var first = service.Pending[0];
        controller.SetBody("doc-00001", "Updated content\n- item");
        Assert(first.Token.IsCancellationRequested && !controller.State.Analyzing && controller.State.Analysis is null,
            "Typing cancels pending analysis without blocking native editing.");
        controller.Analyze();
        var second = service.Pending[1];
        second.Completion.SetResult(new("doc-00001", 22, 4, 2, 1));
        dispatcher.Until(() => !controller.State.Analyzing);
        Assert(controller.State.Analysis == new StudioAnalysis("doc-00001", 22, 4, 2, 1),
            "Newest local analysis delivers expected literal counts.");
        Assert(!controller.LastOperation.IsCompleted, "LastOperation still owns superseded producer completion.");
        first.Completion.SetResult(StudioAnalysis.Calculate(first.Draft));
        dispatcher.Until(() => controller.LastOperation.IsCompleted);
        controller.LastOperation.GetAwaiter().GetResult();
        Assert(controller.State.Analysis == new StudioAnalysis("doc-00001", 22, 4, 2, 1),
            "Late superseded completion cannot overwrite newest analysis.");
        controller.Analyze();
        var third = service.Pending[2];
        controller.ActivateTab("doc-00002");
        Assert(third.Token.IsCancellationRequested && !controller.State.Analyzing && controller.State.Analysis is null,
            "Switching active document cancels old analysis and clears its visible result.");
        third.Completion.SetResult(StudioAnalysis.Calculate(third.Draft));
        dispatcher.Until(() => controller.LastOperation.IsCompleted);
        controller.Analyze();
        var fourth = service.Pending[3];
        controller.CancelAnalysis();
        fourth.Completion.SetCanceled(fourth.Token);
        dispatcher.Until(() => controller.LastOperation.IsCompleted);
        Assert(controller.State.Status == "Analysis canceled. Local draft kept." && controller.State.Error == "",
            "Explicit cancellation is distinguishable from failure.");
        controller.Analyze();
        service.Pending[4].Completion.SetException(new IOException("PRIVATE_DOCUMENT_CONTENT"));
        dispatcher.Until(() => controller.LastOperation.IsCompleted);
        Assert(controller.State.Status == "Analysis failed. Local draft kept." &&
            !controller.State.Error.Contains("PRIVATE") && errors.Count == 0, "Service errors are observed and content is not logged.");
        controller.Analyze();
        var pending = service.Pending[5];
        var checkpoint = controller.CaptureSession();
        var owner = host.GetComponentLifetime(root);
        host.Dispose();
        Assert(owner.Token.IsCancellationRequested && pending.Token.IsCancellationRequested, "Root component disposal cancels the current analysis.");
        int statesBefore = observed.Count;
        pending.Completion.SetResult(StudioAnalysis.Calculate(pending.Draft));
        dispatcher.Until(() => controller.LastOperation.IsCompleted);
        controller.LastOperation.GetAwaiter().GetResult();
        Assert(observed.Count == statesBefore && errors.Count == 0, "Late results never update retired components.");
        Throws<ObjectDisposedException>(() => controller.CaptureSession());
        using var restoredHost = new Host(new Dispatcher());
        using var restored = new WorkspaceStudioController(restoredHost, new AnalysisService(), errors.Add,
            WorkspaceStudioSessionCodec.Restore(WorkspaceStudioSessionCodec.Serialize(checkpoint)));
        Assert(!restored.State.Analyzing && restored.State.Status == "Analysis interrupted. Local drafts restored; analyze again when ready." &&
            restored.State.Session.ActiveDocument == "doc-00002" && restored.State.Session.ChangedCount == 1,
            "Restoration is idle, preserves drafts/routes, and does not resume background work.");
    }
}
