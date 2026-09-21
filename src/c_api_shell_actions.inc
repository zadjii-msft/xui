namespace {
constexpr uint32_t shell_actions_kind = 0x10010;
struct ShellActionsSnapshot {
    std::weak_ptr<State> window;
    HWND proxy{};
    std::shared_ptr<xui::AsyncShellMenu> session;
    xui_shell_current current{};
    void* context{};
    bool valid() const {
        const auto owner = window.lock();
        return owner && !owner->closed && current && current(context) != 0;
    }
    static LRESULT CALLBACK validate(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) noexcept {
        auto* self = reinterpret_cast<ShellActionsSnapshot*>(data);
        if (message == xui::shell_validation_message()) {
            try { return self->session && reinterpret_cast<const void*>(w) == self->session->identity() && self->valid(); }
            catch (...) { return 0; }
        }
        return DefSubclassProc(hwnd, message, w, l);
    }
    ~ShellActionsSnapshot() {
        if (session) session->cancel();
        if (proxy) {
            RemoveWindowSubclass(proxy, validate, 1);
            DestroyWindow(proxy);
        }
    }
};
std::shared_ptr<ShellActionsSnapshot> shell_actions(xui_handle handle) {
    return std::static_pointer_cast<ShellActionsSnapshot>(get(handle, shell_actions_kind)->resource);
}
}
xui_status XUI_CALL xui_shell_actions_create(xui_handle window, const xui_string* paths, uint32_t count,
    xui_shell_current current, void* context, xui_handle* result) noexcept {
    return boundary([&] {
        require(result && current && paths && count && count <= 256, XUI_INVALID_ARGUMENT, "Shell actions require 1 to 256 paths and a current-selection callback.");
        *result = 0;
        auto node = get(window, XUI_WINDOW); editable(node->owner);
        std::vector<std::wstring> values;
        for (uint32_t i = 0; i < count; ++i) {
            auto path = decode(paths[i]);
            require(!path.empty() && path.size() <= 32767, XUI_INVALID_ARGUMENT, "Invalid Shell path.");
            values.push_back(std::move(path));
        }
        auto snapshot = std::make_shared<ShellActionsSnapshot>();
        snapshot->window = node->owner; snapshot->current = current; snapshot->context = context;
        snapshot->proxy = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"XUI Shell snapshot",
            WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(snapshot->proxy != nullptr, XUI_NATIVE_ERROR, "Cannot create Shell snapshot owner.");
        require(SetWindowSubclass(snapshot->proxy, ShellActionsSnapshot::validate, 1, reinterpret_cast<DWORD_PTR>(snapshot.get())) != FALSE,
            XUI_NATIVE_ERROR, "Cannot install Shell snapshot validation.");
        snapshot->session = xui::AsyncShellMenu::prepare(snapshot->proxy, std::move(values), true);
        snapshot->session->begin();
        const auto handle = insert(node->owner, shell_actions_kind);
        get(handle)->resource = std::move(snapshot);
        *result = handle;
    });
}
xui_status XUI_CALL xui_shell_actions_read(xui_handle handle, xui_shell_action_receiver receiver, void* context, uint32_t* ready) noexcept {
    return boundary([&] {
        require(ready, XUI_INVALID_ARGUMENT, "Missing Shell snapshot status.");
        const auto snapshot = shell_actions(handle);
        *ready = 0;
        if (!snapshot->session->ready()) return;
        if (snapshot->session->finished()) {
            snapshot->session->result();
            *ready = 2; return;
        }
        const auto failure = snapshot->session->discovery_error();
        if (!failure.empty()) throw std::runtime_error(failure);
        *ready = 1;
        if (!receiver) return;
        for (const auto& item : snapshot->session->commands()) {
            // Dynamic and native-only groups remain in the full Windows menu.
            if (item.separator || item.native_only || !item.children.empty() || item.label.empty()) continue;
            const auto label = encode(item.label), verb = encode(item.verb);
            require(receiver(context, item.key.id, item.key.version,
                {label.data(), static_cast<uint32_t>(label.size()), 0},
                {verb.data(), static_cast<uint32_t>(verb.size()), 0}, item.enabled ? 1u : 0u) == XUI_OK,
                XUI_CALLBACK_FAILED, "Shell snapshot receiver failed.");
        }
    });
}
xui_status XUI_CALL xui_shell_actions_invoke(xui_handle handle, uint64_t id, uint64_t version) noexcept {
    return boundary([&] {
        const auto snapshot = shell_actions(handle);
        require(snapshot->valid(), XUI_INVALID_ARGUMENT, "The context selection is no longer current.");
        snapshot->session->invoke({id, version});
    });
}
xui_status XUI_CALL xui_shell_actions_windows(xui_handle handle) noexcept {
    return boundary([&] {
        const auto snapshot = shell_actions(handle);
        require(snapshot->valid(), XUI_INVALID_ARGUMENT, "The context selection is no longer current.");
        POINT point{}; GetCursorPos(&point);
        snapshot->session->windows_menu({static_cast<float>(point.x), static_cast<float>(point.y)}, {});
    });
}
xui_status XUI_CALL xui_shell_actions_destroy(xui_handle handle) noexcept {
    return boundary([&] {
        auto snapshot = shell_actions(handle);
        const auto node = get(handle, shell_actions_kind);
        std::erase(node->owner->handles, handle);
        { std::lock_guard lock(registry_mutex); registry.erase(handle); }
    });
}
