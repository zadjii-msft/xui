using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void OperationsDrawerChecks()
    {
        DefaultOperationsControls();
        RetainedOperationsDrawer();
        BusyOperationsDrawer();
        OperationsDrawerFailures();
        NativeDrawerTabActivation();
        BusyNativeOperationsActivation();
    }

    private static void DefaultOperationsControls()
    {
        using var h = new StudioHarness(OperationsDraft());
        var page = h.App.OperationsPage!;
        Assert(!page.DrawerEnabled && page.ControlsReveal is null &&
            h.Backend.Peers.All(peer => peer.Element is not Reveal) && h.Backend.RevealCreations == 0,
            "Default constructor introduces no Reveal element, capability, animation, or fallback requirement.");
        Assert(!((Control)h.Backend.Find("operations-controls-toggle").Element).Visible &&
            page.ScopeInput.AutomationId == "operations-scope" && page.ScopeInput.Enabled,
            "Existing scope/actions remain the same naturally sized controls with the motion command absent.");
        Throws<InvalidOperationException>(() => page.TrySetControlsOpen(false));
        ((ISelectionControlEvents)h.Backend.Find("operations-scope").Events).SelectionChanged(3);
        h.Backend.Find("operations-scan").Events.Click();
        h.Finish();
        Assert(page.Snapshot.Report is { Documents: 2, Words: 11 } && page.ScopeInput.Selected == 3 &&
            ((Control)h.Backend.Find("operations-status").Element).Name ==
                "Local operations snapshot complete. No cloud metrics or user files were accessed.",
            "Default shared controls retain their native identities and existing business behavior.");
    }

    private static void RetainedOperationsDrawer()
    {
        using var h = new StudioHarness(OperationsDraft(), enableOperationsDrawer: true, supportsReveal: true);
        var page = h.App.OperationsPage!;
        var reveal = page.ControlsReveal ?? throw new InvalidOperationException("Opt-in must construct actual Reveal.");
        var peer = (RevealCatalogPeer)h.Backend.Peers.Single(candidate => ReferenceEquals(candidate.Element, reveal));
        Assert(page.DrawerEnabled && page.DrawerOpen && reveal.Open &&
            reveal.Motion == new RevealMotion(180, RevealDirection.Bottom) &&
            h.Host.GetRevealPresentation(reveal) == new RevealPresentation(1, false),
            "Explicit opt-in starts logically open with a settled native presentation and 180ms downward expansion.");
        Assert(reveal.FixedSize is null && reveal.PreferredSize is null && reveal.WidthConstraints is null &&
            reveal.HeightConstraints is null && reveal.Flex == 0 && reveal.Children.Count == 1 &&
            reveal.Content is Xui.Experimental.Portable.Stack,
            "Outer Reveal has no sizing/flex constraints and owns exactly one naturally bounded retained controls stack.");
        var children = Subtree(reveal.Content).ToArray();
        Assert(children.OfType<MultilineText>().Count() == 0 && children.OfType<ScrollView>().Count() == 0 &&
            !children.Contains(h.App.CatalogScroll) &&
            h.Backend.Peers.Where(p => p.Element is MultilineText).All(p => !children.Contains(p.Element)),
            "Main document editors and virtual viewport are completely outside the reveal subtree.");
        var scope = h.Backend.Find("operations-scope");
        var scan = h.Backend.Find("operations-scan");
        var editor = h.Backend.Find("doc-00001-body");
        var controlsRoot = reveal.Content;
        var lifetime = h.Host.GetComponentLifetime(controlsRoot);
        Assert(scope.TryFocus(), "Native scope focus accepted.");
        int focusRequests = h.Backend.Peers.Sum(p => p.FocusRequests);
        Assert(!page.TrySetControlsOpen(false) && page.DrawerOpen && reveal.Open &&
            scope.Focused && h.Backend.Peers.Sum(p => p.FocusRequests) == focusRequests &&
            page.DrawerFeedback == "Controls remain open. Finish native editing or close the open choice popup, then try again.",
            "Close veto exposes feedback without stealing focus, dismissing popup, forcing composition, or claiming closure.");
        Assert(h.Host.TryFocus(page.DrawerToggleButton), "External drawer toggle receives focus without forcing descendant composition.");
        peer.RejectClose = true;
        Assert(!page.TrySetControlsOpen(false) && reveal.Open && page.DrawerOpen,
            "Native popup or IME veto remains authoritative after focus moves.");
        peer.RejectClose = false;
        Assert(h.Backend.Find("operations-controls-toggle").Events.Click() && !page.DrawerOpen && !reveal.Open &&
            !((ISelectionControlEvents)scope.Events).SelectionChanged(2) && !scan.Events.Click(),
            "Native toggle request closes logical input immediately while retaining every control.");
        Assert(ReferenceEquals(controlsRoot, reveal.Content) && !lifetime.Token.IsCancellationRequested &&
            ReferenceEquals(scope, h.Backend.Find("operations-scope")) &&
            ReferenceEquals(editor, h.Backend.Find("doc-00001-body")) &&
            page.DrawerFeedback == "Scan controls collapsed. Snapshot results are unchanged.",
            "Closing retains control/page/editor identities and data, with explicit feedback.");
        Assert(!h.Host.TryFocus(page.ScopeInput),
            "Logical closure rejects programmatic descendant focus immediately, not only after native animation completes.");
        peer.Presentation = new(.42f, true);
        Assert(h.Host.GetRevealPresentation(reveal) == new RevealPresentation(.42f, true) && !page.DrawerOpen,
            "Motion progress is read from actual native presentation and is independent of the logical open state.");
        peer.Presentation = new(0, false);
        Assert(h.Backend.Find("operations-controls-toggle").Events.Click() && page.DrawerOpen &&
            reveal.Open && ReferenceEquals(scope, h.Backend.Find("operations-scope")),
            "Reopening uses the same shared native controls rather than reconstructing a motion-only UI.");
        ((ISelectionControlEvents)scope.Events).SelectionChanged(3);
        scan.Events.Click();
        h.Finish();
        var report = page.Snapshot.Report;
        var resultRoots = page.WorkloadRows.Children.ToArray();
        Assert(h.Host.TryFocus(page.DrawerToggleButton), "Focus stays outside the drawer before a user-requested close.");
        Assert(page.TrySetControlsOpen(false) && ReferenceEquals(report, page.Snapshot.Report) &&
            resultRoots.SequenceEqual(page.WorkloadRows.Children), "Drawer commands never recalculate or replace metric/workload snapshots.");
        h.Controller.ActivateTab("doc-00001");
        h.Controller.SetBody("doc-00001", "Edited while retained controls are collapsed.");
        h.Controller.ActivateTab(StudioTabs.OperationsKey);
        Assert(!page.DrawerOpen && !reveal.Open && page.Snapshot.Stale &&
            ReferenceEquals(scope, h.Backend.Find("operations-scope")) && page.ScopeInput.Selected == 3,
            "Background source refresh and genuine tab changes do not reopen, replace, or reset the collapsed controls.");
        h.Backend.Resize(320, 600);
        h.Dispatcher.Drain();
        Assert(!page.DrawerOpen && !reveal.Open && ReferenceEquals(scope, h.Backend.Find("operations-scope")) &&
            h.App.LayoutMode == WidthMode.Compact, "Responsive metrics reflow preserves native drawer state and inputs.");
        h.Host.Detach();
        h.App.PrepareForAttachment();
        var replacement = new CatalogBackend(h.Dispatcher) { SupportsReveal = true, ReducedMotion = true };
        replacement.Resize(320, 600);
        h.Host.Attach(replacement);
        h.App.AttachView();
        h.Dispatcher.Drain();
        Assert(!reveal.Open && h.Host.GetRevealPresentation(reveal) == new RevealPresentation(0, false),
            "Reattachment starts at retained closed state without replaying a managed animation.");
        Assert(page.TrySetControlsOpen(true) && h.Host.GetRevealPresentation(reveal) == new RevealPresentation(1, false),
            "Native reduced-motion policy can settle immediately; the shared app neither schedules ticks nor overrides it.");
        var pending = h.App.LastOperation;
        h.Host.Dispose();
        h.Dispatcher.Until(() => pending.IsCompleted);
        Assert(lifetime.Token.IsCancellationRequested && replacement.Peers.All(p => p.Disposed),
            "Retirement releases the retained controls and native presentation with normal workspace ownership.");
        Throws<ObjectDisposedException>(() => page.TrySetControlsOpen(false));
    }

    private static void BusyOperationsDrawer()
    {
        var service = new PendingOperations();
        using var h = new StudioHarness(OperationsDraft(), service, enableOperationsDrawer: true, supportsReveal: true);
        var page = h.App.OperationsPage!;
        var peer = (RevealCatalogPeer)h.Backend.Peers.Single(p => ReferenceEquals(p.Element, page.ControlsReveal));
        page.Controller.Scan();
        int closes = peer.CloseRequests;
        Assert(!page.TrySetControlsOpen(false) && page.DrawerOpen &&
            peer.CloseRequests == closes && !page.DrawerToggleButton.Enabled &&
            page.DrawerFeedback == "Finish or cancel the scan before hiding its controls.",
            "App policy keeps Cancel visible while a real scan is running, without fabricating a native veto.");
        var work = service.Pending[0];
        h.Backend.Find("operations-cancel").Events.Click();
        Assert(work.Token.IsCancellationRequested && page.DrawerOpen && page.Snapshot.Busy,
            "Drawer controls still expose actual producer cancellation.");
        work.Completion.SetCanceled(work.Token);
        h.Finish();
        Assert(page.DrawerToggleButton.Enabled && page.TrySetControlsOpen(false),
            "Once the producer settles, normal native close preflight is available again.");
    }

    private static void OperationsDrawerFailures()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var app = WorkspaceStudio.Create(host, new ImmediateAnalysis(), _ => { }, OperationsDraft(),
            enableOperationsDrawer: true);
        var unsupported = new CatalogBackend(dispatcher);
        var unsupportedError = Throws<NotSupportedException>(() => host.Attach(unsupported));
        Assert(unsupportedError.Message.Contains("IRevealElementPeer", StringComparison.Ordinal) &&
            unsupported.Disposed && unsupported.Peers.All(p => p.Disposed) && !host.IsAttached,
            "Opt-in rejects an unqualified native backend explicitly, with no silent animation or control fallback.");
        using var openedLater = new StudioHarness(enableOperationsDrawer: true);
        Assert(openedLater.Backend.Peers.All(p => p.Element is not Reveal),
            "Opt-in does not create a phantom controls drawer before the Operations page exists.");
        var lateError = Throws<KeyedUpdateException>(() => openedLater.Controller.OpenOperations());
        Assert(lateError.ModelCommitted && !openedLater.Host.IsAttached &&
            openedLater.Controller.State.Session.ActiveDocument == StudioTabs.OperationsKey,
            "Unsupported dynamic Operations creation is an explicit postcommit attachment failure, not a fallback or fake success.");

        using var h = new StudioHarness(OperationsDraft(), enableOperationsDrawer: true, supportsReveal: true);
        var page = h.App.OperationsPage!;
        var reveal = page.ControlsReveal!;
        var peer = (RevealCatalogPeer)h.Backend.Peers.Single(p => ReferenceEquals(p.Element, reveal));
        peer.FailRevealUpdate = true;
        var error = Throws<KeyedUpdateException>(() => page.TrySetControlsOpen(false));
        Assert(error.ModelCommitted && !h.Host.IsAttached && !reveal.Open && !page.DrawerOpen &&
            h.Controller.State.Session.OpenTabs.Contains("operations"),
            "Postcommit native motion failure detaches while retaining the real logical closed state and workspace tab.");
        h.App.PrepareForAttachment();
        var replacement = new CatalogBackend(h.Dispatcher) { SupportsReveal = true };
        h.Host.Attach(replacement);
        h.App.AttachView();
        h.Dispatcher.Drain();
        Assert(h.Host.GetRevealPresentation(reveal) == new RevealPresentation(0, false) &&
            !replacement.Find("operations-scan").Events.Click(), "Recovery mounts the committed closed controls without stale input or replay.");
    }

    private static void NativeDrawerTabActivation()
    {
        using var h = new StudioHarness(OperationsDraft(), enableOperationsDrawer: true, supportsReveal: true);
        var page = h.App.OperationsPage!;
        var reveal = page.ControlsReveal!;
        var scope = h.Backend.Find("operations-scope");
        var toggle = h.Backend.Find("operations-controls-toggle");
        var tabs = h.Backend.Find("studio-tabs");
        var controls = reveal.Content;
        Assert(h.Host.TryFocus(page.DrawerToggleButton) && page.TrySetControlsOpen(false),
            "Native tab activation regression begins with retained closed controls.");
        Assert(tabs.ActivatePage(1) && h.Backend.Find("doc-00001-body").Focused,
            "Native document activation moves focus into its visible editor.");
        int scopeFocusRequests = scope.FocusRequests;
        Assert(tabs.ActivatePage(StudioTabs.OperationsId) && toggle.Focused &&
            !scope.Focused && scope.FocusRequests == scopeFocusRequests,
            "Genuine Operations PageActivated focuses the visible external toggle, never a closed scope editor.");
        Assert(!page.DrawerOpen && !reveal.Open && ReferenceEquals(controls, reveal.Content) &&
            ReferenceEquals(scope, h.Backend.Find("operations-scope")) &&
            h.Controller.State.Session.ActiveDocument == StudioTabs.OperationsKey &&
            h.App.DocumentPages.Selected == StudioTabs.OperationsId,
            "Activation preserves closed drawer, retained controls, and authoritative document selection.");
        Assert(tabs.ActivatePage(1), "Return to document before testing native selection preflight.");
        var pages = h.Backend.Peers.Single(peer => ReferenceEquals(peer.Element, h.App.DocumentPages));
        int requests = h.Backend.Peers.Sum(peer => peer.FocusRequests);
        pages.RejectPages = true;
        var veto = Throws<KeyedUpdateException>(() => tabs.ActivatePage(StudioTabs.OperationsId));
        Assert(!veto.ModelCommitted && h.Controller.State.Session.ActiveDocument == "doc-00001" &&
            h.App.DocumentPages.Selected == 1 && h.Backend.Peers.Sum(peer => peer.FocusRequests) == requests &&
            !page.DrawerOpen, "Page activation veto occurs before focus routing, without forcing editor composition or changing drawer state.");
        pages.RejectPages = false;
        toggle.RejectFocus = true;
        Throws<InvalidOperationException>(() => tabs.ActivatePage(StudioTabs.OperationsId));
        Assert(h.App.DocumentPages.Selected == StudioTabs.OperationsId &&
            !page.DrawerOpen && !reveal.Open && scope.FocusRequests == scopeFocusRequests,
            "Native rejection of the chosen visible target stays explicit; it does not try hidden scope or reopen the drawer.");
        toggle.RejectFocus = false;
        Assert(tabs.ActivatePage(StudioTabs.OperationsId) && toggle.Focused,
            "Explicit native activation retry succeeds after focus becomes available.");
        Assert(page.TrySetControlsOpen(true) && tabs.ActivatePage(StudioTabs.OperationsId) && scope.Focused,
            "Open idle Operations retains the original native scope focus policy.");
    }

    private static void BusyNativeOperationsActivation()
    {
        foreach (bool animated in new[] { false, true })
        {
            var service = new PendingOperations();
            using var h = new StudioHarness(OperationsDraft(), service, enableOperationsDrawer: animated, supportsReveal: animated);
            var page = h.App.OperationsPage!;
            var scope = h.Backend.Find("operations-scope");
            var cancel = h.Backend.Find("operations-cancel");
            var tabs = h.Backend.Find("studio-tabs");
            page.Controller.Scan();
            var pending = service.Pending[0];
            Assert(tabs.ActivatePage(1), "Document activation while local scan runs is allowed.");
            int scopeRequests = scope.FocusRequests;
            Assert(tabs.ActivatePage(StudioTabs.OperationsId) && cancel.Focused &&
                scope.FocusRequests == scopeRequests && !page.ScopeInput.Enabled && page.Snapshot.CanCancel,
                "Open busy Operations activation targets enabled Cancel, not the disabled scope choice.");
            Assert(cancel.Events.Click() && pending.Token.IsCancellationRequested &&
                page.Snapshot.CancelRequested && !((Control)cancel.Element).Enabled,
                "Cancellation request disables its native button until the producer settles.");
            Assert(tabs.ActivatePage(1), "Switch away while cancellation settles.");
            int cancelRequests = cancel.FocusRequests;
            Assert(tabs.ActivatePage(StudioTabs.OperationsId) && tabs.Focused &&
                scope.FocusRequests == scopeRequests && cancel.FocusRequests == cancelRequests,
                "When no enabled content target exists, native activation deliberately keeps focus on the visible tab strip.");
            pending.Completion.SetCanceled(pending.Token);
            h.Finish();
            Assert(tabs.ActivatePage(StudioTabs.OperationsId) && scope.Focused &&
                !page.Snapshot.Busy && page.DrawerOpen && h.Errors.Count == 0,
                "After cancellation settles, the retained scope receives normal activation focus without closing or recreating controls.");
        }
    }

    private static IEnumerable<Element> Subtree(Element element)
    {
        yield return element;
        foreach (var child in element.Children)
            foreach (var descendant in Subtree(child)) yield return descendant;
    }
}
