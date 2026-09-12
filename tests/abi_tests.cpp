#include "xui/xui.h"
#include <windows.h>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
int checks{};
void expect(bool value) { if (!value) throw std::runtime_error("ABI assertion " + std::to_string(checks + 1)); ++checks; }
void ok(xui_status status) { expect(status == XUI_OK); }
xui_string s(const char* text) { return {text, static_cast<uint32_t>(strlen(text)), 0}; }
xui_window_options options() { return {sizeof(xui_window_options), XUI_ABI_VERSION, s("ABI tests"), 600, 720}; }
struct Window {
    xui_handle h{};
    Window() { auto o = options(); ok(xui_window_create(&o, &h)); }
    ~Window() { if (h) xui_window_destroy(h); }
    xui_handle control(uint32_t kind, const char* text = "Control", xui_handle content = 0) {
        xui_handle result{}; ok(xui_create(h, kind, s(text), content, &result)); return result;
    }
};
std::string read(xui_handle h) {
    uint32_t count{};
    expect(xui_text_copy(h, nullptr, 0, &count) == XUI_BUFFER_TOO_SMALL);
    std::string result(count, 0); ok(xui_text_copy(h, result.data(), count, &count)); return result;
}
struct Callback { xui_handle window, target; int calls{}, mode{}; };
xui_status XUI_CALL callback(void* data, const xui_event* e) {
    auto& c = *static_cast<Callback*>(data); ++c.calls;
    expect(e->size == sizeof(*e) && e->source == c.target);
    if (c.mode == 1) { ok(xui_subscribe(c.target, nullptr, nullptr)); expect(xui_window_destroy(c.window) == XUI_BUSY); }
    if (c.mode == 2) return 1234;
    if (c.mode == 3) throw std::runtime_error("C++ callback exception");
    if (c.mode == 4) expect(xui_invoke(c.target) == XUI_CALLBACK_FAILED);
    return XUI_OK;
}
}
int main() {
    try {
        expect(xui_abi_version() == XUI_ABI_VERSION);
        HMODULE dll = GetModuleHandleW(L"xui.dll"); expect(dll != nullptr);
        for (auto name : {"xui_abi_version", "xui_error_copy", "xui_window_create", "xui_window_destroy",
            "xui_window_run", "xui_window_close", "xui_create", "xui_stack_create", "xui_stack_add",
            "xui_window_content", "xui_update", "xui_subscribe", "xui_text_copy", "xui_focus",
            "xui_invoke", "xui_image_source", "xui_image_state", "xui_list_items", "xui_list_filter",
            "xui_list_select", "xui_list_state", "xui_window_callback_error"}) expect(GetProcAddress(dll, name) != nullptr);
        auto o = options(); xui_handle invalid = 99;
        o.version++; expect(xui_window_create(&o, &invalid) == XUI_VERSION_MISMATCH); expect(invalid == 0);
        o = options(); o.size--; expect(xui_window_create(&o, &invalid) == XUI_VERSION_MISMATCH);
        expect(xui_window_create(nullptr, &invalid) == XUI_VERSION_MISMATCH);
        expect(xui_window_create(&o, nullptr) == XUI_INVALID_ARGUMENT);
        for (auto h : {0ull, 1ull << 63, ~0ull, 0xdeadbeefull})
            expect(xui_window_destroy(h) == XUI_INVALID_HANDLE);
        uint32_t count{}; xui_status code{}; char error[1024]{};
        expect(xui_error_copy(nullptr, 0, &count, &code) == XUI_BUFFER_TOO_SMALL);
        expect(count > 0 && code == XUI_INVALID_HANDLE);
        ok(xui_error_copy(error, 1024, &count, &code));
        expect(code == XUI_INVALID_HANDLE && std::string(error, count).find("stale") != std::string::npos);
        ok(xui_error_copy(error, 1024, &count, &code)); expect(code == XUI_INVALID_HANDLE);
        {
            Window w, other;
            auto label = w.control(XUI_LABEL, "日本語 😀");
            expect(read(label) == "日本語 😀");
            const auto input = w.control(XUI_TEXT_INPUT);
            const auto named_button = w.control(XUI_BUTTON);
            xui_property button_text{sizeof(xui_property), XUI_TEXT, named_button, s("Renamed 😀")};
            ok(xui_update(w.h, &button_text, 1)); expect(read(named_button) == "Renamed 😀");
            std::string long_text(1023, 'a'); long_text += "😀";
            xui_property long_property{sizeof(xui_property), XUI_TEXT, input, s(long_text.c_str())};
            ok(xui_update(w.h, &long_property, 1));
            expect(read(input) == std::string(1023, 'a'));
            const std::string long_label(1048576, 'x');
            long_property.target = label; long_property.text = s(long_label.c_str());
            ok(xui_update(w.h, &long_property, 1)); expect(read(label) == long_label);
            long_property.text = s("日本語 😀"); ok(xui_update(w.h, &long_property, 1));
            expect(xui_window_run(label) == XUI_WRONG_KIND);
            std::atomic<int> wrong{};
            std::thread worker([&] { wrong = xui_text_copy(label, nullptr, 0, &count); });
            worker.join(); expect(wrong == XUI_WRONG_THREAD);
            for (const auto bytes : {std::string("\xc0\xaf", 2), std::string("\xed\xa0\x80", 3),
                std::string("\xf4\x90\x80\x80", 4), std::string("\xe2\x82", 2), std::string("a\0b", 3)}) {
                xui_handle result{};
                expect(xui_create(w.h, XUI_LABEL, {bytes.data(), static_cast<uint32_t>(bytes.size()), 0}, 0, &result) == XUI_INVALID_ARGUMENT);
                expect(result == 0);
            }
            expect(xui_create(w.h, XUI_LABEL, {nullptr, 1, 0}, 0, &invalid) == XUI_INVALID_ARGUMENT);
            expect(xui_create(w.h, XUI_LABEL, {"x", UINT32_MAX, 0}, 0, &invalid) == XUI_INVALID_ARGUMENT);
            xui_property p[2]{{sizeof(xui_property), XUI_TEXT, label, s("changed")},
                {sizeof(xui_property), XUI_CHECKED, label, {}, 0, 0, 0, 0, 1}};
            expect(xui_update(w.h, p, 2) == XUI_WRONG_KIND); expect(read(label) == "日本語 😀");
            expect(xui_update(other.h, p, 1) == XUI_INVALID_ARGUMENT);
            expect(xui_update(w.h, p, UINT32_MAX) == XUI_INVALID_ARGUMENT);
            expect(xui_update(w.h, nullptr, 1) == XUI_INVALID_ARGUMENT);
            ok(xui_update(w.h, nullptr, 0));
            p[0].size--; expect(xui_update(w.h, p, 1) == XUI_VERSION_MISMATCH);
            p[0].size++; p[0].text.reserved = 1;
            expect(xui_update(w.h, p, 1) == XUI_INVALID_ARGUMENT);
            xui_handle stack{}; ok(xui_stack_create(w.h, 1, &stack));
            ok(xui_stack_add(stack, label, 0));
            expect(xui_stack_add(stack, label, 0) == XUI_INVALID_ARGUMENT);
            expect(xui_stack_add(stack, stack, 0) != XUI_OK);
            auto child = other.control(XUI_LABEL);
            expect(xui_stack_add(stack, child, 0) == XUI_INVALID_ARGUMENT);
            auto image = w.control(XUI_IMAGE);
            expect(xui_image_source(image, s("x"), 0, 144) == XUI_INVALID_ARGUMENT);
            ok(xui_image_source(image, s(""), 192, 144));
            auto list = w.control(XUI_FILE_LIST);
            xui_file_item items[]{{sizeof(xui_file_item), 0, 0, s("zero"), {}},
                {sizeof(xui_file_item), 0, 8, s("eight"), {}}};
            ok(xui_list_items(list, items, 2)); ok(xui_list_select(list, 0));
            uint64_t id{}; uint32_t selected{};
            ok(xui_list_state(list, &count, &id, &selected)); expect(count == 2 && id == 0 && selected);
            ok(xui_list_filter(list, s("eight")));
            ok(xui_list_state(list, &count, &id, &selected)); expect(count == 1 && id == 0 && selected);
            expect(xui_list_select(list, 10) == XUI_INVALID_ARGUMENT);
            items[1].id = 0; expect(xui_list_items(list, items, 2) == XUI_NATIVE_ERROR);
            ok(xui_list_state(list, &count, &id, &selected)); expect(count == 1);
            const auto stale = w.h; ok(xui_window_destroy(w.h)); w.h = 0;
            expect(xui_window_destroy(stale) == XUI_INVALID_HANDLE);
            expect(xui_text_copy(label, nullptr, 0, &count) == XUI_INVALID_HANDLE);
        }
        for (int mode = 1; mode <= 4; ++mode) {
            Window w; const auto button = w.control(XUI_BUTTON);
            Callback state{w.h, button, 0, mode};
            ok(xui_subscribe(button, callback, &state));
            expect(xui_invoke(button) == (mode == 1 ? XUI_OK : XUI_CALLBACK_FAILED));
            xui_status failure{}; ok(xui_window_callback_error(w.h, &failure));
            expect(mode == 2 ? failure == 1234 : (mode == 1 ? failure == 0 : failure != 0));
            if (mode == 1) { ok(xui_invoke(button)); expect(state.calls == 1); }
            else { expect(state.calls == 1); ok(xui_subscribe(button, nullptr, nullptr)); }
        }
        for (int i = 0; i < 100; ++i) { Window w; w.control(XUI_LABEL); }
        std::cout << checks << " ABI assertions passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
