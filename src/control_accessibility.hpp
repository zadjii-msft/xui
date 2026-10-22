#pragma once

#include "xui/controls.hpp"
#include "host_accessibility.hpp"
#include <variant>

namespace xui {

struct FoundationAction {
    enum Kind { value, text, expand, collapse } kind{};
    double number{};
    std::wstring string;
};
struct ScrollAccessibilitySnapshot {
    double scroll_offset{}, scroll_extent{}, viewport_height{};
    bool horizontal_scroll{};
    bool operator==(const ScrollAccessibilitySnapshot&) const = default;
};
struct SelectionAccessibilitySnapshot {
    std::vector<TabItem> tabs;
    std::vector<float> tab_edges;
    std::optional<std::uint64_t> selected_tab;
    std::vector<bool> choice_enabled;
    bool vertical_choices{}, selector_bar{};
    float choice_left{}, choice_width{}, choice_top{}, choice_height{};
    bool operator==(const SelectionAccessibilitySnapshot&) const = default;
};
struct ValueAccessibilitySnapshot {
    std::wstring value_text;
    double minimum{}, maximum{100}, value{}, small_step{1}, large_step{10};
    bool read_only{};
    bool operator==(const ValueAccessibilitySnapshot&) const = default;
};
struct SplitAccessibilitySnapshot {
    double split_ratio{};
    float split_left{}, split_top{}, split_width{}, split_height{};
    ValueAccessibilitySnapshot range;
    bool operator==(const SplitAccessibilitySnapshot&) const = default;
};
struct ControlSnapshot : HostAccessibilityIdentity {
    bool checked{}, expanded{}, invalid{}, toggle_action{}, dialog_surface{};
    bool indeterminate{}, hyperlink{}, menu_bar{}, menu_heading{}, menu_selected{}, visible{};
    std::wstring access_key;
    std::variant<std::monostate, ScrollAccessibilitySnapshot, SelectionAccessibilitySnapshot,
        ValueAccessibilitySnapshot, SplitAccessibilitySnapshot> payload;

    template<class Value> const Value& read_payload() const {
        const auto* value = std::get_if<Value>(&payload);
        // Shared UIA properties retain empty defaults without creating another family's state.
        static const Value empty;
        return value ? *value : empty;
    }
    template<class Value> Value& write_payload() {
        if (auto* value = std::get_if<Value>(&payload)) return *value;
        if (!std::holds_alternative<std::monostate>(payload))
            throw std::logic_error("Accessibility snapshot has a different family payload");
        return payload.emplace<Value>();
    }
    const ScrollAccessibilitySnapshot& scroll() const { return read_payload<ScrollAccessibilitySnapshot>(); }
    ScrollAccessibilitySnapshot& scroll_data() { return write_payload<ScrollAccessibilitySnapshot>(); }
    const SelectionAccessibilitySnapshot& selection() const { return read_payload<SelectionAccessibilitySnapshot>(); }
    SelectionAccessibilitySnapshot& selection_data() { return write_payload<SelectionAccessibilitySnapshot>(); }
    const SplitAccessibilitySnapshot& split() const { return read_payload<SplitAccessibilitySnapshot>(); }
    SplitAccessibilitySnapshot& split_data() { return write_payload<SplitAccessibilitySnapshot>(); }
    const ValueAccessibilitySnapshot& range() const {
        if (const auto* split = std::get_if<SplitAccessibilitySnapshot>(&payload)) return split->range;
        return read_payload<ValueAccessibilitySnapshot>();
    }
    ValueAccessibilitySnapshot& range_data() {
        if (auto* split = std::get_if<SplitAccessibilitySnapshot>(&payload)) return split->range;
        return write_payload<ValueAccessibilitySnapshot>();
    }
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
