#include "xui/control_styling.hpp"

namespace xui {
const StyleTargetSchema* data_grid_chart_style_schema(StyleTarget target) {
    constexpr auto ink = style_property(StyleProperty::foreground);
    constexpr auto line = ink | style_property(StyleProperty::thickness);
    constexpr auto root = style_properties::surface | style_property(StyleProperty::row_height) |
        style_property(StyleProperty::header_height);
    constexpr auto scrollbar_width = style_property(StyleProperty::width);
    constexpr StyleValueLimits text_limits{.vertical_alignments = 7};
    constexpr auto body = style_states::selected | style_states::hovered | style_states::focused |
        style_states::checked | style_states::disabled;
    constexpr auto header = style_states::focused | style_states::sorted | style_states::descending |
        style_states::filtered | style_states::checked | style_states::mixed | style_states::filter_pending |
        style_states::dragging | style_states::disabled;
    static constexpr StyleStateMask order[]{style_states::empty, style_states::focused, style_states::selected,
        style_states::sorted, style_states::descending, style_states::filtered, style_states::checked,
        style_states::mixed, style_states::filter_pending, style_states::hovered, style_states::dragging, style_states::disabled};
    static constexpr StylePartSchema grid_parts[]{
        // Owner-state metrics relayout the whole grid; transient rows never supply these dimensions.
        {StylePart::root, root, style_states::focused | style_states::hovered | style_states::disabled, {}, root},
        {StylePart::row, style_properties::surface, body, StylePart::root},
        {StylePart::alternating_row, style_properties::surface, body, StylePart::row},
        {StylePart::cell, style_properties::text, body, {}, ~StylePropertyMask(0), {}, text_limits},
        {StylePart::header, style_properties::surface | style_properties::text, header, StylePart::root,
            ~StylePropertyMask(0), {}, text_limits},
        {StylePart::indicator, style_properties::surface & ~style_property(StyleProperty::padding), body | header, {}},
        {StylePart::grid_line, line, body | header, {}},
        {StylePart::sort_icon, ink, header, StylePart::header},
        {StylePart::filter_icon, ink, header, StylePart::header},
        {StylePart::scrollbar, scrollbar_width, style_states::disabled, {}, scrollbar_width},
        {StylePart::scrollbar_track, style_properties::surface & ~style_property(StyleProperty::padding),
            style_states::disabled | style_states::dragging, {}},
        {StylePart::scrollbar_thumb, style_properties::surface & ~style_property(StyleProperty::padding),
            style_states::disabled | style_states::dragging, {}},
        {StylePart::reorder_marker, line, style_states::disabled | style_states::dragging, {}},
    };
    static constexpr auto chart_states = style_states::disabled | style_states::empty;
    static constexpr StylePartSchema chart_parts[]{
        {StylePart::root, style_properties::surface, chart_states, {}},
        {StylePart::title, style_properties::text, chart_states, StylePart::root, ~StylePropertyMask(0), {}, text_limits},
        {StylePart::caption, style_properties::text, chart_states, StylePart::root, ~StylePropertyMask(0), {}, text_limits},
        {StylePart::grid_line, line, chart_states, {}},
        {StylePart::plot, line, chart_states, {}},
    };
    static const StyleTargetSchema grid{grid_parts, order}, chart{chart_parts, order};
    if (target == StyleTarget::data_grid) return &grid;
    if (target == StyleTarget::history_chart) return &chart;
    return nullptr;
}
}
