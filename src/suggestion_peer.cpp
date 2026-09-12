#include "suggestion_peer.hpp"
#include "xui/native_edit.hpp"
#include "xui/controls.hpp"
#include <windowsx.h>
#include <algorithm>

namespace xui {
SuggestionPeer::SuggestionPeer(NativeEditBridge& edit, TextInput& input)
    : edit_(edit), input_(input), revision_(input.suggestion_revision()),
      delivery_(std::make_shared<detail::SuggestionDelivery>()) {
    delivery_->window = edit.window();
}
SuggestionPeer::~SuggestionPeer() {
    dismiss();
    delivery_->revoke();
    if (popup_) DestroyWindow(popup_);
    if (font_) DeleteObject(font_);
    if (background_) DeleteObject(background_);
}
void SuggestionPeer::set_colors(COLORREF background, COLORREF text, COLORREF secondary) {
    if (background_color_ == background && text_color_ == text && secondary_color_ == secondary) return;
    HBRUSH brush = CreateSolidBrush(background);
    if (!brush) return;
    if (background_) DeleteObject(background_);
    background_ = brush;
    background_color_ = background; text_color_ = text; secondary_color_ = secondary;
    if (popup_) RedrawWindow(popup_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
bool SuggestionPeer::eligible() const {
    return input_.suggestions() && GetFocus() == edit_.window() &&
        IsWindowVisible(edit_.window()) && IsWindowEnabled(edit_.window()) &&
        !(GetWindowLongPtrW(edit_.window(), GWL_STYLE) & ES_READONLY) && !edit_.composing();
}
void SuggestionPeer::dismiss() {
    KillTimer(edit_.window(), timer_id);
    wanted_ = false;
    delivery_->cancel();
    items_.clear();
    clicked_ = -1;
    if (popup_) {
        ShowWindow(popup_, SW_HIDE);
        SendMessageW(list_, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    }
}
void SuggestionPeer::sync() {
    if (revision_ != input_.suggestion_revision() || !eligible()) dismiss();
    revision_ = input_.suggestion_revision();
}
void SuggestionPeer::changed() {
    if (replacing_) return;
    sync();
    dismiss();
    if (!eligible() || input_.text().empty()) return;
    wanted_ = true;
    explicit_ = false;
    SetTimer(edit_.window(), timer_id, 80, nullptr);
}
void SuggestionPeer::timer() {
    KillTimer(edit_.window(), timer_id);
    sync();
    if (!wanted_ || !eligible()) return;
    if (!worker_) worker_ = detail::SuggestionWorker::shared();
    show({{}, L"Loading suggestions\u2026"});
    try {
        worker_->request(input_.suggestions(), {input_.text(), input_.suggestion_context(), explicit_}, delivery_);
    } catch (...) {
        show({{}, L"Suggestions are unavailable."});
    }
}
void SuggestionPeer::deliver() {
    sync();
    auto result = delivery_->take();
    if (result && wanted_ && eligible()) show(std::move(*result));
}
void SuggestionPeer::show(SuggestionResult result) {
    if (!popup_) {
        static const auto atom = [] {
            WNDCLASSW cls{};
            cls.lpfnWndProc = DefWindowProcW;
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.lpszClassName = L"Xui.Suggestions.1";
            cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            cls.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
            return RegisterClassW(&cls);
        }();
        if (!atom) return;
        const auto name = input_.name() + L" suggestions";
        popup_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"Xui.Suggestions.1", name.c_str(), WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
            0, 0, 1, 1, GetAncestor(edit_.window(), GA_ROOT), nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!popup_) return;
        list_ = CreateWindowExW(WS_EX_NOACTIVATE, L"LISTBOX", name.c_str(),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS,
            0, 0, 1, 1, popup_, nullptr, GetModuleHandleW(nullptr), nullptr);
        status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT | SS_NOPREFIX,
            0, 0, 1, 1, popup_, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!list_ || !status_ || !SetWindowSubclass(list_, procedure, 1, reinterpret_cast<DWORD_PTR>(this))) {
            DestroyWindow(popup_); popup_ = list_ = status_ = nullptr; return;
        }
        if (!SetWindowSubclass(popup_, procedure, 1, reinterpret_cast<DWORD_PTR>(this))) {
            DestroyWindow(popup_); popup_ = list_ = status_ = nullptr; return;
        }
    }
    const auto dpi = GetDpiForWindow(edit_.window());
    if (!font_ || font_dpi_ != dpi) {
        HFONT font = CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (font) {
            SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            if (font_) DeleteObject(font_);
            font_ = font; font_dpi_ = dpi;
        }
    }
    items_ = std::move(result.items);
    std::erase_if(items_, [&](const auto& value) { return value.size() > input_.maximum_length(); });
    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    for (const auto& item : items_) SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    if (result.status.empty() && items_.empty()) result.status = L"No suggestions.";
    SetWindowTextW(status_, result.status.c_str());
    SendMessageW(list_, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    RECT anchor = edit_.suggestion_anchor();
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    const int row = MulDiv(26, dpi, 96);
    SendMessageW(list_, LB_SETITEMHEIGHT, 0, row);
    const int width = std::min(std::max(static_cast<int>(anchor.right - anchor.left), MulDiv(320, dpi, 96)),
        static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    const int footer = result.status.empty() ? 0 : row * 2;
    const int height = std::min(row * static_cast<int>(std::min<std::size_t>(items_.size(), 8)) + footer + 2,
        static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
    const int x = std::clamp(static_cast<int>(anchor.left), static_cast<int>(monitor.rcWork.left),
        static_cast<int>(monitor.rcWork.right) - width);
    const int y = std::clamp(anchor.bottom + height <= monitor.rcWork.bottom ? static_cast<int>(anchor.bottom) :
        static_cast<int>(anchor.top) - height, static_cast<int>(monitor.rcWork.top),
        static_cast<int>(monitor.rcWork.bottom) - height);
    SetWindowPos(popup_, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetWindowPos(list_, nullptr, 0, 0, width - 2, std::max(0, height - 2 - footer),
        SWP_NOZORDER | SWP_NOACTIVATE | (items_.empty() ? SWP_HIDEWINDOW : SWP_SHOWWINDOW));
    SetWindowPos(status_, nullptr, 8, std::max(0, height - 2 - footer) + 4, width - 18, std::max(0, footer - 4),
        SWP_NOZORDER | SWP_NOACTIVATE | (footer ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list_, nullptr, TRUE);
    InvalidateRect(popup_, nullptr, TRUE);
}
void SuggestionPeer::accept(bool submit) {
    const auto row = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
    if (row < 0 || static_cast<std::size_t>(row) >= items_.size()) return;
    const auto text = items_[row];
    dismiss();
    replacing_ = true;
    SendMessageW(edit_.window(), EM_SETSEL, 0, -1);
    SendMessageW(edit_.window(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
    replacing_ = false;
    if (submit) input_.submit();
}
bool SuggestionPeer::key(WPARAM key) {
    sync();
    if (!eligible() || (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) return false;
    if (key == VK_ESCAPE && wanted_) { dismiss(); return true; }
    if (key == VK_DOWN && !wanted_) {
        wanted_ = explicit_ = true;
        timer();
        return true;
    }
    if (!wanted_) return false;
    if (key == VK_DOWN || key == VK_UP) {
        if (!items_.empty()) {
            int row = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
            row = key == VK_DOWN ? std::min(row + 1, static_cast<int>(items_.size()) - 1) : std::max(row - 1, 0);
            SendMessageW(list_, LB_SETCURSEL, row, 0);
            NotifyWinEvent(EVENT_OBJECT_SELECTION, list_, OBJID_CLIENT, row + 1);
        }
        return true;
    }
    if (key == VK_RETURN || key == VK_TAB) {
        const bool selected = !items_.empty() && popup_ && IsWindowVisible(popup_) &&
            SendMessageW(list_, LB_GETCURSEL, 0, 0) != LB_ERR &&
            !(GetKeyState(VK_SHIFT) & 0x8000);
        if (selected) { accept(key == VK_RETURN); return true; }
        dismiss();
    }
    return false;
}
LRESULT CALLBACK SuggestionPeer::procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR id, DWORD_PTR data) noexcept {
    auto& self = *reinterpret_cast<SuggestionPeer*>(data);
    auto& edit = self.edit_;
    try {
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (window == self.popup_ && message == WM_COMMAND && HIWORD(wparam) == LBN_DBLCLK &&
            self.wanted_) { self.accept(true); return 0; }
        if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORLISTBOX) {
            const auto dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, self.background_ ? (message == WM_CTLCOLORSTATIC ? self.secondary_color_ : self.text_color_) :
                GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(dc, self.background_ ? self.background_color_ : GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(self.background_ ? self.background_ : GetSysColorBrush(COLOR_WINDOW));
        }
        if (window == self.popup_ && message == WM_ERASEBKGND && self.background_) {
            RECT rect{}; GetClientRect(window, &rect);
            FillRect(reinterpret_cast<HDC>(wparam), &rect, self.background_);
            return 1;
        }
        if (window == self.list_ && message == WM_SETFOCUS) {
            SetFocus(self.edit_.window());
            return 0;
        }
        if (window == self.list_ &&
            (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK)) {
            const auto hit = SendMessageW(window, LB_ITEMFROMPOINT, 0, lparam);
            const int row = HIWORD(hit) ? -1 : LOWORD(hit);
            if (message == WM_LBUTTONDOWN) {
                self.clicked_ = row;
                if (row >= 0 && static_cast<std::size_t>(row) < self.items_.size()) {
                    SendMessageW(window, LB_SETCURSEL, row, 0);
                    NotifyWinEvent(EVENT_OBJECT_SELECTION, window, OBJID_CLIENT, row + 1);
                }
            } else if (self.wanted_ && row >= 0 &&
                ((message == WM_LBUTTONUP && row == self.clicked_) || message == WM_LBUTTONDBLCLK)) self.accept(true);
            return 0;
        }
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(window, procedure, id);
            if (window == self.popup_) self.popup_ = nullptr;
            if (window == self.list_) self.list_ = nullptr;
        }
    } catch (...) {
        if (IsWindow(window)) self.dismiss();
        edit.report_failure();
        return 0;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}
}
