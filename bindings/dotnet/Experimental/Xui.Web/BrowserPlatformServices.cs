using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.JSInterop;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Web;

/// <summary>Browser services backed by xui-services.js. The caller owns the imported module.</summary>
public sealed class BrowserPlatformServices(IJSInProcessObjectReference module) : IPlatformServices
{
    private readonly IJSInProcessObjectReference module = module ?? throw new ArgumentNullException(nameof(module));

    public CapabilityAvailability GetAvailability(ServiceCapability capability)
    {
        PlatformServicePolicy.ValidateCapability(capability);
        if (capability is ServiceCapability.OpenFile or ServiceCapability.SaveFile)
            return CapabilityAvailability.Unsupported;
        return module.Invoke<string>("getAvailability", capability.ToString()) switch
        {
            "RequiresUserGesture" => CapabilityAvailability.RequiresUserGesture,
            "Unsupported" => CapabilityAvailability.Unsupported,
            "Available" when capability == ServiceCapability.ApplicationStorage => CapabilityAvailability.Available,
            var value => throw new InvalidOperationException($"Invalid browser capability response: {value}.")
        };
    }

    public Task<OperationResult<string>> ReadClipboardAsync(CancellationToken cancellationToken = default) =>
        Invoke<string>("readClipboard", cancellationToken);

    public Task<OperationResult<bool>> WriteClipboardAsync(string text, CancellationToken cancellationToken = default)
    {
        PlatformServicePolicy.ValidateClipboardText(text);
        return Invoke<bool>("writeClipboard", cancellationToken, text);
    }

    public Task<OperationResult<bool>> OpenUriAsync(Uri uri, CancellationToken cancellationToken = default)
    {
        PlatformServicePolicy.ValidateLaunchUri(uri);
        return Invoke<bool>("openUri", cancellationToken, uri.AbsoluteUri);
    }

    private async Task<OperationResult<T>> Invoke<T>(string method, CancellationToken cancellationToken, params object?[] args)
    {
        cancellationToken.ThrowIfCancellationRequested();
        try
        {
            var result = await module.InvokeAsync<ServiceResponse<T>>(method, cancellationToken, args);
            if (result is null) return OperationResult<T>.Failed(new InvalidOperationException("Missing browser service response."));
            return result.Status switch
            {
                "Completed" when result.HasValue && result.Value is not null => OperationResult<T>.Completed(result.Value),
                "Cancelled" => OperationResult<T>.Cancelled(),
                "Denied" => OperationResult<T>.Denied(),
                "Unsupported" => OperationResult<T>.Unsupported(),
                "Failed" when !string.IsNullOrWhiteSpace(result.Error) =>
                    OperationResult<T>.Failed(new InvalidOperationException(result.Error)),
                _ => OperationResult<T>.Failed(new InvalidOperationException("Invalid browser service response."))
            };
        }
        catch (JSException error) { return OperationResult<T>.Failed(error); }
        catch (JsonException error) { return OperationResult<T>.Failed(error); }
    }

    private sealed class ServiceResponse<T>
    {
        public ServiceResponse() { }
        [JsonPropertyName("status")] public string? Status { get; set; }
        [JsonPropertyName("value")] public T? Value { get; set; }
        [JsonPropertyName("hasValue")] public bool HasValue { get; set; }
        [JsonPropertyName("error")] public string? Error { get; set; }
    }
}
