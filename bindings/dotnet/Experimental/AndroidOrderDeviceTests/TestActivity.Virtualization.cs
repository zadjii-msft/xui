using Android.Util;
using Android.Views;
using Android.Views.InputMethods;
using Android.Widget;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private async Task VirtualViewportChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        int before = assertions;
        using var host = new Host(dispatcher);
        var app = VirtualList.CreateForViewport(host);
        Assert(app.RowsView.Children.Count == 0, "A virtual application starts without unleased gaps or editors.");
        var backend = new AndroidBackend(surface, dispatcher);
        host.Attach(backend);
        var scroll = (EnabledScrollView)backend.FindViews("virtual-scroll").Single();
        var requests = new List<VirtualViewportRequest>();
        using var lease = host.BeginVirtualViewport(app.Viewport, app.Controller.RequestedCount, app.Controller.RowHeight,
            app.Controller.RequestedSourceVersion, request =>
            {
                requests.Add(request);
                Log.Info("Xui.Android.Orders", $"Viewport request {request.Epoch}: {request.Requested}; blocked={request.IsBlocked}");
                app.OnViewportRequested(request);
            });
        app.AttachViewport(lease, host.SetVirtualItemInfo);
        if (Intent?.GetBooleanExtra("virtual-smoke", false) == true)
            scroll.VirtualLease!.TraceRequests = message => Log.Info("Xui.Android.Orders", message);
        MeasureNative(surface, 360, 640);
        Assert(scroll.Height == 0 && scroll.ScrollY == 0 && requests.Count == 0,
            "Initial native viewport exposure is held at zero before a posted preparation.");
        await WaitFor(() => app.Controller.Count == 10000 && scroll.Height > 0 && app.Controller.Mounted.Count > 0);
        AssertVirtualCoverage(backend, scroll, app.Controller.RowHeight);
        Assert(app.Controller.Mounted.Count < 30 && requests.Count < 12,
            "Ten thousand logical rows use a bounded mounted native set without an initial request loop.");
        using (var info = scroll.CreateAccessibilityNodeInfo()!)
        using (var collection = info.GetCollectionInfo())
            Assert(collection is not null && collection.RowCount == 10000 && collection.ColumnCount == 1,
                "The native scroll container exposes declared collection size.");

        int previousOffset = scroll.ScrollY;
        lease.RequestOffset(1_280_000);
        Assert(scroll.ScrollY == previousOffset, "Programmatic offset requests do not expose unprepared native content.");
        await WaitFor(() => app.Controller.Offset > 1_270_000);
        AssertVirtualCoverage(backend, scroll, app.Controller.RowHeight);
        Assert(backend.FindViews("virtual-task-09999").Count == 1 && app.Controller.Mounted.Count < 30,
            "The last logical item has a real native editor without constructing the complete dataset.");
        var lastModel = app.Controller.Mounted["task-09999"].Input;
        var last = (EditText)backend.FindViews("virtual-task-09999").Single();
        last.Text = "Last retained draft";
        host.TryFocus(lastModel);
        host.SetSelection(lastModel, new(1, 5));
        await host.DispatchAsync(() => { });
        using var editable = last.EditableText!;
        BaseInputConnection.SetComposingSpans(editable);
        await host.DispatchAsync(() => { });
        int composingStart = BaseInputConnection.GetComposingSpanStart(editable);
        lastModel.SetCaptionVisible(true);
        lastModel.SetCaptionVisible(false);
        Assert(ReferenceEquals(last, backend.FindViews("virtual-task-09999").Single()) &&
            last.HasFocus && last.SelectionStart == 1 && last.SelectionEnd == 5 &&
            BaseInputConnection.GetComposingSpanStart(editable) == composingStart,
            "Lazy native caption creation never replaces or reparents the active virtual editor.");
        int composingOffset = scroll.ScrollY;
        long beforeBlockedRequest = requests[^1].Epoch;
        lease.RequestOffset(0);
        await WaitFor(() => requests[^1].Epoch > beforeBlockedRequest && requests[^1].Requested.Offset == 0 && requests[^1].IsBlocked);
        Assert(scroll.ScrollY == composingOffset && ReferenceEquals(last, backend.FindViews("virtual-task-09999").Single()) &&
            last.HasFocus && last.SelectionStart == 1 && last.SelectionEnd == 5,
            "Global native composition blocks viewport movement without moving or replacing the edited row.");
        BaseInputConnection.RemoveComposingSpans(editable);
        await WaitFor(() => scroll.ScrollY == 0);
        AssertVirtualCoverage(backend, scroll, app.Controller.RowHeight);
        Assert(backend.FindViews("virtual-task-09999").Count == 1 && last.HasFocus,
            "A focused distant row remains sparsely pinned after an unblocked viewport move.");
        var firstModel = app.Controller.Mounted["task-00000"].Input;
        Assert(host.TryFocus(firstModel), "The first realized editor accepts native logical-navigation focus.");
        await WaitFor(() => backend.FindViews("virtual-task-09999").Count == 0);
        Assert(last.Handle == IntPtr.Zero && app.Data.Items["task-09999"].Draft == "Last retained draft" &&
            app.Data.Items["task-09999"].Selection == new TextSelection(1, 5),
            "Unpinned row retirement preserves external draft and selection while releasing its native editor.");
        var toolbar = (Xui.Experimental.Portable.Button)app.Root.Children[2].Children[0];
        host.TryFocus(toolbar);
        await host.DispatchAsync(() => { });
        lease.RequestOffset(1_280_000);
        await WaitFor(() => backend.FindViews("virtual-task-09999").Count == 1 && scroll.ScrollY > 0);
        var restored = (EditText)backend.FindViews("virtual-task-09999").Single();
        await host.DispatchAsync(() => { });
        Assert(!ReferenceEquals(last, restored) && restored.Text == "Last retained draft" &&
            restored.SelectionStart == 1 && restored.SelectionEnd == 5,
            "Recycled editors restore saved selection without overwriting it with initial native zero selection.");
        var priorSource = scroll.VirtualLease!.CommittedSourceVersion;
        new NativeDriver(backend).Click("virtual-reverse");
        await WaitFor(() => scroll.VirtualLease!.CommittedSourceVersion > priorSource);
        AssertVirtualCoverage(backend, scroll, app.Controller.RowHeight);
        Assert(app.Controller.Count == 10000 && app.Controller.Mounted.Count < 30,
            "A source reorder commits a new source version with bounded actual row coverage.");
        var currentRow = app.Controller.Mounted.First();
        host.TryFocus(currentRow.Value.Input);
        await host.DispatchAsync(() => { });
        long sourceBeforeFilter = scroll.VirtualLease!.CommittedSourceVersion;
        int countBeforeFilter = scroll.VirtualLease.CommittedCount;
        new NativeDriver(backend).Click("virtual-filter");
        await host.DispatchAsync(() => { });
        Assert(scroll.VirtualLease.CommittedSourceVersion == sourceBeforeFilter &&
            scroll.VirtualLease.CommittedCount == countBeforeFilter && app.Controller.IsDeferred,
            "A pointer source change does not publish a new extent while an editor remains natively focused.");
        host.TryFocus(toolbar);
        await WaitFor(() => scroll.VirtualLease!.CommittedCount == 5000);
        AssertVirtualCoverage(backend, scroll, app.Controller.RowHeight);
        Assert(scroll.VirtualLease!.CommittedSourceVersion > sourceBeforeFilter,
            "A real native focus transfer unblocks the queued source and commits its exact declared extent.");
        new NativeDriver(backend).Click("virtual-filter");
        await WaitFor(() => scroll.VirtualLease!.CommittedCount == 10000);
        AssertVirtualCoverage(backend, scroll, app.Controller.RowHeight);
        int directBefore = scroll.ScrollY;
        scroll.ScrollTo(0, 0);
        Assert(scroll.ScrollY == directBefore, "Direct native ScrollTo intent is held before row preparation.");
        await WaitFor(() => scroll.ScrollY == 0);
        AssertVirtualCoverage(backend, scroll, app.Controller.RowHeight);
        scroll.Fling(2500);
        Assert(scroll.ScrollY == 0, "Native fling starts by requesting geometry rather than exposing an unprepared offset.");
        await WaitFor(() => scroll.ScrollY > 0);
        lease.RequestOffset(0);
        await WaitFor(() => scroll.ScrollY == 0);
        await Task.Delay(100);
        Assert(scroll.ScrollY == 0, "A newer explicit request cancels the native fling instead of being overwritten by its next tick.");

        int smallHeight = Xui.Experimental.Android.LayoutMath.Pixels(120, scroll.Resources!.DisplayMetrics!.Density);
        app.Viewport.SetHeight(120);
        MeasureNative(surface, 360, 640);
        Assert(scroll.Height <= smallHeight, "A real viewport constraint shrink exposes only the existing intersection.");
        await WaitFor(() => scroll.Height == smallHeight);
        app.Viewport.SetHeight(null);
        MeasureNative(surface, 360, 640);
        Assert(scroll.Height == smallHeight, "Viewport growth remains held before a posted row preparation.");
        await WaitFor(() => scroll.Height > smallHeight);
        AssertVirtualCoverage(backend, scroll, app.Controller.RowHeight);
        lease.Dispose();
        Assert(scroll.Height == 0 && scroll.MeasuredHeight == 0,
            "Retiring a native lease closes viewport exposure instead of enabling ordinary scrolling through sparse gaps.");
        int callbacks = requests.Count;
        host.Detach();
        await host.DispatchAsync(() => { });
        Assert(requests.Count == callbacks && surface.ChildCount == 0,
            "Disposed viewport callbacks cannot target a later attachment.");
        host.Dispose();
        Log.Info("Xui.Android.Orders", $"Virtual viewport: {assertions - before} native assertions, {requests.Count} requests, 10000 logical rows.");
        if (Intent?.GetBooleanExtra("virtual-smoke", false) == true) return;
        await VirtualReattachmentChecks(surface, dispatcher);
        await VirtualCoverageRejectionCheck(surface, dispatcher);
        await VirtualPerformanceChecks(surface, dispatcher);

        async Task WaitFor(Func<bool> condition,
            [System.Runtime.CompilerServices.CallerArgumentExpression(nameof(condition))] string expectation = "")
        {
            var deadline = DateTime.UtcNow.AddSeconds(15);
            while (!condition())
            {
                if (DateTime.UtcNow > deadline)
                    throw new InvalidOperationException($"Native viewport timeout: {expectation}; requests={requests.Count}, count={app.Controller.Count}, mounted={app.Controller.Mounted.Count}, offset={scroll.ScrollY}, height={scroll.Height}, deferred={app.Controller.IsDeferred}.");
                await Task.Delay(20);
            }
        }
    }

    private async Task VirtualCoverageRejectionCheck(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        using var host = new Host(dispatcher);
        Xui.Experimental.Portable.ScrollView viewport;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Xui.Experimental.Portable.Axis.Vertical);
            var rows = host.KeyedStack(Xui.Experimental.Portable.Axis.Vertical);
            viewport = host.ScrollView(rows, "Coverage rejection");
            viewport.AutomationId = "missing-rows";
            root.Add(viewport, 1);
            host.SetContent(root);
            build.Complete();
        }
        var backend = new AndroidBackend(surface, dispatcher);
        host.Attach(backend);
        var scroll = (EnabledScrollView)backend.FindViews("missing-rows").Single();
        bool rejected = false;
        IVirtualViewportLease? lease = null;
        lease = host.BeginVirtualViewport(viewport, 10000, 128, 1, request =>
        {
            try
            {
                if (lease!.TryBeginUpdate(request.Epoch) != VirtualViewportUpdateResult.Ready)
                    throw new InvalidOperationException("The fresh coverage test request was not ready.");
                lease.TryCommit(request.Epoch);
            }
            catch (InvalidOperationException error) when (error.Message.Contains("has not been realized", StringComparison.Ordinal))
            {
                rejected = true;
            }
        });
        MeasureNative(surface, 360, 640);
        Assert(scroll.Height == 0, "An unprepared native viewport cannot expose a dataset-sized gap.");
        var deadline = DateTime.UtcNow.AddSeconds(15);
        while (!rejected)
        {
            if (DateTime.UtcNow > deadline) throw new InvalidOperationException("The invalid native coverage request was not rejected.");
            await Task.Delay(10);
        }
        Assert(!host.IsAttached && surface.ChildCount == 0 && scroll.Handle == IntPtr.Zero,
            "Missing native visible-row coverage rejects publication and detaches the invalid attachment.");
        lease.Dispose();
    }

    private async Task VirtualReattachmentChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        using var host = new Host(dispatcher);
        var app = VirtualList.CreateForViewport(host);
        int callbacks = 0;
        int viewsReleased = 0;
        int maximumRows = 0;
        for (int cycle = 0; cycle < 100; cycle++)
        {
            if (cycle != 0) app.PrepareForViewportAttachment();
            Assert(app.RowsView.Children.Count == 0 && app.Controller.Mounted.Count == 0 && app.Controller.Count == 0,
                "Every reattachment clears realized rows and large gaps before native attachment.");
            var backend = new AndroidBackend(surface, dispatcher);
            host.Attach(backend);
            var scroll = (EnabledScrollView)backend.FindViews("virtual-scroll").Single();
            using var lease = host.BeginVirtualViewport(app.Viewport, app.Controller.RequestedCount, app.Controller.RowHeight,
                app.Controller.RequestedSourceVersion, request =>
                {
                    callbacks++;
                    app.OnViewportRequested(request);
                });
            app.AttachViewport(lease, host.SetVirtualItemInfo);
            var nativeLease = scroll.VirtualLease!;
            MeasureNative(surface, 360, 640);
            await Await(() => app.Controller.Count == 10000 && scroll.Height > 0);
            bool last = cycle % 2 != 0;
            lease.RequestOffset(last ? 1_280_000 : 0);
            await Await(() => last ? scroll.ScrollY > 3_300_000 : scroll.ScrollY == 0);
            await host.DispatchAsync(() => { });
            string key = last ? "task-09999" : "task-00000";
            var row = app.Controller.Mounted[key];
            var input = (EditText)backend.FindViews("virtual-" + key).Single();
            if (cycle >= 2)
                Assert(input.Text == $"cycle-{cycle - 2}" && host.GetSelection(row.Input) == new TextSelection(1, 4),
                    $"Native recycling cycle {cycle}: text='{input.Text}', savedText='{app.Data.Items[key].Draft}', selection={host.GetSelection(row.Input)}, saved={app.Data.Items[key].Selection}, interaction={row.Input.Interaction}, focus={input.HasFocus}.");
            input.Text = $"cycle-{cycle}";
            host.SetSelection(row.Input, new(1, 4));
            maximumRows = Math.Max(maximumRows, app.Controller.Mounted.Count);
            AssertVirtualCoverage(backend, scroll, app.Controller.RowHeight);
            var views = CaptureViews(surface.GetChildAt(0)!);
            app.CaptureEditingState();
            host.Detach();
            int retiredCallbacks = callbacks;
            await host.DispatchAsync(() => { });
            Assert(callbacks == retiredCallbacks && surface.ChildCount == 0 && nativeLease.ResourcesReleased &&
                views.All(view => view.Handle == IntPtr.Zero),
                "Retired native viewport leases drop queued callbacks and release every captured native view handle.");
            viewsReleased += views.Count;
        }
        host.Dispose();
        Assert(maximumRows < 30, "One hundred native reattachments remain proportional to the viewport, not the dataset.");
        Log.Info("Xui.Android.Orders",
            $"Virtual reattachment: 100 cycles, max {maximumRows} rows, {viewsReleased} native view handles released, {callbacks} delivered requests.");

        async Task Await(Func<bool> condition)
        {
            var deadline = DateTime.UtcNow.AddSeconds(15);
            while (!condition())
            {
                if (DateTime.UtcNow > deadline) throw new InvalidOperationException("Virtual reattachment did not settle.");
                await Task.Delay(10);
            }
        }
    }

    private async Task VirtualPerformanceChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        bool diagnostics = Intent?.GetBooleanExtra("profile-phases", false) == true;
        using var host = new Host(dispatcher);
        var app = VirtualList.CreateForViewport(host);
        var backend = new AndroidBackend(surface, dispatcher);
        host.Attach(backend);
        var scroll = (EnabledScrollView)backend.FindViews("virtual-scroll").Single();
        long callbackStart = 0;
        using var lease = host.BeginVirtualViewport(app.Viewport, app.Controller.RequestedCount, app.Controller.RowHeight,
            app.Controller.RequestedSourceVersion, request =>
            {
                if (diagnostics) callbackStart = System.Diagnostics.Stopwatch.GetTimestamp();
                app.OnViewportRequested(request);
            });
        app.AttachViewport(lease, host.SetVirtualItemInfo);
        MeasureNative(surface, 360, 640);
        var deadline = DateTime.UtcNow.AddSeconds(15);
        while (app.Controller.Count != 10000 || scroll.Height == 0)
        {
            if (DateTime.UtcNow > deadline) throw new InvalidOperationException("The native performance fixture did not initialize.");
            await Task.Delay(10);
        }
        var timings = new List<double>();
        var phases = new List<double[]>();
        var accessCounts = new List<long>();
        var peerTimings = new List<double[]>();
        var offsets = VirtualListPerformance.RequestOffsets(10000, app.Controller.RowHeight, app.Controller.ViewportHeight);
        foreach (float offset in offsets)
        {
            var completion = new TaskCompletionSource<double>(TaskCreationOptions.RunContinuationsAsynchronously);
            long start = 0;
            long accessStart = 0;
            bool previousTrace = dispatcher.TraceAccessChecks;
            var metrics = diagnostics ? new NativePeerMetrics() : null;
            long nativeStart = 0, measured = 0, validated = 0, published = 0;
            if (diagnostics)
                scroll.VirtualLease!.TraceCommit = (a, b, c, d) => { nativeStart = a; measured = b; validated = c; published = d; };
            void Committed(VirtualViewportRequest request)
            {
                if (start == 0 || Math.Abs(request.Requested.Offset - offset) > 1 / scroll.Resources!.DisplayMetrics!.Density)
                    return;
                if (scroll.VirtualLease!.LastFlushedEpoch != request.Epoch)
                    throw new InvalidOperationException("Performance completion preceded the native post-prune geometry flush.");
                long completed = System.Diagnostics.Stopwatch.GetTimestamp();
                double Elapsed(long a, long b) => System.Diagnostics.Stopwatch.GetElapsedTime(a, b).TotalMilliseconds;
                if (diagnostics)
                {
                    if (nativeStart < callbackStart) throw new InvalidOperationException("Missing native commit timing for the completed request.");
                    phases.Add([Elapsed(start, callbackStart), Elapsed(callbackStart, nativeStart), Elapsed(nativeStart, measured),
                        Elapsed(measured, validated), Elapsed(validated, published), Elapsed(published, completed)]);
                    accessCounts.Add(dispatcher.AccessChecks - accessStart);
                    peerTimings.Add(metrics!.Milliseconds());
                }
                completion.TrySetResult(Elapsed(start, completed));
            }
            app.ViewportCommitted += Committed;
            try
            {
                await host.DispatchAsync(() =>
                {
                    start = System.Diagnostics.Stopwatch.GetTimestamp();
                    accessStart = dispatcher.AccessChecks;
                    dispatcher.TraceAccessChecks = diagnostics;
                    backend.Metrics = metrics;
                    lease.RequestOffset(offset);
                });
                timings.Add(await completion.Task.WaitAsync(TimeSpan.FromSeconds(15)));
            }
            finally
            {
                app.ViewportCommitted -= Committed;
                scroll.VirtualLease!.TraceCommit = null;
                dispatcher.TraceAccessChecks = previousTrace;
                backend.Metrics = null;
            }
        }
        var result = VirtualListPerformance.Evaluate(timings);
        Log.Info("Xui.Android.Orders",
            $"Virtual performance: warmups=10 samples=100 p50={result.P50Milliseconds:F3}ms p95={result.P95Milliseconds:F3}ms max={result.MaximumMilliseconds:F3}ms budget=50ms.");
        Log.Info("Xui.Android.Orders", "Virtual performance samples(ms): " +
            string.Join(",", result.SamplesMilliseconds.Select(value => value.ToString("F3", System.Globalization.CultureInfo.InvariantCulture))));
        if (diagnostics)
        {
            Log.Info("Xui.Android.Orders", "Virtual phase samples(queue,prepare,measure,coverage,publish,prune+flush ms): " +
                string.Join(";", phases.Skip(10).Select(row => string.Join(",", row.Select(value =>
                    value.ToString("F3", System.Globalization.CultureInfo.InvariantCulture))))));
            Log.Info("Xui.Android.Orders", $"Virtual access checks: mean={accessCounts.Skip(10).Average():F0} max={accessCounts.Skip(10).Max()} per request.");
        }
        Log.Info("Xui.Android.Orders", $"Virtual timing completion: settled geometry; diagnostics={diagnostics}.");
        Log.Info("Xui.Android.Orders", $"Virtual delivered epochs: {scroll.VirtualLease!.DeliveredRequestCount} for initialization and 110 explicit requests.");
        Log.Info("Xui.Android.Orders", $"Virtual delivered model interactions: {backend.DeliveredInteractionCount}.");
        string resultPath = Path.Combine(GetExternalFilesDir(null)?.AbsolutePath ?? throw new InvalidOperationException("Missing native test artifact directory."),
            "virtual-performance.json");
        using (var stream = File.Create(resultPath))
        using (var writer = new System.Text.Json.Utf8JsonWriter(stream))
        {
            writer.WriteStartObject();
            writer.WriteNumber("warmups", 10);
            writer.WriteNumber("samples", 100);
            writer.WriteNumber("p50Ms", result.P50Milliseconds);
            writer.WriteNumber("p95Ms", result.P95Milliseconds);
            writer.WriteNumber("maximumMs", result.MaximumMilliseconds);
            writer.WriteNumber("budgetMs", VirtualListPerformance.P95BudgetMilliseconds);
            writer.WriteBoolean("settledCompletion", true);
            writer.WriteBoolean("phaseInstrumentation", diagnostics);
            writer.WriteNumber("deliveredEpochs", scroll.VirtualLease!.DeliveredRequestCount);
            writer.WriteNumber("modelInteractionNotices", backend.DeliveredInteractionCount);
            writer.WriteStartArray("measurements");
            for (int i = 10; i < timings.Count; i++)
            {
                writer.WriteStartObject();
                writer.WriteNumber("totalMs", timings[i]);
                if (diagnostics)
                {
                    writer.WriteNumber("accessChecks", accessCounts[i]);
                    writer.WriteStartArray("peerCreateDisposeInsertRemoveMoveMetadataUpdateInputConstructionInitialPropertiesListenersMs");
                    foreach (double elapsed in peerTimings[i]) writer.WriteNumberValue(elapsed);
                    writer.WriteEndArray();
                    writer.WriteStartArray("queuePrepareMeasureCoveragePublishPruneAndFlushMs");
                    foreach (double phase in phases[i]) writer.WriteNumberValue(phase);
                    writer.WriteEndArray();
                }
                writer.WriteEndObject();
            }
            writer.WriteEndArray();
            writer.WriteEndObject();
        }
        Assert(result.MeetsInitialBudget, "The measured native viewport transactions satisfy the agreed 50ms p95 budget on this emulator.");
        host.Dispose();
    }

    private void AssertVirtualCoverage(AndroidBackend backend, EnabledScrollView scroll, float rowHeight)
    {
        var lease = scroll.VirtualLease ?? throw new InvalidOperationException("The native viewport has no lease.");
        var rows = backend.VirtualRows(scroll);
        float density = scroll.Resources!.DisplayMetrics!.Density;
        int pitch = Xui.Experimental.Android.LayoutMath.Pixels(rowHeight, density);
        var content = (View)scroll.GetChildAt(0)!;
        var indices = new HashSet<int>();
        foreach (var row in rows)
        {
            var metadata = row.VirtualItem!.Value;
            int top = 0;
            for (View? current = row; current is not null && current != content; current = current.Parent as View)
                top += current.Top;
            Assert(metadata.SourceVersion == lease.CommittedSourceVersion && metadata.Count == lease.CommittedCount &&
                indices.Add(metadata.Index) && Math.Abs((long)top - (long)metadata.Index * pitch) <= 1 &&
                Math.Abs(row.Height - pitch) <= 1,
                "Every mounted native row has current source metadata and exact logical geometry.");
        }
        int first = scroll.ScrollY / pitch;
        int end = Math.Min(lease.CommittedCount, (int)(((long)scroll.ScrollY + scroll.Height + pitch - 1) / pitch));
        Assert(Enumerable.Range(first, end - first).All(indices.Contains),
            "Every actually exposed native viewport index is realized exactly once.");
    }
}
