#pragma once
#include "xui/runtime_hosts.hpp"
#include <windows.h>

namespace xui {
constexpr UINT runtime_event_message = WM_APP + 74;
class NativeRuntimeHost {
public:
    NativeRuntimeHost(std::shared_ptr<RuntimeHost> model, HWND parent, std::function<void()> failed, std::function<bool()> can_activate);
    ~NativeRuntimeHost();
    void sync(bool visible, UINT dpi);
    void event();
    void suspend();
    void cancel_owner();
    bool active() const;
    bool contains_native(HWND window) const;
    static bool drain_shutdown();
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
