#include "xui/control_styling.hpp"

namespace xui {
const StyleTargetSchema* navigation_composites_style_schema(StyleTarget target) {
    constexpr auto surface = style_properties::surface & ~style_property(StyleProperty::foreground);
    constexpr auto paint = surface & ~(style_property(StyleProperty::padding) | style_property(StyleProperty::border_thickness));
    constexpr auto disabled = style_states::disabled;
    constexpr auto overflowed = disabled | style_states::overflowed;
    constexpr auto navigation = disabled | style_states::expanded | style_states::compact | style_states::empty;
    constexpr auto pane = disabled | style_states::loading | style_states::error | style_states::empty;
    constexpr auto caption = style_states::interaction | style_states::active | style_states::inactive | style_states::maximized;
    constexpr auto tabs = style_states::interaction | style_states::selected;
    constexpr StyleValueLimits text_limits{32768.0f, 1024, 7, 15, 7};
    static constexpr StyleStateMask precedence[]{
        style_states::active, style_states::inactive, style_states::maximized, style_states::expanded,
        style_states::compact, style_states::empty, style_states::loading, style_states::error,
        style_states::overflowed, style_states::open, style_states::focused, style_states::selected,
        style_states::current, style_states::hovered, style_states::pressed, style_states::disabled
    };
    static constexpr StylePartSchema navigation_parts[]{
        {StylePart::root, surface, navigation, {}}
    };
    static constexpr StylePartSchema pane_parts[]{
        {StylePart::root, surface, pane, {}}
    };
    static constexpr StylePartSchema breadcrumb_parts[]{
        {StylePart::root, surface, overflowed, {}, paint},
        {StylePart::separator, style_property(StyleProperty::foreground), disabled, {}}
    };
    static constexpr StylePartSchema command_parts[]{
        {StylePart::root, surface, overflowed, {}, paint},
        {StylePart::separator, style_property(StyleProperty::background) | style_property(StyleProperty::thickness),
            disabled, {}, style_property(StyleProperty::background)}
    };
    static constexpr StylePartSchema title_parts[]{
        {StylePart::root, surface, (caption & ~style_states::interaction) | disabled, {}},
        {StylePart::caption_button, style_properties::surface, caption, {}},
        {StylePart::caption_close, style_properties::surface, caption, {}}
    };
    static constexpr StylePartSchema tab_parts[]{
        {StylePart::root, surface, style_states::interaction, {}},
        {StylePart::tab, style_properties::surface | style_property(StyleProperty::width), tabs, {},
            paint | style_property(StyleProperty::foreground)},
        {StylePart::label, style_properties::text, tabs, StylePart::tab, ~StylePropertyMask(0), {}, text_limits},
        {StylePart::close_action, style_properties::surface | style_property(StyleProperty::size), tabs, StylePart::tab,
            style_properties::surface},
        {StylePart::separator, style_property(StyleProperty::background) | style_property(StyleProperty::thickness), disabled, {}},
        {StylePart::selection, paint | style_property(StyleProperty::thickness), tabs, {}}
    };
    static constexpr StylePartSchema tooltip_parts[]{
        {StylePart::root, style_properties::surface, style_states::open, {}},
        {StylePart::text, style_properties::text, style_states::open, StylePart::root, ~StylePropertyMask(0), {}, text_limits}
    };
    static constexpr StylePartSchema split_button_parts[]{
        {StylePart::root, surface | style_property(StyleProperty::spacing) | style_properties::alignment, disabled, {}},
        {StylePart::separator, style_property(StyleProperty::background), disabled, {}}
    };
    static const StyleTargetSchema navigation_schema{navigation_parts, precedence},
        pane_schema{pane_parts, precedence}, breadcrumb_schema{breadcrumb_parts, precedence},
        command_schema{command_parts, precedence}, title_schema{title_parts, precedence},
        tab_schema{tab_parts, precedence}, tooltip_schema{tooltip_parts, precedence},
        split_button_schema{split_button_parts, precedence};
    switch (target) {
    case StyleTarget::navigation_view: return &navigation_schema;
    case StyleTarget::navigation_pane: return &pane_schema;
    case StyleTarget::breadcrumb: return &breadcrumb_schema;
    case StyleTarget::command_bar: return &command_schema;
    case StyleTarget::title_bar: return &title_schema;
    case StyleTarget::tab_strip: return &tab_schema;
    case StyleTarget::tooltip: return &tooltip_schema;
    case StyleTarget::split_button: return &split_button_schema;
    default: return nullptr;
    }
}
}
