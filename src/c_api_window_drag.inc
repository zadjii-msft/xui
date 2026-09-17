static_assert(static_cast<uint32_t>(xui::TabDragKind::join) == XUI_TAB_DRAG_JOIN);
static_assert(static_cast<uint32_t>(xui::TabDragKind::leave) == XUI_TAB_DRAG_LEAVE);

xui_status XUI_CALL xui_window_get_placement(xui_handle window, xui_window_placement* placement) noexcept {
    return boundary([&] {
        require(placement != nullptr, XUI_INVALID_ARGUMENT, "Missing placement output.");
        require(placement->size == sizeof(*placement), XUI_VERSION_MISMATCH, "Window placement size mismatch.");
        const auto value = get(window, XUI_WINDOW)->owner->window->placement();
        *placement = {sizeof(*placement), value.x, value.y, value.width, value.height, value.maximized ? 1u : 0u};
    });
}

xui_status XUI_CALL xui_window_set_placement(xui_handle window, const xui_window_placement* placement) noexcept {
    return boundary([&] {
        require(placement != nullptr, XUI_INVALID_ARGUMENT, "Missing placement.");
        require(placement->size == sizeof(*placement), XUI_VERSION_MISMATCH, "Window placement size mismatch.");
        require(placement->width > 0 && placement->width <= 65536 &&
            placement->height > 0 && placement->height <= 65536 && placement->maximized <= 1 &&
            placement->x >= -1000000 && placement->x <= 1000000 &&
            placement->y >= -1000000 && placement->y <= 1000000,
            XUI_INVALID_ARGUMENT, "Invalid window placement.");
        const auto state = get(window, XUI_WINDOW)->owner;
        editable(state);
        state->window->set_placement({placement->x, placement->y, placement->width, placement->height,
            placement->maximized != 0});
    }, true);
}

xui_status XUI_CALL xui_window_tab_drag_handler(xui_handle window,
    xui_tab_drag_handler callback, void* context) noexcept {
    return boundary([&] {
        const auto node = get(window, XUI_WINDOW);
        const auto state = node->owner;
        editable(state);
        require(!state->content_context && !state->building_content, XUI_BUSY,
            "Content scopes cannot replace window callbacks.");
        if (!callback) { state->window->on_tab_drag({}); return; }
        require(state->application && state->window->titlebar(), XUI_INVALID_ARGUMENT,
            "Tab dragging requires an application window with a custom title bar.");
        state->window->on_tab_drag([weak = std::weak_ptr<Node>(node), callback, context](const xui::TabDragEvent& value) {
            const auto source = weak.lock();
            if (!source || source->owner->closed || source->owner->callback_failure ||
                source->owner->window->state() >= xui::WindowState::closing) return false;
            const auto owner = source->owner;
            auto fail = [&](xui_status status) {
                owner->callback_failure = status;
                owner->window->close();
                return false;
            };
            if (owner->tab_drag_dispatching) return fail(XUI_BUSY);
            xui_handle target{};
            if (value.target) {
                std::lock_guard lock(registry_mutex);
                for (const auto& [handle, candidate] : registry) {
                    if (candidate->kind == XUI_WINDOW && candidate->owner->application == owner->application &&
                        candidate->owner->window.get() == value.target && !candidate->owner->closed &&
                        candidate->owner->window->state() < xui::WindowState::closing) {
                        target = handle;
                        break;
                    }
                }
            }
            if (value.target && !target) return fail(XUI_INVALID_HANDLE);
            if (static_cast<uint32_t>(value.kind) > XUI_TAB_DRAG_LEAVE ||
                value.source_strip > 1 || value.target_strip > 1 || !value.tab_id)
                return fail(XUI_INVALID_ARGUMENT);
            const xui_tab_drag_event event{sizeof(xui_tab_drag_event), static_cast<uint32_t>(value.kind),
                value.source_strip, value.target_strip, value.tab_id, target, static_cast<uint64_t>(value.index)};
            uint32_t accepted{};
            ++owner->callbacks;
            owner->tab_drag_dispatching = true;
            xui_status status{};
            try { status = callback(context, &event, &accepted); }
            catch (...) { status = XUI_CALLBACK_FAILED; }
            owner->tab_drag_dispatching = false;
            --owner->callbacks;
            if (status || accepted > 1) return fail(status ? status : XUI_INVALID_ARGUMENT);
            return accepted != 0 && !owner->closed && !owner->callback_failure &&
                owner->window->state() < xui::WindowState::closing;
        });
    }, true);
}
