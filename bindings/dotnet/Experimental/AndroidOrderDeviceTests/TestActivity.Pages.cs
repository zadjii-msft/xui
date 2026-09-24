using Android.Util;
using Android.Views;
using Android.Views.InputMethods;
using Android.Widget;
using PortableNavigation;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private async Task PagesChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        int before = assertions;
        using var host = new Host(dispatcher);
        var workbench = new RetainedPagesWorkbench(host);
        workbench.Open();
        var backend = new AndroidBackend(surface, dispatcher);
        host.Attach(backend);
        using var stream = typeof(TestActivity).Assembly.GetManifestResourceStream("PageScenarios.json")
            ?? throw new InvalidOperationException("The retained-page scenario corpus is missing.");
        using var reader = new StreamReader(stream);
        string json = reader.ReadToEnd();
        var driver = new PageDriver(host, dispatcher, backend);
        int shared = await Task.Run(() => PageScenarioRunner.Run(json, driver));
        assertions += shared;
        new NativeDriver(backend).Click("pages-reset");
        await host.DispatchAsync(() => { });
        var first = (EditText)backend.FindViews("first-input").Single();
        first.Text = "Composing retained page";
        first.RequestFocus();
        first.SetSelection(0);
        using var editable = first.EditableText!;
        BaseInputConnection.SetComposingSpans(editable);
        Assert(BaseInputConnection.GetComposingSpanStart(editable) >= 0 && workbench.Documents.Selected == 1,
            "The native page veto probe starts on the selected page with a real composing span.");
        bool rejected = false;
        try { workbench.Documents.SetSelected(9007199254740993UL); }
        catch (KeyedUpdateException error) { rejected = !error.ModelCommitted; }
        Assert(rejected && workbench.Documents.Selected == 1 &&
            ReferenceEquals(first, backend.FindViews("first-input").Single()),
            "Retained page switching vetoes affected native composition before model commit.");
        var navigation = (NativeNavigationList)backend.FindViews("workspace-navigation").Single();
        navigation.PerformItemClick(navigation.GetChildAt(0), 0, 1);
        Assert(first.HasFocus && BaseInputConnection.GetComposingSpanStart(editable) >= 0,
            "Deliberate navigation activation does not steal focus or terminate native composition.");
        BaseInputConnection.RemoveComposingSpans(editable);
        navigation.PerformItemClick(navigation.GetChildAt(0), 0, 1);
        Assert(navigation.HasFocus && !first.HasFocus,
            "Noncomposing native navigation activation takes actual focus before authored pane changes.");
        first.RequestFocus();
        workbench.Documents.SetSelected(9007199254740993UL);
        Assert(!first.HasFocus && first.Enabled == false && workbench.Documents.Selected == 9007199254740993UL,
            "Switching a noncomposing focused page performs a real native focus repair and disables inactive input.");
        var second = (EditText)backend.FindViews("second-input").Single();
        second.RequestFocus();
        rejected = false;
        try { workbench.PaneVisible = false; }
        catch (KeyedUpdateException error) { rejected = !error.ModelCommitted; }
        Assert(rejected && workbench.Documents.Visible, "Whole-pane hiding vetoes affected native focus without stealing another control's focus.");
        host.TryFocus(workbench.Tabs);
        workbench.PaneVisible = false;
        Assert(!new NativeDriver(backend).Visible("second-input") &&
            ReferenceEquals(second, backend.FindViews("second-input").Single()),
            "A hidden retained pane removes native input/accessibility eligibility without disposing its editor.");
        workbench.PaneVisible = true;
        workbench.Documents.SetSelected(1);
        Assert(ReferenceEquals(first, backend.FindViews("first-input").Single()) && first.Enabled,
            "Reactivating a page reuses its original native editor.");
        var tabs = (NativeTabStrip)backend.FindViews("workspace-tabs").Single();
        using (var info = tabs.CreateAccessibilityNodeInfo()!)
            Assert(info.ClassName?.Contains("TabWidget", StringComparison.Ordinal) == true,
                "The selector exposes the actual native TabWidget accessibility class.");
        workbench.Navigation.Expanded = false;
        Assert(((NativeNavigationList)backend.FindViews("workspace-navigation").Single()).Expanded == false,
            "Compact navigation is projected on the native list control.");
        var views = CaptureViews(surface.GetChildAt(0)!);
        host.Dispose();
        Assert(surface.ChildCount == 0 && views.All(view => view.Handle == IntPtr.Zero),
            "Retained page and native selector retirement releases all captured native handles.");
        Log.Info("Xui.Android.Orders", $"Pages: {shared} shared expectations; {assertions - before - shared} native retained-page assertions.");
    }

    private sealed class PageDriver(Host host, AndroidDispatcher dispatcher, AndroidBackend backend) : IPageScenarioDriver
    {
        private readonly NativeDriver controls = new(backend);
        private readonly Dictionary<string, View> remembered = [];
        private T Ui<T>(Func<T> action)
        {
            if (dispatcher.CheckAccess()) throw new InvalidOperationException("Run the native page corpus off the UI thread.");
            T result = default!;
            host.DispatchAsync(() => result = action()).GetAwaiter().GetResult();
            return result;
        }
        private void Ui(System.Action action) => Ui(() => { action(); return true; });
        public void Click(string id) => Ui(() => controls.Click(id));
        public void Change(string id, string value) => Ui(() => controls.Change(id, value));
        public void SelectPage(string id, ulong page)
        {
            bool enabled = Ui(() =>
            {
                var view = backend.FindViews(id).Single();
                if (!view.Enabled) return false;
                if (view is NativeTabStrip tabs)
                {
                    int index = tabs.Ids.ToList().IndexOf(page);
                    if (index < 0 || !tabs.GetChildAt(index)!.RequestFocusFromTouch())
                        throw new InvalidOperationException("The native tab could not receive selection focus.");
                }
                else if (view is NativeNavigationList navigation)
                {
                    navigation.RequestFocusFromTouch();
                    int index = navigation.Ids.ToList().IndexOf(page);
                    if (index < 0) throw new InvalidOperationException("The native navigation item is missing.");
                    navigation.SetSelection(index);
                }
                else throw new InvalidOperationException("Expected a native page selector.");
                return true;
            });
            if (!enabled) return;
            var deadline = DateTime.UtcNow.AddSeconds(5);
            while (Selected(id) != page)
            {
                if (DateTime.UtcNow > deadline) throw new InvalidOperationException("Native page selection did not settle.");
                Thread.Sleep(10);
            }
        }
        public void ActivatePage(string id, ulong page) => Ui(() =>
        {
            var tabs = (NativeTabStrip)backend.FindViews(id).Single();
            int index = tabs.Ids.ToList().IndexOf(page);
            if (index < 0) throw new InvalidOperationException("The native tab is missing.");
            tabs.GetChildAt(index)!.PerformClick();
        });
        public void RequestClose(string id, ulong page) => Ui(() =>
        {
            var tabs = (NativeTabStrip)backend.FindViews(id).Single();
            int index = tabs.Ids.ToList().IndexOf(page);
            var header = tabs.GetChildAt(index) as ViewGroup ?? throw new InvalidOperationException("The native tab indicator is missing.");
            var close = header.GetChildAt(1) as global::Android.Widget.Button
                ?? throw new InvalidOperationException("The native close-request control is missing.");
            close.PerformClick();
        });
        public string Text(string id) => Ui(() => controls.Text(id));
        public bool Enabled(string id) => Ui(() => controls.Enabled(id));
        public bool Visible(string id) => Ui(() => controls.Visible(id));
        public bool Exists(string id) => Ui(() => backend.FindViews(id).Count != 0);
        public ulong? Selected(string id) => Ui(() => backend.FindViews(id).Single() switch
        {
            NativeTabStrip tabs => tabs.SelectedId,
            NativeNavigationList navigation => navigation.SelectedId,
            _ => throw new InvalidOperationException("Expected a native page selector.")
        });
        public IReadOnlyList<ulong> Pages(string id) => Ui(() => backend.FindViews(id).Single() switch
        {
            NativeTabStrip tabs => tabs.Ids,
            NativeNavigationList navigation => navigation.Ids,
            _ => throw new InvalidOperationException("Expected a native page selector.")
        });
        public void RememberEditor(string id) => Ui(() => remembered[id] = backend.FindViews(id).Single());
        public bool SameEditor(string id) => Ui(() => ReferenceEquals(remembered[id], backend.FindViews(id).Single()));
    }
}
