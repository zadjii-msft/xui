namespace {
xui::FileDialogOptions file_dialog_options(const xui_file_dialog_options* options) {
    require(options && options->size == sizeof(*options) && options->version == XUI_FILE_DIALOG_VERSION,
        XUI_VERSION_MISMATCH, "Unsupported file dialog options.");
    require(!options->reserved && options->filter_count <= 32 && (options->filters || !options->filter_count),
        XUI_INVALID_ARGUMENT, "Invalid file dialog filters or reserved fields.");
    xui::FileDialogOptions value;
    value.title = decode(options->title);
    value.default_extension = decode(options->default_extension);
    value.suggested_name = decode(options->suggested_name);
    value.initial_directory = decode(options->initial_directory);
    value.filters.reserve(options->filter_count);
    for (uint32_t i = 0; i < options->filter_count; ++i)
        value.filters.push_back({decode(options->filters[i].name), decode(options->filters[i].pattern)});
    value.validate();
    return value;
}
xui_status window_file_dialog(xui_handle window, const xui_file_dialog_options* options,
    xui_file_dialog_receiver receiver, void* context, bool save) noexcept {
    return boundary([&] {
        require(receiver, XUI_INVALID_ARGUMENT, "A file dialog receiver is required.");
        auto node = get(window, XUI_WINDOW);
        const auto state = node->owner;
        editable(state);
        require(!state->content_context, XUI_BUSY, "Content scopes cannot open window dialogs.");
        require(!state->file_dialog_active, XUI_BUSY, "A window file dialog call is already active.");
        const auto value = file_dialog_options(options);
        struct Call {
            std::shared_ptr<State> state;
            explicit Call(std::shared_ptr<State> value) : state(std::move(value)) {
                state->file_dialog_active = true;
                ++state->callbacks;
            }
            ~Call() { --state->callbacks; state->file_dialog_active = false; }
        } call(state);
        std::optional<std::wstring> path;
        try {
            path = save ? state->window->show_save_file_dialog(value) : state->window->show_open_file_dialog(value);
        } catch (const std::invalid_argument&) { throw; }
        catch (const std::logic_error& e) {
            error(XUI_BUSY, e.what());
            throw Failure{XUI_BUSY, last_message.data()};
        }
        callback_result(state);
        const auto bytes = path ? encode(*path) : std::string{};
        const xui_string borrowed{bytes.data(), static_cast<uint32_t>(bytes.size()), 0};
        xui_status status{};
        try { status = receiver(context, path.has_value(), borrowed); }
        catch (...) { status = XUI_CALLBACK_FAILED; }
        if (status) { state->callback_failure = status; state->window->close(); }
        callback_result(state);
    }, true);
}
}
xui_status XUI_CALL xui_window_open_file_dialog(xui_handle window, const xui_file_dialog_options* options,
    xui_file_dialog_receiver receiver, void* context) noexcept {
    return window_file_dialog(window, options, receiver, context, false);
}
xui_status XUI_CALL xui_window_save_file_dialog(xui_handle window, const xui_file_dialog_options* options,
    xui_file_dialog_receiver receiver, void* context) noexcept {
    return window_file_dialog(window, options, receiver, context, true);
}
