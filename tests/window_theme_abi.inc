#include "xui/xui_presentation.h"

namespace window_theme_abi {
xui_window_theme_options snapshot(xui_handle window) {
    xui_window_theme_options result{sizeof(result), XUI_WINDOW_THEME_VERSION};
    ok(xui_window_theme_get(window, &result)); return result;
}
void equal(const xui_window_theme_options& actual, const xui_window_theme_options& expected) {
    expect(std::memcmp(&actual, &expected, sizeof(actual)) == 0);
}
void run() {
    static_assert(sizeof(xui_window_theme_options) == 40 && sizeof(xui_theme_color) == 8);
    expect(xui_window_theme_version() == XUI_WINDOW_THEME_VERSION);
    for (const auto name : {"xui_window_theme_version", "xui_window_theme_get", "xui_window_theme_set"})
        expect(GetProcAddress(GetModuleHandleW(L"xui.dll"), name) != nullptr);
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Requested native theme ABI"), 600, 480, 3};
    xui_handle window{}, root{}, input{};
    ok(xui_window_create(&options, &window)); ok(xui_stack_create(window, 1, &root));
    ok(xui_create(window, XUI_TEXT_INPUT, text("Theme field header"), 0, &input));
    xui_property text_value{sizeof(text_value), XUI_TEXT, input, text("Theme retained text")};
    ok(xui_update(window, &text_value, 1));
    ok(xui_stack_add(root, input, 0)); ok(xui_window_content(window, root));
    auto original = snapshot(window);
    expect(original.theme == 3 && !original.mask);
    const xui_window_theme_options requested{sizeof(requested), XUI_WINDOW_THEME_VERSION, 3, 7,
        {0x000000, 0xcc66ff}, {0xabcdee, 0x102030}, {0xffdd00, 0x000033}};
    ok(xui_window_theme_set(window, &requested)); equal(snapshot(window), requested);
    for (unsigned i = 0; i < 7; ++i) {
        auto invalid = requested;
        if (i == 0) invalid.theme = 4;
        if (i == 1) invalid.mask = 8;
        if (i == 2) invalid.foreground.light = 0x1000000;
        if (i == 3) invalid.background.dark = UINT32_MAX;
        if (i == 4) invalid.accent.light = 0x1000000;
        if (i == 5) invalid.mask = 1;
        if (i == 6) invalid.mask = 0;
        expect(xui_window_theme_set(window, &invalid) == XUI_INVALID_ARGUMENT);
        equal(snapshot(window), requested);
    }
    auto malformed = requested; malformed.size = 39;
    expect(xui_window_theme_set(window, &malformed) == XUI_VERSION_MISMATCH);
    expect(xui_window_theme_get(window, &malformed) == XUI_VERSION_MISMATCH);
    malformed = requested; malformed.version = 0;
    expect(xui_window_theme_set(window, &malformed) == XUI_VERSION_MISMATCH);
    expect(xui_window_theme_get(window, &malformed) == XUI_VERSION_MISMATCH);
    expect(xui_window_theme_set(window, nullptr) == XUI_VERSION_MISMATCH);
    expect(xui_window_theme_get(window, nullptr) == XUI_VERSION_MISMATCH);
    expect(xui_window_theme_set(root, &requested) == XUI_WRONG_KIND);
    expect(xui_window_theme_get(root, &malformed) == XUI_VERSION_MISMATCH);
    malformed = requested;
    expect(xui_window_theme_get(root, &malformed) == XUI_WRONG_KIND);
    std::thread wrong_thread([&] {
        auto value = requested;
        expect(xui_window_theme_get(window, &value) == XUI_WRONG_THREAD);
        expect(xui_window_theme_set(window, &value) == XUI_WRONG_THREAD);
    }); wrong_thread.join();
    struct State { xui_handle window, input; xui_window_theme_options requested, original; unsigned changes{}; bool ran{}; };
    State state{window, input, requested, original};
    ok(xui_subscribe(input, [](void* context, const xui_event* event) -> xui_status {
        if (event->kind == XUI_CHANGE) ++static_cast<State*>(context)->changes;
        return XUI_OK;
    }, &state));
    ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
        if (!execute) return XUI_OK;
        auto& s = *static_cast<State*>(context);
        const auto frame = FindWindowW(L"Xui.Window.1", L"Requested native theme ABI");
        expect(frame != nullptr);
        struct Native { HWND edit{}, header{}; } native;
        EnumChildWindows(frame, [](HWND hwnd, LPARAM context) -> BOOL {
            auto& result = *reinterpret_cast<Native*>(context);
            wchar_t type[64]{}, name[128]{};
            GetClassNameW(hwnd, type, 64); GetWindowTextW(hwnd, name, 128);
            if (_wcsicmp(type, L"EDIT") == 0) result.edit = hwnd;
            if (std::wstring_view(name) == L"Theme field header") result.header = hwnd;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&native));
        expect(native.edit && native.header);
        ok(xui_focus(s.input, 0)); SendMessageW(native.edit, EM_SETSEL, 2, 8);
        const auto identity = GetDlgCtrlID(native.edit);
        const auto font = SendMessageW(native.edit, WM_GETFONT, 0, 0);
        HIGHCONTRASTW contrast{sizeof(contrast)};
        expect(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE);
        const auto native_colors = [&](HWND hwnd, UINT message, COLORREF expected) {
            const auto dc = GetDC(hwnd); expect(dc != nullptr);
            const auto brush = SendMessageW(frame, message, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(hwnd));
            expect(brush != 0 && GetTextColor(dc) == expected);
            const auto background = GetBkColor(dc); ReleaseDC(hwnd, dc); return background;
        };
        for (uint32_t visual : {XUI_STYLE_CLASSIC, XUI_STYLE_WINUI}) {
            ok(xui_window_visual_style_set(s.window, visual));
            for (uint32_t mode : {1u, 0u, 2u, 3u}) {
                auto next = s.requested; next.theme = mode;
                ok(xui_window_theme_set(s.window, &next));
                SendMessageW(frame, WM_APP + 12, 0, 0);
                equal(snapshot(s.window), next);
                const bool hc = mode == 2 || (contrast.dwFlags & HCF_HIGHCONTRASTON);
                if (mode != 3) {
                    const auto ink = hc ? GetSysColor(COLOR_WINDOWTEXT) : mode == 1 ? RGB(0, 0, 0) : RGB(204, 102, 255);
                    const auto fill = native_colors(native.edit, WM_CTLCOLOREDIT, ink);
                    native_colors(native.header, WM_CTLCOLORSTATIC, ink);
                    if (!hc) expect(fill != (mode == 1 ? RGB(171, 205, 238) : RGB(16, 32, 48)));
                }
                DWORD start{}, end{};
                SendMessageW(native.edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
                expect(IsWindow(native.edit) && GetDlgCtrlID(native.edit) == identity &&
                    GetFocus() == native.edit && start == 2 && end == 8 && s.changes == 0);
                if (visual == XUI_STYLE_CLASSIC) expect(SendMessageW(native.edit, WM_GETFONT, 0, 0) == font);
            }
        }
        auto light = s.requested; light.theme = 1;
        ok(xui_window_theme_set(s.window, &light));
        xui_style_property local{sizeof(local), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_FOREGROUND,
            XUI_STYLE_COLOR, XUI_STYLE_TEXT_PART, 0, 0, {0x008800, 0x00ff00}};
        ok(xui_control_set_style_values(s.input, XUI_STYLE_TEXT_PART, &local, 1));
        SendMessageW(frame, WM_APP + 12, 0, 0);
        native_colors(native.edit, WM_CTLCOLOREDIT,
            contrast.dwFlags & HCF_HIGHCONTRASTON ? GetSysColor(COLOR_WINDOWTEXT) : RGB(0, 136, 0));
        SendMessageW(native.edit, WM_IME_STARTCOMPOSITION, 0, 0);
        light.accent = {0, 0xffffff};
        ok(xui_window_theme_set(s.window, &light));
        SendMessageW(frame, WM_APP + 12, 0, 0);
        xui_control_interaction interaction{sizeof(interaction), XUI_CONTROL_INTERACTION_VERSION};
        ok(xui_control_interaction_get(s.input, &interaction));
        expect(interaction.is_composing && interaction.has_focus && s.changes == 0);
        SendMessageW(native.edit, WM_IME_ENDCOMPOSITION, 0, 0);
        ok(xui_window_theme_set(s.window, &s.original)); equal(snapshot(s.window), s.original);
        ok(xui_window_theme_set(s.window, &s.requested)); equal(snapshot(s.window), s.requested);
        s.ran = true; return xui_window_close(s.window);
    }, &state));
    ok(xui_window_run(window)); expect(state.ran);
    equal(snapshot(window), requested);
    expect(xui_window_theme_set(window, &original) == XUI_CLOSED);
    ok(xui_window_destroy(window));
    expect(xui_window_theme_get(window, &original) == XUI_INVALID_HANDLE);
}
}
