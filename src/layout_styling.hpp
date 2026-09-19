#pragma once
#include "xui/control_styling.hpp"
#include <algorithm>

namespace xui::layout_style {
inline Insets insets(const PartStyleValues* values, Insets fallback = {}, bool local_padding = false) {
    const auto p = values && values->padding && !local_padding ? *values->padding : fallback;
    const auto b = values ? values->border_thickness.value_or(Insets{}) : Insets{};
    return {p.left + b.left, p.top + b.top, p.right + b.right, p.bottom + b.bottom};
}
inline Rect inset(Rect bounds, Insets p) {
    const auto left = std::min(bounds.width, p.left), top = std::min(bounds.height, p.top);
    return {bounds.x + left, bounds.y + top, std::max(0.0f, bounds.width - left - p.right),
        std::max(0.0f, bounds.height - top - p.bottom)};
}
inline Size inner(Size available, Insets p) {
    return {std::max(0.0f, available.width - p.left - p.right),
        std::max(0.0f, available.height - p.top - p.bottom)};
}
inline Size outer(Size desired, Insets p) {
    return {desired.width + p.left + p.right, desired.height + p.top + p.bottom};
}
inline void align_axis(float& origin, float& extent, float desired, std::optional<StyleAlignment> alignment) {
    if (!alignment || *alignment == StyleAlignment::stretch) return;
    const auto size = std::min(extent, desired);
    if (*alignment == StyleAlignment::center) origin += (extent - size) / 2;
    else if (*alignment == StyleAlignment::end) origin += extent - size;
    extent = size;
}
inline Rect aligned(Rect bounds, Size desired, const PartStyleValues* values) {
    if (!values) return bounds;
    align_axis(bounds.x, bounds.width, desired.width, values->horizontal_alignment);
    align_axis(bounds.y, bounds.height, desired.height, values->vertical_alignment);
    return bounds;
}
inline Rect aligned_cross(Rect bounds, Size desired, const PartStyleValues* values, Axis main_axis) {
    if (!values) return bounds;
    if (main_axis == Axis::horizontal)
        align_axis(bounds.y, bounds.height, desired.height, values->vertical_alignment);
    else
        align_axis(bounds.x, bounds.width, desired.width, values->horizontal_alignment);
    return bounds;
}
inline Rect content(const Element& owner, Rect bounds, StylePart part = StylePart::root) {
    return inset(bounds, insets(owner.effective_control_style_values(part)));
}
}
