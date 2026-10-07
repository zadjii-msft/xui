#include "window_host.hpp"
#include "platform.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <cmath>

namespace xui::platform {

Runtime::Runtime() {
    hr_require(OleInitialize(nullptr), "Initialize OLE on the UI thread");
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&controls)) {
        const auto error = GetLastError();
        OleUninitialize();
        SetLastError(error);
        win32_require(false, "Initialize Windows controls");
    }
}
Runtime::~Runtime() { OleUninitialize(); }
int Runtime::run(const std::function<bool(MSG&)>& translate, HANDLE ready,
    const std::function<void()>& accept, const std::function<void()>& present) {
    for (;;) {
        const DWORD count = ready ? 1 : 0;
        const DWORD wait = MsgWaitForMultipleObjectsEx(count, ready ? &ready : nullptr,
            INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        win32_require(wait != WAIT_FAILED, "Wait for Windows messages");
        if (ready && wait == WAIT_OBJECT_0) accept();
        if (present) present();
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return static_cast<int>(message.wParam);
            if (!translate(message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (present) present();
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
void appearance(HWND window, ThemeMode theme, const Palette& palette, bool backdrop) {
    const BOOL dark = theme == ThemeMode::dark && !palette.high_contrast;
    // A caption color would replace the material in the system title bar.
    const COLORREF caption = palette.high_contrast || backdrop ? DWMWA_COLOR_DEFAULT : native_color(palette.background);
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
bool system_backdrop_supported() {
    using GetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
    static const bool supported = [] {
        const auto ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto read = ntdll ? reinterpret_cast<GetVersion>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
        OSVERSIONINFOW version{sizeof(version)};
        // DWMWA_SYSTEMBACKDROP_TYPE is documented for Windows 11 build 22621 and later.
        return read && read(&version) == 0 && (version.dwMajorVersion > 10 ||
            (version.dwMajorVersion == 10 && version.dwBuildNumber >= 22621));
    }();
    return supported;
}
void round_corners(HWND window) {
    const DWORD preference = DWMWCP_ROUND;
    const HRESULT result = DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
    if (result != E_INVALIDARG && result != DWM_E_COMPOSITIONDISABLED) hr_require(result, "Round the window corners");
}
void composition_frame(HWND window, bool card) {
    const auto attribute = [window](DWORD id, const void* data, DWORD size, const char* operation) {
        const HRESULT result = DwmSetWindowAttribute(window, id, data, size);
        if (result != E_INVALIDARG && result != DWM_E_COMPOSITIONDISABLED) hr_require(result, operation);
    };
    const BOOL host = TRUE;
    attribute(DWMWA_USE_HOSTBACKDROPBRUSH, &host, sizeof(host), "Enable composition backdrops");
    if (!card) return;
    // The card's own shapes are its edges; DWM would otherwise draw a border and round the rectangle.
    const COLORREF border = DWMWA_COLOR_NONE;
    attribute(DWMWA_BORDER_COLOR, &border, sizeof(border), "Remove the composition card border");
    const DWORD corners = DWMWCP_DONOTROUND;
    attribute(DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners), "Square the composition card");
}
void layered_child(HWND window) {
    const auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    SetWindowLongPtrW(window, GWL_EXSTYLE, style | WS_EX_LAYERED);
    // Child layering needs a Windows 8 or later compatibility manifest; otherwise USER drops the style.
    if (!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYERED) || !SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA))
        throw std::runtime_error("Composition windows require a Windows 8 or later application manifest for native controls");
}
bool transparency_effects_enabled() {
    DWORD value{1}, size{sizeof(value)};
    const auto result = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return result != ERROR_SUCCESS || value != 0;
}
bool system_backdrop(HWND window, DWORD type, bool custom_frame) {
    if (type && !system_backdrop_supported()) {
        OutputDebugStringW(L"XUI: System backdrops require Windows 11 build 22621 or later; using a solid background.\n");
        return false;
    }
    const auto unavailable = [](HRESULT result) { return result == E_INVALIDARG || result == DWM_E_COMPOSITIONDISABLED; };
    // DWM uses the client alpha channel only where the frame extends into the client area.
    // A nonzero top margin makes DWM draw its own caption beneath a custom title bar, so
    // custom frames extend a bottom band across the whole client area instead.
    MARGINS margins{};
    if (type) margins = custom_frame ? MARGINS{0, 0, 0, 0x7fff} : MARGINS{-1, -1, -1, -1};
    HRESULT result = DwmExtendFrameIntoClientArea(window, &margins);
    if (unavailable(result)) {
        if (type) OutputDebugStringW(L"XUI: DWM cannot extend the system backdrop; using a solid background.\n");
        return false;
    }
    hr_require(result, "Extend the system backdrop");
    if (!system_backdrop_supported()) return false;
    const DWORD value = type ? type : DWMSBT_AUTO;
    result = DwmSetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &value, sizeof(value));
    if (unavailable(result)) {
        if (type) {
            OutputDebugStringW(L"XUI: DWM rejected the system backdrop; using a solid background.\n");
            margins = {};
            hr_require(DwmExtendFrameIntoClientArea(window, &margins), "Restore the window frame");
        }
        return false;
    }
    hr_require(result, "Apply the system backdrop");
    return type != 0;
}
namespace {
constexpr UINT_PTR opaque_paint_id = 0x58554941;
bool draws_outside_paint(HWND window, UINT message) {
    switch (message) {
    case WM_CHAR: case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_IME_CHAR:
    case WM_IME_STARTCOMPOSITION: case WM_IME_COMPOSITION: case WM_IME_ENDCOMPOSITION:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN:
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: case WM_VSCROLL: case WM_HSCROLL:
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE:
    case WM_CUT: case WM_PASTE: case WM_CLEAR: case WM_UNDO:
        return true;
    case WM_MOUSEMOVE: case WM_TIMER:
        // Drag selection and automatic scrolling draw while the control has capture.
        return GetCapture() == window;
    default: return false;
    }
}
bool changes_content(UINT message) {
    switch (message) {
    case WM_SETTEXT: case EM_SETSEL: case EM_REPLACESEL: case EM_UNDO: case EM_SETREADONLY:
    case EM_SETPASSWORDCHAR: case EM_SCROLLCARET: case EM_LINESCROLL:
    case WM_USER + 55: // EM_EXSETSEL
    case WM_USER + 68: // EM_SETCHARFORMAT
    case WM_USER + 71: // EM_SETPARAFORMAT
    case WM_USER + 97: // EM_SETTEXTEX
        return true;
    default: return false;
    }
}
LRESULT CALLBACK opaque_paint(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR id, DWORD_PTR) noexcept {
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, opaque_paint, id);
        BufferedPaintUnInit();
        return DefSubclassProc(window, message, wparam, lparam);
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        RECT client{};
        if (dc && GetClientRect(window, &client) && !IsRectEmpty(&client)) {
            HDC buffer{};
            const auto handle = BeginBufferedPaint(dc, &client, BPBF_TOPDOWNDIB, nullptr, &buffer);
            if (handle) {
                SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(buffer), PRF_CLIENT | PRF_ERASEBKGND);
                // The control's own background and text become opaque over the material.
                if (FAILED(BufferedPaintSetAlpha(handle, nullptr, 255)))
                    OutputDebugStringW(L"XUI: Cannot make native pixels opaque over the system backdrop.\n");
                EndBufferedPaint(handle, TRUE);
            } else {
                OutputDebugStringW(L"XUI: Cannot buffer native pixels over the system backdrop.\n");
                SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND);
            }
        }
        EndPaint(window, &paint);
        return 0;
    }
    const auto result = DefSubclassProc(window, message, wparam, lparam);
    if (draws_outside_paint(window, message))
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
    else if (changes_content(message)) InvalidateRect(window, nullptr, FALSE);
    return result;
}
}
void opaque_native_paint(HWND window, bool enabled) {
    if (!window || !IsWindow(window)) return;
    DWORD_PTR data{};
    const bool installed = GetWindowSubclass(window, opaque_paint, opaque_paint_id, &data) != FALSE;
    if (installed == enabled) return;
    if (enabled) {
        hr_require(BufferedPaintInit(), "Initialize native backdrop painting");
        if (!SetWindowSubclass(window, opaque_paint, opaque_paint_id, 0)) {
            BufferedPaintUnInit();
            win32_require(false, "Attach native backdrop painting");
        }
    } else {
        win32_require(RemoveWindowSubclass(window, opaque_paint, opaque_paint_id) != FALSE, "Detach native backdrop painting");
        BufferedPaintUnInit();
    }
    InvalidateRect(window, nullptr, TRUE);
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
