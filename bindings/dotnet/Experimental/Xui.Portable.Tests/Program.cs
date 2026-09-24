using System.Collections.Concurrent;
using PortableDemo;
using Xui.Experimental.Portable;
using Stack = Xui.Experimental.Portable.Stack;

internal static partial class Program
{
    private static int assertions;
    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
        assertions++;
    }
    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T error) { assertions++; return error; }
        throw new Exception($"Expected {typeof(T).Name}.");
    }
    private static void Main()
    {
        DemoRoundtrip();
        OwnershipAndValidation();
        DispatchAndLifetime();
        BackendFailures();
        OrderModelChecks();
        OrderScenarioChecks();
        OrderRetainedStateChecks();
        OrderRunnerChecks();
        DynamicCompositionChecks();
        DynamicFailureChecks();
        DynamicScopeChecks();
        CancellableDispatchChecks();
        PrimitiveControlChecks();
        SettingsScenarioChecks();
        ComponentLifetimeChecks();
        AxisLayoutChecks();
        GridLayoutChecks();
        ViewportLeaseChecks();
        TextInteractionChecks();
        PresentationChecks();
        PresentationRuntimeChecks();
        FormsChecks();
        ChoiceRangeChecks();
        RetainedPageChecks();
        ImageElementChecks();
        MessageDialogChecks();
        BackendTreePreflightChecks();
        Console.WriteLine($"Portable runtime and generated demo: {assertions} assertions passed.");
    }

    private static void DemoRoundtrip()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var demo = new Greeting(host);
        var backend = new Backend();
        Assert(demo.Root.SpacingValue == 12 && demo.Root.PaddingValue == 16, "Generated layout.");
        Assert(demo.Input.Name == "Your name" && demo.Input.Placeholder == "Ada", "Input identity.");
        Assert(demo.Input.PreferredSize is null && demo.Input.FixedSize is null,
            "The sample input uses its native caption and editor height.");
        Assert(demo.Root.Children[1].Flex == 1, "Generated flex.");
        host.Attach(backend);
        int count = backend.Peers.Count;
        Assert(count == 11 && backend.Mounted is not null, "Every generated element has a peer.");
        var input = backend.Find("name");
        var increment = backend.Find("increment");
        var reset = backend.Find("reset");
        Assert(input.Events.Change("Ada"), "Input event accepted.");
        Assert(demo.Entry == "Ada" && demo.Input.Text == "Ada", "User edit changes model and authored state.");
        Assert(input.Updates.Count == 0, "User edit is not written back into the native input.");
        Assert(input.Events.Submit() && demo.Message == "Hello, Ada!", "Submit calls real authored C#.");
        Assert(backend.Find("greeting").Updates.SequenceEqual([ElementProperty.Name]), "Targeted label update.");
        increment.Events.Click();
        Assert(demo.Count == 1 && demo.CountLabel.Text == "Count: 1", "Click updates state and binding.");
        demo.Count = 10;
        Assert(!demo.IncrementButton.Enabled && !increment.Events.Click(), "Disabled events are rejected.");
        Assert(demo.Count == 10, "Disabled click did not run.");
        demo.Entry = "Grace";
        Assert(input.Updates.SequenceEqual([ElementProperty.Text]), "Programmatic input update is silent.");
        Assert(demo.Message == "Hello, Ada!", "Setter did not submit.");
        backend.Find("submit").Events.Click();
        Assert(demo.Message == "Hello, Grace!", "Button and input share authored submit method.");
        reset.Events.Click();
        Assert(demo.Count == 0 && demo.Entry == "" && demo.IncrementButton.Enabled, "Reset state.");
        Assert(ReferenceEquals(input, backend.Find("name")) && backend.Peers.Count == count, "No tree rebuild.");
        Assert(input.SuppressedEchoes == input.Updates.Count, "Synchronous setter echoes are rejected.");
        Throws<InvalidOperationException>(() => input.Events.Click());
        Throws<ArgumentNullException>(() => input.Events.Change(null!));
        Throws<ArgumentException>(() => input.Events.Change("bad\0text"));
        Assert(demo.Input.Text == "", "Invalid input preserves model.");
        demo.Input.Visible = false;
        Assert(!input.Events.Change("hidden") && !input.Events.Submit(), "Invisible input rejects events.");
        demo.Input.Visible = true;
        var scroll = (ScrollView)demo.Root.Children[1];
        scroll.Enabled = false;
        Assert(!increment.Events.Click(), "Disabled ancestor rejects descendant events.");
        scroll.Enabled = true;
        host.Detach();
        Assert(!host.IsAttached && backend.Disposed && backend.Peers.All(p => p.Disposed), "Detach releases every peer.");
        Assert(!input.Events.Change("stale") && !increment.Events.Click(), "Detached event sinks are stale.");
        demo.Entry = "Offline";
        var second = new Backend();
        host.Attach(second);
        Assert(((TextInput)second.Find("name").Element).Text == "Offline", "Reattach reads retained state.");
        Assert(!input.Events.Submit(), "Old sinks stay invalid after reattach.");
        Throws<InvalidOperationException>(() => new Greeting(host));
        host.Dispose();
        Assert(second.Disposed && second.Peers.All(p => p.Disposed), "Host owns attachment lifetime.");
        Assert(!second.Find("name").Events.Submit(), "Events are inert after disposal.");
        Throws<ObjectDisposedException>(() => demo.Count++);
        Throws<ObjectDisposedException>(() => demo.Input.Text = "disposed");
        host.Dispose();
    }

    private static void OwnershipAndValidation()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        Throws<InvalidOperationException>(() => host.Label("outside construction"));
        TextInput abandoned;
        using (host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            abandoned = host.TextInput("abandoned");
            root.Add(abandoned);
            Throws<InvalidOperationException>(() => root.Add(abandoned));
            Throws<InvalidOperationException>(() => root.Add(root));
            var inner = host.Stack(Axis.Horizontal);
            root.Add(inner);
            Throws<InvalidOperationException>(() => inner.Add(root));
            Throws<ArgumentOutOfRangeException>(() => root.Spacing(float.NaN));
            Throws<ArgumentOutOfRangeException>(() => root.Padding(-1));
            Throws<ArgumentOutOfRangeException>(() => ElementExtensions.FixedSize(root, float.PositiveInfinity, 1));
            Throws<ArgumentException>(() => abandoned.Text = "a\0b");
            Throws<ArgumentNullException>(() => abandoned.Name = null!);
            using var other = new Host(dispatcher);
            using var otherBuild = other.BeginBuild();
            var foreign = other.Label("foreign");
            Throws<InvalidOperationException>(() => root.Add(foreign));
            var orphan = host.Button("orphan");
            Throws<ArgumentOutOfRangeException>(() => root.Add(orphan, -1));
            Throws<InvalidOperationException>(() => host.SetContent(root));
            root.Add(orphan);
            host.SetContent(root);
            Throws<InvalidOperationException>(() => root.Add(host.Root!));
        }
        Assert(host.Root is null, "Failed construction rolls back all model elements.");
        Throws<ObjectDisposedException>(() => abandoned.Text = "released");
        var demo = new Greeting(host);
        Throws<InvalidOperationException>(() => demo.Root.Add(demo.Input));
        var backend = new Backend();
        host.Attach(backend);
        ElementExtensions.FixedSize(demo.Input, 100, 40);
        ElementExtensions.PreferredSize(demo.Input, 80, 30);
        demo.Input.SetCaptionVisible(false);
        demo.Input.SetPlaceholder("Name");
        demo.Input.AutomationId = "renamed";
        demo.Input.Help = "Enter a name";
        demo.Root.Spacing(20);
        demo.Root.Padding(4);
        Assert(demo.Input.FixedSize == new Size(100, 40) && !demo.Input.CaptionVisible, "Typed retained presentation state.");
        Assert(backend.Peers[0].Updates.SequenceEqual([ElementProperty.Spacing, ElementProperty.Padding]), "Layout updates are targeted.");
        int updates = backend.Peers.Sum(p => p.Updates.Count);
        demo.Root.Padding(4);
        Assert(backend.Peers.Sum(p => p.Updates.Count) == updates, "Equal values are silent.");
        demo.Input.Submitted += () => throw new ApplicationException("authored error");
        Throws<ApplicationException>(() => backend.Find("renamed").Events.Submit());
    }

    private static void DispatchAndLifetime()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var demo = new Greeting(host);
        var backend = new Backend();
        host.Attach(backend);
        Task.Run(() =>
        {
            Throws<InvalidOperationException>(() => demo.Count++);
            Throws<InvalidOperationException>(() => backend.Find("increment").Events.Click());
            Throws<InvalidOperationException>(() => host.Dispose());
        }).GetAwaiter().GetResult();
        Task? posted = null;
        Task.Run(() => { posted = host.DispatchAsync(() => demo.Count = 3); }).GetAwaiter().GetResult();
        Assert(!posted!.IsCompleted && demo.Count == 0, "Worker dispatch queues on the UI dispatcher.");
        dispatcher.Drain();
        posted.GetAwaiter().GetResult();
        Assert(demo.Count == 3, "Dispatched state mutation.");
        var fault = host.DispatchAsync(() => throw new ApplicationException("callback"));
        dispatcher.Drain();
        Throws<ApplicationException>(() => fault.GetAwaiter().GetResult());
        var pending = host.DispatchAsync(() => demo.Count = 8);
        host.Dispose();
        dispatcher.Drain();
        Throws<ObjectDisposedException>(() => pending.GetAwaiter().GetResult());
        dispatcher.Reject = true;
        Throws<InvalidOperationException>(() => host.DispatchAsync(() => { }).GetAwaiter().GetResult());
    }

    private static void BackendFailures()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var demo = new Greeting(host);
        var creation = new Backend { FailCreateAt = 4 };
        Throws<ApplicationException>(() => host.Attach(creation));
        Assert(!host.IsAttached && creation.Disposed && creation.Peers.All(p => p.Disposed), "Partial attachment cleanup.");
        var mount = new Backend { FailMount = true };
        Throws<ApplicationException>(() => host.Attach(mount));
        Assert(mount.Peers.All(p => p.Disposed), "Mount failure releases peers.");
        var reentrant = new Backend { Creating = () => demo.Count++ };
        Throws<InvalidOperationException>(() => host.Attach(reentrant));
        Assert(demo.Count == 0, "Backend callbacks cannot mutate generated state.");
        var backend = new Backend();
        host.Attach(backend);
        backend.Find("name").FailUpdate = true;
        Throws<ApplicationException>(() => demo.Entry = "retained after failure");
        Assert(!host.IsAttached && demo.Input.Text == "retained after failure", "Failed update detaches backend and retains model.");
        Assert(backend.Peers.All(p => p.Disposed), "Update failure tears down all peers.");
        var cleanup = new Backend { FailDispose = true, PeersFailDispose = true };
        host.Attach(cleanup);
        var error = Throws<AggregateException>(() => host.Detach());
        Assert(error.InnerExceptions.Count == cleanup.Peers.Count + 1, "Cleanup errors are all surfaced.");
        Assert(cleanup.Peers.All(p => p.Disposed) && !host.IsAttached, "Cleanup continues after errors.");
        var final = new Backend { FailDispose = true };
        host.Attach(final);
        Throws<AggregateException>(() => host.Dispose());
        Throws<ObjectDisposedException>(() => _ = demo.Entry);
        Assert(final.Peers.All(p => p.Disposed), "Failed disposal remains terminal.");
    }

    private sealed class Dispatcher : IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        private readonly ConcurrentQueue<Action> pending = new();
        public bool Reject { get; set; }
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action)
        {
            if (Reject) throw new InvalidOperationException("Dispatcher is unavailable.");
            pending.Enqueue(action);
        }
        public void Drain() { while (pending.TryDequeue(out var action)) action(); }
    }

    private sealed class Backend : IBackend
    {
        public List<Peer> Peers { get; } = [];
        public IElementPeer? Mounted { get; private set; }
        public bool Disposed { get; private set; }
        public int FailCreateAt { get; init; } = -1;
        public bool FailMount { get; init; }
        public bool FailDispose { get; init; }
        public bool PeersFailDispose { get; init; }
        public Action? Creating { get; init; }
        public Peer Find(string id) => Peers.Single(p => p.Id == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            Creating?.Invoke();
            if (Peers.Count == FailCreateAt) throw new ApplicationException("create");
            var peer = new Peer(element, events) { FailDispose = PeersFailDispose };
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root)
        {
            if (FailMount) throw new ApplicationException("mount");
            Mounted = root;
        }
        public void Dispose()
        {
            Disposed = true;
            Mounted = null;
            if (FailDispose) throw new ApplicationException("backend dispose");
        }
    }

    private sealed class Peer(Element element, IControlEvents events) : IElementPeer
    {
        public Element Element { get; } = element;
        public IControlEvents Events { get; } = events;
        public string Id { get; private set; } = element is Control control ? control.AutomationId : "";
        public List<ElementProperty> Updates { get; } = [];
        public List<IElementPeer> Children { get; } = [];
        public bool Disposed { get; private set; }
        public bool FailUpdate { get; set; }
        public bool FailDispose { get; init; }
        public int SuppressedEchoes { get; private set; }
        public void AddChild(IElementPeer child) => Children.Add(child);
        public void Update(ElementProperty property)
        {
            if (FailUpdate) throw new ApplicationException("update");
            Updates.Add(property);
            if (Element is Control control) Id = control.AutomationId;
            if (Element is TextInput input && property == ElementProperty.Text && !Events.Change(input.Text))
                SuppressedEchoes++;
        }
        public void Dispose()
        {
            Disposed = true;
            if (FailDispose) throw new ApplicationException("peer dispose");
        }
    }
}
