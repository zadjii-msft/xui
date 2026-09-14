#include "xui/application.hpp"
#include "xui/navigation.hpp"
#include "xui/shell_commands.hpp"
#include "xui/titlebar.hpp"
#include "../src/drawing.hpp"
#include "suggestion_capture.hpp"
#include "owned_window_capture.hpp"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>

namespace {
using namespace xui;
using Microsoft::WRL::ComPtr;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void success(HRESULT hr, const char* text) { if (FAILED(hr)) throw std::runtime_error(std::string(text) + ": " + std::to_string(hr)); }
HWND child(HWND root, const wchar_t* name) {
    struct State { const wchar_t* name; HWND result{}; } state{name};
    EnumChildWindows(root, [](HWND hwnd, LPARAM data) -> BOOL {
        auto& state = *reinterpret_cast<State*>(data); wchar_t text[256]{}; GetWindowTextW(hwnd, text, 256);
        if (state.name == std::wstring_view(text)) { state.result = hwnd; return FALSE; } return TRUE;
    }, reinterpret_cast<LPARAM>(&state)); require(state.result != nullptr, "Owned native control exists"); return state.result;
}
void flush(HWND hwnd) { SendMessageW(hwnd, WM_APP + 12, 0, 0); InvalidateRect(hwnd, nullptr, FALSE); UpdateWindow(hwnd); }
void verify_caption(HWND hwnd, const TitleBar& caption, ThemeMode theme, UINT dpi) {
    const auto close = child(hwnd, L"Close window");
    require(!(GetWindowLongPtrW(close, GWL_STYLE) & WS_TABSTOP), "Caption buttons stay out of client tab order");
    const auto bounds = caption.close()->bounds();
    const auto palette = Palette::system(theme);
    const auto native_color = [](D2D1_COLOR_F color) {
        return RGB(std::lround(color.r * 255), std::lround(color.g * 255), std::lround(color.b * 255));
    };
    const auto pixel = [&](float x, float y) {
        flush(hwnd);
        const auto dc = GetDC(hwnd);
        require(dc != nullptr, "Read owned caption pixels");
        const auto result = GetPixel(dc, static_cast<int>((bounds.x + x) * dpi / 96),
            static_cast<int>((bounds.y + y) * dpi / 96));
        ReleaseDC(hwnd, dc);
        require(result != CLR_INVALID, "Caption pixel exists");
        return result;
    };
    SendMessageW(close, WM_NCMOUSELEAVE, 0, 0);
    require(pixel(3, 12) == native_color(palette.background), "Resting caption has no button surface or border");
    POINT screen{static_cast<LONG>((bounds.x + bounds.width / 2) * dpi / 96),
        static_cast<LONG>((bounds.y + bounds.height / 2) * dpi / 96)};
    ClientToScreen(hwnd, &screen);
    const auto position = MAKELPARAM(screen.x, screen.y);
    SendMessageW(close, WM_NCMOUSEMOVE, HTCLOSE, position);
    require(caption.close()->hovered(), "Nonclient hover reaches retained caption");
    const auto hot = palette.high_contrast ? native_color(palette.selection) : RGB(0xe8, 0x11, 0x23);
    require(pixel(3, 12) == hot && pixel(1, 1) == hot, "Close hover uses an edge-to-edge rectangular highlight");
    SendMessageW(close, WM_NCLBUTTONDOWN, HTCLOSE, position);
    require(caption.close()->pressed() && GetCapture() == close, "Caption press captures pointer without native modal tracking");
    require(pixel(3, 12) == (palette.high_contrast ? hot : RGB(0xc5, 0x0f, 0x1f)), "Close pressed color");
    SendMessageW(close, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-10, -10));
    require(!caption.close()->hovered() && pixel(3, 12) == native_color(palette.background), "Dragging outside removes caption highlight");
    SendMessageW(close, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(MulDiv(23, dpi, 96), MulDiv(16, dpi, 96)));
    require(caption.close()->pressed(), "Dragging back inside restores caption press");
    SendMessageW(close, WM_LBUTTONUP, 0, MAKELPARAM(-10, -10));
    require(!caption.close()->captured() && !caption.close()->hovered() && GetCapture() != close &&
        IsWindow(hwnd), "Release outside cancels close and clears hover");
    SendMessageW(close, WM_NCMOUSEMOVE, HTCLOSE, position);
    SendMessageW(close, WM_NCLBUTTONDOWN, HTCLOSE, position);
    SendMessageW(close, WM_CANCELMODE, 0, 0);
    require(!caption.close()->captured() && GetCapture() != close, "Cancelled caption press releases capture");
    caption.close()->set_enabled(false);
    SendMessageW(close, WM_NCMOUSEMOVE, HTCLOSE, position);
    SendMessageW(close, WM_NCLBUTTONDOWN, HTCLOSE, position);
    require(!caption.close()->hovered() && !caption.close()->pressed() &&
        pixel(3, 12) == native_color(palette.background), "Disabled captions have no pointer highlight or action");
    caption.close()->set_enabled(true);
    SendMessageW(close, WM_NCMOUSELEAVE, 0, 0);
    require(!caption.close()->hovered(), "Nonclient leave clears caption hover");
}
void capture_palette(HWND hwnd, Rect popup, ThemeMode theme, UINT dpi, Point hovered) {
    const auto pixels = owned_window_capture::capture(hwnd);
    const auto pixel = [&](float x, float y) {
        const auto px = static_cast<int>(std::lround(x * dpi / 96));
        const auto py = static_cast<int>(std::lround(y * dpi / 96));
        require(px >= 0 && py >= 0 && px < pixels.width && py < pixels.height, "Palette capture sample is inside the client");
        return pixels.data[py * pixels.width + px] & 0xffffff;
    };
    if (theme != ThemeMode::high_contrast) {
        const auto x = popup.x + popup.width / 2, bottom = popup.y + popup.height;
        const auto inner_shadow = pixel(x, bottom + 2), outer_shadow = pixel(x, bottom + 18), outside = pixel(x, bottom + 24);
        require(inner_shadow < outer_shadow && outer_shadow <= outside, "Live popup shadow fades outside the frame without a hard gutter");
    }
    const auto hover_color = Palette::system(theme).hover;
    const DWORD expected = (static_cast<DWORD>(std::lround(hover_color.r * 255)) << 16) |
        (static_cast<DWORD>(std::lround(hover_color.g * 255)) << 8) | static_cast<DWORD>(std::lround(hover_color.b * 255));
    require(pixel(hovered.x, hovered.y) == expected, "The pointed command row paints the theme hover color");
    const auto directory = std::filesystem::path(L"navigation-captures");
    std::filesystem::create_directories(directory);
    const auto path = directory / (L"palette-live-" + std::to_wstring(static_cast<int>(theme)) + L"-" + std::to_wstring(dpi) + L".bmp");
    BITMAPINFOHEADER info{sizeof(BITMAPINFOHEADER), pixels.width, -pixels.height, 1, 32, BI_RGB};
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(info);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.data.size() * sizeof(DWORD));
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info), sizeof(info));
    file.write(reinterpret_cast<const char*>(pixels.data.data()), static_cast<std::streamsize>(pixels.data.size() * sizeof(DWORD)));
    require(bool(file), "Write live palette capture");
}
void verify_native_print_clip(HWND hwnd) {
    RECT bounds{}; GetClientRect(hwnd, &bounds);
    struct Canvas {
        HDC dc{CreateCompatibleDC(nullptr)};
        HBITMAP bitmap{};
        HGDIOBJ previous{};
        ~Canvas() { if (previous) SelectObject(dc, previous); if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); }
    } canvas;
    BITMAPINFO info{}; info.bmiHeader = {sizeof(BITMAPINFOHEADER), bounds.right, -bounds.bottom, 1, 32, BI_RGB};
    void* pixels{};
    canvas.bitmap = CreateDIBSection(canvas.dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    require(canvas.dc && canvas.bitmap, "Create owned native print fixture");
    canvas.previous = SelectObject(canvas.dc, canvas.bitmap);
    const auto color = RGB(251, 0, 247);
    SetPixel(canvas.dc, bounds.right / 2, bounds.bottom / 2, color);
    SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(canvas.dc), PRF_CLIENT | PRF_ERASEBKGND);
    require(GetPixel(canvas.dc, bounds.right / 2, bounds.bottom / 2) == color, "Native print cannot overwrite pixels occluded by popup");
    SendMessageW(hwnd, WM_PRINT, reinterpret_cast<WPARAM>(canvas.dc), PRF_CLIENT | PRF_ERASEBKGND);
    require(GetPixel(canvas.dc, bounds.right / 2, bounds.bottom / 2) == color, "Native full print respects popup occlusion");
}
ComPtr<IUIAutomationElement> find(IUIAutomation* automation, IUIAutomationElement* root, const wchar_t* id) {
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationCondition> condition;
    const auto hr = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition); VariantClear(&value); success(hr, "Create ID condition");
    ComPtr<IUIAutomationElement> result;
    success(root->FindFirst(TreeScope_Subtree, condition.Get(), &result), "Find virtual item");
    require(result != nullptr, "Virtual UIA item exists"); return result;
}
template<class T> ComPtr<T> pattern(IUIAutomationElement* element, PATTERNID id) {
    ComPtr<T> result; success(element->GetCurrentPatternAs(id, IID_PPV_ARGS(&result)), "Get real UIA pattern"); return result;
}
template<class F> void eventually(F&& callback, const char* text) {
    for (int i = 0; i < 500; ++i) { if (callback()) return; Sleep(20); } throw std::runtime_error(text);
}
int client(HWND hwnd) {
    success(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Client COM");
    struct Com { ~Com() { CoUninitialize(); } } com;
    ComPtr<IUIAutomation> automation; success(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)), "Create UIA");
    ComPtr<IUIAutomationElement> root; success(automation->ElementFromHandle(hwnd, &root), "Own UIA root");
    auto menu = find(automation.Get(), root.Get(), L"native-command-menu");
    CONTROLTYPEID type{}; success(menu->get_CurrentControlType(&type), "Menu role"); require(type == UIA_MenuControlTypeId, "Rich menu exposes Menu role");
    auto section = find(automation.Get(), menu.Get(), L"6:1");
    success(section->get_CurrentControlType(&type), "Section role");
    require(type == UIA_HeaderControlTypeId, "Command sections expose non-interactive UIA headers");
    ComPtr<IUnknown> section_pattern;
    success(section->GetCurrentPattern(UIA_InvokePatternId, &section_pattern), "Read section Invoke support");
    require(!section_pattern, "Section headers do not expose command invocation");
    BOOL section_focusable{};
    success(section->get_CurrentIsKeyboardFocusable(&section_focusable), "Read section focusability");
    require(!section_focusable, "Section headers cannot receive keyboard focus");
    require(section->SetFocus() == UIA_E_INVALIDOPERATION, "UIA rejects focus requests for a section header");
    auto first = find(automation.Get(), menu.Get(), L"1:1");
    success(first->get_CurrentControlType(&type), "MenuItem role"); require(type == UIA_MenuItemControlTypeId, "Command exposes MenuItem role");
    auto toggle = pattern<IUIAutomationTogglePattern>(first.Get(), UIA_TogglePatternId);
    ToggleState checked{}; success(toggle->get_CurrentToggleState(&checked), "Read check state"); require(checked == ToggleState_On, "Checked command state");
    auto pin = find(automation.Get(), first.Get(), L"1:1:action");
    auto pin_action = pattern<IUIAutomationInvokePattern>(pin.Get(), UIA_InvokePatternId);
    success(pin_action->Invoke(), "Invoke distinct pin");
    auto disabled = find(automation.Get(), menu.Get(), L"2:1");
    auto disabled_action = pattern<IUIAutomationInvokePattern>(disabled.Get(), UIA_InvokePatternId);
    require(disabled_action->Invoke() == UIA_E_ELEMENTNOTENABLED, "Disabled UIA command rejected");
    auto separator = find(automation.Get(), menu.Get(), L"3:1");
    success(separator->get_CurrentControlType(&type), "Separator role"); require(type == UIA_SeparatorControlTypeId, "Separator has semantic role");
    RECT separator_bounds{}, first_bounds{};
    success(separator->get_CurrentBoundingRectangle(&separator_bounds), "Read separator bounds");
    success(first->get_CurrentBoundingRectangle(&first_bounds), "Read command bounds");
    require(separator_bounds.bottom - separator_bounds.top == (first_bounds.bottom - first_bounds.top) / 4,
        "UIA exposes compact separator geometry");
    ComPtr<IUnknown> unsupported;
    require(FAILED(separator->GetCurrentPattern(UIA_InvokePatternId, &unsupported)) || !unsupported, "Separator has no Invoke action");
    unsupported.Reset();
    require(FAILED(separator->GetCurrentPattern(UIA_SelectionItemPatternId, &unsupported)) || !unsupported, "Separator has no selection action");
    auto submenu = find(automation.Get(), menu.Get(), L"4:1");
    auto expand = pattern<IUIAutomationExpandCollapsePattern>(submenu.Get(), UIA_ExpandCollapsePatternId);
    success(expand->Expand(), "Open nested menu");
    ExpandCollapseState expanded{};
    eventually([&] { return SUCCEEDED(expand->get_CurrentExpandCollapseState(&expanded)) && expanded == ExpandCollapseState_Expanded; },
        "Parent submenu publishes expanded state");
    success(expand->Expand(), "Repeated submenu expansion is idempotent");
    success(expand->Collapse(), "Collapse only this submenu");
    eventually([&] { return SUCCEEDED(expand->get_CurrentExpandCollapseState(&expanded)) && expanded == ExpandCollapseState_Collapsed; },
        "Parent submenu publishes collapsed state");
    success(expand->Expand(), "Reopen collapsed submenu");
    ComPtr<IUIAutomationElement> nested;
    eventually([&] { try { nested = find(automation.Get(), root.Get(), L"5:1"); return true; } catch (...) { return false; } }, "Nested UIA popup exists");
    auto invoke = pattern<IUIAutomationInvokePattern>(nested.Get(), UIA_InvokePatternId); success(invoke->Invoke(), "Invoke nested command");
    eventually([&] { BOOL off{}; return FAILED(menu->get_CurrentIsOffscreen(&off)) || off; }, "Command menu dismissed");
    PostMessageW(hwnd, WM_KEYDOWN, VK_F9, 0);
    eventually([&] { return FAILED(pin_action->Invoke()); }, "Retained popup provider disconnects");
    auto segment = find(automation.Get(), root.Get(), L"segment-3-1");
    BSTR current_help{}; success(segment->get_CurrentHelpText(&current_help), "Current path semantics");
    const bool current = current_help && std::wstring_view(current_help) == L"Current location"; SysFreeString(current_help);
    require(current, "Breadcrumb publishes current-location semantics");
    success(pattern<IUIAutomationInvokePattern>(segment.Get(), UIA_InvokePatternId)->Invoke(), "Invoke breadcrumb segment");
    auto maximize = find(automation.Get(), root.Get(), L"caption-maximize");
    auto maximize_action = pattern<IUIAutomationInvokePattern>(maximize.Get(), UIA_InvokePatternId);
    success(maximize_action->Invoke(), "Invoke real maximize");
    eventually([&] { return IsZoomed(hwnd) != FALSE; }, "OS maximize state");
    success(maximize_action->Invoke(), "Invoke real restore");
    eventually([&] { return IsZoomed(hwnd) == FALSE; }, "OS restore state");
    const auto click_caption = [&](HWND button, WPARAM hit) {
        RECT bounds{}; GetClientRect(button, &bounds);
        POINT center{bounds.right / 2, bounds.bottom / 2}, screen = center;
        ClientToScreen(button, &screen);
        SendMessageW(button, WM_NCMOUSEMOVE, hit, MAKELPARAM(screen.x, screen.y));
        SendMessageW(button, WM_NCLBUTTONDOWN, hit, MAKELPARAM(screen.x, screen.y));
        SendMessageW(button, WM_LBUTTONUP, 0, MAKELPARAM(center.x, center.y));
    };
    const auto maximize_button = child(hwnd, L"Maximize");
    click_caption(maximize_button, HTMAXBUTTON);
    eventually([&] { return IsZoomed(hwnd) != FALSE; }, "Caption pointer click maximizes");
    click_caption(maximize_button, HTMAXBUTTON);
    eventually([&] { return IsZoomed(hwnd) == FALSE; }, "Caption pointer click restores");
    click_caption(child(hwnd, L"Minimize"), HTMINBUTTON);
    eventually([&] { return IsIconic(hwnd) != FALSE; }, "Caption pointer click minimizes");
    PostMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
    eventually([&] { return IsIconic(hwnd) == FALSE; }, "System command restores minimized caption window");
    PostMessageW(hwnd, WM_KEYDOWN, VK_F10, 0);
    Sleep(80);
    return 0;
}
void run_case(ThemeMode theme, UINT dpi, const std::wstring& executable) {
    Window window({L"XUI navigation contracts", {760, 650}, theme, {420, 320}, true});
    auto root = std::make_shared<Stack>(Axis::vertical); root->set_padding({8, 8, 8, 8}); root->set_spacing(6);
    auto anchor = std::make_shared<Button>(L"Commands anchor"); root->add(anchor);
    auto native = std::make_shared<TextInput>(L"Native input under popup"); native->set_text(L"Native text must not paint over commands."); root->add(native);
    auto path = std::make_shared<Breadcrumb>(); path->set_segments({{{1, 1}, L"Home"}, {{2, 1}, L"Projects"}, {{3, 1}, L"Current"}});
    root->add(path);
    auto range = std::make_shared<RangeInput>(L"Underlying range"); root->add(range);
    root->add(std::make_shared<Label>(L"Owned test fixture. No filesystem command runs."));
    window.set_content(root); window.titlebar()->tabs()->set_tabs({{10, L"First tab"}, {20, L"Second tab"}}, 10);
    auto surface = std::make_shared<CommandSurface>(L"Native commands"); surface->menu()->set_automation_id(L"native-command-menu");
    surface->menu()->set_name(L"Native command rows");
    int primary{}, pins{}, navigations{}; bool validated{}, done{};
    path->on_navigate([&](ItemKey key) { require(key == ItemKey{3, 1}, "Correct breadcrumb identity"); ++navigations; });
    surface->set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
        {6, 0, L"Tools", {}, true, {}, ButtonIcon::none, {}, L"", {}, CommandKind::section},
        {1, 0, L"Inspect", [&] { ++primary; }, true, true, ButtonIcon::up, {L"Enter", L"Ctrl+I"}, L"Pin", [&] { ++pins; }},
        {2, 0, L"Disabled", [&] { ++primary; }, false},
        {3, 0, L"", {}, true, {}, ButtonIcon::none, {}, L"", {}, CommandKind::separator},
        {4, 0, L"More", {}, true, {}, ButtonIcon::none, {}, L"", {}, CommandKind::submenu},
        {5, 4, L"Nested inspect", [&] { ++primary; }}}));
    std::atomic<int> stage{}; std::string driver_error;
    window.on_key([&](const KeyEvent& key) {
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI navigation contracts");
        if (key.key == Key::f11) { window.close(); return true; }
        if (key.key == Key::f12) {
            RECT rect{}; GetWindowRect(hwnd, &rect);
            const auto current = GetDpiForWindow(hwnd);
            rect.right = rect.left + MulDiv(rect.right - rect.left, dpi, current);
            rect.bottom = rect.top + MulDiv(rect.bottom - rect.top, dpi, current);
            SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rect)); flush(hwnd);
            const auto caption = window.titlebar();
            auto screen_hit = [&](Point p) {
                POINT screen{static_cast<LONG>(p.x * dpi / 96), static_cast<LONG>(p.y * dpi / 96)}; ClientToScreen(hwnd, &screen);
                return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(screen.x, screen.y));
            };
            const auto max = caption->maximize()->bounds();
            require(screen_hit({max.x + max.width / 2, 22}) == HTMAXBUTTON, "Snap maximize hit region");
            const auto tabs = caption->tabs()->bounds();
            require(screen_hit({tabs.x + 20, 22}) == HTCLIENT, "Tab action does not drag");
            require(screen_hit({12, 22}) == HTCAPTION, "Empty caption drags the OS window");
            RECT outer{}; GetWindowRect(hwnd, &outer);
            require(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(outer.left + 1, outer.top + 1)) == HTTOPLEFT,
                "Custom frame retains native resize corners");
            require(native->bounds().y >= TitleBar::height, "Nonclient layout preserves content");
            verify_caption(hwnd, *caption, theme, dpi);
            window.focus(*caption->tabs());
            SendMessageW(hwnd, WM_NEXTDLGCTL, 0, FALSE);
            require(GetFocus() == child(hwnd, L"Commands anchor"), "Tab navigation skips caption buttons");
            auto current_segment = std::static_pointer_cast<Control>(path->retained_children()[3]);
            auto previous_segment = std::static_pointer_cast<Control>(path->retained_children()[2]);
            window.focus(*current_segment);
            SendMessageW(child(hwnd, L"Current"), WM_KEYDOWN, VK_LEFT, 0);
            require(previous_segment->focused(), "Breadcrumb Left moves focus without navigation");
            SendMessageW(GetFocus(), WM_KEYDOWN, VK_RIGHT, 0);
            require(current_segment->focused() && navigations == 0, "Breadcrumb Right restores current focus without navigation");
            window.show_commands(surface, *anchor); window.dismiss_popup(*surface->popup()); flush(hwnd);
            const auto before = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
            for (int i = 0; i < 24; ++i) {
                window.show_commands(surface, *anchor); window.dismiss_popup(*surface->popup()); flush(hwnd);
            }
            require(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) == before, "Repeated menu use has stable native peers during dispatch");
            window.focus(*anchor);
            window.show_commands(surface, *anchor); flush(hwnd);
            const auto search = GetFocus();
            require(surface->editor()->focused() && IsWindowVisible(search), "Palette opens with native search focus");
            RECT edit_bounds{}; GetClientRect(search, &edit_bounds);
            require(edit_bounds.bottom >= MulDiv(16, dpi, 96), "Palette search has room for native text and caret");
            const auto expanded_bounds = surface->popup()->bounds();
            RECT client_bounds{}; GetClientRect(hwnd, &client_bounds);
            require(std::abs(expanded_bounds.x + expanded_bounds.width / 2 - client_bounds.right * 48.0f / dpi) < 1,
                "Palette centers horizontally in the window, independent of its opener");
            SendMessageW(hwnd, WM_ACTIVATE, WA_INACTIVE, 0); flush(hwnd);
            require(surface->popup()->is_open(), "Window deactivation does not dismiss the palette");
            SendMessageW(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
            window.focus(*surface->editor());
            surface->editor()->set_text(L"Nested"); surface->request(L"Nested"); flush(hwnd);
            const auto filtered_bounds = surface->popup()->bounds();
            require(filtered_bounds.height < expanded_bounds.height && filtered_bounds.height == 220 &&
                filtered_bounds.x == expanded_bounds.x && filtered_bounds.y == expanded_bounds.y,
                "Filtering shrinks the palette without moving its centered search field");
            surface->editor()->set_text(L"no result"); surface->request(L"no result"); flush(hwnd);
            require(surface->popup()->bounds().height == 220 && !surface->menu()->source()->size(),
                "An empty filter keeps only the empty-state row");
            surface->editor()->set_text(L""); surface->request(L""); flush(hwnd);
            require(surface->popup()->bounds().height == expanded_bounds.height, "Clearing search expands the palette");
            const auto rows_hwnd = child(hwnd, L"Native command rows");
            const auto hover_row = surface->menu()->item_bounds(4);
            SendMessageW(rows_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(static_cast<int>(100 * dpi / 96),
                static_cast<int>((hover_row.y + 4) * dpi / 96)));
            flush(hwnd);
            require(surface->editor()->focused() && surface->menu()->selection().focused() == ItemKey{1, 1},
                "Mouse hover preserves search focus and keyboard selection");
            const auto menu_bounds = surface->menu()->bounds();
            capture_palette(hwnd, surface->popup()->bounds(), theme, dpi,
                {menu_bounds.x + 100, menu_bounds.y + hover_row.y + 4});
            SendMessageW(rows_hwnd, WM_MOUSELEAVE, 0, 0); flush(hwnd);
            require(!surface->menu()->hovered(), "Leaving command rows clears pointer hover");
            SendMessageW(search, WM_CHAR, L'n', 0); flush(hwnd);
            require(surface->editor()->text() == L"n", "Typing immediately after opening reaches the native search");
            surface->editor()->set_text(L""); surface->request(L""); flush(hwnd);
            window.focus(*surface->menu());
            const auto popup_bounds = surface->popup()->bounds(), search_bounds = surface->editor()->bounds();
            const auto search_icon = MAKELPARAM(static_cast<int>((search_bounds.x - popup_bounds.x + 18) * dpi / 96),
                static_cast<int>((search_bounds.y - popup_bounds.y + 24) * dpi / 96));
            SendMessageW(GetParent(search), WM_LBUTTONDOWN, MK_LBUTTON, search_icon);
            SendMessageW(GetParent(search), WM_LBUTTONUP, 0, search_icon);
            require(GetFocus() == search && surface->editor()->focused(), "Clicking the search icon restores native editor focus");
            window.dismiss_popup(*surface->popup()); flush(hwnd);
            require(anchor->focused(), "Palette dismissal restores opener focus");
            window.show_commands(surface, *anchor); flush(hwnd); window.focus(*surface->menu());
            const auto menu = child(hwnd, L"Native command rows");
            SendMessageW(menu, WM_KEYDOWN, VK_DOWN, 0);
            require(surface->menu()->selection().focused() == ItemKey{4, 1}, "Native arrow skips disabled and separator");
            SendMessageW(menu, WM_KEYDOWN, VK_HOME, 0);
            SendMessageW(menu, WM_KEYDOWN, VK_F2, 0); require(pins == 1 && primary == 0, "Native pin has independent identity");
            surface->on_query([](CommandQuery) {});
            auto pending = surface->request(L"old"); surface->cancel();
            require(!surface->complete(pending, surface->menu()->commands()), "Closed query result is rejected");
            flush(hwnd);
            verify_native_print_clip(child(hwnd, L"Native input under popup"));
            verify_native_print_clip(child(hwnd, L"Native text must not paint over commands."));
            suggestion_capture::bitmap(hwnd, nullptr, std::filesystem::path(L"navigation-captures") /
                (L"commands-" + std::to_wstring(static_cast<int>(theme)) + L"-" + std::to_wstring(dpi) + L".bmp"));
            require(Drawing::live_targets() == 1, "One root render target");
            stage = 1; return true;
        }
        if (key.key == Key::f9) {
            flush(hwnd);
            require(primary == 1 && pins == 2 && !surface->popup()->is_open(), "External UIA invokes exact actions then closes menu");
            validated = true; return true;
        }
        if (key.key == Key::f10) {
            require(validated && navigations == 1, "Native and cross-process UIA navigation completed");
            done = true; stage = 2; return true;
        }
        return false;
    });
    std::jthread driver([&] {
        HWND hwnd{};
        for (int i = 0; i < 500 && !hwnd; ++i) { hwnd = FindWindowW(L"Xui.Window.1", L"XUI navigation contracts"); Sleep(10); }
        try {
            require(hwnd != nullptr, "Owned host starts"); Sleep(100); PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0);
            eventually([&] { return stage.load() == 1 || !IsWindow(hwnd); }, "Native phase starts");
            require(IsWindow(hwnd), "Native phase passes");
            std::wstring command = L"\"" + executable + L"\" --uia " + std::to_wstring(reinterpret_cast<std::uintptr_t>(hwnd));
            STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
            require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process) != FALSE, "Start external UIA client");
            const auto wait = WaitForSingleObject(process.hProcess, 30000);
            DWORD code{}; GetExitCodeProcess(process.hProcess, &code);
            if (wait == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 1);
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
            require(wait == WAIT_OBJECT_0 && code == 0, "External UIA client passed");
            eventually([&] { return stage.load() == 2; }, "Final native assertions complete");
            PostMessageW(hwnd, WM_SYSCOMMAND, SC_KEYMENU, L' ');
            const auto thread = GetWindowThreadProcessId(hwnd, nullptr);
            eventually([&] { GUITHREADINFO state{sizeof(state)}; return GetGUIThreadInfo(thread, &state) && (state.flags & GUI_INMENUMODE); },
                "Custom caption retains the native keyboard system menu");
            PostMessageW(hwnd, WM_CANCELMODE, 0, 0);
            eventually([&] { GUITHREADINFO state{sizeof(state)}; return GetGUIThreadInfo(thread, &state) && !(state.flags & GUI_INMENUMODE); },
                "Native system menu cancels without a system command");
            const auto paints = SendMessageW(hwnd, WM_APP + 60, 0, 0); Sleep(150);
            require(SendMessageW(hwnd, WM_APP + 60, 0, 0) == paints, "No idle repaint loop");
        } catch (const std::exception& error) { driver_error = error.what(); std::cerr << driver_error << '\n'; }
        if (hwnd) { PostMessageW(hwnd, WM_CANCELMODE, 0, 0); PostMessageW(hwnd, WM_CLOSE, 0, 0); }
    });
    const auto result = Application::run(window); driver.join();
    if (!window.error().empty()) std::wcerr << window.error() << L'\n';
    if (!driver_error.empty()) throw std::runtime_error(driver_error);
    require(result == 0 && done, "Native scenario completes");
    std::cout << "theme=" << static_cast<int>(theme) << " dpi=" << dpi << " native/UIA pass" << std::endl;
}
void real_shell_discovery() {
    success(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Shell fixture STA");
    struct Com { ~Com() { CoUninitialize(); } } com;
    const auto owner = CreateWindowExW(0, L"STATIC", L"XUI Shell discovery fixture", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(owner != nullptr, "Own Shell fixture host");
    struct Host { HWND hwnd; ~Host() { DestroyWindow(hwnd); } } host{owner};
    const auto folder = std::filesystem::current_path() / L"navigation-shell-fixture";
    std::filesystem::create_directories(folder);
    struct Directory { std::filesystem::path path; ~Directory() { std::filesystem::remove(path); } } directory{folder};
    auto provider = shell_command_provider(owner, {folder.wstring()});
    ShellCommandSession session(provider); session.discover();
    require(!session.commands().empty(), "Real Shell discovery returns commands without invocation");
    session.cancel(); require(session.commands().empty(), "Shell cancellation clears identities");
    bool wrong_thread{};
    std::jthread other([&] { try { provider->discover({}); } catch (const std::logic_error&) { wrong_thread = true; } });
    other.join(); require(wrong_thread, "Real Shell provider rejects another apartment thread");
    DestroyWindow(owner); host.hwnd = nullptr;
    bool closed{};
    try { provider->discover({}); } catch (const std::logic_error&) { closed = true; }
    require(closed, "Owner closure revokes real Shell discovery and invocation boundary");
}
void command_lifecycle(int mode) {
    auto window = std::make_unique<Window>(WindowOptions{L"XUI command lifecycle", {520, 440}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto anchor = std::make_shared<Button>(L"Lifecycle anchor"); root->add(anchor); window->set_content(root);
    auto surface = std::make_shared<CommandSurface>(L"Lifecycle commands", false);
    surface->menu()->set_name(L"Lifecycle rows");
    int calls{};
    surface->set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{{1, 0, L"Lifecycle action", [&] {
        ++calls;
        if (mode == 0) throw std::runtime_error("Expected command callback failure");
        if (mode == 1) window->close();
        if (mode == 2) window.reset();
        if (mode == 3) {
            auto next = std::make_shared<CommandSurface>(L"Reentrant commands", false);
            next->set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{{2, 0, L"Finish", [&] { ++calls; window->close(); }}}));
            window->show_commands(next, *anchor);
            next->menu()->execute(2);
        }
    }}}));
    window->on_key([&](const KeyEvent& key) {
        if (key.key != Key::f12) return false;
        window->show_commands(surface, *anchor);
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI command lifecycle");
        SendMessageW(child(hwnd, L"Lifecycle rows"), WM_KEYDOWN, VK_RETURN, 0);
        return true;
    });
    std::jthread driver([] {
        HWND hwnd{};
        for (int i = 0; i < 500 && !hwnd; ++i) { hwnd = FindWindowW(L"Xui.Window.1", L"XUI command lifecycle"); Sleep(10); }
        if (hwnd) { Sleep(80); PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0); }
        for (int i = 0; i < 500 && IsWindow(hwnd); ++i) Sleep(10);
        if (IsWindow(hwnd)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    });
    const auto result = Application::run(*window); driver.join();
    require(result == (mode == 0 ? 1 : 0), "Command lifecycle result");
    require(calls == (mode == 3 ? 2 : 1), "Callback runs exactly once per explicit command");
    require(!surface->popup()->is_open(), "Lifecycle closes old menu");
    if (mode == 2) require(!window, "Public Window can be deleted during command dispatch");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--uia") return client(reinterpret_cast<HWND>(std::stoull(argv[2])));
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
        for (auto theme : {ThemeMode::dark, ThemeMode::light, ThemeMode::high_contrast})
            for (UINT dpi : {96u, 144u, 192u}) run_case(theme, dpi, executable);
        require(Drawing::live_targets() == 0, "Render targets released after windows close");
        for (int mode = 0; mode < 4; ++mode) command_lifecycle(mode);
        real_shell_discovery(); std::cout << "Read-only real Shell discovery passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
