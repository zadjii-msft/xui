using Android.Util;
using Android.Widget;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private async Task SingleChoiceChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        int before = assertions;
        using var host = new Host(dispatcher);
        SingleChoice choice;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            choice = host.SingleChoice("Native choice");
            choice.AutomationId = "native-choice";
            choice.SetItems([new(1, "First"), new(9007199254740993UL, "Large ID"), new(3, "Disabled", false)]);
            root.Add(choice);
            host.SetContent(root);
            build.Complete();
        }
        int notices = 0;
        choice.Changed += _ => notices++;
        var backend = new AndroidBackend(surface, dispatcher);
        host.Attach(backend);
        var spinner = (NativeSingleChoice)backend.FindViews("native-choice").Single();
        await Settle();
        Assert(spinner.SelectedId == 1 && notices == 0, "Initial native choice selection is silent.");
        choice.SetSelected(9007199254740993UL);
        await Settle();
        Assert(spinner.SelectedId == 9007199254740993UL && notices == 0,
            "Native Spinner preserves a choice ID beyond JavaScript's exact integer range without an event echo.");
        spinner.SetSelection(0);
        await Settle();
        Assert(choice.Selected == 1 && notices == 1, "A real native Spinner selection updates the model exactly once.");
        spinner.SetSelection(2);
        await Settle();
        Assert(choice.Selected == 1 && spinner.SelectedId == 1 && notices == 1,
            "Native disabled rows restore the accepted selection without an authored callback.");
        choice.SetItems([new(4, "All disabled", false)]);
        await Settle();
        Assert(choice.Selected is null && spinner.SelectedId is null && spinner.Adapter!.Count == 2 &&
            spinner.Adapter.GetItemId(0) == -1 && notices == 1,
            "All-disabled choices retain a native absence row rather than inventing a selected ID.");
        choice.SetItems([]);
        await Settle();
        Assert(spinner.SelectedId is null && spinner.Adapter!.Count == 1, "Empty native choices preserve genuine selection absence.");
        choice.SetItems([new(1, "First"), new(2, "Second")]);
        await Settle();
        Assert(choice.Selected == 1 && spinner.SelectedId == 1 && notices == 1, "Repopulation silently restores the model's deterministic choice.");
        choice.Changed += id => { if (id == 2) choice.SetSelected(1); };
        spinner.SetSelection(1);
        await Settle();
        Assert(choice.Selected == 1 && spinner.SelectedId == 1 && notices == 2,
            "A programmatic choice made inside the native callback wins over the older event.");
        Assert(ReferenceEquals(spinner, backend.FindViews("native-choice").Single()), "Choice changes retain the original native Spinner.");
        using (var info = spinner.CreateAccessibilityNodeInfo()!)
            Assert(info.ClassName?.Contains("Spinner", StringComparison.Ordinal) == true,
                "SingleChoice exposes real Android Spinner accessibility semantics.");
        host.Detach();
        Assert(spinner.Handle == IntPtr.Zero && surface.ChildCount == 0, "Detaching the native choice releases its owned control.");
        Log.Info("Xui.Android.Orders", $"SingleChoice: {assertions - before} native assertions.");

        async Task Settle()
        {
            await host.DispatchAsync(() => { });
            await Task.Delay(50);
        }
    }
}
