using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Windows;

/// <summary>Explicit clipboard and URI services scheduled on the owning Windows UI dispatcher.</summary>
public sealed class WindowsPlatformServices : IPlatformServices
{
    private readonly ICancellableUiDispatcher dispatcher;
    private readonly IWindowsServiceBridge bridge;

    public WindowsPlatformServices(Window window, WindowsDispatcher dispatcher)
    {
        ArgumentNullException.ThrowIfNull(window);
        ArgumentNullException.ThrowIfNull(dispatcher);
        window.VerifyAccess();
        if (!ReferenceEquals(window, dispatcher.Window))
            throw new ArgumentException("Services and dispatcher must belong to the same window.", nameof(dispatcher));
        this.dispatcher = dispatcher;
        bridge = new NativeWindowsServices(window);
    }

    internal WindowsPlatformServices(ICancellableUiDispatcher dispatcher, IWindowsServiceBridge bridge)
    {
        this.dispatcher = dispatcher;
        this.bridge = bridge;
    }

    public CapabilityAvailability GetAvailability(ServiceCapability capability)
    {
        PlatformServicePolicy.ValidateCapability(capability);
        return capability is ServiceCapability.Clipboard or ServiceCapability.OpenUri
            ? CapabilityAvailability.Available : CapabilityAvailability.Unsupported;
    }

    public Task<OperationResult<string>> ReadClipboardAsync(CancellationToken cancellationToken = default) =>
        InvokeAsync(bridge.ReadClipboard, cancellationToken);

    public Task<OperationResult<bool>> WriteClipboardAsync(string text, CancellationToken cancellationToken = default)
    {
        PlatformServicePolicy.ValidateClipboardText(text);
        return InvokeAsync(() => { bridge.WriteClipboard(text); return true; }, cancellationToken);
    }

    public Task<OperationResult<bool>> OpenUriAsync(Uri uri, CancellationToken cancellationToken = default)
    {
        PlatformServicePolicy.ValidateLaunchUri(uri);
        return InvokeAsync(() => { bridge.OpenUri(uri); return true; }, cancellationToken);
    }

    private async Task<OperationResult<T>> InvokeAsync<T>(Func<T> action, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var completion = new TaskCompletionSource<OperationResult<T>>(TaskCreationOptions.RunContinuationsAsynchronously);
        using var registration = cancellationToken.Register(() => completion.TrySetCanceled(cancellationToken));
        void Run()
        {
            if (completion.Task.IsCompleted) return;
            try
            {
                if (!dispatcher.CheckAccess()) throw new InvalidOperationException("Windows services require UI-thread dispatch.");
                completion.TrySetResult(OperationResult<T>.Completed(action()));
            }
            catch (Win32Exception error) when (error.NativeErrorCode == 1223)
            { completion.TrySetResult(OperationResult<T>.Cancelled()); }
            catch (Exception error) when (error is UnauthorizedAccessException or Win32Exception { NativeErrorCode: 5 })
            { completion.TrySetResult(OperationResult<T>.Denied()); }
            catch (Exception error) when (error is NotSupportedException or Win32Exception { NativeErrorCode: 1155 })
            { completion.TrySetResult(OperationResult<T>.Unsupported()); }
            catch (Exception error) when (error is Win32Exception or XuiException or IOException or InvalidOperationException)
            { completion.TrySetResult(OperationResult<T>.Failed(error)); }
            catch (Exception error) { completion.TrySetException(error); }
        }
        if (!completion.Task.IsCompleted)
        {
            try
            {
                dispatcher.Post(Run, error => completion.TrySetResult(OperationResult<T>.Failed(error)));
            }
            catch (Exception error) { completion.TrySetResult(OperationResult<T>.Failed(error)); }
        }
        return await completion.Task.ConfigureAwait(false);
    }
}

internal interface IWindowsServiceBridge
{
    string ReadClipboard();
    void WriteClipboard(string text);
    void OpenUri(Uri uri);
}

internal sealed class NativeWindowsServices(Window window) : IWindowsServiceBridge
{
    public void WriteClipboard(string text) => window.SetClipboardText(text);

    public string ReadClipboard()
    {
        window.VerifyAccess();
        if (!OpenClipboard(0)) throw new Win32Exception(Marshal.GetLastPInvokeError());
        try
        {
            if (!IsClipboardFormatAvailable(13))
                throw new NotSupportedException("The clipboard does not contain Unicode text.");
            nint memory = GetClipboardData(13);
            if (memory == 0) throw new Win32Exception(Marshal.GetLastPInvokeError());
            nuint bytes = GlobalSize(memory);
            if (bytes is < 2 or > 16 * 1024 * 1024 || bytes % 2 != 0)
                throw new InvalidDataException("Clipboard text has an invalid or oversized allocation.");
            nint data = GlobalLock(memory);
            if (data == 0) throw new Win32Exception(Marshal.GetLastPInvokeError());
            try
            {
                var text = new char[checked((int)(bytes / 2))];
                Marshal.Copy(data, text, 0, text.Length);
                int terminator = Array.IndexOf(text, '\0');
                if (terminator < 0) throw new InvalidDataException("Clipboard text is not NUL-terminated.");
                return new string(text, 0, terminator);
            }
            finally
            {
                if (!GlobalUnlock(memory) && Marshal.GetLastPInvokeError() is int error and not 0)
                    throw new Win32Exception(error);
            }
        }
        finally
        {
            if (!CloseClipboard()) throw new Win32Exception(Marshal.GetLastPInvokeError());
        }
    }

    public void OpenUri(Uri uri)
    {
        window.VerifyAccess();
        // ShellExecute accepts the already-validated URI as a target, not a shell command line.
        using var process = Process.Start(new ProcessStartInfo(uri.AbsoluteUri) { UseShellExecute = true });
    }

    [DllImport("user32.dll", SetLastError = true)] private static extern bool OpenClipboard(nint owner);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool CloseClipboard();
    [DllImport("user32.dll")] private static extern bool IsClipboardFormatAvailable(uint format);
    [DllImport("user32.dll", SetLastError = true)] private static extern nint GetClipboardData(uint format);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern nuint GlobalSize(nint memory);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern nint GlobalLock(nint memory);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool GlobalUnlock(nint memory);
}
