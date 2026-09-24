using Android.Util;
using Android.Views;
using Android.Views.InputMethods;
using Android.Widget;
using PortableMutation;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;
using NativeButton = Android.Widget.Button;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private async Task DynamicChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        int before = assertions;
        using var host = new Host(dispatcher);
        var board = new MutationBoard(host);
        var backend = new AndroidBackend(surface, dispatcher);
        host.Attach(backend);
        var driver = new NativeDriver(backend);
        using (var stream = typeof(TestActivity).Assembly.GetManifestResourceStream("MutationScenarios.json")
            ?? throw new InvalidOperationException("The shared mutation scenario resource is missing."))
        using (var reader = new StreamReader(stream))
        {
            int shared = MutationScenarioRunner.Run(reader.ReadToEnd(), new MutationDriver(backend, surface));
            assertions += shared;
            Log.Info("Xui.Android.Orders", $"Mutation corpus: {shared} shared expectations on native widgets.");
        }
        driver.Click("reset-rows");
        driver.Click("add-row");
        driver.Click("add-row");
        driver.Click("add-row");
        var input = (EditText)backend.FindViews("row-3-input").Single();
        for (int attempt = 0; !input.IsAttachedToWindow && attempt < 100; attempt++) await Task.Delay(10);
        Assert(input.IsAttachedToWindow, "The keyed fixture exercises actual window-attached native editors.");
        driver.Change("row-3-input", "Draft C");
        driver.Click("row-3-increment");
        input.RequestFocus();
        input.SetSelection(1, 4);
        using var editable = input.EditableText!;
        BaseInputConnection.SetComposingSpans(editable);
        int start = BaseInputConnection.GetComposingSpanStart(editable);
        int end = BaseInputConnection.GetComposingSpanEnd(editable);
        Assert(start >= 0 && end > start, "The moved keyed row contains an active native composing range.");
        var frame1 = RowFrame((EditText)backend.FindViews("row-1-input").Single());
        var frame2 = RowFrame((EditText)backend.FindViews("row-2-input").Single());
        var frame3 = RowFrame(input);
        var container = (ViewGroup)frame3.Parent!;
        using var attachment = new AttachCounter();
        input.AddOnAttachStateChangeListener(attachment);
        try
        {
            driver.Click("reverse-rows");
            Assert(container.GetChildAt(0)!.Handle == frame3.Handle &&
                container.GetChildAt(1)!.Handle == frame2.Handle && container.GetChildAt(2)!.Handle == frame1.Handle,
                "Keyed moves update native child order, not just visual positions.");
            Assert(ReferenceEquals(input, backend.FindViews("row-3-input").Single()) &&
                input.HasFocus && input.SelectionStart == 1 && input.SelectionEnd == 4 &&
                input.Text == "Draft C" && driver.Text("row-3-count") == "Edits: 1",
                "A moved key retains its component state, editor identity, focus and selection.");
            Assert(attachment.Attached == 0 && attachment.Detached == 0 && input.IsAttachedToWindow &&
                BaseInputConnection.GetComposingSpanStart(editable) == start &&
                BaseInputConnection.GetComposingSpanEnd(editable) == end,
                "Same-parent keyed moves preserve native window attachment and composition.");

            var old = board.Rows;
            board.Rows =
            [
                KeyedItem.Create("front-1", owner => new MutationRow(owner, "front-1")),
                KeyedItem.Create("front-2", owner => new MutationRow(owner, "front-2")),
                KeyedItem.Create("front-3", owner => new MutationRow(owner, "front-3")),
                old[1], old[2], old[0]
            ];
            Assert(container.ChildCount == 6 && container.IndexOfChild(frame2) == 3 &&
                container.IndexOfChild(frame1) == 4 && container.IndexOfChild(frame3) == 5,
                "Move preflight accepts final indices beyond the original count when insertion precedes the move.");
            Assert(input.HasFocus && input.SelectionStart == 1 && input.SelectionEnd == 4 &&
                attachment.Detached == 0 &&
                BaseInputConnection.GetComposingSpanStart(editable) == start &&
                BaseInputConnection.GetComposingSpanEnd(editable) == end,
                "Insertion and reordering of sibling scopes leave composition on the surviving row intact.");
        }
        finally { input.RemoveOnAttachStateChangeListener(attachment); }
        BaseInputConnection.RemoveComposingSpans(editable);

        var retired = CaptureViews(frame3);
        var retiredButton = (NativeButton)backend.FindViews("row-3-increment").Single();
        board.Rows = board.Rows[..^1];
        Assert(retired.All(view => view.Handle == IntPtr.Zero) &&
            backend.FindViews("row-3-input").Count == 0 && backend.FindViews("row-3-increment").Count == 0,
            "Removing the focused row releases its subtree and removes retired automation lookup entries.");
        board.Rows = [.. board.Rows, KeyedItem.Create("row-3", owner => new MutationRow(owner, "row-3"))];
        var newInput = backend.FindViews("row-3-input").Single();
        Assert(!ReferenceEquals(input, newInput) && driver.Text("row-3-input") == "" &&
            driver.Text("row-3-count") == "Edits: 0",
            "A removed key reappears with a fresh component and native editor.");
        bool oldRejected = false;
        try { retiredButton.PerformClick(); }
        catch (ObjectDisposedException) { oldRejected = true; }
        Assert(oldRejected && driver.Text("row-3-count") == "Edits: 0",
            "A retired native callback cannot reach the replacement with the same key.");
        board.Rows = [.. board.Rows[..^1], KeyedItem.Create("row-3", owner => new MutationBanner(owner, "row-3-banner"))];
        Assert(newInput.Handle == IntPtr.Zero && backend.FindViews("row-3-input").Count == 0 &&
            backend.FindViews("row-3-banner").Count == 1,
            "Changing component type under a key replaces only that owned subtree.");
        driver.Click("toggle-notice");
        var notice = backend.FindViews("notice").Single();
        driver.Click("toggle-notice");
        Assert(notice.Handle == IntPtr.Zero && backend.FindViews("notice").Count == 0,
            "Conditional generated content creates and disposes its native subtree.");

        int childCount = container.ChildCount;
        bool rejected = false;
        try { board.RowsView.Reconcile([board.Rows[0], board.Rows[0]]); }
        catch (KeyedUpdateException error) when (!error.ModelCommitted) { rejected = true; }
        Assert(rejected && host.IsAttached && container.ChildCount == childCount && frame1.Handle != IntPtr.Zero,
            "Duplicate keys fail before changing the live Android tree.");
        rejected = false;
        try
        {
            board.RowsView.Reconcile([.. board.Rows, KeyedItem.Create<MutationRow>("broken", owner =>
            {
                _ = new MutationRow(owner, "staged");
                throw new InvalidOperationException("Expected native fixture factory failure.");
            })]);
        }
        catch (KeyedUpdateException error) when (!error.ModelCommitted) { rejected = true; }
        Assert(rejected && host.IsAttached && container.ChildCount == childCount &&
            backend.FindViews("staged-input").Count == 0,
            "A failed staged factory leaves the native tree mounted and creates no abandoned native peer.");

        var views = CaptureViews(surface.GetChildAt(0)!);
        bool committedFailure = false;
        try
        {
            board.RowsView.Reconcile([.. board.Rows[..^1],
                KeyedItem.Create("row-3", owner => new MutationBanner(owner, "row-3-banner"),
                    _ => throw new InvalidOperationException("Expected native fixture update failure."))]);
        }
        catch (KeyedUpdateException error) when (error.ModelCommitted) { committedFailure = true; }
        Assert(committedFailure && !host.IsAttached && surface.ChildCount == 0 &&
            views.All(view => view.Handle == IntPtr.Zero),
            "A committed keyed update failure detaches and releases the entire Android attachment.");
        var recovered = new AndroidBackend(surface, dispatcher);
        host.Attach(recovered);
        Assert(recovered.FindViews("row-3-banner").Count == 1 && recovered.FindViews("row-1-input").Count == 1,
            "The committed model can attach again after a keyed update failure.");
        new NativeDriver(recovered).Click("reset-rows");
        Assert(recovered.FindViews("row-1-input").Count == 0 && board.RowsView.Children.Count == 0,
            "Generated reset removes all keyed rows after recovery.");
        host.Dispose();
        Assert(surface.ChildCount == 0, "The generated dynamic fixture disposes its final attachment.");
        Log.Info("Xui.Android.Orders", $"Dynamic generated fixture: {assertions - before} Android assertions.");
    }

    private static View RowFrame(EditText input)
    {
        View frame = input;
        for (int parent = 0; parent < 4; parent++)
            frame = frame.Parent as View ?? throw new InvalidOperationException("Unexpected native row hierarchy.");
        return frame;
    }

    private sealed class AttachCounter : Java.Lang.Object, View.IOnAttachStateChangeListener
    {
        internal int Attached { get; private set; }
        internal int Detached { get; private set; }
        public void OnViewAttachedToWindow(View? view) => Attached++;
        public void OnViewDetachedFromWindow(View? view) => Detached++;
    }

    private sealed class MutationDriver(AndroidBackend backend, OrderSurface surface) : IMutationScenarioDriver
    {
        private readonly NativeDriver controls = new(backend);
        public void Click(string id) => controls.Click(id);
        public void Change(string id, string value) => controls.Change(id, value);
        public string Text(string id) => controls.Text(id);
        public bool Exists(string id) => backend.FindViews(id).Count != 0;
        public IReadOnlyList<string> Order() => CaptureViews(surface.GetChildAt(0)!).OfType<EditText>()
            .Select(view => view.Tag?.ToString() ?? throw new InvalidOperationException("A native row has no automation ID."))
            .Where(id => id.EndsWith("-input", StringComparison.Ordinal))
            .Select(id => id[..^6]).ToArray();
    }
}
