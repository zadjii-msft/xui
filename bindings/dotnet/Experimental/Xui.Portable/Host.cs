namespace Xui.Experimental.Portable;

public sealed class Host : IDisposable
{
    private readonly IUiDispatcher dispatcher;
    private readonly List<Element> elements = [];
    private BuildScope? building;
    private Element? root;
    private Attachment? attachment;
    private bool disposed;
    private bool transitioning;
    private int updating;

    public Host(IUiDispatcher dispatcher)
    {
        ArgumentNullException.ThrowIfNull(dispatcher);
        this.dispatcher = dispatcher;
        VerifyAccess();
    }

    public Element? Root { get { VerifyAccess(); return root; } }
    public bool IsAttached { get { VerifyAccess(); return attachment is not null; } }

    private void VerifyThread()
    {
        if (!dispatcher.CheckAccess()) throw new InvalidOperationException("This operation requires the host UI thread.");
    }

    public void VerifyAccess()
    {
        VerifyThread();
        ObjectDisposedException.ThrowIf(disposed, this);
    }

    public void VerifyMutation()
    {
        VerifyAccess();
        if (transitioning || updating != 0) throw new InvalidOperationException("A backend callback cannot mutate the tree.");
    }

    internal void VerifyBuilding()
    {
        VerifyMutation();
        if (building is null || root is not null) throw new InvalidOperationException("The tree structure is fixed after construction.");
    }

    public Task DispatchAsync(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void Run()
        {
            try { VerifyAccess(); action(); completion.SetResult(); }
            catch (Exception error) { completion.SetException(error); }
        }
        try { dispatcher.Post(Run); }
        catch (Exception error) { completion.TrySetException(error); }
        return completion.Task;
    }

    public BuildScope BeginBuild()
    {
        VerifyMutation();
        if (building is not null || elements.Count != 0 || root is not null)
            throw new InvalidOperationException("A host owns exactly one component tree.");
        return building = new BuildScope(this);
    }

    private T Create<T>(Func<T> create) where T : Element
    {
        VerifyBuilding();
        T element = create();
        elements.Add(element);
        return element;
    }

    public Stack Stack(Axis axis)
    {
        if (!Enum.IsDefined(axis)) throw new ArgumentOutOfRangeException(nameof(axis));
        return Create(() => new Stack(this, axis));
    }
    public Label Label(string text) => Create(() => new Label(this, text));
    public Button Button(string text) => Create(() => new Button(this, text));
    public TextInput TextInput(string name) => Create(() => new TextInput(this, name));
    public ScrollView ScrollView(Element content, string name) => Create(() => new ScrollView(this, content, name));

    public void SetContent(Stack content)
    {
        VerifyBuilding();
        ArgumentNullException.ThrowIfNull(content);
        if (content.Owner != this || content.Parent is not null)
            throw new InvalidOperationException("The root must be an unparented stack from this host.");
        var reached = new HashSet<Element>();
        void Visit(Element node) { reached.Add(node); foreach (var child in node.Children) Visit(child); }
        Visit(content);
        if (reached.Count != elements.Count) throw new InvalidOperationException("Every element must belong to the root tree.");
        root = content;
    }

    public void Attach(IBackend backend)
    {
        VerifyMutation();
        ArgumentNullException.ThrowIfNull(backend);
        if (attachment is not null || building is not null || root is null)
            throw new InvalidOperationException("Attach requires a completed tree without an attached backend.");
        var next = new Attachment(backend);
        transitioning = true;
        try
        {
            IElementPeer Visit(Element element)
            {
                var peer = backend.Create(element, new Events(this, next, element))
                    ?? throw new InvalidOperationException("The backend returned a null peer.");
                if (next.Peers.Values.Any(existing => ReferenceEquals(existing, peer)))
                    throw new InvalidOperationException("The backend reused an element peer.");
                next.Peers.Add(element, peer);
                foreach (var child in element.Children) peer.AddChild(Visit(child));
                return peer;
            }
            backend.Mount(Visit(root));
            attachment = next;
        }
        catch (Exception error)
        {
            var failures = new List<Exception> { error };
            ReleaseAttachment(next, failures);
            if (failures.Count > 1) throw new AggregateException(failures);
            throw;
        }
        finally { transitioning = false; }
    }

    internal void Update(Element element, ElementProperty property)
    {
        if (attachment is not { } current) return;
        updating++;
        try { current.Peers[element].Update(property); }
        catch (Exception error)
        {
            attachment = null;
            var failures = new List<Exception> { error };
            transitioning = true;
            try { ReleaseAttachment(current, failures); }
            finally { transitioning = false; }
            if (failures.Count > 1) throw new AggregateException(failures);
            throw;
        }
        finally { updating--; }
    }

    private static void ReleaseAttachment(Attachment current, List<Exception> failures)
    {
        try { current.Backend.Dispose(); }
        catch (Exception error) { failures.Add(error); }
        foreach (var peer in current.Peers.Values.Reverse())
        {
            try { peer.Dispose(); }
            catch (Exception error) { failures.Add(error); }
        }
        current.Peers.Clear();
    }

    public void Detach()
    {
        VerifyMutation();
        if (attachment is not { } current) return;
        attachment = null;
        transitioning = true;
        var failures = new List<Exception>();
        try { ReleaseAttachment(current, failures); }
        finally { transitioning = false; }
        if (failures.Count != 0) throw new AggregateException(failures);
    }

    public void Dispose()
    {
        VerifyThread();
        if (disposed) return;
        VerifyMutation();
        var failures = new List<Exception>();
        try { Detach(); }
        catch (Exception error) { failures.Add(error); }
        foreach (var element in elements) element.Release();
        elements.Clear();
        root = null;
        building = null;
        disposed = true;
        if (failures.Count != 0) throw new AggregateException(failures);
    }

    public sealed class BuildScope : IDisposable
    {
        private readonly Host host;
        private bool complete;
        internal BuildScope(Host host) { this.host = host; }
        public void Complete()
        {
            host.VerifyMutation();
            if (complete || host.building != this || host.root is null)
                throw new InvalidOperationException("Complete requires this scope's root tree.");
            host.building = null;
            complete = true;
        }
        public void Dispose()
        {
            host.VerifyThread();
            if (complete || host.disposed) return;
            foreach (var element in host.elements) element.Release();
            host.elements.Clear();
            host.root = null;
            host.building = null;
            complete = true;
        }
    }

    private sealed class Attachment(IBackend backend)
    {
        internal IBackend Backend { get; } = backend;
        internal Dictionary<Element, IElementPeer> Peers { get; } = [];
    }

    private sealed class Events(Host host, Attachment attachment, Element element) : IControlEvents
    {
        private bool Active()
        {
            host.VerifyThread();
            return !host.disposed && host.attachment == attachment && !host.transitioning &&
                host.updating == 0 && element.AcceptsInput();
        }
        public bool Click()
        {
            if (!Active()) return false;
            if (element is not Button button) throw new InvalidOperationException("Only Button accepts click events.");
            button.RaiseClick();
            return true;
        }
        public bool Change(string text)
        {
            if (!Active()) return false;
            if (element is not TextInput input) throw new InvalidOperationException("Only TextInput accepts change events.");
            input.RaiseChange(text);
            return true;
        }
        public bool Submit()
        {
            if (!Active()) return false;
            if (element is not TextInput input) throw new InvalidOperationException("Only TextInput accepts submit events.");
            input.RaiseSubmit();
            return true;
        }
    }
}
