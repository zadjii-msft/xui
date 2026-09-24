using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private sealed class Dispatcher : IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action) => throw new NotSupportedException("This application has no asynchronous work.");
    }

    private sealed class Backend : IBackend
    {
        public List<Peer> Peers { get; } = [];
        public bool Disposed { get; private set; }
        public bool RejectMove { get; set; }
        public bool FailInsert { get; set; }
        public Peer Find(string id) => Peers.Single(p => !p.Disposed && p.Id == id);
        public Peer? FindVisible(string id) => Peers.SingleOrDefault(p => !p.Disposed && p.Id == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new Peer(this, element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) => Assert(Peers.Any(p => ReferenceEquals(p, root)), "Mount uses a created peer.");
        public void Dispose() => Disposed = true;
    }

    private sealed class Peer(Backend backend, Element element, IControlEvents events) : IMutableElementPeer
    {
        public Element Element { get; } = element;
        public IControlEvents Events { get; } = events;
        public string Id { get; } = element is Control control ? control.AutomationId : "";
        public List<IElementPeer> Children { get; } = [];
        public List<ElementProperty> Updates { get; } = [];
        public bool Disposed { get; private set; }
        public void AddChild(IElementPeer child) => Children.Add(child);
        public void InsertChild(int index, IElementPeer child)
        {
            if (backend.FailInsert) throw new InvalidOperationException("Injected native insertion failure.");
            Children.Insert(index, child);
        }
        public void RemoveChild(IElementPeer child) => Assert(Children.Remove(child), "Removed child was native-parented.");
        public void ValidateMove(IElementPeer child, int index)
        {
            if (backend.RejectMove) throw new NotSupportedException("Native editor is composing; retry explicitly later.");
            Assert(Children.Contains(child) && index >= 0, "Move validated against an existing native child.");
        }
        public void MoveChild(IElementPeer child, int index)
        {
            Assert(Children.Remove(child), "Moved child already exists.");
            Children.Insert(index, child);
        }
        public void Update(ElementProperty property)
        {
            Updates.Add(property);
            if (property == ElementProperty.Text && Element is TextInput input)
                Assert(!Events.Change(input.Text), "Programmatic edits reject native echoes.");
        }
        public void Dispose() => Disposed = true;
    }

    private sealed class FixedBackend : IBackend
    {
        public List<FixedPeer> Peers { get; } = [];
        public bool Disposed { get; private set; }
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new FixedPeer();
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) => throw new InvalidOperationException("Unsupported keyed tree must not mount.");
        public void Dispose() => Disposed = true;
    }
    private sealed class FixedPeer : IElementPeer
    {
        public bool Disposed { get; private set; }
        public void AddChild(IElementPeer child) { }
        public void Update(ElementProperty property) { }
        public void Dispose() => Disposed = true;
    }

    private sealed class Driver(Backend backend) : IApplicationScenarioDriver
    {
        public void Change(string id, string value) => Assert(backend.Find(id).Events.Change(value), $"Change accepted: {id}.");
        public void Click(string id) => Assert(backend.Find(id).Events.Click(), $"Click accepted: {id}.");
        public void Submit(string id) => Assert(backend.Find(id).Events.Submit(), $"Submit accepted: {id}.");
        public string Text(string id) => backend.Find(id).Element switch
        {
            TextInput input => input.Text,
            Label label => label.Text,
            Button button => button.Text,
            _ => throw new InvalidOperationException($"'{id}' has no text.")
        };
        public bool Enabled(string id) => ((Control)backend.Find(id).Element).Enabled;
        public bool Visible(string id) => backend.FindVisible(id)?.Element is Control control && control.Visible;
    }
}
