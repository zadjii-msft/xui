namespace password_cleanup_abi {
void run() {
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Terminal password cleanup ABI"), 400, 300};
    xui_handle window{}, root{}, password{};
    ok(xui_window_create(&options, &window));
    ok(xui_stack_create(window, 1, &root));
    password = create(window, XUI_PASSWORD_INPUT);
    ok(xui_stack_add(root, password, 0)); ok(xui_window_content(window, root));
    unsigned changes{};
    ok(xui_subscribe(password, [](void* context, const xui_event* event) -> xui_status {
        if (event->kind == XUI_CHANGE) {
            expect(event->value == 0);
            ++*static_cast<unsigned*>(context);
        }
        return XUI_OK;
    }, &changes));
    auto secret = value(); secret.text = text("first-owned-secret");
    ok(xui_feature_set(password, XUI_F_PASSWORD, &secret));
    const auto before = read_value(password, XUI_F_PASSWORD).first; expect(before == 18);
    expect(xui_password_clear(root) == XUI_WRONG_KIND);
    expect(xui_password_clear(UINT64_MAX) == XUI_INVALID_HANDLE);
    xui_status wrong_thread{};
    std::thread worker([&] { wrong_thread = xui_password_clear(password); }); worker.join();
    expect(wrong_thread == XUI_WRONG_THREAD);
    ok(xui_password_read(password, [](void* context, const char*, uint32_t length) -> xui_status {
        expect(length == 18);
        expect(xui_password_clear(*static_cast<xui_handle*>(context)) == XUI_BUSY);
        return XUI_OK;
    }, &password));
    ok(xui_password_clear(password));
    expect(read_value(password, XUI_F_PASSWORD).first == 0 && changes == 0);
    ok(xui_password_clear(password));
    secret = value(); secret.text = text("live-owned-secret");
    ok(xui_feature_set(password, XUI_F_PASSWORD, &secret));
    struct State { xui_handle window, password; unsigned* changes; bool ran{}; };
    State state{window, password, &changes};
    ok(xui_window_closed(window, [](void* context, const xui_event*) -> xui_status {
        auto& state = *static_cast<State*>(context);
        ok(xui_password_clear(state.password));
        expect(read_value(state.password, XUI_F_PASSWORD).first == 0);
        return XUI_OK;
    }, &state));
    ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
        if (!execute) return XUI_OK;
        auto& state = *static_cast<State*>(context);
        const auto frame = FindWindowW(L"Xui.Window.1", L"Terminal password cleanup ABI");
        expect(frame != nullptr);
        HWND native{};
        EnumChildWindows(frame, [](HWND child, LPARAM data) -> BOOL {
            if (GetWindowLongPtrW(child, GWL_STYLE) & ES_PASSWORD) {
                *reinterpret_cast<HWND*>(data) = child; return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&native));
        expect(native != nullptr && GetWindowTextLengthW(native) == 17);
        ok(xui_focus(state.password, 0));
        SendMessageW(native, EM_SETSEL, 0, -1);
        SendMessageW(native, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"native-owned-secret"));
        expect(*state.changes == 1 && SendMessageW(native, EM_CANUNDO, 0, 0));
        const auto before = *state.changes;
        ok(xui_password_clear(state.password));
        expect(GetWindowTextLengthW(native) == 0 && !SendMessageW(native, EM_CANUNDO, 0, 0));
        expect(read_value(state.password, XUI_F_PASSWORD).first == 0 && *state.changes == before);
        auto secret = value(); secret.text = text("closing-owned-secret");
        ok(xui_feature_set(state.password, XUI_F_PASSWORD, &secret));
        SendMessageW(frame, WM_APP + 12, 0, 0);
        expect(GetWindowTextLengthW(native) == 20);
        ok(xui_window_close(state.window));
        ok(xui_password_clear(state.password));
        expect(read_value(state.password, XUI_F_PASSWORD).first == 0 && *state.changes == before);
        state.ran = true;
        return XUI_OK;
    }, &state));
    ok(xui_window_run(window)); expect(state.ran);
    ok(xui_password_clear(password));
    expect(read_value(password, XUI_F_PASSWORD).first == 0);
    ok(xui_window_destroy(window));
    expect(xui_password_clear(password) == XUI_INVALID_HANDLE);
    expect(GetProcAddress(GetModuleHandleW(L"xui.dll"), "xui_password_clear") != nullptr);
}
}
