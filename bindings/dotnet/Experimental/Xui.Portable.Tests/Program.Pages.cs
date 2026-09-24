using PortableNavigation;
using PortableMutation;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void RetainedPageChecks()
    {
        RetainedPageRoundtrip();
        RetainedPageFailures();
        PageLinkOwnership();
        RetainedPageCorpus();
        InactivePagePreviews();
    }

    private static void RetainedPageRoundtrip()
    {
        using var host = new Host(new Dispatcher());
        var app = new RetainedPagesWorkbench(host);
        app.Open();
        var backend = new PagesBackend();
        host.Attach(backend);
        var first = backend.Find("first-input");
        var second = backend.Find("second-input");
        var tabs = backend.Find("workspace-tabs");
        var navigation = backend.Find("workspace-navigation");
        var pagePeer = backend.Peer(app.Documents);
        Assert(backend.Mounted && ReferenceEquals(tabs.LinkedPages, pagePeer) && ReferenceEquals(navigation.LinkedPages, pagePeer), "Selectors link actual native page peers before mounting.");
        Assert(app.Documents.Selected == 1 && app.Tabs.Selected == 1 && app.Navigation.Selected == 1, "One page selection is authoritative for all linked controls.");
        Assert(first.Events.Change("first retained draft") && !second.Events.Change("inactive edit"), "Only the active page admits editor events.");
        Assert(!host.TryFocus((TextInput)second.Element), "Inactive native pages cannot receive programmatic focus.");
        backend.Find("first-increment").Events.Click();
        var peers = backend.Peers.ToArray();
        tabs.Select(9007199254740993UL);
        Assert(app.Documents.Selected == 9007199254740993UL && app.OpenPages.Selected == 9007199254740993UL && app.Changes == 1, "Native tab selection commits page activation before authored selection handlers.");
        Assert(app.Activation == "No activation" && backend.FocusRequests == 0, "Arrow-style selection does not activate or steal focus into content.");
        Assert(!first.Events.Submit() && second.Events.Change("second retained draft"), "The old page becomes input-inactive without disposing its editor.");
        tabs.Activate(9007199254740993UL);
        Assert(app.Activation == "Activated: 9007199254740993" && app.Changes == 1, "Activation of the selected page is separate from a selection change.");
        navigation.Select(1);
        Assert(app.Changes == 2 && app.Documents.Selected == 1 && app.Tabs.Selected == 1 &&
            ((TextInput)first.Element).Text == "first retained draft", "Navigation and tabs share retained page selection.");
        Assert(((Label)backend.Find("first-count").Element).Text == "Edits: 1" && peers.SequenceEqual(backend.Peers), "Returning to a page retains all editors and row-local state.");
        Assert(first.DisposeCalls == 0 && second.DisposeCalls == 0 && first.TextUpdates == 0, "Switching pages does not unmount, clear, or rewrite either editor.");
        app.PaneVisible = false;
        Assert(!app.Documents.Visible && !pagePeer.NativeVisible && app.Documents.Selected == 1 &&
            !first.Events.Change("hidden page") && !host.TryFocus((TextInput)first.Element),
            "Whole-page visibility preserves selection while blocking descendant input and programmatic focus.");
        app.PaneVisible = true;
        Assert(first.Events.Change("first retained draft") && first.DisposeCalls == 0, "Showing a retained page reuses the same native editor.");
        tabs.Close(1);
        Assert(app.CloseRequest == "Close requested: 1" && app.Documents.Pages.Count == 2, "Close requests allow the application to veto without removing data or UI.");
        app.Tabs.Closable = false;
        Throws<InvalidOperationException>(() => tabs.Close(1));
        app.Tabs.Closable = true;
        app.Tabs.Enabled = false;
        Assert(!((IPageControlEvents)tabs.Events).PageSelected(9007199254740993UL) &&
            !((IPageControlEvents)tabs.Events).PageCloseRequested(1), "Disabled selectors reject native actions.");
        app.Tabs.Enabled = true;
        app.Navigation.Visible = false;
        Assert(!((IPageControlEvents)navigation.Events).PageActivated(9007199254740993UL), "Hidden navigation cannot activate pages.");
        app.Navigation.Visible = true;
        app.Navigation.Expanded = false;
        Assert(!navigation.NativeExpanded, "Responsive pane collapse updates the existing navigation peer.");
        var original = app.OpenPages;
        app.OpenPages = ([original.Items[1], original.Items[0]], 1UL);
        Assert(backend.Peer(app.Documents).Children.Select(peer => peer.Element).SequenceEqual(app.Documents.Children) &&
            ReferenceEquals(first, backend.Find("first-input")) && app.Documents.Selected == 1, "Reordered page IDs retain their native editors and selected identity.");
        app.OpenPages = ([original.Items[1]], null);
        Assert(app.Documents.Selected == 9007199254740993UL && first.DisposeCalls == 1 && !first.Events.Change("stale"), "Closing a selected page retires only that page and falls back deterministically.");
        app.OpenPages = ([], null);
        Assert(app.Documents.Selected is null && tabs.NativeSelected is null && navigation.NativeSelected is null &&
            second.DisposeCalls == 1, "An empty page source has no native or model selection.");
        app.Open();
        Assert(!ReferenceEquals(first, backend.Find("first-input")), "Reopening a closed ID constructs a fresh component.");
        app.OpenPages = ([PageItem.Create(1UL, "Disabled page", h => new MutationRow(h, "disabled"), enabled: false)], null);
        Assert(app.Documents.Selected is null && !backend.Find("first-input").Events.Change("not active"), "All-disabled pages remain retained but unselected and inaccessible to input.");
        host.Detach();
        Assert(backend.Peers.All(peer => peer.DisposeCalls == 1), "Detach releases all remaining native page peers once.");
        Assert(!((IPageControlEvents)tabs.Events).PageSelected(1), "Old selector events stay stale after detach.");
        var replacement = new PagesBackend();
        host.Attach(replacement);
        Assert(app.Documents.Pages.Count == 1 && app.Documents.Selected is null, "Reattachment retains the page model without inventing a selection.");
        app.OpenPages = ([], null);
    }

    private static void RetainedPageFailures()
    {
        using var host = new Host(new Dispatcher());
        var app = new RetainedPagesWorkbench(host);
        app.Open();
        var legacy = new Backend();
        Throws<NotSupportedException>(() => host.Attach(legacy));
        Assert(legacy.Disposed && legacy.Peers.All(peer => peer.Disposed), "Legacy page/selector capabilities reject explicitly with partial cleanup.");
        var backend = new PagesBackend();
        host.Attach(backend);
        var tabs = backend.Find("workspace-tabs");
        var pages = backend.Peer(app.Documents);
        var first = backend.Find("first-input");
        var original = app.OpenPages;
        var originalChildren = app.Documents.Children.ToArray();
        int factories = 0;
        pages.Reject = true;
        var reject = Throws<KeyedUpdateException>(() => app.OpenPages =
            ([.. original.Items, PageItem.Create(3UL, "Third", h => { factories++; return new MutationRow(h, "third"); })], 3UL));
        Assert(!reject.ModelCommitted && factories == 0 && app.OpenPages == original &&
            app.Documents.Children.SequenceEqual(originalChildren), "Native page preflight runs before factories, selection, or child-model commit.");
        tabs.NativeSelected = 9007199254740993UL;
        Throws<KeyedUpdateException>(() => tabs.Select(9007199254740993UL));
        Assert(tabs.NativeSelected == 1 && app.Documents.Selected == 1 && app.Changes == 0 && host.IsAttached,
            "A rejected optimistic native selection is restored to the old page instead of diverging.");
        pages.Reject = false;
        pages.RejectVisibility = true;
        var hidden = Throws<KeyedUpdateException>(() => app.PaneVisible = false);
        Assert(!hidden.ModelCommitted && app.PaneVisible && app.Documents.Visible && pages.NativeVisible,
            "Unsafe whole-pane hiding rejects before generated state, model, or native visibility changes.");
        pages.RejectVisibility = false;
        tabs.Reject = true;
        reject = Throws<KeyedUpdateException>(() => app.OpenPages = (original.Items, 9007199254740993UL));
        Assert(!reject.ModelCommitted && app.OpenPages == original && app.Documents.Selected == 1, "Every linked selector participates in precommit capability validation.");
        tabs.Reject = false;
        void Reject(PageItem[]? next, ulong? selected = null)
        {
            var error = Throws<KeyedUpdateException>(() => app.OpenPages = (next!, selected));
            Assert(!error.ModelCommitted && app.OpenPages == original && host.IsAttached, "Invalid page snapshot does not change a published state.");
        }
        Reject(null);
        Reject([original.Items[0], original.Items[0]]);
        Reject([null!]);
        Reject(original.Items, 99);
        Reject([PageItem.Create(1UL, "Disabled", h => new MutationRow(h, "disabled"), enabled: false)], 1);
        MutationRow? abandoned = null;
        Reject([
            PageItem.Create(2UL, "Candidate", h => abandoned = new MutationRow(h, "candidate")),
            PageItem.Create<MutationRow>(3UL, "Failure", _ => throw new ApplicationException("page factory"))
        ]);
        Throws<ObjectDisposedException>(() => _ = abandoned!.Entry);
        Reject([PageItem.Create(2UL, "Orphan", h =>
        {
            var row = new MutationRow(h, "orphan");
            h.Label("unowned");
            return row;
        })]);
        using var foreign = new Host(new Dispatcher());
        var foreignRow = new MutationRow(foreign, "foreign");
        Reject([PageItem.Create(4UL, "Foreign", _ => foreignRow)]);
        Assert(foreignRow.Entry == "", "A foreign factory return is not consumed or disposed.");
        app.OpenPages = ([PageItem.Create(1UL, "Different type", h => new MutationBanner(h, "replacement"))], 1UL);
        Assert(first.DisposeCalls == 1 && !first.Events.Submit(), "Changing a page's component type retires the prior editor.");
        app.OpenPages = original;
        pages.FailUpdate = true;
        var failure = Throws<KeyedUpdateException>(() => app.OpenPages = (original.Items, 9007199254740993UL));
        Assert(failure.ModelCommitted && !host.IsAttached && app.Documents.Selected == 9007199254740993UL,
            "Native activation failure detaches and retains the committed authoritative selection.");
        app.OpenPages = original;
        Assert(app.Documents.Selected == 1, "Restoring an earlier snapshot after committed activation failure does not skip reconciliation.");
        var recovered = new PagesBackend();
        host.Attach(recovered);
        recovered.Find("workspace-tabs").FailUpdate = true;
        failure = Throws<KeyedUpdateException>(() => app.Documents.SetSelected(9007199254740993UL));
        Assert(failure.ModelCommitted && !host.IsAttached && recovered.Disposed, "A linked native-header update failure retires the complete attachment.");
        Assert(!((IPageControlEvents)recovered.Find("workspace-navigation").Events).PageActivated(1), "Failed attachment selector callbacks remain stale.");
        var finalBackend = new PagesBackend();
        host.Attach(finalBackend);
        finalBackend.Peer(app.Documents).FailUpdate = true;
        var visibilityFailure = Throws<KeyedUpdateException>(() => app.PaneVisible = false);
        Assert(visibilityFailure.ModelCommitted && !host.IsAttached && !app.PaneVisible && !app.Documents.Visible,
            "A native hide failure preserves committed visibility and detaches.");
        app.PaneVisible = true;
        Assert(app.Documents.Visible, "A previous visible binding can restore the retained model after failed native hiding.");
    }

    private static void PageLinkOwnership()
    {
        using var firstHost = new Host(new Dispatcher());
        PageView firstPages;
        using (var build = firstHost.BeginBuild())
        {
            var root = firstHost.Stack(Axis.Vertical);
            firstPages = firstHost.PageView("First host");
            var tabs = firstHost.TabStrip("Tabs");
            tabs.BindPages(firstPages);
            Throws<InvalidOperationException>(() => tabs.BindPages(firstPages));
            root.Add(tabs).Add(firstPages);
            firstHost.SetContent(root);
            build.Complete();
        }
        using var secondHost = new Host(new Dispatcher());
        using (var build = secondHost.BeginBuild())
        {
            var root = secondHost.Stack(Axis.Vertical);
            var tabs = secondHost.TabStrip("Unbound");
            Throws<InvalidOperationException>(() => tabs.BindPages(firstPages));
            root.Add(tabs);
            secondHost.SetContent(root);
            build.Complete();
        }
        var backend = new PagesBackend();
        Throws<InvalidOperationException>(() => secondHost.Attach(backend));
        Assert(backend.Disposed && backend.Peers.All(peer => peer.DisposeCalls == 1), "An unbound selector fails attachment explicitly instead of exposing a fake navigation control.");
        Throws<ArgumentException>(() => PageItem.Create(0, "Invalid", h => new MutationRow(h, "invalid")));
        Throws<ArgumentException>(() => PageItem.Create(1, " ", h => new MutationRow(h, "invalid")));
        Throws<ArgumentException>(() => PageItem.Create(1, "nul\0title", h => new MutationRow(h, "invalid")));
        Throws<InvalidOperationException>(() => firstPages.Reconcile([]));
        firstHost.Dispose();
        Throws<ObjectDisposedException>(() => firstPages.SetPages([]));
        using var linkedHost = new Host(new Dispatcher());
        KeyedStack owned;
        RetainedPagesWorkbench? nested = null;
        using (var build = linkedHost.BeginBuild())
        {
            var root = linkedHost.Stack(Axis.Vertical);
            owned = linkedHost.KeyedStack(Axis.Vertical);
            owned.Reconcile([KeyedItem.Create("nested", h => nested = new RetainedPagesWorkbench(h))]);
            var outside = linkedHost.TabStrip("Surviving selector");
            outside.BindPages(nested!.Documents);
            root.Add(outside).Add(owned);
            linkedHost.SetContent(root);
            build.Complete();
        }
        var linkedBackend = new PagesBackend();
        linkedHost.Attach(linkedBackend);
        var removal = Throws<KeyedUpdateException>(() => owned.Reconcile([]));
        Assert(!removal.ModelCommitted && linkedHost.IsAttached && owned.Children.Count == 1,
            "Removing a page container cannot leave a surviving selector linked to a retired tree.");

        using var dynamicHost = new Host(new Dispatcher());
        var outer = new RetainedPagesWorkbench(dynamicHost);
        var dynamicBackend = new PagesBackend();
        dynamicHost.Attach(dynamicBackend);
        outer.OpenPages = ([PageItem.Create(1, "Nested pages", h => new RetainedPagesWorkbench(h))], 1UL);
        Assert(dynamicBackend.Peers.Where(peer => peer.Element is PageSelector).All(peer => peer.LinkedPages is not null),
            "A dynamically constructed subtree links its native selectors before insertion.");
        outer.OpenPages = ([], null);
        Assert(dynamicHost.IsAttached, "Retiring nested pages and their selectors together is supported.");
    }

    private static void InactivePagePreviews()
    {
        using var host = new Host(new Dispatcher());
        var app = new RetainedPagesWorkbench(host);
        RangePage? row = null;
        app.OpenPages = ([PageItem.Create(1, "Range", h => row = new RangePage(h)),
            PageItem.Create(2, "Other", h => new MutationRow(h, "other"))], 1UL);
        var backend = new PagesBackend();
        host.Attach(backend);
        int cancellations = 0;
        row!.Range.Canceled += _ => cancellations++;
        var events = (IRangeControlEvents)backend.Find("page-range").Events;
        events.RangePreviewed(60);
        app.Documents.SetSelected(2);
        Assert(row.Range.PreviewValue == row.Range.Value && cancellations == 0, "Switching away silently clears native-only range preview state.");
        app.Documents.SetSelected(1);
        events.RangePreviewed(70);
        app.Documents.Visible = false;
        Assert(row.Range.PreviewValue == row.Range.Value && cancellations == 0, "Whole-pane hiding clears transient preview without retiring the page or emitting user cancellation.");
    }

    private sealed class RangePage : IPortableComponent
    {
        public Element Root { get; }
        public RangeInput Range { get; }
        public RangePage(Host host)
        {
            using var build = host.BeginBuild();
            var root = host.Stack(Axis.Vertical);
            Range = host.RangeInput("Retained page range");
            Range.AutomationId = "page-range";
            root.Add(Range);
            host.SetContent(root);
            build.Complete();
            Root = root;
        }
    }

    private sealed class PagesBackend : IBackend
    {
        public List<PagesPeer> Peers { get; } = [];
        public bool Mounted { get; private set; }
        public bool Disposed { get; private set; }
        public int FocusRequests { get; set; }
        public PagesPeer Find(string id) => Peers.Last(peer => peer.Id == id);
        public PagesPeer Peer(Element element) => Peers.Single(peer => ReferenceEquals(peer.Element, element) && peer.DisposeCalls == 0);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new PagesPeer(this, element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root)
        {
            Assert(Peers.Where(peer => peer.Element is PageSelector).All(peer => peer.LinkedPages is not null),
                "Native navigation relationships exist before attachment exposure.");
            Mounted = true;
        }
        public void Dispose() { Disposed = true; Mounted = false; }
    }

    private static void RetainedPageCorpus()
    {
        using var host = new Host(new Dispatcher());
        _ = new RetainedPagesWorkbench(host);
        var backend = new PagesBackend();
        host.Attach(backend);
        var driver = new PageDriver(backend);
        int count = PageScenarioRunner.Run(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Fixtures", "PageScenarios.json")), driver);
        assertions += count;
        Assert(count > 35, "The page corpus covers literal native selection/order, activation, close requests, and retained editor identity.");
        Console.WriteLine($"Retained page corpus: {count} literal native-state expectations passed.");
        const string prefix = """{"version":1,"scenarios":[{"name":"invalid","steps":[{"action":"click","id":"pages-reset"},""";
        foreach (string step in new[]
        {
                """{"action":"select-page","id":"workspace-tabs","page":9007199254740993}""",
                """{"action":"select-page","id":"workspace-tabs","page":"01"}""",
                """{"action":"expect","id":"workspace-tabs","selected":null}""",
                """{"action":"expect","id":"workspace-tabs","pages":["9007199254740993","1"]}""",
                """{"action":"expect","id":"workspace-tabs","enabled":false}""",
                """{"action":"expect","id":"first-input","visible":false}""",
                """{"action":"expect","id":"workspace-tabs"}""",
                """{"action":"expect-absent","id":"first-input"}""",
                """{"action":"change","id":"first-input","value":null}""",
                """{"action":"unknown","id":"workspace-tabs"}"""
            })
        {
            var error = Throws<InvalidOperationException>(() => PageScenarioRunner.Run(prefix + step + "]}]}", driver));
            Assert(error.InnerException is not null && error.Message.StartsWith("Page scenario 'invalid', step 2:", StringComparison.Ordinal),
                "Shared page runner identifies the exact failed literal step.");
        }
        Throws<InvalidOperationException>(() => PageScenarioRunner.Run("""{"version":2,"scenarios":[]}""", driver));
        Throws<InvalidOperationException>(() => PageScenarioRunner.Run("""{"version":1,"scenarios":[]}""", driver));
    }

    private sealed class PageDriver(PagesBackend backend) : IPageScenarioDriver
    {
        private readonly Dictionary<string, PagesPeer> remembered = [];
        public void Click(string id) { if (Enabled(id) && Visible(id)) backend.Find(id).Events.Click(); }
        public void Change(string id, string value)
        {
            if (!Enabled(id) || !Visible(id)) return;
            var peer = backend.Find(id);
            peer.NativeText = value;
            peer.Events.Change(value);
        }
        public void SelectPage(string id, ulong page) { if (Enabled(id) && Visible(id)) backend.Find(id).Select(page); }
        public void ActivatePage(string id, ulong page) { if (Enabled(id) && Visible(id)) backend.Find(id).Activate(page); }
        public void RequestClose(string id, ulong page) { if (Enabled(id) && Visible(id)) backend.Find(id).Close(page); }
        public string Text(string id) => backend.Find(id).NativeText;
        public bool Enabled(string id)
        {
            for (var peer = backend.Find(id); peer is not null; peer = peer.Parent)
                if (!peer.NativeEnabled) return false;
            return true;
        }
        public bool Visible(string id)
        {
            for (var peer = backend.Find(id); peer is not null; peer = peer.Parent)
            {
                if (!peer.NativeVisible) return false;
                if (peer.Parent is { Element: PageView } pages)
                {
                    int index = pages.Children.IndexOf(peer);
                    if (index < 0 || pages.NativeEntries[index].Id != pages.NativeSelected || !pages.NativeEntries[index].Enabled) return false;
                }
            }
            return true;
        }
        public bool Exists(string id) => backend.Peers.Any(peer => peer.Id == id && peer.DisposeCalls == 0);
        public ulong? Selected(string id) => backend.Find(id).NativeSelected;
        public IReadOnlyList<ulong> Pages(string id) => backend.Find(id).NativeEntries.Select(entry => entry.Id).ToArray();
        public void RememberEditor(string id) => remembered[id] = backend.Find(id);
        public bool SameEditor(string id) => remembered.TryGetValue(id, out var peer) && ReferenceEquals(peer, backend.Find(id)) && peer.DisposeCalls == 0;
    }
    private sealed class PagesPeer : IPageViewElementPeer, IPageSelectorElementPeer, IFocusableElementPeer, IRangeElementPeer
    {
        private readonly PagesBackend backend;
        public Element Element { get; }
        public IControlEvents Events { get; }
        public string Id { get; }
        public List<PagesPeer> Children { get; } = [];
        public PagesPeer? LinkedPages { get; private set; }
        public ulong? NativeSelected { get; set; }
        public bool NativeExpanded { get; private set; } = true;
        public bool NativeVisible { get; private set; } = true;
        public bool NativeEnabled { get; private set; } = true;
        public string NativeText { get; set; } = "";
        public PageEntry[] NativeEntries { get; private set; } = [];
        public PagesPeer? Parent { get; private set; }
        public int DisposeCalls { get; private set; }
        public int TextUpdates { get; private set; }
        public bool Reject { get; set; }
        public bool RejectVisibility { get; set; }
        public bool FailUpdate { get; set; }
        public bool HasFocus => false;
        public PagesPeer(PagesBackend backend, Element element, IControlEvents events)
        {
            this.backend = backend;
            Element = element; Events = events;
            Id = element is Control control ? control.AutomationId : "";
            Capture();
        }
        private void Capture()
        {
            if (Element is PageView page) { NativeSelected = page.Selected; NativeVisible = page.Visible; NativeEntries = page.Pages.ToArray(); }
            if (Element is PageSelector selector) { NativeSelected = selector.Selected; NativeEntries = selector.Pages.Pages.ToArray(); }
            if (Element is Control control)
            {
                NativeVisible = control.Visible;
                NativeEnabled = control.Enabled;
                NativeText = control is TextInput input ? input.Text : control.Name;
            }
            if (Element is NavigationView navigation) NativeExpanded = navigation.Expanded;
        }
        public void ValidatePages(IReadOnlyList<PageEntry> pages, ulong? selected)
        {
            if (Reject) throw new NotSupportedException("Native page composition or header capability rejects the snapshot.");
        }
        public void ValidateRange(NumericRange range, double value) { }
        public void ConnectPages(IPageViewElementPeer pages)
        {
            LinkedPages = (PagesPeer)pages;
        }
        public void ValidateVisibility(bool visible)
        {
            if (RejectVisibility) throw new InvalidOperationException("Native composing content cannot be hidden.");
        }
        public void AddChild(IElementPeer child) { var peer = (PagesPeer)child; peer.Parent = this; Children.Add(peer); }
        public void InsertChild(int index, IElementPeer child)
        {
            void AssertLinked(PagesPeer peer)
            {
                if (peer.Element is PageSelector) Assert(peer.LinkedPages is not null, "Dynamic selector links precede insertion.");
                foreach (var nested in peer.Children) AssertLinked(nested);
            }
            AssertLinked((PagesPeer)child);
            ((PagesPeer)child).Parent = this;
            Children.Insert(index, (PagesPeer)child);
        }
        public void RemoveChild(IElementPeer child)
        {
            Assert(!((PagesPeer)child).Events.Click(), "A retiring page root cannot deliver input during native removal.");
            Children.Remove((PagesPeer)child);
        }
        public void ValidateMove(IElementPeer child, int index) { }
        public void MoveChild(IElementPeer child, int index) { Children.Remove((PagesPeer)child); Children.Insert(index, (PagesPeer)child); }
        public void Update(ElementProperty property)
        {
            if (FailUpdate) throw new ApplicationException("native page update");
            Capture();
            if (property == ElementProperty.Text) TextUpdates++;
        }
        public bool TryFocus() { backend.FocusRequests++; return true; }
        public void Select(ulong id)
        {
            NativeSelected = id;
            ((IPageControlEvents)Events).PageSelected(id);
        }
        public void Activate(ulong id) => ((IPageControlEvents)Events).PageActivated(id);
        public void Close(ulong id) => ((IPageControlEvents)Events).PageCloseRequested(id);
        public void Dispose() => DisposeCalls++;
    }
}
