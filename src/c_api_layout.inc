#include "xui/xui_layout.h"
#include "xui/titlebar.hpp"

xui_status XUI_CALL xui_window_visual_style_set(xui_handle window, uint32_t style) noexcept {
    return boundary([&] {
        auto w = get(window, XUI_WINDOW); editable(w->owner);
        require(style <= XUI_STYLE_WINUI, XUI_INVALID_ARGUMENT, "Invalid visual style.");
        w->owner->window->set_visual_style(static_cast<xui::VisualStyle>(style));
    });
}
xui_status XUI_CALL xui_window_visual_style_get(xui_handle window, uint32_t* style) noexcept {
    return boundary([&] {
        require(style != nullptr, XUI_INVALID_ARGUMENT, "Missing visual style output.");
        auto w = get(window, XUI_WINDOW);
        *style = static_cast<uint32_t>(w->owner->window->visual_style());
    });
}

xui_status XUI_CALL xui_popup_placement(xui_handle target, uint32_t placement) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_POPUP); editable(n->owner);
        require(placement <= 4, XUI_INVALID_ARGUMENT, "Invalid popup placement.");
        as<xui::Popup>(n).set_placement(static_cast<xui::PopupPlacement>(placement));
    });
}
xui_status XUI_CALL xui_popup_window_background(xui_handle target, uint32_t enabled) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_POPUP); editable(n->owner);
        require(enabled <= 1, XUI_INVALID_ARGUMENT, "Invalid popup background flag.");
        as<xui::Popup>(n).set_window_background(enabled != 0);
    });
}
xui_status XUI_CALL xui_items_trailing_shortcut_badges(xui_handle target, uint32_t enabled) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_ITEMS_VIEW); editable(n->owner);
        require(enabled <= 1, XUI_INVALID_ARGUMENT, "Invalid shortcut badge flag.");
        as<xui::ItemsView>(n).set_trailing_shortcut_badges(enabled != 0);
    });
}
xui_status XUI_CALL xui_text_input_caption(xui_handle target, uint32_t visible) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_TEXT_INPUT); editable(n->owner);
        require(visible <= 1, XUI_INVALID_ARGUMENT, "Invalid input caption visibility.");
        as<xui::TextInput>(n).set_caption_visible(visible != 0);
    });
}
xui_status XUI_CALL xui_text_input_placeholder(xui_handle target, xui_string text) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_TEXT_INPUT); editable(n->owner);
        as<xui::TextInput>(n).set_placeholder(decode(text));
    });
}

xui_status XUI_CALL xui_window_titlebar_layout(xui_handle window, xui_handle first_pane,
    xui_handle second_pane, uint32_t show_title) noexcept {
    return boundary([&] {
        auto w = get(window, XUI_WINDOW); editable(w->owner);
        require(show_title <= 1, XUI_INVALID_ARGUMENT, "Invalid title visibility.");
        const auto& titlebar = w->owner->window->titlebar();
        require(titlebar != nullptr, XUI_WRONG_KIND, "A custom title bar is required.");
        auto first = get(first_pane); same(w, first);
        require(first->element != nullptr, XUI_WRONG_KIND, "A content pane element is required.");
        std::shared_ptr<xui::Element> second;
        if (second_pane) {
            auto node = get(second_pane); same(w, node);
            require(node->element != nullptr, XUI_WRONG_KIND, "A content pane element is required.");
            second = node->element;
        }
        require(first->element != second, XUI_INVALID_ARGUMENT, "Title bar panes must be distinct.");
        titlebar->set_tab_panes(first->element, second);
        titlebar->set_title_visible(show_title != 0);
    });
}
xui_status XUI_CALL xui_navigation_header(xui_handle target, uint32_t visible) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_NAVIGATION_VIEW); editable(n->owner);
        require(visible <= 1, XUI_INVALID_ARGUMENT, "Invalid navigation header visibility.");
        as<xui::NavigationView>(n).set_header_visible(visible != 0);
    });
}
xui_status XUI_CALL xui_element_bounds(xui_handle target, float* x, float* y, float* width, float* height) noexcept {
    return boundary([&] {
        require(x && y && width && height, XUI_INVALID_ARGUMENT, "Missing bounds output.");
        auto n = get(target);
        require(n->element != nullptr, XUI_WRONG_KIND, "An element is required.");
        const auto b = n->element->bounds();
        *x = b.x; *y = b.y; *width = b.width; *height = b.height;
    });
}
