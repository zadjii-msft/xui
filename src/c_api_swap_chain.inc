xui_status XUI_CALL xui_swap_chain_set(xui_handle panel, void* swap_chain) noexcept {
    return boundary([&] {
        const auto node = get(panel, XUI_SWAP_CHAIN_PANEL);
        editable(node->owner);
        as<xui::SwapChainPanel>(node).set_swap_chain(static_cast<IDXGISwapChain*>(swap_chain));
    }, true);
}
xui_status XUI_CALL xui_swap_chain_set_surface(xui_handle panel, void* surface) noexcept {
    return boundary([&] {
        const auto node = get(panel, XUI_SWAP_CHAIN_PANEL);
        editable(node->owner);
        as<xui::SwapChainPanel>(node).set_swap_chain_handle(surface);
    }, true);
}
xui_status XUI_CALL xui_swap_chain_get_metrics(xui_handle panel, xui_swap_chain_metrics* metrics) noexcept {
    return boundary([&] {
        require(metrics != nullptr, XUI_INVALID_ARGUMENT, "Missing metrics output.");
        require(metrics->size == sizeof(*metrics), XUI_VERSION_MISMATCH, "Swap chain metrics size mismatch.");
        const auto& value = as<xui::SwapChainPanel>(get(panel, XUI_SWAP_CHAIN_PANEL)).metrics();
        *metrics = {sizeof(*metrics), value.pixel_width, value.pixel_height, value.rasterization_scale, uint32_t(value.visible)};
    });
}
xui_status XUI_CALL xui_swap_chain_get_window(xui_handle panel, void** window) noexcept {
    return boundary([&] {
        require(window != nullptr, XUI_INVALID_ARGUMENT, "Missing native window output.");
        *window = as<xui::SwapChainPanel>(get(panel, XUI_SWAP_CHAIN_PANEL)).native_window();
    });
}
xui_status XUI_CALL xui_swap_chain_get_visible_pixel_bounds(xui_handle panel,
    xui_swap_chain_visible_pixel_bounds* bounds) noexcept {
    return boundary([&] {
        require(bounds != nullptr, XUI_INVALID_ARGUMENT, "Missing visible pixel bounds output.");
        require(bounds->size == sizeof(*bounds), XUI_VERSION_MISMATCH, "Visible pixel bounds size mismatch.");
        const auto value = as<xui::SwapChainPanel>(get(panel, XUI_SWAP_CHAIN_PANEL)).visible_pixel_bounds();
        *bounds = {sizeof(*bounds), value.x, value.y, value.width, value.height};
    });
}
xui_status XUI_CALL xui_swap_chain_native_input(xui_handle panel, uint32_t enabled) noexcept {
    return boundary([&] {
        require(enabled <= 1, XUI_INVALID_ARGUMENT, "Expected a native-input boolean.");
        const auto node = get(panel, XUI_SWAP_CHAIN_PANEL);
        editable(node->owner);
        as<xui::SwapChainPanel>(node).set_native_input(enabled != 0);
    });
}
