using Microsoft.AspNetCore.Components.WebAssembly.Hosting;
using Microsoft.JSInterop;
using PortableDemo;
using Xui.Experimental.Portable;
using Xui.Experimental.Web;

var builder = WebAssemblyHostBuilder.CreateDefault(args);
var app = builder.Build();
var js = app.Services.GetRequiredService<IJSRuntime>();
using var module = await js.InvokeAsync<IJSInProcessObjectReference>("import", "./_content/Xui.Web/xui-dom.js");
void Report(Exception error) => module.InvokeVoid("reportError", "errors", error.ToString());
using var host = new Host(new BrowserDispatcher(Report));
try
{
#if DEBUG
    var uri = new Uri(app.Services.GetRequiredService<Microsoft.AspNetCore.Components.NavigationManager>().Uri);
    Greeting? demo = null;
    if (System.Web.HttpUtility.ParseQueryString(uri.Query).AllKeys.Contains("mutation"))
        _ = new PortableMutation.MutationBoard(host);
    else demo = new Greeting(host);
#else
    _ = new Greeting(host);
#endif
    host.Attach(new DomBackend(module, "app", "errors"));
#if DEBUG
    using var tests = demo is null ? null : await BrowserTestDriver.Install(js, module, host, demo);
#endif
    using var lifetime = new BrowserLifetime(module, "errors", () =>
    {
        try { host.Dispose(); }
        finally
        {
#if DEBUG
            try { tests?.Dispose(); }
            finally { module.Dispose(); }
#else
            module.Dispose();
#endif
        }
    });
    await app.RunAsync();
}
catch (Exception error)
{
    Report(error);
    throw;
}
