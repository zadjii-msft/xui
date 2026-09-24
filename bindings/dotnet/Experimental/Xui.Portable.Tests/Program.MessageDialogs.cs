using PortableDemo;
using PortableNavigation;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void MessageDialogChecks()
    {
        MessageDialogValues();
        MessageDialogCompletion();
        MessageDialogCancellation();
        MessageDialogFailures();
        MessageDialogInputGuard();
        MessageDialogPageLifetime();
    }

    private static void MessageDialogValues()
    {
        var confirm = MessageDialogRequest.Confirm("Revert draft?", "Line one\r\nLine two \ud83d\ude80", "Revert", "Keep draft");
        Assert(confirm.Kind == MessageDialogKind.Confirm && confirm.DefaultDecision == MessageDialogDecision.Declined &&
            confirm.Message == "Line one\r\nLine two \ud83d\ude80" && confirm.DeclineText == "Keep draft",
            "Confirm defaults to decline and preserves explicitly requested text without normalization.");
        var alert = MessageDialogRequest.Alert("Notice", "No data changed.");
        Assert(alert.Kind == MessageDialogKind.Alert && alert.DefaultDecision == MessageDialogDecision.Accepted &&
            alert.AcceptText == "OK" && alert.DeclineText is null, "Alert exposes one explicit acknowledgment action.");
        foreach (string invalid in new[] { "", "  ", "\0", "\ud800", "\udc00", "first\nsecond", "first\u2028second" })
        {
            Throws<ArgumentException>(() => MessageDialogRequest.Alert(invalid, "Message"));
            Throws<ArgumentException>(() => MessageDialogRequest.Confirm("Title", "Message", invalid, "Decline"));
            Throws<ArgumentException>(() => MessageDialogRequest.Confirm("Title", "Message", "Accept", invalid));
        }
        foreach (string invalid in new[] { "", " ", "bad\0body", "bad\ud800body" })
            Throws<ArgumentException>(() => MessageDialogRequest.Alert("Title", invalid));
        Throws<ArgumentException>(() => MessageDialogRequest.Alert(new string('x', 257), "Body"));
        Throws<ArgumentException>(() => MessageDialogRequest.Alert("Title", new string('x', 8193)));
        Throws<ArgumentException>(() => MessageDialogRequest.Alert("Title", "Body", new string('x', 81)));
        _ = MessageDialogRequest.Confirm(new string('x', 256), new string('x', 8192), new string('a', 80), new string('b', 80));
    }

    private static void MessageDialogCompletion()
    {
        var request = MessageDialogRequest.Confirm("Revert", "Discard the local draft?");
        using (var host = new Host(new Dispatcher()))
        {
            _ = new Greeting(host);
            Assert(host.GetMessageDialogAvailability() == CapabilityAvailability.Unsupported, "Detached hosts do not advertise native dialogs.");
            Throws<InvalidOperationException>(() => host.ShowMessageAsync(request));
            var legacy = new Backend();
            host.Attach(legacy);
            var unsupported = host.ShowMessageAsync(request).GetAwaiter().GetResult();
            Assert(unsupported.Status == OperationStatus.Unsupported && host.IsAttached, "Missing dialog authority/capability is an explicit unsupported result.");
        }
        foreach (var expected in new[]
        {
            OperationResult<MessageDialogDecision>.Completed(MessageDialogDecision.Accepted),
            OperationResult<MessageDialogDecision>.Completed(MessageDialogDecision.Declined),
            OperationResult<MessageDialogDecision>.Cancelled(),
            OperationResult<MessageDialogDecision>.Denied(),
            OperationResult<MessageDialogDecision>.Unsupported(),
            OperationResult<MessageDialogDecision>.Failed(new ApplicationException("Native dialog unavailable."))
        })
        {
            using var host = new Host(new Dispatcher());
            _ = new Greeting(host);
            var backend = new MessageBackend();
            host.Attach(backend);
            Assert(host.GetMessageDialogAvailability() == CapabilityAvailability.Available, "Explicit native outer-surface authority is reported.");
            var task = host.ShowMessageAsync(request);
            var native = backend.Requests.Single();
            Assert(!task.IsCompleted && ReferenceEquals(native.Request, request), "Begin transfers one owned request without completing inline.");
            Throws<InvalidOperationException>(() => host.ShowMessageAsync(request));
            native.Complete(expected);
            Assert(ReferenceEquals(task.GetAwaiter().GetResult(), expected) && native.DisposeCalls == 1 &&
                native.DisposeThread == Environment.CurrentManagedThreadId, "Native completion releases the request once on UI before delivering its exact result.");
            native.Complete(OperationResult<MessageDialogDecision>.Cancelled());
            Assert(native.DisposeCalls == 1 && ReferenceEquals(task.Result, expected), "Late duplicate completion cannot replace the winning result.");
            var next = host.ShowMessageAsync(MessageDialogRequest.Alert("Notice", "Still open."));
            backend.Requests.Last().Complete(OperationResult<MessageDialogDecision>.Completed(MessageDialogDecision.Accepted));
            Assert(next.Result.Value == MessageDialogDecision.Accepted && backend.Requests.Count == 2, "A settled request permits the next explicit dialog without a queue.");
        }
        using (var host = new Host(new Dispatcher()))
        {
            _ = new Greeting(host);
            var backend = new MessageBackend { Availability = CapabilityAvailability.Unsupported };
            host.Attach(backend);
            Assert(host.ShowMessageAsync(request).Result.Status == OperationStatus.Unsupported && backend.Requests.Count == 0,
                "A borrowed native surface never opens an outer-window modal without explicit authority.");
            backend.Availability = CapabilityAvailability.RequiresUserGesture;
            var task = host.ShowMessageAsync(request);
            backend.Requests.Single().Complete(OperationResult<MessageDialogDecision>.Denied());
            Assert(task.Result.Status == OperationStatus.Denied, "Gesture availability is advisory; the native operation may explicitly deny.");
        }
    }

    private static void MessageDialogCancellation()
    {
        var request = MessageDialogRequest.Confirm("Revert", "Discard local changes?");
        using var host = new Host(new Dispatcher());
        _ = new Greeting(host);
        var backend = new MessageBackend();
        host.Attach(backend);
        using (var alreadyCanceled = new CancellationTokenSource())
        {
            alreadyCanceled.Cancel();
            var task = host.ShowMessageAsync(request, alreadyCanceled.Token);
            Throws<OperationCanceledException>(() => task.GetAwaiter().GetResult());
            Assert(backend.Requests.Count == 0, "A pre-canceled request does not enter native UI.");
        }
        using (var cancellation = new CancellationTokenSource())
        {
            var task = host.ShowMessageAsync(request, cancellation.Token);
            var native = backend.Requests.Last();
            Task.Run(cancellation.Cancel).GetAwaiter().GetResult();
            Assert(native.CancelCalls == 1 && native.CancelThread != Environment.CurrentManagedThreadId &&
                native.DisposeCalls == 0 && !task.IsCompleted, "Worker cancellation requests native closure without worker Dispose or premature completion.");
            Throws<InvalidOperationException>(() => host.ShowMessageAsync(request));
            native.Complete(OperationResult<MessageDialogDecision>.Completed(MessageDialogDecision.Accepted));
            Throws<OperationCanceledException>(() => task.GetAwaiter().GetResult());
            Assert(native.DisposeCalls == 1 && native.DisposeThread == Environment.CurrentManagedThreadId,
                "Cancellation wins a racing native click only after native completion/unwind acknowledges it.");
        }
        using (var cancellation = new CancellationTokenSource())
        {
            backend.DuringBegin = cancellation.Cancel;
            var task = host.ShowMessageAsync(request, cancellation.Token);
            var native = backend.Requests.Last();
            Assert(native.CancelCalls == 1 && !task.IsCompleted, "Cancellation before native dialog creation is remembered by the returned owned request.");
            backend.DuringBegin = null;
            native.Complete(OperationResult<MessageDialogDecision>.Cancelled());
            Throws<OperationCanceledException>(() => task.GetAwaiter().GetResult());
        }
        using (var cancellation = new CancellationTokenSource())
        {
            var task = host.ShowMessageAsync(request, cancellation.Token);
            var native = backend.Requests.Last();
            native.Complete(OperationResult<MessageDialogDecision>.Completed(MessageDialogDecision.Declined));
            cancellation.Cancel();
            Assert(task.Result.Value == MessageDialogDecision.Declined && native.CancelCalls == 0, "A completed decline cannot be replaced by later token cancellation.");
        }
        var pending = host.ShowMessageAsync(request);
        var retired = backend.Requests.Last();
        host.Detach();
        Throws<OperationCanceledException>(() => pending.GetAwaiter().GetResult());
        Assert(retired.DisposeCalls == 1 && backend.Trace.IndexOf("request-dispose") < backend.Trace.LastIndexOf("backend-dispose"),
            "Attachment retirement cancels logical completion and revokes the native request before backend unmount.");
        retired.Complete(OperationResult<MessageDialogDecision>.Completed(MessageDialogDecision.Accepted));
        Assert(pending.IsCanceled && retired.DisposeCalls == 1, "Late native completion after owner teardown cannot resurrect a request.");
        var nextBackend = new MessageBackend();
        host.Attach(nextBackend);
        var nextTask = host.ShowMessageAsync(request);
        retired.Complete(OperationResult<MessageDialogDecision>.Cancelled());
        Assert(!nextTask.IsCompleted, "Old request callbacks cannot complete a replacement attachment's dialog.");
        nextBackend.Requests.Single().Complete(OperationResult<MessageDialogDecision>.Cancelled());
        Assert(nextTask.Result.Status == OperationStatus.Cancelled, "A native user cancellation remains distinct from a canceled Task.");
    }

    private static void MessageDialogFailures()
    {
        var request = MessageDialogRequest.Confirm("Title", "Private user-authored message.");
        using (var host = new Host(new Dispatcher()))
        {
            var app = new Greeting(host);
            var backend = new MessageBackend();
            host.Attach(backend);
            Task.Run(() => Throws<InvalidOperationException>(() => host.ShowMessageAsync(request))).GetAwaiter().GetResult();
            backend.DuringAvailability = () => Throws<InvalidOperationException>(() => app.Entry = "reentrant availability");
            backend.DuringBegin = () => Throws<InvalidOperationException>(() => app.Entry = "reentrant begin");
            var task = host.ShowMessageAsync(request);
            Assert(app.Entry == "", "Native preflight and Begin cannot reenter authored mutation.");
            var native = backend.Requests.Single();
            Task.Run(() => Throws<InvalidOperationException>(() => native.Complete(OperationResult<MessageDialogDecision>.Cancelled()))).GetAwaiter().GetResult();
            Assert(!task.IsCompleted, "Wrong-thread completion is rejected without manufacturing a result.");
            native.Complete(OperationResult<MessageDialogDecision>.Cancelled());
        }
        foreach (string mode in new[] { "inline", "null-handle", "begin-throws", "invalid-result", "invalid-decision", "alert-declined", "dispose-throws", "begin-and-cleanup" })
        {
            using var host = new Host(new Dispatcher());
            _ = new Greeting(host);
            var backend = new MessageBackend { Mode = mode };
            host.Attach(backend);
            var task = host.ShowMessageAsync(mode == "alert-declined" ? MessageDialogRequest.Alert("Alert", "Acknowledge.") : request);
            if (mode is "invalid-result" or "invalid-decision" or "alert-declined" or "dispose-throws")
            {
                var outcome = mode == "invalid-result" ? null! :
                    OperationResult<MessageDialogDecision>.Completed(mode == "invalid-decision" ? (MessageDialogDecision)99 : MessageDialogDecision.Declined);
                backend.Requests.Single().Complete(outcome);
            }
            var error = Throws<Exception>(() => task.GetAwaiter().GetResult());
            Assert(task.IsFaulted && host.IsAttached && backend.Requests.All(native => native.DisposeCalls == 1),
                mode + " exposes protocol/cleanup failure and retires owned handles without false success.");
            if (mode == "begin-and-cleanup")
            {
                var errors = ((AggregateException)error).Flatten().InnerExceptions.Select(value => value.Message).ToArray();
                Assert(errors.Any(value => value.Contains("posted after Begin", StringComparison.Ordinal)) && errors.Contains("dialog dispose"),
                    "A bad Begin and failed cleanup are reported together.");
            }
        }
        foreach (bool worker in new[] { false, true })
        foreach (bool teardown in new[] { false, true })
        {
            using var host = new Host(new Dispatcher());
            _ = new Greeting(host);
            var backend = new MessageBackend();
            host.Attach(backend);
            using var cancellation = new CancellationTokenSource();
            var task = host.ShowMessageAsync(request, cancellation.Token);
            var native = backend.Requests.Single();
            native.FailCancel = true;
            if (worker) Task.Run(cancellation.Cancel).GetAwaiter().GetResult();
            else cancellation.Cancel();
            Throws<ApplicationException>(() => task.GetAwaiter().GetResult());
            Assert(native.DisposeCalls == 0, "Failed cancellation transport retains the native request on either calling thread.");
            Throws<InvalidOperationException>(() => host.ShowMessageAsync(request));
            Assert(!backend.Find("increment").Events.Click() && !backend.Find("name").Events.Change("queued while cancellation failed"),
                "A transport failure keeps originating-host user input blocked until native completion.");
            if (teardown)
            {
                host.Detach();
                native.Complete(OperationResult<MessageDialogDecision>.Cancelled());
                backend = new MessageBackend();
                host.Attach(backend);
            }
            else native.Complete(OperationResult<MessageDialogDecision>.Cancelled());
            Assert(native.DisposeCalls == 1 && task.IsFaulted, "A cancellation transport fault remains explicit after native cleanup.");
            Assert(backend.Find("increment").Events.Click(), "Native completion or owner teardown releases the modal input guard after failed cancellation.");
            var next = host.ShowMessageAsync(request);
            backend.Requests.Last().Complete(OperationResult<MessageDialogDecision>.Completed(MessageDialogDecision.Declined));
            Assert(next.Result.Value == MessageDialogDecision.Declined, "A closed faulted request does not poison future dialogs.");
        }
        using (var host = new Host(new Dispatcher()))
        {
            _ = new Greeting(host);
            var backend = new MessageBackend();
            host.Attach(backend);
            using var cancellation = new CancellationTokenSource();
            var task = host.ShowMessageAsync(request, cancellation.Token);
            var native = backend.Requests.Single();
            native.CompleteDuringCancel = true;
            cancellation.Cancel();
            Throws<InvalidOperationException>(() => task.GetAwaiter().GetResult());
            Assert(native.DisposeCalls == 1 && host.IsAttached, "An inline native cancellation completion is rejected and its owned request closes on UI.");
        }
        foreach (bool worker in new[] { false, true })
        using (var host = new Host(new Dispatcher()))
        {
            _ = new Greeting(host);
            var backend = new MessageBackend();
            host.Attach(backend);
            using var cancellation = new CancellationTokenSource();
            var task = host.ShowMessageAsync(request, cancellation.Token);
            var native = backend.Requests.Single();
            native.FailCancel = true;
            native.FailDispose = true;
            if (worker) Task.Run(cancellation.Cancel).GetAwaiter().GetResult();
            else cancellation.Cancel();
            Throws<ApplicationException>(() => task.GetAwaiter().GetResult());
            var lateCleanup = Throws<ApplicationException>(() => native.Complete(OperationResult<MessageDialogDecision>.Cancelled()));
            Assert(lateCleanup.Message == "dialog dispose" && native.DisposeCalls == 1,
                "Cleanup failure after an already-observed transport fault is surfaced on the native callback boundary, not swallowed.");
        }
        using (var host = new Host(new Dispatcher()))
        {
            _ = new Greeting(host);
            var backend = new MessageBackend();
            host.Attach(backend);
            using var cancellation = new CancellationTokenSource();
            var task = host.ShowMessageAsync(request, cancellation.Token);
            var native = backend.Requests.Single();
            native.DuringDispose = cancellation.Cancel;
            native.Complete(OperationResult<MessageDialogDecision>.Completed(MessageDialogDecision.Accepted));
            Throws<OperationCanceledException>(() => task.GetAwaiter().GetResult());
            Assert(native.DisposeCalls == 1 && native.CancelCalls == 0,
                "Cancellation observed before logical completion wins without reentering a retired native request.");
            backend.Availability = (CapabilityAvailability)99;
            Throws<InvalidOperationException>(() => host.ShowMessageAsync(request));
            Assert(backend.Requests.Count == 1, "Malformed availability is rejected without entering native UI.");
        }
        using (var host = new Host(new Dispatcher()))
        {
            _ = new Greeting(host);
            var backend = new MessageBackend { Mode = "dispose-throws" };
            host.Attach(backend);
            var task = host.ShowMessageAsync(request);
            Throws<AggregateException>(() => host.Detach());
            Throws<ApplicationException>(() => task.GetAwaiter().GetResult());
            Assert(backend.Disposed && backend.Peers.All(peer => peer.Disposed) && backend.Requests.Single().DisposeCalls == 1,
                "Owner teardown continues through dialog cleanup failure without losing its faulted Task.");
        }
        using (var host = new Host(new Dispatcher()))
        {
            using var build = host.BeginBuild();
            var root = host.Stack(Axis.Vertical);
            Throws<InvalidOperationException>(() => host.ShowMessageAsync(request));
            host.SetContent(root);
            build.Complete();
        }
    }

    private sealed class MessageBackend : IMessageDialogBackend
    {
        public List<MessageRequest> Requests { get; } = [];
        public List<MessagePeer> Peers { get; } = [];
        public List<string> Trace { get; } = [];
        public string Mode { get; set; } = "";
        public CapabilityAvailability Availability { get; set; } = CapabilityAvailability.Available;
        public Action? DuringAvailability { get; set; }
        public Action? DuringBegin { get; set; }
        public bool Disposed { get; private set; }
        public MessagePeer Find(string id) => Peers.Last(peer => peer.Id == id);
        public CapabilityAvailability MessageDialogAvailability
        {
            get { DuringAvailability?.Invoke(); return Availability; }
        }
        public IMessageDialogRequest BeginMessageDialog(MessageDialogRequest request, Action<OperationResult<MessageDialogDecision>> completed)
        {
            DuringBegin?.Invoke();
            if (Mode == "begin-throws") throw new ApplicationException("dialog begin");
            if (Mode == "null-handle") return null!;
            var native = new MessageRequest(this, request, completed) { FailDispose = Mode is "dispose-throws" or "begin-and-cleanup" };
            Requests.Add(native);
            if (Mode is "inline" or "begin-and-cleanup") native.Complete(OperationResult<MessageDialogDecision>.Completed(MessageDialogDecision.Accepted));
            return native;
        }
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new MessagePeer(element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() { Disposed = true; Trace.Add("backend-dispose"); }
    }

    private sealed class MessageRequest(MessageBackend backend, MessageDialogRequest request,
        Action<OperationResult<MessageDialogDecision>> completed) : IMessageDialogRequest
    {
        private int cancels;
        public MessageDialogRequest Request { get; } = request;
        public int CancelCalls => Volatile.Read(ref cancels);
        public int CancelThread { get; private set; }
        public int DisposeCalls { get; private set; }
        public int DisposeThread { get; private set; }
        public bool FailCancel { get; set; }
        public bool FailDispose { get; set; }
        public bool CompleteDuringCancel { get; set; }
        public Action? DuringDispose { get; set; }
        public void Cancel()
        {
            CancelThread = Environment.CurrentManagedThreadId;
            Interlocked.Increment(ref cancels);
            if (FailCancel) throw new ApplicationException("dialog cancel transport");
            if (CompleteDuringCancel) completed(OperationResult<MessageDialogDecision>.Cancelled());
        }
        public void Complete(OperationResult<MessageDialogDecision> result) => completed(result);
        public void Dispose()
        {
            DisposeCalls++;
            DisposeThread = Environment.CurrentManagedThreadId;
            backend.Trace.Add("request-dispose");
            DuringDispose?.Invoke();
            if (FailDispose) throw new ApplicationException("dialog dispose");
        }
    }

    private sealed class MessagePeer(Element element, IControlEvents events) : IPageViewElementPeer, IPageSelectorElementPeer, IRangeElementPeer
    {
        public string Id { get; } = element is Control control ? control.AutomationId : "";
        public IControlEvents Events { get; } = events;
        public bool Disposed { get; private set; }
        public void AddChild(IElementPeer child) { }
        public void InsertChild(int index, IElementPeer child) { }
        public void RemoveChild(IElementPeer child) { }
        public void MoveChild(IElementPeer child, int index) { }
        public void ValidateMove(IElementPeer child, int index) { }
        public void ValidatePages(IReadOnlyList<PageEntry> pages, ulong? selected) { }
        public void ValidateVisibility(bool visible) { }
        public void ConnectPages(IPageViewElementPeer pages) { }
        public void ValidateRange(NumericRange range, double value) { }
        public void Update(ElementProperty property) { }
        public void Dispose() => Disposed = true;
    }

    private static void MessageDialogInputGuard()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        RetainedPagesWorkbench app;
        RangeInput range;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            app = new RetainedPagesWorkbench(host);
            range = host.RangeInput("Background range");
            range.AutomationId = "background-range";
            root.Add(app.Root).Add(range);
            host.SetContent(root);
            build.Complete();
        }
        app.Open();
        var backend = new MessageBackend();
        host.Attach(backend);
        var button = backend.Find("first-increment").Events;
        var input = backend.Find("first-input").Events;
        var tabs = (IPageControlEvents)backend.Find("workspace-tabs").Events;
        var rangeEvents = (IRangeControlEvents)backend.Find("background-range").Events;
        int cancelled = 0;
        range.Canceled += _ => cancelled++;
        rangeEvents.RangePreviewed(20);
        int accepted = 0;
        dispatcher.Post(() =>
        {
            if (button.Click()) accepted++;
            if (input.Change("queued background edit")) accepted++;
            if (input.Submit()) accepted++;
            if (tabs.PageSelected(9007199254740993UL)) accepted++;
            if (tabs.PageCloseRequested(1)) accepted++;
        });
        using var cancellation = new CancellationTokenSource();
        var pending = host.ShowMessageAsync(MessageDialogRequest.Confirm("Revert", "Discard the draft?"), cancellation.Token);
        dispatcher.Drain();
        Assert(accepted == 0 && app.Documents.Selected == 1 && app.Changes == 0,
            "Accepted dialog ownership blocks previously queued originating-host user input before native modality appears.");
        Assert(((ITextInteractionEvents)input).InteractionChanged(new(false, false)),
            "Modal ownership does not block focus and composition metadata.");
        rangeEvents.RangeCanceled(0);
        Assert(range.PreviewValue == 0 && cancelled == 0, "A modal-owned range cancellation clears preview without an authored user callback.");
        app.Selection = "Programmatic update remains legal";
        Assert(app.Selection == "Programmatic update remains legal", "Modal input blocking does not prohibit application model updates.");
        var native = backend.Requests.Single();
        native.FailCancel = true;
        Task.Run(cancellation.Cancel).GetAwaiter().GetResult();
        Throws<ApplicationException>(() => pending.GetAwaiter().GetResult());
        Assert(!button.Click() && !tabs.PageSelected(9007199254740993UL),
            "A failed cancellation transport keeps user input blocked until native completion or owner teardown.");
        native.Complete(OperationResult<MessageDialogDecision>.Cancelled());
        Assert(button.Click() && tabs.PageSelected(9007199254740993UL),
            "User input resumes only after the request is retired.");
    }

    private static void MessageDialogPageLifetime()
    {
        using var host = new Host(new Dispatcher());
        var app = new RetainedPagesWorkbench(host);
        PortableMutation.MutationRow? row = null;
        app.OpenPages = ([PageItem.Create(1, "Dialog owner", h => row = new PortableMutation.MutationRow(h, "dialog-owner"))], 1UL);
        var backend = new MessageBackend();
        host.Attach(backend);
        var task = host.ShowMessageAsync(MessageDialogRequest.Confirm("Revert", "Discard the local draft?"), row!.Lifetime.Token);
        var native = backend.Requests.Single();
        app.OpenPages = ([], null);
        Assert(native.CancelCalls == 1 && native.DisposeCalls == 0 && !task.IsCompleted,
            "A retiring page lifetime requests cancellation without claiming the modal stack has already unwound.");
        native.Complete(OperationResult<MessageDialogDecision>.Cancelled());
        Throws<OperationCanceledException>(() => task.GetAwaiter().GetResult());
        Assert(native.DisposeCalls == 1 && host.IsAttached, "Native completion retires the canceled request after its page is gone.");
    }
}
