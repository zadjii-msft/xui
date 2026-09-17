#include "xui/xui.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <cstring>
namespace {
void expect(bool value) { if (!value) throw std::runtime_error("preview ABI assertion"); }
xui_string text(const char* value) { return {value, static_cast<uint32_t>(strlen(value)), 0}; }
xui_status XUI_CALL changed(void* state, const xui_event* event) {
    auto& calls = *static_cast<int*>(state);
    xui_preview_status status{sizeof(status), XUI_PREVIEW_VERSION};
    expect(event->kind == XUI_CHANGE);
    expect(xui_shell_preview_get_status(event->source, &status) == XUI_OK);
    expect(event->value == status.generation);
    ++calls;
    return XUI_OK;
}
}
int main() {
    try {
        static_assert(sizeof(xui_preview_status) == 40);
        xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Preview ABI"), 480, 320};
        xui_handle window{}, preview{}, wrong{};
        expect(xui_window_create(&options, &window) == XUI_OK);
        expect(xui_shell_preview_create(window, text("Windows preview"), &preview) == XUI_OK);
        expect(xui_create(window, XUI_LABEL, text("Not a preview"), 0, &wrong) == XUI_OK);
        xui_preview_status status{sizeof(status), XUI_PREVIEW_VERSION};
        expect(xui_shell_preview_get_status(preview, &status) == XUI_OK && status.state == XUI_PREVIEW_IDLE);
        expect(xui_shell_preview_get_status(wrong, &status) == XUI_WRONG_KIND);
        status.reserved = 1;
        expect(xui_shell_preview_get_status(preview, &status) == XUI_INVALID_ARGUMENT);
        status.reserved = 0;
        expect(xui_shell_preview_focus_content(preview, 2) == XUI_INVALID_ARGUMENT);
        uint64_t generation{};
        expect(xui_shell_preview_load_local(preview, text(""), &generation) == XUI_INVALID_ARGUMENT);
        int calls{};
        expect(xui_subscribe(preview, changed, &calls) == XUI_OK);
        expect(xui_shell_preview_load_local(preview, text("C:\\locally-authored.txt"), &generation) == XUI_OK);
        expect(generation == 1 && calls == 1);
        expect(xui_shell_preview_cancel(preview, generation + 1) == XUI_OK && calls == 1);
        expect(xui_shell_preview_cancel(preview, generation) == XUI_OK && calls == 2);
        std::atomic<int> result{};
        std::thread worker([&] { result = xui_shell_preview_get_status(preview, &status); });
        worker.join();
        expect(result == XUI_WRONG_THREAD);
        expect(xui_window_destroy(window) == XUI_OK);
        expect(xui_shell_preview_get_status(preview, &status) == XUI_INVALID_HANDLE);
        std::cout << "preview ABI passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << "\n"; return 1; }
}
