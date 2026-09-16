#include "xui/control_styling.hpp"

namespace xui {
const StyleTargetSchema* hosts_scenes_style_schema(StyleTarget target) {
    static constexpr StyleStateMask precedence[]{
        style_states::focused, style_states::empty, style_states::idle, style_states::loading,
        style_states::ready, style_states::playing, style_states::paused, style_states::stopped,
        style_states::suspended, style_states::selected, style_states::error,
        style_states::hovered, style_states::pressed, style_states::disabled
    };
    constexpr auto surface = style_properties::surface | style_properties::typography;
    constexpr auto text = style_properties::text;
    constexpr StyleValueLimits text_limits{32768.0f, 1024, 7, 15, 7};
    constexpr auto image_states = style_states::interaction | style_states::empty | style_states::loading |
        style_states::ready | style_states::error;
    constexpr auto vector_states = style_states::interaction | style_states::empty | style_states::selected;
    constexpr auto map_states = style_states::interaction | style_states::loading | style_states::error | style_states::selected;
    constexpr auto web_states = style_states::interaction | style_states::idle | style_states::loading |
        style_states::ready | style_states::stopped | style_states::suspended | style_states::error;
    constexpr auto media_states = web_states | style_states::playing | style_states::paused;
    static constexpr StylePartSchema image_parts[]{
        {StylePart::root, surface, image_states, {}},
        {StylePart::placeholder, text, image_states, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits},
        {StylePart::error, text, image_states, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits}
    };
    static constexpr StylePartSchema vector_parts[]{
        {StylePart::root, surface, vector_states, {}},
        {StylePart::empty, text, vector_states, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits},
        {StylePart::selection, style_property(StyleProperty::foreground), vector_states, {}}
    };
    static constexpr StylePartSchema map_parts[]{
        {StylePart::root, surface, map_states, {}},
        {StylePart::coordinate, text, map_states, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits},
        {StylePart::error, text, map_states, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits},
        {StylePart::selection, style_property(StyleProperty::foreground), map_states, {}}
    };
    static constexpr StylePartSchema media_parts[]{
        {StylePart::root, surface, media_states, {}},
        {StylePart::placeholder, text, media_states, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits},
        {StylePart::caption, text, media_states, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits},
        {StylePart::status, text, media_states, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits},
        {StylePart::error, text, media_states, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits}
    };
    static constexpr StylePartSchema web_parts[]{
        {StylePart::root, surface, web_states, {}},
        {StylePart::placeholder, text, web_states, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits},
        {StylePart::caption, text, web_states, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits},
        {StylePart::status, text, web_states, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits},
        {StylePart::error, text, web_states, StylePart::root, ~StylePropertyMask(0), StylePart::root, text_limits}
    };
    static const StyleTargetSchema image{image_parts, precedence}, vector{vector_parts, precedence},
        map{map_parts, precedence}, media{media_parts, precedence}, web{web_parts, precedence};
    switch (target) {
    case StyleTarget::image: return &image;
    case StyleTarget::vector_canvas: return &vector;
    case StyleTarget::map_view: return &map;
    case StyleTarget::media_playback: return &media;
    case StyleTarget::web_content: return &web;
    default: return nullptr;
    }
}
}
