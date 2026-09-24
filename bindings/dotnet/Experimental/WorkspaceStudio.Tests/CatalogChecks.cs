using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void CatalogChecks()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var controller = new WorkspaceStudioController(host, new AnalysisService(), _ => throw new InvalidOperationException("Unexpected catalog test failure."));
        Xui.Experimental.Portable.Stack root;
        KeyedStack rows;
        ScrollView scroll;
        using (var build = host.BeginBuild())
        {
            root = host.Stack(Axis.Vertical);
            rows = host.KeyedStack(Axis.Vertical);
            scroll = host.ScrollView(rows, "Studio document catalog");
            root.Add(scroll, 1);
            host.SetContent(root);
            build.Complete();
        }
        string status = "";
        var lifetime = host.GetComponentLifetime(root);
        var catalog = new StudioCatalogViewport(host, scroll, rows, controller, lifetime, text => status = text);
        controller.Attach(lifetime, catalog.UpdateSource);
        Assert(rows.Children.Count == 0 && catalog.MountedCount == 0 && !catalog.IsReady,
            "Initial 10k catalog queues data but creates no native rows or giant initial gap before attachment.");
        var backend = new CatalogBackend(dispatcher);
        host.Attach(backend);
        catalog.Attach();
        dispatcher.Drain();
        Assert(StudioCatalogViewport.RowHeight == 128 && backend.LiveRows.All(peer =>
            peer.Element.HeightConstraints == AxisConstraints.Fixed(128)),
            "Every realized catalog row uses the declared fixed Studio pitch before viewport allocation.");
        Assert(catalog.IsReady && catalog.MountedCount is >= 4 and <= 8 && backend.Lease!.Flushes > 0,
            "Viewport materializes only visible rows plus bounded overscan and settles native geometry.");
        Assert(backend.LiveRows.All(peer => peer.Info!.Value.Count == 10000) &&
            backend.LiveRows.Any(peer => peer.Info!.Value.Key == "doc-00001"),
            "Virtual metadata identifies stable documents and logical collection count.");
        var firstButton = backend.Find("doc-00001-open");
        var firstTitle = backend.Find("doc-00001-catalog-title");
        var titleLabel = (Label)firstTitle.Element;
        Assert(((Button)firstButton.Element).Text == "Open" &&
            titleLabel.Text == "Keyboard navigation 00001" &&
            titleLabel.TextLayout == LabelTextLayout.SingleLine(TextOverflow.CharacterEllipsis),
            "Catalog uses an invariant short native action beside a real single-line title label.");
        Assert(firstButton.Element.Parent is Xui.Experimental.Portable.Stack { Axis: Axis.Horizontal } header &&
            ReferenceEquals(header.Children[0], titleLabel) && ReferenceEquals(header.Children[1], firstButton.Element) &&
            titleLabel.Flex == 1 && firstButton.Element.Flex == 0,
            "Native title shrinks and ellipsizes in remaining width while the Open action keeps its natural width.");
        string longTitle = new string('W', 158) + "\U0001F642";
        controller.SetTitle("doc-00001", longTitle);
        Assert(longTitle.Length == 160 && ReferenceEquals(firstTitle, backend.Find("doc-00001-catalog-title")) &&
            ReferenceEquals(firstButton, backend.Find("doc-00001-open")) &&
            titleLabel.Text == longTitle && titleLabel.Name == longTitle &&
            ((Button)firstButton.Element).Help == "Open document: " + longTitle,
            "Long/dirty title updates preserve full native accessible text, descriptive action help, and retained peer identity.");
        Assert(((Button)firstButton.Element).Text == "Open" &&
            ((Label)backend.Find("doc-00001-category").Element).Text == "Engineering / Local draft",
            "Dirty state and long document names cannot enlarge the action text into multiline content.");
        string incompleteTitle = new('x', 500);
        controller.SetTitle("doc-00001", incompleteTitle);
        Assert(titleLabel.Name == incompleteTitle && titleLabel.Text == incompleteTitle &&
            ((Button)firstButton.Element).Text == "Open" &&
            !controller.State.Session.Document("doc-00001").CanAnalyze,
            "Even an overlong editable title retains full accessible content without changing native action text or bypassing validation.");
        controller.SetTitle("doc-00001", " ");
        Assert(titleLabel.Text == "Untitled draft" && ((Button)firstButton.Element).Help == "Open document: Untitled draft" &&
            controller.State.Session.Document("doc-00001").Title == " ",
            "Whitespace title uses a visible descriptive placeholder without normalizing the editable draft.");
        controller.SetTitle("doc-00001", longTitle);
        firstButton.Focused = true;
        backend.Lease!.RequestOffset(500 * StudioCatalogViewport.RowHeight);
        dispatcher.Drain();
        Assert(catalog.MountedCount <= 11 && !firstButton.Disposed &&
            backend.LiveRows.Any(peer => peer.Info!.Value.Key == "doc-00501"),
            "Far scrolling keeps bounded rows plus the actual focused catalog row.");
        Assert(catalog.Offset == 64000 && status.Contains("10000 documents", StringComparison.Ordinal),
            "App reflects committed offset and logical count, not native row count as data count.");
        firstButton.Focused = false;
        backend.Lease.RequestOffset(600 * StudioCatalogViewport.RowHeight);
        dispatcher.Drain();
        Assert(firstButton.Disposed && !firstButton.Events.Click(), "Unpinned offscreen document row retires and rejects stale callbacks.");
        var row = backend.LiveRows.First(peer => peer.Info!.Value.Key == "doc-00601");
        var open = backend.Find("doc-00601-open");
        Assert(open.Events.Click() && controller.State.Session.ActiveDocument == "doc-00601",
            "Native virtual row opens the correct stable document identity.");
        long version = catalog.SourceVersion;
        controller.SetQuery("doc-00002");
        dispatcher.Drain();
        Assert(catalog.SourceVersion > version && catalog.MountedCount == 1 &&
            backend.LiveRows.Single().Info!.Value is { Key: "doc-00002", Index: 0, Count: 1 },
            "Filtering publishes new source version, extent, and truthful row metadata.");
        Assert(row.Disposed, "Old source rows retire when no longer in projection.");
        host.Detach();
        Assert(backend.Disposed && backend.Peers.All(peer => peer.Disposed), "Detach releases catalog peers and native lease.");
        catalog.PrepareForAttachment();
        Assert(rows.Children.Count == 0 && catalog.MountedCount == 0 && !catalog.IsReady,
            "Detached bootstrap empties old presentation before a new attachment.");
        var replacement = new CatalogBackend(dispatcher);
        host.Attach(replacement);
        catalog.Attach();
        dispatcher.Drain();
        Assert(catalog.MountedCount == 1 && replacement.LiveRows.Single().Info!.Value.Key == "doc-00002" &&
            controller.State.Session.ActiveDocument == "doc-00601",
            "Reattachment preserves catalog projection and separately owned active document.");
        var pending = replacement.Lease!;
        pending.RequestOffset(0);
        host.Dispose();
        dispatcher.Drain();
        Assert(lifetime.Token.IsCancellationRequested && replacement.Disposed && replacement.Peers.All(peer => peer.Disposed),
            "Root lifetime retires catalog observer/controller with no late row resurrection.");
    }

    private sealed class CatalogBackend(Dispatcher dispatcher) : IBackend, IHostViewportBackend, IThemeBackend
    {
        private Action<Size>? viewportChanged;
        public Size Viewport { get; private set; } = new(1280, 900);
        public ThemeSettings? Theme { get; private set; }
        public bool SupportsReveal { get; set; }
        public bool ReducedMotion { get; set; }
        public int RevealCreations { get; private set; }
        public List<CatalogPeer> Peers { get; } = [];
        public CatalogLease? Lease { get; set; }
        public bool Disposed { get; private set; }
        public IEnumerable<CatalogPeer> LiveRows => Peers.Where(peer => !peer.Disposed && peer.Info is not null);
        public CatalogPeer Find(string id) => Peers.Single(peer => !peer.Disposed && peer.Id == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            CatalogPeer peer;
            if (element is Reveal reveal && SupportsReveal)
            {
                RevealCreations++;
                peer = new RevealCatalogPeer(this, dispatcher, reveal, events);
            }
            else peer = new CatalogPeer(this, dispatcher, element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() { Disposed = true; viewportChanged = null; }
        public IDisposable ObserveViewport(Action<Size> changed)
        {
            viewportChanged = changed;
            changed(Viewport);
            return new Subscription(() => viewportChanged = null);
        }
        public void Resize(float width, float height)
        {
            Viewport = new(width, height);
            viewportChanged?.Invoke(Viewport);
        }
        public void ValidateTheme(ThemeSettings? theme) { }
        public void ApplyTheme(ThemeSettings? theme) => Theme = theme;
    }
    private class CatalogPeer(CatalogBackend backend, Dispatcher dispatcher, Element element, IControlEvents events) :
        IMutationPreflightPeer, IConstrainedElementPeer, IFocusableElementPeer, IVirtualViewportPeer, IVirtualItemPeer,
        IPageViewElementPeer, IPageSelectorElementPeer, IPresentationPeer, ITextLayoutPeer, ISingleChoiceElementPeer
    {
        protected CatalogBackend Backend => backend;
        public Element Element { get; } = element;
        public IControlEvents Events { get; } = events;
        public string Id { get; } = element is Control control ? control.AutomationId : "";
        public List<IElementPeer> Children { get; } = [];
        public List<ElementProperty> Updates { get; } = [];
        public VirtualItemInfo? Info { get; private set; }
        public bool Disposed { get; private set; }
        public bool Focused { get; set; }
        public bool RejectPages { get; set; }
        public bool RejectVisibility { get; set; }
        public bool FailPages { get; set; }
        public bool FailVisibility { get; set; }
        public bool RejectFocus { get; set; }
        public int FocusRequests { get; private set; }
        public bool RejectMutation { get; set; }
        public bool FailInsert { get; set; }
        public CatalogPeer? LinkedPages { get; private set; }
        public PageEntry[] NativeEntries { get; private set; } = ReadPages(element);
        public ulong? NativeSelected { get; private set; } = ReadSelected(element);
        public bool NativePageVisible { get; private set; } = element is not PageView page || page.Visible;
        public bool HasFocus => Focused;
        public bool TryFocus()
        {
            FocusRequests++;
            if (RejectFocus) return false;
            foreach (var peer in backend.Peers) peer.Focused = false;
            Focused = true;
            return true;
        }
        public void AddChild(IElementPeer child) => Children.Add(child);
        public void InsertChild(int index, IElementPeer child)
        {
            if (FailInsert) throw new InvalidOperationException("Native child insertion failed.");
            Children.Insert(index, child);
        }
        public void ValidateMutation()
        {
            if (RejectMutation) throw new NotSupportedException("Native child composition is active.");
        }
        public void RemoveChild(IElementPeer child) { if (!Children.Remove(child)) throw new InvalidOperationException("Unknown native child."); }
        public void ValidateMove(IElementPeer child, int index) { if (!Children.Contains(child)) throw new InvalidOperationException("Unknown native move."); }
        public void MoveChild(IElementPeer child, int index) { Children.Remove(child); Children.Insert(index, child); }
        public virtual void Update(ElementProperty property)
        {
            if (FailPages && property == ElementProperty.Pages) throw new InvalidOperationException("Native page update failed.");
            if (FailVisibility && property == ElementProperty.Visible && Element is PageView)
                throw new InvalidOperationException("Native page visibility update failed.");
            Updates.Add(property);
            if (property == ElementProperty.Pages)
            {
                NativeEntries = ReadPages(Element);
                NativeSelected = ReadSelected(Element);
            }
            if (property == ElementProperty.Visible && Element is PageView page) NativePageVisible = page.Visible;
            if (property == ElementProperty.Text)
            {
                string text = Element switch { TextInput input => input.Text, MultilineText input => input.Text, _ => "" };
                Assert(!Events.Change(text), "Programmatic editor changes reject native event echoes.");
            }
        }
        public void SetVirtualItemInfo(VirtualItemInfo info) { info.Validate(); Info = info; }
        public void Dispose() => Disposed = true;
        public void ValidateTypography(Typography? typography) { }
        public void ValidateTextLayout(LabelTextLayout? layout) { }
        public void ValidateChoices(IReadOnlyList<Choice> items, ulong? selected) { }
        public void ValidatePages(IReadOnlyList<PageEntry> pages, ulong? selected)
        {
            if (RejectPages) throw new NotSupportedException("Native document composition is active.");
        }
        public void ValidateVisibility(bool visible)
        {
            if (RejectVisibility) throw new InvalidOperationException("Native document cannot be hidden while composing.");
        }
        public void ConnectPages(IPageViewElementPeer pages) => LinkedPages = (CatalogPeer)pages;
        public bool SelectPage(ulong id)
        {
            NativeSelected = id;
            return ((IPageControlEvents)Events).PageSelected(id);
        }
        public bool ActivatePage(ulong id) => ((IPageControlEvents)Events).PageActivated(id);
        public bool ClosePage(ulong id) => ((IPageControlEvents)Events).PageCloseRequested(id);
        private static PageEntry[] ReadPages(Element value) => value switch
        {
            PageView page => page.Pages.ToArray(),
            PageSelector selector => selector.Pages.Pages.ToArray(),
            _ => []
        };
        private static ulong? ReadSelected(Element value) => value switch
        {
            PageView page => page.Selected,
            PageSelector selector => selector.Selected,
            _ => null
        };
        public IVirtualViewportLease BeginVirtualViewport(int itemCount, float rowHeight, long sourceVersion, Action<VirtualViewportRequest> requested)
        {
            var lease = new CatalogLease(dispatcher, itemCount, rowHeight, sourceVersion, requested);
            backend.Lease = lease;
            lease.Queue();
            return lease;
        }
    }
    private sealed class RevealCatalogPeer(CatalogBackend backend, Dispatcher dispatcher, Reveal reveal, IControlEvents events) :
        CatalogPeer(backend, dispatcher, reveal, events), IRevealElementPeer
    {
        public bool RejectClose { get; set; }
        public bool FailRevealUpdate { get; set; }
        public int CloseRequests { get; private set; }
        public RevealPresentation Presentation { get; set; } = new(reveal.Open ? 1 : 0, false);
        public bool CanSetOpen(bool open)
        {
            if (open) return true;
            CloseRequests++;
            if (RejectClose) return false;
            foreach (var peer in Backend.Peers.Where(peer => !peer.Disposed && peer.Focused))
                for (Element? ancestor = peer.Element; ancestor is not null; ancestor = ancestor.Parent)
                    if (ReferenceEquals(ancestor, reveal)) return false;
            return true;
        }
        public void ValidateMotion(RevealMotion motion)
        {
            Assert(motion.Direction == RevealDirection.Bottom && motion.DurationMilliseconds == 180,
                "Operations asks the native peer for exactly the bounded bottom-expanding motion.");
        }
        public override void Update(ElementProperty property)
        {
            base.Update(property);
            if (property != ElementProperty.RevealState) return;
            if (FailRevealUpdate) throw new InvalidOperationException("Native reveal update failed.");
            Presentation = Backend.ReducedMotion
                ? new(reveal.Open ? 1 : 0, false)
                : new(Presentation.Progress, true);
        }
    }
    private sealed class Subscription(Action release) : IDisposable
    {
        private bool disposed;
        public void Dispose() { if (!disposed) { disposed = true; release(); } }
    }
    private sealed class CatalogLease(Dispatcher dispatcher, int count, float pitch, long sourceVersion,
        Action<VirtualViewportRequest> requested) : ISettledVirtualViewportLease
    {
        private VirtualViewportRect committed = new(0, 240, 384, 0);
        private VirtualViewportRect candidate = new(0, 240, 384, count * pitch);
        private long committedVersion;
        private long candidateVersion = sourceVersion;
        private long epoch;
        private long reserved;
        private long committedEpoch;
        private bool queued;
        private bool disposed;
        public int Flushes { get; private set; }
        public void Queue()
        {
            if (queued || disposed) return;
            queued = true;
            dispatcher.Post(() =>
            {
                queued = false;
                if (!disposed) requested(new(++epoch, committedVersion, candidateVersion, committed, candidate, false));
            });
        }
        public void SetExtent(int itemCount, long version)
        {
            candidateVersion = version;
            float extent = itemCount * pitch;
            candidate = candidate with { Extent = extent, Offset = Math.Min(candidate.Offset, Math.Max(0, extent - candidate.Height)) };
            Queue();
        }
        public void RequestOffset(float offset)
        {
            candidate = candidate with { Offset = Math.Clamp(offset, 0, Math.Max(0, candidate.Extent - candidate.Height)) };
            Queue();
        }
        public VirtualViewportUpdateResult TryBeginUpdate(long expected)
        {
            if (expected != epoch) return VirtualViewportUpdateResult.Superseded;
            reserved = expected;
            return VirtualViewportUpdateResult.Ready;
        }
        public VirtualViewportCommitResult TryCommit(long expected)
        {
            if (reserved != expected) throw new InvalidOperationException("Epoch was not reserved.");
            committed = candidate;
            committedVersion = candidateVersion;
            committedEpoch = expected;
            reserved = 0;
            return VirtualViewportCommitResult.Committed;
        }
        public void Cancel(long expected) { reserved = 0; }
        public void FlushCommitted(long expected)
        {
            if (reserved != 0 || expected != committedEpoch) throw new InvalidOperationException("Must flush current committed epoch after pruning.");
            Flushes++;
        }
        public void Dispose() => disposed = true;
    }
}
