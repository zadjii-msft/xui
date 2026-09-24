#include "xui/xui_text.h"

namespace grid_layout_abi {
void metric(float actual, float expected, const std::source_location where = std::source_location::current()) {
    if (!std::isfinite(actual) || std::abs(actual - expected) >= 0.01f)
        std::cerr << "Grid metric " << actual << " != " << expected << " at line " << where.line() << '\n';
    expect(std::isfinite(actual) && std::abs(actual - expected) < 0.01f, where);
}
xui::Rect bounds(xui_handle element) {
    xui::Rect result{};
    ok(xui_element_bounds(element, &result.x, &result.y, &result.width, &result.height));
    return result;
}
xui_grid_track track(uint32_t sizing, float value = 1, float minimum = 0,
    float maximum = (std::numeric_limits<float>::max)()) {
    return {sizeof(xui_grid_track), sizing, value, minimum, maximum, 0};
}
void run() {
    expect(xui_grid_layout_version() == XUI_GRID_LAYOUT_VERSION);
    expect(GetProcAddress(GetModuleHandleW(L"xui.dll"), "xui_grid_layout_version") != nullptr);
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Native Grid measurement ABI"), 600, 650};
    xui_handle window{}, root{};
    ok(xui_window_create(&options, &window)); ok(xui_window_show_activated(window, 0));
    ok(xui_stack_create(window, 1, &root)); ok(xui_window_content(window, root));
    const auto preferred = [&](xui_handle target, float width, float height) {
        xui_property property{sizeof(property), XUI_PREFERRED_SIZE, target, {}, width, height};
        ok(xui_update(window, &property, 1));
    };
    const auto label = [&](const char* name, float width, float height) {
        xui_handle result{}; ok(xui_create(window, XUI_LABEL, text(name), 0, &result));
        preferred(result, width, height); return result;
    };
    const auto spanned = create(window, XUI_GRID);
    auto row = track(1);
    xui_grid_track columns[]{track(0, 40), track(1)};
    xui_grid_track rows[]{track(1), track(1)};
    ok(xui_grid_tracks(spanned, rows, 2, columns, 2));
    const auto span = label("Spanning Grid cell", 150, 30), fixed = label("Fixed Grid cell", 10, 20),
        automatic = label("Automatic Grid cell", 20, 20);
    ok(xui_panel_add(spanned, span, 0, 0, 1, 2));
    ok(xui_panel_add(spanned, fixed, 1, 0, 1, 1));
    ok(xui_panel_add(spanned, automatic, 1, 1, 1, 1));
    ok(xui_stack_add(root, spanned, 0));
    auto invalid = columns[0]; invalid.sizing = 99;
    expect(xui_grid_tracks(spanned, rows, 2, &invalid, 1) == XUI_INVALID_ARGUMENT);
    expect(xui_panel_add(spanned, automatic, 0, 0, 1, 1) == XUI_INVALID_ARGUMENT);
    const auto scrolling = create(window, XUI_GRID);
    xui_grid_track stars[]{track(2, 1), track(2, 3)}, star = track(2);
    ok(xui_grid_tracks(scrolling, stars, 2, &star, 1));
    const auto first = label("Scroll Grid first", 80, 40), second = label("Scroll Grid second", 80, 80);
    ok(xui_panel_add(scrolling, first, 0, 0, 1, 1));
    ok(xui_panel_add(scrolling, second, 1, 0, 1, 1));
    xui_handle scroll{}; ok(xui_create(window, XUI_SCROLL_VIEW, text("Grid scroll"), scrolling, &scroll));
    uint32_t filling{};
    ok(xui_scroll_view_get_fill_viewport(scroll, &filling)); expect(filling == 1);
    expect(xui_scroll_view_set_fill_viewport(scroll, 2) == XUI_INVALID_ARGUMENT);
    ok(xui_scroll_view_set_fill_viewport(scroll, 0));
    ok(xui_scroll_view_get_fill_viewport(scroll, &filling)); expect(filling == 0);
    preferred(scroll, 240, 90); ok(xui_stack_add(root, scroll, 0));
    const auto bounded = create(window, XUI_GRID);
    xui_grid_track bounded_rows[]{track(0, 44), track(0, 8), track(2)};
    ok(xui_grid_tracks(bounded, bounded_rows, 3, &star, 1));
    const auto top = label("Legacy Grid top", 80, 44), body = label("Legacy Grid body", 80, 10);
    ok(xui_panel_add(bounded, top, 0, 0, 1, 1)); ok(xui_panel_add(bounded, body, 2, 0, 1, 1));
    preferred(bounded, 400, 180); ok(xui_stack_add(root, bounded, 0));
    const auto capped = create(window, XUI_GRID);
    xui_grid_track caps[]{track(0, 30), track(1, 1, 20, 50), track(1, 1, 10, 200)};
    ok(xui_grid_tracks(capped, &row, 1, caps, 3));
    const auto cap_span = label("Capped spanning cell", 180, 20), a = label("Capped first auto", 0, 10),
        b = label("Capped second auto", 0, 10);
    ok(xui_panel_add(capped, cap_span, 0, 0, 1, 3));
    ok(xui_panel_add(capped, a, 0, 1, 1, 1)); ok(xui_panel_add(capped, b, 0, 2, 1, 1));
    xui_axis_constraints constraints{sizeof(constraints), XUI_AXIS_CONSTRAINTS_VERSION,
        {XUI_AXIS_OVERRIDE | XUI_AXIS_LENGTH, 100, 0, 0}, {XUI_AXIS_OVERRIDE}};
    ok(xui_element_set_axis_constraints(capped, &constraints));
    ok(xui_stack_add(root, capped, 0));
    xui_handle flex{}, flex_scroll{};
    ok(xui_stack_create(window, 1, &flex));
    xui_property spacing{sizeof(spacing), XUI_SPACING, flex, {}, 8};
    ok(xui_update(window, &spacing, 1));
    const auto flex_first = label("Natural flex first", 60, 32), flex_second = label("Natural flex second", 60, 48);
    ok(xui_stack_add(flex, flex_first, 1)); ok(xui_stack_add(flex, flex_second, 3));
    ok(xui_create(window, XUI_SCROLL_VIEW, text("Flex scroll"), flex, &flex_scroll));
    preferred(flex_scroll, 240, 100); ok(xui_stack_add(root, flex_scroll, 0));
    struct State {
        xui_handle window, spanned, span, fixed, automatic, scroll, first, second, bounded, top, body, capped, a, b,
            flex_scroll, flex_first, flex_second;
        bool ran{};
    };
    State state{window, spanned, span, fixed, automatic, scroll, first, second, bounded, top, body, capped, a, b,
        flex_scroll, flex_first, flex_second};
    ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
        if (!execute) return XUI_OK;
        auto& s = *static_cast<State*>(context);
        const auto grid = bounds(s.spanned), span = bounds(s.span), fixed = bounds(s.fixed), automatic = bounds(s.automatic);
        metric(span.width, 150); metric(fixed.width, 40); metric(automatic.width, 110); metric(automatic.x - grid.x, 40);
        const auto scroll = bounds(s.scroll), first = bounds(s.first), second = bounds(s.second);
        metric(scroll.height, 90); metric(first.height, 40); metric(second.height, 80);
        metric(first.y, scroll.y); metric(second.y - first.y, 40);
        const auto bounded = bounds(s.bounded), top = bounds(s.top), body = bounds(s.body);
        metric(top.height, 44); metric(body.y - bounded.y, 52); metric(body.height, 128);
        const auto capped = bounds(s.capped), a = bounds(s.a), b = bounds(s.b);
        metric(capped.width, 100); metric(a.width, 40); metric(b.width, 30);
        metric(bounds(s.flex_first).height, 23);
        metric(bounds(s.flex_second).y - bounds(s.flex_first).y, 31);
        // Grid itself is not a mutable Stack, even though its exact family now supports axis constraints.
        expect(xui_content_stack_move(s.spanned, s.fixed, 0) == XUI_WRONG_KIND);
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"Native Grid measurement ABI");
        expect(hwnd != nullptr);
        ok(xui_scroll_view_set_fill_viewport(s.flex_scroll, 0));
        SendMessageW(hwnd, WM_APP + 12, 0, 0);
        metric(bounds(s.flex_first).height, 32); metric(bounds(s.flex_second).height, 48);
        metric(bounds(s.flex_second).y - bounds(s.flex_first).y, 40);
        struct Native { HWND fixed{}, automatic{}; } native;
        EnumChildWindows(hwnd, [](HWND child, LPARAM data) -> BOOL {
            auto& result = *reinterpret_cast<Native*>(data);
            wchar_t caption[64]{}; GetWindowTextW(child, caption, 64);
            if (std::wstring_view(caption) == L"Fixed Grid cell") result.fixed = child;
            if (std::wstring_view(caption) == L"Automatic Grid cell") result.automatic = child;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&native));
        expect(native.fixed && native.automatic && IsWindowVisible(native.fixed) && IsWindowVisible(native.automatic));
        RECT native_fixed{}, native_auto{};
        GetWindowRect(native.fixed, &native_fixed); GetWindowRect(native.automatic, &native_auto);
        const auto scale = GetDpiForWindow(hwnd) / 96.0f;
        metric(static_cast<float>(native_auto.left - native_fixed.left), static_cast<float>(std::lround(40 * scale)));
        metric(static_cast<float>(native_auto.right - native_auto.left), static_cast<float>(std::lround(110 * scale)));
        xui_property offset{sizeof(offset), XUI_SCROLL_OFFSET, s.scroll, {}, 30};
        ok(xui_update(s.window, &offset, 1));
        SendMessageW(hwnd, WM_APP + 12, 0, 0);
        metric(bounds(s.second).y, bounds(s.scroll).y + 10);
        s.ran = true;
        return xui_window_close(s.window);
    }, &state));
    ok(xui_window_run(window)); expect(state.ran);
    ok(xui_window_destroy(window));
}
void focus_query() {
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Native focus query"), 400, 300};
    xui_handle window{}, root{}, content{}, input{}, button{}, scroll{};
    ok(xui_window_create(&options, &window));
    ok(xui_stack_create(window, 1, &root)); ok(xui_stack_create(window, 1, &content));
    ok(xui_create(window, XUI_TEXT_INPUT, text("Focus input"), 0, &input));
    ok(xui_create(window, XUI_BUTTON, text("Other focus"), 0, &button));
    xui_property text_property{sizeof(text_property), XUI_TEXT, input, text("Retained focus")};
    ok(xui_update(window, &text_property, 1));
    ok(xui_stack_add(content, input, 0)); ok(xui_stack_add(content, button, 0));
    ok(xui_create(window, XUI_SCROLL_VIEW, text("Focus parent"), content, &scroll));
    ok(xui_stack_add(root, scroll, 1)); ok(xui_window_content(window, root));
    expect(read_value(input, XUI_F_FOCUSED).first == 0);
    struct State { xui_handle window, input, button, scroll; unsigned focus_events{}; bool ran{}; };
    State state{window, input, button, scroll};
    ok(xui_subscribe(input, [](void* context, const xui_event* event) -> xui_status {
        if (event->kind == XUI_FOCUS_ENTERED) ++static_cast<State*>(context)->focus_events;
        return XUI_OK;
    }, &state));
    ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
        if (!execute) return XUI_OK;
        auto& s = *static_cast<State*>(context);
        ok(xui_focus(s.input, 0));
        const auto edit = GetFocus(); expect(edit != nullptr);
        ok(xui_text_input_selection_set(s.input, 2, 6));
        expect(read_value(s.input, XUI_F_FOCUSED).first == 1);
        expect(read_value(s.scroll, XUI_F_FOCUSED).first == 0);
        const auto events = s.focus_events;
        xui_property enabled[]{ {sizeof(xui_property), XUI_ENABLED, s.input, {}, 0, 0, 0, 0, 0},
            {sizeof(xui_property), XUI_ENABLED, s.input, {}, 0, 0, 0, 0, 1} };
        ok(xui_update(s.window, enabled, 2));
        auto visibility = value();
        ok(xui_feature_set(s.input, XUI_F_VISIBLE, &visibility));
        visibility.first = 1; ok(xui_feature_set(s.input, XUI_F_VISIBLE, &visibility));
        expect(GetFocus() == edit);
        expect(read_value(s.input, XUI_F_FOCUSED).first == 1);
        ok(xui_focus(s.input, 0));
        expect(GetFocus() == edit && read_value(s.input, XUI_F_FOCUSED).first == 1);
        uint64_t start{}, end{}; ok(xui_text_input_selection_get(s.input, &start, &end));
        expect(start == 2 && end == 6 && events == s.focus_events);
        ok(xui_focus(s.button, 0));
        expect(GetFocus() != edit && read_value(s.input, XUI_F_FOCUSED).first == 0);
        expect(read_value(s.button, XUI_F_FOCUSED).first == 1);
        expect(read_value(s.scroll, XUI_F_FOCUSED).first == 0);
        s.ran = true; return xui_window_close(s.window);
    }, &state));
    ok(xui_window_run(window)); expect(state.ran);
    expect(read_value(input, XUI_F_FOCUSED).first == 0);
    expect(read_value(button, XUI_F_FOCUSED).first == 0);
    ok(xui_window_destroy(window));
}
}
