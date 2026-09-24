using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private sealed class ImmediateAnalysis : IStudioAnalysisService
    {
        public int Calls { get; private set; }
        public Task<StudioAnalysis> AnalyzeAsync(StudioDocumentDraft draft, CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Calls++;
            return Task.FromResult(StudioAnalysis.Calculate(draft));
        }
    }
    private sealed class StudioHarness : IDisposable
    {
        public Dispatcher Dispatcher { get; } = new();
        public Host Host { get; }
        public WorkspaceStudio App { get; }
        public WorkspaceStudioController Controller => App.Controller;
        public CatalogBackend Backend { get; }
        public ImmediateAnalysis Analysis { get; } = new();
        public List<Exception> Errors { get; } = [];
        public StudioHarness(WorkspaceStudioSession? session = null, IStudioOperationsService? operations = null,
            bool enableOperationsDrawer = false, bool supportsReveal = false, bool reducedMotion = false)
        {
            Host = new(Dispatcher);
            App = WorkspaceStudio.Create(Host, Analysis, Errors.Add, session, operations, enableOperationsDrawer);
            Backend = new(Dispatcher) { SupportsReveal = supportsReveal, ReducedMotion = reducedMotion };
            Host.Attach(Backend);
            App.AttachView();
            Dispatcher.Drain();
        }
        public void OpenDocument(string key)
        {
            App.Catalog.Reveal(key);
            Dispatcher.Drain();
            Assert(Backend.Find(key + "-open").Events.Click(), "Catalog activation uses a realized native button.");
            Dispatcher.Drain();
        }
        public void Finish()
        {
            var pending = App.LastOperation;
            Dispatcher.Until(() => pending.IsCompleted);
            pending.GetAwaiter().GetResult();
        }
        public void Dispose() => Host.Dispose();
    }
    private static void WorkspaceChecks()
    {
        WorkspaceRoundtrip();
        ResponsiveWorkspace();
        WorkspaceFailures();
        WorkspaceLimits();
        WorkspaceTextLayouts();
        using var driver = new StudioDriver();
        int count = WorkspaceStudioScenarioRunner.Run(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "WorkspaceStudioScenarios.json")), driver);
        assertions += count;
        Assert(count >= 35, "Workspace corpus contains literal native-linked navigation and tab expectations.");
        Console.WriteLine($"Studio corpus: {count} literal expectations.");
    }
    private static void WorkspaceRoundtrip()
    {
        using var h = new StudioHarness();
        Assert(h.App.DocumentTabs is TabStrip && h.App.Navigation is NavigationView &&
            h.App.DocumentPages.Pages.Count == 2 && h.App.DocumentPages.Selected == 1 &&
            h.App.SectionPages.Pages.Count == 3 && h.App.SectionPages.Selected == 1,
            "Workspace uses genuine linked native navigation, tab strip, and retained page graph.");
        Assert(ReferenceEquals(h.Backend.Find("studio-tabs").LinkedPages,
            h.Backend.Peers.Single(peer => ReferenceEquals(peer.Element, h.App.DocumentPages))),
            "Document tab peer links to the actual page peer rather than copied state or automation-ID matching.");
        Assert(h.App.Catalog.MountedCount <= 8 && h.Analysis.Calls == 0 &&
            h.Backend.Peers.Count(peer => !peer.Disposed && peer.Element is MultilineText) == 2,
            "10k workspace startup realizes bounded catalog rows and only the two open editors, with no automatic background work.");
        var firstBody = h.Backend.Find("doc-00001-body");
        var firstTitle = h.Backend.Find("doc-00001-title");
        var secondBody = h.Backend.Find("doc-00002-body");
        var firstRoot = h.App.DocumentPages.Children[0];
        var firstLifetime = h.Host.GetComponentLifetime(firstRoot);
        firstBody.Updates.Clear();
        Assert(firstBody.Events.Change("A retained editor\n- One local task"), "Real document text event accepted.");
        Assert(firstBody.Updates.Count == 0 && h.Controller.State.Session.HasDraft("doc-00001"), "Typing updates immutable draft without rewriting the editor.");
        var tabs = h.Backend.Find("studio-tabs");
        Assert(tabs.SelectPage(2) && h.Controller.State.Session.ActiveDocument == "doc-00002" &&
            h.App.DocumentPages.Selected == 2 && tabs.NativeSelected == 2, "Native tab selection commits one linked page selection then synchronizes app model.");
        Assert(!firstBody.Events.Change("hidden corruption") && !firstLifetime.Token.IsCancellationRequested &&
            !firstBody.Disposed, "Inactive document input is blocked while its editor and lifetime remain retained.");
        Assert(tabs.SelectPage(1) && ReferenceEquals(firstBody, h.Backend.Find("doc-00001-body")) &&
            ((MultilineText)firstBody.Element).Text == "A retained editor\n- One local task",
            "Re-selecting document retains the exact native editor and draft.");
        Assert(tabs.ActivatePage(1) && firstBody.Focused, "Explicit tab activation focuses the active native editor.");
        h.Controller.MoveTab("doc-00001", 1);
        Assert(h.App.DocumentPages.Pages.Select(page => page.Id).SequenceEqual([2UL, 1UL]) &&
            ReferenceEquals(firstBody, h.Backend.Find("doc-00001-body")) && ReferenceEquals(secondBody, h.Backend.Find("doc-00002-body")),
            "Tab reorder preserves both native editor instances and stable page IDs.");
        Assert(tabs.ClosePage(1) && firstLifetime.Token.IsCancellationRequested && firstBody.Disposed &&
            h.Controller.State.Session.HasDraft("doc-00001"), "Closing a native tab retires page lifetime but keeps independent unsaved draft.");
        h.OpenDocument("doc-00001");
        Assert(!ReferenceEquals(firstBody, h.Backend.Find("doc-00001-body")) &&
            ((MultilineText)h.Backend.Find("doc-00001-body").Element).Text == "A retained editor\n- One local task",
            "Closed document reopens as a fresh native page using the retained draft.");
        Assert(!firstTitle.Events.Change("stale title"), "Retired title callback cannot mutate reopened document.");
        Assert(h.Backend.Find("studio-navigation").ActivatePage(2) && h.Controller.State.Session.Section == StudioSection.Drafts,
            "Native workspace section selection drives draft catalog projection.");
        h.Dispatcher.Drain();
        Assert(h.Controller.VisibleKeys.SequenceEqual(["doc-00001"]) &&
            h.App.DocumentPages.Pages.Count == 2, "Section navigation changes catalog without recreating document tabs.");
        var currentBody = h.Backend.Find("doc-00001-body");
        var checkpoint = h.Controller.CaptureSession();
        h.Host.Detach();
        h.App.PrepareForAttachment();
        var replacement = new CatalogBackend(h.Dispatcher);
        h.Host.Attach(replacement);
        h.App.AttachView();
        h.Dispatcher.Drain();
        Assert(currentBody.Disposed && h.App.Catalog.IsReady && h.App.Catalog.MountedCount == 1 &&
            ((MultilineText)replacement.Find("doc-00001-body").Element).Text == checkpoint.Document("doc-00001").Body,
            "Full backend reattachment bootstraps the catalog safely while retaining document state.");
        h.Host.Dispose();
        using var restored = new StudioHarness(WorkspaceStudioSessionCodec.Restore(WorkspaceStudioSessionCodec.Serialize(checkpoint)));
        Assert(restored.Controller.State.Session.ActiveDocument == "doc-00001" &&
            restored.App.DocumentPages.Pages.Select(page => page.Id).SequenceEqual([2UL, 1UL]) &&
            ((MultilineText)restored.Backend.Find("doc-00001-body").Element).Text == "A retained editor\n- One local task" &&
            restored.Analysis.Calls == 0, "Fresh host recreates logical tabs, draft data, and section without automatic I/O or analysis.");
    }
    private static void ResponsiveWorkspace()
    {
        using var h = new StudioHarness();
        var body = h.Backend.Find("doc-00001-body");
        var pageRoot = h.App.DocumentPages.Children[0];
        var pagesPeer = h.Backend.Peers.Single(peer => ReferenceEquals(peer.Element, h.App.DocumentPages));
        body.Updates.Clear();
        foreach ((float width, WidthMode mode) in new[]
        {
            (1120f, WidthMode.Expanded), (1119f, WidthMode.Medium), (720f, WidthMode.Medium),
            (719f, WidthMode.Compact), (320f, WidthMode.Compact), (1280f, WidthMode.Expanded)
        })
        {
            h.Backend.Resize(width, 720);
            h.Dispatcher.Drain();
            Assert(h.App.LayoutMode == mode && ReferenceEquals(pageRoot, h.App.DocumentPages.Children[0]) &&
                ReferenceEquals(body, h.Backend.Find("doc-00001-body")) && body.Updates.Count == 0,
                "Responsive breakpoint preserves document page/editor identity and never rewrites its text.");
        }
        Assert(h.App.Columns.Length == 4 && h.App.Columns[2].Minimum == 0, "Responsive grid keeps four tracks and permits compact editor shrinkage.");
        h.Backend.Resize(320, 600);
        h.Dispatcher.Drain();
        Assert(!h.App.Navigation.Expanded && !h.App.CatalogScroll.Visible && !h.App.DetailsScroll.Visible &&
            h.App.DocumentPages.Visible && h.App.DocumentTabs.Visible, "Compact starts with genuine collapsed navigation and active editor pane.");
        Assert(h.App.Columns[0].Value == 48 && h.App.Columns[1].Value == 0 && h.App.Columns[2].Sizing == TrackSizing.Star &&
            h.App.Columns[3].Value == 0, "Compact grid allocates only navigation and current pane, without a 320px editor minimum overflow.");
        h.Backend.Find("studio-navigation").ActivatePage(1);
        h.Dispatcher.Drain();
        Assert(h.App.CatalogScroll.Visible && !h.App.DocumentPages.Visible && !h.App.DocumentTabs.Visible &&
            !pagesPeer.NativePageVisible && !body.Events.Change("hidden edit"),
            "Compact catalog hides native page input/accessibility without disposing editors or relying on zero-width fiction.");
        var oldBody = body;
        h.OpenDocument("doc-00003");
        Assert(h.App.DocumentPages.Visible && !h.App.CatalogScroll.Visible &&
            ReferenceEquals(oldBody, h.Backend.Find("doc-00001-body")), "Opening from compact library shows a real retained document pane.");
        h.Backend.Find("studio-navigation").ActivatePage(3);
        Assert(h.App.DetailsScroll.Visible && !h.App.DocumentPages.Visible && !h.App.CatalogScroll.Visible,
            "Compact Insights displays the inspector, not a fake navigation button page.");
        h.Backend.Resize(1280, 900);
        h.Dispatcher.Drain();
        Assert(h.App.DocumentPages.Visible && h.App.CatalogScroll.Visible && h.App.DetailsScroll.Visible, "Expanded restores all panes without recreating documents.");
        h.Backend.Find("studio-navigation").ActivatePage(1);
        h.Backend.Find("studio-tabs").ActivatePage(3);
        h.Backend.Find("studio-search").Focused = true;
        h.Backend.Resize(320, 600);
        h.Dispatcher.Drain();
        Assert(h.App.LayoutMode == WidthMode.Expanded && h.App.LayoutPending && h.App.CatalogScroll.Visible,
            "Resize defers hiding an auxiliary pane with a focused native search editor.");
        h.Backend.Find("studio-search").Focused = false;
        h.Backend.Find("studio-layout-retry").Events.Click();
        Assert(h.App.LayoutMode == WidthMode.Compact && !h.App.LayoutPending && h.App.DocumentPages.Visible,
            "Explicit retry applies pending responsive layout after focus is safe.");
        pagesPeer.RejectVisibility = true;
        Assert(h.Backend.Find("studio-navigation").ActivatePage(1), "Native Library activation is accepted without exposing a recoverable hide veto as a fatal callback.");
        Assert(h.App.DocumentPages.Visible && h.App.DocumentsShown && h.App.LayoutPending &&
            h.App.LayoutStatus == "Navigation deferred by native editing. Finish the interaction, then apply the layout.",
            "Rejected native page hide retains the editor and explicitly offers a deferred navigation retry.");
        pagesPeer.RejectVisibility = false;
        h.Backend.Find("studio-layout-retry").Events.Click();
        Assert(h.App.CatalogShown && !h.App.DocumentPages.Visible && !h.App.LayoutPending,
            "Retry completes the pending native pane navigation only after the editing veto is gone.");
        h.Backend.Find("studio-theme").Events.Click();
        Assert(h.Backend.Theme?.Mode == ThemeMode.Light && h.Backend.Theme.Resources.Accent?.Light == 0x335de0,
            "Theme command uses host-scoped semantic colors through the real theme capability.");
        h.Backend.Find("studio-theme").Events.Click();
        Assert(h.Backend.Theme?.Mode == ThemeMode.Dark && h.Backend.Theme.Resources.Background?.Dark == 0x111827 &&
            ReferenceEquals(body, h.Backend.Find("doc-00001-body")), "Dark theme changes resources without replacing retained editors.");
    }
    private static void WorkspaceFailures()
    {
        using var h = new StudioHarness();
        var pages = h.Backend.Peers.Single(peer => ReferenceEquals(peer.Element, h.App.DocumentPages));
        var tabs = h.Backend.Find("studio-tabs");
        var old = h.Controller.State;
        pages.RejectPages = true;
        var error = Throws<KeyedUpdateException>(() => tabs.SelectPage(2));
        Assert(!error.ModelCommitted && tabs.NativeSelected == 1 && h.App.DocumentPages.Selected == 1 &&
            h.Controller.State.Session.ActiveDocument == "doc-00001", "Native composition rejection restores selector/page/app identity coherently.");
        error = Throws<KeyedUpdateException>(() => h.Controller.OpenDocument("doc-00003"));
        Assert(!error.ModelCommitted && h.Controller.State == old && h.App.DocumentPages.Pages.Count == 2,
            "Failed document construction/selection preflight does not publish phantom open tabs.");
        pages.RejectPages = false;
        pages.FailPages = true;
        error = Throws<KeyedUpdateException>(() => h.Controller.OpenDocument("doc-00003"));
        Assert(error.ModelCommitted && !h.Host.IsAttached && h.Controller.State.Session.OpenTabs.Length == 3 &&
            h.App.DocumentPages.Pages.Count == 3 && h.App.DocumentPages.Selected == 3,
            "Postcommit native page failure retains matching document model and detaches instead of pretending rollback.");
        h.App.PrepareForAttachment();
        var recovered = new CatalogBackend(h.Dispatcher);
        h.Host.Attach(recovered);
        h.App.AttachView();
        h.Dispatcher.Drain();
        Assert(((TextInput)recovered.Find("doc-00003-title").Element).Text == "Design notes 00003" &&
            h.App.DocumentTabs.Selected == 3, "Explicit reattachment recovers the committed document/tab graph.");
    }
    private static void WorkspaceLimits()
    {
        using var h = new StudioHarness();
        for (int index = 2; index < 16; index++) h.Controller.OpenDocument(StudioCatalog.Entries[index].Key);
        Assert(h.App.DocumentPages.Pages.Count == 16 &&
            h.Backend.Peers.Count(peer => !peer.Disposed && peer.Element is MultilineText) == 16 &&
            h.App.Catalog.MountedCount <= 8, "Large catalog never causes unbounded editor-page realization.");
        int created = h.Backend.Peers.Count;
        h.Controller.OpenDocument("doc-00017");
        Assert(h.App.DocumentPages.Pages.Count == 16 && h.Backend.Peers.Count == created &&
            h.App.ErrorLabel.Text == "Close a tab before opening another document. Local drafts are kept.",
            "Open-editor limit is visible, creates no phantom tab, and preserves existing pages.");
        h.Controller.CloseTab("doc-00005");
        h.Controller.OpenDocument("doc-00017");
        Assert(h.App.DocumentPages.Selected == 17 &&
            h.Backend.Peers.Count(peer => !peer.Disposed && peer.Element is MultilineText) == 16,
            "Closing a tab releases its editor before another can be opened.");
        h.Controller.Analyze();
        h.Finish();
        var result = h.Controller.State.Analysis;
        h.Controller.ActivateTab("doc-00017");
        Assert(h.Controller.State.Analysis == result && result is not null,
            "Reselecting the same active document does not discard a valid local analysis.");
        h.Backend.Resize(320, 600);
        h.Dispatcher.Drain();
        h.Host.Dispose();
        h.Backend.Resize(1280, 900);
        h.Dispatcher.Drain();
        Assert(h.Backend.Disposed && h.Backend.Peers.All(peer => peer.Disposed) && h.Errors.Count == 0,
            "Retirement cleans every retained tab and ignores viewport callbacks after disposal.");
    }
    private static void WorkspaceTextLayouts()
    {
        using var h = new StudioHarness();
        Assert(h.App.StatusLabel.TextLayout == LabelTextLayout.Wrap(2) &&
            h.App.ErrorLabel.TextLayout == LabelTextLayout.Wrap(3), "Workspace footer uses real bounded native label wrapping.");
        Assert(((Label)h.Backend.Find("studio-local-notice").Element).TextLayout == LabelTextLayout.Wrap() &&
            ((Label)h.Backend.Find("studio-empty").Element).TextLayout == LabelTextLayout.Wrap(),
            "Scrollable inspector guidance and empty-state instructions wrap without source-text truncation.");
        Assert(((Label)h.Backend.Find("studio-title").Element).TextLayout ==
            LabelTextLayout.SingleLine(TextOverflow.CharacterEllipsis), "Compact brand requests real native single-line ellipsis.");
        var category = h.Backend.Find("doc-00001-category");
        Assert(((Label)category.Element).TextLayout == LabelTextLayout.SingleLine(TextOverflow.CharacterEllipsis),
            "Fixed-pitch catalog caption uses explicit single-line native overflow policy.");
        var status = h.App.StatusLabel.Text;
        h.Backend.Resize(320, 500);
        h.Dispatcher.Drain();
        Assert(h.App.StatusLabel.Text == status, "Narrow layout never rewrites label source or accessibility text to simulate wrapping.");
        using var unsupportedHost = new Host(new Dispatcher());
        _ = WorkspaceStudio.Create(unsupportedHost, new ImmediateAnalysis(), _ => { });
        var unsupported = new NoTextLayoutBackend();
        var error = Throws<NotSupportedException>(() => unsupportedHost.Attach(unsupported));
        Assert(error.Message.Contains("ITextLayoutPeer", StringComparison.Ordinal),
            "Capability rejection specifically identifies missing native text layout rather than an unrelated feature.");
        Assert(unsupported.Disposed && unsupported.Peers.All(peer => peer.Disposed),
            "Missing native label-layout capability rejects attachment explicitly and releases partial peers.");
    }
    private sealed class NoTextLayoutBackend : IBackend
    {
        public List<NoTextLayoutPeer> Peers { get; } = [];
        public bool Disposed { get; private set; }
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new NoTextLayoutPeer();
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) => throw new InvalidOperationException("Unsupported text layout must not mount.");
        public void Dispose() => Disposed = true;
    }
    private sealed class NoTextLayoutPeer : IElementPeer, IPresentationPeer
    {
        public bool Disposed { get; private set; }
        public void AddChild(IElementPeer child) { }
        public void Update(ElementProperty property) { }
        public void ValidateTypography(Typography? typography) { }
        public void Dispose() => Disposed = true;
    }
    private sealed class StudioDriver : IWorkspaceStudioScenarioDriver, IDisposable
    {
        private StudioHarness? current;
        private StudioHarness Current => current ?? throw new InvalidOperationException("Create a workspace first.");
        public void CreateFreshWorkspace() { current?.Dispose(); current = new(); }
        public void SelectSection(ulong id)
        {
            Assert(Current.Backend.Find("studio-navigation").ActivatePage(id), "Native navigation activation accepted.");
            Current.Dispatcher.Drain();
        }
        public void SelectTab(ulong id)
        {
            Assert(Current.Backend.Find("studio-tabs").SelectPage(id), "Native tab selection accepted.");
            Current.Dispatcher.Drain();
        }
        public void CloseTab(ulong id)
        {
            Assert(Current.Backend.Find("studio-tabs").ClosePage(id), "Native tab close request accepted.");
            Current.Dispatcher.Drain();
        }
        public void SelectOperationsScope(ulong id)
        {
            Assert(((ISelectionControlEvents)Current.Backend.Find("operations-scope").Events).SelectionChanged(id),
                "Native operations dataset choice accepted.");
            Current.Dispatcher.Drain();
        }
        public void OpenDocument(string key) => Current.OpenDocument(key);
        public void Change(string id, string value)
        {
            Assert(Current.Backend.Find(id).Events.Change(value), "Native workspace edit accepted.");
            Current.Dispatcher.Drain();
        }
        public void Click(string id)
        {
            Assert(Current.Backend.Find(id).Events.Click(), "Native workspace command accepted.");
            Current.Dispatcher.Drain();
        }
        public void AwaitAnalysis() => Current.Finish();
        public bool NavigateBack()
        {
            bool handled = Current.App.TryNavigateBack();
            Current.Dispatcher.Drain();
            return handled;
        }
        public bool BackBlocked() => Current.App.BackBlocked;
        public string Text(string id) => Current.Backend.Find(id).Element switch
        {
            TextInput input => input.Text,
            MultilineText input => input.Text,
            Control control => control.Name,
            _ => throw new InvalidOperationException("Not a text element.")
        };
        public bool Enabled(string id) => ((Control)Current.Backend.Find(id).Element).Enabled;
        public int OpenTabCount() => Current.Backend.Find("studio-tabs").NativeEntries.Length;
        public ulong? SelectedTab() => Current.Backend.Find("studio-tabs").NativeSelected;
        public void Dispose() => current?.Dispose();
    }
}
