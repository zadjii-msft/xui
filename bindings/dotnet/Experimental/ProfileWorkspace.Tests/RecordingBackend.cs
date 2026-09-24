using System.Collections.Concurrent;
using System.Diagnostics;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private sealed class Dispatcher : IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        private readonly ConcurrentQueue<Action> pending = new();
        public bool RejectPost { get; set; }
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action)
        {
            if (RejectPost) throw new InvalidOperationException("Dispatcher is unavailable.");
            pending.Enqueue(action);
        }
        public void Drain()
        {
            while (pending.TryDequeue(out var action)) action();
        }
        public void Until(Func<bool> complete)
        {
            var timeout = Stopwatch.StartNew();
            while (!complete())
            {
                Drain();
                if (timeout.Elapsed > TimeSpan.FromSeconds(10))
                    throw new TimeoutException("Profile operation did not reach its expected checkpoint.");
                Thread.Sleep(1);
            }
            Drain();
        }
        public void Complete(Task task)
        {
            Until(() => task.IsCompleted);
            task.GetAwaiter().GetResult();
        }
    }

    private sealed class Backend : IBackend
    {
        public List<Peer> Peers { get; } = [];
        public bool Disposed { get; private set; }
        public bool RejectMutation { get; set; }
        public bool FailInsert { get; set; }
        public string? FailUpdateId { get; set; }
        public Peer Find(string id) => Peers.Single(p => !p.Disposed && p.Id == id);
        public bool Contains(string id) => Peers.Any(p => !p.Disposed && p.Id == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new Peer(this, element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) => Assert(Peers.Any(p => ReferenceEquals(p, root)), "Backend mounts a created peer.");
        public void Dispose() => Disposed = true;
    }

    private sealed class Peer(Backend backend, Element element, IControlEvents events) : IMutationPreflightPeer
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
            if (backend.FailInsert) throw new InvalidOperationException("Native insertion failed.");
            Children.Insert(index, child);
        }
        public void ValidateMutation()
        {
            if (backend.RejectMutation) throw new NotSupportedException("Native composition is active.");
        }
        public void RemoveChild(IElementPeer child) => Assert(Children.Remove(child), "Removed native child exists.");
        public void ValidateMove(IElementPeer child, int index) => Assert(Children.Contains(child), "Move references a live child.");
        public void MoveChild(IElementPeer child, int index)
        {
            Assert(Children.Remove(child), "Moved native child exists.");
            Children.Insert(index, child);
        }
        public void Update(ElementProperty property)
        {
            if (backend.FailUpdateId == Id) throw new InvalidOperationException("Native property update failed.");
            Updates.Add(property);
            if (property == ElementProperty.Text && Element is TextInput input)
                Assert(!Events.Change(input.Text), "Programmatic text does not echo as native input.");
        }
        public void Dispose() => Disposed = true;
    }
}
