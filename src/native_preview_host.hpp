#pragma once
#include "preview_protocol.hpp"
#include <functional>

namespace xui {
class NativePreviewHost {
public:
    NativePreviewHost(std::shared_ptr<ShellPreview> model, HWND parent,
        std::function<bool()> can_activate, std::function<void(bool)> leave,
        std::function<void()> dismiss, std::function<void()> failed);
    ~NativePreviewHost();
    void sync(bool visible, UINT dpi);
    void cancel_owner();
    bool active() const;
    bool contains_native(HWND window) const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
