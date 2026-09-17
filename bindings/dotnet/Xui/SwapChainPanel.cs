using System.Runtime.InteropServices;

namespace Xui;

public readonly record struct SwapChainPanelMetrics(uint PixelWidth, uint PixelHeight,
    float RasterizationScale, bool Visible);

/// <summary>A native composition host. It does not implement terminal input or text accessibility.</summary>
public sealed class SwapChainPanel : Control
{
    internal SwapChainPanel(Window window, ulong handle) : base(window, handle) { }
    private Action<SwapChainPanelMetrics>? metricsChanged;

    private void OnMetrics(UiEvent value)
    {
        if (value.Kind == EventKind.View) metricsChanged?.Invoke(Metrics);
    }

    public event Action<SwapChainPanelMetrics> MetricsChanged
    {
        add { Window.Guard(); if (metricsChanged is null) Event += OnMetrics; metricsChanged += value; }
        remove { Window.Guard(); metricsChanged -= value; if (metricsChanged is null) Event -= OnMetrics; }
    }

    public SwapChainPanelMetrics Metrics
    {
        get
        {
            Window.Guard();
            var value = new Native.SwapChainMetrics { Size = 20 };
            Window.Check(Native.SwapChainGetMetrics(Handle, ref value));
            return new(value.PixelWidth, value.PixelHeight, value.RasterizationScale, value.Visible != 0);
        }
    }

    /// <summary>Borrowed child HWND. Zero before attachment and after native teardown. Never destroy it.</summary>
    public nint NativeWindow
    {
        get { Window.Guard(); Window.Check(Native.SwapChainGetWindow(Handle, out var value)); return value; }
    }

    /// <summary>Attaches an IDXGISwapChain pointer. The host retains its own COM reference. Zero detaches.</summary>
    public void SetSwapChain(nint swapChain)
    {
        Window.Guard();
        Window.Check(Native.SwapChainSet(Handle, swapChain));
    }

    /// <summary>Attaches a DirectComposition surface handle, not a shared texture. The caller owns the handle.</summary>
    public void SetSurfaceHandle(nint surface)
    {
        Window.Guard();
        Window.Check(Native.SwapChainSetSurface(Handle, surface));
    }

    /// <summary>Enables the tab stop and native Tab/Page keys for an application-owned HWND input adapter.</summary>
    public void SetNativeInput(bool enabled)
    {
        Window.Guard();
        Window.Check(Native.SwapChainNativeInput(Handle, enabled ? 1u : 0u));
    }
}

public sealed partial class Window
{
    public SwapChainPanel SwapChainPanel(string name) => new(this, Create(56, name));
}

internal static partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct SwapChainMetrics
    {
        internal uint Size, PixelWidth, PixelHeight;
        internal float RasterizationScale;
        internal uint Visible;
    }

    [LibraryImport("xui", EntryPoint = "xui_swap_chain_set")]
    internal static partial int SwapChainSet(ulong panel, nint swapChain);
    [LibraryImport("xui", EntryPoint = "xui_swap_chain_set_surface")]
    internal static partial int SwapChainSetSurface(ulong panel, nint surface);
    [LibraryImport("xui", EntryPoint = "xui_swap_chain_get_metrics")]
    internal static partial int SwapChainGetMetrics(ulong panel, ref SwapChainMetrics metrics);
    [LibraryImport("xui", EntryPoint = "xui_swap_chain_get_window")]
    internal static partial int SwapChainGetWindow(ulong panel, out nint window);
    [LibraryImport("xui", EntryPoint = "xui_swap_chain_native_input")]
    internal static partial int SwapChainNativeInput(ulong panel, uint enabled);
}
