using Android.Util;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private void GalleryChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        using var stream = typeof(TestActivity).Assembly.GetManifestResourceStream("GalleryScenarios.json")
            ?? throw new InvalidOperationException("The shared gallery scenario resource is missing.");
        using var reader = new StreamReader(stream);
        string json = reader.ReadToEnd();
        foreach (string id in new[] { "task-board", "expense-ledger", "session-planner" })
        {
            using var host = new Host(dispatcher);
            switch (id)
            {
                case "task-board": _ = new TaskBoard(host); break;
                case "expense-ledger": _ = new ExpenseLedger(host); break;
                case "session-planner": _ = new SessionPlanner(host); break;
            }
            var backend = new AndroidBackend(surface, dispatcher);
            host.Attach(backend);
            int count = ApplicationScenarioRunner.Run(json, id, new NativeDriver(backend));
            assertions += count;
            Log.Info("Xui.Android.Orders", $"Gallery {id}: {count} shared expectations on native widgets.");
            var views = CaptureViews(surface.GetChildAt(0)!);
            host.Dispose();
            Assert(surface.ChildCount == 0 && views.All(view => view.Handle == IntPtr.Zero),
                $"{id} releases its complete native tree.");
        }
    }
}
