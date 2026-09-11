#include "window_host.hpp"
#include "platform.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <cmath>

namespace xui::platform {

Runtime::Runtime() {
    hr_require(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Initialize COM on the UI thread");
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&controls)) {
        const auto error = GetLastError();
        CoUninitialize();
        SetLastError(error);
        win32_require(false, "Initialize Windows controls");
    }
}
Runtime::~Runtime() { CoUninitialize(); }
int Runtime::run(const std::function<bool(MSG&)>& translate, HANDLE ready,
    const std::function<void()>& accept) {
    for (;;) {
        const DWORD count = ready ? 1 : 0;
        const DWORD wait = MsgWaitForMultipleObjectsEx(count, ready ? &ready : nullptr,
            INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        win32_require(wait != WAIT_FAILED, "Wait for Windows messages");
        if (ready && wait == WAIT_OBJECT_0) accept();
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return static_cast<int>(message.wParam);
            if (!translate(message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }
}
COLORREF native_color(D2D1_COLOR_F color) {
    return RGB(static_cast<BYTE>(std::lround(color.r * 255)),
        static_cast<BYTE>(std::lround(color.g * 255)),
        static_cast<BYTE>(std::lround(color.b * 255)));
}
void place(HWND window, Rect bounds, UINT dpi) {
    const float scale = dpi / 96.0f;
    win32_require(SetWindowPos(window, nullptr, static_cast<int>(std::lround(bounds.x * scale)),
        static_cast<int>(std::lround(bounds.y * scale)),
        std::max(0, static_cast<int>(std::lround(bounds.width * scale))),
        std::max(0, static_cast<int>(std::lround(bounds.height * scale))),
        SWP_NOZORDER | SWP_NOACTIVATE) != 0, "Arrange child window");
}
void appearance(HWND window, ThemeMode theme, const Palette& palette) {
    const BOOL dark = theme == ThemeMode::dark && !palette.high_contrast;
    const COLORREF caption = palette.high_contrast ? DWMWA_COLOR_DEFAULT : native_color(palette.background);
    const COLORREF text = palette.high_contrast ? DWMWA_COLOR_DEFAULT : native_color(palette.text);
    auto attribute = [window](DWORD id, const void* data, DWORD size) {
        const HRESULT result = DwmSetWindowAttribute(window, id, data, size);
        if (result != E_INVALIDARG && result != DWM_E_COMPOSITIONDISABLED)
            hr_require(result, "Apply window appearance");
    };
    attribute(DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    attribute(DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    attribute(DWMWA_TEXT_COLOR, &text, sizeof(text));
}
bool traverse_focus(std::span<const HWND> windows, bool reverse, HWND current) {
    if (windows.empty()) return false;
    const auto found = std::find(windows.begin(), windows.end(), current ? current : GetFocus());
    size_t index = found == windows.end() ? (reverse ? 0 : windows.size() - 1)
                                        : static_cast<size_t>(found - windows.begin());
    for (size_t count = 0; count < windows.size(); ++count) {
        index = reverse ? (index ? index - 1 : windows.size() - 1) : (index + 1) % windows.size();
        if (IsWindowEnabled(windows[index]) && IsWindowVisible(windows[index])) {
            SetFocus(windows[index]);
            return true;
        }
    }
    return false;
}
void request_focus_restore(HWND window) {
    win32_require(PostMessageW(window, restore_focus_message, 0, 0) != 0, "Schedule focus restoration");
}
void restore_focus(HWND window, HWND preferred, std::span<const HWND> targets) {
    // Let a native/UIA focus transaction finish before restoring application focus.
    if (GetFocus() != window) return;
    if (preferred && IsWindowEnabled(preferred) && IsWindowVisible(preferred)) SetFocus(preferred);
    else traverse_focus(targets, false);
}

}
