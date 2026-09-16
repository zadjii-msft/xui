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
constexpr UINT foundation_action_message = WM_APP + 33;
struct FoundationAction {
    enum Kind { value, text, expand, collapse } kind{};
    double number{};
    std::wstring string;
};
struct GridAction {
    enum Kind { select, invoke, focus, header_focus, sort, scroll, reveal, clear,
        add, remove, check, expand, collapse, inline_action, filter, filter_open, select_all } kind{};
    std::optional<RowKey> key;
    std::size_t column{};
    double x{}, y{};
    std::wstring text;
};
struct ControlSnapshot {
    HWND window{};
    std::uint64_t id{};
    ControlRole role{};
    std::wstring name;
    std::wstring automation_id;
    bool enabled{}, focused{}, checked{};
    double scroll_offset{}, scroll_extent{}, viewport_height{};
    bool horizontal_scroll{};
    std::vector<TabItem> tabs;
    std::vector<float> tab_edges;
    std::optional<std::uint64_t> selected_tab;
    double split_ratio{};
    float split_left{}, split_top{}, split_width{}, split_height{};
    std::shared_ptr<const GridSource> grid;
    std::vector<GridColumn> columns;
    std::vector<std::size_t> column_order;
    std::optional<RowKey> selected_row;
    double grid_x{}, grid_y{}, grid_width{}, grid_height{};
    GridGeometry grid_geometry;
    std::size_t sort_column{}, header_column{};
    bool descending{}, header_focus{};
    std::wstring help_text, value_text;
    std::vector<bool> choice_enabled;
    double minimum{}, maximum{100}, value{}, small_step{1}, large_step{10};
    bool vertical_choices{}, expanded{}, read_only{}, invalid{}, toggle_action{};
    float choice_left{}, choice_width{};
    bool dialog_surface{};
    bool single_selection{};
    bool visible{};
    std::shared_ptr<const ItemsSource> collection;
    CollectionSelection selection;
    std::size_t collection_columns{1};
    double collection_item_height{56}, collection_offset{}, collection_width{}, collection_height{};
    double collection_viewport_x{}, collection_viewport_y{};
    std::vector<std::wstring> grid_filters;
    GridHeaderPart header_part{};
    std::shared_ptr<const CollectionIndex> full_source;
    bool operator==(const ControlSnapshot&) const = default;
};
struct ControlAccessibility {
    std::mutex mutex;
    ControlSnapshot snapshot;
    std::uint64_t next_grid_action{};
    std::map<std::uint64_t, GridAction> grid_actions;
    std::map<std::uint64_t, FoundationAction> foundation_actions;
};
IRawElementProviderSimple* create_control_provider(std::shared_ptr<ControlAccessibility> state);
IRawElementProviderSimple* create_native_clip_provider(std::shared_ptr<ControlAccessibility> state);
void publish_control(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderSimple* provider, const Control& control, HWND window);
void disconnect_control(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderSimple* provider);
void raise_control_invoked(IRawElementProviderSimple* provider);
IRawElementProviderSimple* create_collection_provider(std::shared_ptr<ControlAccessibility> state);
void raise_collection_changes(IRawElementProviderSimple* provider, const ControlSnapshot& before, const ControlSnapshot& after);

}
