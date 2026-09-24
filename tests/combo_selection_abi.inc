namespace combo_selection_abi {
void selected(xui_handle combo, uint64_t expected, uint32_t has) {
    uint64_t actual = UINT64_MAX; uint32_t present = 99;
    ok(xui_combo_box_get_selected(combo, &actual, &present));
    expect(actual == expected && present == has);
}
void run() {
    expect(xui_combo_box_selection_version() == XUI_COMBO_SELECTION_VERSION);
    for (const auto symbol : {"xui_combo_box_selection_version", "xui_combo_box_get_selected"})
        expect(GetProcAddress(GetModuleHandleW(L"xui.dll"), symbol) != nullptr);
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Native ComboBox selection"), 500, 320};
    xui_handle window{}, root{}; ok(xui_window_create(&options, &window)); ok(xui_stack_create(window, 1, &root));
    const auto combo = create(window, XUI_COMBO_BOX);
    selected(combo, 0, 0);
    constexpr uint64_t large = (1ull << 40) + 7;
    xui_choice items[]{{sizeof(xui_choice), 0, large, 0, text("Large native identity")},
        {sizeof(xui_choice), 0, 9, 0, text("Second native choice")},
        {sizeof(xui_choice), 1, 17, 0, text("Disabled native choice")}};
    ok(xui_choices(combo, items, 3, large, 1)); selected(combo, large, 1);
    ok(xui_choices(combo, items, 3, 0, 0)); selected(combo, large, 1);
    uint64_t id{}; uint32_t has{};
    expect(xui_combo_box_get_selected(combo, nullptr, &has) == XUI_INVALID_ARGUMENT);
    expect(xui_combo_box_get_selected(combo, &id, nullptr) == XUI_INVALID_ARGUMENT);
    expect(xui_combo_box_get_selected(root, &id, &has) == XUI_WRONG_KIND);
    expect(xui_choices(combo, items, 3, 99, 1) == XUI_INVALID_ARGUMENT);
    expect(xui_choices(combo, items, 3, 17, 1) != XUI_OK); selected(combo, large, 1);
    std::thread wrong([&] { uint64_t id{}; uint32_t has{}; expect(xui_combo_box_get_selected(combo, &id, &has) == XUI_WRONG_THREAD); });
    wrong.join();
    struct State { xui_handle window, combo; unsigned changes{}; bool ran{}; } state{window, combo};
    ok(xui_subscribe(combo, [](void* context, const xui_event* event) -> xui_status {
        auto& s = *static_cast<State*>(context);
        if (event->kind == XUI_SELECTION) { ++s.changes; selected(s.combo, event->value, 1); }
        return XUI_OK;
    }, &state));
    ok(xui_stack_add(root, combo, 0)); ok(xui_window_content(window, root));
    ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
        if (!execute) return XUI_OK;
        auto& s = *static_cast<State*>(context);
        constexpr uint64_t large = (1ull << 40) + 7;
        xui_choice items[]{{sizeof(xui_choice), 0, large, 0, text("Large native identity")},
            {sizeof(xui_choice), 0, 9, 0, text("Second native choice")}};
        ok(xui_choices(s.combo, items, 2, large, 1)); selected(s.combo, large, 1); expect(s.changes == 0);
        ok(xui_feature_action(s.combo, XUI_A_SELECT, 9, 0)); selected(s.combo, 9, 1); expect(s.changes == 1);
        ok(xui_choices(s.combo, items, 2, large, 1)); selected(s.combo, large, 1); expect(s.changes == 1);
        ok(xui_choices(s.combo, nullptr, 0, 0, 0)); selected(s.combo, 0, 0); expect(s.changes == 1);
        auto disabled = items[1]; disabled.flags = 1;
        ok(xui_choices(s.combo, &disabled, 1, 0, 0)); selected(s.combo, 0, 0);
        s.ran = true; return xui_window_close(s.window);
    }, &state));
    ok(xui_window_run(window)); expect(state.ran); selected(combo, 0, 0);
    ok(xui_window_destroy(window)); expect(xui_combo_box_get_selected(combo, &id, &has) == XUI_INVALID_HANDLE);
}
}
