namespace {
struct FileCall {
    std::shared_ptr<State> state;
    explicit FileCall(std::shared_ptr<State> value) : state(std::move(value)) { ++state->callbacks; }
    ~FileCall() { --state->callbacks; }
};
std::vector<std::wstring> file_paths(const xui_string* paths, uint32_t count, bool allow_empty = false) {
    require((paths || !count) && count <= xui::maximum_transfer_paths && (count || allow_empty),
        XUI_INVALID_ARGUMENT, "File transfer requires 1 to 4096 paths.");
    std::vector<std::wstring> result; result.reserve(count);
    std::size_t units = 11;
    for (uint32_t i = 0; i < count; ++i) {
        auto path = decode(paths[i]);
        require(path.size() <= 32767, XUI_INVALID_ARGUMENT, "File path exceeds 32767 UTF-16 units.");
        units += path.size() + 1;
        require(units <= 8 * 1024 * 1024, XUI_INVALID_ARGUMENT, "File transfer paths exceed 16 MiB.");
        result.push_back(std::move(path));
    }
    return result;
}
xui::FileTransferEffect file_effect(uint32_t effect) {
    require(effect == XUI_FILE_COPY || effect == XUI_FILE_MOVE, XUI_INVALID_ARGUMENT, "Use Copy or Move, not both.");
    return static_cast<xui::FileTransferEffect>(effect);
}
struct FileSpans {
    std::vector<std::string> strings;
    std::vector<xui_string> spans;
    explicit FileSpans(const std::vector<std::wstring>& paths) {
        strings.reserve(paths.size()); spans.reserve(paths.size());
        for (const auto& path : paths) strings.push_back(encode(path));
        for (const auto& path : strings) spans.push_back({path.data(), static_cast<uint32_t>(path.size()), 0});
    }
};
template<class F> bool file_callback(const std::shared_ptr<Node>& node, F callback) {
    const auto state = node->owner;
    if (state->closed || state->callback_failure) return false;
    if (node->file_dispatching) {
        state->callback_failure = XUI_BUSY; state->window->close(); return false;
    }
    node->file_dispatching = true;
    FileCall scope(state);
    xui_status status{};
    try { status = callback(); } catch (...) { status = XUI_CALLBACK_FAILED; }
    node->file_dispatching = false;
    if (status) { state->callback_failure = status; state->window->close(); }
    return !status && !state->closed && !state->callback_failure;
}
}
xui_status XUI_CALL xui_window_set_file_clipboard(xui_handle window, const xui_string* paths, uint32_t count, uint32_t effect) noexcept {
    return boundary([&] {
        auto node = get(window, XUI_WINDOW); editable(node->owner); FileCall scope(node->owner);
        node->owner->window->set_file_clipboard(file_paths(paths, count), file_effect(effect));
        callback_result(node->owner);
    });
}
xui_status XUI_CALL xui_window_get_file_clipboard(xui_handle window, xui_file_receiver receiver, void* context) noexcept {
    return boundary([&] {
        require(receiver, XUI_INVALID_ARGUMENT, "Missing clipboard receiver.");
        auto node = get(window, XUI_WINDOW); editable(node->owner); FileCall scope(node->owner);
        auto content = node->owner->window->get_file_clipboard();
        FileSpans values(content ? content->paths : std::vector<std::wstring>{});
        const auto ok = file_callback(node, [&] { return receiver(context, values.spans.data(),
            static_cast<uint32_t>(values.spans.size()), content ? static_cast<uint32_t>(content->effect) : 0); });
        require(ok, XUI_CALLBACK_FAILED, "File clipboard receiver failed.");
    });
}
xui_status XUI_CALL xui_window_set_clipboard_text(xui_handle window, xui_string text) noexcept {
    return boundary([&] {
        auto node = get(window, XUI_WINDOW); editable(node->owner); FileCall scope(node->owner);
        node->owner->window->set_clipboard_text(decode(text)); callback_result(node->owner);
    });
}
xui_status XUI_CALL xui_window_transfer_files(xui_handle window, const xui_string* paths, uint32_t count,
    xui_string destination, uint32_t effect, uint32_t* completed) noexcept {
    return boundary([&] {
        require(completed, XUI_INVALID_ARGUMENT, "Missing file transfer result."); *completed = 0;
        auto node = get(window, XUI_WINDOW); editable(node->owner); FileCall scope(node->owner);
        *completed = node->owner->window->transfer_files(file_paths(paths, count), decode(destination), file_effect(effect));
        callback_result(node->owner);
    });
}
xui_status XUI_CALL xui_window_paste_files(xui_handle window, xui_string destination, uint32_t* result) noexcept {
    return boundary([&] {
        require(result, XUI_INVALID_ARGUMENT, "Missing paste result."); *result = 0;
        auto node = get(window, XUI_WINDOW); editable(node->owner); FileCall scope(node->owner);
        const auto completed = node->owner->window->paste_files(decode(destination));
        *result = completed ? (*completed ? 1 : 2) : 0;
        callback_result(node->owner);
    });
}
xui_status XUI_CALL xui_grid_file_drag_paths(xui_handle target, const xui_string* paths, uint32_t count) noexcept {
    return boundary([&] {
        auto node = get(target, XUI_DATA_GRID); editable(node->owner);
        require(node->file_requesting, XUI_BUSY, "Supply drag paths only during its Request.");
        node->file_paths = file_paths(paths, count, true);
    });
}
xui_status XUI_CALL xui_grid_file_drag_bind(xui_handle target, xui_callback callback, void* context) noexcept {
    return boundary([&] {
        auto node = get(target, XUI_DATA_GRID); editable(node->owner);
        require(!node->file_dispatching, XUI_BUSY, "Cannot replace file callbacks during dispatch.");
        node->file_drag_callback = callback; node->file_drag_context = context;
        auto& grid = as<xui::DataGrid>(node);
        if (!callback) { grid.on_file_drag({}, {}); return; }
        std::weak_ptr<Node> weak = node;
        grid.on_file_drag([weak] {
            auto node = weak.lock();
            if (!node) return std::vector<std::wstring>{};
            node->file_paths.clear(); node->file_requesting = true;
            const xui_event event{sizeof(xui_event), XUI_REQUEST, node->handle, 0};
            const auto ok = file_callback(node, [&] { return node->file_drag_callback(node->file_drag_context, &event); });
            node->file_requesting = false;
            return ok ? std::move(node->file_paths) : std::vector<std::wstring>{};
        }, [weak](xui::FileTransferEffect effect) {
            if (auto node = weak.lock()) {
                const xui_event event{sizeof(xui_event), XUI_ACTION, node->handle, static_cast<uint64_t>(effect)};
                file_callback(node, [&] { return node->file_drag_callback(node->file_drag_context, &event); });
            }
        });
    });
}
xui_status XUI_CALL xui_grid_file_drop_bind(xui_handle target, xui_file_drop_handler callback, void* context) noexcept {
    return boundary([&] {
        auto node = get(target, XUI_DATA_GRID); editable(node->owner);
        require(!node->file_dispatching, XUI_BUSY, "Cannot replace file callbacks during dispatch.");
        node->file_drop_callback = callback; node->file_drop_context = context;
        auto& grid = as<xui::DataGrid>(node);
        if (!callback) { grid.on_file_drop({}, {}); return; }
        std::weak_ptr<Node> weak = node;
        const auto invoke = [weak](std::optional<xui::RowKey> key, const std::vector<std::wstring>& paths,
            xui::FileTransferEffect requested, bool perform) {
            auto node = weak.lock();
            if (!node) return xui::FileTransferEffect::none;
            uint32_t effect{};
            FileSpans values(paths);
            const auto ok = file_callback(node, [&] {
                const auto status = node->file_drop_callback(node->file_drop_context, key ? key->id : 0, key ? key->version : 0,
                    key.has_value(), values.spans.data(), static_cast<uint32_t>(values.spans.size()),
                    static_cast<uint32_t>(requested), perform, &effect);
                return status ? status : effect > XUI_FILE_MOVE ||
                    (perform && effect && effect != static_cast<uint32_t>(requested)) ? XUI_INVALID_ARGUMENT : XUI_OK;
            });
            return ok ? static_cast<xui::FileTransferEffect>(effect) : xui::FileTransferEffect::none;
        };
        grid.on_file_drop([invoke](auto key, auto requested) { return invoke(key, {}, requested, false); },
            [invoke](auto key, const auto& paths, auto requested) { return invoke(key, paths, requested, true); });
    });
}
