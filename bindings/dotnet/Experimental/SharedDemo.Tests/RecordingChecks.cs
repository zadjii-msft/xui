using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private sealed record Snapshot(Func<string> Save, Action<string> Restore);

    private static void RecordingChecks()
    {
        string json = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "GalleryScenarios.json"));
        CheckApp(json, "task-board", 1, host =>
        {
            var app = new TaskBoard(host);
            Assert(app.TitleInput.AutomationId == "task-name" && app.ProgressLabel.Text == "1 of 3 complete", "Task generated refs and seed.");
            return new(() => GalleryStateCodec.Serialize(app.State), saved => app.State = GalleryStateCodec.RestoreTaskBoard(saved));
        });
        CheckApp(json, "expense-ledger", 4, host =>
        {
            var app = new ExpenseLedger(host);
            Assert(app.BudgetInput.AutomationId == "ledger-budget" && app.FoodInput.AutomationId == "ledger-food" &&
                app.TravelInput.AutomationId == "ledger-travel" && app.SuppliesInput.AutomationId == "ledger-supplies", "Ledger generated input refs.");
            return new(() => GalleryStateCodec.Serialize(app.State), saved => app.State = GalleryStateCodec.RestoreExpenseLedger(saved));
        });
        CheckApp(json, "session-planner", 5, host =>
        {
            var app = new SessionPlanner(host);
            Assert(app.StartInput.AutomationId == "planner-start" && app.FirstInput.AutomationId == "planner-first" &&
                app.SecondInput.AutomationId == "planner-second" && app.ThirdInput.AutomationId == "planner-third" &&
                app.BreakInput.AutomationId == "planner-break", "Planner generated input refs.");
            return new(() => GalleryStateCodec.Serialize(app.State), saved => app.State = GalleryStateCodec.RestoreSessionPlanner(saved));
        });
        ScenarioValidationChecks();
    }

    private static void CheckApp(string json, string id, int inputCount, Func<Host, Snapshot> create)
    {
        using var host = new Host(new Dispatcher());
        var snapshot = create(host);
        string seed = snapshot.Save();
        var backend = new Backend();
        host.Attach(backend);
        var original = backend.Peers.ToArray();
        int checks = ApplicationScenarioRunner.Run(json, id, new Driver(backend));
        Assert(checks >= 40, $"{id} corpus includes at least forty literal checks.");
        assertions += checks;
        Assert(snapshot.Save() == seed, $"{id} corpus ends at the exact screenshot seed.");
        Assert(original.SequenceEqual(backend.Peers), $"{id} fixed forms retain their peers.");
        var controls = backend.Peers.Select(p => p.Element).OfType<Control>().ToArray();
        Assert(controls.All(c => c.AutomationId.Length > 0) && controls.Select(c => c.AutomationId).Distinct().Count() == controls.Length, $"{id} IDs are explicit and unique.");
        var inputs = backend.Peers.Where(p => p.Element is TextInput).ToArray();
        Assert(inputs.Length == inputCount, $"{id} declared editor count.");
        foreach (var peer in inputs)
        {
            var input = (TextInput)peer.Element;
            Assert(input.CaptionVisible && input.Name.Length > 0 && input.Placeholder.Length > 0 && input.Help.Length > 0, $"{id} semantic captions and help.");
            Assert(input.FixedSize is null && input.PreferredSize is null, $"{id} native caption/editor height is unconstrained.");
            peer.Updates.Clear();
        }

        int changes = 0;
        foreach (var peer in inputs) ((TextInput)peer.Element).Changed += _ => changes++;
        var first = inputs[0];
        Assert(first.Events.Change("  draft \u674e  "), $"{id} native edit accepted.");
        Assert(((TextInput)first.Element).Text == "  draft \u674e  " && changes == 1, $"{id} native draft preserved exactly.");
        Assert(inputs.All(p => p.Updates.Count == 0), $"{id} typing does not echo into native editors.");
        string draft = snapshot.Save();
        Assert(first.Events.Change("  draft \u674e  ") && changes == 1 && snapshot.Save() == draft, $"{id} identical edits are silent.");
        snapshot.Restore(seed);
        Assert(changes == 1 && snapshot.Save() == seed, $"{id} programmatic restore raises no change events.");
        Assert(inputs.Skip(1).All(p => p.Updates.Count == 0), $"{id} restore doesn't rewrite unrelated inputs.");
        snapshot.Restore(draft);
        host.Detach();
        Assert(backend.Disposed && backend.Peers.All(p => p.Disposed), $"{id} detach releases all peers.");
        Assert(!first.Events.Change("stale") && !first.Events.Submit(), $"{id} detached sinks are inert.");
        Assert(snapshot.Save() == draft, $"{id} detach retains the latest draft.");
        var replacement = new Backend();
        host.Attach(replacement);
        var restoredInput = replacement.Find(((TextInput)first.Element).AutomationId);
        Assert(((TextInput)restoredInput.Element).Text == "  draft \u674e  ", $"{id} reattach starts from retained draft.");
        Assert(!first.Events.Change("old attachment"), $"{id} old sink stays invalid after reattach.");
        snapshot.Restore(seed);
        host.Dispose();
        Assert(replacement.Disposed && replacement.Peers.All(p => p.Disposed), $"{id} terminal cleanup.");
        Assert(!restoredInput.Events.Submit(), $"{id} disposed callback inert.");
        Throws<ObjectDisposedException>(() => snapshot.Restore(seed));
        Console.WriteLine($"{id}: {checks} literal fixture expectations.");
    }

    private static void ScenarioValidationChecks()
    {
        using var host = new Host(new Dispatcher());
        _ = new TaskBoard(host);
        var backend = new Backend();
        host.Attach(backend);
        var driver = new Driver(backend);
        const string valid = """
            {"version":1,"applications":[{"id":"task-board","reset":"task-reset","scenarios":[{"name":"seed","steps":[
              {"action":"click","id":"task-reset"},{"action":"expect","id":"task-progress","text":"1 of 3 complete"}
            ]}]}]}
            """;
        Assert(ApplicationScenarioRunner.Run(valid, "task-board", driver) == 1, "Minimal generic fixture.");
        Throws<InvalidOperationException>(() => ApplicationScenarioRunner.Run(valid, "missing", driver));
        Throws<InvalidOperationException>(() => ApplicationScenarioRunner.Run(valid.Replace("\"version\":1", "\"version\":2"), "task-board", driver));
        Throws<InvalidOperationException>(() => ApplicationScenarioRunner.Run(valid.Replace("\"action\":\"click\"", "\"action\":\"submit\""), "task-board", driver));
        Throws<InvalidOperationException>(() => ApplicationScenarioRunner.Run(valid.Replace("\"text\":\"1 of 3 complete\"", "\"text\":\"wrong\""), "task-board", driver));
        Throws<InvalidOperationException>(() => ApplicationScenarioRunner.Run(valid.Replace("\"text\":\"1 of 3 complete\"", "\"unknown\":true"), "task-board", driver));
        Throws<InvalidOperationException>(() => ApplicationScenarioRunner.Run(valid.Replace(",\"text\":\"1 of 3 complete\"", ""), "task-board", driver));
        Throws<InvalidOperationException>(() => ApplicationScenarioRunner.Run(valid.Replace("\"action\":\"expect\"", "\"action\":\"expect\",\"action\":\"expect\""), "task-board", driver));
        Throws<InvalidOperationException>(() => ApplicationScenarioRunner.Run("""{"version":1,"applications":[]}""", "task-board", driver));
        Throws<InvalidOperationException>(() => ApplicationScenarioRunner.Run("""
            {"version":1,"applications":[{"id":"task-board","reset":"task-reset","scenarios":[{"name":"empty","steps":[{"action":"click","id":"task-reset"}]}]}]}
            """, "task-board", driver));
    }

    private sealed class Dispatcher : IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action) => throw new NotSupportedException("These synchronous sample tests must not dispatch asynchronous work.");
    }

    private sealed class Backend : IBackend
    {
        public List<Peer> Peers { get; } = [];
        public bool Disposed { get; private set; }
        public Peer Find(string id) => Peers.Single(p => p.Element is Control c && c.AutomationId == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new Peer(element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) => Assert(Peers.Any(p => ReferenceEquals(p, root)), "Mount uses a created peer.");
        public void Dispose() => Disposed = true;
    }

    private sealed class Peer(Element element, IControlEvents events) : IElementPeer
    {
        public Element Element { get; } = element;
        public IControlEvents Events { get; } = events;
        public List<ElementProperty> Updates { get; } = [];
        public List<IElementPeer> Children { get; } = [];
        public bool Disposed { get; private set; }
        public void AddChild(IElementPeer child) => Children.Add(child);
        public void Update(ElementProperty property)
        {
            Updates.Add(property);
            if (property == ElementProperty.Text && Element is TextInput input)
                Assert(!Events.Change(input.Text), "Programmatic changes reject synchronous native echoes.");
        }
        public void Dispose() => Disposed = true;
    }

    private sealed class Driver(Backend backend) : IApplicationScenarioDriver
    {
        public void Change(string id, string value) => Assert(backend.Find(id).Events.Change(value), $"Native change accepted: {id}.");
        public void Click(string id) => Assert(backend.Find(id).Events.Click(), $"Native click accepted: {id}.");
        public void Submit(string id) => Assert(backend.Find(id).Events.Submit(), $"Native submit accepted: {id}.");
        public string Text(string id) => backend.Find(id).Element switch
        {
            TextInput input => input.Text,
            Label label => label.Text,
            Button button => button.Text,
            _ => throw new InvalidOperationException($"'{id}' is not a text control.")
        };
        public bool Enabled(string id) => ((Control)backend.Find(id).Element).Enabled;
        public bool Visible(string id) => ((Control)backend.Find(id).Element).Visible;
    }
}
