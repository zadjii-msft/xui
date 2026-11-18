#pragma once
#include "xui/core.hpp"
#include <algorithm>
#include <cmath>

namespace xui::detail {
inline std::size_t bounded_index(double value, std::size_t count) {
    if (!(value > 0)) return 0;
    // SIZE_MAX can round up when converted to double; compare before casting.
    if (value >= static_cast<double>(count)) return count;
    return static_cast<std::size_t>(value);
}

// Uniform grids include one boundary row. Variable-height and animated sources
// keep their own row_at/presentation geometry.
template<class Height>
VisibleRange uniform_viewport(std::size_t count, Height height, double offset, Height viewport,
    std::size_t columns = 1) {
    if (!columns || !(height > 0) || !std::isfinite(height)) return {};
    const auto rows = count / columns + (count % columns != 0);
    const auto first_row = bounded_index(offset / height, rows);
    const auto first = first_row >= rows ? count : first_row * columns;
    const auto remaining = count - first;
    const auto visible = bounded_index(std::ceil(viewport / height) + 1,
        remaining / columns + (remaining % columns != 0));
    const auto length = visible > remaining / columns ? remaining : visible * columns;
    return {first, first + length};
}
}
