using System.Collections.Concurrent;
using PortableDemo;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeStudioScenarios(bool enableDrawer = false)
    {
        using var application = new Application();
        using var window = application.CreateWindow("Portable native scenarios", 1440, 900, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var errors = new ConcurrentQueue<Exception>();
        var app = WorkspaceStudio.Create(host, new LocalStudioAnalysisService(), errors.Enqueue,
            enableOperationsDrawer: enableDrawer);
        var backend = new WindowsBackend(surface, dispatcher, WindowsThemeAuthority.ExclusiveWindow);
        host.Attach(backend);
        app.AttachView();
        application.Show(window);
        var ui = new Driver(application, window, host, () => backend);
        var work = Task.Run(() =>
        {
            try
            {
                ui.Wait(() => app.Catalog.IsReady && app.LayoutMode == P.WidthMode.Expanded);
                ui.Change("doc-00001-title", "Retained desktop draft");
                var title = ui.Ui(() => (TextInput)backend.FindControls("doc-00001-title").Single());
                var body = ui.Ui(() => (MultilineText)backend.FindControls("doc-00001-body").Single());
                nint titleEditor = ui.Ui(() =>
                {
                    title.Focus();
                    title.Selection = new(2, 8);
                    Check(app.Catalog.MountedCount <= 20 && PageSelector.GetState((TabStrip)backend.FindControls("studio-tabs").Single()).Count == 2,
                        "The real Studio graph did not retain two document tabs and a bounded 10,000-item catalog.");
                    return GetFocus();
                });
                nint bodyEditor = ui.Ui(() =>
                {
                    body.Focus();
                    SendText(GetFocus(), 0x000c, 0, "A retained native document\nSecond line");
                    return GetFocus();
                });
                ui.Wait(() => app.Controller.State.Session.Document("doc-00001").Body == "A retained native document\nSecond line");
                ui.Ui(() =>
                {
                    backend.FindControls("studio-theme").Single().Focus();
                    app.Controller.ActivateTab("doc-00002");
                });
                ui.Wait(() => PageSelector.GetState((TabStrip)backend.FindControls("studio-tabs").Single()).Selected == 2);
                ui.Ui(() =>
                {
                    Check(IsWindow(titleEditor) && IsWindow(bodyEditor), "Switching real Studio tabs destroyed inactive native editors.");
                    app.Controller.MoveTab("doc-00001", 1);
                    app.Controller.ActivateTab("doc-00001");
                });
                ui.Wait(() => PageSelector.GetState((TabStrip)backend.FindControls("studio-tabs").Single()).Selected == 1);
                nint top = ui.Ui(() =>
                {
                    title.Focus();
                    Check(GetFocus() == titleEditor && title.Selection == new TextSelection(2, 8),
                        "Studio tab reorder or activation lost the retained native title editor or selection.");
                    return GetAncestor(titleEditor, 2);
                });
                uint dpi = ui.Ui(() => GetDpiForWindow(top));
                ui.Ui(() => Check(SetWindowPos(top, 0, 0, 0, checked((int)Math.Round(320 * dpi / 96.0)),
                    checked((int)Math.Round(760 * dpi / 96.0)), 0x0016), "Studio compact resize failed."));
                ui.Wait(() => app.LayoutMode == P.WidthMode.Compact && app.DocumentPages.Visible);
                ui.Ui(() =>
                {
                    Check(GetFocus() == titleEditor && title.Selection == new TextSelection(2, 8) &&
                        app.Controller.State.Session.Document("doc-00001").Body == "A retained native document\nSecond line",
                        $"Responsive compact layout changed editing state: oldHWND={titleEditor}, focus={GetFocus()}, oldLive={IsWindow(titleEditor)}, nativeFocused={title.Focused}, selection={title.Selection}, bodyMatches={app.Controller.State.Session.Document("doc-00001").Body == "A retained native document\nSecond line"}, bounds={title.GetBounds()}.");
                    Check(body.GetBounds().Width > 100 && body.GetBounds().Height > 100,
                        "The actual compact editor allocation is unusable.");
                });
                ui.Ui(() =>
                {
                    backend.FindControls("studio-theme").Single().Focus();
                    app.Controller.OpenDocument("doc-00003");
                });
                ui.Wait(() => app.Controller.State.Session.ActiveDocument == "doc-00003");
                ui.Ui(() =>
                {
                    app.Controller.ActivateTab("doc-00001");
                    Check(IsWindow(bodyEditor), "Opening a third document discarded the prior native editor.");
                    app.Controller.Analyze();
                });
                ui.Ui(() => app.LastOperation).WaitAsync(Timeout).GetAwaiter().GetResult();
                ui.Ui(() =>
                {
                    Check(app.Controller.State.Analysis is not null && errors.IsEmpty,
                        "The shared local analysis did not settle through the native host.");
                    backend.FindControls("studio-theme").Single().Focus();
                    app.Controller.OpenOperations();
                });
                ui.Wait(() => app.OperationsPage is not null && backend.FindControls("operations-scope").Count == 1);
                ui.Ui(() =>
                {
                    var choice = (ComboBox)backend.FindControls("operations-scope").Single();
                    Check(choice.Editor is null && choice.Selected == 1, "Operations did not use a native noneditable choice selector.");
                    choice.Focus();
                    choice.Select(2);
                });
                ui.Wait(() => (ulong)app.OperationsPage!.Controller.State.Scope == 2);
                if (enableDrawer)
                {
                    var page = ui.Ui(() => app.OperationsPage!);
                    var reveal = ui.Ui(() => page.ControlsReveal ?? throw new InvalidOperationException("The native Operations drawer is missing."));
                    ui.Ui(() =>
                    {
                        Check(host.GetRevealPresentation(reveal) == new P.RevealPresentation(1, false),
                            "The dynamically attached Operations drawer did not start settled open.");
                        Check(!page.TrySetControlsOpen(false) && page.DrawerOpen,
                            "The real Operations drawer hid its focused native choice.");
                        Check(host.TryFocus(page.DrawerToggleButton) && page.TrySetControlsOpen(false),
                            "The real Operations drawer did not close after an explicit safe focus move.");
                        Check(!host.TryFocus(page.ScopeInput), "Closed Operations controls remained keyboard-active during exit.");
                    });
                    ui.Wait(() => host.GetRevealPresentation(reveal) == new P.RevealPresentation(0, false));
                    ui.Ui(() =>
                    {
                        var tabs = (TabStrip)backend.FindControls("studio-tabs").Single();
                        tabs.Focus();
                        tabs.Select(1);
                    });
                    ui.Wait(() => app.Controller.State.Session.ActiveDocument == "doc-00001");
                    ui.Ui(() =>
                    {
                        var tabs = (TabStrip)backend.FindControls("studio-tabs").Single();
                        tabs.Focus();
                        tabs.Select(StudioTabs.OperationsId);
                    });
                    ui.Wait(() => app.Controller.State.Session.ActiveDocument == StudioTabs.OperationsKey);
                    ui.Ui(() =>
                    {
                        SendMessage(GetFocus(), 0x0100, 0x0d, 1);
                        SendMessage(GetFocus(), 0x0101, 0x0d, 1);
                    });
                    ui.Wait(() => backend.FindControls("operations-controls-toggle").Single().Focused);
                    ui.Ui(() => Check(!page.DrawerOpen && !reveal.Open &&
                        host.GetRevealPresentation(reveal) == new P.RevealPresentation(0, false),
                        "Activating a retained closed Operations page reopened or focused hidden controls."));
                    ui.Ui(() =>
                    {
                        Check(IsWindow(bodyEditor) && page.TrySetControlsOpen(true),
                            "Closing the controls drawer retired the separate main document editor.");
                    });
                    ui.Wait(() => host.GetRevealPresentation(reveal) == new P.RevealPresentation(1, false));
                    ui.Ui(() => Check(((ComboBox)backend.FindControls("operations-scope").Single()).Selected == 2,
                        "Reopening Operations controls lost native choice state."));
                }
                ui.Ui(() =>
                {
                    Check(((ComboBox)backend.FindControls("operations-scope").Single()).Selected == 2,
                        "Operations scope did not reflect actual native selection.");
                    app.OperationsPage!.Controller.Scan();
                    if (enableDrawer)
                        Check(!app.OperationsPage.TrySetControlsOpen(false) && app.OperationsPage.DrawerOpen,
                            "The Operations drawer hid the only cancellation controls while work was active.");
                });
                ui.Ui(() => app.LastOperation).WaitAsync(Timeout).GetAwaiter().GetResult();
                ui.Ui(() =>
                {
                    Check(app.OperationsPage!.Controller.State.Report is { Documents: 10000 } && errors.IsEmpty,
                        "The real local Operations page did not compute the selected full-catalog snapshot.");
                    var metricIds = new[] { "operations-documents", "operations-drafts", "operations-open",
                        "operations-words", "operations-characters", "operations-tasks" };
                    var rectangles = metricIds.Select(id => backend.FindControls(id).Single().GetBounds()).ToArray();
                    Check(rectangles.All(rectangle => rectangle.Width > 100 && rectangle.Height > 0),
                        "Compact Operations metrics did not receive complete full-width native allocations.");
                    Check(rectangles.Zip(rectangles.Skip(1)).All(pair =>
                        pair.Second.Y >= pair.First.Y + pair.First.Height + 7),
                        "Compact Operations metric rows overlap or lost their explicit vertical gutter.");
                    Check(backend.FindControls("operations-open").Single().Text == "Open documents: 3" &&
                        backend.FindControls("operations-words").Single().Text.StartsWith("Words: ", StringComparison.Ordinal),
                        "Compact Operations metrics rewrote or omitted their native label values.");
                });
                ui.Ui(() =>
                {
                    Check(errors.IsEmpty, "The shared workspace reported an unexpected native failure.");
                    host.Detach();
                    Check(HandleCount(window) == baseline && !IsWindow(titleEditor) && !IsWindow(bodyEditor),
                        "Studio native detach leaked retained page/editor/viewport resources.");
                    app.PrepareForAttachment();
                    Check(app.CatalogView.Children.Count == 0, "Studio reattachment recreated an oversized ordinary catalog.");
                    backend = new WindowsBackend(surface, dispatcher, WindowsThemeAuthority.ExclusiveWindow);
                    host.Attach(backend);
                    app.AttachView();
                });
                ui.Wait(() => app.Catalog.IsReady && app.LayoutMode == P.WidthMode.Compact);
                ui.Ui(() =>
                {
                    Check(((TextInput)backend.FindControls("doc-00001-title").Single()).Text == "Retained desktop draft",
                        "Studio reattachment lost its authored title draft.");
                    host.Detach();
                    Check(HandleCount(window) == baseline, "Studio reattachment leaked native resources.");
                });
            }
            finally { ui.Ui(window.Close); }
        });
        RunNativeWork(application, work);
    }
}
