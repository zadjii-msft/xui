#pragma once

#include "xui/core.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace xui {

enum class ThemeMode { dark, light, high_contrast };

struct ThemeColors {
    uint32_t background, surface, field, hover, text, secondary;
    uint32_t selection, selection_text, border, accent, folder, file, error;
};

constexpr ThemeColors theme_colors(ThemeMode mode) {
    if (mode == ThemeMode::light)
        return {0xf1f4f7, 0xffffff, 0xf8fafc, 0xeef4f8, 0x1c2935, 0x536577,
                0xdceff6, 0x123a49, 0xc1cbd4, 0x00718d, 0x896111, 0x536577, 0xaa2435};
    return {0x15181b, 0x1c2024, 0x242a30, 0x29313a, 0xe6edf3, 0xa0adb9,
            0x164f65, 0xf2fbff, 0x35404a, 0x70d7ed, 0xe5be70, 0xa0b8d0, 0xffb4ab};
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
