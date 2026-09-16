#include "xui/control_styling.hpp"

namespace xui {
const StyleTargetSchema* native_fields_style_schema(StyleTarget target) {
    constexpr StyleValueLimits native_font_limits{512, 31, 3};
    constexpr auto all_properties = ~StylePropertyMask(0);
    constexpr auto ink = style_property(StyleProperty::foreground);
    constexpr auto text = ink | style_properties::typography;
    constexpr auto focus = style_states::focused | style_states::disabled;
    constexpr auto field = focus | style_states::empty;
    constexpr auto document = field | style_states::read_only;
    constexpr auto password = field | style_states::revealed;
    static constexpr StyleStateMask order[]{style_states::empty, style_states::read_only,
        style_states::revealed, style_states::hovered, style_states::pressed, style_states::focused, style_states::disabled};
    static constexpr StylePartSchema input_parts[]{
        {StylePart::root, style_properties::surface, field, {}},
        {StylePart::text, text, field, StylePart::root, all_properties, {}, native_font_limits},
        {StylePart::header, text, field, {}, all_properties, {}, native_font_limits},
        {StylePart::placeholder, ink, field, {}},
        {StylePart::icon, ink, field, {}},
        {StylePart::shortcut, text | style_property(StyleProperty::background) |
            style_property(StyleProperty::border_brush) | style_property(StyleProperty::corner_radius), field, {}},
        {StylePart::clear_action, ink | style_property(StyleProperty::background) |
            style_property(StyleProperty::border_brush) | style_property(StyleProperty::corner_radius),
            field | style_states::hovered | style_states::pressed, StylePart::root},
    };
    static constexpr StylePartSchema document_parts[]{
        {StylePart::root, style_properties::surface, document, {}},
        {StylePart::text, text, document, StylePart::root, all_properties, {}, native_font_limits},
    };
    static constexpr StylePartSchema password_parts[]{
        {StylePart::root, style_properties::surface, password, {}},
        {StylePart::text, text, password, StylePart::root, all_properties, {}, native_font_limits},
    };
    static constexpr StylePartSchema date_parts[]{
        {StylePart::root, style_properties::surface & ~ink, focus, {}},
        {StylePart::text, style_properties::typography, focus, {}, all_properties, {}, native_font_limits},
    };
    static const StyleTargetSchema input_schema{input_parts, order};
    static const StyleTargetSchema document_schema{document_parts, order};
    static const StyleTargetSchema password_schema{password_parts, order};
    static const StyleTargetSchema date_schema{date_parts, order};
    switch (target) {
    case StyleTarget::text_input: return &input_schema;
    case StyleTarget::multiline_text:
    case StyleTarget::rich_text: return &document_schema;
    case StyleTarget::password_input: return &password_schema;
    case StyleTarget::date_time_picker: return &date_schema;
    default: return nullptr;
    }
}
}
