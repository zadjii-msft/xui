using PortableDemo;
using PortableMutation;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static VirtualViewportRequest Request(long epoch, long version = 1, bool blocked = false) =>
        new(epoch, 0, version, new(0, 300, 0, 320000), new(0, 300, 400, 320000), blocked);

    private static void ViewportLeaseChecks()
    {
        using (var host = new Host(new Dispatcher()))
        {
            var demo = new Greeting(host);
            var scroll = (ScrollView)demo.Root.Children[1];
            var backend = new ViewportBackend();
            host.Attach(backend);
            int callbacks = 0;
            IVirtualViewportLease? lease = null;
            lease = host.BeginVirtualViewport(scroll, 10000, 32, 1, request =>
            {
                callbacks++;
                Assert(lease!.TryBeginUpdate(request.Epoch) == VirtualViewportUpdateResult.Ready, "Native epoch reservation.");
                Assert(!backend.Find("name").Events.Change("reentrant input"), "Authored input is suppressed while a viewport batch stages.");
                demo.Entry = "staged";
                Assert(lease.TryCommit(request.Epoch) == VirtualViewportCommitResult.Committed, "A reserved viewport commits after model staging.");
            });
            Throws<InvalidOperationException>(() => host.BeginVirtualViewport(scroll, 1, 32, 1, _ => { }));
            var native = backend.Leases.Single();
            scroll.Enabled = false;
            scroll.Visible = false;
            native.Emit(Request(1));
            Assert(callbacks == 1 && demo.Entry == "staged" && host.IsAttached, "Request callbacks may stage outside the backend operation guard.");
            native.Emit(Request(1));
            Assert(callbacks == 1, "Duplicate or stale epochs are inert.");
            lease.RequestOffset(1000);
            Assert(native.Offset == 1000, "Programmatic navigation requests rather than directly commits an offset.");
            lease.SetExtent(10000, 2);
            Assert(native.SourceVersion == 2, "Equal-count source changes still advance the source version.");
            Throws<ArgumentOutOfRangeException>(() => lease.SetExtent(10000, 2));
            Throws<ArgumentOutOfRangeException>(() => lease.RequestOffset(float.NaN));
            Throws<ArgumentOutOfRangeException>(() => lease.TryBeginUpdate(0));
            Throws<ArgumentOutOfRangeException>(() => lease.SetExtent(-1, 3));
            Task.Run(() => Throws<InvalidOperationException>(() => lease.RequestOffset(1))).GetAwaiter().GetResult();
            host.Detach();
            Assert(native.DisposeCalls == 1 && backend.Trace.IndexOf("lease") < backend.Trace.IndexOf("backend") &&
                backend.Trace.IndexOf("backend") < backend.Trace.IndexOf("peer"), "Attachment leases retire before backend unmount and reverse peer disposal.");
            native.Emit(Request(3, 2));
            Assert(callbacks == 1, "Old queued viewport callbacks cannot run after detach.");
            lease.Dispose();
            Assert(native.DisposeCalls == 1, "Disposal after attachment retirement is idempotent.");
            Throws<ObjectDisposedException>(() => lease.RequestOffset(1));
            var replacement = new ViewportBackend();
            host.Attach(replacement);
            using var next = host.BeginVirtualViewport(scroll, 10000, 32, 2, _ => callbacks++);
            native.Emit(Request(4, 2));
            replacement.Leases[0].Emit(Request(1, 2));
            Assert(callbacks == 2, "A reattached lease has its own epoch while logical source versions persist.");
        }
        using (var host = new Host(new Dispatcher()))
        {
            var demo = new Greeting(host);
            var backend = new ViewportBackend();
            host.Attach(backend);
            IVirtualViewportLease? lease = null;
            lease = host.BeginVirtualViewport((ScrollView)demo.Root.Children[1], 1, 32, 1, request =>
            {
                var result = lease!.TryBeginUpdate(request.Epoch);
                if (result == VirtualViewportUpdateResult.Ready) lease.Cancel(request.Epoch);
            });
            var native = backend.Leases.Single();
            native.Emit(Request(1));
            Assert(host.IsAttached && backend.Find("name").Events.Change("after cancel"), "Safe cancellation restores ordinary input without detaching.");
            native.UpdateResult = VirtualViewportUpdateResult.Blocked;
            native.Emit(Request(2, blocked: true));
            native.UpdateResult = VirtualViewportUpdateResult.Superseded;
            native.Emit(Request(3));
            Assert(host.IsAttached && backend.Find("name").Events.Change("after blocked"), "Blocked and superseded reservations never enter a staging guard.");
            lease.Dispose();
        }
        using (var host = new Host(new Dispatcher()))
        {
            var demo = new Greeting(host);
            var backend = new ViewportBackend();
            host.Attach(backend);
            var scroll = (ScrollView)demo.Root.Children[1];
            Throws<ArgumentOutOfRangeException>(() => host.BeginVirtualViewport(scroll, -1, 32, 1, _ => { }));
            Throws<ArgumentOutOfRangeException>(() => host.BeginVirtualViewport(scroll, 1, float.NaN, 1, _ => { }));
            Throws<ArgumentOutOfRangeException>(() => host.BeginVirtualViewport(scroll, 2, float.MaxValue, 1, _ => { }));
            Throws<ArgumentOutOfRangeException>(() => host.BeginVirtualViewport(scroll, 1, 32, 0, _ => { }));
            Assert(backend.Leases.Count == 0, "Invalid viewport arguments do not open native resources.");
            var lease = host.BeginVirtualViewport(scroll, 1, 32, 1, _ => { });
            demo.Lifetime.Own(lease);
            host.Dispose();
            Assert(backend.Leases.Single().DisposeCalls == 1, "Component-owned lease handles may dispose safely during terminal lifetime cleanup.");
        }
        using (var host = new Host(new Dispatcher()))
        {
            var demo = new Greeting(host);
            var scroll = (ScrollView)demo.Root.Children[1];
            var unsupported = new Backend();
            host.Attach(unsupported);
            Throws<NotSupportedException>(() => host.BeginVirtualViewport(scroll, 1, 32, 1, _ => { }));
            Assert(host.IsAttached, "Missing native viewport capability is explicit and nondestructive.");
        }
        foreach (string operation in new[] { "begin", "extent", "offset", "prepare", "commit", "cancel" })
        {
            using var host = new Host(new Dispatcher());
            var demo = new Greeting(host);
            var backend = new ViewportBackend();
            host.Attach(backend);
            var scroll = (ScrollView)demo.Root.Children[1];
            int callbacks = 0;
            if (operation == "begin")
            {
                backend.SynchronousBegin = true;
                Throws<InvalidOperationException>(() => host.BeginVirtualViewport(scroll, 1, 32, 1, _ => callbacks++));
                Assert(callbacks == 0 && backend.Leases.Single().DisposeCalls == 1 && host.IsAttached, "Synchronous Begin callbacks reject without invoking the app or leaking the returned lease.");
                continue;
            }
            using var lease = host.BeginVirtualViewport(scroll, 1, 32, 1, _ => callbacks++);
            var native = backend.Leases.Single();
            native.Emit(Request(1));
            if (operation is "commit" or "cancel") lease.TryBeginUpdate(1);
            native.SynchronousOperation = operation;
            Throws<InvalidOperationException>(() =>
            {
                switch (operation)
                {
                    case "extent": lease.SetExtent(2, 2); break;
                    case "offset": lease.RequestOffset(1); break;
                    case "prepare": lease.TryBeginUpdate(1); break;
                    case "commit": lease.TryCommit(1); break;
                    case "cancel": lease.Cancel(1); break;
                }
            });
            Assert(callbacks == 1 && !host.IsAttached && native.DisposeCalls == 1, operation + " synchronous callbacks fail terminally without reentering authored code.");
        }
        foreach (string failure in new[] { "unsafe-cancel", "blocked-commit", "stale-commit", "native-commit-error", "unfinished-stage" })
        {
            using var host = new Host(new Dispatcher());
            var demo = new Greeting(host);
            var backend = new ViewportBackend();
            host.Attach(backend);
            IVirtualViewportLease? lease = null;
            lease = host.BeginVirtualViewport((ScrollView)demo.Root.Children[1], 1, 32, 1, request =>
            {
                lease!.TryBeginUpdate(request.Epoch);
                demo.Entry = "committed model";
                if (failure == "unfinished-stage") return;
                if (failure == "unsafe-cancel") lease.Cancel(request.Epoch);
                else lease.TryCommit(request.Epoch);
            });
            var native = backend.Leases.Single();
            if (failure == "blocked-commit") native.CommitResult = VirtualViewportCommitResult.Blocked;
            if (failure == "stale-commit") native.CommitResult = VirtualViewportCommitResult.Superseded;
            if (failure == "native-commit-error") native.FailOperation = "commit";
            Throws<Exception>(() => native.Emit(Request(1)));
            Assert(!host.IsAttached && demo.Entry == "committed model" && native.DisposeCalls == 1, failure + " detaches without pretending model rollback.");
        }
        using (var host = new Host(new Dispatcher()))
        {
            var board = new MutationBoard(host);
            ViewportRow? row = null;
            board.Rows = [KeyedItem.Create("viewport", h => row = new ViewportRow(h))];
            var backend = new ViewportBackend();
            host.Attach(backend);
            int callbacks = 0;
            var lease = host.BeginVirtualViewport(row!.Viewport, 1, 32, 1, _ => callbacks++);
            var native = backend.Leases.Single();
            board.Rows = [];
            Assert(native.DisposeCalls == 1 && backend.Trace.IndexOf("lease") < backend.Trace.IndexOf("remove"), "Keyed subtree retirement closes its lease before native removal.");
            native.Emit(Request(1));
            Assert(callbacks == 0, "Removed viewport callbacks stay stale even on the same attachment.");
            lease.Dispose();
        }
        using (var host = new Host(new Dispatcher()))
        {
            var demo = new Greeting(host);
            var backend = new ViewportBackend();
            host.Attach(backend);
            using var lease = host.BeginVirtualViewport((ScrollView)demo.Root.Children[1], 1, 32, 1, _ => { });
            backend.Leases.Single().FailOperation = "dispose";
            var error = Throws<AggregateException>(() => host.Detach());
            Assert(error.InnerExceptions.Any(e => e.Message == "dispose") && backend.Disposed &&
                backend.Peers.All(p => p.DisposeCalls == 1), "Lease cleanup failure never skips native unmount or peer release.");
        }
        VirtualItemMetadataChecks();
        ViewportSettlingChecks();
    }

    private static void VirtualItemMetadataChecks()
    {
        using var host = new Host(new Dispatcher());
        KeyedStack rows;
        ScrollView scroll;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            rows = host.KeyedStack(Axis.Vertical);
            scroll = host.ScrollView(rows, "Virtual rows");
            root.Add(scroll);
            host.SetContent(root);
            build.Complete();
        }
        var backend = new ViewportBackend();
        host.Attach(backend);
        MutationRow? row = null;
        IVirtualViewportLease? lease = null;
        lease = host.BeginVirtualViewport(scroll, 10, 32, 1, request =>
        {
            lease!.TryBeginUpdate(request.Epoch);
            rows.Reconcile([KeyedItem.Create("row", h => row = new MutationRow(h, "row"))]);
            var peer = backend.Peers.Single(p => ReferenceEquals(p.Element, row!.Root));
            var info = new VirtualItemInfo("ordinal row", 3, 10, 1);
            Throws<InvalidOperationException>(() => host.SetVirtualItemInfo(row!.Root, info with { Count = 11 }));
            Throws<InvalidOperationException>(() => host.SetVirtualItemInfo(row!.Root, info with { SourceVersion = 2 }));
            Throws<ArgumentException>(() => host.SetVirtualItemInfo(row!.Root, info with { Key = "bad\0key" }));
            host.SetVirtualItemInfo(row!.Root, info);
            Assert(peer.Item == info, "Virtual item identity and position reach the actual native row peer.");
            lease.SetExtent(20, 2);
            host.SetVirtualItemInfo(row.Root, info);
            Assert(peer.Item == info, "Queued source changes do not alter metadata validation for the reserved source.");
            lease.TryCommit(request.Epoch);
        });
        backend.Leases.Single().Emit(Request(1));
        Throws<InvalidOperationException>(() => host.SetVirtualItemInfo(row!.Root, new("row", 0, 20, 2)));
        using var foreign = new Host(new Dispatcher());
        var foreignRow = new MutationRow(foreign, "foreign");
        Throws<InvalidOperationException>(() => host.SetVirtualItemInfo(foreignRow.Root, new("foreign", 0, 20, 2)));
        var retired = row!.Root;
        rows.Reconcile([]);
        Throws<ObjectDisposedException>(() => host.SetVirtualItemInfo(retired, new("row", 0, 20, 2)));
        lease.Dispose();
    }

    private static void TextInteractionChecks()
    {
        using var host = new Host(new Dispatcher());
        var demo = new Greeting(host);
        var backend = new ViewportBackend();
        int notices = 0;
        demo.Input.InteractionChanged += _ => notices++;
        Assert(demo.Input.Interaction is null, "Interaction begins as unknown, not fabricated focus state.");
        host.Attach(backend);
        var peer = backend.Find("name");
        var events = (ITextInteractionEvents)peer.Events;
        Assert(events.InteractionChanged(new(true, true)) && notices == 1 && demo.Input.Interaction == new TextInteraction(true, true), "Current attachment interaction is published.");
        events.InteractionChanged(new(true, true));
        Assert(notices == 1, "Identical native interaction snapshots are silent.");
        demo.Input.Enabled = false;
        demo.Input.Visible = false;
        Assert(events.InteractionChanged(new(false, false)) && notices == 2, "Blur and IME end bypass disabled and hidden user-input gating.");
        Assert(!peer.Events.Change("disabled"), "Ordinary user input still obeys availability.");
        peer.EchoInteraction = true;
        demo.Input.Enabled = true;
        Assert(notices == 3 && demo.Input.Interaction == new TextInteraction(true, false), "Backend metadata is captured and published after the update guard.");
        Throws<InvalidOperationException>(() => ((ITextInteractionEvents)backend.Find("increment").Events).InteractionChanged(default));
        Task.Run(() => Throws<InvalidOperationException>(() => events.InteractionChanged(default))).GetAwaiter().GetResult();
        host.Detach();
        Assert(demo.Input.Interaction is null && notices == 3, "Detach resets the snapshot silently.");
        Assert(!events.InteractionChanged(new(true, false)), "Detached interaction callbacks are stale.");
        var next = new ViewportBackend();
        host.Attach(next);
        Assert(demo.Input.Interaction is null, "Reattachment waits for its own native initial report.");
        ((ITextInteractionEvents)next.Find("name").Events).InteractionChanged(default);
        Assert(notices == 4 && demo.Input.Interaction == default(TextInteraction), "An actual false/false initial snapshot is still an event.");
        InteractionOperationChecks();
    }

    private static void InteractionOperationChecks()
    {
        using var host = new Host(new Dispatcher());
        var demo = new Greeting(host);
        var backend = new ViewportBackend();
        host.Attach(backend);
        var peer = backend.Find("name");
        int notices = 0;
        demo.Input.InteractionChanged += state =>
        {
            Assert(!peer.InOperation, "Authored interaction handlers never run inside native focus/update calls.");
            Assert(demo.Input.Interaction == state, "The snapshot is synchronized before its notification.");
            notices++;
        };
        Assert(host.TryFocus(demo.Input) && host.HasFocus(demo.Input) && notices == 1 &&
            demo.Input.Interaction == new TextInteraction(true, false), "Synchronous native focus cannot leave interaction metadata unknown.");
        peer.FocusAction = () =>
        {
            ((ITextInteractionEvents)peer.Events).InteractionChanged(new(false, true));
            ((ITextInteractionEvents)peer.Events).InteractionChanged(new(false, false));
        };
        host.TryFocus(demo.Input);
        Assert(notices == 2 && demo.Input.Interaction == new TextInteraction(false, false), "Multiple native reports in one operation coalesce to the last snapshot.");
        peer.FocusAction = null;
        peer.EchoInteraction = true;
        demo.Input.Enabled = false;
        Assert(notices == 3, "Synchronous disabled-state metadata is not discarded.");
        demo.Input.Enabled = true;
        IVirtualViewportLease? lease = null;
        lease = host.BeginVirtualViewport((ScrollView)demo.Root.Children[1], 1, 32, 1, request =>
        {
            int before = notices;
            lease!.TryBeginUpdate(request.Epoch);
            peer.FocusAction = () => ((ITextInteractionEvents)peer.Events).InteractionChanged(new(true, true));
            host.TryFocus(demo.Input);
            Assert(notices == before && demo.Input.Interaction == new TextInteraction(true, true), "Viewport staging captures native truth without reentrant authored notices.");
            lease.TryCommit(request.Epoch);
            Assert(notices == before, "Interaction delivery waits until the request callback finishes its controller state.");
        });
        backend.Leases.Single().Emit(Request(1));
        Assert(demo.Input.Interaction == new TextInteraction(true, true), "Staged interaction is published after reservation completion.");
        lease.Dispose();

        Action<TextInteraction> broken = _ => throw new ApplicationException("interaction listener");
        demo.Input.InteractionChanged += broken;
        peer.FocusAction = () => ((ITextInteractionEvents)peer.Events).InteractionChanged(new(false, false));
        peer.FailFocus = true;
        var errors = Throws<AggregateException>(() => host.TryFocus(demo.Input)).Flatten().InnerExceptions;
        Assert(errors.Any(error => error.Message == "native focus") && errors.Any(error => error.Message == "interaction listener"),
            "Native-operation and authored-notification failures both survive.");
        demo.Input.InteractionChanged -= broken;
        peer.FailFocus = false;
        int delivered = notices;
        peer.FailUpdate = true;
        Throws<ApplicationException>(() => demo.Input.Enabled = false);
        Assert(!host.IsAttached && notices == delivered && demo.Input.Interaction is null, "Failed native updates discard pending metadata when the attachment retires.");
        var replacement = new ViewportBackend();
        host.Attach(replacement);
        Assert(!((ITextInteractionEvents)peer.Events).InteractionChanged(new(true, true)), "Old queued metadata cannot enter a replacement attachment.");
        ((ITextInteractionEvents)replacement.Find("name").Events).InteractionChanged(default);
        Assert(demo.Input.Interaction == default(TextInteraction), "The replacement publishes its own initial state.");
    }

    private sealed class ViewportBackend : IBackend
    {
        public List<ViewportPeer> Peers { get; } = [];
        public List<NativeViewportLease> Leases { get; } = [];
        public List<string> Trace { get; } = [];
        public bool SynchronousBegin { get; set; }
        public bool SupportsSettling { get; set; }
        public bool Disposed { get; private set; }
        public ViewportPeer Find(string id) => Peers.Last(peer => peer.Id == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new ViewportPeer(this, element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() { Disposed = true; Trace.Add("backend"); }
    }

    private sealed class ViewportPeer(ViewportBackend backend, Element element, IControlEvents events) : IMutableElementPeer, IVirtualViewportPeer, IFocusableElementPeer, IVirtualItemPeer
    {
        public Element Element { get; } = element;
        public string Id { get; } = element is Control control ? control.AutomationId : "";
        public IControlEvents Events { get; } = events;
        public int DisposeCalls { get; private set; }
        public bool EchoInteraction { get; set; }
        public bool InOperation { get; private set; }
        public bool FailFocus { get; set; }
        public bool FailUpdate { get; set; }
        public Action? FocusAction { get; set; }
        public bool HasFocus { get; private set; }
        public VirtualItemInfo? Item { get; private set; }
        public void SetVirtualItemInfo(VirtualItemInfo info) => Item = info;
        private readonly List<IElementPeer> children = [];
        public void AddChild(IElementPeer child) => children.Add(child);
        public void Update(ElementProperty property)
        {
            InOperation = true;
            try
            {
                if (EchoInteraction) Assert(((ITextInteractionEvents)Events).InteractionChanged(new(true, false)), "Interaction metadata captured during native update.");
                if (FailUpdate) throw new ApplicationException("native update");
            }
            finally { InOperation = false; }
        }
        public bool TryFocus()
        {
            InOperation = true;
            try
            {
                HasFocus = true;
                ((ITextInteractionEvents)Events).InteractionChanged(new(true, false));
                FocusAction?.Invoke();
                if (FailFocus) throw new ApplicationException("native focus");
                return true;
            }
            finally { InOperation = false; }
        }
        public void InsertChild(int index, IElementPeer child) => children.Insert(index, child);
        public void RemoveChild(IElementPeer child) { backend.Trace.Add("remove"); children.Remove(child); }
        public void ValidateMove(IElementPeer child, int index) { }
        public void MoveChild(IElementPeer child, int index) { children.Remove(child); children.Insert(index, child); }
        public void Dispose() { DisposeCalls++; backend.Trace.Add("peer"); }
        public IVirtualViewportLease BeginVirtualViewport(int itemCount, float rowHeight, long sourceVersion, Action<VirtualViewportRequest> requested)
        {
            if (Element is not ScrollView) throw new InvalidOperationException("A native viewport needs a scroll view.");
            var lease = new NativeViewportLease(backend, requested) { SourceVersion = sourceVersion };
            backend.Leases.Add(lease);
            if (backend.SynchronousBegin) requested(Request(1, sourceVersion));
            return backend.SupportsSettling ? new SettledNativeViewportLease(lease) : lease;
        }
    }

    private sealed class NativeViewportLease(ViewportBackend backend, Action<VirtualViewportRequest> requested) : IVirtualViewportLease
    {
        public int DisposeCalls { get; private set; }
        public long SourceVersion { get; set; }
        public float Offset { get; private set; }
        public long CommittedEpoch { get; private set; }
        public long CommittedSourceVersion { get; private set; }
        public float CommittedOffset { get; private set; }
        public int FlushCalls { get; private set; }
        public long FlushedEpoch { get; private set; }
        public Action? DuringFlush { get; set; }
        public bool InFlush { get; private set; }
        public string SynchronousOperation { get; set; } = "";
        public string FailOperation { get; set; } = "";
        public VirtualViewportCommitResult CommitResult { get; set; } = VirtualViewportCommitResult.Committed;
        public VirtualViewportUpdateResult UpdateResult { get; set; } = VirtualViewportUpdateResult.Ready;
        public void Emit(VirtualViewportRequest request) => requested(request);
        private void Operation(string operation)
        {
            if (operation == SynchronousOperation) requested(Request(2, SourceVersion));
            if (operation == FailOperation) throw new ApplicationException(operation);
        }
        public void SetExtent(int itemCount, long sourceVersion) { Operation("extent"); SourceVersion = sourceVersion; }
        public void RequestOffset(float offset) { Operation("offset"); Offset = offset; }
        public VirtualViewportUpdateResult TryBeginUpdate(long expectedEpoch) { Operation("prepare"); return UpdateResult; }
        public VirtualViewportCommitResult TryCommit(long expectedEpoch)
        {
            Operation("commit");
            if (CommitResult == VirtualViewportCommitResult.Committed)
            {
                CommittedEpoch = expectedEpoch;
                CommittedSourceVersion = SourceVersion;
                CommittedOffset = Offset;
                backend.Trace.Add("commit");
            }
            return CommitResult;
        }
        public void FlushCommitted(long expectedEpoch)
        {
            FlushCalls++;
            FlushedEpoch = expectedEpoch;
            InFlush = true;
            try
            {
                Operation("flush");
                DuringFlush?.Invoke();
                backend.Trace.Add("flush");
            }
            finally { InFlush = false; }
        }
        public void Cancel(long expectedEpoch) => Operation("cancel");
        public void Dispose() { DisposeCalls++; backend.Trace.Add("lease"); Operation("dispose"); }
    }

    private sealed class SettledNativeViewportLease(NativeViewportLease native) : ISettledVirtualViewportLease
    {
        public void SetExtent(int itemCount, long sourceVersion) => native.SetExtent(itemCount, sourceVersion);
        public void RequestOffset(float offset) => native.RequestOffset(offset);
        public VirtualViewportUpdateResult TryBeginUpdate(long expectedEpoch) => native.TryBeginUpdate(expectedEpoch);
        public VirtualViewportCommitResult TryCommit(long expectedEpoch) => native.TryCommit(expectedEpoch);
        public void Cancel(long expectedEpoch) => native.Cancel(expectedEpoch);
        public void FlushCommitted(long expectedEpoch) => native.FlushCommitted(expectedEpoch);
        public void Dispose() => native.Dispose();
    }
}
