#include "xui/control_styling.hpp"

namespace xui {
const StyleTargetSchema* layouts_style_schema(StyleTarget target) {
    constexpr auto surface = style_properties::surface & ~style_property(StyleProperty::foreground);
    constexpr auto spacing = style_property(StyleProperty::spacing);
    constexpr auto alignment = style_properties::alignment;
    constexpr auto disabled = style_states::disabled;
    constexpr auto interactive = style_states::interaction;
    constexpr auto dragging = interactive | style_states::dragging;
    constexpr auto expanded = interactive | style_states::expanded;
    constexpr StyleValueLimits text_limits{32768.0f, 1024, 7, 15, 7};
    static constexpr StyleStateMask precedence[]{style_states::focused, style_states::expanded, style_states::compact,
        style_states::open, style_states::scrollable, style_states::hovered, style_states::pressed, style_states::dragging,
        style_states::disabled};
    static constexpr StylePartSchema stack[]{
        {StylePart::root, surface | spacing | alignment, disabled, {}},
        {StylePart::separator, style_property(StyleProperty::background) | style_property(StyleProperty::thickness), disabled, {}}
    };
    static constexpr StylePartSchema grid[]{
        {StylePart::root, surface | spacing | alignment | style_property(StyleProperty::column_gap) |
            style_property(StyleProperty::row_gap), disabled, {}},
        {StylePart::separator, style_property(StyleProperty::background) | style_property(StyleProperty::thickness), disabled, {}}
    };
    static constexpr StylePartSchema adaptive[]{
        {StylePart::root, surface | spacing, disabled | style_states::compact | style_states::expanded, {}},
        {StylePart::separator, style_property(StyleProperty::background) | style_property(StyleProperty::thickness),
            disabled | style_states::compact | style_states::expanded, {}}
    };
    static constexpr StylePartSchema page[]{
        {StylePart::root, surface | alignment, disabled, {}},
        {StylePart::separator, style_property(StyleProperty::background) | style_property(StyleProperty::thickness), disabled, {}}
    };
    static constexpr StylePartSchema content[]{
        {StylePart::root, surface | alignment, disabled, {}}
    };
    static constexpr StylePartSchema scroll[]{
        {StylePart::root, surface, dragging, {}},
        {StylePart::scrollbar_track, (surface & ~style_property(StyleProperty::padding)) |
            style_property(StyleProperty::width), dragging, {}},
        {StylePart::scrollbar_thumb, surface & ~style_property(StyleProperty::padding), dragging | style_states::scrollable, {}}
    };
    static constexpr StylePartSchema split[]{
        {StylePart::root, surface, dragging, {}},
        {StylePart::first_pane, surface, dragging, {}},
        {StylePart::second_pane, surface, dragging, {}},
        {StylePart::divider, (surface & ~style_property(StyleProperty::padding)) |
            style_property(StyleProperty::width), dragging, {}},
        {StylePart::grip, style_property(StyleProperty::background) | style_property(StyleProperty::corner_radius), dragging, {}}
    };
    static constexpr StylePartSchema expander[]{
        {StylePart::root, style_properties::surface, expanded, {}},
        {StylePart::header, surface | style_property(StyleProperty::height), expanded, {}},
        {StylePart::text, style_properties::text, expanded, StylePart::root, ~StylePropertyMask(0), {}, text_limits},
        {StylePart::disclosure, style_property(StyleProperty::foreground) | style_property(StyleProperty::size), expanded, StylePart::root},
        {StylePart::content, surface, expanded, {}}
    };
    static constexpr StylePartSchema popup[]{
        {StylePart::root, surface, disabled | style_states::open, {}}
    };
    static const StyleTargetSchema stack_schema{stack, precedence}, grid_schema{grid, precedence},
        adaptive_schema{adaptive, precedence}, page_schema{page, precedence}, content_schema{content, precedence},
        scroll_schema{scroll, precedence}, split_schema{split, precedence}, expander_schema{expander, precedence},
        popup_schema{popup, precedence};
    switch (target) {
    case StyleTarget::stack: return &stack_schema;
    case StyleTarget::grid: case StyleTarget::wrap: return &grid_schema;
    case StyleTarget::adaptive_layout: return &adaptive_schema;
    case StyleTarget::page_view: return &page_schema;
    case StyleTarget::content_view: return &content_schema;
    case StyleTarget::scroll_view: return &scroll_schema;
    case StyleTarget::split_view: return &split_schema;
    case StyleTarget::expander: return &expander_schema;
    case StyleTarget::popup: return &popup_schema;
    default: return nullptr;
    }
}
}
