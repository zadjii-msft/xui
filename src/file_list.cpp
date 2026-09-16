#include "xui/file_list.hpp"
#include "xui/theme.hpp"

namespace xui {
float FileList::row_height() const {
    if (has_control_styling()) if (const auto* values = effective_control_style_values(StylePart::root))
        return std::max(1.0f, values->row_height.value_or(32));
    return 32;
}
float FileList::scrollbar_width() const {
    if (has_control_styling()) if (const auto* values = effective_control_style_values(StylePart::scrollbar))
        return values->width.value_or(VisualMetrics::gutter);
    return VisualMetrics::gutter;
}
Rect FileList::content_viewport(float width) const {
    Insets inset{};
    if (has_control_styling()) if (const auto* values = effective_control_style_values(StylePart::root)) {
        const auto p = values->padding.value_or(Insets{});
        const auto b = values->border_thickness.value_or(Insets{});
        inset = {p.left + b.left, p.top + b.top, p.right + b.right, p.bottom + b.bottom};
    }
    const auto left = std::min(inset.left, width), top = std::min(inset.top, viewport_height());
    return {left, top, std::max(0.0f, width - left - inset.right - scrollbar_width()),
        std::max(0.0f, viewport_height() - top - inset.bottom)};
}
}
