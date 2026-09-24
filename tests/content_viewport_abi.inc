namespace content_viewport_abi {
struct Fixture {
    xui_handle window{}, root{}, parent{}, host{}, scope{}, content{}, first{}, second{}, pending{}, closing{};
    xui_content_viewport latest{sizeof(latest), XUI_CONTENT_VIEWPORT_VERSION};
    unsigned calls{}, second_calls{}, pending_calls{}, phase{};
    HWND frame{};
    RECT outer{};
    static xui_status XUI_CALL changed(void* context, const xui_content_viewport* value) {
        auto& self = *static_cast<Fixture*>(context);
        expect(value && value->size == sizeof(*value) && value->version == XUI_CONTENT_VIEWPORT_VERSION &&
            std::isfinite(value->width) && value->width >= 0 && std::isfinite(value->height) && value->height >= 0);
        ++self.calls; self.latest = *value;
        xui_property size{sizeof(size), XUI_FIXED_SIZE, self.parent, {}, 50, 50};
        expect(xui_update(self.window, &size, 1) == XUI_BUSY);
        expect(xui_content_viewport_release(self.first) == XUI_BUSY);
        ok(xui_window_post(self.window, process, context));
        return XUI_OK;
    }
    static xui_status XUI_CALL secondary(void* context, const xui_content_viewport*) {
        ++static_cast<Fixture*>(context)->second_calls; return XUI_OK;
    }
    xui_handle subscribe(xui_content_viewport_callback callback, xui_content_viewport& initial) {
        xui_handle result{};
        ok(xui_content_viewport_subscribe(host, callback, this, &initial, &result));
        expect(result != 0); return result;
    }
    void fixed(xui_handle target, float width, float height) {
        xui_property size{sizeof(size), XUI_FIXED_SIZE, target, {}, width, height};
        ok(xui_update(window, &size, 1));
    }
    void padding(float inset) {
        xui_style_property value{sizeof(value), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_PADDING,
            XUI_STYLE_INSETS, XUI_STYLE_ROOT};
        value.insets = {inset, inset + 10, inset + 20, inset + 30};
        ok(xui_control_set_style_values(host, XUI_STYLE_ROOT, &value, 1));
    }
    void flush() { SendMessageW(frame, WM_APP + 12, 0, 0); }
    static xui_status XUI_CALL process(void* context, uint32_t execute) {
        if (!execute) return XUI_OK;
        auto& self = *static_cast<Fixture*>(context);
        self.advance(); return XUI_OK;
    }
    void advance() {
        if (!frame) { frame = FindWindowW(L"Xui.Window.1", L"Content viewport ABI"); expect(frame != nullptr); GetWindowRect(frame, &outer); }
        RECT current{}; GetWindowRect(frame, &current);
        expect(EqualRect(&outer, &current) != FALSE);
        if (phase == 0) {
            expect(calls == 1 && second_calls == 1 && latest.width == 400 && latest.height == 220);
            xui_content_viewport initial{sizeof(initial), XUI_CONTENT_VIEWPORT_VERSION};
            const auto temporary = subscribe(secondary, initial);
            expect(initial.width == 400 && initial.height == 220 && second_calls == 1);
            ok(xui_content_viewport_release(temporary));
            padding(10);
            pending = subscribe([](void* context, const xui_content_viewport* value) -> xui_status {
                auto& self = *static_cast<Fixture*>(context);
                expect(value->width == 358 && value->height == 158);
                ++self.pending_calls; return XUI_OK;
            }, initial);
            expect(initial.width == 400 && initial.height == 220 && second_calls == 1);
            expect(pending_calls == 0);
            flush(); padding(11); flush();
            expect(calls == 1 && second_calls == 1);
            ok(xui_content_viewport_release(second));
            expect(xui_content_viewport_release(second) == XUI_INVALID_HANDLE);
            phase = 1;
        } else if (phase == 1) {
            expect(calls == 2 && second_calls == 1 && pending_calls == 1 && latest.width == 358 && latest.height == 158);
            ok(xui_content_viewport_release(pending));
            const auto old_scope = scope;
            ok(xui_content_begin(host, &scope)); ok(xui_stack_create(window, 1, &content));
            ok(xui_content_commit(scope, content)); flush();
            expect(xui_content_release(old_scope) == XUI_INVALID_HANDLE);
            expect(calls == 2 && second_calls == 1);
            fixed(parent, 300, 300); flush(); expect(calls == 2);
            phase = 2;
        } else if (phase == 2) {
            expect(calls == 3 && second_calls == 1 && latest.width == 258 && latest.height == 158);
            fixed(parent, 0, 0); flush(); expect(calls == 3);
            phase = 3;
        } else {
            expect(phase == 3 && calls == 4 && second_calls == 1 && latest.width == 0 && latest.height == 0);
            ok(xui_content_viewport_release(first));
            ok(xui_content_release(scope));
            uint32_t count{}; ok(xui_content_handle_count(window, &count)); expect(count == 4);
            xui_content_viewport initial{sizeof(initial), XUI_CONTENT_VIEWPORT_VERSION};
            closing = subscribe(secondary, initial);
            fixed(parent, 500, 300); flush();
            expect(second_calls == 1);
            phase = 4; ok(xui_window_close(window));
        }
    }
    void run() {
        xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Content viewport ABI"), 640, 480};
        ok(xui_window_create(&options, &window));
        ok(xui_stack_create(window, 1, &root)); ok(xui_stack_create(window, 1, &parent));
        ok(xui_content_host_create(window, &host));
        ok(xui_stack_add(root, parent, 0)); ok(xui_stack_add(parent, host, 0)); ok(xui_window_content(window, root));
        fixed(parent, 500, 300); fixed(host, 400, 220);
        ok(xui_content_begin(host, &scope)); ok(xui_stack_create(window, 1, &content));
        ok(xui_content_commit(scope, content));
        xui_content_viewport initial{sizeof(initial), XUI_CONTENT_VIEWPORT_VERSION};
        first = subscribe(changed, initial); expect(initial.width == 0 && initial.height == 0 && calls == 0);
        second = subscribe(secondary, initial); expect(initial.width == 0 && initial.height == 0 && second_calls == 0);
        xui_handle invalid = 99;
        auto bad = initial; bad.version = 0;
        expect(xui_content_viewport_subscribe(host, changed, this, &bad, &invalid) == XUI_VERSION_MISMATCH && invalid == 0);
        expect(xui_content_viewport_subscribe(host, nullptr, this, &initial, &invalid) == XUI_INVALID_ARGUMENT);
        expect(xui_content_viewport_subscribe(root, changed, this, &initial, &invalid) == XUI_WRONG_KIND);
        expect(xui_content_viewport_release(host) == XUI_WRONG_KIND);
        std::thread wrong([&] {
            auto value = initial; xui_handle invalid{};
            expect(xui_content_viewport_subscribe(host, changed, this, &value, &invalid) == XUI_WRONG_THREAD);
            expect(xui_content_viewport_release(first) == XUI_WRONG_THREAD);
        }); wrong.join();
        ok(xui_window_run(window));
        expect(phase == 4 && calls == 4 && second_calls == 1);
        ok(xui_content_viewport_release(closing));
        expect(xui_content_viewport_subscribe(host, changed, this, &initial, &invalid) == XUI_CLOSED);
        ok(xui_window_destroy(window));
        expect(xui_content_viewport_release(first) == XUI_INVALID_HANDLE);
    }
};
void run() {
    static_assert(sizeof(xui_content_viewport) == 16);
    expect(xui_content_viewport_version() == XUI_CONTENT_VIEWPORT_VERSION);
    for (const auto name : {"xui_content_viewport_version", "xui_content_viewport_subscribe", "xui_content_viewport_release"})
        expect(GetProcAddress(GetModuleHandleW(L"xui.dll"), name) != nullptr);
    Fixture fixture; fixture.run();
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Content viewport callback failure"), 320, 240};
    xui_handle window{}, root{}, host{}, subscription{};
    ok(xui_window_create(&options, &window)); ok(xui_stack_create(window, 1, &root));
    ok(xui_content_host_create(window, &host)); ok(xui_stack_add(root, host, 1)); ok(xui_window_content(window, root));
    xui_content_viewport initial{sizeof(initial), XUI_CONTENT_VIEWPORT_VERSION};
    unsigned errors{};
    ok(xui_content_viewport_subscribe(host, [](void* context, const xui_content_viewport*) -> xui_status {
        ++*static_cast<unsigned*>(context); return 91;
    }, &errors, &initial, &subscription));
    expect(errors == 0);
    expect(xui_window_run(window) == XUI_CALLBACK_FAILED && errors == 1);
    xui_status error{}; ok(xui_window_callback_error(window, &error)); expect(error == 91);
    ok(xui_content_viewport_release(subscription)); ok(xui_window_destroy(window));
    ok(xui_window_create(&options, &window)); ok(xui_stack_create(window, 1, &root));
    ok(xui_content_host_create(window, &host));
    expect(xui_content_viewport_subscribe(host, Fixture::secondary, &fixture, &initial, &subscription) == XUI_INVALID_ARGUMENT);
    expect(subscription == 0);
    ok(xui_stack_add(root, host, 1)); ok(xui_window_content(window, root));
    ok(xui_content_viewport_subscribe(host, Fixture::secondary, &fixture, &initial, &subscription));
    ok(xui_window_destroy(window));
    expect(xui_content_viewport_release(subscription) == XUI_INVALID_HANDLE);
}
}
