#pragma once

#include "xui/controls.hpp"
#include "host_accessibility.hpp"

namespace xui {

struct FoundationAction {
    enum Kind { value, text, expand, collapse } kind{};
    double number{};
    std::wstring string;
};
struct ControlSnapshot : HostAccessibilityIdentity {
    bool checked{};
    double scroll_offset{}, scroll_extent{}, viewport_height{};
    bool horizontal_scroll{};
    std::vector<TabItem> tabs;
    std::vector<float> tab_edges;
    std::optional<std::uint64_t> selected_tab;
    double split_ratio{};
    float split_left{}, split_top{}, split_width{}, split_height{};
    std::wstring value_text;
    std::vector<bool> choice_enabled;
    double minimum{}, maximum{100}, value{}, small_step{1}, large_step{10};
    bool vertical_choices{}, expanded{}, read_only{}, invalid{}, toggle_action{};
    float choice_left{}, choice_width{};
    bool dialog_surface{};
    bool indeterminate{}, hyperlink{}, selector_bar{}, menu_bar{}, menu_heading{}, menu_selected{};
    float choice_top{}, choice_height{};
    std::wstring access_key;
    bool visible{};
    bool operator==(const ControlSnapshot&) const = default;
};
struct ControlAccessibility {
    std::mutex mutex;
    ControlSnapshot snapshot;
    std::uint64_t next_grid_action{};
    std::map<std::uint64_t, FoundationAction> foundation_actions;
};
IRawElementProviderSimple* create_control_provider(std::shared_ptr<ControlAccessibility> state);
void publish_control(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderSimple* provider, const Control& control, HWND window);
void disconnect_control(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderSimple* provider);
void raise_control_invoked(IRawElementProviderSimple* provider);
}
