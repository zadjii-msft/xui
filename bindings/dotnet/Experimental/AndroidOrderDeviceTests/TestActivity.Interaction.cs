using Android.Util;
using Android.Views.InputMethods;
using Android.Widget;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private async Task InteractionChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        int before = assertions;
        using var host = new Host(dispatcher);
        var greeting = new Greeting(host);
        bool workerRejected = await Task.Run(() =>
        {
            if (dispatcher.CheckAccess()) return false;
            try { greeting.Count = 1; return false; }
            catch (InvalidOperationException) { return true; }
        });
        Assert(workerRejected && greeting.Count == 0, "Cached native UI-thread access rejects a real worker-thread model mutation.");
        bool constructionRejected = await Task.Run(() =>
        {
            try { _ = new AndroidDispatcher(); return false; }
            catch (InvalidOperationException) { return true; }
        });
        Assert(constructionRejected, "The dispatcher still validates actual Android main-looper construction.");
        bool postedOnUi = false;
        await Task.Run(async () => await host.DispatchAsync(() => postedOnUi = dispatcher.CheckAccess()));
        Assert(postedOnUi && dispatcher.CheckAccess(), "Worker dispatch executes on the validated native UI thread.");
        var snapshots = new List<TextInteraction>();
        greeting.Input.InteractionChanged += snapshots.Add;
        var backend = new AndroidBackend(surface, dispatcher);
        host.Attach(backend);
        Assert(snapshots.Count == 0, "Native interaction snapshots are not synchronous attachment callbacks.");
        await host.DispatchAsync(() => { });
        Assert(snapshots.Count == 1 && greeting.Input.Interaction == snapshots[0],
            "The initial native interaction snapshot is delivered after attachment.");
        var input = (EditText)backend.FindViews("name").Single();
        input.Text = "Observed";
        host.TryFocus(greeting.Input);
        await host.DispatchAsync(() => { });
        Assert(greeting.Input.Interaction is { HasFocus: true, IsComposing: false },
            "Native editor focus is delivered without synthesizing a model focus flag.");
        int count = snapshots.Count;
        using var editable = input.EditableText!;
        BaseInputConnection.SetComposingSpans(editable);
        Assert(snapshots.Count == count, "Composition observation never reenters the native span mutation.");
        await host.DispatchAsync(() => { });
        Assert(greeting.Input.Interaction is { IsComposing: true }, "Native composing spans produce an interaction snapshot.");
        BaseInputConnection.RemoveComposingSpans(editable);
        await host.DispatchAsync(() => { });
        Assert(greeting.Input.Interaction is { IsComposing: false } && input.Text == "Observed",
            "Composition completion is observed even when text does not change.");
        count = snapshots.Count;
        host.SetSelection(greeting.Input, new(1, 3));
        await host.DispatchAsync(() => { });
        Assert(snapshots.Count == count, "Selection span changes do not duplicate unchanged interaction state.");
        bool wasTouchMode = input.IsInTouchMode;
        Assert(host.TryFocus(greeting.IncrementButton) && host.HasFocus(greeting.IncrementButton) && !input.HasFocus,
            "Explicit toolbar focus uses a real native focus transfer, including touch-mode fallback.");
        await host.DispatchAsync(() => { });
        Assert(greeting.Input.Interaction is { HasFocus: false, IsComposing: false },
            "Actual native blur releases the editor interaction state.");
        host.TryFocus(greeting.Input);
        input.Text = "Replacement";
        using var replacement = input.EditableText!;
        BaseInputConnection.SetComposingSpans(replacement);
        await host.DispatchAsync(() => { });
        Assert(greeting.Input.Interaction is { HasFocus: true, IsComposing: true },
            "Native interaction observation follows an Editable replacement.");
        BaseInputConnection.RemoveComposingSpans(replacement);
        count = snapshots.Count;
        host.Detach();
        await host.DispatchAsync(() => { });
        Assert(snapshots.Count == count && greeting.Input.Interaction is null,
            "Queued interaction snapshots cannot target a detached generation.");
        host.Dispose();
        Assert(surface.ChildCount == 0, "Interaction listeners release their native surface.");
        Log.Info("Xui.Android.Orders", $"Native interaction: {assertions - before} assertions; explicit button focus began in touch mode={wasTouchMode}.");
    }
}
