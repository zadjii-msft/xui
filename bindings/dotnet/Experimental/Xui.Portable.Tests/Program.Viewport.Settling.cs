using PortableDemo;
using PortableMutation;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void ViewportSettlingChecks()
    {
        using (var host = new Host(new Dispatcher()))
        {
            var demo = new Greeting(host);
            var backend = new ViewportBackend();
            host.Attach(backend);
            using var lease = host.BeginVirtualViewport((ScrollView)demo.Root.Children[1], 10000, 32, 1, _ => { });
            Assert(lease is not ISettledVirtualViewportLease, "A legacy native lease does not acquire a fake settling capability.");
            Assert(host.IsAttached && backend.Leases.Single().CommittedEpoch == 0, "Capability discovery does not stage or commit the old native lease.");
        }
        SettledViewportOrdering();
        SettledViewportGuards();
        SettledViewportFailures();
    }

    private static void SettledViewportOrdering()
    {
        using var host = new Host(new Dispatcher());
        var board = new MutationBoard(host);
        var scroll = board.Root.Children.OfType<ScrollView>().Single();
        var backend = new ViewportBackend { SupportsSettling = true };
        host.Attach(backend);
        IVirtualViewportLease? lease = null;
        int callbacks = 0;
        bool completed = false;
        lease = host.BeginVirtualViewport(scroll, 10000, 32, 1, request =>
        {
            callbacks++;
            var active = lease!;
            var settled = (ISettledVirtualViewportLease)active;
            Assert(active.TryBeginUpdate(request.Epoch) == VirtualViewportUpdateResult.Ready, "The capable facade forwards reservation.");
            board.Rows = [KeyedItem.Create("temporary", h => new MutationRow(h, "temporary"))];
            Assert(active.TryCommit(request.Epoch) == VirtualViewportCommitResult.Committed, "The capable facade forwards commit.");
            board.Rows = [];
            settled.FlushCommitted(request.Epoch);
            completed = true;
            backend.Trace.Add("completed");
        });
        Assert(lease is ISettledVirtualViewportLease, "The Host preserves a real native settling capability.");
        var native = backend.Leases.Single();
        native.Emit(Request(1));
        Assert(completed && native.FlushCalls == 1 && native.FlushedEpoch == 1, "Completion runs only after the actual committed epoch was flushed.");
        Assert(backend.Trace.IndexOf("commit") < backend.Trace.IndexOf("remove") &&
            backend.Trace.IndexOf("remove") < backend.Trace.IndexOf("flush") &&
            backend.Trace.IndexOf("flush") < backend.Trace.IndexOf("completed"), "Settling follows native commit and controller-style pruning, not a posted barrier.");

        var settledLease = (ISettledVirtualViewportLease)lease;
        var committed = (native.CommittedEpoch, native.CommittedSourceVersion, native.CommittedOffset);
        lease.SetExtent(12000, 2);
        lease.RequestOffset(960);
        settledLease.FlushCommitted(1);
        Assert((native.CommittedEpoch, native.CommittedSourceVersion, native.CommittedOffset) == committed &&
            native.SourceVersion == 2 && native.Offset == 960 && callbacks == 1, "Flush leaves committed state unchanged and does not consume queued source or navigation intent.");
        native.Emit(Request(2, 2));
        Assert(callbacks == 2 && native.CommittedEpoch == 2 && native.FlushedEpoch == 2, "A later request is still delivered and can settle independently.");
        int flushes = native.FlushCalls;
        Throws<InvalidOperationException>(() => settledLease.FlushCommitted(1));
        Assert(native.FlushCalls == flushes && host.IsAttached, "An older committed epoch rejects before native work without damaging the attachment.");
        settledLease.FlushCommitted(2);
        Assert(native.FlushCalls == flushes + 1, "Repeated flushes of the current committed epoch may settle later prune work.");
        host.Detach();
        Throws<ObjectDisposedException>(() => settledLease.FlushCommitted(2));
        Assert(native.DisposeCalls == 1, "Capability facades share the attachment's single lease owner.");
        lease.Dispose();
        Assert(native.DisposeCalls == 1, "Facade disposal after detach is idempotent.");
        var replacement = new ViewportBackend { SupportsSettling = true };
        host.Attach(replacement);
        using var newLease = host.BeginVirtualViewport(scroll, 12000, 32, 2, _ => { });
        Throws<ObjectDisposedException>(() => settledLease.FlushCommitted(2));
        Assert(replacement.Leases.Single().FlushCalls == 0, "Old facade handles cannot flush a replacement attachment.");
    }

    private static void SettledViewportGuards()
    {
        using var host = new Host(new Dispatcher());
        ScrollView first, second;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            first = host.ScrollView(host.Stack(Axis.Vertical), "First viewport");
            second = host.ScrollView(host.Stack(Axis.Vertical), "Second viewport");
            root.Add(first).Add(second);
            host.SetContent(root);
            build.Complete();
        }
        var backend = new ViewportBackend { SupportsSettling = true };
        host.Attach(backend);
        using var firstLease = host.BeginVirtualViewport(first, 10, 32, 1, _ => { });
        using var secondLease = host.BeginVirtualViewport(second, 10, 32, 1, _ => { });
        var settled = (ISettledVirtualViewportLease)firstLease;
        var native = backend.Leases[0];
        Throws<ArgumentOutOfRangeException>(() => settled.FlushCommitted(0));
        Throws<ArgumentOutOfRangeException>(() => settled.FlushCommitted(-1));
        Throws<InvalidOperationException>(() => settled.FlushCommitted(1));
        Assert(native.FlushCalls == 0, "A settling capability does not imply an epoch has committed.");
        native.Emit(Request(1));
        firstLease.TryBeginUpdate(1);
        Throws<InvalidOperationException>(() => settled.FlushCommitted(1));
        firstLease.TryCommit(1);
        settled.FlushCommitted(1);
        native.Emit(Request(2));
        firstLease.TryBeginUpdate(2);
        int flushes = native.FlushCalls;
        Throws<InvalidOperationException>(() => settled.FlushCommitted(1));
        Assert(native.FlushCalls == flushes, "Even the last committed epoch cannot flush while its next batch is Ready.");
        firstLease.Cancel(2);
        backend.Leases[1].Emit(Request(1));
        secondLease.TryBeginUpdate(1);
        Throws<InvalidOperationException>(() => settled.FlushCommitted(1));
        Assert(native.FlushCalls == flushes && host.IsAttached, "A Ready batch on another viewport also blocks settling before native work.");
        secondLease.Cancel(1);
        Task.Run(() => Throws<InvalidOperationException>(() => settled.FlushCommitted(1))).GetAwaiter().GetResult();
        Throws<InvalidOperationException>(() => settled.FlushCommitted(2));
        Assert(native.FlushCalls == flushes, "Wrong-thread and uncommitted epochs never reach native flush.");
        settled.FlushCommitted(1);
        Assert(native.FlushCalls == flushes + 1, "Canceling a new batch leaves the earlier committed epoch available to settle.");
    }

    private static void SettledViewportFailures()
    {
        using (var host = new Host(new Dispatcher()))
        {
            var demo = new Greeting(host);
            var backend = new ViewportBackend { SupportsSettling = true };
            host.Attach(backend);
            using var lease = host.BeginVirtualViewport((ScrollView)demo.Root.Children[1], 10, 32, 1, _ => { });
            var native = backend.Leases.Single();
            native.Emit(Request(1));
            lease.TryBeginUpdate(1);
            lease.TryCommit(1);
            int notices = 0;
            demo.Input.InteractionChanged += state =>
            {
                Assert(!native.InFlush, "Native interaction notifications wait until flush leaves the backend guard.");
                Assert(state == new TextInteraction(true, false), "Deferred interaction retains the native snapshot.");
                notices++;
            };
            native.DuringFlush = () =>
            {
                Assert(!backend.Find("name").Events.Change("synchronous input"), "Flush never admits synchronous authored input.");
                ((ITextInteractionEvents)backend.Find("name").Events).InteractionChanged(new(true, false));
                Assert(notices == 0 && demo.Input.Interaction == new TextInteraction(true, false), "Flush captures metadata without invoking authored handlers inline.");
                Throws<InvalidOperationException>(() => demo.Entry = "backend reentry");
            };
            ((ISettledVirtualViewportLease)lease).FlushCommitted(1);
            Assert(notices == 1 && demo.Entry == "", "The flush guard suppresses mutation without losing deferred native metadata.");
        }
        foreach (string failure in new[] { "native", "synchronous-request", "cleanup" })
        {
            using var host = new Host(new Dispatcher());
            var demo = new Greeting(host);
            var backend = new ViewportBackend { SupportsSettling = true };
            host.Attach(backend);
            int callbacks = 0;
            using var lease = host.BeginVirtualViewport((ScrollView)demo.Root.Children[1], 10, 32, 1, _ => callbacks++);
            var native = backend.Leases.Single();
            native.Emit(Request(1));
            lease.TryBeginUpdate(1);
            demo.Entry = "committed model";
            lease.TryCommit(1);
            if (failure == "synchronous-request") native.SynchronousOperation = "flush";
            else if (failure == "native") native.FailOperation = "flush";
            else
            {
                native.DuringFlush = () => throw new ApplicationException("settle failed");
                native.FailOperation = "dispose";
            }
            Exception error = Throws<Exception>(() => ((ISettledVirtualViewportLease)lease).FlushCommitted(1));
            Assert(!host.IsAttached && demo.Entry == "committed model" && callbacks == 1, failure + " settling failure detaches while retaining the committed model and suppressing inline requests.");
            Assert(native.DisposeCalls == 1 && backend.Disposed && backend.Peers.All(peer => peer.DisposeCalls == 1), failure + " settling failure releases every native owner exactly once.");
            if (failure == "cleanup")
            {
                var messages = ((AggregateException)error).Flatten().InnerExceptions.Select(inner => inner.Message).ToArray();
                Assert(messages.Contains("settle failed") && messages.Contains("dispose"), "Settling and cleanup failures are both reported.");
            }
            native.Emit(Request(2));
            Assert(callbacks == 1, "Failure cleanup invalidates later native callbacks.");
        }
    }
}
