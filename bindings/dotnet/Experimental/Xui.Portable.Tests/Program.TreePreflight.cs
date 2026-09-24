using PortableMutation;
using Xui.Experimental.Portable;
using Stack = Xui.Experimental.Portable.Stack;

internal static partial class Program
{
    private static void BackendTreePreflightChecks()
    {
        InitialTreePreflightChecks();
        CandidateTreePreflightChecks();
        CandidateTreeCleanupChecks();
        TreePreflightInteractionChecks();
    }

    private static (Reveal Reveal, KeyedStack Rows, TextInput Input) PreflightTree(Host host)
    {
        using var build = host.BeginBuild();
        var root = host.Stack(Axis.Vertical);
        var rows = host.KeyedStack(Axis.Vertical);
        var input = host.TextInput("Surviving editor");
        input.AutomationId = "preflight-editor";
        var reveal = host.Reveal(rows, "Retained clipping owner");
        reveal.Open = true;
        root.Add(input).Add(reveal);
        host.SetContent(root);
        build.Complete();
        return (reveal, rows, input);
    }

    private static void InitialTreePreflightChecks()
    {
        using var host = new Host(new Dispatcher());
        var tree = PreflightTree(host);
        tree.Rows.Reconcile([KeyedItem.Create("document", h => new PreflightDocument(h))]);
        var root = host.Root;
        var rejected = new TreePreflightBackend();
        rejected.CheckingTree = candidate =>
        {
            Assert(ReferenceEquals(candidate, root) && candidate.Children.Count == 2,
                "Initial validation sees the completed model and its retained descendants.");
            Throws<InvalidOperationException>(() => tree.Input.Text = "native mutation");
            Throws<InvalidOperationException>(() => host.Dispose());
        };
        Throws<NotSupportedException>(() => host.Attach(rejected));
        Assert(rejected.TreeChecks == 1 && rejected.Peers.Count == 0 && !rejected.Disposed && !host.IsAttached,
            "Initial backend policy rejection happens before peer creation or ownership transfer.");
        Assert(ReferenceEquals(root, host.Root) && tree.Rows.Children.Count == 1 && tree.Input.Text == "",
            "Rejected attachment preserves the complete authored tree and editor state.");
        var accepted = new TreePreflightBackend { RejectRevealDocuments = false };
        host.Attach(accepted);
        Assert(host.IsAttached && accepted.Peers.Any(peer => peer.Element is MultilineText),
            "A different backend may support the same subtree; there is no common document ban.");
        host.Detach();
        Assert(accepted.Disposed && accepted.Peers.All(peer => peer.DisposeCalls == 1),
            "Accepted policy does not change ordinary attachment disposal.");
        using var outsideHost = new Host(new Dispatcher());
        _ = new PreflightDocument(outsideHost);
        var outside = new TreePreflightBackend();
        outsideHost.Attach(outside);
        Assert(outsideHost.IsAttached, "The platform-specific policy permits documents outside a Reveal ancestor.");
    }

    private static void CandidateTreePreflightChecks()
    {
        using var host = new Host(new Dispatcher());
        var tree = PreflightTree(host);
        MutationRow? retained = null;
        KeyedItem[] original = [KeyedItem.Create("kept", h => retained = new MutationRow(h, "kept"))];
        tree.Rows.Reconcile(original);
        retained!.Entry = "kept draft";
        var retainedLifetime = retained.Lifetime;
        var backend = new TreePreflightBackend();
        host.Attach(backend);
        var peers = backend.Peers.ToArray();
        var children = tree.Rows.Children.ToArray();
        int factories = 0;
        PreflightDocument? rejected = null;
        MutationRow? sibling = null;
        LifetimeResource? resource = null;
        backend.CheckingInsertion = (parent, candidate) =>
        {
            Assert(factories == 2 && ReferenceEquals(parent, tree.Rows) && ReferenceEquals(parent.Parent, tree.Reveal),
                "Every candidate factory completes before validation receives the real insertion parent and ancestor context.");
            Assert(candidate.Parent is null && tree.Rows.Children.SequenceEqual(children) && backend.Peers.SequenceEqual(peers),
                "Prospective roots are complete and unparented while the live model and peers remain unchanged.");
            Throws<InvalidOperationException>(() => tree.Input.Text = "forbidden");
            Throws<InvalidOperationException>(() => host.Detach());
            Assert(!backend.Peers.Single(peer => ReferenceEquals(peer.Element, tree.Input)).Events.Change("echo"),
                "Read-only backend preflight cannot synchronously deliver authored input.");
        };
        var failure = Throws<KeyedUpdateException>(() => tree.Rows.Reconcile(
        [
            original[0],
            KeyedItem.Create("sibling", h =>
            {
                factories++;
                sibling = new MutationRow(h, "sibling");
                _ = sibling.Lifetime;
                return sibling;
            }),
            KeyedItem.Create("restricted", h =>
            {
                factories++;
                rejected = new PreflightDocument(h);
                resource = rejected.Lifetime.Own(new LifetimeResource(() => { }));
                return rejected;
            })
        ]));
        Assert(!failure.ModelCommitted && failure.InnerException is NotSupportedException && host.IsAttached,
            "An unsupported prospective subtree is a precommit error, not a native insertion failure.");
        Assert(backend.InsertionChecks == 2 && backend.NativeMutations == 0 && backend.Peers.SequenceEqual(peers) &&
            tree.Rows.Children.SequenceEqual(children) && retained.Entry == "kept draft" && !retainedLifetime.Token.IsCancellationRequested,
            "Rejection retains the old editor, lifetime, native tree and selection order.");
        Assert(resource!.Calls == 1 && rejected!.Lifetime.Token.IsCancellationRequested && sibling!.Lifetime.Token.IsCancellationRequested,
            "All staged candidates are retired, including an earlier validated sibling.");
        Throws<ObjectDisposedException>(() => _ = rejected!.Document.Text);
        Throws<ObjectDisposedException>(() => _ = sibling!.Entry);
        backend.CheckingInsertion = null;

        var replacement = Throws<KeyedUpdateException>(() => tree.Rows.Reconcile(
            [KeyedItem.Create("kept", h => new PreflightDocument(h))]));
        Assert(!replacement.ModelCommitted && ReferenceEquals(tree.Rows.Children[0], retained.Root),
            "Replacing a surviving key with an unsupported component type validates before retiring the old component.");
        int checks = backend.InsertionChecks;
        tree.Rows.Reconcile([original[0], KeyedItem.Create("new", h => new MutationRow(h, "new"))]);
        Assert(backend.InsertionChecks == checks + 1 && host.IsAttached, "A supported candidate can commit after complete rollback.");
        var entries = tree.Rows.Children.ToArray();
        checks = backend.InsertionChecks;
        tree.Rows.Reconcile([KeyedItem.Create("new", h => new MutationRow(h, "unused")),
            KeyedItem.Create("kept", h => new MutationRow(h, "unused"), row => row.Entry = "scalar update")]);
        Assert(backend.InsertionChecks == checks && ReferenceEquals(tree.Rows.Children[0], entries[1]) && retained.Entry == "scalar update",
            "Retained-key reordering and scalar updates do not pretend to insert a new subtree.");
        tree.Rows.Reconcile([KeyedItem.Create("new", h => new MutationRow(h, "unused"))]);
        Assert(backend.InsertionChecks == checks, "Removal-only updates do not require insertion validation.");

        using var generatedHost = new Host(new Dispatcher());
        var board = new MutationBoard(generatedHost);
        var generatedBackend = new TreePreflightBackend { CheckingInsertion = (_, _) => throw new NotSupportedException("candidate policy") };
        generatedHost.Attach(generatedBackend);
        var previous = board.Rows;
        var generatedFailure = Throws<KeyedUpdateException>(() => board.Rows = [KeyedItem.Create("row", h => new MutationRow(h, "row"))]);
        Assert(!generatedFailure.ModelCommitted && ReferenceEquals(board.Rows, previous) && board.RowsView.Children.Count == 0,
            "Generated structural state rolls back a backend candidate-preflight rejection.");
    }

    private static void CandidateTreeCleanupChecks()
    {
        using var host = new Host(new Dispatcher());
        var tree = PreflightTree(host);
        var backend = new TreePreflightBackend();
        host.Attach(backend);
        LifetimeResource? badResource = null;
        PreflightDocument? candidate = null;
        var failure = Throws<KeyedUpdateException>(() => tree.Rows.Reconcile(
        [
            KeyedItem.Create("cleanup", h =>
            {
                candidate = new PreflightDocument(h);
                badResource = candidate.Lifetime.Own(new LifetimeResource(() => throw new ApplicationException("candidate cleanup")));
                return candidate;
            })
        ]));
        var failures = ((AggregateException)failure.InnerException!).Flatten().InnerExceptions;
        Assert(!failure.ModelCommitted && failures.Any(error => error is NotSupportedException) &&
            failures.Any(error => error.Message == "candidate cleanup") && badResource!.Calls == 1 &&
            candidate!.Lifetime.Token.IsCancellationRequested && host.IsAttached && tree.Rows.Children.Count == 0,
            "Preflight rejection preserves its original error and every candidate-cleanup failure without altering the live tree.");
        tree.Rows.Reconcile([KeyedItem.Create("healthy", h => new MutationRow(h, "healthy"))]);
        Assert(tree.Rows.Children.Count == 1, "Cleanup failures do not leave a leaked build scope.");
    }

    private sealed class PreflightDocument : IPortableComponent
    {
        public Element Root { get; }
        public MultilineText Document { get; }
        public ComponentLifetime Lifetime { get; }
        public PreflightDocument(Host host)
        {
            using var build = host.BeginBuild();
            var root = host.Stack(Axis.Vertical);
            Document = host.MultilineText("Candidate document");
            root.Add(Document);
            host.SetContent(root);
            build.Complete();
            Root = root;
            Lifetime = host.GetComponentLifetime(root);
        }
    }

    private static void TreePreflightInteractionChecks()
    {
        using var host = new Host(new Dispatcher());
        var tree = PreflightTree(host);
        MutationRow? row = null;
        tree.Rows.Reconcile([KeyedItem.Create("row", h => row = new MutationRow(h, "row"))]);
        var backend = new TreePreflightBackend();
        host.Attach(backend);
        var nativeInput = backend.Peers.Single(peer => ReferenceEquals(peer.Element, row!.Input));
        bool updatingRow = false;
        bool updaterFinished = false;
        int notices = 0;
        row!.Input.InteractionChanged += _ =>
        {
            Assert(!updatingRow && updaterFinished, "Interaction metadata is delivered after the complete reconciliation, not inside its updater.");
            notices++;
        };
        nativeInput.EchoInteraction = true;
        tree.Rows.Reconcile([KeyedItem.Create("row", h => new MutationRow(h, "unused"), retained =>
        {
            updatingRow = true;
            retained.Entry = "updated";
            Assert(notices == 0 && retained.Input.Interaction == new TextInteraction(true, false),
                "Native metadata is captured immediately while authored delivery remains deferred.");
            updatingRow = false;
            updaterFinished = true;
        })]);
        Assert(notices == 1 && host.IsAttached, "The queued interaction is delivered exactly once after reconciliation finishes.");
    }

    private sealed class TreePreflightBackend : IBackendTreePreflight
    {
        public List<TreePreflightPeer> Peers { get; } = [];
        public bool RejectRevealDocuments { get; set; } = true;
        public int TreeChecks { get; private set; }
        public int InsertionChecks { get; private set; }
        public int NativeMutations { get; set; }
        public bool Disposed { get; private set; }
        public Action<Element>? CheckingTree { get; set; }
        public Action<Element, Element>? CheckingInsertion { get; set; }
        public void ValidateTree(Element root)
        {
            TreeChecks++;
            CheckingTree?.Invoke(root);
            Check(root, insideReveal: false);
        }
        public void ValidateInsertion(Element parent, Element candidateRoot)
        {
            InsertionChecks++;
            CheckingInsertion?.Invoke(parent, candidateRoot);
            bool insideReveal = false;
            for (Element? ancestor = parent; ancestor is not null; ancestor = ancestor.Parent)
                insideReveal |= ancestor is Reveal;
            Check(candidateRoot, insideReveal);
        }
        private void Check(Element element, bool insideReveal)
        {
            insideReveal |= element is Reveal;
            if (RejectRevealDocuments && insideReveal && element is MultilineText)
                throw new NotSupportedException("This native reveal does not support this document provider.");
            foreach (var child in element.Children) Check(child, insideReveal);
        }
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new TreePreflightPeer(this, element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() => Disposed = true;
    }

    private sealed class TreePreflightPeer(TreePreflightBackend backend, Element element, IControlEvents events) : IMutableElementPeer, IRevealElementPeer
    {
        public Element Element { get; } = element;
        public IControlEvents Events { get; } = events;
        public int DisposeCalls { get; private set; }
        public bool EchoInteraction { get; set; }
        public bool CanSetOpen(bool open) => true;
        public void ValidateMotion(RevealMotion motion) { }
        public RevealPresentation Presentation => new(Element is Reveal { Open: true } ? 1 : 0, false);
        public void AddChild(IElementPeer child) { }
        public void Update(ElementProperty property)
        {
            backend.NativeMutations++;
            if (EchoInteraction)
                Assert(((ITextInteractionEvents)Events).InteractionChanged(new(true, false)), "Native update metadata is accepted without inline authored delivery.");
        }
        public void InsertChild(int index, IElementPeer child) => backend.NativeMutations++;
        public void RemoveChild(IElementPeer child) => backend.NativeMutations++;
        public void ValidateMove(IElementPeer child, int index) { }
        public void MoveChild(IElementPeer child, int index) => backend.NativeMutations++;
        public void Dispose() => DisposeCalls++;
    }
}
