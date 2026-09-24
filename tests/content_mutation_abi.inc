#include <functional>
#include <source_location>
#include <string>
#include <vector>

namespace content_mutation_abi {
unsigned assertions{};
void check(bool value, const std::source_location location = std::source_location::current()) {
    if (!value) throw std::runtime_error("Content mutation ABI assertion at line " + std::to_string(location.line()));
    ++assertions;
}
void status(xui_status value, xui_status expected = XUI_OK,
    const std::source_location location = std::source_location::current()) {
    if (value != expected) {
        char message[1024]{}; uint32_t length{}; xui_status code{};
        xui_error_copy(message, sizeof(message), &length, &code);
        throw std::runtime_error("Content mutation ABI status " + std::to_string(value) +
            ", expected " + std::to_string(expected) + " at line " + std::to_string(location.line()) + ": " + message);
    }
    ++assertions;
}
xui_string text(const char* value) { return {value, static_cast<uint32_t>(std::strlen(value)), 0}; }
HWND editor(HWND window) {
    HWND result{};
    EnumChildWindows(window, [](HWND child, LPARAM context) -> BOOL {
        wchar_t name[32]{}; GetClassNameW(child, name, 32);
        if (_wcsicmp(name, L"EDIT")) return TRUE;
        *reinterpret_cast<HWND*>(context) = child;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&result));
    check(result != nullptr);
    return result;
}
struct Fixture {
    xui_handle application{}, window{}, root{}, host{}, other_host{}, scope{}, stack{}, input{}, button{}, other_scope{}, other_stack{};
    std::exception_ptr failure;
    unsigned calls{}, native_calls{}, posted{};
    bool application_ran{};
    Fixture() {
        status(xui_application_create(&application));
        xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Subtree ABI ownership"), 500, 650};
        status(xui_application_window_create(application, &options, 0, &window));
        status(xui_stack_create(window, 1, &root));
        status(xui_content_host_create(window, &host));
        status(xui_content_host_create(window, &other_host));
        status(xui_stack_add(root, host, 1)); status(xui_stack_add(root, other_host, 1));
        status(xui_window_content(window, root));
        status(xui_content_begin(host, &scope));
        stack = make_stack();
        input = make(XUI_TEXT_INPUT, "Retained ABI editor");
        button = make(XUI_BUTTON, "Retained ABI button");
        status(xui_stack_add(stack, input, 0)); status(xui_stack_add(stack, button, 0));
        status(xui_content_commit(scope, stack));
        status(xui_content_begin(other_host, &other_scope));
        other_stack = make_stack();
        status(xui_content_commit(other_scope, other_stack));
        status(xui_application_show(application, window));
    }
    ~Fixture() {
        if (window) { xui_window_close(window); xui_window_destroy(window); }
        if (application) xui_application_destroy(application);
    }
    void finish() {
        status(xui_window_close(window));
        if (!application_ran) {
            application_ran = true;
            status(xui_application_run(application));
        }
        status(xui_window_destroy(window)); window = 0;
        status(xui_application_destroy(application)); application = 0;
    }
    xui_handle make_stack() {
        xui_handle result{}; status(xui_stack_create(window, 1, &result)); return result;
    }
    xui_handle make(uint32_t kind, const char* name) {
        xui_handle result{}; status(xui_create(window, kind, text(name), 0, &result)); return result;
    }
    uint32_t count() {
        uint32_t result{}; status(xui_content_handle_count(window, &result)); return result;
    }
    void stale(xui_handle handle) {
        xui_handle owner{}; status(xui_content_owner(handle, &owner), XUI_INVALID_HANDLE);
    }
    static xui_status XUI_CALL count_callback(void* context, const xui_event*) {
        ++static_cast<Fixture*>(context)->calls;
        return XUI_OK;
    }
    static xui_status XUI_CALL native_callback(void* context, const xui_event*) {
        auto& self = *static_cast<Fixture*>(context);
        try {
            ++self.native_calls;
            status(xui_content_validate_mutation(self.scope), XUI_BUSY);
            status(xui_content_begin_append(self.scope), XUI_BUSY);
            status(xui_content_stack_move(self.stack, self.button, 0), XUI_BUSY);
            status(xui_content_stack_remove(self.stack, self.button), XUI_BUSY);
            status(xui_content_release_element(self.scope, self.input), XUI_BUSY);
            return XUI_OK;
        } catch (...) { self.failure = std::current_exception(); return XUI_CALLBACK_FAILED; }
    }
    static xui_status XUI_CALL post(void* context, uint32_t execute) {
        auto& self = *static_cast<Fixture*>(context);
        if (!execute) return XUI_OK;
        try {
            ++self.posted;
            status(xui_content_validate_mutation(self.scope));
            status(xui_content_begin_append(self.scope));
            const auto child = self.make(XUI_BUTTON, "Deferred append");
            status(xui_content_end_append(self.scope, 1));
            status(xui_content_stack_insert(self.stack, 0, child, 0));
            status(xui_invoke(self.button));
            check(self.native_calls != 0);
            status(xui_content_stack_remove(self.stack, child));
            status(xui_content_release_element(self.scope, child));
            status(xui_window_close(self.window));
            return XUI_OK;
        } catch (...) { self.failure = std::current_exception(); return XUI_CALLBACK_FAILED; }
    }
    void run() {
        HMODULE dll = GetModuleHandleW(L"xui.dll");
        for (const auto name : {"xui_content_begin_append", "xui_content_end_append", "xui_content_validate_mutation",
            "xui_content_stack_insert", "xui_content_stack_remove", "xui_content_stack_validate_move",
            "xui_content_stack_move", "xui_content_release_element"})
            check(GetProcAddress(dll, name) != nullptr);
        const auto baseline = count();
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"Subtree ABI ownership");
        check(hwnd != nullptr);
        const auto edit = editor(hwnd);
        const auto id = GetDlgCtrlID(edit);
        SetWindowTextW(edit, L"native retained text");
        SendMessageW(edit, EM_SETSEL, 2, 7);
        SetFocus(edit);
        const auto focus = GetFocus();
        const char* phase = "initial";
        auto retained = [&] {
            DWORD begin{}, end{};
            SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&begin), reinterpret_cast<LPARAM>(&end));
            if (editor(hwnd) != edit || GetDlgCtrlID(edit) != id || GetFocus() != focus || begin != 2 || end != 7) {
                wchar_t name[100]{}; GetWindowTextW(GetFocus(), name, 100);
                RECT rect{}; GetWindowRect(edit, &rect);
                std::cerr << "Retained ABI editor: hwnd=" << edit << " current=" << editor(hwnd)
                    << " id=" << id << "/" << GetDlgCtrlID(edit) << " focus=" << focus << "/" << GetFocus()
                    << " selection=" << begin << "," << end << " iteration=" << calls << " phase=" << phase
                    << " visible=" << IsWindowVisible(edit) << " enabled=" << IsWindowEnabled(edit)
                    << " bounds=" << rect.left << "," << rect.top << "," << rect.right << "," << rect.bottom << "\n";
                std::wcerr << "Focused name=" << name << "\n";
            }
            check(editor(hwnd) == edit && GetDlgCtrlID(edit) == id && GetFocus() == focus && begin == 2 && end == 7);
        };
        status(xui_content_validate_mutation(scope));
        status(xui_content_release_element(scope, input), XUI_BUSY);
        status(xui_content_stack_insert(stack, 0, input, 0), XUI_INVALID_ARGUMENT);
        status(xui_content_stack_insert(stack, 0, stack, 0), XUI_INVALID_ARGUMENT);
        status(xui_content_stack_validate_move(stack, button, UINT32_MAX));
        status(xui_content_stack_move(stack, button, 2), XUI_INVALID_ARGUMENT);
        status(xui_content_stack_remove(stack, stack), XUI_INVALID_ARGUMENT);
        status(xui_content_stack_move(stack, other_stack, 0), XUI_INVALID_ARGUMENT);
        status(xui_content_release_element(other_scope, input), XUI_INVALID_ARGUMENT);
        status(xui_content_begin_append(UINT64_MAX), XUI_INVALID_HANDLE);
        xui_status wrong{};
        std::thread worker([&] { wrong = xui_content_validate_mutation(scope); }); worker.join();
        status(wrong, XUI_WRONG_THREAD);
        xui_handle previous{};
        status(xui_content_context(window, scope, &previous)); check(previous == 0);
        status(xui_content_validate_mutation(scope), XUI_BUSY);
        status(xui_content_begin_append(scope), XUI_BUSY);
        status(xui_content_stack_move(stack, button, 0), XUI_BUSY);
        status(xui_content_context(window, 0, &previous)); check(previous == scope);
        status(xui_content_begin_append(scope));
        status(xui_content_begin_append(scope), XUI_BUSY);
        xui_handle nested{};
        status(xui_content_begin(other_host, &nested), XUI_BUSY);
        const auto aborted = make(XUI_BUTTON, "Aborted callback");
        status(xui_subscribe(aborted, count_callback, this));
        status(xui_stack_add(stack, aborted, 0), XUI_BUSY);
        status(xui_content_stack_insert(stack, 0, aborted, 0), XUI_BUSY);
        status(xui_content_context(window, 0, &previous), XUI_BUSY);
        status(xui_content_end_append(scope, 2), XUI_INVALID_ARGUMENT);
        status(xui_content_end_append(scope, 0));
        stale(aborted); check(count() == baseline);
        status(xui_content_end_append(scope, 0), XUI_BUSY);
        for (unsigned i = 0; i < 32; ++i) {
            status(xui_content_begin_append(scope));
            const auto row = make_stack(), label = make(XUI_LABEL, "Transient label"), child = make(XUI_BUTTON, "Transient callback");
            status(xui_stack_add(row, label, 0)); status(xui_stack_add(row, child, 0));
            status(xui_subscribe(child, count_callback, this));
            status(xui_content_end_append(scope, 1));
            status(xui_content_stack_insert(stack, 50, row, 0), XUI_INVALID_ARGUMENT);
            status(xui_content_stack_insert(stack, 1, row, 0));
            phase = "insert"; retained();
            status(xui_content_stack_move(stack, button, 0));
            phase = "move"; retained();
            status(xui_content_release_element(scope, label), XUI_BUSY);
            status(xui_invoke(child)); check(calls == i + 1);
            phase = "invoke";
            retained();
            status(xui_content_stack_remove(stack, row));
            status(xui_invoke(child)); check(calls == i + 1);
            status(xui_subscribe(child, nullptr, nullptr));
            status(xui_content_release_element(scope, child)); stale(child);
            status(xui_content_stack_insert(stack, 0, row, 0), XUI_INVALID_ARGUMENT);
            status(xui_content_release_element(scope, label)); stale(label);
            status(xui_content_release_element(scope, row)); stale(row);
            check(count() == baseline); retained();
        }
        SendMessageW(edit, WM_IME_STARTCOMPOSITION, 0, 0);
        status(xui_content_validate_mutation(scope), XUI_BUSY);
        status(xui_content_begin_append(scope), XUI_BUSY);
        status(xui_content_stack_validate_move(stack, button, UINT32_MAX), XUI_BUSY);
        status(xui_content_stack_move(stack, button, 1), XUI_BUSY);
        status(xui_content_stack_remove(stack, button), XUI_BUSY);
        retained();
        SendMessageW(edit, WM_IME_ENDCOMPOSITION, 0, 0);
        status(xui_content_begin_append(scope));
        const auto popup_child = make(XUI_BUTTON, "Live popup ownership");
        xui_handle popup{};
        xui_feature_options popup_options{sizeof(popup_options), XUI_FEATURE_VERSION, text("Ownership popup"), popup_child};
        status(xui_feature_create(window, XUI_POPUP, &popup_options, &popup));
        status(xui_content_end_append(scope, 1));
        status(xui_popup_show(popup, button));
        status(xui_content_release_element(scope, popup_child), XUI_BUSY);
        status(xui_content_release_element(scope, popup), XUI_BUSY);
        status(xui_feature_action(popup, XUI_A_DISMISS, 0, 0));
        status(xui_content_stack_move(stack, button, 1));
        status(xui_content_release_element(scope, popup_child));
        status(xui_content_release_element(scope, popup));
        check(count() == baseline);
        status(xui_subscribe(button, native_callback, this));
        status(xui_subscribe(input, native_callback, this));
        SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"changed"));
        if (failure) std::rethrow_exception(failure);
        check(native_calls != 0);
        // These calls run inside a posted callback, including a nested synchronous
        // ABI callback: only the latter is forbidden from mutating the tree.
        status(xui_window_post(window, post, this));
        application_ran = true;
        const auto result = xui_application_run(application);
        if (failure) std::rethrow_exception(failure);
        status(result);
        check(posted == 1 && native_calls >= 2);
        status(xui_content_release(scope));
        stale(input); stale(button); stale(stack); stale(scope);
        status(xui_content_release(other_scope));
        check(count() == 4);
        finish();
    }
};
void run() {
    Fixture{}.run();
    {
        Fixture fixture;
        status(xui_content_begin_append(fixture.scope));
        const auto orphan = fixture.make(XUI_BUTTON, "Abort entire arena");
        status(xui_subscribe(orphan, Fixture::count_callback, &fixture));
        status(xui_content_release(fixture.scope));
        fixture.stale(orphan); fixture.stale(fixture.scope); fixture.stale(fixture.input);
        xui_handle next{}, root{};
        status(xui_content_begin(fixture.host, &next));
        root = fixture.make_stack();
        status(xui_content_commit(next, root));
        status(xui_content_release(next));
        status(xui_content_release(fixture.other_scope));
        check(fixture.count() == 4);
        fixture.finish();
    }
    {
        Fixture fixture;
        status(xui_window_post(fixture.window, [](void* context, uint32_t execute) -> xui_status {
            if (!execute) return XUI_OK;
            auto& fixture = *static_cast<Fixture*>(context);
            try {
                status(xui_content_begin_append(fixture.scope));
                fixture.make(XUI_BUTTON, "Closing arena append");
                status(xui_window_close(fixture.window));
                // Native failure/close can precede the deferred Closed event.
                status(xui_content_release(fixture.scope));
                fixture.stale(fixture.scope);
                return XUI_OK;
            } catch (...) { fixture.failure = std::current_exception(); return XUI_CALLBACK_FAILED; }
        }, &fixture));
        fixture.application_ran = true;
        const auto result = xui_application_run(fixture.application);
        if (fixture.failure) std::rethrow_exception(fixture.failure);
        status(result);
        status(xui_content_release(fixture.other_scope));
        check(fixture.count() == 4);
        fixture.finish();
    }
    {
        Fixture fixture;
        status(xui_window_post(fixture.window, [](void* context, uint32_t execute) -> xui_status {
            if (!execute) return XUI_OK;
            auto& fixture = *static_cast<Fixture*>(context);
            status(xui_content_begin_append(fixture.scope));
            fixture.make(XUI_BUTTON, "Throwing posted construction");
            throw std::runtime_error("Injected foreign post failure");
        }, &fixture));
        fixture.application_ran = true;
        status(xui_application_run(fixture.application), XUI_CALLBACK_FAILED);
        // A thrown foreign callback must unwind both counters, allowing explicit
        // aggregate cleanup of its unfinished append even after native closure.
        status(xui_content_release(fixture.scope));
        fixture.stale(fixture.scope);
        status(xui_content_release(fixture.other_scope));
        check(fixture.count() == 4);
        fixture.finish();
    }
    std::cout << assertions << " content mutation ABI assertions passed.\n";
}
}
