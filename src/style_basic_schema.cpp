#include "xui/control_styling.hpp"

namespace xui {
const StyleTargetSchema* basic_text_style_schema(StyleTarget target) {
    constexpr auto surface = style_properties::surface;
    constexpr auto text = style_properties::text;
    constexpr auto wrap = style_property(StyleProperty::wrapping) | style_property(StyleProperty::maximum_lines);
    constexpr auto icon = style_property(StyleProperty::foreground) | style_property(StyleProperty::size) |
        style_property(StyleProperty::padding);
    constexpr auto interaction = style_states::interaction | style_states::checked;
    constexpr StyleValueLimits text_limits{32768.0f, 1024, 7, 15, 7};
    static constexpr StyleStateMask interactive_precedence[]{style_states::focused, style_states::checked,
        style_states::hovered, style_states::pressed, style_states::disabled};
    static constexpr StyleStateMask label_precedence[]{style_states::disabled};
    static constexpr StylePartSchema button_parts[]{
        {StylePart::root, surface | text, interaction, {}, ~StylePropertyMask(0), {}, text_limits},
        {StylePart::label, text, interaction, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits},
        {StylePart::icon, icon, interaction, StylePart::root},
        {StylePart::arrow, icon, interaction, StylePart::root},
    };
    static constexpr StylePartSchema label_parts[]{
        {StylePart::root, surface | text | wrap, style_states::disabled, {}, ~StylePropertyMask(0), {}, text_limits},
        {StylePart::label, text | wrap, style_states::disabled, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits},
        {StylePart::caption, text | wrap, style_states::disabled, StylePart::label, ~StylePropertyMask(0), StylePart::label, text_limits},
        {StylePart::heading, text | wrap, style_states::disabled, StylePart::label, ~StylePropertyMask(0), StylePart::label, text_limits},
    };
    static constexpr StylePartSchema toggle_parts[]{
        {StylePart::root, surface | text, interaction, {}, ~StylePropertyMask(0), {}, text_limits},
        {StylePart::label, text, interaction, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits},
        {StylePart::indicator, style_property(StyleProperty::background) | style_property(StyleProperty::border_brush) |
            style_property(StyleProperty::border_thickness) | style_property(StyleProperty::corner_radius) |
            style_property(StyleProperty::size), interaction, {}},
        {StylePart::mark, style_property(StyleProperty::foreground), interaction, {}},
    };
    static const StyleTargetSchema button{button_parts, interactive_precedence};
    static const StyleTargetSchema label{label_parts, label_precedence};
    static const StyleTargetSchema toggle{toggle_parts, interactive_precedence};
    switch (target) {
    case StyleTarget::button: return &button;
    case StyleTarget::label: return &label;
    case StyleTarget::toggle: return &toggle;
    default: return nullptr;
    }
}
}
