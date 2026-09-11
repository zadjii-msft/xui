#include "xui/native_edit.hpp"
#include "platform.hpp"
#include "xui/theme.hpp"
#include <commctrl.h>
#include <cmath>

namespace xui {

NativeEditBridge::~NativeEditBridge() {
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    if (font_) DeleteObject(font_);
}

void NativeEditBridge::attach(HWND parent, int control_id) {
    if (window_) throw std::logic_error("Native edit already attached");
    window_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 1, 1, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
        GetModuleHandleW(nullptr), nullptr);
    win32_require(window_ != nullptr, "Create native search field");
    win32_require(SetWindowSubclass(window_, subclass, 1, reinterpret_cast<DWORD_PTR>(this)) != 0,
                  "Attach native search input");
    SendMessageW(window_, EM_SETLIMITTEXT, 1024, 0);
    SendMessageW(window_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    set_dpi(GetDpiForWindow(parent));
}

void NativeEditBridge::set_dpi(UINT dpi) {
    dpi_ = dpi;
    HFONT replacement = CreateFontW(-MulDiv(static_cast<int>(VisualMetrics::body_size), static_cast<int>(dpi), 96), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    win32_require(replacement != nullptr, "Create search font");
    SendMessageW(window_, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
    if (font_) DeleteObject(font_);
    font_ = replacement;
}

void NativeEditBridge::arrange(Rect bounds) {
    Element::arrange(bounds);
    if (!window_) return;
    const auto insets = insets_.value_or(VisualMetrics::search_insets);
    bounds = {bounds.x + insets.left, bounds.y + insets.top,
        std::max(0.0f, bounds.width - insets.left - insets.right),
        std::max(0.0f, bounds.height - insets.top - insets.bottom)};
    const float scale = dpi_ / 96.0f;
    win32_require(SetWindowPos(window_, nullptr, static_cast<int>(std::lround(bounds.x * scale)),
        static_cast<int>(std::lround(bounds.y * scale)),
        static_cast<int>(std::lround(bounds.width * scale)),
        static_cast<int>(std::lround(bounds.height * scale)),
        SWP_NOZORDER | SWP_NOACTIVATE) != 0, "Arrange native search field");
}

std::wstring NativeEditBridge::text() const {
    const int length = GetWindowTextLengthW(window_);
    std::wstring result(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window_, result.data(), length + 1);
    result.resize(static_cast<size_t>(copied));
    return result;
}

void NativeEditBridge::focus(bool select_all) {
    SetFocus(window_);
    if (select_all) SendMessageW(window_, EM_SETSEL, 0, -1);
}

void NativeEditBridge::set_placeholder_color(COLORREF color) {
    placeholder_color_ = color;
    if (window_) win32_require(InvalidateRect(window_, nullptr, FALSE) != 0, "Refresh search hint");
}

void NativeEditBridge::set_placeholder(std::wstring text) {
    placeholder_ = std::move(text);
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void NativeEditBridge::set_insets(Insets insets) {
    insets_ = insets;
    invalidate(Invalidation::layout);
}

LRESULT CALLBACK NativeEditBridge::subclass(HWND window, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR id, DWORD_PTR data) noexcept {
    auto& self = *reinterpret_cast<NativeEditBridge*>(data);
    if (message == WM_PAINT || message == WM_PRINTCLIENT) {
        const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
        // Only the empty-field hint is custom. EDIT still owns text, caret, selection and IME.
        if (!self.composing_ && GetWindowTextLengthW(window) == 0) {
            HDC dc = message == WM_PRINTCLIENT ? reinterpret_cast<HDC>(wparam) : GetDC(window);
            if (dc) {
                const int saved = SaveDC(dc);
                if (saved) {
                    RECT bounds{};
                    if (GetClientRect(window, &bounds)) {
                        bounds.left += 2;
                        SelectObject(dc, self.font_);
                        SetBkMode(dc, TRANSPARENT);
                        SetTextColor(dc, self.placeholder_color_);
                        DrawTextW(dc, self.placeholder_.c_str(), -1, &bounds,
                            DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                    }
                    if (!RestoreDC(dc, saved))
                        OutputDebugStringW(L"XUI: Cannot restore the search hint drawing context.\n");
                } else {
                    OutputDebugStringW(L"XUI: Cannot save the search hint drawing context.\n");
                }
                if (message != WM_PRINTCLIENT) ReleaseDC(window, dc);
            } else {
                OutputDebugStringW(L"XUI: Cannot acquire the search hint drawing context.\n");
            }
        }
        return result;
    }
    if (message == WM_IME_STARTCOMPOSITION) {
        self.composing_ = true;
        if (!InvalidateRect(window, nullptr, TRUE))
            OutputDebugStringW(L"XUI: Cannot refresh the search field at composition start.\n");
    }
    if (message == WM_IME_ENDCOMPOSITION) {
        self.composing_ = false;
        const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
        SendMessageW(GetParent(window), WM_COMMAND,
            MAKEWPARAM(GetDlgCtrlID(window), EN_CHANGE), reinterpret_cast<LPARAM>(window));
        return result;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, subclass, id);
        self.window_ = nullptr;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

}
