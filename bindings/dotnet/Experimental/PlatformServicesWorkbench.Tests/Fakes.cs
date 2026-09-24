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
        public bool RejectPost { get; set; }
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action)
        {
            if (RejectPost) throw new InvalidOperationException("Dispatcher unavailable.");
            queue.Enqueue(action);
        }
        public void Drain() { while (queue.TryDequeue(out var action)) action(); }
        public void Finish(Task task)
        {
            var timeout = Stopwatch.StartNew();
            while (!task.IsCompleted)
            {
                Drain();
                if (timeout.Elapsed > TimeSpan.FromSeconds(10)) throw new TimeoutException("Fake service did not finish.");
                Thread.Sleep(1);
            }
            Drain();
            task.GetAwaiter().GetResult();
        }
    }

    private sealed class Gesture
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        public bool Active { get; private set; }
        public void Run(Action action)
        {
            Active = true;
            try { action(); } finally { Active = false; }
        }
        public void VerifyStart() => Assert(Active && Environment.CurrentManagedThreadId == thread,
            "Provider starts synchronously on the original UI gesture thread.");
    }

    private sealed class Services(Gesture gesture) : IPlatformServices, IDisposable
    {
        public CapabilityAvailability Clipboard { get; set; } = CapabilityAvailability.RequiresUserGesture;
        public CapabilityAvailability Uri { get; set; } = CapabilityAvailability.RequiresUserGesture;
        public int Reads { get; private set; }
        public int Writes { get; private set; }
        public int Launches { get; private set; }
        public bool Disposed { get; private set; }
        public string LastWrite { get; private set; } = "";
        public Uri? LastUri { get; private set; }
        public CancellationToken Token { get; private set; }
        public Func<CancellationToken, Task<OperationResult<string>>> Read { get; set; } =
            _ => Task.FromResult(OperationResult<string>.Completed("test clipboard"));
        public Func<CancellationToken, Task<OperationResult<bool>>> Write { get; set; } =
            _ => Task.FromResult(OperationResult<bool>.Completed(true));
        public Func<CancellationToken, Task<OperationResult<bool>>> Launch { get; set; } =
            _ => Task.FromResult(OperationResult<bool>.Completed(true));
        public CapabilityAvailability GetAvailability(ServiceCapability capability) => capability switch
        {
            ServiceCapability.Clipboard => Clipboard,
            ServiceCapability.OpenUri => Uri,
            _ => CapabilityAvailability.Unsupported
        };
        public Task<OperationResult<string>> ReadClipboardAsync(CancellationToken cancellationToken = default)
        {
            gesture.VerifyStart(); Reads++; Token = cancellationToken; return Read(cancellationToken);
        }
        public Task<OperationResult<bool>> WriteClipboardAsync(string text, CancellationToken cancellationToken = default)
        {
            gesture.VerifyStart(); Writes++; LastWrite = text; Token = cancellationToken; return Write(cancellationToken);
        }
        public Task<OperationResult<bool>> OpenUriAsync(Uri uri, CancellationToken cancellationToken = default)
        {
            gesture.VerifyStart(); Launches++; LastUri = uri; Token = cancellationToken; return Launch(cancellationToken);
        }
        public void Dispose() => Disposed = true;
    }

    private sealed class Picker(Gesture gesture) : IFilePicker
    {
        public CapabilityAvailability Availability { get; set; } = CapabilityAvailability.RequiresUserGesture;
        public int Opens { get; private set; }
        public int Disposals { get; private set; }
        public CancellationToken Token { get; private set; }
        public Func<FileSelectionOptions, CancellationToken, Task<OperationResult<PickedFile>>> Open { get; set; } =
            (options, _) => Task.FromResult(OperationResult<PickedFile>.Completed(
                new PickedFile("example.txt", new TrackingStream(new byte[12]), options)));
        public CapabilityAvailability GetAvailability(ServiceCapability capability) =>
            capability == ServiceCapability.OpenFile ? Availability : CapabilityAvailability.Unsupported;
        public Task<OperationResult<PickedFile>> OpenAsync(FileSelectionOptions options, CancellationToken cancellationToken = default)
        {
            gesture.VerifyStart();
            Assert(options.MaximumBytes == 65536, "Picker gets the actual 64 KiB budget, not a sentinel-expanded allowance.");
            Opens++;
            Token = cancellationToken;
            return Open(options, cancellationToken);
        }
        public void Dispose() => Disposals++;
    }

    private sealed class TrackingStream(byte[] bytes) : MemoryStream(bytes, writable: false)
    {
        public int BytesRead { get; private set; }
        public int ReadCalls { get; private set; }
        public int Disposals { get; private set; }
        public bool FailRead { get; set; }
        public bool FailDispose { get; set; }
        public Task? ReadGate { get; set; }
        public int? RequiredThread { get; set; }
        public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default)
        {
            if (RequiredThread is int thread) Assert(Environment.CurrentManagedThreadId == thread, "UI-bound stream read starts on its owning thread.");
            ReadCalls++;
            if (ReadGate is not null) await ReadGate;
            cancellationToken.ThrowIfCancellationRequested();
            if (FailRead) throw new IOException("PRIVATE_FILE_CONTENT must never reach diagnostics.");
            int count = base.Read(buffer.Span);
            BytesRead += count;
            return count;
        }
        public override ValueTask DisposeAsync()
        {
            Dispose();
            return ValueTask.CompletedTask;
        }
        protected override void Dispose(bool disposing)
        {
            if (RequiredThread is int thread) Assert(Environment.CurrentManagedThreadId == thread, "UI-bound stream cleanup stays on its owning thread.");
            if (disposing) Disposals++;
            base.Dispose(disposing);
            if (FailDispose) throw new IOException("PRIVATE_FILE_CONTENT cleanup failure.");
        }

    }

    private sealed class UiContext(Dispatcher dispatcher) : SynchronizationContext
    {
        public override void Post(SendOrPostCallback callback, object? state) => dispatcher.Post(() => callback(state));
    }

    private sealed class Backend : IBackend
    {
        public List<Peer> Peers { get; } = [];
        public bool Disposed { get; private set; }
        public string? FailUpdateId { get; set; }
        public Peer Find(string id) => Peers.Single(peer => peer.Id == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new Peer(this, element, events); Peers.Add(peer); return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() => Disposed = true;
    }
    private sealed class Peer(Backend backend, Element element, IControlEvents events) : IMutableElementPeer
    {
        public Element Element { get; } = element;
        public IControlEvents Events { get; } = events;
        public string Id { get; } = element is Control control ? control.AutomationId : "";
        public List<ElementProperty> Updates { get; } = [];
        public List<IElementPeer> Children { get; } = [];
        public bool Disposed { get; private set; }
        public void AddChild(IElementPeer child) => Children.Add(child);
        public void InsertChild(int index, IElementPeer child) => Children.Insert(index, child);
        public void RemoveChild(IElementPeer child) => Assert(Children.Remove(child), "Removed service subtree was parented.");
        public void ValidateMove(IElementPeer child, int index) => Assert(Children.Contains(child), "Moved service subtree remains parented.");
        public void MoveChild(IElementPeer child, int index) { Children.Remove(child); Children.Insert(index, child); }
        public void Update(ElementProperty property)
        {
            if (backend.FailUpdateId == Id) throw new InvalidOperationException("PRIVATE_CLIPBOARD_CONTENT from failing native peer.");
            Updates.Add(property);
            if (Element is TextInput input && property == ElementProperty.Text)
                Assert(!Events.Change(input.Text), "Native setters do not echo edits.");
        }
        public void Dispose() => Disposed = true;
    }

    private sealed class Harness : IDisposable
    {
        public Gesture Gesture { get; } = new();
        public Dispatcher Dispatcher { get; } = new();
        public Services Services { get; }
        public Picker Picker { get; }
        public Host Host { get; }
        public Backend Backend { get; } = new();
        public PlatformServicesWorkbench App { get; }
        public PlatformServicesWorkbenchController Controller => App.Controller;
        public List<Exception> Errors { get; } = [];
        public Harness()
        {
            Services = new(Gesture);
            Picker = new(Gesture);
            Host = new(Dispatcher);
            App = PlatformServicesWorkbench.Create(Host, Services, Picker, Errors.Add);
            Host.Attach(Backend);
        }
        public void Run(Action action)
        {
            Gesture.Run(action);
            Dispatcher.Finish(Controller.LastOperation);
        }
        public void Click(string id) => Run(() => Assert(Backend.Find(id).Events.Click(), "Native button activation accepted."));
        public void Dispose()
        {
            Host.Dispose();
            Picker.Dispose();
            Services.Dispose();
        }
    }
    private sealed class Driver(Harness h) : IApplicationScenarioDriver
    {
        public void Click(string id) => h.Click(id);
        public void Change(string id, string value) => Assert(h.Backend.Find(id).Events.Change(value), "Native draft edit accepted.");
        public void Submit(string id) => Assert(h.Backend.Find(id).Events.Submit(), "Native submit accepted.");
        public string Text(string id) => h.Backend.Find(id).Element switch
        {
            TextInput input => input.Text,
            Control control => control.Name,
            _ => throw new InvalidOperationException("Not a text control.")
        };
        public bool Enabled(string id) => ((Control)h.Backend.Find(id).Element).Enabled;
        public bool Visible(string id) => ((Control)h.Backend.Find(id).Element).Visible;
    }
}
