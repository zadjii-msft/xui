using PortableDemo;
using RevealFixtures;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void RuntimeChecks()
    {
        using var host = new Host(new Dispatcher());
        var (reveal, input, button) = Tree(host);
        Assert(!reveal.Open && reveal.Motion == RevealMotion.Default && ReferenceEquals(reveal.Content, input.Parent),
            "A reveal starts logically closed with one retained child and no motion request.");
        Throws<InvalidOperationException>(() => host.GetRevealPresentation(reveal));
        var legacy = new LegacyBackend();
        Throws<NotSupportedException>(() => host.Attach(legacy));
        Assert(!host.IsAttached && legacy.Disposed, "Even zero-duration reveals require the native retained-reveal capability.");
        var backend = new Backend();
        host.Attach(backend);
        var peer = backend.Find(reveal);
        var editor = backend.Find(input);
        var action = backend.Find(button);
        var originalPeers = backend.Peers.ToArray();
        int changes = 0, clicks = 0;
        input.Changed += _ => changes++;
        button.Click += () => clicks++;
        Assert(host.GetRevealPresentation(reveal) == new RevealPresentation(0, false),
            "The initial native attachment is settled rather than starting an implicit entry animation.");
        Assert(!editor.Events.Change("hidden") && !action.Events.Click() && changes == 0 && clicks == 0,
            "A closed reveal rejects descendant events independently of native exit pixels.");
        Assert(reveal.TrySetOpen(true) && reveal.Open && host.GetRevealPresentation(reveal) == new RevealPresentation(1, false),
            "A zero-duration open request commits and observes the native settled presentation.");
        Assert(editor.Events.Change("retained native draft") && action.Events.Click() && changes == 1 && clicks == 1,
            "Open content keeps ordinary native input and activation semantics.");
        peer.NativePresentation = new(0.35f, true);
        Assert(reveal.Open && host.GetRevealPresentation(reveal) == new RevealPresentation(0.35f, true),
            "Native progress is not fabricated from the logical open state or configured duration.");
        int reads = peer.PresentationReads;
        host.GetRevealPresentation(reveal);
        Assert(peer.PresentationReads == reads + 1 && peer.NativePresentation == new RevealPresentation(0.35f, true),
            "Reading presentation does not advance a managed animation clock.");

        peer.Updates.Clear();
        var nullMotion = Throws<KeyedUpdateException>(() => reveal.SetState(false, null!));
        Assert(!nullMotion.ModelCommitted && nullMotion.InnerException is ArgumentNullException &&
            reveal.Open && reveal.Motion == RevealMotion.Default && peer.Updates.Count == 0,
            "A null paired motion request is rejected before either requested field changes.");
        peer.VetoClose = true;
        var proposed = new RevealMotion(400, RevealDirection.Right);
        Assert(!reveal.TrySetState(false, proposed) && reveal.Open && reveal.Motion == RevealMotion.Default && peer.Updates.Count == 0,
            "An atomic close veto preserves both requested fields and makes no native update.");
        var veto = Throws<KeyedUpdateException>(() => reveal.SetOpen(false));
        Assert(!veto.ModelCommitted && veto.InnerException is InvalidOperationException && reveal.Open,
            "The throwing open setter exposes a precommit veto instead of reporting success.");
        peer.RejectPreflight = true;
        var unsupported = Throws<KeyedUpdateException>(() => reveal.TrySetOpen(false));
        Assert(!unsupported.ModelCommitted && unsupported.InnerException is NotSupportedException,
            "TrySetOpen returns false only for focus/composition veto, not unsupported native operations.");
        peer.RejectPreflight = false;
        peer.VetoClose = false;
        peer.Validating = () => Throws<InvalidOperationException>(() => input.Text = "reentrant preflight");
        Assert(reveal.TrySetState(false, proposed), "A validated paired close request succeeds.");
        peer.Validating = null;
        Assert(!reveal.Open && reveal.Motion == proposed && peer.Updates.SequenceEqual([ElementProperty.RevealState]) &&
            peer.NativeState == (false, proposed),
            "Open and motion commit through one complete native state update, not old-duration intermediate states.");
        peer.NativePresentation = new(0.2f, true);
        Assert(!editor.Events.Change("late exit edit") && !action.Events.Click() &&
            input.Text == "retained native draft" && changes == 1,
            "Input is inert immediately on logical close even while actual native presentation is animating out.");
        var beforeOpenChecks = peer.OpenChecks;
        var beforeMotionChecks = peer.MotionChecks;
        reveal.SetState(false, new RevealMotion(400, RevealDirection.Right));
        Assert(peer.OpenChecks == beforeOpenChecks && peer.MotionChecks == beforeMotionChecks && peer.Updates.Count == 1,
            "An equal paired state does not repeat preflight or restart native motion.");
        reveal.SetMotion(new RevealMotion(180));
        Assert(!reveal.Open && peer.NativeState == (false, new RevealMotion(180)),
            "Changing only motion preserves logical open state.");
        reveal.SetOpen(true);
        Assert(reveal.Motion == new RevealMotion(180) && peer.NativeState == (true, new RevealMotion(180)),
            "Changing only open state preserves the configured motion.");
        Assert(originalPeers.SequenceEqual(backend.Peers) && !editor.Updates.Contains(ElementProperty.Text),
            "Reveal operations retain all native peers and never rewrite unrelated editor text.");
        peer.ReadingPresentation = () => Throws<InvalidOperationException>(() => input.Text = "reentrant query");
        host.GetRevealPresentation(reveal);
        peer.ReadingPresentation = null;
        foreach (float invalid in new[] { float.NaN, float.PositiveInfinity, float.NegativeInfinity, -0.01f, 1.01f })
        {
            peer.NativePresentation = new(invalid, true);
            Throws<InvalidOperationException>(() => host.GetRevealPresentation(reveal));
        }
        using var other = new Host(new Dispatcher());
        var otherReveal = Tree(other).Reveal;
        Throws<InvalidOperationException>(() => host.GetRevealPresentation(otherReveal));
        Throws<ArgumentNullException>(() => host.GetRevealPresentation(null!));
        Task.Run(() => Throws<InvalidOperationException>(() => host.GetRevealPresentation(reveal))).GetAwaiter().GetResult();
        peer.NativePresentation = new(0.5f, true);
        peer.FailUpdate = true;
        var failed = Throws<KeyedUpdateException>(() => reveal.SetState(false, new RevealMotion(200, RevealDirection.Right)));
        Assert(failed.ModelCommitted && !host.IsAttached && !reveal.Open && reveal.Motion == new RevealMotion(200, RevealDirection.Right) &&
            backend.Disposed && backend.Peers.All(value => value.Disposed),
            "A native postcommit failure retains both fields, marks commitment, and detaches the failed tree.");
        Assert(!editor.Events.Change("stale") && !action.Events.Click(),
            "A failed native attachment cannot reactivate descendant input callbacks.");
        var recovered = new Backend();
        host.Attach(recovered);
        Assert(host.GetRevealPresentation(reveal) == new RevealPresentation(0, false) &&
            recovered.Find(reveal).NativeState == (false, new RevealMotion(200, RevealDirection.Right)),
            "Reattachment applies retained requested state at a settled presentation, not an accidental transition.");
        host.Dispose();
        Throws<ObjectDisposedException>(() => host.GetRevealPresentation(reveal));
    }

    private static void MotionPolicyChecks()
    {
        using var host = new Host(new Dispatcher());
        var (reveal, input, _) = Tree(host);
        var backend = new Backend();
        host.Attach(backend);
        var peer = backend.Find(reveal);
        var editor = backend.Find(input);
        reveal.SetState(true, new RevealMotion(180));
        Assert(editor.Events.Change("Draft across motion configuration"), "The motion-policy fixture retains a native draft.");
        peer.NativePresentation = new(0.25f, true);
        peer.VetoClose = true;
        int openChecks = peer.OpenChecks;
        reveal.SetMotion(new RevealMotion(240, RevealDirection.Right));
        Assert(reveal.Open && peer.OpenChecks == openChecks && host.GetRevealPresentation(reveal) == new RevealPresentation(1, false),
            "A motion-only change reaches the peer atomically without requesting close or fabricating progress in the host.");
        int updates = peer.Updates.Count;
        reveal.SetState(true, new RevealMotion(240, RevealDirection.Right));
        Assert(peer.Updates.Count == updates && host.GetRevealPresentation(reveal) == new RevealPresentation(1, false),
            "An equal pair cannot restart a settled native presentation.");
        peer.VetoClose = false;
        reveal.SetMotion(new RevealMotion(180));
        peer.NativePresentation = new(0.6f, true);
        reveal.SetOpen(false);
        Assert(host.GetRevealPresentation(reveal) == new RevealPresentation(0.6f, true),
            "An unchanged-motion reversal preserves the peer's current presentation sample.");
        peer.NativePresentation = new(0.4f, true);
        reveal.SetState(true, new RevealMotion(200, RevealDirection.Right));
        Assert(host.GetRevealPresentation(reveal) == new RevealPresentation(0, true) &&
            peer.NativeState == (true, new RevealMotion(200, RevealDirection.Right)),
            "A paired configuration change settles the old closed target before starting the new open target.");
        peer.NativePresentation = new(0.4f, true);
        reveal.SetMotion(new RevealMotion(0, RevealDirection.Right));
        Assert(host.GetRevealPresentation(reveal) == new RevealPresentation(1, false),
            "Switching to instant motion observes the peer's settled current logical endpoint.");
        reveal.SetState(false, new RevealMotion(400));
        Assert(host.GetRevealPresentation(reveal) == new RevealPresentation(1, true) && !reveal.Open,
            "A paired close with new motion starts from the settled old open endpoint while input closes immediately.");
        peer.NativePresentation = new(0.7f, true);
        reveal.SetMotion(new RevealMotion(100));
        Assert(host.GetRevealPresentation(reveal) == new RevealPresentation(0, false),
            "Changing motion while closing settles the closed logical endpoint without another transition.");
        Assert(input.Text == "Draft across motion configuration" && !editor.Updates.Contains(ElementProperty.Text),
            "Configuration changes never rewrite the retained editor draft.");
    }

    private static void GeneratedChecks()
    {
        using var host = new Host(new Dispatcher());
        var fixture = new RevealFixture(host);
        var backend = new Backend();
        host.Attach(backend);
        var peer = backend.Find(fixture.Drawer);
        fixture.Target = new(true, new RevealMotion(180));
        Assert(peer.Updates.SequenceEqual([ElementProperty.RevealState]) && peer.NativeState == (true, new RevealMotion(180)),
            "Paired generated expressions produce one atomic native state update.");
        var before = fixture.Target;
        peer.VetoClose = true;
        var rejected = Throws<KeyedUpdateException>(() => fixture.Target = new(false, new RevealMotion(400, RevealDirection.Right)));
        Assert(!rejected.ModelCommitted && fixture.Target == before && fixture.Drawer.Open &&
            fixture.Drawer.Motion == before.Motion && peer.Updates.Count == 1,
            "Precommit veto restores authored state and binding cache; it cannot leave false UI state with an open native reveal.");
        peer.VetoClose = false;
        fixture.Target = new(false, new RevealMotion(400, RevealDirection.Right));
        Assert(peer.Updates.Count == 2 && !fixture.Drawer.Open && fixture.Drawer.Motion == new RevealMotion(400, RevealDirection.Right),
            "Retrying the previously vetoed generated pair is not suppressed by a poisoned binding cache.");
        var previouslyCached = fixture.Target;
        peer.FailUpdate = true;
        var committed = new RevealTarget(true, new RevealMotion(200));
        var failed = Throws<KeyedUpdateException>(() => fixture.Target = committed);
        Assert(failed.ModelCommitted && fixture.Target == committed && fixture.Drawer.Open &&
            fixture.Drawer.Motion == committed.Motion && !host.IsAttached,
            "Postcommit native failure preserves authored and model state consistently for later reattachment.");
        fixture.Target = previouslyCached;
        Assert(fixture.Target == previouslyCached && fixture.Drawer.Open == previouslyCached.Open &&
            fixture.Drawer.Motion == previouslyCached.Motion,
            "Returning to the formerly cached pair after a postcommit failure still updates the detached model.");
        var recovered = new Backend();
        host.Attach(recovered);
        Assert(recovered.Find(fixture.Drawer).NativeState == (previouslyCached.Open, previouslyCached.Motion) &&
            host.GetRevealPresentation(fixture.Drawer) == new RevealPresentation(0, false),
            "Reattachment receives the restored pair, not the failed request hidden by an obsolete generated cache.");

        using var openHost = new Host(new Dispatcher());
        var open = new OpenOnlyFixture(openHost);
        var openBackend = new Backend();
        openHost.Attach(openBackend);
        open.Drawer.Motion = new RevealMotion(250, RevealDirection.Right);
        open.Expanded = true;
        Assert(open.Drawer.Open && open.Drawer.Motion == new RevealMotion(250, RevealDirection.Right),
            "An open-only generated binding does not erase an independently set motion value.");
        using var motionHost = new Host(new Dispatcher());
        var motion = new MotionOnlyFixture(motionHost);
        motionHost.Attach(new Backend());
        motion.Drawer.Open = true;
        motion.Motion = new RevealMotion(300);
        Assert(motion.Drawer.Open && motion.Drawer.Motion == new RevealMotion(300),
            "A motion-only generated binding does not erase an independently set open value.");
        WorkbenchChecks();
    }

    private static void WorkbenchChecks()
    {
        using var host = new Host(new Dispatcher());
        var workbench = new RevealWorkbench(host);
        var backend = new Backend();
        host.Attach(backend);
        var original = backend.Peers.ToArray();
        var reveal = backend.Find(workbench.Drawer);
        Peer ById(string id) => backend.Peers.Single(peer => peer.Element is Control control && control.AutomationId == id);
        Assert(ById("reveal-toggle").Events.Click() && workbench.RequestedOpen && workbench.Drawer.Open,
            "The shared workbench opens its retained native details.");
        ById("reveal-note").Events.Change("Native retained workbench draft");
        reveal.VetoClose = true;
        Assert(ById("reveal-toggle").Events.Click() && workbench.Drawer.Open && workbench.RequestedOpen &&
            workbench.Status.StartsWith("Close deferred:", StringComparison.Ordinal),
            "The shared app reports a close veto and keeps the requested state truthful.");
        reveal.VetoClose = false;
        foreach (string id in new[] { "reveal-instant", "reveal-right", "reveal-animate", "reveal-bottom", "reveal-toggle", "reveal-toggle" })
            Assert(ById(id).Events.Click(), "A shared workbench action reaches real compiled C#.");
        Assert(workbench.Draft == "Native retained workbench draft" && original.SequenceEqual(backend.Peers) &&
            !ById("reveal-note").Updates.Contains(ElementProperty.Text),
            "Shared reveal actions preserve the editor draft and peer identity without writeback.");
    }

    private static (Reveal Reveal, TextInput Input, Button Button) Tree(Host host)
    {
        using var build = host.BeginBuild();
        var root = host.Stack(Axis.Vertical);
        var content = host.Stack(Axis.Vertical);
        var input = host.TextInput("Retained reveal input");
        var button = host.Button("Retained action");
        content.Add(input).Add(button);
        var reveal = host.Reveal(content, "Retained details");
        root.Add(reveal);
        host.SetContent(root);
        build.Complete();
        return (reveal, input, button);
    }

    private sealed class Dispatcher : IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action) => throw new NotSupportedException("Reveal model fixtures do not create a managed clock.");
    }

    private sealed class Backend : IBackend
    {
        public List<Peer> Peers { get; } = [];
        public bool Disposed { get; private set; }
        public Peer Find(Element element) => Peers.Single(peer => ReferenceEquals(peer.Element, element));
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new Peer(element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() => Disposed = true;
    }

    private sealed class Peer(Element element, IControlEvents events) : IRevealElementPeer, IConstrainedElementPeer
    {
        public Element Element { get; } = element;
        public IControlEvents Events { get; } = events;
        public List<ElementProperty> Updates { get; } = [];
        public (bool Open, RevealMotion Motion)? NativeState { get; private set; } =
            element is Reveal reveal ? (reveal.Open, reveal.Motion) : null;
        public RevealPresentation NativePresentation { get; set; } = new(element is Reveal { Open: true } ? 1 : 0, false);
        public bool Disposed { get; private set; }
        public bool VetoClose { get; set; }
        public bool RejectPreflight { get; set; }
        public bool FailUpdate { get; set; }
        public int OpenChecks { get; private set; }
        public int MotionChecks { get; private set; }
        public int PresentationReads { get; private set; }
        public Action? Validating { get; set; }
        public Action? ReadingPresentation { get; set; }
        public RevealPresentation Presentation
        {
            get { PresentationReads++; ReadingPresentation?.Invoke(); return NativePresentation; }
        }
        public bool CanSetOpen(bool open)
        {
            OpenChecks++;
            Validating?.Invoke();
            if (RejectPreflight) throw new NotSupportedException("Native reveal preflight is unsupported.");
            return open || !VetoClose;
        }
        public void ValidateMotion(RevealMotion motion)
        {
            MotionChecks++;
            Validating?.Invoke();
            if (RejectPreflight) throw new NotSupportedException("Native reveal motion is unsupported.");
        }
        public void AddChild(IElementPeer child) { }
        public void Update(ElementProperty property)
        {
            if (FailUpdate) throw new ApplicationException("Native reveal application failed.");
            Updates.Add(property);
            if (property == ElementProperty.RevealState)
            {
                var reveal = (Reveal)Element;
                var previous = NativeState ?? throw new InvalidOperationException("A native reveal state was not initialized.");
                bool configurationChanged = previous.Motion != reveal.Motion;
                float start = configurationChanged ? previous.Open ? 1 : 0 : NativePresentation.Progress;
                if (previous.Open != reveal.Open)
                {
                    float target = reveal.Open ? 1 : 0;
                    NativePresentation = reveal.Motion.DurationMilliseconds == 0
                        ? new(target, false) : new(start, start != target);
                }
                else if (configurationChanged) NativePresentation = new(start, false);
                NativeState = (reveal.Open, reveal.Motion);
            }
        }
        public void Dispose() => Disposed = true;
    }

    private sealed class LegacyBackend : IBackend
    {
        public bool Disposed { get; private set; }
        public IElementPeer Create(Element element, IControlEvents events) => new LegacyPeer();
        public void Mount(IElementPeer root) { }
        public void Dispose() => Disposed = true;
    }

    private sealed class LegacyPeer : IElementPeer
    {
        public void AddChild(IElementPeer child) { }
        public void Update(ElementProperty property) { }
        public void Dispose() { }
    }
}
