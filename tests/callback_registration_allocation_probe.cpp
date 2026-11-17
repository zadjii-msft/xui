#include "xui/xui.h"
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace allocation_probe {
thread_local bool enabled{};
thread_local std::size_t calls{}, bytes{};
}
void* operator new(std::size_t size) {
    if (auto* value = std::malloc(size ? size : 1)) {
        if (allocation_probe::enabled) { ++allocation_probe::calls; allocation_probe::bytes += size; }
        return value;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
void ok(xui_status status) {
    if (status != XUI_OK) throw std::runtime_error("Allocation census ABI call failed");
}
xui_status XUI_CALL event(void*, const xui_event*) { return XUI_OK; }
template<class F> void census(const char* type, const char* phase, F&& action) {
    allocation_probe::calls = allocation_probe::bytes = 0;
    allocation_probe::enabled = true;
    try { action(); }
    catch (...) { allocation_probe::enabled = false; throw; }
    allocation_probe::enabled = false;
    std::cout << "{\"type\":\"" << type << "\",\"phase\":\"" << phase
        << "\",\"ordinary_allocations\":" << allocation_probe::calls
        << ",\"requested_bytes\":" << allocation_probe::bytes << "}\n";
}
}
int main() {
    try {
        // Compile c_api.cpp into this executable: caller hooks cannot observe a separate DLL's heap.
        const xui_string name{"Census", 6, 0};
        xui_window_options options{sizeof(options), XUI_ABI_VERSION, name, 600, 400};
        xui_handle owner{};
        ok(xui_window_create(&options, &owner));
        for (auto kind : {XUI_BUTTON, XUI_TEXT_INPUT}) {
            const auto* type = kind == XUI_BUTTON ? "Button" : "TextInput";
            xui_handle control{};
            ok(xui_create(owner, kind, name, 0, &control));
            census(type, "first-subscribe", [&] { ok(xui_subscribe(control, event, nullptr)); });
            census(type, "replace", [&] { ok(xui_subscribe(control, event, nullptr)); });
            census(type, "remove-resubscribe", [&] {
                ok(xui_subscribe(control, nullptr, nullptr));
                ok(xui_subscribe(control, event, nullptr));
            });
        }
        ok(xui_window_destroy(owner));
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
