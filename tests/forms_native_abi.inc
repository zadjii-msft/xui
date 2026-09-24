#include <richedit.h>

namespace forms_native_abi {
void run() {
    expect(xui_forms_version() == XUI_FORMS_VERSION);
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Native forms purpose ABI"), 600, 650};
    xui_handle window{}, root{};
    ok(xui_window_create(&options, &window)); ok(xui_stack_create(window, 1, &root));
    std::array<xui_handle, 5> inputs{};
    for (uint32_t purpose = 0; purpose < inputs.size(); ++purpose) {
        ok(xui_text_input_create_with_purpose(window, text("Advisory field"), purpose, &inputs[purpose]));
        uint32_t actual{}; ok(xui_text_input_get_purpose(inputs[purpose], &actual)); expect(actual == purpose);
        ok(xui_stack_add(root, inputs[purpose], 0));
        const auto value = "unchanged AbC 12.3 + - @" + std::to_string(purpose);
        xui_property property{sizeof(property), XUI_TEXT, inputs[purpose], text(value.c_str())};
        ok(xui_update(window, &property, 1));
    }
    xui_handle invalid = 99;
    expect(xui_text_input_create_with_purpose(window, text("Invalid"), 5, &invalid) == XUI_INVALID_ARGUMENT && invalid == 0);
    uint32_t kind{};
    expect(xui_text_input_get_purpose(root, &kind) == XUI_WRONG_KIND);
    expect(xui_text_input_get_purpose(inputs[0], nullptr) == XUI_INVALID_ARGUMENT);
    const auto document = create(window, XUI_MULTILINE_TEXT), password = create(window, XUI_PASSWORD_INPUT);
    xui_axis_constraints axes{sizeof(axes), XUI_AXIS_CONSTRAINTS_VERSION,
        {XUI_AXIS_OVERRIDE | XUI_AXIS_LENGTH, 300, 0, 0}, {XUI_AXIS_OVERRIDE}};
    ok(xui_element_set_axis_constraints(document, &axes));
    ok(xui_element_set_axis_constraints(password, &axes));
    ok(xui_stack_add(root, document, 0)); ok(xui_stack_add(root, password, 0));
    auto doc = value(); doc.text = text("line one\nline two");
    ok(xui_feature_set(document, XUI_F_DOCUMENT_TEXT, &doc));
    ok(xui_window_content(window, root));
    struct State { xui_handle window; std::array<xui_handle, 5> inputs; bool ran{}; };
    State state{window, inputs};
    ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
        if (!execute) return XUI_OK;
        auto& state = *static_cast<State*>(context);
        const auto frame = FindWindowW(L"Xui.Window.1", L"Native forms purpose ABI");
        expect(frame != nullptr);
        std::vector<HWND> edits;
        EnumChildWindows(frame, [](HWND hwnd, LPARAM data) -> BOOL {
            wchar_t name[32]{}; GetClassNameW(hwnd, name, 32);
            if (_wcsicmp(name, L"EDIT") == 0 && !(GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_PASSWORD))
                reinterpret_cast<std::vector<HWND>*>(data)->push_back(hwnd);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&edits));
        expect(edits.size() == 5);
        for (auto edit : edits) {
            expect(!(GetWindowLongPtrW(edit, GWL_STYLE) & (ES_NUMBER | ES_MULTILINE)));
            const auto length = GetWindowTextLengthW(edit);
            SendMessageW(edit, EM_SETSEL, length, length);
            SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L" Z+@"));
            expect(GetWindowTextLengthW(edit) == length + 4);
            SendMessageW(edit, EM_SETSEL, 2, 6);
            DWORD start{}, end{}; SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            expect(start == 2 && end == 6);
        }
        for (auto input : state.inputs) {
            uint32_t required{};
            expect(xui_text_copy(input, nullptr, 0, &required) == XUI_BUFFER_TOO_SMALL);
            std::string contents(required, '\0');
            ok(xui_text_copy(input, contents.data(), required, &required));
            expect(contents.starts_with("unchanged AbC 12.3 + - @") && contents.ends_with(" Z+@"));
        }
        state.ran = true;
        return xui_window_close(state.window);
    }, &state));
    ok(xui_window_run(window)); expect(state.ran); ok(xui_window_destroy(window));
}
void geometry() {
    for (uint32_t visual : {XUI_STYLE_CLASSIC, XUI_STYLE_WINUI}) {
        xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Native forms geometry"), 600, 600};
        xui_handle window{}, root{};
        ok(xui_window_create(&options, &window)); ok(xui_window_visual_style_set(window, visual));
        ok(xui_window_set_presentation(window, text("Segoe UI"), 28, 0, 0));
        ok(xui_stack_create(window, 1, &root)); ok(xui_window_content(window, root));
        const auto document = create(window, XUI_MULTILINE_TEXT), password = create(window, XUI_PASSWORD_INPUT);
        const xui_axis_constraints axes{sizeof(axes), XUI_AXIS_CONSTRAINTS_VERSION,
            {XUI_AXIS_OVERRIDE | XUI_AXIS_LENGTH, 300}, {XUI_AXIS_OVERRIDE}};
        ok(xui_element_set_axis_constraints(document, &axes)); ok(xui_element_set_axis_constraints(password, &axes));
        ok(xui_stack_add(root, document, 0)); ok(xui_stack_add(root, password, 0));
        auto document_text = value(); document_text.text = text("alpha\nbeta\r\ngamma");
        ok(xui_feature_set(document, XUI_F_DOCUMENT_TEXT, &document_text));
        auto password_text = value(); password_text.text = text("masked-native");
        ok(xui_feature_set(password, XUI_F_PASSWORD, &password_text));
        struct State { xui_handle window, document, password; unsigned changes{}, submits{}, secret_changes{}; bool ran{}; };
        State state{window, document, password};
        ok(xui_subscribe(document, [](void* context, const xui_event* event) -> xui_status {
            auto& state = *static_cast<State*>(context);
            if (event->kind == XUI_CHANGE) ++state.changes;
            if (event->kind == XUI_SUBMIT) ++state.submits;
            return XUI_OK;
        }, &state));
        ok(xui_subscribe(password, [](void* context, const xui_event* event) -> xui_status {
            if (event->kind == XUI_CHANGE) ++static_cast<State*>(context)->secret_changes;
            return XUI_OK;
        }, &state));
        ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
            if (!execute) return XUI_OK;
            auto& s = *static_cast<State*>(context);
            const auto frame = FindWindowW(L"Xui.Window.1", L"Native forms geometry");
            expect(frame != nullptr);
            struct Native { HWND document{}, password{}; } native;
            EnumChildWindows(frame, [](HWND hwnd, LPARAM data) -> BOOL {
                auto& found = *reinterpret_cast<Native*>(data);
                wchar_t type[64]{}; GetClassNameW(hwnd, type, 64);
                if (_wcsicmp(type, L"RICHEDIT50W") == 0) found.document = hwnd;
                if (_wcsicmp(type, L"EDIT") == 0 && (GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_PASSWORD)) found.password = hwnd;
                return TRUE;
            }, reinterpret_cast<LPARAM>(&native));
            expect(native.document && native.password);
            const auto bounds = grid_layout_abi::bounds(s.document), password_bounds = grid_layout_abi::bounds(s.password);
            expect(bounds.width == 300 && bounds.height >= 180 && password_bounds.width == 300 && password_bounds.height >= 48);
            CHARFORMAT2W font{sizeof(font)};
            SendMessageW(native.document, EM_GETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&font));
            expect(font.yHeight == 420);
            const auto dc = GetDC(native.password);
            const auto previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(native.password, WM_GETFONT, 0, 0)));
            TEXTMETRICW metrics{}; expect(GetTextMetricsW(dc, &metrics) != FALSE);
            SelectObject(dc, previous); ReleaseDC(native.password, dc);
            RECT rect{}; GetWindowRect(native.password, &rect);
            expect(rect.bottom - rect.top >= metrics.tmHeight);
            ok(xui_focus(s.document, 0));
            SendMessageW(native.document, EM_SETSEL, static_cast<WPARAM>(-1), -1);
            SendMessageW(native.document, WM_KEYDOWN, VK_RETURN, 1);
            SendMessageW(native.document, WM_CHAR, VK_RETURN, 1);
            SendMessageW(native.document, WM_KEYUP, VK_RETURN, (1u << 30) | (1u << 31) | 1);
            if (s.changes != 1 || s.submits != 0)
                std::cout << "Multiline Enter changes=" << s.changes << " submits=" << s.submits
                    << " native_length=" << GetWindowTextLengthW(native.document)
                    << " control_key=" << GetKeyState(VK_CONTROL) << '\n';
            expect(s.changes == 1 && s.submits == 0);
            uint32_t required{};
            expect(xui_text_copy(s.document, nullptr, 0, &required) == XUI_BUFFER_TOO_SMALL);
            std::string contents(required, '\0'); ok(xui_text_copy(s.document, contents.data(), required, &required));
            expect(contents.starts_with("alpha\rbeta\rgamma") && contents.ends_with("\r"));
            auto readonly = value(); readonly.first = 1;
            ok(xui_feature_set(s.document, XUI_F_READ_ONLY, &readonly));
            SendMessageW(frame, WM_APP + 12, 0, 0);
            SendMessageW(native.document, WM_CHAR, L'Z', 0);
            expect(s.changes == 1);
            ok(xui_focus(s.password, 0));
            SendMessageW(native.password, EM_SETSEL, 1, 4);
            SendMessageW(native.password, WM_IME_STARTCOMPOSITION, 0, 0);
            const auto cleared = xui_password_clear(s.password);
            expect(cleared == XUI_OK || cleared == XUI_NATIVE_ERROR);
            if (cleared != XUI_OK) {
                char error[1024]{}; uint32_t count{}; xui_status code{};
                ok(xui_error_copy(error, sizeof(error), &count, &code));
                expect(code == XUI_NATIVE_ERROR && std::string_view(error, count).find("Cancel terminal password composition") != std::string_view::npos);
                std::cout << "Synthetic IME cancellation reported an explicit error; verifying remaining terminal cleanup.\n";
            }
            expect(GetWindowTextLengthW(native.password) == 0 && s.secret_changes == 0 &&
                read_value(s.password, XUI_F_PASSWORD).first == 0);
            s.ran = true; return xui_window_close(s.window);
        }, &state));
        ok(xui_window_run(window)); expect(state.ran); ok(xui_window_destroy(window));
    }
}
}
