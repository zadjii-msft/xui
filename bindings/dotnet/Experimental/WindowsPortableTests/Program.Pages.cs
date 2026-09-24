using PortableNavigation;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeRetainedPageScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Retained native pages", 760, 900, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var sample = new RetainedPagesWorkbench(host);
        sample.Open();
        P.ElementExtensions.FixedSize(sample.Navigation, 760, 120);
        P.ElementExtensions.FixedSize(sample.Tabs, 760, 40);
        var backend = new WindowsBackend(surface, dispatcher);
        Check(backend.SupportsRetainedPages, "The qualified native retained-page contract is required.");
        host.Attach(backend);
        application.Show(window);
        var ui = new NativeUi(application);
        var work = Task.Run(() =>
        {
            try
            {
                const ulong secondId = 9007199254740993UL;
                var first = ui.Ui(() => (TextInput)backend.FindControls("first-input").Single());
                var second = ui.Ui(() => (TextInput)backend.FindControls("second-input").Single());
                var tabs = ui.Ui(() => (TabStrip)backend.FindControls("workspace-tabs").Single());
                var navigation = ui.Ui(() => (NavigationView)backend.FindControls("workspace-navigation").Single());
                nint firstEditor = ui.Ui(() =>
                {
                    Check(first.GetBounds().Width > 0 && first.GetBounds().Height > 0,
                        $"Selected page editor has no allocation: input={first.GetBounds()}, tabs={tabs.GetBounds()}, nav={navigation.GetBounds()}, pages={backend.Peer(sample.Documents).Native.GetBounds()}.");
                    first.Text = "Native retained draft";
                    first.Focus();
                    first.Selection = new(2, 6);
                    Check(PageSelector.GetClosable(tabs), "Native tab close affordance was not enabled.");
                    Throws<P.KeyedUpdateException>(() => sample.Documents.SetSelected(secondId));
                    Check(sample.Documents.Selected == 1 && first.Focused,
                        "Unsafe native page selection changed the model or stole editor focus.");
                    return GetFocus();
                });
                ui.Ui(() =>
                {
                    tabs.Focus();
                    sample.Documents.SetSelected(secondId);
                });
                ui.Wait(() => PageSelector.GetState(tabs).Selected == secondId &&
                    PageSelector.GetState(navigation).Selected == secondId);
                nint secondEditor = ui.Ui(() =>
                {
                    Check(IsWindow(firstEditor) && !host.TryFocus((P.TextInput)sample.Documents.Children[0].Children[0]),
                        "Inactive page input remained focusable or its original editor was destroyed.");
                    second.Focus();
                    second.Selection = new(0, 0);
                    nint editor = GetFocus();
                    Throws<P.KeyedUpdateException>(() => sample.Documents.Visible = false);
                    Check(sample.PaneVisible && sample.Documents.Visible && GetFocus() == editor,
                        "Unsafe pane hiding changed visibility or focus.");
                    return editor;
                });
                ui.Ui(() =>
                {
                    tabs.Focus();
                    sample.PaneVisible = false;
                });
                ui.Wait(() => first.GetBounds().Height == 0 && second.GetBounds().Height == 0);
                ui.Ui(() =>
                {
                    Check(IsWindow(firstEditor) && IsWindow(secondEditor),
                        "Hiding the complete page pane destroyed retained editors.");
                    sample.PaneVisible = true;
                    sample.OpenPages = (sample.OpenPages.Items.Reverse().ToArray(), secondId);
                });
                ui.Ui(() =>
                {
                    var pagePeer = (WindowsPagePeer)backend.Peer(sample.Documents);
                    var nativePages = (RetainedPages)pagePeer.Native;
                    Check(nativePages.GetPageId(0) == secondId && nativePages.GetPageId(1) == 1,
                        "Native retained-page order did not follow a model reorder.");
                    sample.Documents.SetSelected(1);
                });
                ui.Wait(() => PageSelector.GetState(tabs).Selected == 1);
                ui.Ui(() =>
                {
                    first.Focus();
                    Check(GetFocus() == firstEditor && first.Selection == new TextSelection(2, 6) &&
                        first.Text == "Native retained draft",
                        "Switching and reordering pages did not retain the original native edit session.");
                    tabs.Focus();
                    sample.Tabs.Closable = false;
                    Check(!PageSelector.GetClosable(tabs), "Native close affordance did not follow the authoritative property.");
                    int changes = sample.Changes;
                    sample.Tabs.Closable = true;
                    tabs.Select(secondId);
                    Check(sample.Changes == changes, "A native selector user event was delivered synchronously.");
                });
                ui.Wait(() => sample.Documents.Selected == secondId);
                ui.Ui(() =>
                {
                    Check(sample.Selection == "Selected: " + secondId && sample.Changes == 1,
                        "Native selector notification lost its 64-bit page identity.");
                    tabs.Focus();
                    sample.OpenPages = (sample.OpenPages.Items.Where(page => page.Entry.Id == secondId).ToArray(), secondId);
                    Check(!IsWindow(firstEditor) && IsWindow(secondEditor),
                        "Removing a page did not retire only that page's native editor.");
                    host.Detach();
                    Check(HandleCount(window) == baseline && !IsWindow(secondEditor),
                        "Retained page teardown leaked native handles or editors.");
                });
            }
            finally { ui.Ui(window.Close); }
        });
        RunNativeWork(application, work);
    }
}
