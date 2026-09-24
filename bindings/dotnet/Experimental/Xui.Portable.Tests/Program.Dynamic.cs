using PortableMutation;
using Xui.Experimental.Portable;
using Stack = Xui.Experimental.Portable.Stack;

internal static partial class Program
{
    private static void DynamicCompositionChecks()
    {
        using var host = new Host(new Dispatcher());
        var board = new MutationBoard(host);
        var backend = new MutationBackend();
        host.Attach(backend);
        RunMutationCorpus(board, backend);
        Assert(board.BannerRoot.Children.OfType<Label>().Single().Text == "Scoped reusable content", "Portable Content constructs a reusable generated component in a nested scope.");
        backend.Find("add-row").Events.Click();
        backend.Find("add-row").Events.Click();
        backend.Find("add-row").Events.Click();
        var first = backend.Find("row-1-input");
        var second = backend.Find("row-2-input");
        var third = backend.Find("row-3-input");
        first.Events.Change("retained \u674e draft");
        backend.Find("row-1-increment").Events.Click();
        Assert(backend.Find("row-1-count").Element is Label { Text: "Edits: 1" }, "Generated row-local state receives authored events.");
        int peers = backend.Peers.Count;
        backend.Find("reverse-rows").Events.Click();
        Assert(ReferenceEquals(first, backend.Find("row-1-input")) && backend.Peers.Count == peers, "Keyed reverse retains all native peers.");
        Assert(((TextInput)first.Element).Text == "retained \u674e draft" && first.TextUpdates == 0, "Move does not rewrite input text or local state.");
        Assert(backend.Children(board.RowsView).SequenceEqual(board.RowsView.Children), "Native and managed keyed child orders match.");
        backend.Find("toggle-notice").Events.Click();
        var notice = backend.Find("notice");
        backend.Find("toggle-notice").Events.Click();
        Assert(notice.DisposeCount == 1 && board.NoticeView.Children.Count == 0, "Conditional removal retires its component subtree.");
        backend.Find("toggle-notice").Events.Click();
        Assert(!ReferenceEquals(notice, backend.Find("notice")), "Removed/readded conditional key is a fresh component.");
        backend.Find("remove-first").Events.Click();
        Assert(third.DisposeCount == 1 && !third.Events.Change("stale") && !third.Events.Submit(), "Removed row events are inert.");
        Assert(first.DisposeCount == 0 && second.DisposeCount == 0 && backend.Find("row-1-count").Element is Label { Text: "Edits: 1" }, "Removing another row preserves local state.");

        MutationRow? retained = null;
        int creates = 0, updates = 0;
        KeyedItem Row(string key, string text) => KeyedItem.Create(key, h =>
        {
            creates++;
            return new MutationRow(h, key);
        }, row => { retained = row; updates++; row.Entry = text; });
        board.Rows = [Row("typed", "first")];
        var typed = backend.Find("typed-input");
        var component = retained!;
        board.Rows = [Row("typed", "second")];
        Assert(creates == 1 && updates == 2 && ReferenceEquals(retained, component), "Typed update callback receives the retained row, not a reconstructed component.");
        Assert(component.Entry == "second" && typed.TextUpdates == 2, "Explicit model updates target only changed input text.");
        board.Rows = [KeyedItem.Create("typed", h => new MutationBanner(h, "replacement"))];
        Assert(typed.DisposeCount == 1 && !typed.Events.Submit(), "Changing component type under a key replaces the old subtree.");
        Throws<ObjectDisposedException>(() => _ = component.Entry);
        Throws<ObjectDisposedException>(() => component.Entry = "retired");

        board.Rows = [Row("a", "A"), Row("b", "B"), Row("c", "C")];
        var a = backend.Find("a-input");
        var b = backend.Find("b-input");
        b.Element.Parent!.Children.OfType<TextInput>().Single().Visible = false;
        board.Rows = [Row("new", "N"), Row("c", "C"), Row("b", "B"), Row("a", "A")];
        Assert(backend.Children(board.RowsView).SequenceEqual(board.RowsView.Children), "Final-index moves include hidden children and preceding insertions.");
        Assert(ReferenceEquals(a, backend.Find("a-input")) && ReferenceEquals(b, backend.Find("b-input")), "Inserted prefix never recreates existing rows.");
        for (int i = 0; i < 40; i++)
        {
            board.Rows = [Row("kept", "stable"), Row("cycle-" + i, "temporary")];
            board.Rows = [Row("kept", "stable")];
        }
        Assert(backend.Peers.Where(p => p.DisposeCount != 0).All(p => p.DisposeCount == 1), "Repeated replacement disposes each retired peer exactly once.");
        Assert(backend.Children(board.RowsView).SequenceEqual(board.RowsView.Children), "Stress leaves matching child order.");
        host.Detach();
        Assert(backend.Peers.All(p => p.DisposeCount == 1), "Detach releases remaining peers without redisposing retired peers.");
        var next = new MutationBackend();
        host.Attach(next);
        Assert(((TextInput)next.Find("kept-input").Element).Text == "stable", "Detached keyed model attaches to a fresh backend.");
        Assert(!backend.Find("kept-input").Events.Submit(), "Old attachment event sinks stay inactive.");
    }

    private static void RunMutationCorpus(MutationBoard board, MutationBackend backend)
    {
        string corpus = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Fixtures", "MutationScenarios.json"));
        var driver = new MutationDriver(board, backend);
        int checks = MutationScenarioRunner.Run(corpus, driver);
        Assert(checks > 20, "Mutation corpus covers literal text, native order, and removal.");
        assertions += checks;
        const string start = """{"version":1,"scenarios":[{"name":"invalid","steps":[{"action":"click","id":"reset-rows"},""";
        foreach (string invalid in new[]
        {
            """{"action":"expect","id":"row-count","text":"wrong"}""",
            """{"action":"expect-order","keys":["missing"]}""",
            """{"action":"expect-order","keys":[null]}""",
            """{"action":"expect-absent","id":"row-count"}""",
            """{"action":"change","id":"row-count","value":null}""",
            """{"action":"unknown","id":"row-count"}""",
            """{"action":"expect","id":"row-count","text":"Rows: 0","typo":true}""",
            """{"action":"click","id":"unknown"}"""
        })
        {
            var error = Throws<InvalidOperationException>(() => MutationScenarioRunner.Run(start + invalid + "]}]}", driver));
            Assert(error.Message.StartsWith("Mutation scenario 'invalid', step 2:", StringComparison.Ordinal) && error.InnerException is not null, "Mutation runner preserves the exact failing scenario and step.");
        }
        Throws<InvalidOperationException>(() => MutationScenarioRunner.Run("""{"version":2,"scenarios":[]}""", driver));
        Throws<InvalidOperationException>(() => MutationScenarioRunner.Run("""{"version":1,"scenarios":[]}""", driver));
        Throws<InvalidOperationException>(() => MutationScenarioRunner.Run("""{"version":1,"scenarios":[{"name":"wrong-start","steps":[{"action":"click","id":"add-row"}]}]}""", driver));
        backend.Find("reset-rows").Events.Click();
        Console.WriteLine($"Mutation corpus: {checks} literal expectations passed.");
    }

    private sealed class MutationDriver(MutationBoard board, MutationBackend backend) : IMutationScenarioDriver
    {
        public void Click(string id) => backend.Find(id).Events.Click();
        public void Change(string id, string value) => backend.Find(id).Events.Change(value);
        public string Text(string id) => backend.Find(id).Element is TextInput input ? input.Text : ((Control)backend.Find(id).Element).Name;
        public bool Exists(string id) => backend.Peers.Any(p => p.Id == id && p.DisposeCount == 0);
        public IReadOnlyList<string> Order()
        {
            var native = backend.Peers.Single(p => ReferenceEquals(p.Element, board.RowsView) && p.DisposeCount == 0);
            return native.Children.Select(row => row.Children.Single(p => p.Element is TextInput).Id[..^6]).ToArray();
        }
    }

    private static void DynamicFailureChecks()
    {
        KeyedItem Item(string key) => KeyedItem.Create(key, h => new MutationRow(h, key));
        using (var host = new Host(new Dispatcher()))
        {
            var board = new MutationBoard(host);
            var unsupported = new Backend();
            Throws<NotSupportedException>(() => host.Attach(unsupported));
            Assert(!host.IsAttached && unsupported.Disposed && unsupported.Peers.All(p => p.Disposed), "A nonmutable backend explicitly rejects even an empty keyed tree and cleans up.");
            var backend = new MutationBackend();
            host.Attach(backend);
            board.Rows = [Item("a"), Item("b"), Item("c")];
            var original = board.Rows;
            var roots = board.RowsView.Children.ToArray();
            int peers = backend.Peers.Count;
            void Rejected(KeyedItem[] next)
            {
                var error = Throws<KeyedUpdateException>(() => board.Rows = next);
                Assert(!error.ModelCommitted && host.IsAttached && ReferenceEquals(board.Rows, original), "Precommit rejection preserves published descriptor state and attachment.");
                Assert(board.RowsView.Children.SequenceEqual(roots) && backend.Peers.Count == peers && board.CountLabel.Text == "Rows: 3", "Rejected update changes no tree, count binding, or native peers.");
            }
            Rejected([Item("duplicate"), Item("duplicate")]);
            Rejected([null!]);
            Rejected(null!);
            int factories = 0;
            backend.RejectMutation = true;
            Rejected([..original, KeyedItem.Create("insert", h => { factories++; return new MutationRow(h, "insert"); })]);
            Rejected(original[1..]);
            Rejected([original[2], original[1], original[0]]);
            Assert(factories == 0, "Whole-edit preflight protects insert/remove/reorder before any candidate factory.");
            board.Rows = [.. original];
            Assert(host.IsAttached, "An unchanged-key updater snapshot does not trigger structural preflight.");
            original = board.Rows;
            backend.RejectMutation = false;
            backend.RejectMove = true;
            Rejected([KeyedItem.Create("new", h => { factories++; return new MutationRow(h, "new"); }), original[2], original[1], original[0]]);
            Assert(factories == 0, "Composition move preflight occurs before any candidate factory or native mutation.");
            backend.RejectMove = false;
            backend.ValidateCalls = 0;
            backend.RejectMoveAt = 2;
            Rejected([original[2], original[1], original[0]]);
            Assert(backend.ValidateCalls == 2 && backend.Children(board.RowsView).SequenceEqual(roots), "Every move is preflighted before any removal, insertion, or reorder.");
            backend.RejectMoveAt = 0;
            board.Rows = [original[2], original[1], original[0]];
            Assert(host.IsAttached && backend.Children(board.RowsView).SequenceEqual(board.RowsView.Children), "Explicit retry succeeds after composition block is cleared.");

            original = board.Rows;
            roots = board.RowsView.Children.ToArray();
            peers = backend.Peers.Count;
            MutationRow? rolledBack = null;
            Rejected([KeyedItem.Create("candidate", h => rolledBack = new MutationRow(h, "candidate")),
                KeyedItem.Create<MutationRow>("broken", _ => throw new ApplicationException("factory failed"))]);
            Throws<ObjectDisposedException>(() => rolledBack!.Entry = "discarded");
            Rejected([KeyedItem.Create("orphan", h =>
            {
                var row = new MutationRow(h, "orphan");
                h.Label("unowned");
                return row;
            })]);
            Rejected([KeyedItem.Create("leaked", h =>
            {
                var row = new MutationRow(h, "leaked");
                _ = h.BeginBuild();
                return row;
            })]);
            Rejected([KeyedItem.Create("live-mutation", h =>
            {
                ((TextInput)backend.Find("a-input").Element).Text = "forbidden";
                return new MutationRow(h, "live-mutation");
            })]);
            using var foreign = new Host(new Dispatcher());
            var foreignRow = new MutationRow(foreign, "foreign");
            Rejected([KeyedItem.Create("foreign", _ => foreignRow)]);
            Assert(foreignRow.Entry == "", "Foreign factory return remains owned by its original host.");
            Rejected([KeyedItem.Create<MutationRow>("null", _ => null!)]);
            Rejected([KeyedItem.Create("detach", h => { h.Detach(); return new MutationRow(h, "detach"); })]);
            Rejected([KeyedItem.Create("dispose", h => { h.Dispose(); return new MutationRow(h, "dispose"); })]);
            Task.Run(() => Throws<InvalidOperationException>(() => board.Rows = [])).GetAwaiter().GetResult();
            Assert(ReferenceEquals(board.Rows, original), "Off-thread structural state assignment is rejected before publication.");
            board.Rows = [Item("healthy")];
            Assert(host.IsAttached && backend.Find("healthy-input").DisposeCount == 0, "Factory rollback leaves no open scope and subsequent construction works.");
        }
        foreach (string failure in new[] { "create", "add", "insert", "remove", "move", "dispose", "update", "callback", "detach", "host-dispose", "reconcile" })
        {
            using var host = new Host(new Dispatcher());
            var board = new MutationBoard(host);
            var backend = new MutationBackend();
            host.Attach(backend);
            board.Rows = [Item("a"), Item("b")];
            var a = backend.Find("a-input");
            var previous = board.Rows;
            backend.Failure = failure;
            KeyedItem[] requested = failure switch
            {
                "remove" or "dispose" => [previous[1]],
                "move" => [previous[1], previous[0]],
                "update" => [KeyedItem.Create("a", h => new MutationRow(h, "a"), row => row.Entry = "update")],
                "callback" => [KeyedItem.Create("a", h => new MutationRow(h, "a"), _ => throw new ApplicationException("updater"))],
                "detach" => [KeyedItem.Create("a", h => new MutationRow(h, "a"), _ => host.Detach())],
                "host-dispose" => [KeyedItem.Create("a", h => new MutationRow(h, "a"), _ => host.Dispose())],
                "reconcile" => [KeyedItem.Create("a", h => new MutationRow(h, "a"), _ => board.RowsView.Reconcile([]))],
                _ => [previous[0], Item("new")]
            };
            var error = Throws<KeyedUpdateException>(() => board.Rows = requested);
            Assert(error.ModelCommitted && ReferenceEquals(board.Rows, requested) && !host.IsAttached, $"Postcommit {failure} failure retains new descriptors and detaches.");
            Assert(backend.Disposed && backend.Peers.All(p => p.DisposeCount == 1), $"Postcommit {failure} failure disposes every peer exactly once.");
            Assert(!a.Events.Change("late") && !a.Events.Submit(), $"Postcommit {failure} invalidates old events.");
            var recovery = new MutationBackend();
            host.Attach(recovery);
            Assert(board.RowsView.Children.Count == requested.Length && recovery.Children(board.RowsView).SequenceEqual(board.RowsView.Children), $"Postcommit {failure} model can attach again without rerunning failed updater.");
            board.Rows = previous;
            Assert(board.RowsView.Children.Select(row => ((TextInput)row.Children[0]).AutomationId).SequenceEqual(["a-input", "b-input"]) &&
                recovery.Children(board.RowsView).SequenceEqual(board.RowsView.Children), $"Postcommit {failure} can return to the previously cached descriptor array.");
        }
        using (var host = new Host(new Dispatcher()))
        {
            var board = new MutationBoard(host);
            var initialEmpty = board.Rows;
            var backend = new MutationBackend { Failure = "insert" };
            host.Attach(backend);
            var error = Throws<KeyedUpdateException>(() => board.Rows = [Item("failed-native")]);
            Assert(error.ModelCommitted && !host.IsAttached && board.RowsView.Children.Count == 1, "Native insertion failure retains its committed child before recovery.");
            board.Rows = initialEmpty;
            Assert(board.RowsView.Children.Count == 0, "Returning to the original empty descriptor array removes a postcommit-failed subtree.");
            var recovery = new MutationBackend();
            host.Attach(recovery);
            Assert(!recovery.Peers.Any(peer => peer.Id == "failed-native-input"), "Reattachment after clearing cannot resurrect the failed native subtree.");
        }
    }

    private static void DynamicScopeChecks()
    {
        Throws<ArgumentException>(() => KeyedItem.Create("", h => new MutationRow(h, "empty")));
        Throws<ArgumentException>(() => KeyedItem.Create("bad\0key", h => new MutationRow(h, "nul")));
        Throws<ArgumentNullException>(() => KeyedItem.Create<MutationRow>("key", null!));
        using var host = new Host(new Dispatcher());
        MutationRow nested;
        using (var outer = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            nested = new MutationRow(host, "nested");
            root.Add(nested.Root);
            using (var abandoned = host.BeginBuild())
            {
                _ = host.TextInput("abandoned");
            }
            host.SetContent(root);
            outer.Complete();
        }
        Assert(host.Root!.Children.Count == 1 && nested.Entry == "", "Abandoned nested construction leaves the parent tree intact.");
        Throws<InvalidOperationException>(() => new MutationRow(host, "second-root"));
        host.Dispose();
        Throws<ObjectDisposedException>(() => _ = nested.Entry);
        using var other = new Host(new Dispatcher());
        using (var outer = other.BeginBuild())
        {
            using (var inner = other.BeginBuild())
                Throws<InvalidOperationException>(() => outer.Dispose());
            var root = other.Stack(Axis.Vertical);
            other.SetContent(root);
            outer.Complete();
        }
        Assert(other.Root is Stack, "Out-of-order scope close rejects without corrupting the outer scope.");
        using var nestedHost = new Host(new Dispatcher());
        var board = new MutationBoard(nestedHost);
        board.Rows = [KeyedItem.Create("nested", h => new MutationBoard(h))];
        var backend = new MutationBackend();
        nestedHost.Attach(backend);
        Assert(board.RowsView.Children.Single() is Stack, "Nested keyed component construction is supported.");
        board.Rows = [];
        Assert(backend.Peers.Where(p => p.Element is KeyedStack && p.DisposeCount != 0).All(p => p.DisposeCount == 1), "Removing nested collections cleans their descendants.");
    }

    private static void CancellableDispatchChecks()
    {
        var dispatcher = new CancelDispatcher();
        using var host = new Host(dispatcher);
        int calls = 0;
        var pending = host.DispatchAsync(() => calls++);
        dispatcher.Cancel!(new ObjectDisposedException("dispatcher"));
        Throws<ObjectDisposedException>(() => pending.GetAwaiter().GetResult());
        dispatcher.Action!();
        Assert(calls == 0, "A canceled accepted dispatch cannot run its action later.");
        var success = host.DispatchAsync(() => calls++);
        dispatcher.Action!();
        dispatcher.Cancel!(new ApplicationException("late cancel"));
        success.GetAwaiter().GetResult();
        Assert(calls == 1, "Completed dispatch ignores late cancellation.");
    }

    private sealed class CancelDispatcher : ICancellableUiDispatcher
    {
        public Action? Action;
        public Action<Exception>? Cancel;
        public bool CheckAccess() => true;
        public void Post(Action action) => throw new InvalidOperationException("Optional overload expected.");
        public void Post(Action action, Action<Exception> canceled) { Action = action; Cancel = canceled; }
    }

    private sealed class MutationBackend : IBackend
    {
        public List<MutationPeer> Peers { get; } = [];
        public string Failure { get; set; } = "";
        public bool RejectMove { get; set; }
        public bool RejectMutation { get; set; }
        public int RejectMoveAt { get; set; }
        public int ValidateCalls { get; set; }
        public bool Disposed { get; private set; }
        public Action? BeforeUnmount { get; set; }
        public MutationPeer Find(string id) => Peers.Last(p => p.Id == id);
        public IEnumerable<Element> Children(Element parent) => Peers.Single(p => ReferenceEquals(p.Element, parent) && p.DisposeCount == 0).Children.Select(p => p.Element);
        public void Check(string operation)
        {
            if (Failure == operation) throw new ApplicationException(operation);
        }
        public IElementPeer Create(Element element, IControlEvents events)
        {
            Check("create");
            var peer = new MutationPeer(this, element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() { Disposed = true; BeforeUnmount?.Invoke(); }
    }

    private sealed class MutationPeer(MutationBackend backend, Element element, IControlEvents events) : IMutationPreflightPeer
    {
        public Element Element { get; } = element;
        public string Id { get; } = element is Control control ? control.AutomationId : "";
        public IControlEvents Events { get; } = events;
        public List<MutationPeer> Children { get; } = [];
        public int DisposeCount { get; private set; }
        public int TextUpdates { get; private set; }
        public void AddChild(IElementPeer child) { backend.Check("add"); Children.Add((MutationPeer)child); }
        public void Update(ElementProperty property)
        {
            backend.Check("update");
            if (Element is TextInput input && property == ElementProperty.Text)
            {
                TextUpdates++;
                Assert(!Events.Change(input.Text), "Native setter echoes are suppressed during keyed updates.");
            }
        }
        public void InsertChild(int index, IElementPeer child)
        {
            backend.Check("insert");
            Children.Insert(index, (MutationPeer)child);
        }
        public void RemoveChild(IElementPeer child)
        {
            var peer = (MutationPeer)child;
            void CheckInactive(MutationPeer current)
            {
                Assert(!current.Events.Click(), "Every removed event sink is inactive before native unmount.");
                foreach (var nested in current.Children) CheckInactive(nested);
            }
            CheckInactive(peer);
            backend.BeforeUnmount?.Invoke();
            backend.Check("remove");
            Assert(Children.Remove(peer), "Native removal targets a direct child.");
        }
        public void ValidateMove(IElementPeer child, int index)
        {
            Assert(index >= 0 && Children.Contains((MutationPeer)child), "Move validation uses current native membership and nonnegative final index.");
            backend.ValidateCalls++;
            if (backend.RejectMove || backend.ValidateCalls == backend.RejectMoveAt) throw new InvalidOperationException("Composition is active; retry later.");
        }
        public void ValidateMutation()
        {
            if (backend.RejectMutation) throw new InvalidOperationException("Native scope composition is active; retry later.");
        }
        public void MoveChild(IElementPeer child, int index)
        {
            backend.Check("move");
            Assert(Children.Remove((MutationPeer)child), "Move retains a current peer.");
            Children.Insert(index, (MutationPeer)child);
        }
        public void Dispose()
        {
            DisposeCount++;
            backend.Check("dispose");
        }
    }
}
