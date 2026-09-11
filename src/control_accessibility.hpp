#pragma once

#include "xui/controls.hpp"
#include <windows.h>
#include <ole2.h>
#include <UIAutomationCore.h>
#include <mutex>

namespace xui {

constexpr UINT control_action_message = WM_APP + 31;
struct ControlSnapshot {
    HWND window{};
    std::uint64_t id{};
    ControlRole role{};
    std::wstring name;
    std::wstring automation_id;
    bool enabled{}, focused{}, checked{};
};
struct ControlAccessibility {
    std::mutex mutex;
    ControlSnapshot snapshot;
};
IRawElementProviderSimple* create_control_provider(std::shared_ptr<ControlAccessibility> state);
void publish_control(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderSimple* provider, const Control& control, HWND window);
void disconnect_control(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderSimple* provider);
void raise_control_invoked(IRawElementProviderSimple* provider);

}
