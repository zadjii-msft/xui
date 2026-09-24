using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void ShortHeightChecks()
    {
        HeightOnlyEditorRetention();
        HeightFocusAndPreflight();
        NativeSectionHideVeto();
        ShortHeightCancellationAccess();
    }
    private static void HeightOnlyEditorRetention()
    {
        using var h = new StudioHarness();
        h.Backend.Resize(914, 800);
        h.Dispatcher.Drain();
        var input = h.Backend.Find("doc-00001-title");
        var body = h.Backend.Find("doc-00001-body");
        Assert(body.Events.Change("Retain this draft through landscape and keyboard height changes."),
            "Native body edit before height transition.");
        h.Host.TryFocus((Control)body.Element);
        var roots = h.App.DocumentPages.Children.ToArray();
        var peerCount = h.Backend.Peers.Count;
        input.Updates.Clear(); body.Updates.Clear();
        h.Backend.Resize(914, 314);
        h.Dispatcher.Drain();
        Assert(h.App.LayoutMode == WidthMode.Medium && h.App.ShortHeight &&
            !h.App.SectionPages.Visible && h.App.DocumentPages.Visible && h.App.DocumentTabs.Visible,
            "Actual 914x314 allocation changes height policy despite staying in Medium width mode.");
        Assert(roots.SequenceEqual(h.App.DocumentPages.Children) && h.Backend.Peers.Count == peerCount &&
            ReferenceEquals(input, h.Backend.Find("doc-00001-title")) &&
            ReferenceEquals(body, h.Backend.Find("doc-00001-body")) && body.Focused &&
            input.Updates.Count == 0 && body.Updates.Count == 0,
            "Short-height chrome does not recreate, rewrite, hide, or refocus the native document editors.");
        Assert(h.App.Root.PaddingValue == 4 && h.App.Root.SpacingValue == 4 &&
            ((Xui.Experimental.Portable.Stack)roots[0]).PaddingValue == 4 &&
            ((Xui.Experimental.Portable.Stack)roots[0]).SpacingValue == 4,
            "Only shared layout spacing is reduced; native text fonts/caption geometry remain unchanged.");
        Assert(((Control)input.Element).Visible && ((TextInput)input.Element).CaptionVisible &&
            ((Control)body.Element).Visible && body.Element.Flex == 1 &&
            !h.App.TabLeftButton.Visible && !h.App.TabRightButton.Visible &&
            !h.App.StatusLabel.Visible && !h.Backend.Find("studio-layout").Element.AsVisible() &&
            !h.Backend.Find("doc-00001-draft-state").Element.AsVisible() &&
            !h.Backend.Find("doc-00001-analyze").Element.AsVisible() &&
            !h.Backend.Find("doc-00001-revert").Element.AsVisible(),
            "Redundant context, reorder, descriptive footer, and secondary editor toolbar yield height to the title/body.");
        Assert(!h.App.CatalogScroll.Visible || h.App.CatalogScroll.Flex == 1,
            "Catalog remains in a bounded independent viewport, never a whole-workspace unbounded scroll.");
        Assert(input.Events.Change("") && h.Backend.Find("doc-00001-validation").Element.AsVisible() &&
            ((Label)h.Backend.Find("doc-00001-validation").Element).Text == "Enter a document title.",
            "Invalid draft validation remains visible even when descriptive editor text is suppressed.");
        input.Events.Change("Restored title");
        h.Backend.Find("studio-navigation").ActivatePage(3);
        h.Dispatcher.Drain();
        Assert(h.App.DetailsShown && h.App.DetailsScroll.Visible && h.App.Columns[3].Value == 224 &&
            h.App.DocumentPages.Visible, "Short-height Medium Insights exposes scrollable Analyze/Cancel/Revert commands alongside the editor.");
        Assert(h.Backend.Find("studio-revert-active").Events.Click() &&
            h.Controller.State.Session.Document("doc-00001") == StudioCatalog.Original("doc-00001"),
            "Explicit revert remains reachable without moving/recreating the editor's native command tree.");
        h.Backend.Find("studio-navigation").ActivatePage(1);
        h.Backend.Resize(914, 250);
        h.Dispatcher.Drain();
        Assert(h.App.ShortHeight && h.App.DocumentPages.Visible && ReferenceEquals(body, h.Backend.Find("doc-00001-body")),
            "Keyboard-height reduction retains existing short-height editor objects.");
        h.Backend.Resize(914, 640);
        h.Dispatcher.Drain();
        Assert(!h.App.ShortHeight && h.App.SectionPages.Visible && h.App.StatusLabel.Visible &&
            h.App.TabLeftButton.Visible && h.App.TabRightButton.Visible &&
            h.Backend.Find("doc-00001-analyze").Element.AsVisible(),
            "Height-only expansion restores normal chrome at the declared threshold without changing width mode.");
        Assert(ReferenceEquals(input, h.Backend.Find("doc-00001-title")) && roots.SequenceEqual(h.App.DocumentPages.Children),
            "Restoring descriptive chrome preserves page/control identity.");
        h.Backend.Resize(914, 314); h.Dispatcher.Drain();
        h.Controller.OpenDocument("doc-00003");
        Assert(!h.Backend.Find("doc-00003-draft-state").Element.AsVisible() &&
            h.Backend.Find("doc-00003-title").Element.AsVisible(), "A newly opened document immediately inherits current height policy.");
    }
    private static void HeightFocusAndPreflight()
    {
        using var h = new StudioHarness();
        h.Backend.Resize(914, 800); h.Dispatcher.Drain();
        var toolbar = h.Backend.Find("doc-00001-analyze");
        toolbar.TryFocus();
        int focusRequests = h.Backend.Peers.Sum(peer => peer.FocusRequests);
        h.Backend.Resize(914, 314); h.Dispatcher.Drain();
        Assert(!h.App.ShortHeight && h.App.LayoutPending && toolbar.Focused &&
            h.Backend.Peers.Sum(peer => peer.FocusRequests) == focusRequests,
            "Hiding focused secondary chrome defers explicitly instead of stealing focus or disappearing an active command.");
        h.Host.TryFocus(h.App.ThemeButton);
        h.Backend.Find("studio-layout-retry").Events.Click();
        Assert(h.App.ShortHeight && !h.App.LayoutPending, "Explicit retry applies short chrome after the native focus owner changes.");
        h.Backend.Resize(914, 800); h.Dispatcher.Drain();
        var context = h.Backend.Peers.Single(peer => ReferenceEquals(peer.Element, h.App.SectionPages));
        context.RejectVisibility = true;
        h.Backend.Resize(914, 314); h.Dispatcher.Drain();
        Assert(h.App.LayoutPending && !h.App.ShortHeight && h.App.SectionPages.Visible,
            "Section context visibility preflight veto leaves height class and native layout coherent.");
        context.RejectVisibility = false;
        h.Backend.Find("studio-layout-retry").Events.Click();
        Assert(h.App.ShortHeight && !h.App.SectionPages.Visible && !h.App.LayoutPending,
            "Retry goes through actual page visibility preflight rather than zero-height hiding.");
        h.Backend.Resize(914, 800); h.Dispatcher.Drain();
        context.FailVisibility = true;
        var title = (Control)h.Backend.Find("doc-00001-title").Element;
        h.Backend.Resize(914, 314);
        var failure = Throws<KeyedUpdateException>(h.Dispatcher.Drain);
        Assert(failure.ModelCommitted && !h.Host.IsAttached && h.App.ShortHeight &&
            !h.App.SectionPages.Visible && title.Visible,
            "Postcommit native context-hide failure retains coherent short-height model and detaches explicitly.");
    }
    private static void NativeSectionHideVeto()
    {
        using var h = new StudioHarness();
        h.Backend.Resize(320, 700); h.Dispatcher.Drain();
        var title = h.Backend.Find("doc-00001-title");
        title.TryFocus();
        var pages = h.Backend.Peers.Single(peer => ReferenceEquals(peer.Element, h.App.DocumentPages));
        pages.RejectVisibility = true;
        int focusRequests = h.Backend.Peers.Sum(peer => peer.FocusRequests);
        Assert(h.Backend.Find("studio-navigation").ActivatePage(1) && h.App.DocumentPages.Visible &&
            h.App.LayoutPending && title.Focused &&
            h.Backend.Peers.Sum(peer => peer.FocusRequests) == focusRequests,
            "Native Library pointer activation while editor hide is vetoed is handled as pending, not an uncaught JNI callback failure.");
        Assert(h.App.LayoutStatus == "Navigation deferred by native editing. Finish the interaction, then apply the layout.",
            "Native composition deferral stays visible with an actionable retry, not a silent success.");
        pages.RejectVisibility = false;
        h.Host.TryFocus(h.App.Navigation);
        h.Backend.Find("studio-layout-retry").Events.Click();
        Assert(!h.App.LayoutPending && h.App.CatalogShown && !h.App.DocumentPages.Visible &&
            ReferenceEquals(title, h.Backend.Find("doc-00001-title")),
            "After genuine focus/preflight clearance the pending pane transition completes using the same retained editor.");
    }
    private static void ShortHeightCancellationAccess()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var service = new AnalysisService();
        var errors = new List<Exception>();
        var app = WorkspaceStudio.Create(host, service, errors.Add);
        var backend = new CatalogBackend(dispatcher);
        backend.Resize(914, 314);
        host.Attach(backend);
        app.AttachView();
        dispatcher.Drain();
        app.Controller.Analyze();
        var work = service.Pending[0];
        Assert(app.ShortHeight && app.ShortCancelButton.Visible && app.ShortCancelButton.Enabled &&
            backend.Find("studio-short-cancel").Events.Click() && work.Token.IsCancellationRequested,
            "The compressed shell exposes a real enabled cancellation command while asynchronous work is running.");
        work.Completion.SetCanceled(work.Token);
        dispatcher.Until(() => app.LastOperation.IsCompleted);
        app.LastOperation.GetAwaiter().GetResult();
        app.Controller.Analyze();
        service.Pending[1].Completion.SetException(new InvalidOperationException("Injected analysis failure."));
        dispatcher.Until(() => app.LastOperation.IsCompleted);
        Assert(app.ErrorLabel.Visible && app.ErrorLabel.Text.Length > 0 && errors.Count == 0 &&
            ((Control)backend.Find("doc-00001-title").Element).Visible &&
            ((Control)backend.Find("doc-00001-body").Element).Visible,
            "Short-height mode never hides errors or replaces the title/body to display them.");
    }
}

internal static class HeightTestElements
{
    internal static bool AsVisible(this Element element) => element is Control control && control.Visible;
}
