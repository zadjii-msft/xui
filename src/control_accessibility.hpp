#pragma once

#include "xui/controls.hpp"
#include "xui/data_grid.hpp"
#include "collection_presentation.hpp"
#include <windows.h>
#include <ole2.h>
#include <UIAutomationCore.h>
#include <mutex>
#include <map>

namespace xui {

RECT clipped_bounds(HWND window);
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
    bool indeterminate{}, hyperlink{}, selector_bar{}, menu_bar{}, menu_heading{}, menu_selected{};
    float choice_top{}, choice_height{};
    std::wstring access_key;
    bool single_selection{};
    bool visible{};
    bool logical_hidden{};
    std::shared_ptr<const ItemsSource> collection;
    std::shared_ptr<const detail::CollectionPresentation> collection_presentation;
    CollectionSelection selection;
    std::size_t collection_columns{1};
    std::vector<GridColumn> collection_details;
    double collection_item_height{56}, collection_offset{}, collection_width{}, collection_height{};
    double collection_viewport_x{}, collection_viewport_y{};
    std::vector<std::wstring> grid_filters;
    GridHeaderPart header_part{};
    std::shared_ptr<const CollectionIndex> full_source;
    bool operator==(const ControlSnapshot&) const = default;
};
class FragmentNavigation {
public:
    virtual ~FragmentNavigation() = default;
    virtual HRESULT navigate(std::uint64_t control, HWND window, NavigateDirection direction, IRawElementProviderFragment** result) = 0;
    virtual HRESULT fragment_root(IRawElementProviderFragmentRoot** result) = 0;
};
struct ControlAccessibility {
    std::mutex mutex;
    ControlSnapshot snapshot;
    std::uint64_t next_grid_action{};
    std::map<std::uint64_t, GridAction> grid_actions;
    std::map<std::uint64_t, FoundationAction> foundation_actions;
    std::weak_ptr<FragmentNavigation> fragment_navigation;
};
std::optional<HRESULT> navigate_fragment(const std::shared_ptr<ControlAccessibility>& state,
    NavigateDirection direction, IRawElementProviderFragment** result);
std::optional<HRESULT> fragment_root(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderFragmentRoot** result);
IRawElementProviderSimple* create_control_provider(std::shared_ptr<ControlAccessibility> state);
IRawElementProviderSimple* create_native_clip_provider(std::shared_ptr<ControlAccessibility> state);
void publish_control(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderSimple* provider, const Control& control, HWND window, bool logical_hidden = false);
void disconnect_control(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderSimple* provider);
void raise_control_invoked(IRawElementProviderSimple* provider);
IRawElementProviderSimple* create_collection_provider(std::shared_ptr<ControlAccessibility> state);
void raise_collection_changes(IRawElementProviderSimple* provider, const ControlSnapshot& before, const ControlSnapshot& after);

}
