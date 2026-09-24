using PortableDemo;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeRevealScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Native Reveal ownership", 620, 700, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var sample = new RevealWorkbench(host);
        var backend = new WindowsBackend(surface, dispatcher);
        Check(backend.SupportsReveal, "The qualified native Reveal contract is required.");
        host.Attach(backend);
        Check(host.GetRevealPresentation(sample.Drawer) == new P.RevealPresentation(0, false),
            "Initial Reveal attachment unexpectedly animated instead of settling its requested state.");
        application.Show(window);
        var ui = new NativeUi(application);
        var work = Task.Run(() =>
        {
            try
            {
                ui.Ui(() =>
                {
                    Check(!host.TryFocus(sample.NoteInput), "Closed logical Reveal content accepted native focus.");
                    sample.Drawer.SetState(true, new P.RevealMotion(180));
                });
                ui.Wait(() => host.GetRevealPresentation(sample.Drawer) == new P.RevealPresentation(1, false));
                nint editor = ui.Ui(() =>
                {
                    Check(host.TryFocus(sample.NoteInput), "The retained revealed input could not receive native focus.");
                    sample.Draft = "Retained reveal draft";
                    host.SetSelection(sample.NoteInput, new(2, 6));
                    Check(!sample.Drawer.TrySetOpen(false) && sample.Drawer.Open,
                        "Reveal did not veto closing its focused descendant before model mutation.");
                    Check(!sample.Drawer.TrySetState(false, new P.RevealMotion(0, P.RevealDirection.Right)) &&
                        sample.Drawer.Motion == new P.RevealMotion(180),
                        "A rejected atomic Reveal pair partially changed its motion state.");
                    return GetFocus();
                });
                ui.Ui(() =>
                {
                    Check(host.TryFocus(sample.ToggleButton), "The explicit Reveal focus target did not receive focus.");
                    sample.Drawer.SetOpen(false);
                    Check(!sample.Drawer.Open && !host.TryFocus(sample.NoteInput),
                        "Closing Reveal kept logical descendant input active during exit.");
                });
                ui.Wait(() => host.GetRevealPresentation(sample.Drawer) == new P.RevealPresentation(0, false));
                ui.Ui(() =>
                {
                    Check(IsWindow(editor), "Settled closed Reveal discarded its previously created native editor.");
                    sample.Drawer.SetState(true, new P.RevealMotion(0, P.RevealDirection.Right));
                });
                ui.Wait(() => host.GetRevealPresentation(sample.Drawer) == new P.RevealPresentation(1, false));
                ui.Ui(() =>
                {
                    Check(host.TryFocus(sample.NoteInput) && GetFocus() == editor &&
                        host.GetSelection(sample.NoteInput) == new P.TextSelection(2, 6) &&
                        sample.NoteInput.Text == "Retained reveal draft",
                        "Reopening Reveal lost the original native edit session.");
                    SendMessage(editor, 0x010d, 0, 0);
                    Check(!sample.Drawer.TrySetOpen(false), "Reveal did not veto closing active composition.");
                    sample.Drawer.SetMotion(new P.RevealMotion(180, P.RevealDirection.Bottom));
                    Check(host.GetRevealPresentation(sample.Drawer) == new P.RevealPresentation(1, false) &&
                        GetFocus() == editor, "Changing motion did not settle the unchanged logical open target.");
                    SendMessage(editor, 0x010e, 0, 0);
                    host.TryFocus(sample.ToggleButton);
                    sample.Drawer.SetOpen(false);
                    host.Detach();
                    Check(HandleCount(window) == baseline && !IsWindow(editor),
                        "Reveal teardown left a native clock, editor, or scope alive.");
                });
            }
            finally { ui.Ui(window.Close); }
        });
        RunNativeWork(application, work);
    }

    private static void NativeRevealTreePreflight()
    {
        using var window = new Window("Reveal candidate ownership");
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        P.KeyedStack rows;
        using (var build = host.BeginBuild())
        {
            rows = host.KeyedStack(P.Axis.Vertical);
            var reveal = host.Reveal(rows, "Scoped candidates");
            host.SetContent(host.Stack(P.Axis.Vertical).Add(reveal));
            build.Complete();
        }
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        uint attached = HandleCount(window);
        try
        {
            rows.Reconcile([P.KeyedItem.Create("unsupported", owner => new UnsupportedRevealRow(owner))]);
            throw new InvalidOperationException("A RichEdit candidate inside native Reveal was accepted.");
        }
        catch (P.KeyedUpdateException error)
        {
            Check(!error.ModelCommitted && error.InnerException is NotSupportedException &&
                rows.Children.Count == 0 && host.IsAttached && HandleCount(window) == attached,
                "Unsupported native Reveal insertion was not rejected before model/native mutation.");
        }
        host.Detach();
        Check(HandleCount(window) == baseline, "Reveal candidate rejection leaked native resources.");

        using (var unsupported = new P.Host(dispatcher))
        {
            using (var build = unsupported.BeginBuild())
            {
                unsupported.SetContent(unsupported.Stack(P.Axis.Vertical).Add(
                    unsupported.Reveal(unsupported.MultilineText("Unsupported initial content"), "Rejected initial Reveal")));
                build.Complete();
            }
            using var rejectedBackend = new WindowsBackend(surface, dispatcher);
            Throws<NotSupportedException>(() => unsupported.Attach(rejectedBackend));
            Check(!unsupported.IsAttached && HandleCount(window) == baseline,
                "Unsupported initial Reveal acquired native ownership before tree preflight.");
        }

        using (var virtualHost = new P.Host(dispatcher))
        {
            P.ScrollView scroll;
            using (var build = virtualHost.BeginBuild())
            {
                scroll = virtualHost.ScrollView(virtualHost.Stack(P.Axis.Vertical), "Nested virtual candidate");
                virtualHost.SetContent(virtualHost.Stack(P.Axis.Vertical).Add(virtualHost.Reveal(scroll, "Native provider boundary")));
                build.Complete();
            }
            virtualHost.Attach(new WindowsBackend(surface, dispatcher));
            uint beforeLease = HandleCount(window);
            Throws<NotSupportedException>(() => virtualHost.BeginVirtualViewport(scroll, 10000, 128, 1, _ => { }));
            Check(virtualHost.IsAttached && HandleCount(window) == beforeLease,
                "Unsupported leased viewport under Reveal allocated or detached instead of rejecting before Begin.");
            virtualHost.Detach();
        }
        Check(HandleCount(window) == baseline, "Reveal provider preflight fixtures leaked native ownership.");
    }

    private sealed class UnsupportedRevealRow : P.IPortableComponent
    {
        public P.Element Root { get; }
        internal UnsupportedRevealRow(P.Host host)
        {
            using var build = host.BeginBuild();
            var root = host.Stack(P.Axis.Vertical).Add(host.MultilineText("Unsupported inside native Reveal"));
            host.SetContent(root);
            build.Complete();
            Root = root;
        }
    }
}
