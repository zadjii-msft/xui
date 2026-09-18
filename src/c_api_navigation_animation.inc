xui_status XUI_CALL xui_navigation_view_set_duration(xui_handle target, uint32_t milliseconds) noexcept {
    return boundary([&] {
        auto node = get(target, XUI_NAVIGATION_VIEW); editable(node->owner);
        require(milliseconds <= 10000, XUI_INVALID_ARGUMENT, "Navigation duration must be between 0 and 10000 milliseconds.");
        as<xui::NavigationView>(node).set_duration(milliseconds);
    });
}
xui_status XUI_CALL xui_navigation_view_get_duration(xui_handle target, uint32_t* milliseconds) noexcept {
    return boundary([&] {
        require(milliseconds, XUI_INVALID_ARGUMENT, "Missing navigation duration output.");
        *milliseconds = as<xui::NavigationView>(get(target, XUI_NAVIGATION_VIEW)).duration();
    });
}
xui_status XUI_CALL xui_navigation_view_get_animating(xui_handle target, uint32_t* animating) noexcept {
    return boundary([&] {
        require(animating, XUI_INVALID_ARGUMENT, "Missing navigation animation output.");
        *animating = as<xui::NavigationView>(get(target, XUI_NAVIGATION_VIEW)).animating() ? 1u : 0u;
    });
}
