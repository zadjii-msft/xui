namespace Xui.Experimental.Portable;

public sealed partial class Host : IDisposable
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

    public Element? Root { get { VerifyAccess(); return root ?? (building is { Parent: null, Staged: false } ? building.Content : null); } }
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
        if (building is null || building.Content is not null) throw new InvalidOperationException("Structural construction requires an active build scope.");
    }

    internal void VerifyBuildElement(Element element)
    {
        if (building is null || elements.IndexOf(element) < building.Start)
            throw new InvalidOperationException("Construction cannot claim elements outside its scope.");
    }

    internal void VerifyElementMutation(Element element)
    {
        VerifyMutation();
        if (building is not null) VerifyBuildElement(element);
    }

    internal void VerifyConstraintsSupport(Element element)
    {
        if (attachment is { } current && current.Peers.TryGetValue(element, out var peer) && peer is not IConstrainedElementPeer)
            throw new NotSupportedException("Per-axis constraints require an IConstrainedElementPeer backend.");
    }

    public void VerifyComponent(Element componentRoot)
    {
        VerifyAccess();
        ArgumentNullException.ThrowIfNull(componentRoot);
        if (componentRoot.Owner != this) throw new InvalidOperationException("The component belongs to another host.");
        ObjectDisposedException.ThrowIf(componentRoot.Disposed, componentRoot);
    }

    public void VerifyComponentMutation(Element componentRoot)
    {
        VerifyComponent(componentRoot);
        VerifyElementMutation(componentRoot);
    }

    public Task DispatchAsync(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void Run()
        {
            if (completion.Task.IsCompleted) return;
            try { VerifyAccess(); action(); completion.TrySetResult(); }
            catch (Exception error) { completion.TrySetException(error); }
        }
        try
        {
            if (dispatcher is ICancellableUiDispatcher cancellable)
                cancellable.Post(Run, error => completion.TrySetException(error));
            else dispatcher.Post(Run);
        }
        catch (Exception error) { completion.TrySetException(error); }
        return completion.Task;
    }

    public BuildScope BeginBuild()
    {
        VerifyMutation();
        if (building is null && (elements.Count != 0 || root is not null))
            throw new InvalidOperationException("A host owns exactly one component tree.");
        if (building?.Content is not null) throw new InvalidOperationException("Complete this scope before constructing another component.");
        return building = new BuildScope(this, building, elements.Count);
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
    public KeyedStack KeyedStack(Axis axis)
    {
        if (!Enum.IsDefined(axis)) throw new ArgumentOutOfRangeException(nameof(axis));
        return Create(() => new KeyedStack(this, axis));
    }
    public Label Label(string text) => Create(() => new Label(this, text));
    public Button Button(string text) => Create(() => new Button(this, text));
    public Toggle Toggle(string text) => Create(() => new Toggle(this, text));
    public CheckBox CheckBox(string text) => Create(() => new CheckBox(this, text));
    public Progress Progress(string name) => Create(() => new Progress(this, name));
    public Grid Grid(string name) => Create(() => new Grid(this, name));
    public TextInput TextInput(string name) => Create(() => new TextInput(this, name));
    public ScrollView ScrollView(Element content, string name)
    {
        VerifyBuilding();
        VerifyComponent(content);
        VerifyBuildElement(content);
        if (content.Parent is not null) throw new InvalidOperationException("Scroll content must be unparented.");
        var scroll = Create(() => new ScrollView(this, name));
        scroll.AddChild(content, 0);
        return scroll;
    }

    public Reveal Reveal(Element content, string name)
    {
        VerifyBuilding();
        VerifyComponent(content);
        VerifyBuildElement(content);
        if (content.Parent is not null) throw new InvalidOperationException("Reveal content must be unparented.");
        var reveal = Create(() => new Reveal(this, content, name));
        reveal.AddChild(content, 0);
        return reveal;
    }

    internal T RevealOperation<T>(Func<T> operation) => DeferInteractionDelivery(operation);

    public void SetContent(Stack content)
    {
        VerifyBuilding();
        ArgumentNullException.ThrowIfNull(content);
        if (content.Owner != this || content.Parent is not null)
            throw new InvalidOperationException("The root must be an unparented stack from this host.");
        VerifyBuildElement(content);
        var reached = new HashSet<Element>();
        void Visit(Element node) { reached.Add(node); foreach (var child in node.Children) Visit(child); }
        Visit(content);
        if (reached.Count != elements.Count - building!.Start || elements.Skip(building.Start).Any(element => !reached.Contains(element)))
            throw new InvalidOperationException("Every scoped element must belong to the component root tree.");
        building.Content = content;
    }

    public void Attach(IBackend backend)
        => DeferInteractionDelivery(() => { AttachCore(backend); return true; });

    private void AttachCore(IBackend backend)
    {
        VerifyMutation();
        if (reconciling != 0) throw new InvalidOperationException("A keyed update cannot attach a backend.");
        ArgumentNullException.ThrowIfNull(backend);
        if (attachment is not null || building is not null || root is null)
            throw new InvalidOperationException("Attach requires a completed tree without an attached backend.");
        ValidateThemeBackend(backend);
        if (backend is IBackendTreePreflight preflight)
            InputOperation(() => { preflight.ValidateTree(root); return true; });
        foreach (var input in elements.OfType<TextInput>()) input.ResetInteraction();
        var next = new Attachment(backend);
        transitioning = true;
        try
        {
            var rootPeer = CreatePeers(next, root);
            ConnectPageSelectors(next);
            backend.Mount(rootPeer);
            ApplyAttachedTheme(backend);
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
        foreach (var image in next.Order.OfType<Image>()) QueueImageState(image);
    }

    internal void Update(Element element, ElementProperty property)
    {
        if (attachment is not { } current || !current.Peers.ContainsKey(element)) return;
        UpdateAttachment(current, () => current.Peers[element].Update(property));
    }

    private static void ReleaseAttachment(Attachment current, List<Exception> failures)
    {
        RetireAttachmentResources(current, null, failures);
        try { current.Backend.Dispose(); }
        catch (Exception error) { failures.Add(error); }
        foreach (var element in current.Order.AsEnumerable().Reverse())
        {
            try { current.Peers[element].Dispose(); }
            catch (Exception error) { failures.Add(error); }
        }
        current.Peers.Clear();
        current.Order.Clear();
        current.PageLinks.Clear();
    }

    public void Detach()
    {
        VerifyMutation();
        if (building is not null || reconciling != 0) throw new InvalidOperationException("Complete construction and keyed updates before detaching.");
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
        if (building is not null || reconciling != 0) throw new InvalidOperationException("Complete construction and keyed updates before disposing the host.");
        var failures = new List<Exception>();
        var current = attachment;
        attachment = null;
        transitioning = true;
        try
        {
            RetireComponents(elements, failures);
            if (current is not null) ReleaseAttachment(current, failures);
            foreach (var element in elements) element.Release();
            elements.Clear();
            root = null;
            building = null;
            disposed = true;
        }
        finally { transitioning = false; }
        if (failures.Count != 0) throw new AggregateException(failures);
    }

    public sealed class BuildScope : IDisposable
    {
        private readonly Host host;
        private bool complete;
        private bool rollingBack;
        internal BuildScope? Parent { get; }
        internal int Start { get; }
        internal Stack? Content { get; set; }
        internal bool Staged { get; }
        internal BuildScope(Host host, BuildScope? parent, int start, bool staged = false)
        {
            this.host = host;
            Parent = parent;
            Start = start;
            Staged = staged;
        }
        public void Complete()
        {
            host.VerifyMutation();
            if (complete || host.building != this || Content is null)
                throw new InvalidOperationException("Complete requires this scope's root tree.");
            host.componentRoots.Add(Content);
            if (Parent is null && !Staged) host.root = Content;
            host.building = Parent;
            complete = true;
        }
        public void Dispose()
        {
            host.VerifyThread();
            if (complete || host.disposed) return;
            if (rollingBack) throw new InvalidOperationException("A build scope cannot reenter its own rollback.");
            if (host.building != this) throw new InvalidOperationException("Build scopes must close in reverse construction order.");
            var failures = new List<Exception>();
            rollingBack = true;
            try
            {
                host.RollbackElements(Start, failures);
                host.building = Parent;
                complete = true;
            }
            finally { rollingBack = false; }
            if (failures.Count != 0) throw new AggregateException(failures);
        }
    }

    private sealed class Attachment(IBackend backend)
    {
        internal IBackend Backend { get; } = backend;
        internal Dictionary<Element, IElementPeer> Peers { get; } = [];
        internal List<Element> Order { get; } = [];
        internal List<AttachmentResource> Resources { get; } = [];
        internal Dictionary<TextInput, TextInteraction> Interactions { get; } = [];
        internal HashSet<PageSelector> PageLinks { get; } = [];
        internal Dictionary<Image, long> ImageStates { get; } = [];
        internal bool ImageStateQueued;
        internal Dictionary<Image, (long Generation, ImageLoadState State)> PublishedImageStates { get; } = [];
    }

    private sealed class Events(Host host, Attachment attachment, Element element) : IValueControlEvents, ITextInteractionEvents, IPasswordControlEvents, ISelectionControlEvents, IRangeControlEvents, IPageControlEvents, IImagePresentationEvents
    {
        private bool Attached()
        {
            host.VerifyThread();
            return !host.disposed && host.attachment == attachment && !host.transitioning &&
                host.updating == 0 && host.viewportUpdates == 0 && !element.Disposed && attachment.Peers.ContainsKey(element);
        }
        private bool Active() => Attached() && !host.MessageDialogInputBlocked && element.AcceptsInput();
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
            if (element is TextInput input) input.RaiseChange(text);
            else if (element is MultilineText document)
            {
                if (document.ReadOnly) return false;
                document.RaiseChange(text);
            }
            else throw new InvalidOperationException("Only a text editor accepts string change events.");
            return true;
        }
        public bool Submit()
        {
            if (!Active()) return false;
            if (element is not TextInput input) throw new InvalidOperationException("Only TextInput accepts submit events.");
            input.RaiseSubmit();
            return true;
        }
        public bool ToggleChanged(bool value)
        {
            if (!Active()) return false;
            if (element is not Toggle toggle) throw new InvalidOperationException("Only Toggle accepts boolean changes.");
            toggle.RaiseChange(value);
            return true;
        }
        public bool CheckChanged(CheckState value)
        {
            if (!Active()) return false;
            if (element is not CheckBox checkBox) throw new InvalidOperationException("Only CheckBox accepts check-state changes.");
            checkBox.RaiseChange(value);
            return true;
        }
        public bool InteractionChanged(TextInteraction interaction)
        {
            host.VerifyThread();
            if (host.disposed || host.attachment != attachment || host.transitioning ||
                element.Disposed || !attachment.Peers.ContainsKey(element)) return false;
            if (element is not TextInput input) throw new InvalidOperationException("Only TextInput accepts text interaction snapshots.");
            host.CaptureInteraction(attachment, input, interaction);
            return true;
        }
        public bool PasswordChanged()
        {
            if (!Active()) return false;
            if (element is not PasswordInput password) throw new InvalidOperationException("Only PasswordInput accepts password change notices.");
            password.RaiseChange();
            return true;
        }
        public bool SelectionChanged(ulong selected)
        {
            if (!Active()) return false;
            if (element is not SingleChoice choice) throw new InvalidOperationException("Only SingleChoice accepts selection events.");
            choice.RaiseChanged(selected);
            return true;
        }
        public bool RangePreviewed(double value)
        {
            if (!Active()) return false;
            if (element is not RangeInput range) throw new InvalidOperationException("Only RangeInput accepts preview events.");
            range.RaisePreview(value);
            return true;
        }
        public bool RangeChanged(double value)
        {
            if (!Active()) return false;
            if (element is not RangeInput range) throw new InvalidOperationException("Only RangeInput accepts numeric input events.");
            range.RaiseChanged(value);
            return true;
        }
        public bool RangeCanceled(double value)
        {
            if (!Attached()) return false;
            if (element is not RangeInput range) throw new InvalidOperationException("Only RangeInput accepts range cancellation.");
            range.RaiseCanceled(value, !host.MessageDialogInputBlocked && element.AcceptsInput());
            return true;
        }
        public bool PageSelected(ulong id)
        {
            if (!Active()) return false;
            if (element is not PageSelector selector) throw new InvalidOperationException("Only page selectors accept page selection.");
            selector.RaiseSelected(id);
            return true;
        }
        public bool PageActivated(ulong id)
        {
            if (!Active()) return false;
            if (element is not PageSelector selector) throw new InvalidOperationException("Only page selectors accept page activation.");
            selector.RaiseActivated(id);
            return true;
        }
        public bool PageCloseRequested(ulong id)
        {
            if (!Active()) return false;
            if (element is not TabStrip tabs) throw new InvalidOperationException("Only TabStrip accepts page close requests.");
            tabs.RaiseCloseRequested(id);
            return true;
        }
        public bool ImageCompleted(long generation, ImageDecodePlan plan, int pixelWidth, int pixelHeight)
        {
            if (!Attached()) return false;
            if (element is not Image image) throw new InvalidOperationException("Only Image accepts decode completion.");
            if (!image.Awaiting(generation)) return false;
            try { image.Complete(plan, pixelWidth, pixelHeight); }
            catch (Exception error) { throw host.FailAttachment(attachment, error); }
            host.QueueImageState(image);
            return true;
        }
        public bool ImageFailed(long generation, string message)
        {
            if (!Attached()) return false;
            if (element is not Image image) throw new InvalidOperationException("Only Image accepts decode failures.");
            if (!image.Awaiting(generation)) return false;
            try { image.Fail(message); }
            catch (Exception error) { throw host.FailAttachment(attachment, error); }
            host.QueueImageState(image);
            return true;
        }
        public bool ImagePresentationFailed(long generation, Exception error)
        {
            host.VerifyThread();
            if (host.disposed || host.attachment != attachment || element.Disposed ||
                !attachment.Peers.ContainsKey(element)) return false;
            if (element is not Image image) throw new InvalidOperationException("Only Image accepts presentation failures.");
            if (image.SourceGeneration != generation || image.Source is null ||
                image.State is not (ImageLoadState.Loading or ImageLoadState.Ready)) return false;
            ArgumentNullException.ThrowIfNull(error);
            if (host.transitioning || host.updating != 0 || host.building is not null ||
                host.reconciling != 0 || host.viewportUpdates != 0)
                throw new InvalidOperationException("Image presentation failures must be queued outside native operations, construction, reconciliation, and viewport staging.", error);
            throw host.FailAttachment(attachment, error);
        }
    }
}
