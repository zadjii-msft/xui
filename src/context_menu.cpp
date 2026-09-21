#include "context_menu.hpp"
#include "platform.hpp"
#include "xui/shell_commands.hpp"
#include "shell_commands_internal.hpp"
#include <commctrl.h>
#include <oleacc.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <exception>
#include <iterator>
#include <limits>

namespace xui {
std::atomic<ContextMenuTestAccess::Track> ContextMenuTestAccess::track{};
std::atomic<ContextMenuTestAccess::Paint> ContextMenuTestAccess::paint{};
namespace {
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
    HRGN outline{};
    SIZE frame_size{};
    HHOOK hook{}, input_hook{};
    std::exception_ptr failure;
    std::function<bool()> current;
    std::function<bool()> refresh;
    int label_width{}, shortcut_width{}, row_height{}, icon_offset{};
    bool owner_attached{}, root_attached{}, marked{}, timer{}, refreshing{}, cancelled{}, painted{};
    // The CBT callback has no user-data parameter. This pointer exists only inside
    // TrackPopupMenuEx on this thread, and never supplies another window's palette.
    static thread_local Menu* creating;

    Menu(std::vector<MenuItem> value, HWND window, Palette colors, UINT scale, std::function<bool()> valid)
        : items(std::move(value)), entries(items.size()), palette(colors), dpi(scale),
          owner(window), root(GetAncestor(window, GA_ROOT)), current(std::move(valid)) {}
    ~Menu() {
        if (timer) KillTimer(owner, reinterpret_cast<UINT_PTR>(this));
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
        if (outline) DeleteObject(outline);
    }
    int px(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    void remember_failure() noexcept {
        if (!failure) failure = std::current_exception();
        EndMenu();
    }
    void initialize() {
        win32_require(items.size() < std::numeric_limits<UINT>::max(), "Create context menu commands");
        const bool icons = std::any_of(items.begin(), items.end(), [](const auto& item) { return !item.separator && item.icon; });
        if (icons && std::any_of(items.begin(), items.end(), [](const auto& item) { return !item.separator && item.checked; }))
            icon_offset = px(22);
        for (const auto& item : items) if (item.icon) {
            const auto& icon = *item.icon;
            if (icon.width > 64 || icon.height > 64 || (icon.width == 0) != (icon.height == 0) ||
                icon.pixels.size() != std::size_t(icon.width) * icon.height)
                throw std::invalid_argument("Invalid context menu icon");
        }
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
        if (current || refresh) {
            win32_require(SetTimer(owner, reinterpret_cast<UINT_PTR>(this), 16, nullptr) != 0,
                "Observe context menu completion");
            timer = true;
        }
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
            RECT label{rect.left + px(36) + icon_offset, rect.top, rect.right - px(14) -
                (shortcut_width ? shortcut_width + px(28) : 0), rect.bottom};
            UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
            if (draw.itemState & ODS_NOACCEL) flags |= DT_HIDEPREFIX;
            if (!entry.label.empty()) complete = DrawTextW(dc, entry.label.c_str(),
                static_cast<int>(entry.label.size()), &label, flags) != 0 && complete;
            RECT shortcut{label.right + px(28), rect.top, rect.right - px(14), rect.bottom};
            SetTextColor(dc, color(!item.enabled ? palette.disabled : selected ? palette.selection_text : palette.secondary));
            if (!entry.shortcut.empty()) complete = DrawTextW(dc, entry.shortcut.c_str(),
                static_cast<int>(entry.shortcut.size()), &shortcut, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX) != 0 && complete;
            if (item.icon) {
                const int x = rect.left + px(12) + icon_offset, y = (rect.top + rect.bottom - px(16)) / 2;
                const auto& icon = *item.icon;
                if (item.enabled && !palette.high_contrast && !icon.pixels.empty()) {
                    const auto backdrop = color(selected ? palette.selection : palette.surface);
                    std::array<std::uint32_t, 64 * 64> pixels;
                    std::copy(icon.pixels.begin(), icon.pixels.end(), pixels.begin());
                    for (std::size_t i = 0; i < icon.pixels.size(); ++i) {
                        auto& pixel = pixels[i];
                        const auto inverse = 255 - (pixel >> 24);
                        const auto blend = [inverse](auto channel, auto background) {
                            return std::min(255u, channel + (background * inverse + 127) / 255);
                        };
                        pixel = blend(pixel & 255, GetBValue(backdrop)) |
                            (blend((pixel >> 8) & 255, GetGValue(backdrop)) << 8) |
                            (blend((pixel >> 16) & 255, GetRValue(backdrop)) << 16);
                    }
                    BITMAPINFO info{};
                    info.bmiHeader = {sizeof(BITMAPINFOHEADER), static_cast<LONG>(icon.width),
                        -static_cast<LONG>(icon.height), 1, 32, BI_RGB};
                    const auto extent = std::max(icon.width, icon.height);
                    const int width = std::max(1, MulDiv(px(16), icon.width, extent));
                    const int height = std::max(1, MulDiv(px(16), icon.height, extent));
                    complete = StretchDIBits(dc, x + (px(16) - width) / 2, y + (px(16) - height) / 2, width, height,
                        0, 0, icon.width, icon.height, pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY) > 0 && complete;
                } else {
                    SelectObject(dc, GetStockObject(DC_PEN));
                    SelectObject(dc, GetStockObject(NULL_BRUSH));
                    SetDCPenColor(dc, ink);
                    complete = Rectangle(dc, x + px(2), y + px(1), x + px(14), y + px(15)) != FALSE && complete;
                    for (int line : {5, 8, 11}) {
                        complete = MoveToEx(dc, x + px(5), y + px(line), nullptr) != FALSE && complete;
                        complete = LineTo(dc, x + px(11), y + px(line)) != FALSE && complete;
                    }
                }
            }
            if (item.checked) {
                SelectObject(dc, GetStockObject(DC_PEN));
                SetDCPenColor(dc, ink);
                const int x = rect.left + px(14), y = (rect.top + rect.bottom) / 2;
                complete = MoveToEx(dc, x, y, nullptr) && complete;
                complete = LineTo(dc, x + px(4), y + px(4)) && complete;
                complete = LineTo(dc, x + px(11), y - px(4)) && complete;
            }
        }
        if (popup && outline) {
            // Native menus can buffer item drawing. Keep the curved border in that same buffer.
            RECT bounds{};
            POINT origin{};
            if (GetWindowRect(popup, &bounds) && ClientToScreen(popup, &origin)) {
                complete = IntersectClipRect(dc, rect.left, rect.top, rect.right, rect.bottom) != ERROR && complete;
                complete = OffsetViewportOrgEx(dc, bounds.left - origin.x, bounds.top - origin.y, nullptr) != FALSE && complete;
                complete = FrameRgn(dc, outline, border, 1, 1) != FALSE && complete;
            } else complete = false;
        }
        complete = RestoreDC(dc, saved) != FALSE && complete;
        win32_require(complete, "Draw context menu command");
        painted = true;
        if (const auto observer = ContextMenuTestAccess::paint.load()) observer(handle, owner);
    }
    void shape(HWND window) {
        RECT bounds{};
        win32_require(GetWindowRect(window, &bounds) != FALSE, "Read context menu size");
        const SIZE size{bounds.right - bounds.left, bounds.bottom - bounds.top};
        if (size.cx <= 0 || size.cy <= 0 ||
            (size.cx == frame_size.cx && size.cy == frame_size.cy)) return;
        struct Region {
            HRGN handle{};
            ~Region() { if (handle) DeleteObject(handle); }
        };
        const int diameter = palette.high_contrast ? 0 : px(16);
        Region next{diameter ? CreateRoundRectRgn(0, 0, size.cx + 1, size.cy + 1, diameter, diameter) :
            CreateRectRgn(0, 0, size.cx, size.cy)};
        Region clip{CreateRectRgn(0, 0, 0, 0)};
        win32_require(next.handle && clip.handle, "Create context menu outline");
        win32_require(CombineRgn(clip.handle, next.handle, nullptr, RGN_COPY) != ERROR, "Copy context menu outline");
        frame_size = size; // SetWindowRgn can send another WM_WINDOWPOSCHANGED.
        win32_require(SetWindowRgn(window, clip.handle, FALSE) != 0, "Set context menu outline");
        clip.handle = nullptr; // Windows owns the region after SetWindowRgn succeeds.
        if (outline) DeleteObject(outline);
        outline = next.handle;
        next.handle = nullptr;
        win32_require(RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME) != FALSE,
            "Invalidate context menu outline");
    }
    void paint_frame(HWND window) {
        const HDC dc = GetWindowDC(window);
        win32_require(dc != nullptr, "Acquire context menu frame context");
        try { frame(window, dc); }
        catch (...) { ReleaseDC(window, dc); throw; }
        win32_require(ReleaseDC(window, dc) != 0, "Release context menu frame context");
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
        const bool filled = FillRect(dc, &outer, background) != FALSE;
        const bool restored = RestoreDC(dc, saved) != FALSE;
        // The curved border can cross into the client area at higher DPI.
        const bool edged = outline ? FrameRgn(dc, outline, border, 1, 1) != FALSE :
            FrameRect(dc, &outer, border) != FALSE;
        win32_require(filled && restored && edged, "Draw context menu frame");
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
            if ((message == WM_ENTERIDLE || (message == WM_TIMER && wparam == id)) &&
                self.current && !self.current()) { self.cancelled = true; EndMenu(); }
            if (message == WM_TIMER && wparam == id) {
                bool interacting = GetKeyState(VK_LBUTTON) < 0 || GetKeyState(VK_RBUTTON) < 0;
                for (UINT i = 0; !interacting && i < self.items.size(); ++i)
                    interacting = (GetMenuState(self.handle, i, MF_BYPOSITION) & MF_HILITE) != 0;
                const bool ready = !self.cancelled && self.painted && self.refresh && self.refresh();
                if (!interacting && ready) { self.refreshing = true; EndMenu(); }
                return 0;
            }
            if (message == WM_MEASUREITEM) {
                auto& item = *reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
                if (item.CtlType == ODT_MENU && item.itemID > 0 && item.itemID <= self.items.size()) {
                    item.itemWidth = self.px(50) + self.icon_offset + self.label_width +
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
                (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE)) { self.cancelled = true; EndMenu(); }
            if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, owner_proc, id);
        } catch (...) { self.remember_failure(); return 0; }
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    static LRESULT CALLBACK popup_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR id, DWORD_PTR data) noexcept {
        auto& self = *reinterpret_cast<Menu*>(data);
        try {
            if (message == WM_WINDOWPOSCHANGED) {
                const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
                self.shape(hwnd);
                return result;
            }
            if (message == WM_NCPAINT) {
                self.paint_frame(hwnd);
                return 0;
            }
            if (message == WM_PRINT) {
                const auto dc = reinterpret_cast<HDC>(wparam);
                const int saved = SaveDC(dc);
                win32_require(saved != 0, "Save context menu print context");
                if (self.outline && ExtSelectClipRgn(dc, self.outline, RGN_AND) == ERROR) {
                    RestoreDC(dc, saved);
                    win32_require(false, "Clip context menu print outline");
                }
                const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
                try { if (lparam & PRF_NONCLIENT) self.frame(hwnd, dc); }
                catch (...) { RestoreDC(dc, saved); throw; }
                win32_require(RestoreDC(dc, saved) != FALSE, "Restore context menu print context");
                return result;
            }
            if (message == WM_PAINT) {
                const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
                self.paint_frame(hwnd);
                return result;
            }
            if (message == WM_NCDESTROY) {
                self.popup = nullptr;
                self.frame_size = {};
                if (self.outline) { DeleteObject(self.outline); self.outline = nullptr; }
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
            try {
                if (creating->current && !creating->current()) {
                    creating->cancelled = true;
                    EndMenu();
                    // The native loop must receive its cancellation wakeup.
                    return CallNextHookEx(nullptr, code, wparam, lparam);
                }
            } catch (...) { creating->remember_failure(); return CallNextHookEx(nullptr, code, wparam, lparam); }
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
thread_local bool shell_waiting{};

// A native menu pumps window messages, not Window.Post's outer event queue.
// Completion therefore uses a window timer; no posted callback retains the control.
struct ShellWait {
    HWND root;
    const void* identity;
    const std::function<bool()>& current;
    std::exception_ptr failure;
    bool cancelled{};
    ShellWait(HWND window, const void* request, const std::function<bool()>& valid)
        : root(window), identity(request), current(valid) {
        if (shell_waiting) throw std::logic_error("A Shell action is already pending");
        win32_require(SetWindowSubclass(root, proc, reinterpret_cast<UINT_PTR>(this),
            reinterpret_cast<DWORD_PTR>(this)) != FALSE, "Observe Shell action cancellation");
        if (!SetPropW(root, active_menu_property, this)) {
            RemoveWindowSubclass(root, proc, reinterpret_cast<UINT_PTR>(this));
            win32_require(false, "Guard Shell action");
        }
        shell_waiting = true;
    }
    ~ShellWait() {
        shell_waiting = false;
        if (IsWindow(root)) {
            RemoveWindowSubclass(root, proc, reinterpret_cast<UINT_PTR>(this));
            if (GetPropW(root, active_menu_property) == this) RemovePropW(root, active_menu_property);
        }
    }
    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) noexcept {
        auto& self = *reinterpret_cast<ShellWait*>(data);
        if (message == shell_validation_message()) {
            if (w != reinterpret_cast<WPARAM>(self.identity) || self.cancelled) return 0;
            try { return self.current() ? 1 : 0; }
            catch (...) { self.failure = std::current_exception(); self.cancelled = true; return 0; }
        }
        if (message == WM_CANCELMODE || message == WM_DESTROY || (message == WM_ENABLE && !w) ||
            message == WM_THEMECHANGED || message == WM_SYSCOLORCHANGE || message == WM_SETTINGCHANGE ||
            message == WM_DPICHANGED || message == WM_DISPLAYCHANGE)
            self.cancelled = true;
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, proc, id);
        return DefSubclassProc(hwnd, message, w, l);
    }
};
bool wait_shell(const std::shared_ptr<AsyncShellMenu>& request, HWND root, const std::function<bool()>& current) {
    ShellWait wait(root, request->identity(), current);
    while (!request->finished()) {
        if (wait.failure) std::rethrow_exception(wait.failure);
        if (wait.cancelled || !current()) { request->cancel(); return false; }
        MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(message.wParam)); request->cancel(); return false;
            }
            TranslateMessage(&message); DispatchMessageW(&message);
            if (wait.failure) std::rethrow_exception(wait.failure);
            if (wait.cancelled || !current()) { request->cancel(); return false; }
        }
    }
    return !wait.cancelled && current();
}
struct ShellSnapshot final : ShellCommandProvider {
    std::shared_ptr<AsyncShellMenu> request;
    std::function<void(ItemKey)> action;
    std::vector<ShellCommandInfo> discover(std::stop_token) override {
        if (const auto error = request->discovery_error(); !error.empty()) throw std::runtime_error(error);
        return request->commands();
    }
    void invoke(ItemKey key) override { action(key); }
};
}

void cancel_control_menu(HWND root) {
    if (GetPropW(root, active_menu_property)) SendMessageW(root, WM_CANCELMODE, 0, 0);
}

void show_control_menu(Control& control, HWND window, LPARAM position, const Palette& palette, UINT dpi) {
    if (!IsWindow(window) || !control.enabled()) return;
    if (Menu::creating || shell_waiting) return;
    const HWND root = GetAncestor(window, GA_ROOT);
    if (!IsWindowEnabled(root) || !IsWindowEnabled(window) || GetPropW(root, active_menu_property)) return;
    const HWND previous_focus = GetFocus();
    // Cancel pending suggestions as well as visible popups before the factory runs.
    SendMessageW(root, WM_CANCELMODE, 0, 0);
    auto content = control.context_menu_content();
    if (!IsWindow(window) || !IsWindow(root) || (content.current && !content.current())) return;
    if (content.items.empty() && content.shell_paths.empty()) return;
    POINT point{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
    if (point.x == -1 && point.y == -1) {
        point = {MulDiv(12, dpi, 96), MulDiv(12, dpi, 96)};
        win32_require(ClientToScreen(window, &point) != FALSE, "Locate context menu");
    }
    std::shared_ptr<AsyncShellMenu> shell;
    std::function<void()> populate;
    const auto apps = content.items;
    if (!content.shell_paths.empty()) {
        if (content.presentation == ShellMenuPresentation::xui) {
            if (apps.size() > CommandSet::maximum_commands - 4)
                throw std::length_error("Too many app commands for a custom Shell menu");
            const Point location{static_cast<float>(point.x), static_cast<float>(point.y)};
            const auto current = [root, window, valid = content.current] {
                return IsWindow(root) && IsWindow(window) && IsWindowEnabled(root) && IsWindowEnabled(window) && (!valid || valid());
            };
            shell = AsyncShellMenu::prepare(root, std::move(content.shell_paths));
            const auto fallback = [shell, root, location, apps, current] {
                if (!current()) return;
                shell->windows_menu(location, apps);
                if (wait_shell(shell, root, current)) {
                    const auto selected = shell->result();
                    if (selected && *selected < apps.size() && current() && apps[*selected].action)
                        apps[*selected].action();
                }
            };
            auto immediate = apps;
            for (auto& item : immediate) if (item.action) {
                item.action = [shell, current, action = std::move(item.action)] {
                    if (!current()) return;
                    shell->cancel();
                    action();
                };
            }
            if (!immediate.empty()) immediate.push_back({L"", {}, false, false, true});
            immediate.push_back({L"Show Windows menu...", fallback});
            populate = [&, shell, current, fallback, immediate] {
                auto provider = std::make_shared<ShellSnapshot>();
                provider->request = shell;
                provider->action = [shell, root, current](ItemKey key) {
                    if (!current()) return;
                    shell->invoke(key);
                    if (wait_shell(shell, root, current)) shell->result();
                };
                auto model = std::make_shared<CustomShellMenu>(std::move(provider), apps, current, fallback);
                if (!model->discovery_error().empty()) {
                    OutputDebugStringA(("XUI Shell discovery: " + model->discovery_error() + "\n").c_str());
                }
                auto discovered = model->menu_items();
                discovered.pop_back(); // The explicit fallback already has a stable position.
                if (!discovered.empty() && discovered.back().separator) discovered.pop_back();
                discovered.resize(discovered.size() - apps.size());
                if (!discovered.empty() && discovered.back().separator) discovered.pop_back();
                content.items = immediate;
                if (!discovered.empty()) {
                    content.items.push_back({L"", {}, false, false, true});
                    content.items.insert(content.items.end(), std::make_move_iterator(discovered.begin()),
                        std::make_move_iterator(discovered.end()));
                }
                content.current = [model] { return model->current(); };
            };
            content.current = current;
            content.items = std::move(immediate);
            content.items.push_back({L"Loading Windows commands...", {}, false});
        } else {
            track_shell_commands(root, content.shell_paths, {static_cast<float>(point.x), static_cast<float>(point.y)},
                content.items, std::move(content.current));
            return;
        }
    }
    std::function<void()> action;
    for (;;) {
        Menu menu(std::move(content.items), window, palette, dpi, content.current);
        if (populate) menu.refresh = [shell] { shell->begin(); return shell->ready(); };
        menu.initialize();
        Menu::creating = &menu;
        SetLastError(ERROR_SUCCESS);
        const auto track = ContextMenuTestAccess::track.load();
        const UINT chosen = track ? track(menu.handle, window, {static_cast<float>(point.x), static_cast<float>(point.y)}) :
            TrackPopupMenuEx(menu.handle, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NOANIMATION, point.x, point.y, window, nullptr);
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
        if (!chosen && menu.refreshing && !menu.cancelled && (!content.current || content.current())) {
            // End and rebuild the HMENU so Windows recomputes its dimensions and
            // accessibility children. Never mutate owner-draw storage mid-paint.
            populate(); populate = {};
            continue;
        }
        break;
    }
    if (!IsWindow(window) || !IsWindow(root) || !IsWindowEnabled(window) || !IsWindowEnabled(root)) return;
    if (previous_focus && IsWindow(previous_focus) && IsChild(root, previous_focus) &&
        IsWindowEnabled(previous_focus) && GetForegroundWindow() == root && GetFocus() != previous_focus)
        SetFocus(previous_focus);
    // No menu, HWND, control, or item access after the copied command starts.
    if (action && (!content.current || content.current())) action();
}
}
