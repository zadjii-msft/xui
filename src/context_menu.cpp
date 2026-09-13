#include "context_menu.hpp"
#include "platform.hpp"
#include <commctrl.h>
#include <oleacc.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <algorithm>
#include <exception>
#include <limits>

namespace xui {
namespace {
constexpr wchar_t active_menu_property[] = L"Xui.ContextMenu.Active.1";
COLORREF color(D2D1_COLOR_F value) {
    return RGB(std::lround(value.r * 255), std::lround(value.g * 255), std::lround(value.b * 255));
}
struct MenuEntry {
    MSAAMENUINFO accessible{};
    std::wstring label, shortcut, name;
};
struct Menu {
    std::vector<MenuItem> items;
    std::vector<MenuEntry> entries;
    Palette palette;
    UINT dpi;
    HWND owner{}, root{}, popup{};
    HMENU handle{};
    HFONT font{};
    HBRUSH background{}, border{}, selection{};
    HHOOK hook{}, input_hook{};
    std::exception_ptr failure;
    int label_width{}, shortcut_width{}, row_height{};
    bool owner_attached{}, root_attached{}, marked{};
    // The CBT callback has no user-data parameter. This pointer exists only inside
    // TrackPopupMenuEx on this thread, and never supplies another window's palette.
    static thread_local Menu* creating;

    Menu(std::vector<MenuItem> value, HWND window, Palette colors, UINT scale)
        : items(std::move(value)), entries(items.size()), palette(colors), dpi(scale),
          owner(window), root(GetAncestor(window, GA_ROOT)) {}
    ~Menu() {
        if (input_hook) UnhookWindowsHookEx(input_hook);
        if (hook) UnhookWindowsHookEx(hook);
        if (creating == this) creating = nullptr;
        if (popup && IsWindow(popup)) RemoveWindowSubclass(popup, popup_proc, reinterpret_cast<UINT_PTR>(this));
        if (owner_attached && IsWindow(owner)) RemoveWindowSubclass(owner, owner_proc, reinterpret_cast<UINT_PTR>(this));
        if (root_attached && IsWindow(root)) RemoveWindowSubclass(root, owner_proc, reinterpret_cast<UINT_PTR>(this));
        if (marked && IsWindow(root)) RemovePropW(root, active_menu_property);
        if (handle) DestroyMenu(handle);
        if (font) DeleteObject(font);
        if (background) DeleteObject(background);
        if (border) DeleteObject(border);
        if (selection) DeleteObject(selection);
    }
    int px(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    void remember_failure() noexcept {
        if (!failure) failure = std::current_exception();
        EndMenu();
    }
    void initialize() {
        win32_require(items.size() < std::numeric_limits<UINT>::max(), "Create context menu commands");
        handle = CreatePopupMenu();
        win32_require(handle != nullptr, "Create context menu");
        background = CreateSolidBrush(color(palette.surface));
        border = CreateSolidBrush(color(palette.border));
        selection = CreateSolidBrush(color(palette.selection));
        font = CreateFontW(-px(static_cast<int>(VisualMetrics::body_size)), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        win32_require(background && border && selection && font, "Create context menu drawing resources");
        MENUINFO info{sizeof(info)};
        info.fMask = MIM_BACKGROUND | MIM_STYLE;
        info.hbrBack = background;
        info.dwStyle = MNS_NOCHECK;
        win32_require(SetMenuInfo(handle, &info) != FALSE, "Set context menu background");
        struct MeasureContext {
            HDC dc{};
            HGDIOBJ previous{};
            ~MeasureContext() {
                if (previous) SelectObject(dc, previous);
                if (dc) DeleteDC(dc);
            }
        } context{CreateCompatibleDC(nullptr)};
        const auto dc = context.dc;
        win32_require(dc != nullptr, "Measure context menu font");
        context.previous = SelectObject(dc, font);
        win32_require(context.previous && context.previous != HGDI_ERROR, "Select context menu font");
        TEXTMETRICW metrics{};
        const bool measured = GetTextMetricsW(dc, &metrics) != FALSE;
        row_height = std::max(px(32), static_cast<int>(metrics.tmHeight) + px(12));
        bool complete = measured;
        for (std::size_t i = 0; i < items.size(); ++i) {
            auto& entry = entries[i];
            const auto& item = items[i];
            const auto tab = item.text.find(L'\t');
            entry.label = item.text.substr(0, tab);
            if (tab != std::wstring::npos) entry.shortcut = item.text.substr(tab + 1);
            for (std::size_t j = 0; j < entry.label.size(); ++j) {
                if (entry.label[j] == L'&' && j + 1 < entry.label.size()) {
                    if (entry.label[j + 1] != L'&') continue;
                    ++j;
                }
                entry.name += entry.label[j];
            }
            entry.accessible.dwMSAASignature = MSAA_MENU_SIG;
            entry.accessible.cchWText = static_cast<DWORD>(entry.name.size());
            entry.accessible.pszWText = entry.name.data();
            RECT text{};
            if (!entry.label.empty()) {
                complete = DrawTextW(dc, entry.label.c_str(), static_cast<int>(entry.label.size()), &text,
                    DT_CALCRECT | DT_SINGLELINE) != 0 && complete;
                label_width = std::max(label_width, static_cast<int>(text.right));
            }
            text = {};
            if (!entry.shortcut.empty()) {
                complete = DrawTextW(dc, entry.shortcut.c_str(), static_cast<int>(entry.shortcut.size()), &text,
                    DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX) != 0 && complete;
                shortcut_width = std::max(shortcut_width, static_cast<int>(text.right));
            }
        }
        win32_require(complete, "Measure context menu text");
        for (UINT i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            MENUITEMINFOW entry{sizeof(entry)};
            entry.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_DATA | MIIM_STRING;
            entry.fType = MFT_OWNERDRAW | (item.separator ? MFT_SEPARATOR : 0);
            entry.fState = (item.enabled ? MFS_ENABLED : MFS_DISABLED) | (item.checked ? MFS_CHECKED : 0);
            entry.wID = i + 1;
            entry.dwItemData = reinterpret_cast<ULONG_PTR>(&entries[i]);
            entry.dwTypeData = items[i].text.data();
            win32_require(InsertMenuItemW(handle, i, TRUE, &entry) != FALSE, "Insert context menu command");
        }
        win32_require(SetWindowSubclass(owner, owner_proc, reinterpret_cast<UINT_PTR>(this),
            reinterpret_cast<DWORD_PTR>(this)) != FALSE, "Attach context menu owner");
        owner_attached = true;
        if (root != owner) {
            win32_require(SetWindowSubclass(root, owner_proc, reinterpret_cast<UINT_PTR>(this),
                reinterpret_cast<DWORD_PTR>(this)) != FALSE, "Attach context menu cancellation");
            root_attached = true;
        }
        win32_require(SetPropW(root, active_menu_property, this) != FALSE, "Register active context menu");
        marked = true;
        hook = SetWindowsHookExW(WH_CBT, hook_proc, nullptr, GetCurrentThreadId());
        win32_require(hook != nullptr, "Attach context menu frame");
        input_hook = SetWindowsHookExW(WH_MSGFILTER, input_proc, nullptr, GetCurrentThreadId());
        win32_require(input_hook != nullptr, "Attach context menu edge keys");
    }
    void draw(const DRAWITEMSTRUCT& draw) {
        if (!draw.itemID || draw.itemID > items.size()) return;
        const auto index = draw.itemID - 1;
        const auto& item = items[index];
        const auto& entry = entries[index];
        const HDC dc = draw.hDC;
        const int saved = SaveDC(dc);
        win32_require(saved != 0, "Save context menu drawing context");
        const auto rect = draw.rcItem;
        bool complete = FillRect(dc, &rect, background) != FALSE;
        if (item.separator) {
            RECT line{rect.left + px(12), (rect.top + rect.bottom) / 2, rect.right - px(12),
                (rect.top + rect.bottom) / 2 + std::max(1, px(1))};
            complete = FillRect(dc, &line, border) != FALSE && complete;
        } else {
            const bool selected = item.enabled && (draw.itemState & (ODS_SELECTED | ODS_HOTLIGHT));
            RECT highlight{rect.left + px(4), rect.top + px(2), rect.right - px(4), rect.bottom - px(2)};
            if (selected) {
                SelectObject(dc, selection);
                SelectObject(dc, GetStockObject(NULL_PEN));
                complete = RoundRect(dc, highlight.left, highlight.top, highlight.right, highlight.bottom,
                    palette.high_contrast ? 0 : px(8), palette.high_contrast ? 0 : px(8)) != FALSE && complete;
            }
            const auto ink = color(!item.enabled ? palette.disabled : selected ? palette.selection_text : palette.text);
            SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, ink);
            RECT label{rect.left + px(36), rect.top, rect.right - px(14) -
                (shortcut_width ? shortcut_width + px(28) : 0), rect.bottom};
            UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
            if (draw.itemState & ODS_NOACCEL) flags |= DT_HIDEPREFIX;
            if (!entry.label.empty()) complete = DrawTextW(dc, entry.label.c_str(),
                static_cast<int>(entry.label.size()), &label, flags) != 0 && complete;
            RECT shortcut{label.right + px(28), rect.top, rect.right - px(14), rect.bottom};
            SetTextColor(dc, color(!item.enabled ? palette.disabled : selected ? palette.selection_text : palette.secondary));
            if (!entry.shortcut.empty()) complete = DrawTextW(dc, entry.shortcut.c_str(),
                static_cast<int>(entry.shortcut.size()), &shortcut, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX) != 0 && complete;
            if (item.checked) {
                SelectObject(dc, GetStockObject(DC_PEN));
                SetDCPenColor(dc, ink);
                const int x = rect.left + px(14), y = (rect.top + rect.bottom) / 2;
                complete = MoveToEx(dc, x, y, nullptr) && complete;
                complete = LineTo(dc, x + px(4), y + px(4)) && complete;
                complete = LineTo(dc, x + px(11), y - px(4)) && complete;
            }
        }
        complete = RestoreDC(dc, saved) != FALSE && complete;
        win32_require(complete, "Draw context menu command");
    }
    void frame(HWND window, HDC dc) {
        RECT outer{}, client{};
        win32_require(GetWindowRect(window, &outer) && GetClientRect(window, &client), "Read context menu frame");
        POINT origin{};
        win32_require(ClientToScreen(window, &origin) != FALSE, "Locate context menu frame");
        OffsetRect(&client, origin.x - outer.left, origin.y - outer.top);
        OffsetRect(&outer, -outer.left, -outer.top);
        const int saved = SaveDC(dc);
        win32_require(saved != 0, "Save context menu frame context");
        ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
        const bool complete = FillRect(dc, &outer, background) && FrameRect(dc, &outer, border);
        const bool restored = RestoreDC(dc, saved) != FALSE;
        win32_require(complete && restored, "Draw context menu frame");
    }
    LRESULT character(wchar_t key) {
        if (key == 0xfffe || key == 0xffff) {
            for (std::size_t n = 0; n < items.size(); ++n) {
                const auto i = key == 0xfffe ? n : items.size() - n - 1;
                if (items[i].enabled && !items[i].separator) return MAKELRESULT(i, MNC_SELECT);
            }
            return MAKELRESULT(0, MNC_IGNORE);
        }
        key = static_cast<wchar_t>(towupper(key));
        std::vector<UINT> matches;
        bool mnemonic{};
        for (UINT i = 0; i < items.size(); ++i) {
            if (!items[i].enabled || items[i].separator) continue;
            const auto& label = entries[i].label;
            bool explicit_match{};
            for (std::size_t j = 0; j + 1 < label.size(); ++j) if (label[j] == L'&') {
                if (label[j + 1] == L'&') { ++j; continue; }
                if (towupper(label[j + 1]) == key) explicit_match = true;
            }
            if (explicit_match && !mnemonic) { matches.clear(); mnemonic = true; }
            if (explicit_match || (!mnemonic && !label.empty() && towupper(label.front()) == key)) matches.push_back(i);
        }
        if (matches.empty()) return MAKELRESULT(0, MNC_IGNORE);
        UINT selected = matches.front();
        for (const auto i : matches) if (GetMenuState(handle, i, MF_BYPOSITION) & MF_HILITE) {
            const auto found = std::find(matches.begin(), matches.end(), i);
            selected = std::next(found) == matches.end() ? matches.front() : *std::next(found);
            break;
        }
        return MAKELRESULT(selected, mnemonic && matches.size() == 1 ? MNC_EXECUTE : MNC_SELECT);
    }
    static LRESULT CALLBACK owner_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR id, DWORD_PTR data) noexcept {
        auto& self = *reinterpret_cast<Menu*>(data);
        try {
            if (message == WM_MEASUREITEM) {
                auto& item = *reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
                if (item.CtlType == ODT_MENU && item.itemID > 0 && item.itemID <= self.items.size()) {
                    item.itemWidth = self.px(50) + self.label_width +
                        (self.shortcut_width ? self.shortcut_width + self.px(28) : 0);
                    item.itemHeight = self.items[item.itemID - 1].separator ? self.px(9) : self.row_height;
                    return TRUE;
                }
            }
            if (message == WM_DRAWITEM) {
                auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
                if (item.CtlType == ODT_MENU && reinterpret_cast<HMENU>(item.hwndItem) == self.handle) {
                    self.draw(item); return TRUE;
                }
            }
            if (message == WM_MENUCHAR && reinterpret_cast<HMENU>(lparam) == self.handle)
                return self.character(LOWORD(wparam));
            if (message == WM_CANCELMODE || message == WM_THEMECHANGED || message == WM_SYSCOLORCHANGE ||
                message == WM_SETTINGCHANGE || message == WM_DPICHANGED || message == WM_DISPLAYCHANGE ||
                message == WM_DESTROY || (message == WM_ENABLE && !wparam) ||
                (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE)) EndMenu();
            if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, owner_proc, id);
        } catch (...) { self.remember_failure(); return 0; }
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    static LRESULT CALLBACK popup_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR id, DWORD_PTR data) noexcept {
        auto& self = *reinterpret_cast<Menu*>(data);
        try {
            if (message == WM_NCPAINT) {
                const HDC dc = GetWindowDC(hwnd);
                win32_require(dc != nullptr, "Acquire context menu frame context");
                try { self.frame(hwnd, dc); }
                catch (...) { ReleaseDC(hwnd, dc); throw; }
                win32_require(ReleaseDC(hwnd, dc) != 0, "Release context menu frame context");
                return 0;
            }
            if (message == WM_PRINT) {
                const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
                if (lparam & PRF_NONCLIENT) self.frame(hwnd, reinterpret_cast<HDC>(wparam));
                return result;
            }
            if (message == WM_NCDESTROY) {
                self.popup = nullptr;
                RemoveWindowSubclass(hwnd, popup_proc, id);
            }
        } catch (...) { self.remember_failure(); return 0; }
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    static LRESULT CALLBACK hook_proc(int code, WPARAM wparam, LPARAM lparam) noexcept {
        auto* self = creating;
        if (self && code == HCBT_CREATEWND && !self->popup) {
            const HWND hwnd = reinterpret_cast<HWND>(wparam);
            wchar_t name[32]{};
            if (GetClassNameW(hwnd, name, 32) && std::wstring_view(name) == L"#32768") {
                try {
                    win32_require(SetWindowSubclass(hwnd, popup_proc, reinterpret_cast<UINT_PTR>(self),
                        reinterpret_cast<DWORD_PTR>(self)) != FALSE, "Attach context menu frame drawing");
                    self->popup = hwnd;
                    hr_require(SetWindowTheme(hwnd, L"", L""), "Disable default context menu theme");
                } catch (...) { self->remember_failure(); return 1; }
            }
        }
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }
    static LRESULT CALLBACK input_proc(int code, WPARAM wparam, LPARAM lparam) noexcept {
        if (creating && code == MSGF_MENU) {
            auto& message = *reinterpret_cast<MSG*>(lparam);
            if (message.message == WM_KEYDOWN && (message.wParam == VK_HOME || message.wParam == VK_END)) {
                // Route edge keys through the documented WM_MENUCHAR/MNC_SELECT
                // protocol, so Windows keeps its own selection and UIA focus.
                message.message = WM_CHAR;
                message.wParam = message.wParam == VK_HOME ? 0xfffe : 0xffff;
            }
        }
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }
};
thread_local Menu* Menu::creating{};
}

void cancel_control_menu(HWND root) {
    if (GetPropW(root, active_menu_property)) EndMenu();
}

void show_control_menu(Control& control, HWND window, LPARAM position, const Palette& palette, UINT dpi) {
    if (!IsWindow(window) || !control.enabled()) return;
    if (Menu::creating) return;
    const HWND root = GetAncestor(window, GA_ROOT);
    if (!IsWindowEnabled(root) || !IsWindowEnabled(window) || GetPropW(root, active_menu_property)) return;
    const HWND previous_focus = GetFocus();
    // Cancel pending suggestions as well as visible popups before the factory runs.
    SendMessageW(root, WM_CANCELMODE, 0, 0);
    auto items = control.context_menu();
    if (items.empty() || !IsWindow(window) || !IsWindow(root)) return;
    POINT point{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
    if (point.x == -1 && point.y == -1) {
        point = {MulDiv(12, dpi, 96), MulDiv(12, dpi, 96)};
        win32_require(ClientToScreen(window, &point) != FALSE, "Locate context menu");
    }
    std::function<void()> action;
    {
        Menu menu(std::move(items), window, palette, dpi);
        menu.initialize();
        Menu::creating = &menu;
        SetLastError(ERROR_SUCCESS);
        const UINT chosen = TrackPopupMenuEx(menu.handle, TPM_RETURNCMD | TPM_RIGHTBUTTON,
            point.x, point.y, window, nullptr);
        const DWORD error = GetLastError();
        if (menu.failure) std::rethrow_exception(menu.failure);
        if (!chosen && error != ERROR_SUCCESS) {
            SetLastError(error);
            win32_require(false, "Show context menu");
        }
        if (chosen && chosen <= menu.items.size()) {
            auto& item = menu.items[chosen - 1];
            if (item.enabled && !item.separator) action = std::move(item.action);
        }
    }
    if (!IsWindow(window) || !IsWindow(root) || !IsWindowEnabled(window) || !IsWindowEnabled(root)) return;
    if (previous_focus && IsWindow(previous_focus) && IsChild(root, previous_focus) &&
        IsWindowEnabled(previous_focus) && GetForegroundWindow() == root && GetFocus() != previous_focus)
        SetFocus(previous_focus);
    // No menu, HWND, control, or item access after the copied command starts.
    if (action) action();
}
}
