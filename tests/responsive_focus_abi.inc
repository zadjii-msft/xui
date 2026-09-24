namespace responsive_focus_abi {
struct Fixture {
    xui_handle window{}, outer{}, host{}, scope{}, grid{}, pages{}, page{}, title{}, body{}, outside{}, observer{};
    HWND frame{}, edit{};
    HHOOK hook{};
    bool started{}, queued{}, finished{};
    bool multiline{};
    bool composition{};
    bool move_focus_before_compact{};
    HWND expected_focus{};
    unsigned expected_losses{};
    DWORD selection_first{}, selection_last{};
    std::wstring native_text;
    unsigned focus_losses{};
    bool visible_at_loss{}, enabled_at_loss{};
    inline static thread_local Fixture* observing{};
    static LRESULT CALLBACK focus_hook(int code, WPARAM target, LPARAM previous) {
        if (code == HCBT_SETFOCUS && observing && reinterpret_cast<HWND>(previous) == observing->edit &&
            reinterpret_cast<HWND>(target) != observing->edit) {
            ++observing->focus_losses;
            observing->visible_at_loss = IsWindowVisible(observing->edit) != FALSE;
            observing->enabled_at_loss = IsWindowEnabled(observing->edit) != FALSE;
        }
        return CallNextHookEx(nullptr, code, target, previous);
    }
    static xui_status XUI_CALL changed(void* context, const xui_content_viewport* viewport) {
        auto& self = *static_cast<Fixture*>(context);
        if (self.started && viewport->width <= 320.1f && !self.queued) {
            self.queued = true;
            ok(xui_window_post(self.window, [](void* context, uint32_t execute) -> xui_status {
                if (execute) static_cast<Fixture*>(context)->compact();
                return XUI_OK;
            }, context));
        }
        return XUI_OK;
    }
    void tracks(bool compact) {
        const auto row = grid_layout_abi::track(2);
        const xui_grid_track columns[]{grid_layout_abi::track(0, compact ? 68.0f : 184.0f),
            grid_layout_abi::track(2), grid_layout_abi::track(0, compact ? 0.0f : 240.0f),
            grid_layout_abi::track(0, compact ? 0.0f : 224.0f)};
        ok(xui_grid_tracks(grid, &row, 1, columns, 4));
    }
    void start() {
        frame = FindWindowW(L"Xui.Window.1", L"Responsive page focus ABI"); expect(frame != nullptr);
        edit = retained_pages_abi::Fixture::named(frame,
            multiline ? L"Responsive body remains unchanged" : L"Responsive title draft");
        const auto input = multiline ? body : title;
        ok(xui_focus(input, 0)); SendMessageW(edit, EM_SETSEL, 2, 8);
        if (composition) SendMessageW(edit, WM_IME_STARTCOMPOSITION, 0, 0);
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&selection_first), reinterpret_cast<LPARAM>(&selection_last));
        wchar_t text[128]{}; GetWindowTextW(edit, text, 128); native_text = text;
        observing = this;
        hook = SetWindowsHookExW(WH_CBT, focus_hook, nullptr, GetCurrentThreadId()); expect(hook != nullptr);
        RECT bounds{}, client{}; GetWindowRect(frame, &bounds); GetClientRect(frame, &client);
        const auto width = bounds.right - bounds.left - client.right + MulDiv(320, GetDpiForWindow(frame), 96);
        started = true;
        expect(SetWindowPos(frame, nullptr, bounds.left, bounds.top, width, bounds.bottom - bounds.top,
            SWP_NOZORDER | SWP_NOACTIVATE) != FALSE);
        SendMessageW(frame, WM_APP + 12, 0, 0);
        RECT actual{}; GetWindowRect(edit, &actual);
        wchar_t name[128]{}, type[64]{};
        GetWindowTextW(GetFocus(), name, 128); GetClassNameW(GetFocus(), type, 64);
        std::wcout << L"Responsive zero slot multiline=" << multiline << L" composing=" << composition
            << L": pageWidth=" << grid_layout_abi::bounds(pages).width
            << L" editWidth=" << actual.right - actual.left << L" retainedFocus=" << (GetFocus() == edit)
            << L" focusLosses=" << focus_losses << L" visibleAtLoss=" << visible_at_loss
            << L" enabledAtLoss=" << enabled_at_loss << L" currentFocus=" << type << L":" << name << L'\n';
        expect(grid_layout_abi::bounds(pages).width == 0 && actual.right == actual.left);
        expect(GetFocus() == edit && focus_losses == 0 && IsWindowEnabled(edit) && IsWindowVisible(edit));
        RECT clip{}; GetClientRect(GetParent(edit), &clip); expect(clip.right == 0);
        expect(xui_focus(multiline ? title : body, 0) != XUI_OK && GetFocus() == edit);
        xui_control_interaction interaction{sizeof(interaction), XUI_CONTROL_INTERACTION_VERSION};
        ok(xui_control_interaction_get(input, &interaction)); expect(interaction.has_focus && interaction.is_composing == uint32_t(composition));
        expect(!queued);
        expected_focus = edit;
        if (move_focus_before_compact) {
            ok(xui_focus(outside, 0)); expected_focus = GetFocus();
            expect(expected_focus != edit);
        }
        expected_losses = focus_losses;
    }
    void compact() {
        const auto input = multiline ? body : title;
        expect(GetFocus() == expected_focus);
        tracks(true); SendMessageW(frame, WM_APP + 12, 0, 0);
        const auto bounds = grid_layout_abi::bounds(pages);
        DWORD first{}, last{};
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
        wchar_t text[128]{}; GetWindowTextW(edit, text, 128);
        expect(bounds.width > 200 && GetFocus() == expected_focus && first == selection_first && last == selection_last &&
            native_text == text && focus_losses == expected_losses);
        xui_control_interaction interaction{sizeof(interaction), XUI_CONTROL_INTERACTION_VERSION};
        ok(xui_control_interaction_get(input, &interaction));
        expect(interaction.has_focus == uint32_t(expected_focus == edit) && interaction.is_composing == uint32_t(composition));
        if (composition) SendMessageW(edit, WM_IME_ENDCOMPOSITION, 0, 0);
        if (move_focus_before_compact) ok(xui_focus(input, 0));
        expect(xui_retained_pages_set_visible(pages, 0) == XUI_BUSY);
        ok(xui_focus(outside, 0));
        ok(xui_retained_pages_set_visible(pages, 0)); SendMessageW(frame, WM_APP + 12, 0, 0);
        expect(!IsWindowVisible(edit) && !IsWindowEnabled(edit));
        expect(grid_layout_abi::bounds(pages).width == 0 && grid_layout_abi::bounds(pages).height == 0);
        ok(xui_retained_pages_set_visible(pages, 1)); SendMessageW(frame, WM_APP + 12, 0, 0);
        ok(xui_focus(input, 0));
        xui_property disable{sizeof(disable), XUI_ENABLED, input, {}, 0, 0, 0, 0, 0};
        ok(xui_update(window, &disable, 1)); SendMessageW(frame, WM_APP + 12, 0, 0);
        expect(GetFocus() != edit && !IsWindowEnabled(edit));
        expect(UnhookWindowsHookEx(hook) != FALSE); hook = nullptr; observing = nullptr;
        ok(xui_content_viewport_release(observer));
        finished = true; ok(xui_window_close(window));
    }
    void run() {
        xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Responsive page focus ABI"), 1100, 500};
        ok(xui_window_create(&options, &window)); ok(xui_window_visual_style_set(window, XUI_STYLE_WINUI));
        ok(xui_stack_create(window, 1, &outer)); ok(xui_content_host_create(window, &host));
        ok(xui_stack_add(outer, host, 1)); ok(xui_window_content(window, outer)); ok(xui_content_begin(host, &scope));
        grid = create(window, XUI_GRID);
        xui_property size{sizeof(size), XUI_FIXED_SIZE, grid, {}, 1100, 300}; ok(xui_update(window, &size, 1));
        tracks(false);
        ok(xui_retained_pages_create(window, text("Document pages"), &pages));
        const xui_page_entry entry{sizeof(entry), 1, 11, text("Document")};
        ok(xui_retained_pages_set(pages, &entry, 1, 11));
        ok(xui_stack_create(window, 1, &page));
        ok(xui_create(window, XUI_TEXT_INPUT, text("Title"), 0, &title));
        body = create(window, XUI_MULTILINE_TEXT);
        xui_property text_value{sizeof(text_value), XUI_TEXT, title, text("Responsive title draft")};
        ok(xui_update(window, &text_value, 1));
        auto body_text = value(); body_text.text = text("Responsive body remains unchanged");
        ok(xui_feature_set(body, XUI_F_DOCUMENT_TEXT, &body_text));
        ok(xui_stack_add(page, title, 0)); ok(xui_stack_add(page, body, 0));
        ok(xui_retained_pages_insert(pages, 0, 11, page));
        ok(xui_create(window, XUI_BUTTON, text("Outside page"), 0, &outside));
        ok(xui_panel_add(grid, outside, 0, 0, 1, 1)); ok(xui_panel_add(grid, pages, 0, 1, 1, 1));
        ok(xui_content_commit(scope, grid));
        xui_content_viewport initial{sizeof(initial), XUI_CONTENT_VIEWPORT_VERSION};
        ok(xui_content_viewport_subscribe(host, changed, this, &initial, &observer));
        ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
            if (execute) static_cast<Fixture*>(context)->start();
            return XUI_OK;
        }, this));
        ok(xui_window_run(window)); expect(finished); ok(xui_window_destroy(window));
    }
};
void run() {
    for (bool composition : {false, true}) for (bool multiline : {false, true}) {
        Fixture fixture; fixture.multiline = multiline; fixture.composition = composition; fixture.run();
    }
    Fixture moved; moved.move_focus_before_compact = true; moved.run();
}
}
