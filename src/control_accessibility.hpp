#pragma once

#include "xui/controls.hpp"
#include "xui/data_grid.hpp"
#include <windows.h>
#include <ole2.h>
#include <UIAutomationCore.h>
#include <mutex>
#include <map>

namespace xui {

constexpr UINT control_action_message = WM_APP + 31;
constexpr UINT grid_action_message = WM_APP + 32;
struct GridAction {
    enum Kind { select, invoke, focus, header_focus, sort, scroll, reveal, clear } kind{};
    std::optional<RowKey> key;
    std::size_t column{};
    double x{}, y{};
};
struct ControlSnapshot {
    HWND window{};
    std::uint64_t id{};
    ControlRole role{};
    std::wstring name;
    std::wstring automation_id;
    bool enabled{}, focused{}, checked{};
    double scroll_offset{}, scroll_extent{}, viewport_height{};
    std::vector<TabItem> tabs;
    std::vector<float> tab_edges;
    std::optional<std::uint64_t> selected_tab;
    double split_ratio{};
    float split_left{}, split_width{};
    std::shared_ptr<const GridSource> grid;
    std::vector<GridColumn> columns;
    std::optional<RowKey> selected_row;
    double grid_x{}, grid_y{}, grid_width{}, grid_height{};
    std::size_t sort_column{}, header_column{};
    bool descending{}, header_focus{};
    bool operator==(const ControlSnapshot&) const = default;
};
struct ControlAccessibility {
    std::mutex mutex;
    ControlSnapshot snapshot;
    std::uint64_t next_grid_action{};
    std::map<std::uint64_t, GridAction> grid_actions;
};
IRawElementProviderSimple* create_control_provider(std::shared_ptr<ControlAccessibility> state);
IRawElementProviderSimple* create_native_clip_provider(std::shared_ptr<ControlAccessibility> state);
void publish_control(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderSimple* provider, const Control& control, HWND window);
void disconnect_control(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderSimple* provider);
void raise_control_invoked(IRawElementProviderSimple* provider);

}
