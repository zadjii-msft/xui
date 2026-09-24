using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void BackChecks()
    {
        CompactBackChecks();
        SectionBackChecks();
        BlockedBackChecks();
        BackFailureChecks();
        BackDuringOperationsChecks();
    }

    private static void CompactBackChecks()
    {
        using var h = new StudioHarness();
        h.Backend.Resize(320, 600);
        h.Dispatcher.Drain();
        h.Backend.Find("doc-00001-body").Events.Change("Draft must survive Back.\n- Pending item");
        var before = h.Controller.State.Session;
        var pages = h.App.DocumentPages.Children.ToArray();
        var input = h.Backend.Find("doc-00001-body");
        Task.Run(() => Throws<InvalidOperationException>(() => h.App.TryNavigateBack())).GetAwaiter().GetResult();
        h.Backend.Find("studio-tabs").ActivatePage(1);
        Assert(input.Focused && h.App.DocumentsShown, "Back begins in a focused compact document editor.");
        Assert(h.App.TryNavigateBack() && !h.App.BackBlocked && h.App.CatalogShown &&
            !h.App.DocumentsShown && !h.App.DetailsShown && h.Backend.Find("studio-navigation").Focused,
            "Compact Document Back reaches current Library catalog and repairs focus to real navigation.");
        Assert(ReferenceEquals(before, h.Controller.State.Session) && pages.SequenceEqual(h.App.DocumentPages.Children) &&
            !input.Disposed && h.Controller.State.Session.Document("doc-00001").Body == "Draft must survive Back.\n- Pending item",
            "Pane Back does not select/close/reorder a tab, discard a draft, or replace an editor.");
        Assert(!h.App.TryNavigateBack() && !h.App.BackBlocked && h.Host.IsAttached &&
            h.Controller.State.Session.OpenTabs.Length == 2, "Library catalog is the compact root: false delegates to host without closing anything.");
        h.OpenDocument("doc-00001");
        h.Backend.Find("studio-navigation").ActivatePage(2);
        h.Dispatcher.Drain();
        Assert(h.Controller.State.Session.Section == StudioSection.Drafts && h.App.CatalogShown,
            "Native Drafts section establishes its catalog landing pane.");
        h.OpenDocument("doc-00001");
        Assert(h.App.TryNavigateBack() && h.App.CatalogShown &&
            h.Controller.State.Session.Section == StudioSection.Drafts, "Document Back returns to the current Drafts catalog, not an invented route history.");
        Assert(h.App.TryNavigateBack() && h.Controller.State.Session.Section == StudioSection.Library &&
            h.App.CatalogShown && h.App.SectionPages.Selected == 1, "Back from Drafts catalog selects genuine Library page identity.");
        Assert(!h.App.TryNavigateBack(), "A following Back at Library catalog is unhandled.");

        h.Backend.Find("studio-navigation").ActivatePage(3);
        Assert(h.App.DetailsShown && h.Controller.State.Session.Section == StudioSection.Insights, "Insights activates compact details.");
        Assert(h.App.TryNavigateBack() && h.Controller.State.Session.Section == StudioSection.Library &&
            h.App.CatalogShown && !h.App.DetailsShown, "Details Back returns to Library catalog without implicitly opening or closing a document.");
        h.Backend.Find("studio-navigation").ActivatePage(3);
        h.Controller.OpenOperations();
        var operationPage = h.App.OperationsPage!;
        Assert(h.App.DocumentsShown && h.Controller.State.Session.Section == StudioSection.Insights, "Operations opened from Insights uses the document pane.");
        Assert(h.App.TryNavigateBack() && h.App.DetailsShown && !h.App.DocumentsShown &&
            h.App.DocumentTabs.Selected == 10001 && !operationPage.Lifetime.Token.IsCancellationRequested,
            "Operations/document Back within Insights returns to Details while retaining the selected page.");
        Assert(h.App.TryNavigateBack() && h.App.CatalogShown && h.Controller.State.Session.Section == StudioSection.Library &&
            h.Controller.State.Session.HasDraft("doc-00001"), "Next Back reaches Library with all drafts intact.");
        h.Controller.OpenOperations();
        Assert(h.App.DocumentsShown && h.App.DocumentPages.Selected == 10001 &&
            ReferenceEquals(operationPage, h.App.OperationsPage),
            "Opening the already selected Operations tab after Back reveals the same retained dashboard rather than remaining on catalog.");
    }

    private static void SectionBackChecks()
    {
        using var h = new StudioHarness();
        foreach (float width in new[] { 720f, 1280f })
        {
            h.Backend.Resize(width, 800);
            h.Dispatcher.Drain();
            Assert(!h.App.TryNavigateBack(), "Medium/Expanded Library root leaves Back to host policy.");
            var input = h.Backend.Find("doc-00001-body");
            h.Backend.Find("studio-navigation").ActivatePage(2);
            h.Backend.Find("studio-tabs").ActivatePage(1);
            Assert(h.App.TryNavigateBack() && h.Controller.State.Session.Section == StudioSection.Library &&
                h.App.DocumentsShown && ReferenceEquals(input, h.Backend.Find("doc-00001-body")),
                "Desktop section Back returns Library without hiding/recreating the retained document pane.");
            h.Backend.Find("studio-navigation").ActivatePage(3);
            Assert(h.App.TryNavigateBack() && h.App.SectionPages.Selected == 1 &&
                h.App.DocumentPages.Selected == 1, "Insights Back resets only section identity, never selected document.");
        }
        using var empty = new StudioHarness(new WorkspaceStudioSession(2, StudioSection.Insights, StudioCategory.All, "",
            [], "", "", [], false));
        empty.Backend.Resize(320, 600);
        empty.Dispatcher.Drain();
        Assert(empty.App.DetailsShown && empty.App.TryNavigateBack() && empty.App.CatalogShown &&
            empty.Controller.State.Session.OpenTabs.IsEmpty, "Empty Insights Back reaches catalog without fabricating an editor.");
        using var sectionRejected = new StudioHarness();
        sectionRejected.Backend.Find("studio-navigation").ActivatePage(2);
        var sectionPages = sectionRejected.Backend.Peers.Single(peer => ReferenceEquals(peer.Element, sectionRejected.App.SectionPages));
        sectionPages.RejectPages = true;
        Assert(sectionRejected.App.TryNavigateBack() && sectionRejected.App.BackBlocked &&
            sectionRejected.Controller.State.Session.Section == StudioSection.Drafts &&
            sectionRejected.App.SectionPages.Selected == 2 && sectionRejected.App.Snapshot.Session.Section == StudioSection.Drafts,
            "Native section selection preflight rejection keeps application and linked navigation identity together.");
        sectionPages.RejectPages = false;
        Assert(sectionRejected.App.TryNavigateBack() && !sectionRejected.App.BackBlocked &&
            sectionRejected.Controller.State.Session.Section == StudioSection.Library,
            "Section Back can be retried explicitly after native preflight becomes available.");
    }

    private static void BlockedBackChecks()
    {
        using var h = new StudioHarness();
        h.Backend.Resize(320, 600);
        h.Dispatcher.Drain();
        var pages = h.Backend.Peers.Single(peer => ReferenceEquals(peer.Element, h.App.DocumentPages));
        var navigation = h.Backend.Find("studio-navigation");
        var body = h.Backend.Find("doc-00001-body");
        h.Backend.Find("studio-tabs").ActivatePage(1);
        int focusRequests = navigation.FocusRequests;
        var before = h.Controller.State;
        pages.RejectVisibility = true;
        Assert(h.App.TryNavigateBack() && h.App.BackBlocked && h.App.DocumentsShown &&
            h.App.DocumentPages.Visible && navigation.FocusRequests == focusRequests && body.Focused &&
            h.Controller.State == before, "Native composition veto consumes Back without forcing focus, changing pane, or falling through to OS.");
        Assert(h.App.BackStatus == "Back was blocked by native page readiness or editing. Finish the interaction, then try Back again.",
            "Blocked Back has an explicit visible reason.");
        Assert(h.App.BackNotice && ((Control)h.Backend.Find("studio-layout").Element).Visible,
            "Short-height chrome never hides a blocked Back diagnostic.");
        pages.RejectVisibility = false;
        Assert(h.App.TryNavigateBack() && !h.App.BackBlocked && h.App.CatalogShown, "A later Back retries through the same native preflight after composition ends.");

        var search = h.Backend.Find("studio-search");
        search.TryFocus();
        Assert(h.App.SearchInput.Interaction is null && h.App.TryNavigateBack() && h.App.BackBlocked &&
            navigation.FocusRequests == focusRequests + 1, "Unknown focused search interaction cannot be coerced into ending composition or exiting.");
        ((ITextInteractionEvents)search.Events).InteractionChanged(new(true, true));
        Assert(h.App.TryNavigateBack() && h.App.BackBlocked && search.Focused &&
            h.Controller.State.Session.Section == StudioSection.Library, "Known active search composition blocks even root fall-through without forcing cancellation.");
        ((ITextInteractionEvents)search.Events).InteractionChanged(new(true, false));
        Assert(!h.App.TryNavigateBack() && !h.App.BackBlocked && search.Focused,
            "At root, settled native search allows host Back while retaining its current focus.");
        h.Backend.Find("studio-navigation").ActivatePage(2);
        ((ITextInteractionEvents)search.Events).InteractionChanged(new(true, false));
        Assert(h.App.TryNavigateBack() && h.App.CatalogShown && search.Focused &&
            h.Controller.State.Session.Section == StudioSection.Library,
            "Section-only Back keeps a visible noncomposing search editor instead of stealing focus.");
        search.Focused = false;
        h.Backend.Resize(1280, 900);
        h.Dispatcher.Drain();
        h.Backend.Find("studio-tabs").ActivatePage(1);
        search.Focused = true;
        h.Backend.Resize(320, 600);
        h.Dispatcher.Drain();
        Assert(h.App.LayoutPending && h.App.TryNavigateBack() && h.App.BackBlocked &&
            h.App.BackStatus.StartsWith("Back is waiting for a pending layout change.", StringComparison.Ordinal),
            "A deferred responsive transition is not mistaken for an OS-level root Back.");

        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var app = WorkspaceStudio.Create(host, new ImmediateAnalysis(), _ => { });
        Throws<InvalidOperationException>(() => app.TryNavigateBack());
        var backend = new CatalogBackend(dispatcher);
        host.Attach(backend);
        app.AttachView();
        Assert(app.TryNavigateBack() && app.BackBlocked &&
            app.BackStatus.StartsWith("Back is waiting for the initial workspace layout.", StringComparison.Ordinal),
            "Startup Back waits for the actual viewport instead of guessing phone versus desktop.");
        dispatcher.Drain();
        Assert(!app.TryNavigateBack(), "After initial desktop allocation, Library correctly delegates Back.");
        host.Detach();
        Throws<InvalidOperationException>(() => app.TryNavigateBack());
        host.Dispose();
        Throws<ObjectDisposedException>(() => app.TryNavigateBack());
    }

    private static void BackFailureChecks()
    {
        using var h = new StudioHarness();
        h.Backend.Resize(320, 600);
        h.Dispatcher.Drain();
        var pages = h.Backend.Peers.Single(peer => ReferenceEquals(peer.Element, h.App.DocumentPages));
        pages.FailVisibility = true;
        var error = Throws<KeyedUpdateException>(() => h.App.TryNavigateBack());
        Assert(error.ModelCommitted && !h.Host.IsAttached && !h.App.DocumentPages.Visible &&
            !h.App.DocumentsShown && h.App.CatalogShown && h.App.Columns[1].Sizing == TrackSizing.Star,
            "Postcommit Back visibility failure retains the committed pane model while detaching; it never returns false or claims rollback.");
        h.App.PrepareForAttachment();
        var replacement = new CatalogBackend(h.Dispatcher);
        replacement.Resize(320, 600);
        h.Host.Attach(replacement);
        h.App.AttachView();
        h.Dispatcher.Drain();
        Assert(h.App.CatalogShown && !h.App.DocumentsShown && !h.App.TryNavigateBack(),
            "Explicit attachment recovery keeps the committed catalog pane and root Back policy coherent.");
        h.Controller.ActivateTab("doc-00002");
        var navigation = replacement.Find("studio-navigation");
        navigation.RejectFocus = true;
        Throws<InvalidOperationException>(() => h.App.TryNavigateBack());
        Assert(h.App.CatalogShown && !h.App.DocumentsShown && h.App.BackStatus ==
            "Back changed the workspace pane, but native navigation focus was not accepted.",
            "Rejected focus repair is explicit after committed navigation, never a false OS-close result.");
    }

    private static void BackDuringOperationsChecks()
    {
        var service = new PendingOperations();
        using var h = new StudioHarness(OperationsDraft(), service);
        h.Backend.Resize(320, 600);
        h.Dispatcher.Drain();
        var operations = h.App.OperationsPage!;
        operations.Controller.SetScope(StudioOperationsScope.LocalDrafts);
        operations.Controller.Scan();
        var work = service.Pending[0];
        var tabs = h.Controller.State.Session.OpenTabs;
        Assert(h.App.TryNavigateBack() && h.App.CatalogShown && operations.Snapshot.Busy &&
            !work.Token.IsCancellationRequested && !operations.Lifetime.Token.IsCancellationRequested &&
            h.Controller.State.Session.OpenTabs == tabs, "Back hides but does not close a busy retained Operations page or cancel its owned work.");
        Assert(!h.App.TryNavigateBack() && !work.Token.IsCancellationRequested && h.Host.IsAttached,
            "Root false delegates the host's exit decision; it does not itself tear down work or drafts.");
        Task pending = h.App.LastOperation;
        h.Host.Dispose();
        Assert(work.Token.IsCancellationRequested, "Only subsequent explicit host retirement cancels the page's pending work.");
        work.Completion.SetResult(new LocalStudioOperationsService().ScanAsync(work.Request, CancellationToken.None).GetAwaiter().GetResult());
        h.Dispatcher.Until(() => pending.IsCompleted);
        pending.GetAwaiter().GetResult();
        Assert(h.Errors.Count == 0, "Back plus explicit root retirement leaves no unobserved completion.");
    }
}
