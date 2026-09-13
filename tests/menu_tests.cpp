#include "xui/application.hpp"
#include "xui/suggestions.hpp"
#include "../src/context_menu.hpp"
#include "suggestion_capture.hpp"
#include <UIAutomation.h>
#include <oleacc.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

using Microsoft::WRL::ComPtr;
namespace {
int assertions{};
void require(bool value, const char* message) {
    ++assertions;
    if (!value) { std::cerr << message << '\n'; throw std::runtime_error(message); }
}
void check(HRESULT value, const char* message) { require(SUCCEEDED(value), message); }
bool wait(const std::function<bool()>& predicate) {
    for (int i = 0; i < 500; ++i) { if (predicate()) return true; Sleep(10); }
    return false;
}
void activate(HWND host, HWND target) {
    MSG message{}; PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    const auto caller = GetCurrentThreadId();
    const auto foreground = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const auto owner = GetWindowThreadProcessId(host, nullptr);
    const bool joined_foreground = caller != foreground && AttachThreadInput(caller, foreground, TRUE) != FALSE;
    const bool joined_owner = caller != owner && owner != foreground && AttachThreadInput(caller, owner, TRUE) != FALSE;
    BringWindowToTop(host);
    SetForegroundWindow(host);
    SetFocus(target);
    if (joined_owner) AttachThreadInput(caller, owner, FALSE);
    if (joined_foreground) AttachThreadInput(caller, foreground, FALSE);
}
HWND popup_for(DWORD thread) {
    HWND result{};
    EnumThreadWindows(thread, [](HWND h, LPARAM p) -> BOOL {
        wchar_t cls[40]{};
        GetClassNameW(h, cls, 40);
        if (std::wstring_view(cls) == L"#32768" && IsWindowVisible(h)) {
            *reinterpret_cast<HWND*>(p) = h; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
void key(HWND host, WORD value, bool shift = false) {
    require(GetForegroundWindow() == host, "Input belongs to the owned menu host");
    INPUT inputs[4]{};
    UINT count{};
    auto add = [&](WORD code, DWORD flags) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = code;
        inputs[count++].ki.dwFlags = flags | ((code == VK_HOME || code == VK_END ||
            code == VK_UP || code == VK_DOWN || code == VK_APPS) ? KEYEVENTF_EXTENDEDKEY : 0);
    };
    if (shift) add(VK_SHIFT, 0);
    add(value, 0); add(value, KEYEVENTF_KEYUP);
    if (shift) add(VK_SHIFT, KEYEVENTF_KEYUP);
    require(SendInput(count, inputs, sizeof(INPUT)) == count, "Send owned menu keyboard input");
    Sleep(40);
}
ComPtr<IUIAutomationElement> find(IUIAutomation* automation, HWND popup, const wchar_t* name) {
    ComPtr<IUIAutomationElement> menu, item;
    check(automation->ElementFromHandle(popup, &menu), "Get menu provider");
    CONTROLTYPEID type{};
    check(menu->get_CurrentControlType(&type), "Get menu type");
    require(type == UIA_MenuControlTypeId, "Native popup exposes Menu");
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(name);
    ComPtr<IUIAutomationCondition> condition;
    const auto result = automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition);
    VariantClear(&value); check(result, "Find named menu item");
    check(menu->FindFirst(TreeScope_Descendants, condition.Get(), &item), "Read named menu item");
    require(item != nullptr, "Owner-drawn menu retains accessible names");
    check(item->get_CurrentControlType(&type), "Read menu item type");
    require(type == UIA_MenuItemControlTypeId, "Command exposes MenuItem");
    return item;
}
template<class T> ComPtr<T> pattern(IUIAutomationElement* item, PATTERNID id) {
    ComPtr<T> result;
    check(item->GetCurrentPatternAs(id, __uuidof(T), reinterpret_cast<void**>(result.GetAddressOf())), "Read menu pattern");
    require(result != nullptr, "Menu pattern exists");
    return result;
}
COLORREF native(D2D1_COLOR_F c) { return RGB(std::lround(c.r * 255), std::lround(c.g * 255), std::lround(c.b * 255)); }
struct Fixture {
    std::atomic<HWND> host{}, edit{};
    std::atomic<int> chosen{}, completed{}, failures{}, paints{};
    std::atomic<DWORD> thread{};
    xui::Button control{L"Menu commands"};
    xui::Palette palette = xui::Palette::system();
    UINT dpi{96};
    bool baseline{};
    std::jthread ui;
    Fixture() {
        control.on_context_menu([this] {
            return std::vector<xui::MenuItem>{
                {L"&Open\tEnter", [this] { chosen = 1; }},
                {L"Copy path\tCtrl+C", [this] { chosen = 2; }},
                {L"Copy name\tCtrl+Shift+C", [this] { chosen = 3; }},
                {L"Unavailable\tDelete", [this] { chosen = 99; }, false},
                {L"", {}, true, false, true},
                {L"Dark theme", [this] { chosen = 4; }, true, true},
                {L"&Refresh\tF5", [this] { chosen = 5; }}
            };
        });
        ui = std::jthread([this] {
            thread = GetCurrentThreadId();
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            WNDCLASSW cls{};
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.lpfnWndProc = procedure;
            cls.lpszClassName = L"Xui.Menu.Test.1";
            cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            RegisterClassW(&cls);
            host = CreateWindowExW(0, cls.lpszClassName, L"XUI owned menu test",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 80, 660, 510, nullptr, nullptr, cls.hInstance, this);
            edit = CreateWindowExW(0, L"EDIT", L"Native focus and selection", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                24, 24, 360, 28, host, nullptr, cls.hInstance, nullptr);
            SetFocus(edit); SendMessageW(edit, EM_SETSEL, 2, 9);
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            CoUninitialize();
        });
        require(wait([&] { return host && edit; }), "Create owned menu fixture");
        SetWindowPos(host, HWND_TOPMOST, 80, 80, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
        activate(host, edit);
        ComPtr<IUIAutomation> automation;
        ComPtr<IUIAutomationElement> input;
        CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
        if (automation && SUCCEEDED(automation->ElementFromHandle(edit, &input)) && input) input->SetFocus();
        if (!wait([&] { if (GetForegroundWindow() != host) activate(host, edit); return GetForegroundWindow() == host; })) {
            PostMessageW(host, WM_CLOSE, 0, 0);
            require(false, "Activate owned fixture");
        }
    }
    ~Fixture() {
        if (host) {
            SendMessageW(host, WM_CANCELMODE, 0, 0);
            PostMessageW(host, WM_CLOSE, 0, 0);
        }
    }
    static LRESULT CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l) noexcept {
        auto* self = reinterpret_cast<Fixture*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<Fixture*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(h, m, w, l);
        try {
            if (m == WM_CANCELMODE) { EndMenu(); return 0; }
            if (m == WM_APP + 1) {
                self->palette = xui::Palette::system(static_cast<xui::ThemeMode>(w));
                self->dpi = static_cast<UINT>(l);
                InvalidateRect(h, nullptr, FALSE); return 0;
            }
            if (m == WM_APP + 2) { self->baseline = w != 0; return 0; }
            if (m == WM_APP + 3) return GetFocus() == self->edit;
            if (m == WM_APP + 4) return static_cast<LRESULT>(xui::Drawing::live_targets());
            if (m == WM_CONTEXTMENU) {
                if (self->baseline) {
                    const auto menu = CreatePopupMenu();
                    const auto items = self->control.context_menu();
                    for (UINT i = 0; i < items.size(); ++i) AppendMenuW(menu, items[i].separator ? MF_SEPARATOR :
                        MF_STRING | (items[i].enabled ? MF_ENABLED : MF_GRAYED) | (items[i].checked ? MF_CHECKED : 0),
                        i + 1, items[i].text.c_str());
                    POINT point{12, 12}; ClientToScreen(h, &point);
                    TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, h, nullptr);
                    DestroyMenu(menu);
                } else xui::show_control_menu(self->control, h, l, self->palette, self->dpi);
                ++self->completed;
                return 0;
            }
            if (m == WM_PAINT) {
                PAINTSTRUCT ps{};
                const auto dc = BeginPaint(h, &ps);
                SetDCBrushColor(dc, native(self->palette.background));
                FillRect(dc, &ps.rcPaint, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                EndPaint(h, &ps); ++self->paints; return 0;
            }
            if (m == WM_DESTROY) { self->host = nullptr; PostQuitMessage(0); return 0; }
        } catch (...) { ++self->failures; ++self->completed; return 0; }
        return DefWindowProcW(h, m, w, l);
    }
    HWND open(LPARAM point = -1) {
        chosen = 0;
        const auto start = std::chrono::steady_clock::now();
        require(PostMessageW(host, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(host.load()), point) != FALSE, "Open menu");
        HWND popup{};
        require(wait([&] { popup = popup_for(thread); return popup != nullptr; }), "Menu becomes visible");
        SendMessageW(popup, WM_NULL, 0, 0);
        std::cout << "menu_visible_ms=" << std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count() << '\n';
        return popup;
    }
    void escape() {
        const auto before = completed.load();
        key(host, VK_ESCAPE);
        require(wait([&] { return completed > before && !popup_for(thread); }), "Escape dismisses the popup");
        require(chosen == 0, "Escape does not run a command");
    }
    void focus() {
        require(SendMessageW(host, WM_APP + 3, 0, 0) == 1, "Menu restores native EDIT focus");
        DWORD start{}, end{};
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        require(start == 2 && end == 9, "Menu preserves native EDIT selection");
    }
};
void visuals_and_input(const std::filesystem::path& directory) {
    Fixture fixture;
    ComPtr<IUIAutomation> automation;
    check(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)), "Create menu automation");
    SendMessageW(fixture.host, WM_APP + 2, 1, 0);
    auto popup = fixture.open();
    key(fixture.host, VK_HOME);
    suggestion_capture::bitmap(popup, nullptr, directory / L"before-native.bmp");
    fixture.escape();
    SendMessageW(fixture.host, WM_APP + 2, 0, 0);
    for (auto mode : {xui::ThemeMode::dark, xui::ThemeMode::light, xui::ThemeMode::high_contrast}) {
        for (UINT dpi : {96u, 144u, 192u}) {
            SendMessageW(fixture.host, WM_APP + 1, static_cast<WPARAM>(mode), dpi);
            popup = fixture.open();
            key(fixture.host, VK_HOME);
            const auto palette = xui::Palette::system(mode);
            auto first = find(automation.Get(), popup, L"Open");
            BOOL focused{};
            check(first->get_CurrentHasKeyboardFocus(&focused), "Read focused menu command");
            require(focused != FALSE, "Home updates native UIA keyboard focus");
            auto disabled = find(automation.Get(), popup, L"Unavailable");
            BOOL enabled{};
            check(disabled->get_CurrentIsEnabled(&enabled), "Read disabled menu state");
            require(!enabled, "Disabled command stays disabled in UIA");
            auto disabled_invoke = pattern<IUIAutomationInvokePattern>(disabled.Get(), UIA_InvokePatternId);
            require(FAILED(disabled_invoke->Invoke()), "UIA refuses disabled invocation");
            auto checked = find(automation.Get(), popup, L"Dark theme");
            auto legacy = pattern<IUIAutomationLegacyIAccessiblePattern>(checked.Get(), UIA_LegacyIAccessiblePatternId);
            DWORD state{};
            check(legacy->get_CurrentState(&state), "Read checked menu state");
            require((state & STATE_SYSTEM_CHECKED) != 0, "Checked state survives owner drawing");
            RECT bounds{}, row{};
            GetWindowRect(popup, &bounds);
            check(first->get_CurrentBoundingRectangle(&row), "Read command bounds");
            require(row.bottom - row.top >= MulDiv(30, dpi, 96), "Menu row padding scales with DPI");
            HDC dc = GetWindowDC(popup);
            require(dc != nullptr, "Read visible menu pixels");
            const auto frame = GetPixel(dc, 0, (bounds.bottom - bounds.top) / 2);
            const auto margin = GetPixel(dc, 2, 2);
            const auto highlight = GetPixel(dc, row.left - bounds.left + MulDiv(28, dpi, 96),
                (row.top + row.bottom) / 2 - bounds.top);
            ReleaseDC(popup, dc);
            require(frame == native(palette.border), "Actual popup frame uses shared palette, not a native white border");
            require(margin == native(palette.surface), "Actual popup margin uses shared surface");
            require(highlight == native(palette.selection), "Actual focused row uses shared selection color");
            const auto file = std::wstring(mode == xui::ThemeMode::dark ? L"dark-" :
                mode == xui::ThemeMode::light ? L"light-" : L"contrast-") + std::to_wstring(dpi) + L".bmp";
            suggestion_capture::bitmap(popup, nullptr, directory / file);
            fixture.escape();
            fixture.focus();
        }
    }
    SendMessageW(fixture.host, WM_APP + 1, static_cast<WPARAM>(xui::ThemeMode::dark), 96);
    popup = fixture.open();
    auto copy = find(automation.Get(), popup, L"Copy path");
    check(pattern<IUIAutomationInvokePattern>(copy.Get(), UIA_InvokePatternId)->Invoke(), "Invoke menu command through UIA");
    require(wait([&] { return fixture.chosen == 2 && !popup_for(fixture.thread); }), "UIA dispatches the copied command once");
    fixture.focus();
    fixture.open();
    key(fixture.host, VK_END); key(fixture.host, VK_UP); key(fixture.host, VK_UP); key(fixture.host, VK_RETURN);
    require(fixture.chosen == 0, "Enter on a disabled native item does not run a command");
    require(wait([&] { return !popup_for(fixture.thread); }), "Native disabled Enter dismisses the menu");
    fixture.open();
    key(fixture.host, VK_END); key(fixture.host, VK_UP); key(fixture.host, VK_UP);
    key(fixture.host, VK_UP); key(fixture.host, VK_RETURN);
    require(wait([&] { return fixture.chosen == 3; }), "Up skips separators and preserves native disabled-item navigation");
    fixture.open();
    key(fixture.host, VK_HOME); key(fixture.host, VK_DOWN); key(fixture.host, VK_RETURN);
    require(wait([&] { return fixture.chosen == 2; }), "Home Down Enter runs expected command");
    fixture.open();
    key(fixture.host, 'C'); key(fixture.host, 'C'); key(fixture.host, VK_RETURN);
    require(wait([&] { return fixture.chosen == 3; }), "Typeahead cycles duplicate initial letters");
    fixture.open(); key(fixture.host, 'R');
    require(wait([&] { return fixture.chosen == 5; }), "Explicit mnemonic runs its unique command");
    popup = fixture.open();
    auto first = find(automation.Get(), popup, L"Open");
    RECT row{}; first->get_CurrentBoundingRectangle(&row);
    require(SetCursorPos((row.left + row.right) / 2, (row.top + row.bottom) / 2) != FALSE, "Move pointer to owned command");
    INPUT mouse[2]{};
    for (auto& input : mouse) input.type = INPUT_MOUSE;
    mouse[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; mouse[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    require(SendInput(2, mouse, sizeof(INPUT)) == 2, "Click owned menu item");
    require(wait([&] { return fixture.chosen == 1; }), "Mouse runs the same menu command");
    fixture.open();
    SendMessageW(fixture.host, WM_THEMECHANGED, 0, 0);
    require(wait([&] { return !popup_for(fixture.thread); }), "Theme changes dismiss stale menu colors");
    fixture.open();
    SendMessageW(fixture.host, WM_ACTIVATE, WA_INACTIVE, 0);
    require(wait([&] { return !popup_for(fixture.thread); }), "Deactivation cancels the menu without a command");
    require(fixture.chosen == 0, "Deactivation never invokes a command");
    fixture.open();
    RECT host_rect{}; GetWindowRect(fixture.host, &host_rect);
    require(SetCursorPos(host_rect.right - 30, host_rect.bottom - 40) != FALSE, "Move pointer outside menu inside owned host");
    require(SendInput(2, mouse, sizeof(INPUT)) == 2, "Click outside menu inside owned host");
    require(wait([&] { return !popup_for(fixture.thread); }), "Outside click dismisses menu");
    require(fixture.chosen == 0, "Outside click never invokes a command");
    std::vector<RECT> areas;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFO info{sizeof(info)};
        if (GetMonitorInfoW(monitor, &info)) reinterpret_cast<std::vector<RECT>*>(data)->push_back(info.rcWork);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&areas));
    for (const auto area : areas) {
        popup = fixture.open(MAKELPARAM(area.right - 2, area.bottom - 2));
        RECT rect{}; GetWindowRect(popup, &rect);
        require(rect.left >= area.left && rect.top >= area.top && rect.right <= area.right && rect.bottom <= area.bottom,
            "Native placement keeps the menu inside each monitor work area");
        fixture.escape();
    }
    fixture.open(); fixture.escape();
    const auto gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const auto user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int i = 0; i < 50; ++i) { fixture.open(); fixture.escape(); }
    require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi, "Fifty menu cycles do not retain GDI objects");
    require(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= user, "Fifty menu cycles do not retain USER objects");
    require(SendMessageW(fixture.host, WM_APP + 4, 0, 0) == 0, "Menus allocate no Direct2D targets");
    Sleep(80);
    const auto paints = fixture.paints.load();
    Sleep(250);
    require(paints == fixture.paints, "Closed menus do not schedule idle paints");
    require(fixture.failures == 0, "Menu platform operations complete without hidden errors");
    std::cout << "cycles=50 gdi=" << gdi << " user=" << user << " idle_paints=0 targets=0 monitors=" << areas.size() << '\n';
}
class PendingSuggestions final : public xui::SuggestionSource {
public:
    std::atomic<bool> entered{}, cancelled{}, release{};
    xui::SuggestionResult suggest(const xui::SuggestionRequest&, const std::function<bool()>& cancel) override {
        entered = true;
        while (!release) {
            if (cancel()) cancelled = true;
            Sleep(5);
        }
        return {{L"Late suggestion"}, {}};
    }
};
void public_routes() {
    xui::Window window({L"XUI public menu routes", {520, 320}});
    auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto input = std::make_shared<xui::TextInput>(L"Menu input");
    auto button = std::make_shared<xui::Button>(L"Menu button");
    button->set_automation_id(L"menu-button");
    auto source = std::make_shared<PendingSuggestions>();
    input->set_suggestions(source);
    root->add(input); root->add(button); window.set_content(root);
    std::atomic<int> commands{};
    const auto menu = [&] { return std::vector<xui::MenuItem>{{L"Same command", [&] { ++commands; }}}; };
    input->on_context_menu(menu); button->on_context_menu(menu);
    std::exception_ptr failure;
    std::jthread driver([&] {
        HWND host{};
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            require(wait([&] {
                host = FindWindowW(L"Xui.Window.1", L"XUI public menu routes");
                DWORD process{}; if (host) GetWindowThreadProcessId(host, &process);
                return process == GetCurrentProcessId() && SendMessageW(host, WM_APP + 60, 0, 0) > 0;
            }), "Public menu route host paints");
            const auto thread = GetWindowThreadProcessId(host, nullptr);
            const auto edit = FindWindowExW(host, nullptr, L"EDIT", nullptr);
            const auto peer = FindWindowExW(host, nullptr, L"Xui.Control.1", L"Menu button");
            require(edit && peer, "Native EDIT and button peers exist");
            ComPtr<IUIAutomation> automation;
            check(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)), "Create route automation");
            const auto focus = [&](HWND hwnd) {
                ComPtr<IUIAutomationElement> element;
                check(automation->ElementFromHandle(hwnd, &element), "Read public control provider");
                if (hwnd == peer) {
                    ComPtr<IUIAutomationElement> host_element;
                    check(automation->ElementFromHandle(host, &host_element), "Read public menu host provider");
                    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(L"menu-button");
                    ComPtr<IUIAutomationCondition> condition;
                    const auto result = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
                    VariantClear(&value);
                    check(result, "Find semantic button condition");
                    ComPtr<IUIAutomationElement> child;
                    check(host_element->FindFirst(TreeScope_Descendants, condition.Get(), &child), "Read semantic button");
                    require(child != nullptr, "Button peer exposes its semantic child");
                    element = std::move(child);
                }
                activate(host, hwnd);
                const auto result = element->SetFocus();
                check(result, "Focus public control");
                const bool focused = wait([&] { BOOL value{}; return SUCCEEDED(element->get_CurrentHasKeyboardFocus(&value)) &&
                    value && GetForegroundWindow() == host; });
                if (!focused) {
                    GUITHREADINFO info{sizeof(info)}; GetGUIThreadInfo(thread, &info);
                    BOOL value{}; element->get_CurrentHasKeyboardFocus(&value);
                    std::cout << "route_focus native=" << (info.hwndFocus == hwnd) << " uia=" << value
                        << " foreground=" << (GetForegroundWindow() == host) << '\n';
                }
                require(focused, "Public control receives keyboard focus");
            };
            const auto invoke = [&](int expected) {
                HWND popup{};
                require(wait([&] { popup = popup_for(thread); return popup != nullptr; }), "Public control opens shared menu");
                auto command = find(automation.Get(), popup, L"Same command");
                check(pattern<IUIAutomationInvokePattern>(command.Get(), UIA_InvokePatternId)->Invoke(), "Invoke shared route command");
                require(wait([&] { return commands == expected && !popup_for(thread); }), "Each public route invokes the command once");
            };
            focus(peer);
            key(host, VK_APPS); invoke(1);
            key(host, VK_F10, true); invoke(2);
            PostMessageW(peer, WM_RBUTTONUP, 0, MAKELPARAM(20, 16)); invoke(3);
            focus(edit);
            SendMessageW(edit, EM_SETSEL, 2, 2);
            SendMessageW(edit, WM_IME_STARTCOMPOSITION, 0, 0);
            PostMessageW(edit, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(edit), -1);
            Sleep(100);
            require(popup_for(thread) == nullptr, "Custom menu does not intercept active IME composition");
            SendMessageW(edit, WM_IME_ENDCOMPOSITION, 0, 0);
            SendMessageW(edit, WM_CHAR, L'a', 0);
            require(wait([&] { return source->entered.load(); }), "Suggestion provider starts pending request");
            PostMessageW(edit, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(edit), -1);
            HWND popup{};
            require(wait([&] { popup = popup_for(thread); return popup != nullptr; }), "Native EDIT custom menu opens while provider is pending");
            require(wait([&] { return source->cancelled.load(); }), "Opening a context menu cancels pending suggestions");
            source->release = true;
            Sleep(120);
            bool suggestions{};
            EnumThreadWindows(thread, [](HWND hwnd, LPARAM value) -> BOOL {
                wchar_t cls[64]{}; GetClassNameW(hwnd, cls, 64);
                if (std::wstring_view(cls) == L"Xui.Suggestions.1" && IsWindowVisible(hwnd))
                    *reinterpret_cast<bool*>(value) = true;
                return TRUE;
            }, reinterpret_cast<LPARAM>(&suggestions));
            require(!suggestions, "Late suggestions cannot overlap the context menu");
            invoke(4);
            focus(edit);
            key(host, VK_F10, true); invoke(5);
            focus(peer);
            key(host, VK_APPS);
            require(wait([&] { return popup_for(thread) != nullptr; }), "Open menu before host closure");
            PostMessageW(host, WM_CLOSE, 0, 0);
            require(wait([&] { return !IsWindow(host) && !popup_for(thread); }), "Host closure dismisses active menu without dispatch");
        } catch (...) {
            failure = std::current_exception();
            if (host) { SendMessageW(host, WM_CANCELMODE, 0, 0); PostMessageW(host, WM_CLOSE, 0, 0); }
        }
        source->release = true;
        CoUninitialize();
    });
    const auto result = xui::Application::run(window);
    driver.join();
    if (failure) std::rethrow_exception(failure);
    require(result == 0 && commands == 5, "Public menu routes and pending suggestions close without errors");
}
void lifecycle(int mode) {
    auto window = std::make_unique<xui::Window>(xui::WindowOptions{L"XUI menu lifecycle", {400, 220}});
    auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto button = std::make_shared<xui::Button>(L"Menu lifecycle command");
    root->add(button); window->set_content(root);
    int invoked{};
    button->on_context_menu([&] {
        return std::vector<xui::MenuItem>{{L"Run", [&] {
            ++invoked;
            if (mode == 0) window->close();
            if (mode == 1) throw std::runtime_error("Menu callback failure");
            if (mode == 2 || mode == 3) { window.reset(); if (mode == 3) throw std::runtime_error("Deleted menu callback failure"); }
            if (mode == 4) {
                const auto host = FindWindowW(L"Xui.Window.1", L"XUI menu lifecycle");
                const auto peer = FindWindowExW(host, nullptr, L"Xui.Control.1", L"Menu lifecycle command");
                button->on_context_menu([&] { return std::vector<xui::MenuItem>{{L"Close", [&] { ++invoked; window->close(); }}}; });
                SendMessageW(peer, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(peer), -1);
            }
        }}};
    });
    std::atomic<bool> driven{};
    std::jthread driver([&] {
        HWND host{}, peer{};
        if (!wait([&] {
            host = FindWindowW(L"Xui.Window.1", L"XUI menu lifecycle");
            DWORD process{};
            if (host) GetWindowThreadProcessId(host, &process);
            if (process != GetCurrentProcessId()) return false;
            peer = FindWindowExW(host, nullptr, L"Xui.Control.1", L"Menu lifecycle command");
            return peer && SendMessageW(host, WM_APP + 60, 0, 0) > 0;
        })) return;
        const auto thread = GetWindowThreadProcessId(host, nullptr);
        SetForegroundWindow(host);
        PostMessageW(peer, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(peer), -1);
        if (!wait([&] { return popup_for(thread) != nullptr; })) { PostMessageW(host, WM_CLOSE, 0, 0); return; }
        PostMessageW(host, WM_KEYDOWN, VK_HOME, 0);
        PostMessageW(host, WM_KEYDOWN, VK_RETURN, 0);
        if (mode == 4) {
            Sleep(100);
            if (!wait([&] { return popup_for(thread) != nullptr; })) { PostMessageW(host, WM_CLOSE, 0, 0); return; }
            PostMessageW(host, WM_KEYDOWN, VK_HOME, 0);
            PostMessageW(host, WM_KEYDOWN, VK_RETURN, 0);
        }
        driven = true;
    });
    const auto result = xui::Application::run(*window);
    driver.join();
    require(driven && invoked == (mode == 4 ? 2 : 1), "Menu callback executes once after native menu closure");
    require(result == (mode == 1 || mode == 3 ? 1 : 0), "Close, deletion, reentrant menu, and callback failures unwind safely");
    if (mode == 1) require(window->error().find(L"Menu callback failure") != std::wstring::npos, "Callback failure reaches Window error");
}
}
int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Initialize menu test COM");
        if (argc < 3 || std::wstring_view(argv[2]) != L"--routes")
            visuals_and_input(argc > 1 ? argv[1] : L"build\\menus\\captures");
        CoUninitialize();
        public_routes();
        for (int mode = 0; mode < 5; ++mode) lifecycle(mode);
        std::cout << "Menu tests passed: " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Menu test failed: " << error.what() << " after " << assertions << " assertions\n";
        return 1;
    }
}
