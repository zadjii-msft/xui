using Xui.Experimental.Portable;

internal static class Program
{
    private static int assertions;

    private static void Main()
    {
        BreakpointChecks();
        ObservationChecks();
        RetainedGridChecks();
        FailureChecks();
        Console.WriteLine($"Responsive width: {assertions} assertions passed.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private static void BreakpointChecks()
    {
        var points = WidthBreakpoints.Default;
        foreach (var (width, mode) in new[]
        {
            (0f, WidthMode.Compact), (320f, WidthMode.Compact),
            (float.BitDecrement(720), WidthMode.Compact), (720f, WidthMode.Medium),
            (float.BitIncrement(720), WidthMode.Medium), (float.BitDecrement(1120), WidthMode.Medium),
            (1120f, WidthMode.Expanded), (float.BitIncrement(1120), WidthMode.Expanded),
            (float.MaxValue, WidthMode.Expanded)
        })
            Assert(points.Select(width) == mode, $"Width {width} selects its exact inclusive breakpoint.");
        Assert(new WidthBreakpoints() == points && new WidthBreakpoints(600, 1000).Select(600) == WidthMode.Medium,
            "Immutable breakpoint policies support explicit application thresholds.");
        foreach (float invalid in new[] { -1f, float.NaN, float.PositiveInfinity, float.NegativeInfinity })
        {
            Throws<ArgumentOutOfRangeException>(() => points.Select(invalid));
            Throws<ArgumentOutOfRangeException>(() => new WidthBreakpoints(invalid, 1120));
            Throws<ArgumentOutOfRangeException>(() => new WidthBreakpoints(720, invalid));
        }
        Throws<ArgumentOutOfRangeException>(() => new WidthBreakpoints(0, 1120));
        Throws<ArgumentOutOfRangeException>(() => new WidthBreakpoints(720, 720));
        Throws<ArgumentOutOfRangeException>(() => new WidthBreakpoints(720, 719));
    }

    private static void ObservationChecks()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var editor = Build(host);
        var backend = new Backend { Initial = new(640, 480) };
        host.Attach(backend);
        var peers = backend.Peers.ToArray();
        var sizes = new List<Size>();
        using var observation = host.ObserveViewport(size =>
        {
            sizes.Add(size);
            editor.Help = WidthBreakpoints.Default.Select(size.Width).ToString();
        });
        Assert(sizes.Count == 0 && dispatcher.Pending == 1,
            "Synchronous native initial metadata is captured without invoking authored code inline.");
        for (int i = 0; i < 100; i++) backend.Emit(new(640 + i, 500 + i));
        Assert(dispatcher.Pending == 1 && sizes.Count == 0,
            "A native resize burst owns only one pending dispatcher callback.");
        dispatcher.DrainOne();
        Assert(sizes.SequenceEqual([new Size(739, 599)]) && editor.Help == "Medium",
            "The latest allocation is delivered outside the native guard and can update retained controls.");
        Assert(dispatcher.Pending == 0 && peers.SequenceEqual(backend.Peers) &&
            editor.Text == "Retained draft" && backend.Peers.All(peer => !peer.Updates.Contains(ElementProperty.Text)),
            "Viewport delivery creates no idle work, no peers, and no editor text writes.");
        backend.Emit(new(739, 599));
        Assert(dispatcher.Pending == 0, "Equal native size notifications do not schedule work.");
        backend.Emit(new(1120, 599));
        backend.Emit(new(739, 599));
        dispatcher.DrainOne();
        Assert(sizes.Count == 1 && dispatcher.Pending == 0,
            "A burst that returns to the last delivered allocation does not emit a false mode transition.");
        backend.Emit(new(739, 600));
        dispatcher.DrainOne();
        Assert(sizes[^1] == new Size(739, 600), "Height-only changes remain observable allocation changes.");
        var isolatedSizes = new List<Size>();
        using (var isolated = host.ObserveViewport(isolatedSizes.Add))
        {
            Assert(dispatcher.Pending == 1, "A new subscriber owns its own initial delivery.");
        }
        dispatcher.DrainOne();
        Assert(isolatedSizes.Count == 0 && backend.SubscriptionDisposals == 1 && host.IsAttached,
            "Explicit disposal suppresses its queued initial callback without detaching other subscribers.");
        backend.Emit(new(1120, 600));
        var stale = backend.Callback!;
        host.Detach();
        Assert(backend.Log.IndexOf("unsubscribe") < backend.Log.IndexOf("backend-dispose"),
            "Native viewport listeners retire before backend unmount.");
        dispatcher.DrainOne();
        stale(new(1000, 1000));
        Assert(sizes.Count == 2 && dispatcher.Pending == 0,
            "Queued and late old-attachment callbacks cannot reach authored code.");
        observation.Dispose();
        var nextBackend = new Backend { Initial = new(1120, 480) };
        host.Attach(nextBackend);
        var modes = new List<WidthMode>();
        using var next = host.ObserveViewport(size => modes.Add(WidthBreakpoints.Default.Select(size.Width)));
        dispatcher.DrainOne();
        Assert(modes.SequenceEqual([WidthMode.Expanded]) && nextBackend.Peers.Count == peers.Length,
            "A fresh attachment explicitly acquires its own current allocation without reviving old subscriptions.");
        using var reentrant = host.ObserveViewport(size =>
        {
            if (size.Width == 1120) nextBackend.Emit(new(600, 480));
        });
        dispatcher.DrainOne();
        Assert(dispatcher.Pending == 2, "Authored resize feedback is queued, not recursively delivered inline.");
        dispatcher.DrainOne();
        dispatcher.DrainOne();
        Assert(modes[^1] == WidthMode.Compact && dispatcher.Pending == 0,
            "Reentrant metadata converges on a later queued delivery without persistent idle callbacks.");
        Task.Run(() => Throws<InvalidOperationException>(() => host.ObserveViewport(_ => { }))).GetAwaiter().GetResult();
        Task.Run(() => Throws<InvalidOperationException>(() => next.Dispose())).GetAwaiter().GetResult();
        host.Dispose();
        Assert(nextBackend.SubscriptionDisposals == 2 && nextBackend.Disposed,
            "Host disposal retires every independently owned allocation subscription.");
        next.Dispose();
        reentrant.Dispose();
    }

    private static void FailureChecks()
    {
        var dispatcher = new Dispatcher();
        using (var host = new Host(new PlainDispatcher(dispatcher)))
        {
            Build(host);
            var backend = new Backend { Initial = new(0, 0) };
            host.Attach(backend);
            Size? observed = null;
            using var observation = host.ObserveViewport(size => observed = size);
            dispatcher.DrainOne();
            Assert(observed == new Size(0, 0),
                "A not-yet-laid-out zero allocation is valid on an ordinary asynchronous dispatcher.");
            Throws<ArgumentNullException>(() => host.ObserveViewport(null!));
        }
        using (var detached = new Host(dispatcher))
        {
            Throws<InvalidOperationException>(() => detached.ObserveViewport(_ => { }));
            Build(detached);
            detached.Attach(new LegacyBackend());
            Throws<NotSupportedException>(() => detached.ObserveViewport(_ => { }));
            Assert(detached.IsAttached, "An old backend explicitly rejects observation without losing its attachment.");
        }
        foreach (var invalid in new[] { new Size(float.NaN, 1), new Size(1, -1), new Size(float.PositiveInfinity, 1) })
        {
            using var host = new Host(new Dispatcher());
            Build(host);
            var backend = new Backend { Initial = invalid };
            host.Attach(backend);
            Throws<InvalidOperationException>(() => host.ObserveViewport(_ => { }));
            Assert(!host.IsAttached && backend.SubscriptionDisposals == 1 &&
                backend.Log.IndexOf("unsubscribe") < backend.Log.IndexOf("backend-dispose"),
                "Invalid initial native metadata releases the returned subscription before detaching.");
        }
        foreach (bool nullSubscription in new[] { false, true })
        {
            using var host = new Host(new Dispatcher());
            Build(host);
            var backend = new Backend { OmitInitial = !nullSubscription, NullSubscription = nullSubscription };
            host.Attach(backend);
            Throws<InvalidOperationException>(() => host.ObserveViewport(_ => { }));
            Assert(!host.IsAttached && backend.Disposed, "Missing initial metadata or null subscriptions are explicit protocol failures.");
        }
        using (var host = new Host(dispatcher))
        {
            Build(host);
            var backend = new Backend { RejectObserve = true };
            host.Attach(backend);
            Throws<NotSupportedException>(() => host.ObserveViewport(_ => { }));
            Assert(host.IsAttached && !backend.Disposed,
                "A capability rejection before returning a subscription leaves the active backend owned and attached.");
            backend.RejectObserve = false;
            using var observation = host.ObserveViewport(_ => throw new ApplicationException("authored callback"));
            Throws<ApplicationException>(dispatcher.DrainOne);
            Assert(host.IsAttached && dispatcher.Pending == 0,
                "Authored callback errors propagate through the dispatcher without an unobserved task or automatic retry.");
            Throws<InvalidOperationException>(() => backend.Emit(new(1, float.NaN)));
            Assert(!host.IsAttached && backend.SubscriptionDisposals == 1,
                "Invalid later metadata faults and retires the native attachment.");
        }
        foreach (bool inline in new[] { false, true })
        {
            var brokenDispatcher = new Dispatcher { Inline = inline, Reject = !inline };
            using var host = new Host(brokenDispatcher);
            Build(host);
            var backend = new Backend();
            host.Attach(backend);
            Throws<InvalidOperationException>(() => host.ObserveViewport(_ => throw new Exception("must not run inline")));
            Assert(!host.IsAttached && backend.SubscriptionDisposals == 1,
                "Rejected posts and synchronous dispatchers fail explicitly instead of invoking authored work inside native operations.");
        }
        var laterReject = new Dispatcher();
        using (var host = new Host(laterReject))
        {
            Build(host);
            var backend = new Backend();
            host.Attach(backend);
            using var observation = host.ObserveViewport(_ => { });
            laterReject.DrainOne();
            laterReject.Reject = true;
            Throws<InvalidOperationException>(() => backend.Emit(new(1000, 600)));
            Assert(!host.IsAttached && backend.SubscriptionDisposals == 1,
                "A later dispatcher rejection retires the attachment, not just initial observation failures.");
        }
        var canceledDispatcher = new Dispatcher();
        using (var host = new Host(canceledDispatcher))
        {
            Build(host);
            var backend = new Backend();
            host.Attach(backend);
            using var observation = host.ObserveViewport(_ => throw new Exception("canceled work ran"));
            Throws<OperationCanceledException>(canceledDispatcher.CancelOne);
            Assert(!host.IsAttached && backend.SubscriptionDisposals == 1,
                "Canceled accepted dispatcher work releases the subscription and surfaces cancellation.");
        }
        using (var host = new Host(new Dispatcher()))
        {
            Build(host);
            var backend = new Backend { Initial = new(float.NaN, 0), FailSubscriptionDispose = true, FailDispose = true };
            host.Attach(backend);
            var error = Throws<AggregateException>(() => host.ObserveViewport(_ => { }));
            Assert(error.Flatten().InnerExceptions.Count == 3 && !host.IsAttached,
                "Protocol, subscription cleanup, and backend cleanup errors all remain visible.");
        }
        using (var host = new Host(new Dispatcher()))
        {
            Build(host);
            var backend = new Backend { FailSubscriptionDispose = true };
            host.Attach(backend);
            var observation = host.ObserveViewport(_ => { });
            Throws<ApplicationException>(observation.Dispose);
            Assert(!host.IsAttached && backend.Disposed && backend.SubscriptionDisposals == 1,
                "An explicit unsubscription failure detaches rather than leaving an unknown native listener active.");
            observation.Dispose();
        }
        var insideMutation = new Dispatcher();
        using (var host = new Host(insideMutation))
        {
            var input = Build(host);
            var backend = new Backend();
            host.Attach(backend);
            bool called = false;
            using var observation = host.ObserveViewport(_ => called = true);
            backend.Peers[^1].DuringUpdate = insideMutation.DrainOne;
            Throws<InvalidOperationException>(() => input.Help = "Triggers hostile nested message pumping");
            Assert(!called && !host.IsAttached && backend.SubscriptionDisposals == 1,
                "A dispatcher pumped inside native mutation never invokes authored viewport code under the native guard.");
        }
        var callbackDetach = new Dispatcher();
        using (var host = new Host(callbackDetach))
        {
            Build(host);
            var backend = new Backend();
            host.Attach(backend);
            using var observation = host.ObserveViewport(_ => host.Detach());
            callbackDetach.DrainOne();
            Assert(!host.IsAttached && backend.SubscriptionDisposals == 1 && callbackDetach.Pending == 0,
                "An authored viewport callback can detach safely without reentrant delivery or leftover work.");
        }
    }

    private static void RetainedGridChecks()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        Grid grid;
        TextInput editor;
        Label catalog;
        Label details;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            grid = host.Grid("Retained responsive workspace");
            grid.SetTracks([new(TrackSizing.Star, 1)],
                [new(TrackSizing.Star, 1), new(TrackSizing.Star, 1), new(TrackSizing.Star, 1), new(TrackSizing.Star, 1)]);
            var navigation = host.Label("Native navigation slot");
            catalog = host.Label("Catalog");
            editor = host.TextInput("Persistent editor");
            editor.Text = "Draft across exact width boundaries";
            details = host.Label("Details");
            grid.Add(navigation, column: 0).Add(catalog, column: 1).Add(editor, column: 2).Add(details, column: 3);
            root.Add(grid);
            host.SetContent(root);
            build.Complete();
        }
        var backend = new Backend { Initial = new(320, 640) };
        host.Attach(backend);
        var originalPeers = backend.Peers.ToArray();
        var originalChildren = grid.Children.ToArray();
        WidthMode? applied = null;
        int transitions = 0;
        using var observation = host.ObserveViewport(size =>
        {
            var mode = WidthBreakpoints.Default.Select(size.Width);
            if (mode == applied) return;
            applied = mode;
            transitions++;
            grid.SetTracks([new(TrackSizing.Star, 1)],
            [
                new(TrackSizing.Fixed, mode == WidthMode.Expanded ? 200 : 48),
                new(TrackSizing.Fixed, mode == WidthMode.Expanded ? 240 : mode == WidthMode.Medium ? 200 : 0),
                new(TrackSizing.Star, 1, mode == WidthMode.Compact ? 0 : 320),
                new(TrackSizing.Fixed, mode == WidthMode.Expanded ? 240 : 0)
            ]);
            catalog.Visible = mode != WidthMode.Compact;
            details.Visible = mode == WidthMode.Expanded;
        });
        foreach (var (width, mode, editorWidth) in new[]
        {
            (320f, WidthMode.Compact, 272f), (719f, WidthMode.Compact, 671f),
            (720f, WidthMode.Medium, 472f), (1119f, WidthMode.Medium, 871f),
            (1120f, WidthMode.Expanded, 440f), (719f, WidthMode.Compact, 671f)
        })
        {
            backend.Emit(new(width, 640));
            dispatcher.DrainOne();
            Assert(applied == mode && grid.Columns.Count == 4 && grid.Rows.Count == 1,
                "Responsive transitions retain the same authored grid shape.");
            var allocation = GridLayoutMath.Arrange(grid.Rows, grid.Columns,
                grid.Children.Select(child => child.Cell!.Value).ToArray(),
                new(width, 640), false, false, (_, _, _) => new(0, 0));
            Assert(allocation.Cells[2].Width == editorWidth && allocation.Columns.Sum() == width,
                "The editor consumes the actual remaining width, including compact widths below the desktop minimum.");
            Assert(editor.Visible && catalog.Visible == (mode != WidthMode.Compact) &&
                details.Visible == (mode == WidthMode.Expanded),
                "Only auxiliary controls are hidden; zero-width tracks alone are not treated as input or accessibility hiding.");
            Assert(originalChildren.SequenceEqual(grid.Children) && originalPeers.SequenceEqual(backend.Peers) &&
                editor.Text == "Draft across exact width boundaries",
                "Width-mode changes neither reparent the editor nor replace any native peer.");
        }
        Assert(transitions == 4 && backend.Peers.All(peer => !peer.Updates.Contains(ElementProperty.Text)),
            "Same-mode resizing skips track mutations and never writes editor text.");
    }

    private static TextInput Build(Host host)
    {
        using var build = host.BeginBuild();
        var root = host.Stack(Axis.Vertical);
        var input = host.TextInput("Draft");
        input.Text = "Retained draft";
        root.Add(input);
        host.SetContent(root);
        build.Complete();
        return input;
    }

    private sealed class Dispatcher : ICancellableUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        private readonly Queue<(Action Run, Action<Exception> Cancel)> pending = [];
        public bool Inline { get; init; }
        public bool Reject { get; set; }
        public int Pending => pending.Count;
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action) => Post(action, error => throw error);
        public void Post(Action action, Action<Exception> canceled)
        {
            if (Reject) throw new InvalidOperationException("Dispatcher rejects work.");
            if (Inline) action();
            else pending.Enqueue((action, canceled));
        }
        public void DrainOne() => pending.Dequeue().Run();
        public void CancelOne() => pending.Dequeue().Cancel(new OperationCanceledException("Dispatcher stopped."));
    }

    private sealed class PlainDispatcher(Dispatcher inner) : IUiDispatcher
    {
        public bool CheckAccess() => inner.CheckAccess();
        public void Post(Action action) => inner.Post(action);
    }

    private sealed class Backend : IHostViewportBackend
    {
        private readonly List<Action<Size>> callbacks = [];
        public Size Initial { get; init; } = new(800, 600);
        public bool OmitInitial { get; init; }
        public bool NullSubscription { get; init; }
        public bool RejectObserve { get; set; }
        public bool FailSubscriptionDispose { get; init; }
        public bool FailDispose { get; init; }
        public bool Disposed { get; private set; }
        public int SubscriptionDisposals { get; private set; }
        public List<string> Log { get; } = [];
        public List<Peer> Peers { get; } = [];
        public Action<Size>? Callback => callbacks.FirstOrDefault();
        public void Emit(Size size)
        {
            foreach (var callback in callbacks.ToArray()) callback(size);
        }
        public IDisposable ObserveViewport(Action<Size> changed)
        {
            if (RejectObserve) throw new NotSupportedException("No viewport authority.");
            if (!OmitInitial) changed(Initial);
            if (NullSubscription) return null!;
            callbacks.Add(changed);
            return new Subscription(() =>
            {
                Log.Add("unsubscribe");
                SubscriptionDisposals++;
                callbacks.Remove(changed);
                if (FailSubscriptionDispose) throw new ApplicationException("Unsubscribe failed.");
            });
        }
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new Peer();
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose()
        {
            Disposed = true;
            Log.Add("backend-dispose");
            if (FailDispose) throw new ApplicationException("Backend cleanup failed.");
        }
    }

    private sealed class Subscription(Action release) : IDisposable
    {
        public void Dispose() => release();
    }

    private sealed class LegacyBackend : IBackend
    {
        public IElementPeer Create(Element element, IControlEvents events) => new Peer();
        public void Mount(IElementPeer root) { }
        public void Dispose() { }
    }

    private sealed class Peer : IElementPeer
    {
        public List<ElementProperty> Updates { get; } = [];
        public Action? DuringUpdate { get; set; }
        public void AddChild(IElementPeer child) { }
        public void Update(ElementProperty property) { Updates.Add(property); DuringUpdate?.Invoke(); }
        public void Dispose() { }
    }
}
