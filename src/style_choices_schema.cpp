#include "xui/control_styling.hpp"

namespace xui {
const StyleTargetSchema* choices_status_style_schema(StyleTarget target) {
    using namespace style_states;
    constexpr auto surface = style_properties::surface;
    constexpr auto text = style_properties::text;
    constexpr auto color = style_property(StyleProperty::foreground);
    constexpr auto size = style_property(StyleProperty::size);
    constexpr auto padding = style_property(StyleProperty::padding);
    constexpr auto shape = surface & ~(color | padding);
    constexpr auto thickness = style_property(StyleProperty::thickness);
    constexpr auto rows = style_property(StyleProperty::row_height) | style_property(StyleProperty::spacing);
    constexpr auto header = text | style_property(StyleProperty::header_height);
    constexpr StyleValueLimits text_limits{32768.0f, 1024, 7, 15, 7};
    constexpr auto choice_states = interaction | selected;
    constexpr auto combo_states = interaction | open | empty;
    constexpr auto number_states = interaction | invalid | minimum | maximum;
    constexpr auto range_states = interaction | dragging | minimum | maximum;
    constexpr auto progress_states = disabled | determinate | indeterminate | paused | error | unknown;
    constexpr auto status_states = disabled | information | success | warning | error | dismissed;
    static constexpr StyleStateMask precedence[]{focused, selected, empty, open, minimum, maximum,
        determinate, indeterminate, unknown, information, success, warning, paused, invalid, error,
        hovered, pressed, dragging, dismissed, disabled};
    static constexpr StylePartSchema radio[]{
        {StylePart::root, surface | rows, interaction, {}},
        {StylePart::item, surface, choice_states, StylePart::root},
        {StylePart::label, text, choice_states, StylePart::item, ~StylePropertyMask(0), {}, text_limits},
        {StylePart::indicator, shape | color | size, choice_states, {}},
        {StylePart::mark, color | size, choice_states, StylePart::indicator}
    };
    static constexpr StylePartSchema choices[]{
        {StylePart::root, surface | rows, interaction, {}},
        {StylePart::item, surface, choice_states, StylePart::root},
        {StylePart::label, text, choice_states, StylePart::item, ~StylePropertyMask(0), {}, text_limits},
        {StylePart::selected_marker, shape | size, choice_states, {}}
    };
    static constexpr StylePartSchema combo[]{
        {StylePart::root, surface, combo_states, {}},
        {StylePart::field, surface, combo_states, StylePart::root},
        {StylePart::text, text, combo_states, StylePart::field, ~StylePropertyMask(0), {}, text_limits},
        {StylePart::header, header, combo_states, StylePart::root, ~StylePropertyMask(0), {}, text_limits},
        {StylePart::arrow, color | size, combo_states, StylePart::field}
    };
    static constexpr StylePartSchema number[]{
        {StylePart::root, surface, number_states, {}},
        {StylePart::field, surface, number_states, StylePart::root},
        {StylePart::header, header, number_states, StylePart::root, ~StylePropertyMask(0), {}, text_limits}
    };
    static constexpr StylePartSchema range[]{
        {StylePart::root, surface, range_states, {}},
        {StylePart::track, shape | thickness, range_states, {}},
        {StylePart::fill, shape | color, range_states, StylePart::root},
        {StylePart::thumb, shape | size, range_states, {}}
    };
    static constexpr StylePartSchema progress[]{
        {StylePart::root, surface, progress_states, {}},
        {StylePart::caption, text, progress_states, StylePart::root, ~StylePropertyMask(0), {}, text_limits},
        {StylePart::track, shape | thickness, progress_states, {}},
        {StylePart::fill, shape, progress_states, {}}
    };
    static constexpr StylePartSchema status[]{
        {StylePart::root, surface, status_states, {}},
        {StylePart::stripe, shape | size, status_states, {}},
        {StylePart::icon, color | size, status_states, StylePart::root},
        {StylePart::message, text, status_states, StylePart::root, ~StylePropertyMask(0), {}, text_limits}
    };
    static constexpr StylePartSchema picker[]{
        {StylePart::root, surface, disabled | invalid, {}},
        {StylePart::checkerboard_light, style_property(StyleProperty::background), disabled | invalid, {}},
        {StylePart::checkerboard_dark, style_property(StyleProperty::background), disabled | invalid, {}},
        {StylePart::preview, style_property(StyleProperty::border_brush) |
            style_property(StyleProperty::border_thickness) | style_property(StyleProperty::corner_radius), disabled | invalid, {}},
        {StylePart::swatch, style_property(StyleProperty::border_brush) |
            style_property(StyleProperty::border_thickness) | style_property(StyleProperty::corner_radius),
            interaction | selected, {}},
        {StylePart::channel_label, text | padding, disabled | invalid, StylePart::root, ~StylePropertyMask(0), {}, text_limits}
    };
    static const StyleTargetSchema schemas[]{
        {radio, precedence}, {choices, precedence}, {combo, precedence}, {number, precedence},
        {range, precedence}, {progress, precedence}, {status, precedence}, {picker, precedence}
    };
    switch (target) {
    case StyleTarget::radio_group: return &schemas[0];
    case StyleTarget::choice_list: return &schemas[1];
    case StyleTarget::combo_box: return &schemas[2];
    case StyleTarget::numeric_input: return &schemas[3];
    case StyleTarget::range_input: return &schemas[4];
    case StyleTarget::progress: return &schemas[5];
    case StyleTarget::inline_status: return &schemas[6];
    case StyleTarget::color_picker: return &schemas[7];
    default: return nullptr;
    }
}
}
