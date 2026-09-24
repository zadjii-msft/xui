#if DEBUG
using Microsoft.JSInterop;
using PortableDemo;
using Xui.Experimental.Portable;
using Xui.Experimental.Web;

// This bridge is absent from Release builds and inactive without the explicit test query.
internal sealed class BrowserTestDriver : IDisposable
{
    private readonly Host host;
    private readonly Greeting demo;
    private readonly IJSInProcessObjectReference module;
    private readonly DotNetObjectReference<BrowserTestDriver> reference;
    private readonly IJSInProcessObjectReference bridge;
    private readonly IJSRuntime js;
    private bool disposed;
    private BrowserMutationFixture? mutation;
    private BrowserViewportProbe? viewportProbe;
    private BrowserVirtualListFixture? virtualList;
    private BrowserFeatureFixture? feature;
    private IndexedDbApplicationStorage? storage;
    private IJSInProcessObjectReference? storageModule;

    private BrowserTestDriver(Host host, Greeting demo, IJSInProcessObjectReference module, IJSInProcessObjectReference bridge, IJSRuntime js)
    {
        this.host = host;
        this.demo = demo;
        this.module = module;
        this.bridge = bridge;
        this.js = js;
        reference = DotNetObjectReference.Create(this);
        bridge.InvokeVoid("install", reference);
    }

    internal static async Task<BrowserTestDriver?> Install(IJSRuntime js, IJSInProcessObjectReference module, Host host, Greeting demo)
    {
        var bridge = await js.InvokeAsync<IJSInProcessObjectReference>("import", "./test-driver.js");
        if (!bridge.Invoke<bool>("requested")) { bridge.Dispose(); return null; }
        return new BrowserTestDriver(host, demo, module, bridge, js);
    }

    [JSInvokable]
    public async Task<object> Command(string command, string? value)
    {
        if (command == "feature-start")
        {
            if (feature is not null) throw new InvalidOperationException("A feature fixture is already active.");
            host.Detach();
            feature = new BrowserFeatureFixture(module, value!);
            return new { active = true };
        }
        if (command.StartsWith("feature-", StringComparison.Ordinal))
            return (feature ?? throw new InvalidOperationException("Start the feature fixture first.")).Command(command["feature-".Length..], value);
        if (command == "virtual-start")
        {
            if (virtualList is not null) throw new InvalidOperationException("The virtual list is already active.");
            host.Detach();
            virtualList = new BrowserVirtualListFixture(module);
            return virtualList.State();
        }
        if (command.StartsWith("virtual-", StringComparison.Ordinal))
        {
            var list = virtualList ?? throw new InvalidOperationException("Start the virtual list first.");
            switch (command)
            {
                case "virtual-state": break;
                case "virtual-performance": return await list.MeasurePerformance();
                case "virtual-profile":
                    var metrics = await list.MeasurePerformance(profile: true);
                    return new { metrics, phases = list.PerformanceDiagnostics };
                case "virtual-cycle": list.Cycle(); break;
                case "virtual-dispose": list.Dispose(); return new { disposed = true };
                default: throw new ArgumentException("Unknown virtual list command.", nameof(command));
            }
            return list.State();
        }
        if (command == "viewport-probe-start")
        {
            if (viewportProbe is not null) throw new InvalidOperationException("The viewport probe is already active.");
            host.Detach();
            viewportProbe = new BrowserViewportProbe(module);
            return viewportProbe.Snapshot();
        }
        if (command.StartsWith("viewport-probe-", StringComparison.Ordinal))
        {
            var probe = viewportProbe ?? throw new InvalidOperationException("Start the viewport probe first.");
            switch (command)
            {
                case "viewport-probe-state": break;
                case "viewport-probe-commit": return probe.Commit();
                case "viewport-probe-max": probe.MaximumSource(); break;
                case "viewport-probe-close": probe.Close(); break;
                default: throw new ArgumentException("Unknown viewport probe command.", nameof(command));
            }
            return probe.Snapshot();
        }
        if (command == "layout-math") return BrowserLayoutMathFixture.Evaluate(value ?? throw new ArgumentNullException(nameof(value)));
        if (command == "input-probe-focus")
        {
            using var probe = new Host(new BrowserDispatcher(error => module.InvokeVoid("reportError", "errors", error.ToString())));
            Control target;
            using (var build = probe.BeginBuild())
            {
                var root = probe.Stack(Axis.Vertical);
                target = value switch
                {
                    "progress" => probe.Progress("Focus probe"),
                    "toggle" => probe.Toggle("Focus probe"),
                    "checkbox" => probe.CheckBox("Focus probe"),
                    _ => throw new ArgumentException("Unknown focus probe.", nameof(value))
                };
                root.Add(target);
                probe.SetContent(root);
                build.Complete();
            }
            probe.Attach(new DomBackend(module, "input-focus-probe", "errors"));
            return new { focused = probe.TryFocus(target), actual = probe.HasFocus(target) };
        }
        if (command == "input-focus")
        {
            Control control = value switch
            {
                "input" => demo.Input,
                "button" => demo.IncrementButton,
                "label" => demo.GreetingLabel,
                "scroll" => (Control)demo.Root.Children[1],
                _ => throw new ArgumentException("Unknown focus target.", nameof(value))
            };
            return new { focused = host.TryFocus(control), actual = host.HasFocus(control) };
        }
        if (command == "input-has-focus") return new { focused = host.HasFocus(demo.Input) };
        if (command == "input-get-selection") return host.GetSelection(demo.Input);
        if (command == "input-set-selection")
        {
            var parts = (value ?? throw new ArgumentNullException(nameof(value))).Split(',');
            if (parts.Length != 2) throw new ArgumentException("Expected two selection offsets.", nameof(value));
            host.SetSelection(demo.Input, new(int.Parse(parts[0]), int.Parse(parts[1])));
            return host.GetSelection(demo.Input);
        }
        if (command == "storage-open")
        {
            if (storage is not null) throw new InvalidOperationException("Close storage before opening another database.");
            storageModule ??= await js.InvokeAsync<IJSInProcessObjectReference>("import", "./_content/Xui.Web/xui-storage.js");
            storage = new IndexedDbApplicationStorage(storageModule, value!, 1024);
            return new { opened = true };
        }
        if (command.StartsWith("storage-", StringComparison.Ordinal))
        {
            var current = storage ?? throw new InvalidOperationException("Storage is not open.");
            if (command == "storage-close") { current.Dispose(); storage = null; return new { closed = true }; }
            if (command == "storage-read")
            {
                var read = await current.ReadAsync(value!);
                return new { status = read.Status.ToString(),
                    exists = read.Status == OperationStatus.Completed ? (bool?)read.Value.Exists : null,
                    text = read.Status == OperationStatus.Completed ? System.Text.Encoding.UTF8.GetString(read.Value.Data.Span) : null,
                    error = read.Error?.Message };
            }
            if (command == "storage-cancel")
            {
                using var source = new CancellationTokenSource();
                var pending = current.WriteAsync("document", System.Text.Encoding.UTF8.GetBytes("cancelled replacement"), source.Token);
                source.Cancel();
                try { await pending; }
                catch (OperationCanceledException) when (pending.IsCanceled) { return new { taskCancelled = true }; }
                throw new InvalidOperationException("The storage operation did not cancel.");
            }
            var result = command switch
            {
                "storage-write" => await current.WriteAsync("document", System.Text.Encoding.UTF8.GetBytes(value!)),
                "storage-delete" => await current.DeleteAsync(value!),
                _ => throw new ArgumentException("Unknown storage test command.", nameof(command))
            };
            return new { status = result.Status.ToString(), error = result.Error?.Message };
        }
        if (command.StartsWith("services-", StringComparison.Ordinal))
        {
            using var servicesModule = await js.InvokeAsync<IJSInProcessObjectReference>("import", "./_content/Xui.Web/xui-services.js");
            var services = new BrowserPlatformServices(servicesModule);
            using var cancellation = new CancellationTokenSource();
            switch (command)
            {
                case "services-availability":
                    return Enum.GetValues<ServiceCapability>().ToDictionary(capability => capability.ToString(),
                        capability => services.GetAvailability(capability).ToString());
                case "services-read":
                    var read = await services.ReadClipboardAsync();
                    return new { status = read.Status.ToString(), value = read.Status == OperationStatus.Completed ? read.Value : null, error = read.Error?.Message };
                case "services-write":
                    var written = await services.WriteClipboardAsync(value ?? "");
                    return new { status = written.Status.ToString(), value = written.Status == OperationStatus.Completed ? (bool?)written.Value : null, error = written.Error?.Message };
                case "services-cancelled":
                    cancellation.Cancel();
                    var canceled = services.WriteClipboardAsync("never written", cancellation.Token);
                    try { await canceled; }
                    catch (OperationCanceledException) when (canceled.IsCanceled) { return new { taskCancelled = true }; }
                    throw new InvalidOperationException("The service task did not cancel.");
                case "services-cancel-pending":
                    cancellation.CancelAfter(TimeSpan.FromMilliseconds(50));
                    var pending = services.ReadClipboardAsync(cancellation.Token);
                    try { await pending; }
                    catch (OperationCanceledException) when (pending.IsCanceled) { return new { taskCancelled = true }; }
                    throw new InvalidOperationException("The pending service task did not cancel.");
                case "services-uri":
                    var opened = await services.OpenUriAsync(new Uri(value!, UriKind.RelativeOrAbsolute));
                    return new { status = opened.Status.ToString(), value = opened.Status == OperationStatus.Completed ? (bool?)opened.Value : null, error = opened.Error?.Message };
                default: throw new ArgumentException("Unknown service test command.", nameof(command));
            }
        }
        if (command == "mutation-start")
        {
            if (mutation is not null) throw new InvalidOperationException("The mutation fixture is already active.");
            host.Detach();
            mutation = new BrowserMutationFixture(module);
            return mutation.State();
        }
        if (command.StartsWith("mutation-", StringComparison.Ordinal))
        {
            var fixture = mutation ?? throw new InvalidOperationException("Start the mutation fixture first.");
            switch (command)
            {
                case "mutation-state": break;
                case "mutation-reconcile": fixture.Reconcile(value ?? throw new ArgumentNullException(nameof(value))); break;
                case "mutation-attach": fixture.Attach(); break;
                case "mutation-dispose": fixture.Dispose(); return new { disposed = true };
                default: throw new ArgumentException("Unknown mutation test command.", nameof(command));
            }
            return fixture.State();
        }
        switch (command)
        {
            case "state": break;
            case "layout-on": demo.Input.SetConstraints(120, AxisConstraints.Auto); break;
            case "layout-off": demo.Input.SetConstraints(null, null); break;
            case "entry": demo.Entry = value ?? throw new ArgumentNullException(nameof(value)); break;
            case "count": demo.Count = int.Parse(value!); break;
            case "visible": demo.Input.Visible = bool.Parse(value!); break;
            case "enabled": ((Control)demo.Root.Children[1]).Enabled = bool.Parse(value!); break;
            case "caption": demo.Input.SetCaptionVisible(bool.Parse(value!)); break;
            case "help": demo.Input.Help = value!; break;
            case "detach": host.Detach(); break;
            case "attach": host.Attach(new DomBackend(module, "app", "errors")); break;
            case "dispose": host.Dispose(); return new { disposed = true };
            case "throw": demo.Input.Submitted += Fail; break;
            case "unthrow": demo.Input.Submitted -= Fail; break;
            case "dispatch": await host.DispatchAsync(() => demo.Count++); break;
            default: throw new ArgumentException("Unknown test command.", nameof(command));
        }
        return new { count = demo.Count, entry = demo.Entry, message = demo.Message, attached = host.IsAttached };
    }

    private static void Fail() => throw new InvalidOperationException("Intentional browser callback failure.");

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        try
        {
            try { storage?.Dispose(); }
            finally { storageModule?.Dispose(); }
        }
        finally
        {
            try
            {
                try
                {
                    try { feature?.Dispose(); }
                    finally { virtualList?.Dispose(); }
                }
                finally
                {
                    try { viewportProbe?.Dispose(); }
                    finally { mutation?.Dispose(); }
                }
            }
            finally
            {
                try { bridge.InvokeVoid("uninstall"); }
                finally { reference.Dispose(); bridge.Dispose(); }
            }
        }
    }
}
#endif
