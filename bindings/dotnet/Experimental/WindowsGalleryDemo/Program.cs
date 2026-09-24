using System.Runtime.InteropServices;
using System.Collections.Concurrent;
using PortableDemo;
using Xui;
using Xui.Experimental.Portable;
using Xui.Experimental.Windows;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        string sample = "Order";
        string? capture = null;
        bool smoke = false;
        bool operations = false;
        bool drawer = false;
        float? width = null, height = null;
        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--sample" when i + 1 < args.Length: sample = args[++i]; break;
                case "--capture" when i + 1 < args.Length: capture = Path.GetFullPath(args[++i]); break;
                case "--smoke": smoke = true; break;
                case "--operations": operations = true; break;
                case "--drawer": drawer = true; break;
                case "--width" when i + 1 < args.Length:
                    width = float.Parse(args[++i], System.Globalization.CultureInfo.InvariantCulture); break;
                case "--height" when i + 1 < args.Length:
                    height = float.Parse(args[++i], System.Globalization.CultureInfo.InvariantCulture); break;
                default: throw new ArgumentException($"Unknown or incomplete option '{args[i]}'.");
            }
        }
        if (sample is not ("Order" or "Tasks" or "Expenses" or "Planner" or "DynamicTasks" or "Settings" or "Axes" or "Grid" or "Profile" or "Virtual" or "Forms" or "Workshop" or "Cart" or "Services" or "Presentation" or "Studio" or "Image"))
            throw new ArgumentException("--sample requires Order, Tasks, Expenses, Planner, DynamicTasks, Settings, Axes, Grid, Profile, Virtual, Forms, Workshop, Cart, Services, Presentation, Studio, or Image.");
        if (width is { } w && (!float.IsFinite(w) || w < 240 || w > 3840) ||
            height is { } h && (!float.IsFinite(h) || h < 240 || h > 2160))
            throw new ArgumentOutOfRangeException(nameof(args), "Gallery dimensions must be finite and within the supported display range.");
        if (operations && (sample != "Studio" || capture is null))
            throw new ArgumentException("--operations requires an explicit Studio capture.");
        if (drawer && (sample != "Studio" || !Xui.Reveal.SupportsPortableState))
            throw new NotSupportedException("--drawer requires Studio and the qualified native Reveal capability.");

        using var application = new Application();
        using var window = application.CreateWindow($"XUI portable gallery - {sample}",
            width ?? (sample == "Studio" ? 1440 : 620), height ?? 960, visualStyle: VisualStyle.WinUI);
        var surface = window.CreateContentHost();
        window.SetContent(window.Stack().Add(surface, 1));
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new Host(dispatcher);
        var reported = new ConcurrentQueue<Exception>();
        void ReportFatal(Exception error)
        {
            reported.Enqueue(error);
            try { Console.Error.WriteLine($"Shared application failed: {error}"); }
            catch (Exception loggingError) { reported.Enqueue(loggingError); }
            try
            {
                window.PostUnscoped(() =>
                {
                    try { window.Close(); }
                    catch (Exception closeError) { reported.Enqueue(closeError); }
                });
            }
            catch (Exception dispatchError) { reported.Enqueue(dispatchError); }
        }
        Func<Task>? ownedOperation = null;
        using var picker = sample == "Services" ? new WindowsFilePicker(dispatcher, ReportFatal) : null;
        VirtualList? virtualList = null;
        WorkspaceStudio? studio = null;
        TaskCompletionSource? virtualReady = null;
        TaskCompletionSource? imageReady = null;
        string inputId;
        switch (sample)
        {
            case "Order": _ = new OrderBuilder(host); inputId = "customer-name"; break;
            case "Tasks": _ = new TaskBoard(host); inputId = "task-name"; break;
            case "Expenses": _ = new ExpenseLedger(host); inputId = "ledger-budget"; break;
            case "DynamicTasks": _ = DynamicTaskBoard.Create(host); inputId = "dynamic-draft"; break;
            case "Settings": _ = new SettingsShowcase(host); inputId = "settings-draft"; break;
            case "Axes": _ = new PortableLayout.AxisSizingShowcase(host); inputId = "axes-input"; break;
            case "Grid": _ = new PortableLayout.GridSizingShowcase(host); inputId = "grid-input"; break;
            case "Profile":
                var profileController = ProfileWorkspace.Create(host, new ProfileStorage(), ReportFatal).Controller;
                ownedOperation = () => profileController.LastOperation;
                inputId = "profile-name";
                break;
            case "Virtual":
                virtualList = VirtualList.CreateForViewport(host);
                virtualReady = new(TaskCreationOptions.RunContinuationsAsynchronously);
                inputId = "virtual-task-00000";
                break;
            case "Forms": _ = new FormsWorkbench(host); inputId = "forms-email"; break;
            case "Workshop": _ = new WorkshopRegistration(host); inputId = "workshop-name"; break;
            case "Presentation": _ = PresentationWorkbench.Create(host); inputId = "presentation-draft"; break;
            case "Image":
                var assets = new PackagedAssetManifest(typeof(Program).Assembly);
                var image = new ImageWorkbench(host, assets);
                if (smoke || capture is not null)
                {
                    imageReady = new(TaskCreationOptions.RunContinuationsAsynchronously);
                    image.Preview.StateChanged += state =>
                    {
                        if (state == ImageLoadState.Ready) imageReady.TrySetResult();
                        else if (state == ImageLoadState.Error)
                            imageReady.TrySetException(new InvalidOperationException(image.Preview.ErrorMessage));
                    };
                    image.Source = new(assets, "zoey.png");
                }
                inputId = "image-notes";
                break;
            case "Studio":
                studio = WorkspaceStudio.Create(host, new LocalStudioAnalysisService(), ReportFatal,
                    enableOperationsDrawer: drawer);
                ownedOperation = () => studio.LastOperation;
                inputId = "studio-theme";
                break;
            case "Cart":
                var cartController = EditableCart.Create(host, new LocalCartQuoteService(), ReportFatal).Controller;
                ownedOperation = () => cartController.LastOperation;
                inputId = "cart-view-coffee";
                break;
            case "Services":
                var servicesController = PlatformServicesWorkbench.Create(host, new WindowsPlatformServices(window, dispatcher),
                    picker!, ReportFatal).Controller;
                ownedOperation = () => servicesController.LastOperation;
                inputId = "services-clipboard-draft";
                break;
            default: _ = new SessionPlanner(host); inputId = "planner-start"; break;
        }
        var backend = new WindowsBackend(surface, dispatcher,
            sample is "Presentation" or "Studio" ? WindowsThemeAuthority.ExclusiveWindow : WindowsThemeAuthority.BorrowedSurface);
        host.Attach(backend);
        studio?.AttachView();
        if (virtualList is { } list)
        {
            var lease = host.BeginVirtualViewport(list.Viewport, list.Controller.RequestedCount, list.Controller.RowHeight,
                list.Controller.RequestedSourceVersion, request =>
                {
                    list.OnViewportRequested(request);
                    if (list.Controller.ViewportHeight > 0 && list.Controller.Count > 0 && list.Controller.Mounted.Count > 0 &&
                        list.Controller.SourceVersion == list.Controller.RequestedSourceVersion)
                        virtualReady!.TrySetResult();
                });
            list.AttachViewport(lease, host.SetVirtualItemInfo);
        }
        application.Show(window);
        Exception? failure = null;
        Task? work = null;
        if (capture is not null || smoke)
        {
            work = Task.Run(async () =>
            {
                if (virtualReady is not null) await virtualReady.Task.WaitAsync(TimeSpan.FromSeconds(15));
                if (imageReady is not null)
                {
                    try { await imageReady.Task.WaitAsync(TimeSpan.FromSeconds(20)); }
                    catch { application.Post(window.Close); throw; }
                }
                if (studio is not null)
                {
                    try
                    {
                        if (operations)
                        {
                            var operation = new TaskCompletionSource<Task>(TaskCreationOptions.RunContinuationsAsynchronously);
                            if (!application.Post(() =>
                            {
                                try
                                {
                                    studio.Controller.OpenOperations();
                                    studio.OperationsPage!.Controller.SetScope(StudioOperationsScope.AllDocuments);
                                    studio.OperationsPage.Controller.Scan();
                                    operation.SetResult(studio.LastOperation);
                                }
                                catch (Exception error) { operation.SetException(error); }
                            })) throw new InvalidOperationException("The Studio dispatcher closed before the requested local Operations capture.");
                            await (await operation.Task.WaitAsync(TimeSpan.FromSeconds(15))).WaitAsync(TimeSpan.FromSeconds(20));
                        }
                        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(20);
                        while (true)
                        {
                            var ready = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
                            if (!application.Post(() =>
                            {
                                try { ready.SetResult(StudioReadyForCapture(studio, backend, surface)); }
                                catch (Exception error) { ready.SetException(error); }
                            })) throw new InvalidOperationException("The Studio dispatcher closed before capture readiness.");
                            if (await ready.Task.WaitAsync(TimeSpan.FromSeconds(15))) break;
                            if (DateTime.UtcNow >= deadline) throw new TimeoutException("Studio did not publish its native catalog/layout before capture.");
                            await Task.Delay(10);
                        }
                    }
                    catch
                    {
                        application.Post(window.Close);
                        throw;
                    }
                }
                await Task.Delay(750);
                var complete = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!application.Post(() =>
                {
                    try
                    {
                        var input = backend.FindControls(inputId).Single();
                        input.Focus();
                        nint edit = GetFocus();
                        nint nativeWindow = GetAncestor(edit, 2);
                        if (edit == 0 || nativeWindow == 0 || !input.Focused || surface.GetBounds().Width <= 0)
                            throw new InvalidOperationException("The portable gallery has no rendered native focus target.");
                        if (capture is not null) WindowCapture.Save(nativeWindow, capture);
                        virtualList?.CaptureEditingState();
                        Console.WriteLine($"Windows portable {sample}: native surface ready{(capture is null ? "" : $", captured {capture}")}.");
                        complete.SetResult();
                    }
                    catch (Exception error) { complete.SetException(error); }
                    finally { window.Close(); }
                })) throw new InvalidOperationException("The gallery dispatcher closed before capture.");
                await complete.Task.WaitAsync(TimeSpan.FromSeconds(15));
            });
        }
        try { application.Run(); }
        catch (Exception error) { failure = error; }
        var pendingOperation = ownedOperation?.Invoke();
        try { host.Dispose(); }
        catch (Exception error) { failure = failure is null ? error : new AggregateException(failure, error); }
        try { work?.GetAwaiter().GetResult(); }
        catch (Exception error) { failure = failure is null ? error : new AggregateException(failure, error); }
        try { pendingOperation?.WaitAsync(TimeSpan.FromSeconds(15)).GetAwaiter().GetResult(); }
        catch (Exception error) { failure = failure is null ? error : new AggregateException(failure, error); }
        foreach (var error in reported)
            failure = failure is null ? error : new AggregateException(failure, error);
        if (failure is null) return 0;
        Console.Error.WriteLine(failure);
        return 1;
    }

    private static bool StudioReadyForCapture(WorkspaceStudio studio, WindowsBackend backend, ContentHost surface)
    {
        if (studio.LayoutMode != WidthBreakpoints.Default.Select(surface.GetBounds().Width)) return false;
        if (!studio.CatalogShown) return true;
        if (!studio.Catalog.IsReady || studio.Catalog.SourceVersion <= 0) return false;
        if (studio.Controller.VisibleKeys.Length == 0) return studio.Catalog.MountedCount == 0;
        if (studio.Catalog.MountedCount == 0) return false;
        var viewport = backend.FindControls("studio-catalog").Single().GetBounds();
        foreach (var element in Walk(studio.CatalogView))
        {
            if (element is not Xui.Experimental.Portable.Button button) continue;
            var native = backend.FindControls(button.AutomationId).Single();
            var bounds = native.GetBounds();
            if (bounds.Width <= 0 || bounds.Height <= 0 ||
                bounds.X >= viewport.X + viewport.Width || bounds.Y >= viewport.Y + viewport.Height ||
                bounds.X + bounds.Width <= viewport.X || bounds.Y + bounds.Height <= viewport.Y) continue;
            if (HasVisibleNativeControl(bounds)) return true;
        }
        return false;
    }

    private static IEnumerable<Xui.Experimental.Portable.Element> Walk(Xui.Experimental.Portable.Element root)
    {
        yield return root;
        foreach (var child in root.Children)
            foreach (var descendant in Walk(child)) yield return descendant;
    }

    private static bool HasVisibleNativeControl(ElementBounds expected)
    {
        nint owner = 0;
        EnumThreadWindows(GetCurrentThreadId(), (candidate, _) =>
        {
            GetWindowThreadProcessId(candidate, out uint process);
            var title = new System.Text.StringBuilder(128);
            GetWindowText(candidate, title, title.Capacity);
            if (process == Environment.ProcessId && IsWindowVisible(candidate) && title.ToString() == "XUI portable gallery - Studio")
                owner = candidate;
            return true;
        }, 0);
        if (owner == 0) return false;
        float scale = GetDpiForWindow(owner) / 96f;
        bool found = false;
        EnumChildWindows(owner, (child, _) =>
        {
            if (!IsWindowVisible(child) || !GetWindowRect(child, out var bounds)) return true;
            var origin = new CapturePoint(bounds.Left, bounds.Top);
            ScreenToClient(owner, ref origin);
            if (Math.Abs(origin.X - expected.X * scale) <= 2 &&
                Math.Abs(origin.Y - expected.Y * scale) <= 2 &&
                Math.Abs(bounds.Right - bounds.Left - expected.Width * scale) <= 2 &&
                Math.Abs(bounds.Bottom - bounds.Top - expected.Height * scale) <= 2) found = true;
            return true;
        }, 0);
        return found;
    }

    private sealed class ProfileStorage : IApplicationStorage
    {
        private readonly Lazy<DirectoryApplicationStorage> storage = new(() => new(Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData, Environment.SpecialFolderOption.DoNotVerify),
            "Xui", "PortableGallery")));

        public Task<OperationResult<StoredValue>> ReadAsync(string key, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return storage.Value.ReadAsync(key, cancellationToken);
        }
        public Task<OperationResult<bool>> WriteAsync(string key, ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return storage.Value.WriteAsync(key, data, cancellationToken);
        }
        public Task<OperationResult<bool>> DeleteAsync(string key, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return storage.Value.DeleteAsync(key, cancellationToken);
        }
    }

    [DllImport("user32.dll")] private static extern nint GetFocus();
    [DllImport("user32.dll")] private static extern nint GetAncestor(nint window, uint flags);
    private delegate bool EnumWindowCallback(nint window, nint context);
    [StructLayout(LayoutKind.Sequential)] private struct CaptureRect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] private struct CapturePoint(int x, int y) { public int X = x, Y = y; }
    [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] private static extern bool EnumThreadWindows(uint thread, EnumWindowCallback callback, nint context);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(nint parent, EnumWindowCallback callback, nint context);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(nint window, out uint process);
    [DllImport("user32.dll", EntryPoint = "GetWindowTextW", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(nint window, System.Text.StringBuilder text, int capacity);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(nint window);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(nint window);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(nint window, out CaptureRect rectangle);
    [DllImport("user32.dll")] private static extern bool ScreenToClient(nint window, ref CapturePoint point);
}
