using Android.Util;
using Android.Views.InputMethods;
using Android.Widget;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;
using NativeButton = Android.Widget.Button;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private void DynamicTaskChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        int before = assertions;
        using var host = new Host(dispatcher);
        var board = DynamicTaskBoard.Create(host);
        var backend = new AndroidBackend(surface, dispatcher);
        host.Attach(backend);
        var driver = new NativeDriver(backend);
        using var stream = typeof(TestActivity).Assembly.GetManifestResourceStream("DynamicTaskScenarios.json")
            ?? throw new InvalidOperationException("The shared dynamic task corpus is missing.");
        using var reader = new StreamReader(stream);
        int shared = ApplicationScenarioRunner.Run(reader.ReadToEnd(), "dynamic-task-board", driver);
        assertions += shared;
        driver.Click("dynamic-reset");
        var input = (EditText)backend.FindViews("dynamic-task-3-title").Single();
        input.RequestFocus();
        input.SetSelection(1, 4);
        using var editable = input.EditableText!;
        BaseInputConnection.SetComposingSpans(editable);
        int start = BaseInputConnection.GetComposingSpanStart(editable);
        int end = BaseInputConnection.GetComposingSpanEnd(editable);
        driver.Click("dynamic-reverse");
        var nativeOrder = CaptureViews(surface.GetChildAt(0)!).OfType<EditText>()
            .Select(view => view.Tag?.ToString()).Where(id => id?.EndsWith("-title", StringComparison.Ordinal) == true).ToArray();
        Assert(nativeOrder.SequenceEqual(new[] { "dynamic-task-3-title", "dynamic-task-2-title", "dynamic-task-1-title" }),
            "The shared dynamic application reorders actual native row editors.");
        Assert(ReferenceEquals(input, backend.FindViews("dynamic-task-3-title").Single()) &&
            input.HasFocus && input.SelectionStart == 1 && input.SelectionEnd == 4 &&
            start >= 0 && BaseInputConnection.GetComposingSpanStart(editable) == start &&
            BaseInputConnection.GetComposingSpanEnd(editable) == end,
            "Shared dynamic task edits retain native composition during reordering.");
        BaseInputConnection.RemoveComposingSpans(editable);
        string saved = DynamicTaskBoardCodec.Serialize(board.GetState());
        driver.Click("dynamic-reset");
        board.RestoreState(DynamicTaskBoardCodec.Restore(saved));
        Assert(driver.Text("dynamic-summary") == "3 tasks / 1 done" &&
            board.GetState().Items[0].Id == 3 && driver.Text("dynamic-task-3-title") == "Write guide",
            "The exact shared codec restores task data and order into native peers.");
        var remove = (NativeButton)backend.FindViews("dynamic-task-3-remove").Single();
        driver.Click("dynamic-task-3-remove");
        Assert(!driver.Visible("dynamic-task-3-title") && driver.Text("dynamic-summary") == "2 tasks / 1 done",
            "Shared task removal unregisters its native editor.");
        bool rejected = false;
        try { remove.PerformClick(); }
        catch (ObjectDisposedException) { rejected = true; }
        Assert(rejected && driver.Text("dynamic-summary") == "2 tasks / 1 done",
            "Removed shared task callbacks cannot run again.");
        var views = CaptureViews(surface.GetChildAt(0)!);
        host.Dispose();
        Assert(surface.ChildCount == 0 && views.All(view => view.Handle == IntPtr.Zero),
            "The dynamic task application releases its complete native tree.");
        Log.Info("Xui.Android.Orders", $"Dynamic task board: {shared} shared expectations; {assertions - before - shared} native application assertions.");
    }
}
