XUI_API xui_status XUI_CALL xui_shell_preview_create(xui_handle window, xui_string name, xui_handle* result) noexcept {
    return boundary([&] {
        require(result != nullptr, XUI_INVALID_ARGUMENT, "Preview output is required");
        *result = 0;
        auto owner = get(window, XUI_WINDOW); topology(owner->owner);
        auto model = std::make_shared<xui::ShellPreview>(decode(name));
        const auto handle = insert(owner->owner, XUI_SHELL_PREVIEW, model);
        *result = handle;
    });
}
XUI_API xui_status XUI_CALL xui_shell_preview_load_local(xui_handle handle, xui_string path, uint64_t* generation) noexcept {
    return boundary([&] {
        require(generation != nullptr, XUI_INVALID_ARGUMENT, "Preview generation output is required");
        *generation = 0;
        auto node = get(handle, XUI_SHELL_PREVIEW); editable(node->owner);
        auto value = decode(path);
        require(!value.empty() && value.size() <= 32767, XUI_INVALID_ARGUMENT, "Preview path requires 1 to 32767 UTF-16 units");
        *generation = as<xui::ShellPreview>(node).load_local(std::move(value));
        callback_result(node->owner);
    });
}
XUI_API xui_status XUI_CALL xui_shell_preview_cancel(xui_handle handle, uint64_t generation) noexcept {
    return boundary([&] { auto node = get(handle, XUI_SHELL_PREVIEW); editable(node->owner); as<xui::ShellPreview>(node).cancel(generation); callback_result(node->owner); });
}
XUI_API xui_status XUI_CALL xui_shell_preview_unload(xui_handle handle) noexcept {
    return boundary([&] { auto node = get(handle, XUI_SHELL_PREVIEW); editable(node->owner); as<xui::ShellPreview>(node).unload(); callback_result(node->owner); });
}
XUI_API xui_status XUI_CALL xui_shell_preview_focus_content(xui_handle handle, uint32_t reverse) noexcept {
    return boundary([&] {
        require(reverse <= 1, XUI_INVALID_ARGUMENT, "Preview focus direction must be zero or one");
        auto node = get(handle, XUI_SHELL_PREVIEW); editable(node->owner);
        as<xui::ShellPreview>(node).focus_content(reverse != 0);
    });
}
XUI_API xui_status XUI_CALL xui_shell_preview_get_status(xui_handle handle, xui_preview_status* result) noexcept {
    return boundary([&] {
        require(result && result->size == sizeof(xui_preview_status) && result->version == XUI_PREVIEW_VERSION &&
            !result->reserved, XUI_INVALID_ARGUMENT, "Invalid preview status record");
        const auto& value = as<xui::ShellPreview>(get(handle, XUI_SHELL_PREVIEW)).status();
        *result = {sizeof(xui_preview_status), XUI_PREVIEW_VERSION, value.generation,
            static_cast<uint32_t>(value.state), static_cast<uint32_t>(value.reason), static_cast<uint32_t>(value.phase),
            value.hresult, static_cast<uint32_t>(value.cleanup), 0};
    });
}
