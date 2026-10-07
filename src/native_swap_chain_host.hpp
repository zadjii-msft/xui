#pragma once

#include "xui/swap_chain_panel.hpp"
#include <memory>

namespace xui {
namespace platform { class CompositionPresenter; }
class NativeSwapChainHost final {
public:
    // With a presenter, the producer joins the window's composition tree; otherwise the peer HWND
    // gets its own DirectComposition target.
    NativeSwapChainHost(std::shared_ptr<SwapChainPanel> model, HWND window,
        std::function<bool()> can_activate, std::weak_ptr<platform::CompositionPresenter> presenter = {},
        HWND root = nullptr);
    ~NativeSwapChainHost();
    // In composition windows, popup_layer places a panel owned by a popup above the popup layer.
    void sync(bool visible, UINT dpi, Rect clip, bool popup_layer = false);
    void suspend();
    void cancel_owner() noexcept;
    bool active() const;
private:
    std::shared_ptr<SwapChainPanel> model_;
};
}
