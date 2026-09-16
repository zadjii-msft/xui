#pragma once
#include "xui/control_styling.hpp"
#include <algorithm>

namespace xui {
inline Rect host_content_rect(Rect bounds, const PartStyleValues* style,
    Insets default_padding = {}, Insets default_border = {}) {
    auto content = style_content_bounds(bounds, style, default_padding, default_border);
    content.x = std::min(bounds.x + bounds.width, content.x);
    content.y = std::min(bounds.y + bounds.height, content.y);
    return content;
}
}
