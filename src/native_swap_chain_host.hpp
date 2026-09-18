#pragma once

#include "xui/swap_chain_panel.hpp"

namespace xui {
class NativeSwapChainHost final {
public:
    NativeSwapChainHost(std::shared_ptr<SwapChainPanel> model, HWND window,
        std::function<bool()> can_activate);
    ~NativeSwapChainHost();
    void sync(bool visible, UINT dpi, Rect clip);
    void suspend();
    void cancel_owner() noexcept;
    bool active() const;
private:
    std::shared_ptr<SwapChainPanel> model_;
};
}
