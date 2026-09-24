using Microsoft.AspNetCore.Components;
using Microsoft.AspNetCore.Components.WebAssembly.Hosting;
using Microsoft.JSInterop;
using PortableDemo;
using Xui.Experimental.Portable;
using Xui.Experimental.Web;

var builder = WebAssemblyHostBuilder.CreateDefault(args);
var app = builder.Build();
var js = app.Services.GetRequiredService<IJSRuntime>();
var module = await js.InvokeAsync<IJSInProcessObjectReference>("import", "./_content/Xui.Web/xui-dom.js");
var reporter = new BrowserErrorReporter(module, "errors");
var host = new Host(new BrowserDispatcher(reporter.Report));
IJSInProcessObjectReference? storageModule = null;
IndexedDbApplicationStorage? storage = null;
VirtualList? virtualList = null;
WorkspaceStudio? studio = null;
BrowserStudioHost? studioHost = null;
bool disposed = false;
void DisposeApplication()
{
    if (disposed) return;
    disposed = true;
    List<Exception> errors = [];
    try { if (virtualList is not null && host.IsAttached) virtualList.CaptureEditingState(); }
    catch (Exception error) { errors.Add(error); }
    foreach (var resource in new IDisposable?[] { studioHost, host, storage, reporter, storageModule, module })
    {
        try { resource?.Dispose(); }
        catch (Exception error) { errors.Add(error); }
    }
    if (errors.Count != 0) throw new AggregateException("Gallery cleanup failed.", errors);
}
try
{
    var uri = new Uri(app.Services.GetRequiredService<NavigationManager>().Uri);
    var query = System.Web.HttpUtility.ParseQueryString(uri.Query);
    var selection = query["app"] ?? "task-board";
    switch (selection)
    {
        case "task-board": _ = new TaskBoard(host); break;
        case "expense-ledger": _ = new ExpenseLedger(host); break;
        case "session-planner": _ = new SessionPlanner(host); break;
        case "dynamic-tasks": _ = DynamicTaskBoard.Create(host); break;
        case "settings": _ = new SettingsShowcase(host); break;
        case "forms": _ = new FormsWorkbench(host); break;
        case "presentation": _ = PresentationWorkbench.Create(host); break;
        case "label-layout": _ = new LabelLayoutWorkbench(host); break;
        case "reveal": _ = new RevealWorkbench(host); break;
        case "image":
        case "image-hidden":
            var imageWorkbench = new ImageWorkbench(host, new PackagedAssetManifest(typeof(ImageWorkbench).Assembly));
            if (selection == "image-hidden")
            {
                imageWorkbench.Preview.Visible = false;
                imageWorkbench.Preview.StateChanged += state =>
                {
                    if (state == ImageLoadState.Ready) imageWorkbench.Preview.Visible = true;
                };
            }
            break;
        case "image-pixels":
            using (var build = host.BeginBuild())
            {
                var root = host.Stack(Axis.Vertical);
                var image = host.Image("Exact fixture pixels");
                image.AutomationId = "exact-image";
                image.SetImage(new(new PackagedAssetManifest(typeof(ImageWorkbench).Assembly), "pixels.png"),
                    query["small"] == "1" ? new(2, 2) : new(3, 2));
                root.Add(image);
                host.SetContent(root);
                build.Complete();
            }
            break;
        case "pages":
            var pages = new PortableNavigation.RetainedPagesWorkbench(host);
            pages.Open();
            break;
        case "studio": studio = WorkspaceStudio.Create(host, new LocalStudioAnalysisService(), reporter.Report,
            enableOperationsDrawer: query["motion"] == "1"); break;
        case "axes": _ = new PortableLayout.AxisSizingShowcase(host); break;
        case "grid": _ = new PortableLayout.GridSizingShowcase(host); break;
        case "virtual-list": virtualList = VirtualList.CreateForViewport(host); break;
        case "profile-workspace":
            var databaseName = query["profile-store"] ?? "profile-workspace";
            StorageKeys.Validate(databaseName);
            if (!databaseName.StartsWith("profile-", StringComparison.Ordinal))
                throw new ArgumentException("A profile database name must start with profile-.");
            storageModule = await js.InvokeAsync<IJSInProcessObjectReference>("import", "./_content/Xui.Web/xui-storage.js");
            storage = new IndexedDbApplicationStorage(storageModule, databaseName);
            _ = ProfileWorkspace.Create(host, storage, reporter.Report);
            break;
        default: throw new ArgumentException($"Unknown gallery application: {selection}.");
    }
    var backend = new DomBackend(module, "app", "errors");
    host.Attach(backend);
#if DEBUG
    if (selection.StartsWith("image", StringComparison.Ordinal))
    {
        BrowserImageProbe.Host = host;
        BrowserImageProbe.Backend = backend;
    }
#endif
    studio?.AttachView();
    if (studio is not null) studioHost = await BrowserStudioHost.Create(js, studio, reporter, query["studio-test"] == "1");
    if (virtualList is not null)
    {
        var lease = host.BeginVirtualViewport(virtualList.Viewport, virtualList.Controller.RequestedCount,
            virtualList.Controller.RowHeight, virtualList.Controller.RequestedSourceVersion, virtualList.OnViewportRequested);
        virtualList.AttachViewport(lease, host.SetVirtualItemInfo);
    }
    using var lifetime = new BrowserLifetime(module, "errors", DisposeApplication);
    await app.RunAsync();
}
catch (Exception error)
{
    reporter.Report(error);
    throw;
}
finally
{
    try { DisposeApplication(); }
    catch (Exception error) { reporter.Report(error); throw; }
}
