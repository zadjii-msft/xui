#include "xui/native_edit.hpp"
#include "platform.hpp"
#include "xui/theme.hpp"
#include "xui/controls.hpp"
#include "suggestion_peer.hpp"
#include <commctrl.h>
#include <cmath>

namespace xui {

NativeEditBridge::NativeEditBridge() = default;
NativeEditBridge::~NativeEditBridge() {
    suggestions_.reset();
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
    if (font_ && dpi_ == dpi) return;
    HFONT replacement = CreateFontW(-MulDiv(static_cast<int>(VisualMetrics::body_size), static_cast<int>(dpi), 96), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    win32_require(replacement != nullptr, "Create search font");
    SendMessageW(window_, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
    if (font_) DeleteObject(font_);
    font_ = replacement;
    dpi_ = dpi;
}

void NativeEditBridge::arrange(Rect bounds) {
    const auto previous = this->bounds();
    if (previous.x != bounds.x || previous.y != bounds.y ||
        previous.width != bounds.width || previous.height != bounds.height) dismiss_suggestions();
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

void NativeEditBridge::sync_suggestions(TextInput& input) {
    if (input.suggestions() && !suggestions_)
        suggestions_ = std::make_unique<SuggestionPeer>(*this, input);
    if (!input.suggestions()) suggestions_.reset();
    if (suggestions_) suggestions_->sync();
}
void NativeEditBridge::text_changed() {
    if (suggestions_ && !setting_text_) suggestions_->changed();
}
void NativeEditBridge::dismiss_suggestions() {
    if (suggestions_) suggestions_->dismiss();
}
void NativeEditBridge::set_suggestion_colors(COLORREF background, COLORREF text, COLORREF secondary) {
    if (suggestions_) suggestions_->set_colors(background, text, secondary);
}
RECT NativeEditBridge::suggestion_anchor() const {
    const auto area = bounds();
    const auto scale = dpi_ / 96.0f;
    RECT rect{static_cast<LONG>(std::lround(area.x * scale)), static_cast<LONG>(std::lround(area.y * scale)),
        static_cast<LONG>(std::lround((area.x + area.width) * scale)),
        static_cast<LONG>(std::lround((area.y + area.height) * scale))};
    MapWindowPoints(GetParent(window_), nullptr, reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}
bool NativeEditBridge::suggestion_key(WPARAM key) {
    return suggestions_ && suggestions_->key(key);
}
void NativeEditBridge::set_model_text(const std::wstring& text) {
    dismiss_suggestions();
    setting_text_ = true;
    SetWindowTextW(window_, text.c_str());
    setting_text_ = false;
}

void NativeEditBridge::set_placeholder_color(COLORREF color) {
    if (placeholder_color_ == color) return;
    placeholder_color_ = color;
    if (window_) win32_require(InvalidateRect(window_, nullptr, FALSE) != 0, "Refresh search hint");
}

void NativeEditBridge::set_placeholder(std::wstring text) {
    if (placeholder_ == text) return;
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
    try {
        if (message == detail::suggestions_ready) {
            if (self.suggestions_) self.suggestions_->deliver();
            return 0;
        }
        if (message == WM_TIMER && wparam == SuggestionPeer::timer_id) {
            if (self.suggestions_) self.suggestions_->timer();
            return 0;
        }
        if (message == WM_KEYDOWN && self.suggestion_key(wparam)) return 0;
        if (message == WM_KILLFOCUS || message == WM_CANCELMODE ||
            message == WM_IME_STARTCOMPOSITION || message == EM_SETREADONLY ||
            (message == WM_ENABLE && !wparam) || (message == WM_SHOWWINDOW && !wparam))
            self.dismiss_suggestions();
    } catch (...) {
        self.dismiss_suggestions();
        self.report_failure();
        return 0;
    }
    if (message == WM_ACTIVATE && LOWORD(wparam) != WA_INACTIVE) {
        const auto result = DefSubclassProc(window, message, wparam, lparam);
        // The native UIA proxy can activate EDIT itself, even in a minimized
        // window. Keep activation on the top-level host and keyboard focus on EDIT.
        if (GetActiveWindow() == window) {
            const auto root = GetAncestor(window, GA_ROOT);
            if (IsIconic(root)) ShowWindow(root, SW_RESTORE);
            SetForegroundWindow(root);
            SetActiveWindow(root);
            SetFocus(window);
        }
        return result;
    }
    if (message == WM_PAINT && !self.composing_ && GetWindowTextLengthW(window) == 0 &&
        !self.placeholder_.empty()) {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        RECT bounds{};
        GetClientRect(window, &bounds);
        const auto buffer = dc ? CreateCompatibleDC(dc) : nullptr;
        const auto bitmap = dc ? CreateCompatibleBitmap(dc, bounds.right, bounds.bottom) : nullptr;
        bool complete{};
        if (buffer && bitmap) {
            const auto previous = SelectObject(buffer, bitmap);
            // Publish EDIT's background and its hint in one blit, not two visible passes.
            SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(buffer), PRF_CLIENT | PRF_ERASEBKGND);
            complete = BitBlt(dc, 0, 0, bounds.right, bounds.bottom, buffer, 0, 0, SRCCOPY) != FALSE;
            SelectObject(buffer, previous);
        }
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        EndPaint(window, &paint);
        if (!complete) {
            try { throw std::runtime_error("Draw native search hint"); }
            catch (...) { self.report_failure(); }
        }
        return 0;
    }
    if (message == WM_PAINT || message == WM_PRINTCLIENT) {
        const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
        // Only the empty-field hint is custom. EDIT still owns text, caret, selection and IME.
        if (message == WM_PRINTCLIENT && !self.composing_ && GetWindowTextLengthW(window) == 0 &&
            !self.placeholder_.empty()) {
            HDC dc = reinterpret_cast<HDC>(wparam);
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
        const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
        self.composing_ = false;
        SendMessageW(GetParent(window), WM_COMMAND,
            MAKEWPARAM(GetDlgCtrlID(window), EN_CHANGE), reinterpret_cast<LPARAM>(window));
        return result;
    }
    if (message == WM_NCDESTROY) {
        self.suggestions_.reset();
        RemoveWindowSubclass(window, subclass, id);
        self.window_ = nullptr;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

}
