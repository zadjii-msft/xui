using Microsoft.AspNetCore.Components.WebAssembly.Hosting;
using Microsoft.JSInterop;
using PortableApp;
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
    _ = new Counter(host);
    host.Attach(new DomBackend(module, "app", "errors"));
    using var lifetime = new BrowserLifetime(module, "errors", () =>
    {
        try { host.Dispose(); }
        finally { module.Dispose(); }
    });
    await app.RunAsync();
}
catch (Exception error)
{
    Report(error);
    throw;
}
