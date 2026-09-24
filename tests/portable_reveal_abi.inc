#include "xui/xui_reveal_portable.h"
namespace portable_reveal_abi {
xui_portable_reveal_presentation presentation(xui_handle reveal) {
    xui_portable_reveal_presentation value{sizeof(value), XUI_PORTABLE_REVEAL_VERSION};
    ok(xui_portable_reveal_get_presentation(reveal, &value)); return value;
}
void run() {
    static_assert(sizeof(xui_portable_reveal_state) == 24 && sizeof(xui_portable_reveal_presentation) == 16);
    expect(xui_portable_reveal_version() == XUI_PORTABLE_REVEAL_VERSION);
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Portable Reveal ABI"), 600, 480};
    xui_handle window{}, outer{}, host{}, scope{}, root{}, content{}, reveal{}, input{}, outside{};
    ok(xui_window_create(&options, &window)); ok(xui_stack_create(window, 1, &outer));
    ok(xui_content_host_create(window, &host)); ok(xui_stack_add(outer, host, 1)); ok(xui_window_content(window, outer));
    ok(xui_content_begin(host, &scope)); ok(xui_stack_create(window, 1, &root));
    ok(xui_stack_create(window, 1, &content)); ok(xui_reveal_create(window, content, text("Portable content"), &reveal));
    xui_portable_reveal_state state{sizeof(state), XUI_PORTABLE_REVEAL_VERSION, 1, 0, 0, 1};
    ok(xui_portable_reveal_validate_state(reveal, &state)); ok(xui_portable_reveal_apply_state(reveal, &state));
    expect(presentation(reveal).progress == 1 && !presentation(reveal).animating);
    expect(xui_portable_reveal_apply_state(reveal, &state) == XUI_INVALID_ARGUMENT);
    state.initial = 0;
    for (unsigned i = 0; i < 4; ++i) {
        auto invalid = state;
        if (i == 0) invalid.open = 2;
        if (i == 1) invalid.duration_ms = 401;
        if (i == 2) invalid.direction = 2;
        if (i == 3) invalid.initial = 2;
        expect(xui_portable_reveal_apply_state(reveal, &invalid) == XUI_INVALID_ARGUMENT);
        expect(presentation(reveal).progress == 1);
    }
    auto wrong_version = state; wrong_version.version = 0;
    expect(xui_portable_reveal_validate_state(reveal, &wrong_version) == XUI_VERSION_MISMATCH);
    expect(xui_portable_reveal_validate_state(content, &state) == XUI_WRONG_KIND);
    expect(xui_reveal_set_open(reveal, 0) == XUI_INVALID_ARGUMENT);
    expect(xui_reveal_set_duration(reveal, 200) == XUI_INVALID_ARGUMENT);
    expect(xui_reveal_set_direction(reveal, 3) == XUI_INVALID_ARGUMENT);
    expect(xui_reveal_set_layout(reveal, 1) == XUI_INVALID_ARGUMENT);
    xui_property size{sizeof(size), XUI_FIXED_SIZE, reveal, {}, 100, 100};
    expect(xui_update(window, &size, 1) == XUI_INVALID_ARGUMENT);
    expect(xui_stack_add(root, reveal, 1) == XUI_INVALID_ARGUMENT);
    ok(xui_stack_add(root, reveal, 0));
    const auto unsupported = create(window, XUI_MULTILINE_TEXT);
    expect(xui_panel_add(window, unsupported, 0, 0, 1, 1) == XUI_BUSY);
    xui_control_style_options definition{sizeof(definition), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_TARGET_LABEL};
    xui_handle style{}; ok(xui_control_style_create(window, &definition, &style));
    expect(xui_panel_add(style, unsupported, 0, 0, 1, 1) == XUI_WRONG_KIND);
    expect(xui_stack_add(content, unsupported, 0) == XUI_INVALID_ARGUMENT);
    const auto password = create(window, XUI_PASSWORD_INPUT);
    ok(xui_stack_add(content, password, 0));
    ok(xui_create(window, XUI_TEXT_INPUT, text("Reveal editor"), 0, &input));
    ok(xui_stack_add(content, input, 0));
    ok(xui_create(window, XUI_BUTTON, text("Outside reveal"), 0, &outside)); ok(xui_stack_add(root, outside, 0));
    ok(xui_content_commit(scope, root));
    uint32_t allowed{};
    expect(xui_portable_reveal_can_set_open(reveal, 2, &allowed) == XUI_INVALID_ARGUMENT);
    expect(xui_portable_reveal_can_set_open(reveal, 0, nullptr) == XUI_INVALID_ARGUMENT);
    std::thread wrong([&] { uint32_t allowed{}; expect(xui_portable_reveal_can_set_open(reveal, 0, &allowed) == XUI_WRONG_THREAD); });
    wrong.join();
    struct State { xui_handle window, scope, reveal, input, outside, content; xui_portable_reveal_state value; bool ran{}; };
    State current{window, scope, reveal, input, outside, content, state};
    ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
        if (!execute) return XUI_OK;
        auto& s = *static_cast<State*>(context);
        ok(xui_focus(s.input, 0));
        const auto edit = GetFocus();
        uint32_t allowed{}; ok(xui_portable_reveal_can_set_open(s.reveal, 0, &allowed)); expect(!allowed);
        s.value.open = 0;
        expect(xui_portable_reveal_apply_state(s.reveal, &s.value) == XUI_BUSY);
        expect(presentation(s.reveal).progress == 1 && GetFocus() == edit);
        SendMessageW(edit, WM_IME_STARTCOMPOSITION, 0, 0);
        ok(xui_portable_reveal_can_set_open(s.reveal, 0, &allowed)); expect(!allowed);
        s.value.open = 1; s.value.duration_ms = 200; s.value.direction = 1;
        ok(xui_portable_reveal_apply_state(s.reveal, &s.value));
        expect(GetFocus() == edit && presentation(s.reveal).progress == 1 && !presentation(s.reveal).animating);
        SendMessageW(edit, WM_IME_ENDCOMPOSITION, 0, 0);
        ok(xui_focus(s.outside, 0));
        ok(xui_portable_reveal_can_set_open(s.reveal, 0, &allowed)); expect(allowed == 1);
        s.value.open = 0; s.value.duration_ms = 0;
        ok(xui_portable_reveal_apply_state(s.reveal, &s.value));
        expect(presentation(s.reveal).progress == 0 && !IsWindowEnabled(edit));
        ok(xui_content_begin_append(s.scope));
        const auto rich = create(s.window, XUI_RICH_TEXT);
        ok(xui_content_end_append(s.scope, 1));
        expect(xui_content_stack_insert(s.content, 2, rich, 0) == XUI_INVALID_ARGUMENT);
        ok(xui_content_release_element(s.scope, rich));
        s.value.open = 1; s.value.duration_ms = 400;
        ok(xui_portable_reveal_apply_state(s.reveal, &s.value));
        ok(xui_portable_reveal_cancel(s.reveal)); ok(xui_portable_reveal_cancel(s.reveal));
        expect(presentation(s.reveal).progress == 1 && !presentation(s.reveal).animating);
        expect(xui_portable_reveal_apply_state(s.reveal, &s.value) == XUI_CLOSED);
        s.ran = true; return xui_window_close(s.window);
    }, &current));
    ok(xui_window_run(window)); expect(current.ran);
    ok(xui_portable_reveal_cancel(reveal));
    ok(xui_content_release(scope)); uint32_t count{}; ok(xui_content_handle_count(window, &count)); expect(count == 3);
    ok(xui_window_destroy(window));
}
}
