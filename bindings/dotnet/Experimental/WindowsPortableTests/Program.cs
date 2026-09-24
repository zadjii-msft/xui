using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using PortableDemo;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static int assertions;
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(15);
    private static bool SupportsPortableConstraints => Xui.Element.SupportsAxisConstraints && ScrollView.SupportsFillViewport;

    [STAThread]
    private static void Main(string[] args)
    {
        if (args.Distinct(StringComparer.Ordinal).Count() != args.Length ||
            args.Any(arg => arg is not ("--require-mutation" or "--require-constraints" or "--require-grid" or "--require-virtualization" or "--services-only" or "--constraints-contracts-only" or "--constraints-fallback-only" or "--grid-fallback-only" or "--viewport-contracts-only" or "--viewport-fallback-only" or "--image-contracts-only" or "--image-cleanup-only" or "--image-only" or "--dynamic-only" or "--pre-virtualization-only" or "--virtualization-correctness-only" or "--viewport-lifetime-only" or "--viewport-reattach-probe" or "--viewport-phase-probe" or "--forms-only" or "--typography-only" or "--theme-only" or "--responsive-only" or "--pages-only" or "--label-only" or "--selection-only" or "--reveal-only" or "--reveal-preflight-only" or "--studio-only" or "--studio-drawer-only" or "--studio-row-metrics" or "--applications-only")) ||
            (args.Length > 1 && args.Any(arg => arg is not ("--require-mutation" or "--require-constraints" or "--require-grid" or "--require-virtualization"))))
            throw new ArgumentException("Usage: WindowsPortableTests [--require-mutation] [--require-constraints] [--require-grid] [--require-virtualization], or one isolated contracts/fallback mode.");
        if (args is ["--dynamic-only"])
        {
            DynamicApplicationScenarios();
            Console.WriteLine($"Windows dynamic application: {assertions} assertions passed.");
            return;
        }
        if (args is ["--virtualization-correctness-only"])
        {
            Check(ScrollView.SupportsVirtualViewport && Control.SupportsInteraction, "Native virtualization support is required.");
            NativeViewportOwnership();
            NativeVirtualizationScenarios(measurePerformance: false);
            Console.WriteLine($"Windows native virtualization correctness: {assertions} assertions passed; performance was not measured.");
            return;
        }
        if (args is ["--viewport-phase-probe"])
        {
            NativeVirtualizationPhaseProbe();
            return;
        }
        if (args is ["--viewport-lifetime-only"])
        {
            Check(ScrollView.SupportsVirtualViewport && Control.SupportsInteraction, "Native virtualization support is required.");
            NativeViewportOwnership();
            NativeVirtualizationScenarios(measurePerformance: false, scrollCycles: 0);
            Console.WriteLine($"Windows native viewport lifetime: {assertions} assertions passed; performance and the 80-scroll stress phase were not measured.");
            return;
        }
        if (args is ["--viewport-reattach-probe"])
        {
            Check(ScrollView.SupportsVirtualViewport && Control.SupportsInteraction, "Native virtualization support is required.");
            NativeVirtualizationScenarios(measurePerformance: false, scrollCycles: 0, attachmentCycles: 2);
            Console.WriteLine($"Windows viewport reattachment probe: {assertions} assertions passed; this is not the 100-cycle or performance gate.");
            return;
        }
        if (args is ["--forms-only"])
        {
            NativeFormsScenarios();
            NativeWorkshopScenarios();
            Console.WriteLine($"Windows bounded Forms: {assertions} assertions passed.");
            return;
        }
        if (args is ["--typography-only"])
        {
            NativeTypographyScenarios();
            Console.WriteLine($"Windows typography: {assertions} assertions passed.");
            return;
        }
        if (args is ["--applications-only"])
        {
            NativeApplicationScenarios();
            Console.WriteLine($"Windows shared application fixtures: {assertions} assertions passed with fake external services only.");
            return;
        }
        if (args is ["--theme-only"])
        {
            NativeThemeScenarios();
            SharedPresentationScenarios();
            Console.WriteLine($"Windows theme ownership: {assertions} assertions passed; native pixel/state/HC gates are separate.");
            return;
        }
        if (args is ["--responsive-only"])
        {
            NativeResponsiveScenarios();
            Console.WriteLine($"Windows owned viewport observation: {assertions} assertions passed.");
            return;
        }
        if (args is ["--pages-only"])
        {
            NativeRetainedPageScenarios();
            Console.WriteLine($"Windows retained page ownership: {assertions} assertions passed.");
            return;
        }
        if (args is ["--studio-only"])
        {
            NativeStudioScenarios();
            Console.WriteLine($"Windows actual Studio workspace: {assertions} assertions passed.");
            return;
        }
        if (args is ["--studio-drawer-only"])
        {
            NativeStudioScenarios(enableDrawer: true);
            Console.WriteLine($"Windows real Studio Operations drawer: {assertions} assertions passed.");
            return;
        }
        if (args is ["--label-only"])
        {
            NativeLabelLayoutScenarios();
            Console.WriteLine($"Windows explicit label layout: {assertions} assertions passed.");
            return;
        }
        if (args is ["--studio-row-metrics"])
        {
            NativeCatalogTextMetrics();
            return;
        }
        if (args is ["--selection-only"])
        {
            NativeSelectionScenarios();
            Console.WriteLine($"Windows native choice/range: {assertions} assertions passed.");
            return;
        }
        if (args is ["--reveal-only"])
        {
            NativeRevealTreePreflight();
            NativeRevealScenarios();
            Console.WriteLine($"Windows portable Reveal: {assertions} assertions passed.");
            return;
        }
        if (args is ["--reveal-preflight-only"])
        {
            NativeRevealTreePreflight();
            Console.WriteLine($"Windows Reveal tree preflight: {assertions} assertions passed without showing a native window.");
            return;
        }
        if (args is ["--viewport-contracts-only"])
        {
            ViewportAbiContracts();
            Console.WriteLine($"Windows viewport ABI contracts: {assertions} assertions passed without native UI.");
            return;
        }
        if (args is ["--image-contracts-only"])
        {
            ImageAbiContracts();
            Console.WriteLine($"Windows Image memory ABI: {assertions} assertions passed without native calls or UI.");
            return;
        }
        if (args is ["--image-only"])
        {
            NativeImageScenarios();
            Console.WriteLine($"Windows packaged-memory Image: {assertions} assertions passed.");
            return;
        }
        if (args is ["--image-cleanup-only"])
        {
            ImageCleanupFailureChecks();
            Console.WriteLine($"Windows Image cleanup failures: {assertions} assertions passed without native calls or UI.");
            return;
        }
        if (args is ["--viewport-fallback-only"])
        {
            ViewportCapabilityFallback();
            Console.WriteLine($"Windows viewport capability fallback: {assertions} assertions passed without showing a native window.");
            return;
        }
        if (args is ["--constraints-contracts-only"])
        {
            ConstraintValueContracts();
            Console.WriteLine($"Windows axis value contracts: {assertions} assertions passed without native UI.");
            return;
        }
        if (args is ["--constraints-fallback-only"])
        {
            Check(!SupportsPortableConstraints, "The fallback fixture requires a native runtime without qualified portable axis support.");
            PortableAxisConstraintScenarios();
            Console.WriteLine($"Windows axis capability fallback: {assertions} assertions passed without showing a native window.");
            return;
        }
        if (args is ["--grid-fallback-only"])
        {
            Check(!Grid.SupportsPortableLayout, "The fallback fixture requires a native runtime without qualified Grid support.");
            PortableGridScenarios();
            Console.WriteLine($"Windows Grid capability fallback: {assertions} assertions passed without showing a native window.");
            return;
        }
        ServiceContracts();
        if (args is ["--services-only"])
        {
            Console.WriteLine($"Windows service protocol: {assertions} assertions passed without native side effects.");
            return;
        }
        if (args.Contains("--require-mutation")) Check(ContentUpdate.SupportsMutation, "The native runtime does not provide the required mutation exports.");
        if (args.Contains("--require-constraints")) Check(SupportsPortableConstraints, "The native runtime does not provide the required axis and unbounded-scroll exports.");
        if (args.Contains("--require-grid")) Check(Grid.SupportsPortableLayout, "The native runtime does not provide the required Grid layout version.");
        if (args.Contains("--require-virtualization"))
            Check(ScrollView.SupportsVirtualViewport && Control.SupportsInteraction,
                "The native runtime does not provide the required viewport and interaction protocol.");
        ConstraintValueContracts();
        if (Xui.Element.SupportsAxisConstraints)
        {
            NativeAxisConstraintScenarios();
        }
        else Console.WriteLine("Native axis constraint exports unavailable; axis value contracts only.");
        if (SupportsPortableConstraints) SharedAxisSizingScenarios();
        else Console.WriteLine("Portable unbounded-scroll constraints unavailable; constrained attachment rejection fixtures only.");
        PortableAxisConstraintScenarios();
        PortableGridScenarios();
        if (Grid.SupportsPortableLayout && ContentUpdate.SupportsMutation) SharedGridSizingScenarios();
        NativeVisibilitySpacing();
        OwnershipAndFailure();
        NativeScenarios();
        GalleryScenarios();
        NativeSettingsScenarios();
        if (ContentUpdate.SupportsMutation)
        {
            AppendOwnership();
            NativeMutationScenarios();
            DynamicApplicationScenarios();
            NativeProfileScenarios();
        }
        else Console.WriteLine("Native mutation exports unavailable; fixed-tree compatibility fixtures only.");
        CallbackFailure();
        DispatchCancellation();
        if (!args.Contains("--pre-virtualization-only") && ScrollView.SupportsVirtualViewport && Control.SupportsInteraction)
        {
            NativeViewportOwnership();
            NativeVirtualizationScenarios();
        }
        Console.WriteLine($"Windows portable adapter: {assertions} assertions passed.");
    }

    private static void Check(bool condition, string message)
    {
        Interlocked.Increment(ref assertions);
        if (!condition) throw new InvalidOperationException(message);
    }

    private static void Throws<T>(Action action) where T : Exception
    {
        Interlocked.Increment(ref assertions);
        try { action(); }
        catch (T) { return; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private static ContentHost Surface(Window window)
    {
        var surface = window.CreateContentHost();
        window.SetContent(window.Stack().Add(surface, 1));
        return surface;
    }

    private static void OwnershipAndFailure()
    {
        using var window = new Window("Portable ownership");
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var greeting = new Greeting(host);
        uint attached = 0;
        for (int i = 0; i < 12; i++)
        {
            var backend = new WindowsBackend(surface, dispatcher);
            host.Attach(backend);
            var input = (TextInput)backend.FindControls("name").Single();
            if (i == 0) attached = HandleCount(window);
            Check(HandleCount(window) == attached && attached > baseline, "Attachment resources grew.");
            greeting.Entry = $"attachment {i}";
            Check(input.Text == greeting.Entry, "Reattached model was not applied.");
            host.Detach();
            Check(HandleCount(window) == baseline, "Detach leaked native handles.");
            Throws<XuiException>(() => _ = input.Text);
            backend.Dispose();
        }
        Throws<InvalidOperationException>(() => host.Attach(new MountFailure(new WindowsBackend(surface, dispatcher))));
        Check(!host.IsAttached && HandleCount(window) == baseline, "Post-mount failure leaked native scope.");
        host.Attach(new WindowsBackend(surface, dispatcher));
        Throws<ArgumentException>(() => greeting.Message = new string('x', 1_048_577));
        Check(!host.IsAttached && HandleCount(window) == baseline, "Update failure did not detach its scope.");
        Throws<ArgumentException>(() => host.Attach(new WindowsBackend(surface, dispatcher)));
        Check(HandleCount(window) == baseline, "Partial peer creation leaked native handles.");
        greeting.Message = "Recovered";
        host.Attach(new WindowsBackend(surface, dispatcher));
        Check(host.IsAttached, "An attachment failure poisoned the retained model.");
        using var competingHost = new P.Host(dispatcher);
        _ = new Greeting(competingHost);
        uint beforeRejected = HandleCount(window);
        Throws<InvalidOperationException>(() => competingHost.Attach(new WindowsBackend(surface, dispatcher)));
        Check(host.IsAttached && HandleCount(window) == beforeRejected, "A competing host replaced a live attachment.");
        host.Detach();

        using var other = new Window("Other portable owner");
        using var otherDispatcher = new WindowsDispatcher(other);
        Throws<ArgumentException>(() => _ = new WindowsBackend(surface, otherDispatcher));
        Task.Run(() => Throws<InvalidOperationException>(host.Detach)).GetAwaiter().GetResult();
        Check(HandleCount(window) == baseline, "Rejected operations allocated native handles.");
    }

    private sealed class MountFailure(WindowsBackend backend) : P.IBackend
    {
        public P.IElementPeer Create(P.Element element, P.IControlEvents events) => backend.Create(element, events);
        public void Mount(P.IElementPeer root) { backend.Mount(root); throw new InvalidOperationException("mount fixture"); }
        public void Dispose() => backend.Dispose();
    }

    private static void NativeScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Portable native scenarios", 560, 800);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        int uiThread = Environment.CurrentManagedThreadId;
        var order = new OrderBuilder(host);
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        application.Show(window);
        var driver = new Driver(application, window, host, () => backend);
        var work = Task.Run(() =>
        {
            try
            {
                using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("OrderScenarios.json")!;
                using var reader = new StreamReader(stream);
                Interlocked.Add(ref assertions, OrderScenarioRunner.Run(reader.ReadToEnd(), driver));
                driver.Ui(() =>
                {
                    var input = driver.Input("customer-name");
                    input.Focus();
                    nint edit = GetFocus();
                    Check(ClassName(edit) == "Edit", "The portable input is not a native Edit.");
                    int changes = 0;
                    order.CustomerNameInput.Changed += _ => changes++;
                    order.State = new OrderState { CustomerName = "Ada \u03bb \ud83d\ude00", Email = "ada@example.com" };
                    input.Selection = new(1, 3);
                    var selection = input.Selection;
                    for (int i = 0; i < 30; i++)
                        order.State = order.State with { CoffeeQuantity = i % 9 };
                    Check(GetFocus() == edit && input.Selection == selection && changes == 0,
                        "Unrelated updates replaced the native editor, selection, or echoed change.");
                    order.State = order.State with { CustomerName = "Bea \u03bb \ud83d\ude00" };
                    Check(input.Selection == selection && changes == 0, "Programmatic text update changed selection or echoed.");
                    SendText(edit, 0x000c, 0, "queued stale edit");
                    order.State = order.State with { CustomerName = "programmatic winner" };
                });
                driver.Ui(() => Check(order.State.CustomerName == "programmatic winner",
                    "A deferred native edit overwrote newer programmatic state."));

                driver.Change("customer-name", "Native Ada");
                driver.Ui(() => Check(order.State.CustomerName == "Native Ada", "Native input did not reach authored C#."));
                driver.Ui(() =>
                {
                    driver.Input("customer-name").Focus();
                    var edit = GetFocus();
                    var root = GetAncestor(edit, 2);
                    Check(GetWindowRect(edit, out var bounds) && bounds.Bottom - bounds.Top >= 20,
                        "The native captioned editor is clipped.");
                    Check(SetWindowPos(root, 0, 0, 0, 320, 420, 0x0016), "Could not resize the test window.");
                });
                driver.Wait(() => driver.Native("order-title").GetBounds().Width < 300);
                driver.Ui(() =>
                {
                    var input = driver.Input("customer-name");
                    input.Focus();
                    Check(GetWindowRect(GetFocus(), out var bounds) && bounds.Right - bounds.Left > 150 &&
                        bounds.Bottom - bounds.Top >= 20, "The narrow native editor has unusable geometry.");
                });

                for (int i = 0; i < 8; i++)
                {
                    driver.Ui(() =>
                    {
                        var oldInput = driver.Input("customer-name");
                        oldInput.Focus();
                        nint oldEdit = GetFocus();
                        SendText(oldEdit, 0x000c, 0, "retired event");
                        host.Detach();
                        Check(!IsWindow(oldEdit) && HandleCount(window) == baseline, "Detach left an HWND or scoped handle alive.");
                        backend = new WindowsBackend(surface, dispatcher);
                        host.Attach(backend);
                        Check(driver.Input("customer-name").Text == "Native Ada", "Reattachment lost retained state.");
                    });
                    driver.Ui(() => Check(order.State.CustomerName == "Native Ada", "A retired event reached a replacement attachment."));
                }
                driver.Ui(() =>
                {
                    host.Detach();
                    Check(HandleCount(window) == baseline, "Repeated live attachment leaked handles.");
                });

                var greetingHost = driver.Ui(() => new P.Host(dispatcher));
                var greeting = driver.Ui(() =>
                {
                    var component = new Greeting(greetingHost);
                    backend = new WindowsBackend(surface, dispatcher);
                    greetingHost.Attach(backend);
                    return component;
                });
                var greetingDriver = new Driver(application, window, greetingHost, () => backend);
                greetingDriver.Change("name", "portable Windows");
                greetingDriver.Click("submit");
                greetingDriver.Ui(() => Check(greeting.Message == "Hello, portable Windows!", "Shared Greeting did not execute through Host."));
                greetingDriver.Click("increment");
                greetingDriver.Ui(() =>
                {
                    Check(greeting.Count == 1 && greetingDriver.Native("count").Text == "Count: 1", "Greeting binding did not update in place.");
                    greetingHost.Dispose();
                });
                host.DispatchAsync(() => Check(Environment.CurrentManagedThreadId == uiThread,
                    "Accepted dispatch was not executed.")).WaitAsync(Timeout).GetAwaiter().GetResult();
                Throws<InvalidOperationException>(() => host.DispatchAsync(() => throw new InvalidOperationException("dispatch fixture"))
                    .WaitAsync(Timeout).GetAwaiter().GetResult());
                driver.Ui(() => Check(window.CallbackStatus == 0, "A native callback unexpectedly failed."));
            }
            finally { driver.Ui(window.Close); }
        });
        try { application.Run(); }
        finally { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }
    }

    private static void CallbackFailure()
    {
        using var window = new Window("Portable callback failure");
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var greeting = new Greeting(host);
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        greeting.IncrementButton.Click += () => greeting.Message = new string('x', 1_048_577);
        Check(window.Post(() => ((Button)backend.FindControls("increment").Single()).Invoke()), "Callback fixture post rejected.");
        var originalError = Console.Error;
        using var errorLog = new StringWriter();
        Console.SetError(errorLog);
        try { Throws<XuiException>(window.Run); }
        finally { Console.SetError(originalError); }
        Check(errorLog.ToString().Contains("Xui.Windows callback failed:", StringComparison.Ordinal),
            "The native callback failure was not explicitly reported.");
        Check(backend.CallbackError is ArgumentException && !host.IsAttached,
            "The authored update failure was swallowed or retained a broken attachment.");
        Check(HandleCount(window) == baseline, "Callback update failure leaked its scope.");
    }

    private static void GalleryScenarios()
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("GalleryScenarios.json")!;
        using var reader = new StreamReader(stream);
        string json = reader.ReadToEnd();
        foreach (string id in new[] { "task-board", "expense-ledger", "session-planner" })
        {
            using var application = new Application();
            using var window = application.CreateWindow("Portable native scenarios", 620, 960, visualStyle: VisualStyle.WinUI);
            var surface = Surface(window);
            uint baseline = HandleCount(window);
            using var dispatcher = new WindowsDispatcher(window);
            using var host = new P.Host(dispatcher);
            switch (id)
            {
                case "task-board": _ = new TaskBoard(host); break;
                case "expense-ledger": _ = new ExpenseLedger(host); break;
                default: _ = new SessionPlanner(host); break;
            }
            var backend = new WindowsBackend(surface, dispatcher);
            host.Attach(backend);
            application.Show(window);
            var driver = new Driver(application, window, host, () => backend);
            var work = Task.Run(() =>
            {
                try
                {
                    Interlocked.Add(ref assertions, ApplicationScenarioRunner.Run(json, id, driver));
                    driver.Ui(() =>
                    {
                        host.Detach();
                        Check(HandleCount(window) == baseline, $"Gallery '{id}' leaked native resources.");
                    });
                }
                finally { driver.Ui(window.Close); }
            });
            try { application.Run(); }
            finally { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }
        }
    }

    private static void DispatchCancellation()
    {
        using var window = new Window("Portable dispatch cancellation");
        var surface = Surface(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        _ = new Greeting(host);
        host.Attach(new WindowsBackend(surface, dispatcher));
        bool ran = false;
        Task? pending = null;
        Check(window.Post(() =>
        {
            pending = host.DispatchAsync(() => ran = true);
            window.Close();
        }), "Cancellation fixture post rejected.");
        window.Run();
        Check(pending is { IsCompleted: true } && !ran, "Accepted dispatch was abandoned or ran after close.");
        Throws<ObjectDisposedException>(() => pending!.GetAwaiter().GetResult());
        Throws<ObjectDisposedException>(() => host.DispatchAsync(() => ran = true).GetAwaiter().GetResult());
        host.Detach();

        using var other = new Window("Portable dispatcher disposal");
        using var otherDispatcher = new WindowsDispatcher(other);
        using var otherHost = new P.Host(otherDispatcher);
        var disposedPending = otherHost.DispatchAsync(() => ran = true);
        otherDispatcher.Dispose();
        Check(disposedPending.IsCompleted && !ran, "Explicit dispatcher disposal stranded accepted work.");
        Throws<ObjectDisposedException>(() => disposedPending.GetAwaiter().GetResult());

        using var neverShown = new Window("Unshown dispatcher disposal");
        using var neverShownDispatcher = new WindowsDispatcher(neverShown);
        using var neverShownHost = new P.Host(neverShownDispatcher);
        var unshownPending = neverShownHost.DispatchAsync(() => ran = true);
        neverShown.Dispose();
        Check(unshownPending.IsCompleted && !ran, "Disposing an unshown native window stranded accepted work.");
        Throws<ObjectDisposedException>(() => unshownPending.GetAwaiter().GetResult());
    }

    private class NativeUi(Application application)
    {
        internal T Ui<T>(Func<T> action)
        {
            var completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!application.Post(() =>
            {
                try { completion.SetResult(action()); }
                catch (Exception error) { completion.SetException(error); }
            })) throw new InvalidOperationException("Native UI dispatch rejected test work.");
            return completion.Task.WaitAsync(Timeout).GetAwaiter().GetResult();
        }
        internal void Ui(Action action) => Ui(() => { action(); return true; });
        internal void Wait(Func<bool> condition, Func<string>? failure = null)
        {
            var elapsed = System.Diagnostics.Stopwatch.StartNew();
            while (!Ui(condition))
            {
                if (elapsed.Elapsed > Timeout)
                    throw new TimeoutException(failure is null ? "The native state did not settle." : Ui(failure));
                Thread.Sleep(10);
            }
        }
    }

    private sealed class Driver(Application application, Window window, P.Host host, Func<WindowsBackend> backend)
        : NativeUi(application), IOrderScenarioDriver, IApplicationScenarioDriver, PortableMutation.IMutationScenarioDriver, ISettingsScenarioDriver
    {
        private readonly Dictionary<ulong, nint> nativeButtons = [];
        internal Control Native(string id) => backend().FindControls(id).Single();
        internal TextInput Input(string id) => (TextInput)Native(id);
        internal P.Control Model(string id)
        {
            return Walk(host.Root!).OfType<P.Control>().Single(control => control.AutomationId == id);
        }
        private static IEnumerable<P.Element> Walk(P.Element element) =>
            new[] { element }.Concat(element.Children.SelectMany(Walk));
        public bool Exists(string id) => Ui(() => backend().FindControls(id).Count != 0);
        public IReadOnlyList<string> Order() => Ui(() => (IReadOnlyList<string>)Walk(host.Root!).OfType<P.TextInput>()
            .Select(input => input.AutomationId)
            .Where(id => id.StartsWith("row-", StringComparison.Ordinal) && id.EndsWith("-input", StringComparison.Ordinal))
            .OrderBy(id => Native(id).GetBounds().Y).Select(id => id[..^6]).ToArray());
        public void Change(string id, string value)
        {
            Ui(() =>
            {
                var input = Input(id);
                input.Focus();
                Check(ClassName(GetFocus()) == "Edit", "The shared scenario did not use a native Edit.");
                Check(SendText(GetFocus(), 0x000c, 0, value) != 0, "Native input rejected text.");
            });
            Ui(() => Check(((P.TextInput)Model(id)).Text == value, "Deferred input did not reach the model."));
        }
        public void Click(string id)
        {
            Ui(() => Check(Native(id) is Button or Xui.Toggle or Xui.CheckBox, $"'{id}' is not clickable."));
            Ui(() => { if (IsWindowEnabled(ButtonWindow(id))) Native(id).Focus(); });
            Ui(() =>
            {
                nint button = ButtonWindow(id);
                Check(GetClientRect(button, out var bounds), "Native button geometry unavailable.");
                int point = (bounds.Bottom / 2 << 16) | bounds.Right / 2;
                SendMessage(button, 0x0201, 1, point);
                SendMessage(button, 0x0202, 0, point);
            });
            Ui(() => Check(window.CallbackStatus == 0, "Native click failed."));
        }
        public void Submit(string id)
        {
            int submits = 0;
            void Submitted() => submits++;
            Ui(() => ((P.TextInput)Model(id)).Submitted += Submitted);
            try
            {
                Ui(() =>
                {
                    Input(id).Focus();
                    Check(PostMessage(GetFocus(), 0x0100, 0x0d, 1), "Native Enter post failed.");
                });
                Wait(() => submits == 1);
            }
            finally { Ui(() => ((P.TextInput)Model(id)).Submitted -= Submitted); }
        }
        public string Text(string id) => Ui(() => Native(id).Text);
        public bool Enabled(string id)
        {
            if (Ui(() => Native(id) is TextInput))
            {
                return Ui(() =>
                {
                    var input = (TextInput)Native(id);
                    input.Focus();
                    Check(input.Focused, $"Native input '{id}' cannot be inspected.");
                    return IsWindowEnabled(GetFocus());
                });
            }
            nint native = Ui(() => ButtonWindow(id));
            // HWND properties are published by the native update pass, not by the model setter.
            Wait(() => IsWindowEnabled(native) == Model(id).Enabled,
                () => $"Native enabled state for '{id}' did not settle: native={IsWindowEnabled(native)}, model={Model(id).Enabled}, HWND={native}, bounds={Native(id).GetBounds()}.");
            return Ui(() => IsWindowEnabled(native));
        }
        private nint ButtonWindow(string id)
        {
            var control = Native(id);
            if (nativeButtons.TryGetValue(control.Id, out nint cached) && IsWindow(cached)) return cached;
            if (control is Button or Xui.Toggle or Xui.CheckBox && Model(id).Enabled && Model(id).Visible)
            {
                control.Focus();
                Check(control.Focused, $"Native button '{id}' did not focus.");
                return nativeButtons[control.Id] = GetFocus();
            }
            nint root = FindWindow(null, "Portable native scenarios");
            var expected = control.GetBounds();
            float scale = GetDpiForWindow(root) / 96f;
            var matches = new List<nint>();
            EnumChildWindows(root, (child, _) =>
            {
                var name = new StringBuilder(256);
                if (GetClassName(child, name, name.Capacity) == 0 || name.ToString() != "Xui.Control.1") return true;
                if (expected.Width == 0 || expected.Height == 0)
                {
                    var caption = new StringBuilder(256);
                    GetTypographyWindowText(child, caption, caption.Capacity);
                    if (caption.ToString() == control.Text) matches.Add(child);
                    return true;
                }
                Check(GetWindowRect(child, out var rectangle), "Native control bounds are unavailable.");
                var origin = new Point { X = rectangle.Left, Y = rectangle.Top };
                Check(ScreenToClient(root, ref origin), "Native control coordinates could not be converted.");
                if (Math.Abs(origin.X - expected.X * scale) <= 1.5 &&
                    Math.Abs(origin.Y - expected.Y * scale) <= 1.5 &&
                    Math.Abs(rectangle.Right - rectangle.Left - expected.Width * scale) <= 1.5 &&
                    Math.Abs(rectangle.Bottom - rectangle.Top - expected.Height * scale) <= 1.5)
                    matches.Add(child);
                return true;
            }, 0);
            Check(matches.Count == 1, $"Expected one native '{id}' control.");
            return nativeButtons[control.Id] = matches[0];
        }
        public bool Checked(string id) => Ui(() =>
        {
            var value = new FeatureValue { Size = (uint)Marshal.SizeOf<FeatureValue>(), Version = 0x00010001 };
            Check(FeatureGet(Native(id).Id, 46, ref value) == 0, "Native checked query failed.");
            return value.First != 0;
        });
        public string CheckState(string id) => Ui(() => ((Xui.CheckBox)Native(id)).State.ToString());
        public SettingsProgressSnapshot Progress(string id) => Ui(() =>
        {
            var progress = (Xui.Progress)Native(id);
            var range = progress.Range;
            bool indeterminate = progress.State == Xui.ProgressState.Indeterminate;
            return new SettingsProgressSnapshot(range.Minimum, range.Maximum, indeterminate ? null : progress.Value, indeterminate);
        });
        public bool Visible(string id) => Ui(() =>
        {
            if (backend().FindControls(id).Count == 0) return false;
            var value = new FeatureValue { Size = (uint)Marshal.SizeOf<FeatureValue>(), Version = 0x00010001 };
            Check(FeatureGet(Native(id).Id, 37, ref value) == 0, "Native visibility query failed.");
            return value.First != 0;
        });
    }

    private static uint HandleCount(Window window)
    {
        ulong handle = (ulong)typeof(Window).GetProperty("Handle", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window)!;
        Check(ContentHandleCount(handle, out uint count) == 0, "Native scope accounting failed.");
        return count;
    }
    private static string ClassName(nint window)
    {
        var text = new StringBuilder(256);
        Check(GetClassName(window, text, text.Capacity) > 0, "Native class lookup failed.");
        return text.ToString();
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct FeatureValue
    {
        public uint Size, Version;
        public double A, B, C, D;
        public ulong First, Second;
        public nint TextData;
        public uint TextLength, TextReserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    private struct Point { public int X, Y; }
    [DllImport("xui", EntryPoint = "xui_content_handle_count")] private static extern int ContentHandleCount(ulong window, out uint count);
    [DllImport("xui", EntryPoint = "xui_feature_get")] private static extern int FeatureGet(ulong control, uint property, ref FeatureValue value);
    [DllImport("user32.dll")] private static extern nint GetFocus();
    [DllImport("user32.dll")] private static extern nint GetAncestor(nint window, uint flags);
    [DllImport("user32.dll")] private static extern bool IsWindow(nint window);
    [DllImport("user32.dll")] private static extern bool IsWindowEnabled(nint window);
    private delegate bool EnumWindow(nint window, nint parameter);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(nint window, EnumWindow callback, nint parameter);
    [DllImport("user32.dll", EntryPoint = "FindWindowW", CharSet = CharSet.Unicode)] private static extern nint FindWindow(string? className, string title);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(nint window, out Rect rect);
    [DllImport("user32.dll")] private static extern bool GetClientRect(nint window, out Rect rect);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(nint window);
    [DllImport("user32.dll")] private static extern bool ScreenToClient(nint window, ref Point point);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(nint window, nint after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll", EntryPoint = "GetClassNameW", CharSet = CharSet.Unicode)] private static extern int GetClassName(nint window, StringBuilder text, int length);
    [DllImport("user32.dll", EntryPoint = "SendMessageW", CharSet = CharSet.Unicode)] private static extern nint SendText(nint window, uint message, nuint first, string text);
    [DllImport("user32.dll", EntryPoint = "SendMessageW")] private static extern nint SendMessage(nint window, uint message, nuint first, nint second);
    [DllImport("user32.dll", EntryPoint = "PostMessageW")] private static extern bool PostMessage(nint window, uint message, nuint first, nint second);
}
