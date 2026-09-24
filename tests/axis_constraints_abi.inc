#include <limits>

namespace axis_constraints_abi {
void run() {
    static_assert(sizeof(xui_axis_constraint) == 16 && sizeof(xui_axis_constraints) == 40);
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Axis ABI"), 400, 300};
    xui_handle window{}; ok(xui_window_create(&options, &window));
    xui_handle stack{}, host{}, scroll_content{}, scroll{};
    ok(xui_stack_create(window, 1, &stack));
    ok(xui_content_host_create(window, &host));
    ok(xui_stack_create(window, 1, &scroll_content));
    ok(xui_create(window, XUI_SCROLL_VIEW, text("Scroll"), scroll_content, &scroll));
    std::vector<xui_handle> supported{stack, host, scroll};
    for (auto kind : {XUI_LABEL, XUI_BUTTON, XUI_TOGGLE, XUI_TEXT_INPUT}) {
        xui_handle control{}; ok(xui_create(window, kind, text("Axis control"), 0, &control));
        supported.push_back(control);
    }
    supported.push_back(create(window, XUI_CHECK_BOX));
    supported.push_back(create(window, XUI_PROGRESS));
    supported.push_back(create(window, XUI_GRID));
    supported.push_back(create(window, XUI_MULTILINE_TEXT));
    supported.push_back(create(window, XUI_PASSWORD_INPUT));
    const xui_axis_constraints configured{sizeof(configured), XUI_AXIS_CONSTRAINTS_VERSION,
        {XUI_AXIS_OVERRIDE | XUI_AXIS_LENGTH | XUI_AXIS_MAXIMUM, 50, 20, 100}, {XUI_AXIS_OVERRIDE, 0, 0, 0}};
    for (auto target : supported) {
        xui_axis_constraints result{sizeof(result), XUI_AXIS_CONSTRAINTS_VERSION};
        ok(xui_element_get_axis_constraints(target, &result));
        expect(result.width.flags == 0 && result.height.flags == 0);
        ok(xui_element_set_axis_constraints(target, &configured));
        ok(xui_element_get_axis_constraints(target, &result));
        expect(std::memcmp(&result, &configured, sizeof(result)) == 0);
        auto bad = configured; bad.width.length = 30;
        for (const auto invalid : {
            xui_axis_constraint{8, 0, 0, 0}, xui_axis_constraint{XUI_AXIS_LENGTH, 10, 0, 0},
            xui_axis_constraint{0, 0, 1, 0}, xui_axis_constraint{XUI_AXIS_OVERRIDE, 1, 0, 0},
            xui_axis_constraint{XUI_AXIS_OVERRIDE, 0, 0, 1},
            xui_axis_constraint{XUI_AXIS_OVERRIDE, 0, -1, 0},
            xui_axis_constraint{XUI_AXIS_OVERRIDE | XUI_AXIS_LENGTH, -1, 0, 0},
            xui_axis_constraint{XUI_AXIS_OVERRIDE | XUI_AXIS_LENGTH, 1, 2, 0},
            xui_axis_constraint{XUI_AXIS_OVERRIDE | XUI_AXIS_MAXIMUM, 0, 2, 1},
            xui_axis_constraint{7, 30, 0, 20},
            xui_axis_constraint{XUI_AXIS_OVERRIDE | XUI_AXIS_LENGTH, NAN, 0, 0},
            xui_axis_constraint{XUI_AXIS_OVERRIDE | XUI_AXIS_LENGTH, INFINITY, 0, 0},
            xui_axis_constraint{XUI_AXIS_OVERRIDE, 0, NAN, 0},
            xui_axis_constraint{XUI_AXIS_OVERRIDE | XUI_AXIS_MAXIMUM, 0, 0, INFINITY}}) {
            bad.height = invalid;
            expect(xui_element_set_axis_constraints(target, &bad) == XUI_INVALID_ARGUMENT);
            ok(xui_element_get_axis_constraints(target, &result));
            expect(std::memcmp(&result, &configured, sizeof(result)) == 0);
        }
        bad = configured; bad.size = sizeof(bad) - 1;
        expect(xui_element_set_axis_constraints(target, &bad) == XUI_VERSION_MISMATCH);
        expect(xui_element_get_axis_constraints(target, &bad) == XUI_VERSION_MISMATCH);
        bad = configured; ++bad.version;
        expect(xui_element_set_axis_constraints(target, &bad) == XUI_VERSION_MISMATCH);
        expect(xui_element_get_axis_constraints(target, &bad) == XUI_VERSION_MISMATCH);
        xui_status threaded{};
        std::thread worker([&] { threaded = xui_element_set_axis_constraints(target, &configured); });
        worker.join(); expect(threaded == XUI_WRONG_THREAD);
        bad = {sizeof(bad), XUI_AXIS_CONSTRAINTS_VERSION};
        ok(xui_element_set_axis_constraints(target, &bad));
        ok(xui_element_get_axis_constraints(target, &result));
        expect(std::memcmp(&result, &bad, sizeof(result)) == 0);
    }
    for (const auto target : {window, create(window, XUI_WRAP), create(window, XUI_RICH_TEXT),
        create(window, XUI_TOGGLE_SWITCH), create(window, XUI_PROGRESS_RING)}) {
        expect(xui_element_set_axis_constraints(target, &configured) == XUI_WRONG_KIND);
        auto result = configured;
        expect(xui_element_get_axis_constraints(target, &result) == XUI_WRONG_KIND);
        expect(std::memcmp(&result, &configured, sizeof(result)) == 0);
    }
    expect(xui_element_set_axis_constraints(stack, nullptr) == XUI_VERSION_MISMATCH);
    expect(xui_element_get_axis_constraints(stack, nullptr) == XUI_VERSION_MISMATCH);
    expect(xui_element_set_axis_constraints(UINT64_MAX, &configured) == XUI_INVALID_HANDLE);
    HMODULE dll = GetModuleHandleW(L"xui.dll");
    expect(GetProcAddress(dll, "xui_element_set_axis_constraints") != nullptr);
    expect(GetProcAddress(dll, "xui_element_get_axis_constraints") != nullptr);
    ok(xui_window_close(window));
    expect(xui_element_set_axis_constraints(stack, &configured) == XUI_CLOSED);
    ok(xui_window_destroy(window));
    auto result = configured;
    expect(xui_element_get_axis_constraints(stack, &result) == XUI_INVALID_HANDLE);
}
}
