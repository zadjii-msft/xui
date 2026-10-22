#pragma once

#include "xui/controls.hpp"
#include "xui/collections.hpp"
#include <windows.h>
#include <ole2.h>
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>
#include <map>
#include <mutex>

namespace xui {

RECT clipped_bounds(HWND window);
constexpr UINT control_action_message = WM_APP + 31;
constexpr UINT grid_action_message = WM_APP + 32;
constexpr UINT foundation_action_message = WM_APP + 33;

struct GridAction {
    enum Kind { select, invoke, focus, header_focus, sort, scroll, reveal, clear,
        add, remove, check, expand, collapse, inline_action, filter, filter_open, select_all } kind{};
    std::optional<ItemKey> key;
    std::size_t column{};
    double x{}, y{};
    std::wstring text;
};

struct HostAccessibilityIdentity {
    HWND window{};
    std::uint64_t id{};
    ControlRole role{};
    std::wstring name, automation_id, help_text;
    bool enabled{}, focused{};
    bool operator==(const HostAccessibilityIdentity&) const = default;
};

inline HostAccessibilityIdentity accessibility_identity(const Control& control, HWND window) {
    return {window, control.id(), control.role(), control.name(), control.automation_id(),
        control.help_text(), control.enabled() && IsWindowEnabled(window), control.focused()};
}

template<class Snapshot>
struct RowAccessibilityState {
    std::mutex mutex;
    Snapshot snapshot;
    std::uint64_t next_grid_action{};
    std::map<std::uint64_t, GridAction> grid_actions;
};

template<class State>
auto read_host_snapshot(const std::shared_ptr<State>& state) {
    std::lock_guard lock(state->mutex);
    return state->snapshot;
}

template<class State, class Snapshot>
bool replace_host_snapshot(const std::shared_ptr<State>& state, Snapshot next, Snapshot& previous) {
    std::lock_guard lock(state->mutex);
    if (state->snapshot == next) return false;
    previous = std::move(state->snapshot);
    state->snapshot = std::move(next);
    return true;
}

template<class Snapshot>
bool take_row_action(const std::shared_ptr<RowAccessibilityState<Snapshot>>& state,
    std::uint64_t id, HWND window, std::uint64_t token, GridAction& action) {
    std::lock_guard lock(state->mutex);
    if (!window || state->snapshot.window != window || state->snapshot.id != id) return false;
    const auto it = state->grid_actions.find(token);
    if (it == state->grid_actions.end()) return false;
    action = std::move(it->second);
    state->grid_actions.erase(it);
    return true;
}

template<class Snapshot>
void disconnect_row_accessibility(const std::shared_ptr<RowAccessibilityState<Snapshot>>& state,
    IRawElementProviderSimple* provider) {
    Snapshot retired;
    std::map<std::uint64_t, GridAction> actions;
    {
        std::lock_guard lock(state->mutex);
        retired = std::move(state->snapshot);
        state->snapshot = {};
        actions.swap(state->grid_actions);
    }
    if (provider) UiaDisconnectProvider(provider);
}

}
