#include "xui/xui_presentation.h"

uint32_t XUI_CALL xui_window_theme_version() noexcept { return XUI_WINDOW_THEME_VERSION; }
xui_status XUI_CALL xui_window_theme_get(xui_handle window, xui_window_theme_options* options) noexcept {
    return boundary([&] {
        require(options && options->size == sizeof(*options) && options->version == XUI_WINDOW_THEME_VERSION,
            XUI_VERSION_MISMATCH, "Window theme snapshot version mismatch.");
        const auto value = get(window, XUI_WINDOW)->owner->window->theme_options();
        xui_window_theme_options result{sizeof(result), XUI_WINDOW_THEME_VERSION, static_cast<uint32_t>(value.theme)};
        const auto copy = [&](const auto& color, uint32_t mask, xui_theme_color& output) {
            if (color) { result.mask |= mask; output = {color->light, color->dark}; }
        };
        copy(value.foreground, XUI_THEME_FOREGROUND, result.foreground);
        copy(value.background, XUI_THEME_BACKGROUND, result.background);
        copy(value.accent, XUI_THEME_ACCENT, result.accent);
        *options = result;
    });
}
xui_status XUI_CALL xui_window_theme_set(xui_handle window, const xui_window_theme_options* options) noexcept {
    return boundary([&] {
        require(options && options->size == sizeof(*options) && options->version == XUI_WINDOW_THEME_VERSION,
            XUI_VERSION_MISMATCH, "Window theme snapshot version mismatch.");
        require(options->theme <= 3 && !(options->mask & ~7u), XUI_INVALID_ARGUMENT, "Invalid requested theme or semantic mask.");
        xui::WindowThemeOptions value{static_cast<xui::ThemeMode>(options->theme)};
        const auto color = [&](uint32_t mask, xui_theme_color input) -> std::optional<xui::ThemeColor> {
            require(input.light <= 0xffffff && input.dark <= 0xffffff &&
                ((options->mask & mask) || (!input.light && !input.dark)), XUI_INVALID_ARGUMENT,
                "Invalid RGB24 pair or nonzero unset semantic role.");
            return options->mask & mask ? std::optional<xui::ThemeColor>{{input.light, input.dark}} : std::nullopt;
        };
        value.foreground = color(XUI_THEME_FOREGROUND, options->foreground);
        value.background = color(XUI_THEME_BACKGROUND, options->background);
        value.accent = color(XUI_THEME_ACCENT, options->accent);
        auto node = get(window, XUI_WINDOW); editable(node->owner);
        node->owner->window->set_theme_options(std::move(value));
    }, true);
}
