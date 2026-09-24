using System.Collections.Concurrent;
using System.Diagnostics;
using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private sealed class Dispatcher : IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        private readonly ConcurrentQueue<Action> queue = new();
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action) => queue.Enqueue(action);
        public void Drain() { while (queue.TryDequeue(out var action)) action(); }
        public void Complete(Task task)
        {
            var timer = Stopwatch.StartNew();
            while (!task.IsCompleted)
            {
                Drain();
                if (timer.Elapsed > TimeSpan.FromSeconds(10)) throw new TimeoutException("Cart operation timed out.");
                Thread.Sleep(1);
            }
            Drain();
            task.GetAwaiter().GetResult();
        }
    }
    private sealed class Backend : IBackend
    {
        public List<Peer> Peers { get; } = [];
        public bool Disposed { get; private set; }
        public bool RejectMutation { get; set; }
        public bool FailInsert { get; set; }
        public Peer Find(string id) => Peers.Single(peer => !peer.Disposed && peer.Id == id);
        public bool Exists(string id) => Peers.Any(peer => !peer.Disposed && peer.Id == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new Peer(this, element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) => Assert(Peers.Contains(root), "Root is created.");
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
        public void RemoveChild(IElementPeer child) => Assert(Children.Remove(child), "Removed peer was parented.");
        public void ValidateMutation()
        {
            if (backend.RejectMutation) throw new NotSupportedException("Active native composition.");
        }
        public void ValidateMove(IElementPeer child, int index) => Assert(Children.Contains(child), "Move targets retained peer.");
        public void MoveChild(IElementPeer child, int index) { Children.Remove(child); Children.Insert(index, child); }
        public void Update(ElementProperty property)
        {
            Updates.Add(property);
            if (Element is TextInput input && property == ElementProperty.Text)
                Assert(!Events.Change(input.Text), "Programmatic updates suppress native echoes.");
        }
        public void Dispose() => Disposed = true;
    }
    private sealed class Quotes : ICartQuoteService
    {
        public int Calls { get; private set; }
        public CancellationToken Token { get; private set; }
        public Func<EditableCartDraft, CancellationToken, Task<CartQuote>>? Produce { get; set; }
        public Task<CartQuote> QuoteAsync(EditableCartDraft draft, CancellationToken cancellationToken)
        {
            Calls++;
            Token = cancellationToken;
            return Produce is null ? Task.FromResult(CartQuote.Calculate(draft)) : Produce(draft, cancellationToken);
        }
    }
    private sealed class Harness : IDisposable
    {
        public Dispatcher Dispatcher { get; } = new();
        public Backend Backend { get; } = new();
        public Quotes Quotes { get; }
        public Host Host { get; }
        public EditableCart App { get; }
        public EditableCartController Controller => App.Controller;
        public List<Exception> Errors { get; } = [];
        public Harness(EditableCartSession? session = null, Quotes? quotes = null)
        {
            Quotes = quotes ?? new();
            Host = new(Dispatcher);
            App = EditableCart.Create(Host, Quotes, Errors.Add, session);
            Host.Attach(Backend);
        }
        public void Finish() => Dispatcher.Complete(Controller.LastOperation);
        public void Dispose() => Host.Dispose();
    }
    private sealed class Driver(Harness h) : IApplicationScenarioDriver
    {
        public void Change(string id, string value) => Assert(h.Backend.Find(id).Events.Change(value), "Native change accepted: " + id);
        public void Click(string id) { Assert(h.Backend.Find(id).Events.Click(), "Native click accepted: " + id); h.Finish(); }
        public void Submit(string id) => Assert(h.Backend.Find(id).Events.Submit(), "Native submit accepted: " + id);
        public string Text(string id) => h.Backend.Find(id).Element switch
        {
            TextInput input => input.Text,
            Label label => label.Text,
            Button button => button.Text,
            _ => throw new InvalidOperationException("Not a text control.")
        };
        public bool Enabled(string id) => ((Control)h.Backend.Find(id).Element).Enabled;
        public bool Visible(string id) => h.Backend.Exists(id) && ((Control)h.Backend.Find(id).Element).Visible;
    }
}
