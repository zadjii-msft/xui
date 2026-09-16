#pragma once

#include "xui/core.hpp"
#include <windows.h>
#include <ole2.h>
#include <UIAutomationCore.h>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace xui {

// Providers read immutable data. Actions return to the window's owning thread.
inline constexpr UINT accessibility_action_message = WM_APP + 41;
enum class AccessibilityAction : WPARAM {
    select, focus_item, reveal, scroll_percent, focus_list, add_selection, remove_selection
};

struct AccessibleSnapshot {
    std::uint64_t control_id{};
    std::shared_ptr<const FilteredView> view;
    std::optional<ItemId> selected;
    std::optional<ItemId> focused_item;
    RECT screen_bounds{};
    float row_height_pixels{32};
    float offset_pixels{};
    float row_right_inset_pixels{};
    bool focused{};
    bool enabled{true};
    std::shared_ptr<const std::wstring> name;
    std::shared_ptr<const std::wstring> automation_id;
    std::shared_ptr<const std::wstring> help_text;
    float row_left_inset_pixels{}, row_top_inset_pixels{}, row_bottom_inset_pixels{};
};

struct AccessibilityState {
    std::mutex mutex;
    HWND window{};
    AccessibleSnapshot snapshot;
};

// LPARAM is the stable ItemId for item actions, or percent * 100 for scroll.
// The 64-bit window resolves IDs in the current filtered model before each action.
IRawElementProviderSimple* create_list_provider(std::shared_ptr<AccessibilityState> state);
void raise_list_selection(IRawElementProviderSimple* provider,
                          std::shared_ptr<AccessibilityState> state, size_t index);
void raise_list_structure(IRawElementProviderSimple* provider);
void raise_list_selection_removed(IRawElementProviderSimple* provider,
                                 std::shared_ptr<AccessibilityState> state, size_t index);
void raise_list_focus(IRawElementProviderSimple* provider,
                      std::shared_ptr<AccessibilityState> state, std::optional<size_t> index);
void raise_list_properties(IRawElementProviderSimple* provider,
    const std::shared_ptr<AccessibilityState>& state, const AccessibleSnapshot& previous);

}
