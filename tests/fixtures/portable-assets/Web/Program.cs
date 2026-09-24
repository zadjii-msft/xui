using Microsoft.AspNetCore.Components.WebAssembly.Hosting;
using Microsoft.JSInterop;

var builder = WebAssemblyHostBuilder.CreateDefault(args);
var app = builder.Build();
var js = app.Services.GetRequiredService<IJSRuntime>();
try
{
    await AssetConsumer.AssetProof.VerifyAsync();
    await js.InvokeVoidAsync("assetProofCompleted");
}
catch (Exception error)
{
    await js.InvokeVoidAsync("assetProofFailed", error.ToString());
    throw;
}
await app.RunAsync();
