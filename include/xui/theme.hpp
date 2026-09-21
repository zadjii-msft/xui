#pragma once

#include "xui/core.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace xui {

enum class ThemeMode { dark, light, high_contrast, system };
enum class VisualStyle { classic, winui };
enum class ButtonAppearance { standard, accent, subtle };

struct ThemeColors {
    uint32_t background, surface, field, hover, text, secondary;
    uint32_t selection, selection_text, border, accent, folder, file, error;
};

constexpr ThemeColors theme_colors(ThemeMode mode, VisualStyle style = VisualStyle::classic) {
    if (style == VisualStyle::winui) {
        if (mode == ThemeMode::light)
            return {0xf3f3f3, 0xffffff, 0xfafafa, 0xf0f0f0, 0x1a1a1a, 0x616161,
                    0xebebeb, 0x1a1a1a, 0xe5e5e5, 0x005fb8, 0x9d6600, 0x616161, 0xc42b1c};
        return {0x202020, 0x272727, 0x1e1e1e, 0x323232, 0xffffff, 0xcecece,
                0x383838, 0xffffff, 0x404040, 0x60cdff, 0xe5be70, 0xcecece, 0xff99a4};
    }
    if (mode == ThemeMode::light)
        return {0xf1f4f7, 0xffffff, 0xf8fafc, 0xeef4f8, 0x1c2935, 0x536577,
                0xdceff6, 0x123a49, 0xc1cbd4, 0x00718d, 0x896111, 0x536577, 0xaa2435};
    return {0x15181b, 0x1c2024, 0x242a30, 0x29313a, 0xe6edf3, 0xa0adb9,
            0x164f65, 0xf2fbff, 0x35404a, 0x70d7ed, 0xe5be70, 0xa0b8d0, 0xffb4ab};
}

struct WinUIControlColors {
    uint32_t fill, hover, pressed, disabled, text, secondary, disabled_text;
    uint32_t stroke, bottom_stroke, accent, accent_hover, accent_pressed, accent_text;
};

constexpr WinUIControlColors winui_control_colors(ThemeMode mode) {
    if (mode == ThemeMode::light)
        return {0xffffff, 0xf9f9f9, 0xf0f0f0, 0xf5f5f5, 0x1a1a1a, 0x606060, 0x9b9b9b,
                0xe5e5e5, 0xcccccc, 0x005fb8, 0x196fc0, 0x2675b9, 0xffffff};
    return {0x333333, 0x3e3e3e, 0x303030, 0x2d2d2d, 0xffffff, 0xcecece, 0x838383,
            0x454545, 0x292929, 0x60cdff, 0x58bee9, 0x50aed5, 0x000000};
}

struct ButtonVisual {
    uint32_t fill, text, stroke, bottom_stroke;
    bool fill_visible{true}, stroke_visible{true};
};

constexpr uint32_t winui_input_background(ThemeMode mode, bool enabled, bool focused, bool hovered) {
    if (mode == ThemeMode::light)
        return !enabled ? 0x4df9f9f9 : focused ? 0xffffffff : hovered ? 0x80f9f9f9 : 0xb3ffffff;
    return !enabled ? 0x0bffffff : focused ? 0xb31e1e1e : hovered ? 0x15ffffff : 0x0fffffff;
}

struct InputStrokeColors {
    uint32_t outline, elevation;
};

struct FocusStrokeColors {
    uint32_t outer, inner;
};

struct WinUICardBrushes {
    uint32_t fill, secondary_fill, stroke;
};

constexpr WinUICardBrushes winui_card_brushes(ThemeMode mode) {
    return mode == ThemeMode::light ? WinUICardBrushes{0xb3ffffff, 0x80f6f6f6, 0x0f000000} :
        WinUICardBrushes{0x0dffffff, 0x08ffffff, 0x19000000};
}

constexpr FocusStrokeColors winui_focus_strokes(ThemeMode mode) {
    return mode == ThemeMode::light ? FocusStrokeColors{0xe4000000, 0xb3ffffff} :
        FocusStrokeColors{0xffffffff, 0xb3000000};
}

constexpr InputStrokeColors winui_input_strokes(ThemeMode mode) {
    return mode == ThemeMode::light ? InputStrokeColors{0x0f000000, 0x72000000} :
        InputStrokeColors{0x12ffffff, 0x8bffffff};
}

constexpr uint32_t composite_argb_on_rgb(uint32_t foreground, uint32_t background) {
    const auto alpha = foreground >> 24;
    const auto channel = [&](unsigned shift) {
        return ((((foreground >> shift) & 255) * alpha +
            ((background >> shift) & 255) * (255 - alpha) + 127) / 255) << shift;
    };
    return channel(16) | channel(8) | channel(0);
}

struct WinUIStatusColors {
    uint32_t information_fill, success_fill, warning_fill, error_fill;
    uint32_t success, warning;
};

constexpr WinUIStatusColors winui_status_colors(ThemeMode mode) {
    if (mode == ThemeMode::light)
        return {0xf3f3f3, 0xdff6dd, 0xfff4ce, 0xfde7e9, 0x0f7b0f, 0x9d5d00};
    return {0x303030, 0x263b29, 0x433519, 0x442726, 0x6ccb5f, 0xfce100};
}

struct StyleMetrics {
    float control_radius, surface_radius, progress_thickness;
    float button_height, field_height, input_header_height, input_header_spacing, button_padding;
};

constexpr StyleMetrics style_metrics(VisualStyle style) {
    return style == VisualStyle::winui ? StyleMetrics{4, 8, 4, 32, 32, 20, 8, 12} :
        StyleMetrics{6, 6, 6, 36, 44, 24, 0, 14};
}

inline constexpr float winui_slider_thumb_layout_size = 18.0f;
inline constexpr float winui_slider_thumb_outset = 2.0f;

struct SliderVisual {
    Rect track, filled, thumb;
    float travel_inset{};
    double pointer_fraction(Point point, Axis orientation) const {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            (orientation != Axis::horizontal && orientation != Axis::vertical))
            throw std::invalid_argument("Invalid slider pointer geometry");
        const bool vertical = orientation == Axis::vertical;
        const float length = (vertical ? track.height : track.width) - 2 * travel_inset;
        if (length <= 0) return 0;
        const double value = vertical ? 1.0 - (point.y - track.y - travel_inset) / length :
            (point.x - track.x - travel_inset) / length;
        return std::clamp(value, 0.0, 1.0);
    }
};

inline SliderVisual slider_visual(Size bounds, Axis orientation, bool reversed, double fraction, VisualStyle style,
    float thickness = 4, std::optional<float> thumb_size = {}) {
    if (!std::isfinite(bounds.width) || !std::isfinite(bounds.height) || bounds.width < 0 || bounds.height < 0 ||
        !std::isfinite(fraction) || fraction < 0 || fraction > 1 ||
        !std::isfinite(thickness) || thickness < 0 || thickness > 32768 ||
        (thumb_size && (!std::isfinite(*thumb_size) || *thumb_size < 0 || *thumb_size > 32768)) ||
        (orientation != Axis::horizontal && orientation != Axis::vertical))
        throw std::invalid_argument("Invalid slider geometry");
    const bool vertical = orientation == Axis::vertical;
    const bool winui = style == VisualStyle::winui;
    if (reversed) fraction = 1 - fraction;
    const float layout_radius = thumb_size ? *thumb_size / 2 : winui ? winui_slider_thumb_layout_size / 2 : 8.0f;
    const float radius = layout_radius + (winui && !thumb_size ? winui_slider_thumb_outset : 0);
    const float extent = vertical ? bounds.height : bounds.width;
    const float inset = std::min(winui ? layout_radius : std::max(12.0f, radius), extent / 2);
    const float length = std::max(0.0f, extent - 2 * inset);
    const float position = inset + static_cast<float>(vertical ? 1 - fraction : fraction) * length;
    const float cross_extent = vertical ? bounds.width : bounds.height;
    const float cross = winui ?
        std::min(cross_extent / 2, std::max(14 + thickness / 2, radius)) : cross_extent / 2;
    const float track_start = winui ? 0 : inset, track_length = winui ? extent : length;
    const Rect track = vertical ? Rect{cross - thickness / 2, track_start, thickness, track_length} :
        Rect{track_start, cross - thickness / 2, track_length, thickness};
    const float start = vertical == reversed ? track_start : track_start + track_length;
    const float end = position + (winui ? (vertical == reversed ? -inset : inset) : 0);
    const float filled = std::abs(end - start);
    const Rect fill = vertical ? Rect{track.x, std::min(start, end), thickness, filled} :
        Rect{std::min(start, end), track.y, filled, thickness};
    const Rect thumb = vertical ? Rect{cross - radius, position - radius, 2 * radius, 2 * radius} :
        Rect{position - radius, cross - radius, 2 * radius, 2 * radius};
    return {track, fill, thumb, winui ? inset : 0};
}

constexpr uint32_t winui_text_brush(ThemeMode mode, bool enabled, bool pressed = false, bool on_accent = false) {
    const bool light = mode == ThemeMode::light;
    if (on_accent)
        return !enabled ? (light ? 0xffffffff : 0x87ffffff) :
            pressed ? (light ? 0xb3ffffff : 0x80000000) : light ? 0xffffffff : 0xff000000;
    return !enabled ? (light ? 0x5c000000 : 0x5dffffff) :
        pressed ? (light ? 0x9e000000 : 0xc5ffffff) : light ? 0xe4000000 : 0xffffffff;
}

constexpr uint32_t winui_accent_brush(ThemeMode mode, bool enabled, bool hovered, bool pressed) {
    if (!enabled) return mode == ThemeMode::light ? 0x37000000 : 0x28ffffff;
    return (pressed ? 0xcc000000 : hovered ? 0xe6000000 : 0xff000000) | winui_control_colors(mode).accent;
}

struct IndicatorBrushes {
    uint32_t fill, stroke, mark, text;
};

// ARGB template brushes; the painter composites them over the actual parent.
constexpr IndicatorBrushes winui_indicator_brushes(ThemeMode mode, bool marked, bool enabled,
    bool hovered, bool pressed, bool radio = false) {
    const bool light = mode == ThemeMode::light;
    const auto strong_disabled = light ? 0x37000000u : 0x28ffffffu;
    const auto fill = marked ? winui_accent_brush(mode, enabled, hovered, pressed) : !enabled ? 0x00ffffffu :
        pressed ? (light ? 0x18000000u : 0x12ffffffu) :
        hovered ? (light ? 0x0f000000u : 0x0bffffffu) : light ? 0x06000000u : 0x19000000u;
    return {fill, marked ? fill : !enabled || pressed ? strong_disabled : light ? 0x72000000u : 0x8bffffffu,
        winui_text_brush(mode, radio || enabled, !radio && pressed, true), winui_text_brush(mode, enabled)};
}

constexpr float winui_radio_dot(bool enabled, bool hovered, bool pressed) {
    return !enabled ? 14.0f : pressed ? 10.0f : hovered ? 14.0f : 12.0f;
}

constexpr IndicatorBrushes winui_switch_brushes(ThemeMode mode, bool checked, bool enabled, bool hovered, bool pressed) {
    auto brushes = winui_indicator_brushes(mode, checked, enabled, hovered, pressed);
    brushes.stroke = checked ? 0 : winui_indicator_brushes(mode, false, enabled, false, false).stroke;
    brushes.mark = winui_text_brush(mode, enabled, !checked, checked);
    return brushes;
}

struct ButtonBrushes {
    uint32_t fill, text, stroke, elevation;
    bool accent{}, elevated{};
};

struct SliderBrushes {
    uint32_t track, value, thumb;
};

constexpr SliderBrushes winui_slider_brushes(ThemeMode mode, bool enabled, bool hovered, bool pressed) {
    const bool light = mode == ThemeMode::light;
    return {enabled ? (light ? 0x72000000u : 0x8bffffffu) : (light ? 0x51000000u : 0x3fffffffu),
        winui_accent_brush(mode, enabled, hovered, pressed), light ? 0xffffffffu : 0xff454545u};
}

constexpr ButtonBrushes winui_button_brushes(ThemeMode mode, ButtonAppearance appearance,
    bool enabled, bool hovered, bool pressed, bool checked) {
    const bool light = mode == ThemeMode::light;
    const bool accent = appearance == ButtonAppearance::accent || checked;
    const auto text = winui_text_brush(mode, enabled, pressed, accent);
    if (accent)
        return {winui_accent_brush(mode, enabled, hovered, pressed), text,
            !enabled || pressed ? 0u : 0x14ffffffu, light ? 0x66000000u : 0x23000000u, true, enabled && !pressed};
    if (appearance == ButtonAppearance::subtle) {
        const auto fill = !enabled ? 0u : pressed ? (light ? 0x06000000u : 0x0affffffu) :
            hovered ? (light ? 0x09000000u : 0x0fffffffu) : 0u;
        return {fill, text, fill, fill};
    }
    return {enabled && pressed ? (light ? 0x4df9f9f9u : 0x08ffffffu) :
        winui_input_background(mode, enabled, false, hovered), text, winui_input_strokes(mode).outline,
        light ? 0x29000000u : 0x18ffffffu, false, enabled && !pressed};
}

// RGB preview over the default card. High contrast uses system colors in the backend.
constexpr ButtonVisual winui_button_visual(ThemeMode mode, ButtonAppearance appearance,
    bool enabled, bool hovered, bool pressed, bool checked) {
    const auto brushes = winui_button_brushes(mode, appearance, enabled, hovered, pressed, checked);
    const auto background = theme_colors(mode, VisualStyle::winui).surface;
    const auto fill = composite_argb_on_rgb(brushes.fill, background);
    const auto stroke_background = brushes.accent ? fill : background;
    return {fill, composite_argb_on_rgb(brushes.text, fill), composite_argb_on_rgb(brushes.stroke, stroke_background),
        composite_argb_on_rgb(brushes.elevated ? brushes.elevation : brushes.stroke, stroke_background),
        (brushes.fill >> 24) != 0, (brushes.stroke >> 24) != 0};
}

struct VisualMetrics {
    static constexpr float body_size = 14;
    static constexpr float caption_size = 12;
    static constexpr float heading_size = 24;
    static constexpr float gutter = 16;
    static constexpr float radius = 6;
    static constexpr Insets search_insets{38, 12, 72, 10};
};

struct ScrollThumb {
    float top{}, height{}, travel{}, extent{};
};

// Shared by painting and pointer input so the drag geometry cannot drift.
inline ScrollThumb scroll_thumb(float content, float viewport, float offset, float track) {
    if (!std::isfinite(content) || !std::isfinite(viewport) || !std::isfinite(track) ||
        content <= viewport || viewport <= 0 || track <= 0) return {};
    const float minimum = std::min(28.0f, track * 0.5f);
    const float height = std::min(track, std::max(minimum, track * (viewport / content)));
    const float extent = content - viewport;
    const float travel = track - height;
    const float position = std::isfinite(offset) ? std::clamp(offset, 0.0f, extent) : 0;
    return {travel * (position / extent), height, travel, extent};
}

inline float scroll_from_thumb(const ScrollThumb& thumb, float position) {
    if (thumb.travel <= 0 || !std::isfinite(position)) return 0;
    return std::clamp(position / thumb.travel, 0.0f, 1.0f) * thumb.extent;
}

}
