#pragma once

#include "xui/controls.hpp"
#include <windows.h>
#include <dxgi1_2.h>

namespace xui {
class NativeSwapChainHost;

struct SwapChainPanelMetrics {
    std::uint32_t pixel_width{}, pixel_height{};
    float rasterization_scale{1};
    bool visible{};
    bool operator==(const SwapChainPanelMetrics&) const = default;
};

// Windows-only graphics interop. The producer owns rendering and ResizeBuffers.
class SwapChainPanel final : public Control {
public:
    explicit SwapChainPanel(std::wstring name = L"Swap chain panel");
    ~SwapChainPanel();
    // UI thread only. Null detaches either kind of content.
    void set_swap_chain(IDXGISwapChain* swap_chain);
    // A DCompositionCreateSurfaceHandle handle, not a shared texture handle.
    // The caller retains ownership and can close the handle after this call.
    void set_swap_chain_handle(HANDLE surface);
    bool has_content() const;
    const SwapChainPanelMetrics& metrics() const;
    // Panel-local physical pixels, including fractional edges. Empty when not visible.
    Rect visible_pixel_bounds() const;
    void on_metrics_changed(std::function<void(const SwapChainPanelMetrics&)> callback);
    // Opt-in for an application-owned HWND input adapter. Window shortcuts still run first.
    void set_native_input(bool enabled);
    bool native_input() const;
    // Borrowed peer HWND. Null before attachment and after window teardown.
    HWND native_window() const;
private:
    friend class NativeSwapChainHost;
    struct State;
    std::unique_ptr<State> state_;
};
}
