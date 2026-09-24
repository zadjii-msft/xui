using System.Diagnostics;
using Xui.Experimental.Portable;
using Stack = Xui.Experimental.Portable.Stack;

internal static class ControllerTests
{
    private static int assertions;

    public static void Run()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        Stack root;
        KeyedStack rows;
        using (var scope = host.BeginBuild())
        {
            root = host.Stack(Axis.Vertical);
            rows = host.KeyedStack(Axis.Vertical);
            root.Add(host.ScrollView(rows, "Virtual list"));
            host.SetContent(root);
            scope.Complete();
        }
        var drafts = new Dictionary<string, string>(StringComparer.Ordinal);
        var resources = new ResourceCounts();
        var controller = host.GetComponentLifetime(root).Own(new VirtualizationController<Row>(
            host, rows, 128, 2, (owner, key) => new Row(owner, key, drafts, resources)));
        string[] keys = Enumerable.Range(0, 10000).Select(index => $"item-{index}").ToArray();
        Check(controller.SetKeys(keys) == VirtualizationUpdate.Applied && controller.Count == 10000, "Ten thousand logical items.");
        Check(controller.Mounted.Count == 0 && rows.Children.Count == 1, "Before geometry, only extent exists.");
        controller.SetViewport(0, 512);
        var backend = new Backend();
        host.Attach(backend);
        var first = controller.Mounted["item-0"];
        var firstPeer = backend.Live[first.Input];
        var canceled = 0;
        using var rowCancellation = first.Lifetime.Token.Register(() => canceled++);
        firstPeer.Events.Change("durable draft");
        Check(drafts["item-0"] == "durable draft", "Draft lives outside recycled component.");
        controller.SetInteraction("item-0", true, false);
        controller.SetViewport(128000, 512);
        Check(ReferenceEquals(first, controller.Mounted["item-0"]), "Offscreen focused editor retains identity.");
        Check(controller.Mounted.Count == 9 && rows.Children.Count == 11, "Sparse pin adds one row and one gap, not intervening items.");
        Check(rows.Children.Sum(child => child.HeightConstraints!.Value.Length!.Value) == 1280000, "Rendered sparse tree retains entire scroll extent.");
        Check(!first.Lifetime.Token.IsCancellationRequested, "Pinned row lifetime stays active.");

        controller.SetInteraction("item-0", true, true);
        int created = backend.Created;
        Check(controller.SetViewport(1280000, 900) == VirtualizationUpdate.Deferred, "Composition defers scrolling and viewport growth.");
        Check(backend.Created == created && controller.Offset == 128000 && controller.ViewportHeight == 512, "No structural change or false committed geometry.");
        Check(controller.SetKeys(keys.Reverse()) == VirtualizationUpdate.Deferred, "Composition defers reorder.");
        controller.SetInteraction("item-0", true, false);
        Check(controller.IsDeferred && ReferenceEquals(first, controller.Mounted["item-0"]), "Focus retains deferred source after composition ends.");
        controller.SetInteraction("item-0", false, false);
        Check(!controller.IsDeferred && controller.Count == 10000, "Blur applies latest pending source.");
        Check(controller.Mounted.ContainsKey("item-0"), "Stable key follows reversal to final viewport.");
        controller.SetViewport(0, 512);
        Check(first.Lifetime.Token.IsCancellationRequested && !firstPeer.Events.Change("stale"), "Retired row cancels work and invalidates events.");
        Check(canceled == 1, "A recycled row's asynchronous lifetime is canceled exactly once.");
        controller.SetViewport(controller.OffsetFor("item-0"), 512);
        Check(controller.Mounted["item-0"].Input.Text == "durable draft", "Remounted row restores external draft.");
        Check(!ReferenceEquals(first, controller.Mounted["item-0"]), "Recycled views are new, logical identity is not.");

        controller.SetInteraction("item-0", true, false);
        var focused = controller.Mounted["item-0"];
        Check(controller.SetKeys(keys.Where(key => key != "item-0")) == VirtualizationUpdate.Deferred, "Focused removal defers.");
        Check(controller.Count == 10000 && !focused.Lifetime.Token.IsCancellationRequested, "Focused removal is not silently committed.");
        controller.SetKeys(keys.Where((_, index) => index % 2 == 1));
        controller.SetInteraction("item-0", false, false);
        Check(controller.Count == 5000 && focused.Lifetime.Token.IsCancellationRequested, "Latest filtered snapshot wins on blur.");
        Reject<ArgumentException>(() => controller.SetKeys(["duplicate", "duplicate"]));
        Check(controller.Count == 5000, "Invalid source preserves committed data.");
        Reject<ArgumentOutOfRangeException>(() => controller.SetViewport(float.NaN, 512));
        Reject<InvalidOperationException>(() => Task.Run(() => controller.SetViewport(0, 512)).GetAwaiter().GetResult());

        controller.SetKeys(keys);
        controller.SetViewport(0, 512);
        controller.SetStructuralChangesBlocked(true);
        Check(controller.SetViewport(64000, 512) == VirtualizationUpdate.Deferred, "External composing editor can defer all structural changes.");
        controller.SetStructuralChangesBlocked(false);
        Check(controller.Offset == 64000 && !controller.IsDeferred, "Unblocking applies last requested geometry.");

        var retained = controller.Mounted.Values.First();
        backend.FailPreflight = true;
        var rejected = Reject<KeyedUpdateException>(() => controller.SetViewport(0, 512));
        Check(!rejected.ModelCommitted && controller.Mounted.Values.Contains(retained) && host.IsAttached, "Native preflight rejection preserves committed controller and attachment.");
        backend.FailPreflight = false;
        controller.Refresh();
        Check(controller.Offset == 0, "Explicit retry commits pending geometry.");

        controller.SetViewport(0, 640, 512);
        var oldWindow = controller.Mounted.Keys.ToArray();
        Check(controller.PrepareViewport(new(1, controller.SourceVersion, controller.SourceVersion,
            new(0, 640, 512, 1280000), new(128000, 400, 512, 1280000), false)) == VirtualizationUpdate.Applied, "Prepare a native viewport epoch.");
        Check(controller.ViewportWidth == 640 && rows.WidthConstraints?.Length == 400, "Requested width measures staged rows without publishing the committed native clip.");
        Check(controller.Offset == 0 && controller.ViewportHeight == 512, "Preparation does not publish uncommitted geometry.");
        Check(oldWindow.All(controller.Mounted.ContainsKey) && controller.Mounted.ContainsKey("item-1000"), "Both committed and requested windows exist before native commit.");
        Check(controller.Mounted.Count <= 16 && controller.PreparedEpoch == 1, "Transient storage is bounded by two windows.");
        Check(controller.PrepareViewport(2, 256000, 512) == VirtualizationUpdate.Applied, "New request supersedes prepared epoch.");
        Check(!controller.Mounted.ContainsKey("item-1000") && controller.Mounted.ContainsKey("item-2000"), "Superseded uncommitted window retires.");
        Check(!controller.CommitPreparedViewport(1) && controller.Offset == 0, "Stale native commit cannot publish geometry.");
        Reject<InvalidOperationException>(() => controller.CancelPreparedViewport(2));
        Check(controller.CommitPreparedViewport(2) && controller.Offset == 256000 && controller.ViewportWidth == 400 && controller.Mounted.Count <= 8, "Commit retires old-only rows and publishes prepared width.");
        Check(controller.PrepareViewport(2, 0, 512) == VirtualizationUpdate.Stale, "Completed epoch cannot be reused.");
        controller.SetStructuralChangesBlocked(true);
        Check(controller.PrepareViewport(3, 384000, 1024) == VirtualizationUpdate.Deferred && controller.PreparedEpoch == 0, "Blocked growth is not presented as prepared.");
        Check(!controller.CommitPreparedViewport(3), "Unprepared native commit is rejected.");
        controller.SetStructuralChangesBlocked(false);
        Check(controller.PreparedEpoch == 3 && controller.Mounted.Count <= 20, "End of block realizes old and resized new windows.");
        Check(controller.CommitPreparedViewport(3) && controller.ViewportHeight == 1024, "Resized viewport commits only after realization.");
        controller.ResetViewportLease();
        Check(controller.PrepareViewport(1, 0, 512) == VirtualizationUpdate.Applied, "New attachment can restart its native epoch.");
        controller.CommitPreparedViewport(1);

        string pinnedKey = controller.Mounted.Keys.ElementAt(2);
        var pinnedRow = controller.Mounted[pinnedKey];
        var pinnedPeer = backend.Live[pinnedRow.Input];
        int moves = backend.Moves;
        controller.SetInteraction(pinnedKey, true, false);
        controller.SetViewport(640000, 512);
        var leadingGap = rows.Children[0];
        var middleGap = rows.Children[2];
        controller.SetViewport(800000, 512);
        Check(ReferenceEquals(leadingGap, rows.Children[0]) && ReferenceEquals(middleGap, rows.Children[2]),
            "Retained gap identities stay on the same side of the focused row.");
        controller.SetViewport(0, 512);
        Check(backend.Moves == moves && pinnedPeer.TextWrites == 0 && ReferenceEquals(pinnedPeer, backend.Live[pinnedRow.Input]),
            "Viewport-only reconciliation preserves sibling order and never moves the focused native root.");
        controller.SetInteraction(pinnedKey, false, false);

        backend.Peak = backend.Live.Count;
        var watch = Stopwatch.StartNew();
        int maxRows = 0;
        for (int i = 0; i < 1000; i++)
        {
            controller.SetViewport((i * 7919L % 10000) * 128, 512);
            maxRows = Math.Max(maxRows, controller.Mounted.Count);
            Check(controller.Mounted.Count <= 8 && resources.Alive == controller.Mounted.Count, "Work remains viewport bounded.");
            Check(rows.Children.Sum(child => child.HeightConstraints!.Value.Length!.Value) == 1280000, "Full extent remains invariant during scrolling.");
        }
        watch.Stop();
        int peak = backend.Peak;
        host.Detach();
        Check(backend.Live.Count == 0, "Detach releases all native peers.");
        for (int cycle = 0; cycle < 100; cycle++)
        {
            var next = new Backend();
            host.Attach(next);
            var input = next.Live.Values.First(peer => peer.Element is TextInput);
            controller.SetViewport(cycle * 12800, 512);
            host.Detach();
            Check(next.Live.Count == 0 && next.Disposed, "Repeated attachment releases peer resources.");
            Check(!input.Events.Change("detached"), "Old attachment events cannot update a retained model.");
        }
        host.Dispose();
        Check(resources.Alive == 0, "Terminal disposal releases every row-owned resource.");
        Reject<ObjectDisposedException>(() => controller.SetKeys(keys));
        Reject<ObjectDisposedException>(() => _ = controller.Count);
        Check(peak <= 24, "Native peers remain bounded independently of 10000 logical rows.");
        Console.WriteLine($"Portable virtualization controller: {assertions} assertions; 1000 scroll reconciliations {watch.Elapsed.TotalMilliseconds:F1}ms; peak mounted rows {maxRows}; peak peers {peak}; 100 attach/detach cycles.");
        SampleRoundtrip();
        SourceTransactions();
        LeasedSample();
        LeaseCycles();
        SourceFailures();
        TerminalReferences();
        SettledCompletion();
        Console.WriteLine($"Total controller and compiled-sample fixture assertions: {assertions}.");
    }

    private static void SettledCompletion()
    {
        using (var host = new Host(new Dispatcher()))
        {
            var sample = PortableDemo.VirtualList.CreateForViewport(host);
            var backend = new Backend { SupportsSettling = false };
            host.Attach(backend);
            var gate = host.BeginVirtualViewport(sample.Viewport, sample.Controller.RequestedCount,
                sample.Controller.RowHeight, sample.Controller.RequestedSourceVersion, sample.OnViewportRequested);
            Check(gate is not ISettledVirtualViewportLease, "A legacy backend does not acquire a fabricated settled capability.");
            Reject<NotSupportedException>(() => sample.AttachViewport(gate, host.SetVirtualItemInfo));
            Check(sample.Controller.Count == 0 && sample.RowsView.Children.Count == 0 && host.IsAttached,
                "Unsupported native completion is rejected before realizing rows or taking ownership of the caller's lease.");
        }
        using (var host = new Host(new Dispatcher()))
        {
            var sample = PortableDemo.VirtualList.CreateForViewport(host);
            var backend = new Backend();
            host.Attach(backend);
            var gate = host.BeginVirtualViewport(sample.Viewport, sample.Controller.RequestedCount,
                sample.Controller.RowHeight, sample.Controller.RequestedSourceVersion, sample.OnViewportRequested);
            Check(gate is ISettledVirtualViewportLease, "The native settled capability is exposed when actually supported.");
            sample.AttachViewport(gate, host.SetVirtualItemInfo);
            int observations = 0;
            sample.ViewportCommitted += _ => observations++;
            var native = backend.LastLease!;
            native.Deliver();
            Check(observations == 1 && native.Flushes == 1, "A settled viewport flush precedes completion observation.");
            native.FailFlush = true;
            gate.RequestOffset(128000);
            Reject<ApplicationException>(native.Deliver);
            Check(!host.IsAttached && backend.Live.Count == 0 && native.Disposed && observations == 1,
                "Native flush failure detaches and never publishes a completion event.");
            Check(sample.Controller.Offset == 128000 && sample.Controller.SourceVersion == 1,
                "A failed postcommit flush does not pretend the already committed model rolled back.");
        }
        Console.WriteLine("Settled native completion: truthful capability, post-prune flush, and fatal-failure boundaries passed.");
    }

    private static void TerminalReferences()
    {
        var (sample, backend, observer, lease) = CreateRetiredSample();
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        Check(!backend.IsAlive && !observer.IsAlive && !lease.IsAlive,
            "A retained disposed sample releases backend leases and native/performance subscriber closures.");
        Check(sample.Data.Items.Count == 10000, "Terminal presentation cleanup preserves externally owned application data.");
        Action<VirtualViewportRequest> handler = static _ => { };
        sample.ViewportCommitted -= handler;
        Reject<ObjectDisposedException>(() => sample.ViewportCommitted += handler);
        Console.WriteLine("Terminal retained sample: backend delegates and performance subscribers are released; cleanup unsubscription remains safe.");
    }

    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    private static (PortableDemo.VirtualList Sample, WeakReference Backend, WeakReference Observer, WeakReference Lease) CreateRetiredSample()
    {
        using var host = new Host(new Dispatcher());
        var sample = PortableDemo.VirtualList.CreateForViewport(host);
        var backend = new Backend();
        var observer = new PerformanceObserver(host);
        sample.ViewportCommitted += observer.Committed;
        var lifetime = host.GetComponentLifetime(sample.Root);
        lifetime.Own(new ViewportUnsubscriber(sample));
        host.Attach(backend);
        var lease = host.BeginVirtualViewport(sample.Viewport, sample.Controller.RequestedCount,
            sample.Controller.RowHeight, sample.Controller.RequestedSourceVersion, sample.OnViewportRequested);
        sample.AttachViewport(lease, observer.SetItemInfo);
        backend.LastLease!.Deliver();
        var references = (sample, new WeakReference(backend), new WeakReference(observer), new WeakReference(lease));
        host.Dispose();
        return references;
    }

    private sealed class PerformanceObserver(Host host)
    {
        public void Committed(VirtualViewportRequest request) => host.VerifyAccess();
        public void SetItemInfo(Element row, VirtualItemInfo info) => host.SetVirtualItemInfo(row, info);
    }

    private sealed class ViewportUnsubscriber : IDisposable
    {
        private readonly PortableDemo.VirtualList sample;
        private readonly Action<VirtualViewportRequest> handler = static _ => { };
        public ViewportUnsubscriber(PortableDemo.VirtualList sample)
        {
            this.sample = sample;
            sample.ViewportCommitted += handler;
        }
        public void Dispose() => sample.ViewportCommitted -= handler;
    }

    private static void LeaseCycles()
    {
        using var host = new Host(new Dispatcher());
        var state = new PortableDemo.VirtualListState();
        state.Items["task-00000"].Selection = new(1, 3);
        var sample = PortableDemo.VirtualList.CreateForViewport(host, state);
        var lifetime = host.GetComponentLifetime(sample.Root);
        TextSelection expected = new(1, 3);
        float expectedOffset = 0;
        for (int cycle = 0; cycle < 100; cycle++)
        {
            sample.PrepareForViewportAttachment();
            Check(sample.RowsView.Children.Count == 0 && sample.Controller.Mounted.Count == 0 &&
                sample.Controller.Count == 0 && sample.Controller.RequestedCount == 10000 &&
                sample.Controller.RequestedSourceVersion == 1,
                "Every attachment starts with no realized rows or gaps while preserving the declared source.");
            var backend = new Backend();
            host.Attach(backend);
            var gate = host.BeginVirtualViewport(sample.Viewport, sample.Controller.RequestedCount,
                sample.Controller.RowHeight, sample.Controller.RequestedSourceVersion, sample.OnViewportRequested);
            var native = backend.LastLease!;
            sample.AttachViewport(gate, host.SetVirtualItemInfo);
            native.Deliver();
            Check(sample.Controller.Offset == expectedOffset, "The new native lease restores the previously committed scroll intent.");
            if (cycle == 0) Reject<InvalidOperationException>(sample.PrepareForViewportAttachment);
            if (!sample.Controller.Mounted.ContainsKey("task-00000"))
            {
                sample.Reveal("task-00000");
                native.Deliver();
            }
            foreach (var row in sample.Controller.Mounted.Values)
            {
                var peer = backend.Live[row.Input];
                ((ITextInteractionEvents)peer.Events).InteractionChanged(new(peer.HasFocus, false));
            }
            var input = sample.Controller.Mounted["task-00000"].Input;
            var events = backend.Live[input].Events;
            Check(host.GetSelection(input) == expected, "A fresh native attachment restores selection without overwriting the external range.");
            host.TryFocus(input);
            ((ITextInteractionEvents)events).InteractionChanged(new(true, false));
            events.Change($"draft {cycle}");
            expected = new(2, 4);
            host.SetSelection(input, expected);
            expectedOffset = 12800 + cycle * 1280;
            gate.RequestOffset(expectedOffset);
            native.Deliver();
            Check(ReferenceEquals(input, sample.Controller.Mounted["task-00000"].Input),
                "A focused native editor survives leased scrolling before attachment retirement.");
            sample.CaptureEditingState();
            host.Detach();
            gate.Dispose();
            Check(native.Disposed && backend.Live.Count == 0 && !lifetime.Token.IsCancellationRequested,
                "Lease cleanup releases native resources without ending the retained application lifetime.");
            Check(input.Interaction is null && !events.Change("late") &&
                !((ITextInteractionEvents)events).InteractionChanged(new(true, true)),
                "Old input and interaction sinks stay inactive after lease retirement.");
            Check(backend.Peak < 80 && sample.Controller.Mounted.Count <= 9,
                "Repeated leased attachments remain viewport-and-pin bounded.");
        }
        host.Dispose();
        Check(lifetime.Token.IsCancellationRequested, "Terminal host disposal ends the retained application lifetime.");
        Console.WriteLine("Shared leased sample: 100 attach/detach cycles retained drafts and selection, bounded peers, reset interaction, and rejected stale callbacks.");
    }

    private static void SourceFailures()
    {
        using var host = new Host(new Dispatcher());
        Stack root;
        KeyedStack rows;
        using (var scope = host.BeginBuild())
        {
            root = host.Stack(Axis.Vertical);
            rows = host.KeyedStack(Axis.Vertical);
            root.Add(rows);
            host.SetContent(root);
            scope.Complete();
        }
        var resources = new ResourceCounts();
        var drafts = new Dictionary<string, string>();
        bool failCreate = true;
        bool failUpdate = false;
        var controller = host.GetComponentLifetime(root).Own(new VirtualizationController<Row>(host, rows, 128, 2,
            (owner, key) => failCreate ? throw new ApplicationException("Factory rejected.") : new Row(owner, key, drafts, resources),
            (_, _) => { if (failUpdate) throw new ApplicationException("Row update failed."); }));
        long version = controller.QueueKeys(["one", "two", "three"]);
        var request = new VirtualViewportRequest(1, 0, version, default, new(0, 640, 512, 384), false);
        var backend = new Backend();
        host.Attach(backend);
        var first = Reject<KeyedUpdateException>(() => controller.PrepareViewport(request));
        Check(!first.ModelCommitted && controller.Count == 0 && rows.Children.Count == 0 && resources.Alive == 0,
            "Failed source factory leaves the committed source and row ownership intact.");
        Check(controller.CancelPreparedViewport(1), "A precommit factory failure can cancel its preparation.");
        failCreate = false;
        failUpdate = true;
        var second = Reject<KeyedUpdateException>(() => controller.PrepareViewport(request with { Epoch = 2 }));
        Check(second.ModelCommitted && !host.IsAttached && backend.Live.Count == 0,
            "A post-model-commit update failure detaches native resources.");
        Check(controller.Count == 0 && controller.RequestedCount == 3 && controller.Mounted.Count == 3,
            "Post-model failure remains an explicitly staged source, not a false logical commit.");
        failUpdate = false;
        controller.ResetViewportLease();
        Check(controller.Count == 0 && rows.Children.Count == 0 && resources.Alive == 0,
            "Detached reset retires staged rows while preserving pending source intent.");
        var replacement = new Backend();
        host.Attach(replacement);
        Check(controller.PrepareViewport(request) == VirtualizationUpdate.Applied, "Fresh attachment can realize the preserved pending source.");
        Reject<InvalidOperationException>(controller.ResetViewportLease);
        Check(controller.CommitPreparedViewport(1) && controller.Count == 3,
            "Reset rejection did not corrupt the reserved source transaction.");
        host.Dispose();
        Check(resources.Alive == 0 && replacement.Live.Count == 0, "Source failure recovery releases every row and peer.");
        Console.WriteLine("Source failure boundaries: precommit cancellation, postcommit detach, and explicit detached recovery passed.");
    }

    private static void LeasedSample()
    {
        using var host = new Host(new Dispatcher());
        var sample = PortableDemo.VirtualList.CreateForViewport(host);
        Check(sample.Controller.Count == 0 && sample.RowsView.Children.Count == 0, "Native sample attaches without an oversized ordinary scroll child.");
        var backend = new Backend();
        host.Attach(backend);
        var gate = host.BeginVirtualViewport(sample.Viewport, sample.Controller.RequestedCount, sample.Controller.RowHeight,
            sample.Controller.RequestedSourceVersion, sample.OnViewportRequested);
        var lease = backend.LastLease!;
        sample.AttachViewport(gate, host.SetVirtualItemInfo);
        sample.ViewportCommitted += request =>
        {
            Check(sample.Controller.Offset == request.Requested.Offset && sample.Controller.SourceVersion == request.RequestedSourceVersion,
                "Performance completion is observed only after native and logical source publication.");
            Check(sample.Controller.Mounted.Count <= Math.Ceiling(request.Requested.Height / 128) + 5 + 1,
                "Performance completion is observed after old-only rows are pruned.");
            Check(!backend.GeometryDirty && lease.FlushedEpoch == request.Epoch,
                "Performance completion is observed only after pending native prune geometry is synchronously settled.");
        };
        lease.Deliver();
        Check(!backend.GeometryDirty, "No status or navigation layout work is left after settled completion returns.");
        Check(sample.Controller.Count == 10000 && sample.Controller.ViewportWidth == 640, "Native sample accepts initial lease geometry and source.");
        var firstInput = sample.Controller.Mounted["task-00000"].Input;
        host.TryFocus(firstInput);
        Check(((ITextInteractionEvents)backend.Live[firstInput].Events).InteractionChanged(new(true, false)),
            "Actual row input metadata reaches its authored pin callback.");
        Check(firstInput.Interaction == new TextInteraction(true, false), "The current attachment exposes actual input interaction state.");
        Check(backend.Live[firstInput].Events.Submit(), "Native submit reaches the shared logical navigation handler.");
        lease.Deliver();
        Check(host.HasFocus(sample.Controller.Mounted["task-00001"].Input), "Enter advances to the next logical task through native focus.");
        var composingInput = sample.Controller.Mounted["task-00001"].Input;
        ((ITextInteractionEvents)backend.Live[composingInput].Events).InteractionChanged(new(true, true));
        int beforeSubmit = lease.Requests;
        backend.Live[composingInput].Events.Submit();
        Check(lease.Requests == beforeSubmit && host.HasFocus(composingInput), "A composing editor is never blurred by Enter navigation.");
        ((ITextInteractionEvents)backend.Live[composingInput].Events).InteractionChanged(new(true, false));
        Peer Find(string id) => backend.Live.Values.Single(peer => peer.Element is Control control && control.AutomationId == id);
        void Click(string id) { var peer = Find(id); peer.TryFocus(); peer.Events.Click(); }
        Find("virtual-reverse").Events.Click();
        Check(sample.Controller.SourceVersion == 1 && sample.Controller.RequestedSourceVersion == 2,
            "Authored reverse queues a same-count candidate before native publication.");
        lease.Deliver();
        Check(host.IsAttached && host.HasFocus(composingInput) && sample.Controller.SourceVersion == 1,
            "A pointer command that does not take native focus defers safely without tainting or canceling a mutated batch.");
        Click("virtual-last");
        lease.Deliver();
        Check(sample.Controller.SourceVersion == 2 && sample.Controller.Mounted.ContainsKey("task-00000"),
            "Navigation seeks stable keys in the pending reordered source.");
        Click("virtual-filter");
        lease.Deliver();
        Check(sample.Controller.Count == 5000 && lease.Committed.Extent == 640000, "Native sample commits source count and extent together.");
        float previousOffset = sample.Controller.Offset;
        lease.Blocked = true;
        lease.RequestOffset(32000);
        lease.Deliver();
        Check(sample.Controller.Offset == previousOffset && sample.Status.Contains("deferred"), "Blocked native intent is explicit and never published early.");
        lease.Blocked = false;
        lease.RequestOffset(32000);
        lease.Deliver();
        Check(sample.Controller.Offset == 32000 && lease.Committed.Offset == 32000, "Unblocked native and managed geometry agree.");
        Click("virtual-remove");
        lease.FailCommit = true;
        Reject<InvalidOperationException>(lease.Deliver);
        Check(!host.IsAttached && backend.Live.Count == 0 && lease.Disposed,
            "Broken reserved commit detaches instead of pretending source rollback.");
        Check(sample.Controller.Count == 5000 && sample.Controller.RequestedCount == 4999,
            "Failed native commit leaves the logical source explicitly pending.");
        int deliveries = lease.Deliveries;
        lease.Deliver();
        Check(lease.Deliveries == deliveries, "Closed native lease cannot deliver obsolete callbacks.");
        Console.WriteLine("Compiled leased sample: initial empty tree, candidate source/navigation, held intent, and fatal-commit detach passed (recording backend only).");
    }

    private static void SourceTransactions()
    {
        using var host = new Host(new Dispatcher());
        Stack root;
        KeyedStack rows;
        using (var scope = host.BeginBuild())
        {
            root = host.Stack(Axis.Vertical);
            rows = host.KeyedStack(Axis.Vertical);
            root.Add(host.ScrollView(rows, "Source transaction fixture"));
            host.SetContent(root);
            scope.Complete();
        }
        var resources = new ResourceCounts();
        var drafts = new Dictionary<string, string>();
        var controller = host.GetComponentLifetime(root).Own(
            new VirtualizationController<Row>(host, rows, 128, 2, (owner, key) => new Row(owner, key, drafts, resources)));
        string[] keys = Enumerable.Range(0, 10000).Select(index => $"key-{index}").ToArray();
        long initial = controller.QueueKeys(keys);
        Check(controller.Count == 0 && controller.RequestedCount == 10000 && rows.Children.Count == 0,
            "Queuing initial data does not create a huge ordinary native scroll surface.");
        var backend = new Backend();
        host.Attach(backend);
        Check(controller.PrepareViewport(new(1, 0, initial, default, new(0, 640, 512, 1280000), false)) == VirtualizationUpdate.Applied,
            "Candidate initial source prepares inside a reserved native batch.");
        Check(controller.Count == 0 && controller.SourceVersion == 0 && controller.Mounted.Count == 6,
            "Staged views do not masquerade as a committed logical source.");
        Reject<InvalidOperationException>(() => controller.CancelPreparedViewport(1));
        Check(controller.CommitPreparedViewport(1) && controller.Count == 10000 && controller.SourceVersion == initial,
            "Native commit publishes the source version and count.");

        controller.SetInteraction("key-0", true, false);
        long reversed = controller.QueueKeys(keys.Reverse());
        var before = new VirtualViewportRect(0, 640, 512, 1280000);
        Check(controller.PrepareViewport(new(2, initial, reversed, before, before, false)) == VirtualizationUpdate.Deferred,
            "Focused source mutation defers before destructive staging.");
        Check(controller.CancelPreparedViewport(2) && controller.SourceVersion == initial, "Unstaged source cancellation is safe.");
        controller.SetInteraction("key-0", false, false);
        Check(controller.SourceVersion == initial && controller.Mounted.ContainsKey("key-0"),
            "Blur does not publish queued source outside its native batch.");
        Check(controller.PrepareViewport(new(3, initial, reversed, before, before, false)) == VirtualizationUpdate.Applied,
            "Equal-count reorder stages a distinct source version.");
        Check(controller.SourceVersion == initial && controller.Mounted.ContainsKey("key-9999"),
            "Candidate reordered presentation is explicitly staged, not logically committed.");
        long filtered = controller.QueueKeys(keys.Where((_, index) => index % 2 == 0));
        Reject<InvalidOperationException>(() => controller.PrepareViewport(
            new(4, reversed, filtered, before, new(0, 640, 512, 640000), false)));
        Check(controller.CommitPreparedViewport(3) && controller.SourceVersion == reversed && controller.RequestedSourceVersion == filtered,
            "Committing reserved source retains newer queued intent.");
        Check(controller.Count == 10000 && controller.RequestedCount == 5000, "Committed and pending counts remain distinct.");
        Check(controller.PrepareViewport(new(4, reversed, filtered, before, new(0, 640, 512, 640000), false)) == VirtualizationUpdate.Applied,
            "Filtered extent and source are prepared together.");
        Check(controller.Count == 10000 && controller.Extent == 1280000, "Filtering has not published before native commit.");
        Check(controller.CommitPreparedViewport(4) && controller.Count == 5000 && controller.Extent == 640000,
            "Source extent publishes only with its native epoch.");
        Reject<InvalidOperationException>(() => controller.PrepareViewport(
            new(5, filtered, filtered, before, new(640000, 640, 512, 640000), false)));
        Check(controller.Offset == 0 && controller.SourceVersion == filtered, "Invalid native geometry is rejected before staging or logical publication.");
        Check(controller.PrepareViewport(new(5, initial, reversed, before, before, false)) == VirtualizationUpdate.Stale,
            "An obsolete source request cannot replace the latest projection.");
        Reject<ArgumentException>(() => controller.QueueKeys(["duplicate", "duplicate"]));
        Check(controller.RequestedSourceVersion == filtered, "Invalid keys never advance the requested source version.");
        Reject<ArgumentException>(() => controller.QueueKeys([new string('x', 4097)]));
        Reject<ArgumentException>(() => controller.QueueKeys(["\ud800"]));
        Check(controller.RequestedSourceVersion == filtered, "Key limits are checked before factories or source version advancement.");
        long distinct = controller.QueueKeys(["\u00e9", "e\u0301"]);
        Check(controller.RequestedCount == 2, "Valid Unicode keys are not normalized into a duplicate.");
        Reject<ArgumentException>(() => controller.QueueKeys(["\u00e9", "\u00e9"]));
        Check(controller.RequestedSourceVersion == distinct, "Exact ordinal duplicates are still rejected.");
        host.Detach();
        controller.PrepareForViewportAttachment();
        Check(controller.Count == 0 && controller.SourceVersion == 0 && controller.RequestedCount == 2 &&
            controller.RequestedSourceVersion == distinct && rows.Children.Count == 0,
            "Detached preparation preserves the latest uncommitted projection and version without materializing it.");
        host.Dispose();
        Check(resources.Alive == 0 && backend.Live.Count == 0, "Source transactions retain no peers or row resources after disposal.");
        Console.WriteLine("Candidate source versions: queue, equal-count reorder, focused deferral, extent commit, and newer pending intent passed (recording backend only).");
    }

    private static void SampleRoundtrip()
    {
        using var host = new Host(new Dispatcher());
        var sample = PortableDemo.VirtualList.Create(host);
        sample.AcceptViewport(0, 512);
        var backend = new Backend();
        host.Attach(backend);
        long epoch = 0;
        sample.ConnectScrollRequest(offset =>
        {
            long request = ++epoch;
            Check(sample.PrepareViewport(request, offset, 512) == VirtualizationUpdate.Applied, "Sample prepares requested range.");
            Check(sample.CommitViewport(request), "Sample consumes explicit native commit.");
        });
        Peer Find(string id) => backend.Live.Values.Single(peer => peer.Element is Control control && control.AutomationId == id);
        var input = Find("virtual-task-00000");
        input.Events.Change("Edited first item");
        host.SetSelection((TextInput)input.Element, new(2, 8));
        sample.AcceptInteraction("task-00000", false, false);
        Find("virtual-last").Events.Click();
        Check(sample.Controller.Mounted.ContainsKey("task-09999") && sample.Controller.Count == 10000, "Generated button navigates to logical last row.");
        Check(!input.Events.Change("stale") && sample.Data.Items["task-00000"].Draft == "Edited first item", "Actual sample row recycling rejects stale edits.");
        Find("virtual-first").Events.Click();
        Check(sample.Controller.Mounted["task-00000"].Input.Text == "Edited first item", "Generated app restores external draft.");
        ((ITextInteractionEvents)Find("virtual-task-00000").Events).InteractionChanged(new(false, false));
        Check(sample.Data.Items["task-00000"].Selection == new TextSelection(2, 8),
            "A recycled editor's initial unfocused snapshot does not erase its externally retained selection.");
        Check(sample.FocusSelected() && host.GetSelection(sample.Controller.Mounted["task-00000"].Input) == new TextSelection(2, 8), "Explicit focus restores saved native selection.");
        host.SetSelection(sample.Controller.Mounted["task-00000"].Input, new(3, 7));
        sample.CaptureEditingState();
        host.Detach();
        Check(backend.Live.Count == 0, "Sample attachment closure releases native editors.");
        backend = new Backend();
        host.Attach(backend);
        Check(sample.FocusSelected() && host.GetSelection(sample.Controller.Mounted["task-00000"].Input) == new TextSelection(3, 7),
            "Explicit pre-detach capture retains the latest caret range outside native peers.");
        Find("virtual-reverse").Events.Click();
        Find("virtual-filter").Events.Click();
        Check(sample.Controller.Count == 5000 && sample.Status.Contains("5,000"), "Generated filter/status reflect actual keyed projection.");
        Find("virtual-remove").Events.Click();
        Check(sample.Controller.Count == 4999 && sample.Data.Items["task-00000"].Removed, "Generated removal changes keyed source.");
        Check(backend.Live.Count < 60, "Compiled shared application mounts a bounded control tree.");
        host.Dispose();
        Check(backend.Live.Count == 0, "Generated app ownership releases all peers.");
        Console.WriteLine("Generated VirtualList .xui + compiled C# sample: input, navigation, filtering, removal, selection and disposal passed against the recording backend.");
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    private static T Reject<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private sealed class Dispatcher : IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action) => throw new NotSupportedException("This fixture does not queue dispatch.");
    }

    private sealed class ResourceCounts
    {
        public int Alive;
        public IDisposable Acquire() { Alive++; return new Resource(this); }
        private sealed class Resource(ResourceCounts owner) : IDisposable
        {
            private bool disposed;
            public void Dispose() { if (!disposed) { disposed = true; owner.Alive--; } }
        }
    }

    private sealed class Row : IPortableComponent
    {
        public Element Root { get; }
        public TextInput Input { get; }
        public ComponentLifetime Lifetime { get; }
        public Row(Host host, string key, Dictionary<string, string> drafts, ResourceCounts resources)
        {
            using var scope = host.BeginBuild();
            var root = host.Stack(Axis.Vertical);
            Input = host.TextInput(key);
            Input.SetCaptionVisible(false);
            Input.Text = drafts.GetValueOrDefault(key, key);
            Input.Changed += value => drafts[key] = value;
            root.Add(Input);
            host.SetContent(root);
            scope.Complete();
            Root = root;
            Lifetime = host.GetComponentLifetime(Root);
            Lifetime.Own(resources.Acquire());
        }
    }

    private sealed class Backend : IBackend
    {
        public Dictionary<Element, Peer> Live { get; } = [];
        public int Created;
        public int Peak;
        public int Moves;
        public bool Disposed;
        public bool FailPreflight;
        public Lease? LastLease;
        public bool SupportsSettling = true;
        public bool GeometryDirty;
        public void MarkLayoutDirty(Element element)
        {
            GeometryDirty = true;
        }
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new Peer(this, element, events);
            Live.Add(element, peer);
            Created++;
            Peak = Math.Max(Peak, Live.Count);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() => Disposed = true;
    }

    private sealed class Lease(Backend backend, int count, float pitch, long version, Action<VirtualViewportRequest> requested) : ISettledVirtualViewportLease
    {
        private int requestedCount = count;
        private long requestedVersion = version;
        private long epoch = 1;
        private long committedVersion;
        private long committedEpoch;
        private float requestedOffset;
        private VirtualViewportRequest? active;
        public VirtualViewportRect Committed { get; private set; }
        public bool Blocked;
        public bool FailCommit;
        public bool FailFlush;
        public bool Disposed;
        public int Deliveries;
        public int Requests;
        public int Flushes;
        public long FlushedEpoch;
        private VirtualViewportRequest Snapshot => new(epoch, committedVersion, requestedVersion, Committed,
            new(VirtualizationMath.ClampOffset(requestedCount, pitch, requestedOffset, 512), 640, 512,
                VirtualizationMath.Extent(requestedCount, pitch)), Blocked);
        public void Deliver()
        {
            if (Disposed) return;
            Deliveries++;
            requested(Snapshot);
        }
        public void SetExtent(int itemCount, long sourceVersion)
        {
            ObjectDisposedException.ThrowIf(Disposed, this);
            VirtualizationMath.Extent(itemCount, pitch);
            if (sourceVersion <= requestedVersion) throw new ArgumentOutOfRangeException(nameof(sourceVersion));
            requestedCount = itemCount;
            requestedVersion = sourceVersion;
            epoch++;
        }
        public void RequestOffset(float offset)
        {
            ObjectDisposedException.ThrowIf(Disposed, this);
            VirtualizationMath.ClampOffset(requestedCount, pitch, offset, 512);
            requestedOffset = offset;
            Requests++;
            epoch++;
        }
        public VirtualViewportUpdateResult TryBeginUpdate(long expectedEpoch)
        {
            ObjectDisposedException.ThrowIf(Disposed, this);
            if (expectedEpoch != epoch) return VirtualViewportUpdateResult.Superseded;
            if (Blocked) return VirtualViewportUpdateResult.Blocked;
            if (active.HasValue) throw new InvalidOperationException("A batch is already active.");
            active = Snapshot;
            return VirtualViewportUpdateResult.Ready;
        }
        public VirtualViewportCommitResult TryCommit(long expectedEpoch)
        {
            ObjectDisposedException.ThrowIf(Disposed, this);
            if (active is not { } next || next.Epoch != expectedEpoch) return VirtualViewportCommitResult.Superseded;
            if (FailCommit) return VirtualViewportCommitResult.Blocked;
            Committed = next.Requested;
            committedVersion = next.RequestedSourceVersion;
            committedEpoch = expectedEpoch;
            backend.GeometryDirty = false;
            active = null;
            return VirtualViewportCommitResult.Committed;
        }
        public void FlushCommitted(long expectedEpoch)
        {
            ObjectDisposedException.ThrowIf(Disposed, this);
            if (active.HasValue || expectedEpoch <= 0 || expectedEpoch != committedEpoch)
                throw new InvalidOperationException("Flush requires the current committed epoch outside a reserved update.");
            if (FailFlush) throw new ApplicationException("Native layout could not settle.");
            backend.GeometryDirty = false;
            FlushedEpoch = expectedEpoch;
            Flushes++;
        }
        public void Cancel(long expectedEpoch)
        {
            ObjectDisposedException.ThrowIf(Disposed, this);
            if (active?.Epoch == expectedEpoch) active = null;
        }
        public void Dispose() { Disposed = true; active = null; }
    }

    private sealed class LegacyLease(IVirtualViewportLease native) : IVirtualViewportLease
    {
        public void SetExtent(int itemCount, long sourceVersion) => native.SetExtent(itemCount, sourceVersion);
        public void RequestOffset(float offset) => native.RequestOffset(offset);
        public VirtualViewportUpdateResult TryBeginUpdate(long expectedEpoch) => native.TryBeginUpdate(expectedEpoch);
        public VirtualViewportCommitResult TryCommit(long expectedEpoch) => native.TryCommit(expectedEpoch);
        public void Cancel(long expectedEpoch) => native.Cancel(expectedEpoch);
        public void Dispose() => native.Dispose();
    }

    private sealed class Peer(Backend backend, Element element, IControlEvents events) :
        IMutationPreflightPeer, IConstrainedElementPeer, ITextSelectionPeer, IVirtualViewportPeer, IVirtualItemPeer
    {
        public Element Element => element;
        public IControlEvents Events => events;
        public int TextWrites { get; private set; }
        public VirtualItemInfo? ItemInfo { get; private set; }
        private readonly List<IElementPeer> children = [];
        public bool HasFocus { get; private set; }
        private TextSelection selection;
        public TextSelection Selection
        {
            get => selection;
            set => selection = value.ClampTo(((TextInput)element).Text);
        }
        public bool TryFocus()
        {
            if (element is not (TextInput or Button)) return false;
            foreach (var peer in backend.Live.Values) peer.HasFocus = false;
            HasFocus = true;
            return true;
        }
        public IVirtualViewportLease BeginVirtualViewport(int itemCount, float rowHeight, long sourceVersion,
            Action<VirtualViewportRequest> requested)
        {
            if (element is not ScrollView) throw new InvalidOperationException("A viewport requires a scroll peer.");
            var native = backend.LastLease = new Lease(backend, itemCount, rowHeight, sourceVersion, requested);
            return backend.SupportsSettling ? native : new LegacyLease(native);
        }
        public void SetVirtualItemInfo(VirtualItemInfo info) { info.Validate(); ItemInfo = info; }
        public void AddChild(IElementPeer child) { children.Add(child); backend.MarkLayoutDirty(element); }
        public void InsertChild(int index, IElementPeer child) { children.Insert(index, child); backend.MarkLayoutDirty(element); }
        public void RemoveChild(IElementPeer child) { children.Remove(child); backend.MarkLayoutDirty(element); }
        public void MoveChild(IElementPeer child, int index) { backend.Moves++; children.Remove(child); children.Insert(index, child); backend.MarkLayoutDirty(element); }
        public void ValidateMove(IElementPeer child, int index) => ValidateMutation();
        public void ValidateMutation() { if (backend.FailPreflight) throw new InvalidOperationException("Native composition preflight."); }
        public void Update(ElementProperty property)
        {
            if (element is TextInput && property == ElementProperty.Text) TextWrites++;
            backend.MarkLayoutDirty(element);
        }
        public void Dispose() { Check(backend.Live.Remove(element), "Peer disposed exactly once."); }
    }
}
