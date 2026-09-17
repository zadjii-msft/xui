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
