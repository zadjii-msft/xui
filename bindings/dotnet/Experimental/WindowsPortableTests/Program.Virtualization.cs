using System.Runtime.InteropServices;
using System.Reflection;
using PortableDemo;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void ViewportAbiContracts()
    {
        Type Type(string name) => typeof(Window).Assembly.GetType("Xui.Native+" + name, throwOnError: true)!;
        var options = Type("VirtualViewportOptions");
        var rectangle = Type("VirtualViewportRectValue");
        var request = Type("VirtualViewportRequestValue");
        var interaction = Type("ControlInteractionValue");
        var item = Type("VirtualItemInfoValue");
        Check(Marshal.SizeOf(options) == 24 && Marshal.OffsetOf(options, "SourceVersion").ToInt64() == 16,
            "The managed native viewport option ABI is not 24 bytes.");
        Check(Marshal.SizeOf(rectangle) == 16, "The native viewport rectangle ABI is not 16 bytes.");
        Check(Marshal.SizeOf(request) == 72 && Marshal.OffsetOf(request, "Epoch").ToInt64() == 8 &&
            Marshal.OffsetOf(request, "Committed").ToInt64() == 32 &&
            Marshal.OffsetOf(request, "Requested").ToInt64() == 48 &&
            Marshal.OffsetOf(request, "Blocked").ToInt64() == 64,
            "The managed native viewport request ABI has incorrect offsets.");
        Check(Marshal.SizeOf(interaction) == 16 && Marshal.OffsetOf(interaction, "IsComposing").ToInt64() == 12,
            "The native control-interaction ABI has incorrect offsets.");
        Check(Marshal.SizeOf(item) == 40 && Marshal.OffsetOf(item, "SourceVersion").ToInt64() == 16 &&
            Marshal.OffsetOf(item, "Key").ToInt64() == 24 && Marshal.OffsetOf(item, "KeyLength").ToInt64() == 32,
            "The native virtual row metadata ABI has incorrect offsets.");

        void Set(object target, string field, object value) =>
            target.GetType().GetField(field, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(target, value);
        object Header()
        {
            var value = Activator.CreateInstance(request)!;
            Set(value, "Size", 72u);
            Set(value, "Version", 0x10000u);
            Set(value, "Epoch", 1ul);
            Set(value, "RequestedSourceVersion", 1ul);
            return value;
        }
        var decode = typeof(Xui.VirtualViewportLease).GetMethod("Decode", BindingFlags.Static | BindingFlags.NonPublic)!;
        Xui.VirtualViewportRequest Decode(object value)
        {
            try { return (Xui.VirtualViewportRequest)decode.Invoke(null, [value])!; }
            catch (TargetInvocationException error) when (error.InnerException is not null)
            {
                System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(error.InnerException).Throw();
                throw;
            }
        }
        Check(Decode(Header()) == new Xui.VirtualViewportRequest(1, 0, 1, default, default, false),
            "The initial uncommitted native request did not decode faithfully.");
        foreach (var (field, invalid) in new (string Field, object Value)[]
        {
            ("Size", 71u), ("Version", 0u), ("Epoch", 0ul), ("Epoch", (ulong)long.MaxValue + 1),
            ("RequestedSourceVersion", 0ul), ("RequestedSourceVersion", (ulong)long.MaxValue + 1),
            ("CommittedSourceVersion", (ulong)long.MaxValue + 1), ("CommittedSourceVersion", 2ul),
            ("Blocked", 2u), ("Reserved", 1u)
        })
        {
            var value = Header();
            Set(value, field, invalid);
            Throws<InvalidOperationException>(() => Decode(value));
        }
        foreach (var (field, invalid) in new[] { ("Offset", -1f), ("Width", float.NaN),
            ("Height", float.PositiveInfinity), ("Extent", -1f) })
        {
            var value = Header();
            var rect = Activator.CreateInstance(rectangle)!;
            Set(rect, field, invalid);
            Set(value, "Requested", rect);
            Throws<InvalidOperationException>(() => Decode(value));
        }
        var maximum = Header();
        Set(maximum, "Epoch", (ulong)long.MaxValue);
        Set(maximum, "RequestedSourceVersion", (ulong)long.MaxValue);
        Check(Decode(maximum).Epoch == long.MaxValue && Decode(maximum).RequestedSourceVersion == long.MaxValue,
            "Valid signed epoch/source-version maxima were not preserved.");
    }

    private static void ViewportCapabilityFallback()
    {
        using var window = new Window("Viewport capability fallback");
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        P.ScrollView viewport;
        using (var build = host.BeginBuild())
        {
            var content = host.Stack(P.Axis.Vertical).Add(host.TextInput("A retained editor"));
            viewport = host.ScrollView(content, "Unleased viewport");
            host.SetContent(host.Stack(P.Axis.Vertical).Add(viewport, 1));
            build.Complete();
        }
        var backend = new WindowsBackend(surface, dispatcher);
        Check(!backend.SupportsVirtualization, "The fallback fixture requires a runtime without qualified virtualization.");
        host.Attach(backend);
        uint attached = HandleCount(window);
        bool notified = false;
        Throws<NotSupportedException>(() => host.BeginVirtualViewport(viewport, 10000, 128, 1, _ => notified = true));
        Check(host.IsAttached && !notified && HandleCount(window) == attached,
            "An unsupported viewport lease changed or leaked the existing native attachment.");
        host.Detach();
        Check(HandleCount(window) == baseline, "Viewport capability rejection leaked native handles.");
    }

    private static Xui.VirtualViewportLease NativeViewport(Window window)
    {
        var registry = (System.Collections.IDictionary)typeof(Window).GetField("virtualViewports",
            BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window)!;
        return registry.Values.Cast<Xui.VirtualViewportLease>().Single();
    }

    private static int NativeObserverCount(Window window, string name) =>
        ((System.Collections.IDictionary)typeof(Window).GetField(name,
            BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window)!).Count;

    private static void NativeViewportOwnership()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Native viewport callback ownership", 500, 600);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var content = surface.BeginUpdate();
        var input = window.TextInput("Interaction observer");
        var scroll = window.ScrollView(window.Stack(), "Empty leased viewport").SetFillViewport(false);
        content.Commit(window.Stack().Add(input).Add(scroll, 1));
        int interactions = 0, requests = 0;
        using var observer = input.ObserveInteraction(_ => interactions++);
        using var lease = scroll.BeginVirtualViewport(0, 128, 1, _ => requests++);
        Check(interactions == 0 && requests == 0, "Native observer creation invoked a synchronous callback.");
        Check(NativeObserverCount(window, "controlInteractions") == 1 && NativeObserverCount(window, "virtualViewports") == 1,
            "Native observer roots were not registered with their window.");
        application.Show(window);
        var ui = new NativeUi(application);
        var work = Task.Run(() =>
        {
            try
            {
                ui.Wait(() => interactions != 0 && requests != 0);
                int beforeInteractions = 0, beforeRequests = 0;
                ui.Ui(() =>
                {
                    var request = lease.GetRequest();
                    Check(request.Epoch > 0 && request.RequestedSourceVersion == 1 && request.CommittedSourceVersion == 0,
                        "A read-only native lease published content without a commit.");
                    content.Dispose();
                    beforeInteractions = interactions;
                    beforeRequests = requests;
                    Check(HandleCount(window) == baseline &&
                        NativeObserverCount(window, "controlInteractions") == 0 && NativeObserverCount(window, "virtualViewports") == 0,
                        "Content retirement leaked native handles or managed observer roots.");
                    Throws<ObjectDisposedException>(() => lease.GetRequest());
                    observer.Dispose();
                    lease.Dispose();
                });
                ui.Ui(() => Check(interactions == beforeInteractions && requests == beforeRequests,
                    "Retired native observer callbacks targeted a replacement lifetime."));
            }
            finally { ui.Ui(window.Close); }
        });
        try { application.Run(); }
        finally { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }

        using var unshown = new Window("Unshown viewport roots");
        var unshownInput = unshown.TextInput("Unshown observer");
        var unshownScroll = unshown.ScrollView(unshown.Stack(), "Unshown viewport").SetFillViewport(false);
        unshown.SetContent(unshown.Stack().Add(unshownInput).Add(unshownScroll, 1));
        bool unexpected = false;
        using var unshownObserver = unshownInput.ObserveInteraction(_ => unexpected = true);
        using var unshownLease = unshownScroll.BeginVirtualViewport(0, 128, 1, _ => unexpected = true);
        unshown.Dispose();
        Check(!unexpected && NativeObserverCount(unshown, "controlInteractions") == 0 &&
            NativeObserverCount(unshown, "virtualViewports") == 0,
            "Destroying an unshown window leaked observer roots or executed queued callbacks.");
    }

    private static void NativeVirtualizationScenarios(bool measurePerformance = true, int scrollCycles = 80, int attachmentCycles = 100)
    {
        using var application = new Application();
        using var window = application.CreateWindow("Portable native scenarios", 620, 960, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var sample = VirtualList.CreateForViewport(host);
        Check(sample.RowsView.Children.Count == 0 && sample.Controller.Count == 0,
            "The virtual sample materialized huge ordinary content before leasing its viewport.");
        var backend = new WindowsBackend(surface, dispatcher);
        Check(backend.SupportsVirtualization, "The required native virtualization protocol is unavailable.");
        host.Attach(backend);
        int requests = 0;
        P.IVirtualViewportLease Bind()
        {
            var lease = host.BeginVirtualViewport(sample.Viewport, sample.Controller.RequestedCount,
                sample.Controller.RowHeight, sample.Controller.RequestedSourceVersion, request =>
                {
                    requests++;
                    long started = System.Diagnostics.Stopwatch.GetTimestamp();
                    try { sample.OnViewportRequested(request); }
                    finally
                    {
                        double elapsed = System.Diagnostics.Stopwatch.GetElapsedTime(started).TotalMilliseconds;
                        if (elapsed >= 1000 || requests % 100 == 0)
                            Console.WriteLine($"Viewport delivery {requests}: epoch={request.Epoch}, offset={request.Requested.Offset}, blocked={request.IsBlocked}, duration={elapsed:F1}ms.");
                    }
                });
            sample.AttachViewport(lease, host.SetVirtualItemInfo);
            return lease;
        }
        var viewport = Bind();
        Check(requests == 0, "Beginning a viewport synchronously invoked its consumer.");
        var initial = NativeViewport(window).GetRequest();
        Check(initial.Committed == default && initial.CommittedSourceVersion == 0 &&
            initial.RequestedSourceVersion > 0 && initial.Requested.Extent == 1_280_000,
            "Initial logical publication or declared extent is incorrect.");
        application.Show(window);
        var ui = new Driver(application, window, host, () => backend);
        var work = Task.Run(() =>
        {
            try
            {
                ui.Wait(() => sample.Controller.Count == 10000 && sample.Controller.ViewportHeight > 0 &&
                    sample.Controller.Mounted.Count != 0 && !sample.Controller.IsDeferred);
                ui.Ui(() =>
                {
                    var request = NativeViewport(window).GetRequest();
                    Check(request.CommittedSourceVersion == sample.Controller.SourceVersion &&
                        request.Committed.Extent == 1_280_000, "The first native publication did not match the controller source.");
                    Check(sample.Controller.Mounted.Count <= 20 && HandleCount(window) <= 128,
                        "Ten thousand items allocated an unbounded number of native peers.");
                });

                const string firstKey = "task-00000";
                const string lastKey = "task-09999";
                ui.Change("virtual-" + firstKey, "Retained virtual draft");
                nint retained = ui.Ui(() =>
                {
                    var row = sample.Controller.Mounted[firstKey];
                    Check(host.TryFocus(row.Input), "The first virtual editor did not receive native focus.");
                    host.SetSelection(row.Input, new(2, 6));
                    return GetFocus();
                });
                ui.Wait(() => sample.Controller.Mounted[firstKey].Input.Interaction is { HasFocus: true });
                ui.Ui(() => viewport.RequestOffset(float.MaxValue));
                ui.Wait(() => sample.Controller.Offset > 1_270_000 && !sample.Controller.IsDeferred);
                ui.Ui(() =>
                {
                    Check(sample.Controller.Mounted.ContainsKey(firstKey) && IsWindow(retained) && GetFocus() == retained &&
                        host.GetSelection(sample.Controller.Mounted[firstKey].Input) == new P.TextSelection(2, 6),
                        "A focused row was recreated or lost selection when pinned far outside the viewport.");
                    Check(sample.Controller.Mounted.Count <= 20 && HandleCount(window) <= 128,
                        "A far focus pin materialized intervening rows.");
                    viewport.RequestOffset(0);
                });
                ui.Wait(() => sample.Controller.Offset == 0 && !sample.Controller.IsDeferred);
                ui.Ui(() =>
                {
                    Check(GetFocus() == retained && sample.Data.Items[firstKey].Draft == "Retained virtual draft",
                        "Returning to a pinned row lost native identity or its authored draft.");
                    sample.Reveal(lastKey);
                });
                ui.Wait(() => sample.Controller.Mounted.TryGetValue(lastKey, out var row) && host.HasFocus(row.Input));
                ui.Ui(() =>
                {
                    var editor = GetFocus();
                    var content = GetParent(editor);
                    Check(GetClientRect(content, out var contentBounds) && contentBounds.Bottom > 0 && contentBounds.Bottom < 32767,
                        "The virtual content HWND exceeded the native size limit.");
                    var scroll = backend.FindControls("virtual-scroll").Single().GetBounds();
                    var field = backend.FindControls("virtual-" + lastKey).Single().GetBounds();
                    Check(field.Y >= scroll.Y - 1 && field.Y + field.Height <= scroll.Y + scroll.Height + 1,
                        "The final item did not appear inside the actual viewport.");
                    Check(GetWindowRect(editor, out var nativeBounds) && nativeBounds.Bottom > nativeBounds.Top,
                        "The final row has no real native editor geometry.");
                    Check(sample.Controller.Offset > 1_270_000 && sample.Controller.Mounted.Count <= 20,
                        "End navigation did not remain viewport-bounded.");
                });

                ui.Click("virtual-first");
                ui.Wait(() => sample.Controller.Offset == 0 && host.HasFocus(sample.Controller.Mounted[firstKey].Input));
                ui.Ui(() =>
                {
                    Check(sample.Data.Items[firstKey].Draft == "Retained virtual draft" &&
                        host.GetSelection(sample.Controller.Mounted[firstKey].Input) == new P.TextSelection(2, 6),
                        "Recycling and keyboard-style navigation lost saved draft or selection.");
                    host.TryFocus((P.Button)ui.Model("virtual-last"));
                });
                ui.Wait(() => sample.Controller.Mounted.Values.All(row => row.Input.Interaction is not { HasFocus: true }));
                if (measurePerformance) NativeVirtualizationPerformance(ui, sample, viewport, window);
                for (int i = 0; i < scrollCycles; i++)
                {
                    float offset = i * 15487 % 1_270_000;
                    ui.Ui(() => viewport.RequestOffset(offset));
                    ui.Wait(() => Math.Abs(sample.Controller.Offset - NativeViewport(window).GetRequest().Requested.Offset) <= 1 &&
                        !sample.Controller.IsDeferred);
                    ui.Ui(() => Check(sample.Controller.Mounted.Count <= 20 && HandleCount(window) <= 128,
                        "Repeated native scrolling leaked or accumulated row peers."));
                    if ((i + 1) % 10 == 0) Console.WriteLine($"Native viewport correctness scrolls: {i + 1}/{scrollCycles}.");
                }

                ui.Click("virtual-reverse");
                ui.Wait(() => sample.Controller.SourceVersion == sample.Controller.RequestedSourceVersion &&
                    !sample.Controller.IsDeferred);
                ui.Click("virtual-filter");
                ui.Wait(() => sample.Controller.Count == 5000 && !sample.Controller.IsDeferred);
                ui.Ui(() => Check(NativeViewport(window).GetRequest().Committed.Extent == 640_000,
                    "A source change published an incorrect logical extent."));

                ui.Ui(() => sample.Reveal(firstKey));
                ui.Wait(() => sample.Controller.Mounted.TryGetValue(firstKey, out var row) && host.HasFocus(row.Input));
                nint composing = ui.Ui(() =>
                {
                    var editor = GetFocus();
                    SendMessage(editor, 0x010d, 0, 0);
                    return editor;
                });
                ui.Wait(() => sample.Controller.Mounted[firstKey].Input.Interaction is { IsComposing: true });
                float committedOffset = ui.Ui(() => sample.Controller.Offset);
                float committedHeight = ui.Ui(() => sample.Controller.ViewportHeight);
                var originalWindow = ui.Ui(() =>
                {
                    Check(GetWindowRect(GetAncestor(composing, 2), out var bounds), "The viewport window has no native bounds.");
                    return bounds;
                });
                ui.Ui(() => viewport.RequestOffset(0));
                ui.Wait(() => NativeViewport(window).GetRequest().IsBlocked);
                ui.Ui(() =>
                {
                    Check(sample.Controller.Offset == committedOffset && GetFocus() == composing && IsWindow(composing),
                        "Composition exposed a requested viewport before safe publication.");
                    Check(SetWindowPos(GetAncestor(composing, 2), 0, 0, 0,
                        originalWindow.Right - originalWindow.Left + 80, originalWindow.Bottom - originalWindow.Top + 120, 0x0016),
                        "The composing viewport could not be enlarged.");
                });
                ui.Wait(() => NativeViewport(window).GetRequest().Requested.Height > committedHeight);
                ui.Ui(() =>
                {
                    var request = NativeViewport(window).GetRequest();
                    Check(request.IsBlocked && request.Committed.Height == committedHeight &&
                        sample.Controller.ViewportHeight == committedHeight && GetFocus() == composing,
                        "Composition published viewport growth before its required rows could be staged.");
                    Check(SetWindowPos(GetAncestor(composing, 2), 0, 0, 0,
                        originalWindow.Right - originalWindow.Left - 60, originalWindow.Bottom - originalWindow.Top - 120, 0x0016),
                        "The composing viewport could not be reduced.");
                });
                ui.Wait(() => NativeViewport(window).GetRequest().Requested.Height < committedHeight);
                ui.Ui(() =>
                {
                    var request = NativeViewport(window).GetRequest();
                    Check(request.IsBlocked && request.Committed.Height == committedHeight &&
                        GetFocus() == composing && IsWindow(composing),
                        "Clipping a composing viewport changed its logical publication or editor lifetime.");
                    SendMessage(composing, 0x010e, 0, 0);
                });
                ui.Wait(() => !NativeViewport(window).GetRequest().IsBlocked &&
                    sample.Controller.Mounted.Values.All(row => row.Input.Interaction is not { IsComposing: true }));
                ui.Wait(() => sample.Controller.Offset == 0 && !sample.Controller.IsDeferred);
                ui.Ui(() => Check(SetWindowPos(GetAncestor(composing, 2), 0, 0, 0,
                    originalWindow.Right - originalWindow.Left, originalWindow.Bottom - originalWindow.Top, 0x0016),
                    "The viewport window could not restore its original allocation."));
                ui.Wait(() => Math.Abs(sample.Controller.ViewportHeight - committedHeight) <= 1 && !sample.Controller.IsDeferred);

                for (int cycle = 0; cycle < attachmentCycles; cycle++)
                {
                    ui.Ui(() => sample.Reveal(firstKey));
                    ui.Wait(() => sample.Controller.Mounted.TryGetValue(firstKey, out var row) && host.HasFocus(row.Input) &&
                        !sample.Controller.IsDeferred, () =>
                    {
                        var request = NativeViewport(window).GetRequest();
                        bool mounted = sample.Controller.Mounted.TryGetValue(firstKey, out var row);
                        return $"Cycle {cycle} reveal did not settle: offset={sample.Controller.Offset}, deferred={sample.Controller.IsDeferred}, count={sample.Controller.Count}, source={sample.Controller.SourceVersion}/{sample.Controller.RequestedSourceVersion}, mounted={mounted}, focused={(mounted && host.HasFocus(row!.Input))}, interaction={(mounted ? row!.Input.Interaction : null)}, native={request}.";
                    });
                    float savedOffset = 0;
                    long savedSource = 0;
                    ui.Ui(() =>
                    {
                        Check(sample.Data.Items[firstKey].Draft == "Retained virtual draft" &&
                            host.GetSelection(sample.Controller.Mounted[firstKey].Input) == new P.TextSelection(2, 6),
                            "Reattaching virtual content lost its logical draft or UTF-16 selection.");
                        savedOffset = sample.Controller.Offset;
                        savedSource = sample.Controller.RequestedSourceVersion;
                        nint previousEditor = GetFocus();
                        var oldLease = viewport;
                        sample.CaptureEditingState();
                        host.Detach();
                        Check(HandleCount(window) == baseline && !IsWindow(previousEditor) &&
                            NativeObserverCount(window, "virtualViewports") == 0 &&
                            NativeObserverCount(window, "controlInteractions") == 0,
                            "Virtualization teardown leaked native leases, observers, row providers, or editors.");
                        Throws<ObjectDisposedException>(() => oldLease.RequestOffset(0));
                        sample.PrepareForViewportAttachment();
                        Check(sample.RowsView.Children.Count == 0 && sample.Controller.Mounted.Count == 0 &&
                            sample.Controller.Count == 0 && sample.Controller.SourceVersion == 0 &&
                            sample.Controller.RequestedSourceVersion == savedSource && sample.Controller.RequestedCount == 5000,
                            "Reattachment did not clear the oversized ordinary tree before constructing native peers.");
                        backend = new WindowsBackend(surface, dispatcher);
                        host.Attach(backend);
                        Check(backend.FindControls("virtual-" + firstKey).Count == 0,
                            "A virtual editor was created before the new native viewport lease.");
                        viewport = Bind();
                    });
                    ui.Wait(() => sample.Controller.SourceVersion == savedSource && sample.Controller.Count == 5000 &&
                        Math.Abs(sample.Controller.Offset - savedOffset) <= 1 && !sample.Controller.IsDeferred &&
                        sample.Controller.Mounted.ContainsKey(firstKey));
                    if ((cycle + 1) % 10 == 0) Console.WriteLine($"Native viewport attachment cycles: {cycle + 1}/{attachmentCycles}.");
                }
                ui.Ui(() =>
                {
                    sample.CaptureEditingState();
                    host.Detach();
                    Check(HandleCount(window) == baseline, "A second virtual attachment leaked native resources.");
                    Check(requests > scrollCycles + attachmentCycles, "Native viewport requests were not delivered asynchronously.");
                });
            }
            finally { ui.Ui(window.Close); }
        });
        RunNativeWork(application, work);
    }

    private static void RunNativeWork(Application application, Task work)
    {
        Exception? runFailure = null;
        try { application.Run(); }
        catch (Exception error) { runFailure = error; }
        try { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }
        catch (Exception error)
        {
            if (runFailure is not null) throw new AggregateException("Native run loop and worker both failed.", runFailure, error);
            throw;
        }
        if (runFailure is not null) System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(runFailure).Throw();
    }

    private static void NativeVirtualizationPerformance(Driver ui, VirtualList sample, P.IVirtualViewportLease lease, Window window)
    {
        var offsets = ui.Ui(() => VirtualListPerformance.RequestOffsets(10000, sample.Controller.RowHeight,
            sample.Controller.ViewportHeight));
        var elapsed = new List<double>(offsets.Count);
        TaskCompletionSource<double>? completion = null;
        long started = 0;
        float target = 0;
        void Committed(P.VirtualViewportRequest request)
        {
            if (completion is null || request.RequestedSourceVersion != sample.Controller.SourceVersion ||
                Math.Abs(request.Requested.Offset - target) > 1) return;
            completion.TrySetResult(System.Diagnostics.Stopwatch.GetElapsedTime(started).TotalMilliseconds);
        }
        ui.Ui(() => sample.ViewportCommitted += Committed);
        try
        {
            foreach (float offset in offsets)
            {
                var measured = ui.Ui(() =>
                {
                    target = offset;
                    completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
                    started = System.Diagnostics.Stopwatch.GetTimestamp();
                    lease.RequestOffset(offset);
                    return completion.Task;
                });
                elapsed.Add(measured.WaitAsync(Timeout).GetAwaiter().GetResult());
            }
        }
        finally { ui.Ui(() => sample.ViewportCommitted -= Committed); }
        var result = VirtualListPerformance.Evaluate(elapsed);
        var context = ui.Ui(() =>
        {
            var request = NativeViewport(window).GetRequest();
            nint handle = FindWindow(null, "Portable native scenarios");
            return $"OS={Environment.OSVersion}; process={RuntimeInformation.ProcessArchitecture}; processors={Environment.ProcessorCount}; DPI={GetDpiForWindow(handle)}; font=native default WinUI; viewport={request.Committed.Width:0.##}x{request.Committed.Height:0.##}; pitch=128; overscan=2";
        });
        Console.WriteLine($"Windows native virtual viewport: p50={result.P50Milliseconds:F2}ms; p95={result.P95Milliseconds:F2}ms; max={result.MaximumMilliseconds:F2}ms; samples={result.SamplesMilliseconds.Count}; {context}");
        Check(result.MeetsInitialBudget,
            $"The recorded-host native viewport p95 {result.P95Milliseconds:F2}ms exceeded the initial {VirtualListPerformance.P95BudgetMilliseconds:F0}ms budget.");
    }

    private static void NativeVirtualizationPhaseProbe()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Viewport phase probe", 620, 960, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var sample = VirtualList.CreateForViewport(host);
        var backend = new WindowsBackend(surface, dispatcher);
        Check(backend.SupportsVirtualization, "Native phase profiling requires a qualified viewport backend.");
        var probe = new ViewportPhaseProbe(backend);
        host.Attach(probe);
        var ready = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var viewport = host.BeginVirtualViewport(sample.Viewport, 10000, 128, sample.Controller.RequestedSourceVersion, request =>
        {
            long started = System.Diagnostics.Stopwatch.GetTimestamp();
            probe.Clear();
            sample.OnViewportRequested(request);
            Console.WriteLine($"Viewport phases offset={request.Requested.Offset}, elapsed={System.Diagnostics.Stopwatch.GetElapsedTime(started).TotalMilliseconds:F2}ms: {probe.Report()}");
            if (sample.Ready) ready.TrySetResult();
        });
        sample.AttachViewport(viewport, host.SetVirtualItemInfo);
        application.Show(window);
        var ui = new NativeUi(application);
        var work = Task.Run(() =>
        {
            try
            {
                ready.Task.WaitAsync(Timeout).GetAwaiter().GetResult();
                foreach (float offset in new[] { 160_000f, 320_000f, 480_000f, 640_000f, 800_000f })
                {
                    ui.Ui(() => viewport.RequestOffset(offset));
                    ui.Wait(() => Math.Abs(sample.Controller.Offset - offset) <= 1 && !sample.Controller.IsDeferred);
                }
                ui.Ui(() => { sample.CaptureEditingState(); host.Detach(); });
            }
            finally { ui.Ui(window.Close); }
        });
        RunNativeWork(application, work);
    }

    [DllImport("user32.dll")] private static extern nint GetParent(nint window);
}
