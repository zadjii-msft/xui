#include "xui/xui_swap_chain.h"
#include <windows.h>
#include <commctrl.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void check(xui_status status) { require(status == XUI_OK, "XUI input fixture operation failed"); }
struct Context {
    xui_handle window{}, panel{}, before{}, after{};
    HWND hwnd{};
    std::array<BYTE, 256> original{};
    bool initialized{};
    unsigned tabs{}, shift_tabs{}, pages{}, shortcuts{};
    std::string error;
};
constexpr UINT step_message = WM_APP + 57;
void next(Context& context, WPARAM step, HWND target, UINT key) {
    require(PostMessageW(target, WM_KEYDOWN, key, 1), "Post owned key");
    require(PostMessageW(context.hwnd, step_message, step, 0), "Post owned assertion");
}
LRESULT CALLBACK input(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data) noexcept {
    auto& context = *reinterpret_cast<Context*>(data);
    try {
        if (message == WM_KEYDOWN) {
            if (wparam == VK_TAB) {
                if (GetKeyState(VK_SHIFT) < 0) ++context.shift_tabs;
                else ++context.tabs;
                return 0;
            }
            if (wparam == VK_NEXT) { ++context.pages; return 0; }
        }
        if (message == step_message) {
            switch (wparam) {
            case 0:
                check(xui_focus(context.panel, 0));
                require(GetFocus() == hwnd, "Focus the opted-in panel");
                next(context, 1, hwnd, VK_TAB);
                break;
            case 1: {
                require(context.tabs == 1 && GetFocus() == hwnd, "Tab reaches native input without traversing");
                auto keys = context.original;
                keys[VK_SHIFT] = 0x80;
                require(SetKeyboardState(keys.data()), "Set fixture thread Shift state");
                next(context, 2, hwnd, VK_TAB);
                break;
            }
            case 2:
                require(context.shift_tabs == 1 && GetFocus() == hwnd, "Shift+Tab reaches native input");
                require(SetKeyboardState(context.original.data()), "Restore fixture thread key state");
                next(context, 3, hwnd, VK_NEXT);
                break;
            case 3:
                require(context.pages == 1, "PageDown reaches native input");
                next(context, 4, hwnd, VK_F6);
                break;
            case 4:
                require(context.shortcuts == 1 && GetFocus() != hwnd, "Application shortcut escapes native focus");
                {
                    std::array<BYTE, 256> keys{};
                    keys[VK_SHIFT] = 0x80;
                    require(SetKeyboardState(keys.data()), "Set reverse shortcut modifiers");
                }
                next(context, 5, GetFocus(), VK_F6);
                break;
            case 5:
                require(context.shortcuts == 2 && GetFocus() == hwnd, "Reverse application shortcut returns to terminal");
                require(SetKeyboardState(context.original.data()), "Restore shortcut modifiers");
                check(xui_focus(context.before, 0));
                next(context, 6, GetFocus(), VK_TAB);
                break;
            case 6:
                require(GetFocus() == hwnd, "Ordinary traversal can enter the opted-in host");
                check(xui_swap_chain_native_input(context.panel, 0));
                check(xui_focus(context.before, 0));
                next(context, 7, GetFocus(), VK_TAB);
                break;
            case 7:
                require(GetFocus() != hwnd && context.tabs == 1, "Disabled native input preserves ordinary traversal");
                check(xui_window_close(context.window));
                break;
            }
            return 0;
        }
        if (message == WM_NCDESTROY) {
            SetKeyboardState(context.original.data());
            RemoveWindowSubclass(hwnd, input, 1);
        }
    } catch (const std::exception& error) {
        context.error = error.what();
        SetKeyboardState(context.original.data());
        xui_window_close(context.window);
        return 0;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}
xui_status XUI_CALL metrics(void* pointer, const xui_event* event) noexcept {
    auto& context = *static_cast<Context*>(pointer);
    if (event->kind != XUI_VIEW || context.initialized) return XUI_OK;
    void* hwnd{};
    const auto result = xui_swap_chain_get_window(context.panel, &hwnd);
    if (result || !hwnd) return result;
    context.hwnd = static_cast<HWND>(hwnd);
    context.initialized = true;
    if (!GetKeyboardState(context.original.data()) ||
        !SetWindowSubclass(context.hwnd, input, 1, reinterpret_cast<DWORD_PTR>(&context)) ||
        !PostMessageW(context.hwnd, step_message, 0, 0)) return XUI_NATIVE_ERROR;
    return XUI_OK;
}
xui_status XUI_CALL shortcut(void* pointer, const xui_key_event* event, uint32_t* handled) noexcept {
    auto& context = *static_cast<Context*>(pointer);
    *handled = event->virtual_key == VK_F6;
    if (*handled) {
        ++context.shortcuts;
        return xui_focus(event->modifiers == 2 ? context.panel : context.before, 0);
    }
    return XUI_OK;
}
}
int main() {
    try {
        Context context;
        xui_window_options options{sizeof(options), XUI_ABI_VERSION, {"Native input fixture", 20, 0}, 400, 300};
        check(xui_window_create(&options, &context.window));
        check(xui_create(context.window, XUI_BUTTON, {"Before", 6, 0}, 0, &context.before));
        check(xui_create(context.window, XUI_SWAP_CHAIN_PANEL, {"Terminal", 8, 0}, 0, &context.panel));
        check(xui_create(context.window, XUI_BUTTON, {"After", 5, 0}, 0, &context.after));
        check(xui_swap_chain_native_input(context.panel, 1));
        xui_handle root{};
        check(xui_stack_create(context.window, 1, &root));
        check(xui_stack_add(root, context.before, 0));
        check(xui_stack_add(root, context.panel, 1));
        check(xui_stack_add(root, context.after, 0));
        check(xui_window_content(context.window, root));
        check(xui_subscribe(context.panel, metrics, &context));
        check(xui_window_key_handler(context.window, shortcut, &context));
        check(xui_window_run(context.window));
        check(xui_window_destroy(context.window));
        require(context.error.empty(), context.error.c_str());
        require(context.tabs == 1 && context.shift_tabs == 1 && context.pages == 1 && context.shortcuts == 2,
            "All native routing stages completed");
        std::cout << "Native Tab, Shift+Tab, PageDown, shortcut escape and ordinary traversal passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
