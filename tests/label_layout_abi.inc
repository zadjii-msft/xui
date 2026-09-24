#include "xui/xui_label_layout.h"
namespace label_layout_abi {
xui_label_layout read(xui_handle label) {
    xui_label_layout result{sizeof(result), XUI_LABEL_LAYOUT_VERSION};
    ok(xui_label_layout_get(label, &result)); return result;
}
void run() {
    static_assert(sizeof(xui_label_layout) == 24);
    expect(xui_label_layout_version() == XUI_LABEL_LAYOUT_VERSION);
    for (const auto name : {"xui_label_layout_version", "xui_label_layout_get", "xui_label_layout_set"})
        expect(GetProcAddress(GetModuleHandleW(L"xui.dll"), name) != nullptr);
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Label layout ABI"), 600, 450};
    xui_handle window{}, root{}, label{}, input{}, button{};
    ok(xui_window_create(&options, &window)); ok(xui_stack_create(window, 1, &root));
    ok(xui_create(window, XUI_LABEL, text("first line\nsecond line\nthird line"), 0, &label));
    ok(xui_create(window, XUI_TEXT_INPUT, text("Retained nearby editor"), 0, &input));
    ok(xui_create(window, XUI_BUTTON, text("Untouched button"), 0, &button));
    xui_property contents{sizeof(contents), XUI_TEXT, input, text("label layout neighbor")};
    ok(xui_update(window, &contents, 1));
    for (auto child : {label, input, button}) ok(xui_stack_add(root, child, 0));
    ok(xui_window_content(window, root));
    expect(read(label).mode == 0);
    xui_label_layout wrap{sizeof(wrap), XUI_LABEL_LAYOUT_VERSION, 2, 0, 2};
    xui_label_layout single{sizeof(single), XUI_LABEL_LAYOUT_VERSION, 1};
    expect(xui_label_layout_set(label, &single) == XUI_INVALID_ARGUMENT && read(label).mode == 0);
    ok(xui_label_layout_set(label, &wrap));
    expect(read(label).mode == 2 && read(label).maximum_lines == 2);
    for (unsigned i = 0; i < 6; ++i) {
        auto invalid = wrap;
        if (i == 0) invalid.mode = 3;
        if (i == 1) invalid.overflow = 1;
        if (i == 2) invalid.maximum_lines = 33;
        if (i == 3) invalid.reserved = 1;
        if (i == 4) invalid.mode = 0;
        if (i == 5) invalid.mode = 1;
        expect(xui_label_layout_set(label, &invalid) == XUI_INVALID_ARGUMENT);
        expect(read(label).maximum_lines == 2);
    }
    expect(xui_label_layout_set(input, &single) == XUI_WRONG_KIND);
    const auto text_value = [&](const char* value) {
        xui_property property{sizeof(property), XUI_TEXT, label, text(value)};
        ok(xui_update(window, &property, 1));
    };
    text_value("literal  spaces\tand long text"); ok(xui_label_layout_set(label, &single));
    for (const auto hardbreak : {"a\rb", "a\nb", "a\xc2\x85" "b", "a\xe2\x80\xa8" "b", "a\xe2\x80\xa9" "b"}) {
        for (uint32_t property : {XUI_TEXT, XUI_NAME}) {
            xui_property batch[]{{sizeof(xui_property), XUI_NAME, button, text("Must stay unchanged")},
                {sizeof(xui_property), property, label, text(hardbreak)}};
            expect(xui_update(window, batch, 2) == XUI_INVALID_ARGUMENT);
            char result[128]{}; uint32_t length{};
            ok(xui_text_copy(button, result, sizeof(result), &length));
            expect(std::string_view(result, length) == "Untouched button");
            ok(xui_text_copy(label, result, sizeof(result), &length));
            expect(std::string_view(result, length) == "literal  spaces\tand long text");
        }
    }
    ok(xui_label_layout_set(label, &wrap)); text_value("first line\nsecond line\nthird line");
    const xui_axis_constraints axes{sizeof(axes), XUI_AXIS_CONSTRAINTS_VERSION,
        {XUI_AXIS_OVERRIDE | XUI_AXIS_LENGTH, 180}, {XUI_AXIS_OVERRIDE}};
    ok(xui_element_set_axis_constraints(label, &axes));
    struct State { xui_handle window, label, input; xui_label_layout wrap; bool ran{}; } state{window, label, input, wrap};
    ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
        if (!execute) return XUI_OK;
        auto& s = *static_cast<State*>(context);
        const auto frame = FindWindowW(L"Xui.Window.1", L"Label layout ABI"); expect(frame != nullptr);
        const auto native_label = retained_pages_abi::Fixture::named(frame, L"first line\nsecond line\nthird line");
        const auto edit = retained_pages_abi::Fixture::named(frame, L"label layout neighbor");
        ok(xui_focus(s.input, 0)); SendMessageW(edit, EM_SETSEL, 2, 7);
        const auto before = grid_layout_abi::bounds(s.label);
        expect(before.width == 180 && before.height > 20);
        s.wrap.maximum_lines = 1; ok(xui_label_layout_set(s.label, &s.wrap));
        SendMessageW(frame, WM_APP + 12, 0, 0);
        const auto one = grid_layout_abi::bounds(s.label);
        expect(one.height < before.height && one.width == before.width);
        s.wrap.maximum_lines = 0; ok(xui_label_layout_set(s.label, &s.wrap));
        SendMessageW(frame, WM_APP + 12, 0, 0);
        expect(grid_layout_abi::bounds(s.label).height > before.height);
        xui_label_layout reset{sizeof(reset), XUI_LABEL_LAYOUT_VERSION};
        ok(xui_label_layout_set(s.label, &reset)); SendMessageW(frame, WM_APP + 12, 0, 0);
        expect(read(s.label).mode == 0 && IsWindow(native_label) && IsWindow(edit) && GetFocus() == edit);
        DWORD start{}, end{};
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        expect(start == 2 && end == 7);
        char text[128]{}; uint32_t length{}; ok(xui_text_copy(s.label, text, sizeof(text), &length));
        expect(std::string_view(text, length) == "first line\nsecond line\nthird line");
        s.ran = true; return xui_window_close(s.window);
    }, &state));
    ok(xui_window_run(window)); expect(state.ran && read(label).mode == 0);
    expect(xui_label_layout_set(label, &single) == XUI_CLOSED);
    ok(xui_window_destroy(window));
}
}
