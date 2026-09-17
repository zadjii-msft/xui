#include "xui/application.hpp"
#include "xui/titlebar.hpp"
#include "../src/drawing.hpp"
#include "owned_window_capture.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <UIAutomation.h>
#include <atomic>
#include <thread>

namespace {
using namespace xui;
constexpr wchar_t title[] = L"XUI attached tab contracts";
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void flush(HWND hwnd) {
    SendMessageW(hwnd, WM_APP + 12, 0, 0);
    require(RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW),
        "Paint attached tabs");
    winrt::check_hresult(DwmFlush());
}
DWORD rgb(D2D1_COLOR_F color) {
    return (static_cast<DWORD>(std::lround(color.r * 255)) << 16) |
        (static_cast<DWORD>(std::lround(color.g * 255)) << 8) | static_cast<DWORD>(std::lround(color.b * 255));
}
DWORD pixel(const owned_window_capture::Pixels& pixels, Point point, UINT dpi) {
    const int x = static_cast<int>(point.x * dpi / 96), y = static_cast<int>(point.y * dpi / 96);
    require(x >= 0 && y >= 0 && x < pixels.width && y < pixels.height, "Tab sample is inside the owned client");
    return pixels.data[y * pixels.width + x] & 0xffffff;
}
void save(const owned_window_capture::Pixels& pixels, const std::filesystem::path& path) {
    BITMAPINFOHEADER info{sizeof(BITMAPINFOHEADER), pixels.width, -pixels.height, 1, 32, BI_RGB};
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(info);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.data.size() * sizeof(DWORD));
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info), sizeof(info));
    file.write(reinterpret_cast<const char*>(pixels.data.data()), pixels.data.size() * sizeof(DWORD));
    require(file.good(), "Write the owned tab capture");
}
HWND peer(HWND host, const wchar_t* name) {
    struct State { const wchar_t* name; HWND result{}; } state{name};
    EnumChildWindows(host, [](HWND hwnd, LPARAM data) -> BOOL {
        auto& state = *reinterpret_cast<State*>(data);
        wchar_t text[100]{}; GetWindowTextW(hwnd, text, 100);
        if (std::wstring_view(text) == state.name) { state.result = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&state));
    require(state.result != nullptr, "Find the owned tab peer");
    return state.result;
}
Rect paint_bounds(HWND host, HWND control, UINT dpi) {
    RECT bounds{}; GetWindowRect(control, &bounds);
    MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&bounds), 2);
    return {bounds.left * 96.0f / dpi, bounds.top * 96.0f / dpi,
        (bounds.right - bounds.left) * 96.0f / dpi, (bounds.bottom - bounds.top) * 96.0f / dpi};
}
void require_open_bottom(const owned_window_capture::Pixels& pixels, HWND host, HWND control,
    Rect tab, UINT dpi, DWORD color) {
    RECT bounds{}; GetWindowRect(control, &bounds);
    MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&bounds), 2);
    const int left = bounds.left + static_cast<int>(std::ceil((tab.x + 4) * dpi / 96));
    const int right = bounds.left + static_cast<int>(std::floor((tab.x + tab.width - 4) * dpi / 96));
    for (int y = bounds.bottom - 3; y < bounds.bottom; ++y)
        for (int x = left; x < right; ++x) {
            require(x >= 0 && y >= 0 && x < pixels.width && y < pixels.height, "Bottom edge is inside the owned client");
            const auto actual = pixels.data[y * pixels.width + x] & 0xffffff;
            const auto channel_matches = [&](int shift) {
                return std::abs(static_cast<int>((actual >> shift) & 255) - static_cast<int>((color >> shift) & 255)) <= 1;
            };
            if (!channel_matches(16) || !channel_matches(8) || !channel_matches(0)) {
                std::cerr << "Bottom edge at " << dpi << " DPI, pixel (" << x << ", " << y << "): "
                    << std::hex << actual << " != " << color << std::dec << '\n';
                throw std::runtime_error("The full selected tab bottom must be open, including fractional-DPI pixels");
            }
        }
}
void new_tab_accessibility(HWND host, HWND strip, std::atomic<int>& created) {
    std::exception_ptr failure;
    std::atomic<bool> done{};
    std::thread driver([&] {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            winrt::check_hresult(initialized);
            using Microsoft::WRL::ComPtr;
            ComPtr<IUIAutomation> automation;
            winrt::check_hresult(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)));
            ComPtr<IUIAutomationElement> root;
            winrt::check_hresult(automation->ElementFromHandle(strip, &root));
            VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(L"New tab");
            ComPtr<IUIAutomationCondition> name, type, condition;
            const auto named = automation->CreatePropertyCondition(UIA_NamePropertyId, value, &name);
            VariantClear(&value); winrt::check_hresult(named);
            value.vt = VT_I4; value.lVal = UIA_ButtonControlTypeId;
            winrt::check_hresult(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, value, &type));
            winrt::check_hresult(automation->CreateAndCondition(name.Get(), type.Get(), &condition));
            ComPtr<IUIAutomationElement> button;
            winrt::check_hresult(root->FindFirst(TreeScope_Descendants, condition.Get(), &button));
            require(button != nullptr, "Tab strip UIA descendants include the native New tab Button");
            RECT bounds{}; winrt::check_hresult(button->get_CurrentBoundingRectangle(&bounds));
            require(bounds.right > bounds.left && bounds.bottom > bounds.top, "New tab exposes a visible UIA rectangle");
            ComPtr<IUIAutomationInvokePattern> invoke;
            winrt::check_hresult(button->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke)));
            const auto before = created.load();
            winrt::check_hresult(invoke->Invoke());
            require(created == before + 1, "UIA Invoke dispatches exactly one new-tab action");
            winrt::check_hresult(button->SetFocus());
            BOOL focused{}; winrt::check_hresult(button->get_CurrentHasKeyboardFocus(&focused));
            require(focused, "UIA focus reaches the new-tab button without selecting a tab");
        } catch (...) { failure = std::current_exception(); }
        if (SUCCEEDED(initialized)) CoUninitialize();
        done = true; PostMessageW(host, WM_NULL, 0, 0);
    });
    while (!done) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    driver.join();
    if (failure) std::rethrow_exception(failure);
}
void run(const std::filesystem::path& captures) {
    Window window({title, {960, 400}, ThemeMode::dark, {}, true});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto spacer = std::make_shared<Stack>(Axis::vertical); spacer->set_fixed_size({0, 64}); root->add(spacer);
    auto card = std::make_shared<Stack>(Axis::vertical); card->set_surface(true);
    auto standalone = std::make_shared<TabStrip>(L"Standalone attached tabs");
    standalone->set_tabs({{1, L"Details"}, {2, L"Preview"}, {3, L"History"}}, 2);
    card->add(standalone);
    auto content = std::make_shared<Stack>(Axis::vertical); content->set_fixed_size({0, 80}); card->add(content);
    root->add(card);
    auto editor = std::make_shared<TextInput>(L"Tab content");
    editor->set_caption_visible(false);
    root->add(editor);
    auto pane_row = std::make_shared<Stack>(Axis::horizontal);
    auto sidebar = std::make_shared<Label>(L"Navigation"); sidebar->set_fixed_size({120, 80});
    auto first_pane = std::make_shared<Label>(L"First pane"), second_pane = std::make_shared<Label>(L"Second pane");
    auto split = std::make_shared<SplitView>(first_pane, second_pane);
    pane_row->add(sidebar); pane_row->add(split, 1); root->add(pane_row, 1);
    window.set_content(root);
    auto tabs = window.titlebar()->tabs();
    window.titlebar()->set_title_visible(false);
    tabs->set_tabs({{1, L"Documents"}, {2, L"Pictures"}, {3, L"A long folder name that must stay within its tab"}}, 2);
    int closed{}; std::uint64_t closed_id{};
    tabs->on_close([&](auto id) { ++closed; closed_id = id; });
    int activated{}, tab_focus{};
    std::atomic<int> created{};
    tabs->on_new_tab([&] { ++created; });
    tabs->on_focus([&] { ++tab_focus; });
    tabs->on_activate([&](auto) {
        ++activated;
        require(window.focus(*editor), "Tab activation transfers focus to its content");
    });
    std::exception_ptr failure;
    bool complete{};
    window.on_key([&](const KeyEvent& event) {
        if (event.key != Key::f12) return false;
        try {
            const auto hwnd = FindWindowW(L"Xui.Window.1", title);
            require(hwnd != nullptr, "Find the owned tab fixture");
            const auto tab_peer = peer(hwnd, L"Title bar tabs");
            const auto standalone_peer = peer(hwnd, L"Standalone attached tabs");
            for (auto style : {VisualStyle::classic, VisualStyle::winui})
                for (auto theme : {ThemeMode::dark, ThemeMode::light, ThemeMode::high_contrast})
                    for (UINT dpi : {96u, 120u, 144u, 168u, 192u}) {
                        std::cout << "Tab pixels: style " << static_cast<int>(style) << ", theme " <<
                            static_cast<int>(theme) << ", DPI " << dpi << std::endl;
                        window.set_visual_style(style); window.set_theme(theme);
                        RECT rectangle{}; GetWindowRect(hwnd, &rectangle);
                        rectangle.right = rectangle.left + MulDiv(960, dpi, 96);
                        rectangle.bottom = rectangle.top + MulDiv(440, dpi, 96);
                        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rectangle));
                        tabs->set_enabled(true);
                        require(window.focus(*editor), "Start with focus inside tab content");
                        SendMessageW(tab_peer, WM_MOUSELEAVE, 0, 0);
                        const auto palette = Palette::system(theme, style);
                        for (std::uint64_t selected : {1u, 2u, 3u}) {
                            tabs->select(selected); flush(hwnd);
                            const auto b = paint_bounds(hwnd, tab_peer, dpi), selected_bounds = tabs->tab_bounds(selected - 1);
                            require(tabs->bounds().y + tabs->bounds().height == root->bounds().y, "Title tabs attach without a layout gap");
                            const auto pixels = owned_window_capture::capture(hwnd);
                            const float bottom = b.y + b.height;
                            require(pixel(pixels, {b.x + b.width - 8, b.y + 12}, dpi) == rgb(palette.background),
                                "Unused title tab row blends into the titlebar instead of painting a rectangle");
                            require(pixel(pixels, {b.x + selected_bounds.x + 28, bottom - 0.5f}, dpi) == rgb(palette.background) &&
                                pixel(pixels, {b.x + selected_bounds.x + 28, bottom + 2}, dpi) == rgb(palette.background),
                                "Selected tab has no bottom border or contrasting gap into the content");
                            const auto inactive = tabs->tab_bounds(selected == 1 ? 1 : 0);
                            require(pixel(pixels, {b.x + inactive.x + 8, b.y + 12}, dpi) == rgb(palette.background),
                                "Resting inactive title tabs inherit the titlebar background");
                            require(pixel(pixels, {b.x + inactive.x + 28, bottom - 0.5f}, dpi) == rgb(palette.border),
                                "The baseline remains under inactive tabs");
                            const auto s = paint_bounds(hwnd, standalone_peer, dpi), active = standalone->tab_bounds(1);
                            require_open_bottom(pixels, hwnd, tab_peer, selected_bounds, dpi, rgb(palette.background));
                            require_open_bottom(pixels, hwnd, standalone_peer, active, dpi, rgb(palette.surface));
                            require(pixel(pixels, {s.x + s.width - 8, s.y + 12}, dpi) == rgb(palette.surface),
                                "Unused card tab row inherits the card surface");
                            require(pixel(pixels, {s.x + active.x + 28, s.y + s.height - 0.5f}, dpi) == rgb(palette.surface) &&
                                pixel(pixels, {s.x + active.x + 28, s.y + s.height + 2}, dpi) == rgb(palette.surface),
                                "Tabs on a card join the card surface, not the window background");
                            if (!captures.empty() && dpi == 96 && selected == 2)
                                save(pixels, captures / (std::to_wstring(static_cast<int>(style)) + L"-" +
                                    std::to_wstring(static_cast<int>(theme)) + L".bmp"));
                        }
                        const auto text_tabs = tabs->tabs();
                        const auto text_selection = tabs->selected();
                        const auto text_pixels = owned_window_capture::capture(hwnd);
                        const auto icon_band = paint_bounds(hwnd, tab_peer, dpi);
                        auto icon_tabs = text_tabs;
                        for (auto& tab : icon_tabs) tab.icon = ButtonIcon::folder;
                        tabs->set_tabs(icon_tabs, tabs->selected()); flush(hwnd);
                        const auto icon_pixels = owned_window_capture::capture(hwnd);
                        for (size_t i = 0; i < icon_tabs.size(); ++i) {
                            const auto tab = tabs->tab_bounds(i);
                            bool different{};
                            for (int y = 12; y < static_cast<int>(tab.height - 5); ++y)
                                for (int x = 12; x < 28; ++x) {
                                    const Point p{icon_band.x + tab.x + x, icon_band.y + tab.y + y};
                                    different = different || pixel(text_pixels, p, dpi) != pixel(icon_pixels, p, dpi);
                                }
                            require(different, "Selected and inactive tab icons produce visible pixels at every theme and DPI");
                        }
                        const auto before_icon_activation = activated;
                        const auto icon_tab = tabs->tab_bounds(0);
                        SendMessageW(tab_peer, WM_LBUTTONDOWN, MK_LBUTTON,
                            MAKELPARAM(MulDiv(static_cast<int>(icon_tab.x + 20), dpi, 96), MulDiv(20, dpi, 96)));
                        require(tabs->selected() == text_tabs[0].id && activated == before_icon_activation + 1 && editor->focused(),
                            "Clicking a tab icon activates its stable tab identity and content");
                        tabs->set_tabs(text_tabs, text_selection); flush(hwnd);
                        const TabColors custom{0x122334, 0x244668, 0xffdd33, 0x304050, 0x44eecc, 0x486888, 0x99aabb};
                        tabs->select(2);
                        tabs->set_colors(custom); flush(hwnd);
                        auto custom_pixels = owned_window_capture::capture(hwnd);
                        const auto band = paint_bounds(hwnd, tab_peer, dpi), selected_tab = tabs->tab_bounds(1), inactive_tab = tabs->tab_bounds(0);
                        const auto expected = [&](std::uint32_t value, D2D1_COLOR_F fallback) {
                            return palette.high_contrast ? rgb(fallback) : value;
                        };
                        require(pixel(custom_pixels, {band.x + band.width - 8, band.y + 12}, dpi) ==
                            expected(*custom.row_background, palette.background), "Custom row color is live; high contrast uses the system background");
                        require(pixel(custom_pixels, {band.x + selected_tab.x + 8, band.y + 12}, dpi) ==
                            expected(*custom.selected_background, palette.background), "Custom selected tab fill respects high contrast");
                        require_open_bottom(custom_pixels, hwnd, tab_peer, selected_tab, dpi,
                            expected(*custom.selected_background, palette.background));
                        require(pixel(custom_pixels, {band.x + inactive_tab.x + 8, band.y + 12}, dpi) ==
                            expected(*custom.inactive_background, palette.background), "Custom inactive tab fill respects high contrast");
                        require(pixel(custom_pixels, {band.x + 28, band.y + band.height - 0.5f}, dpi) ==
                            expected(*custom.border, palette.border), "Custom border applies without closing the selected tab");
                        if (!palette.high_contrast) {
                            const auto has_text_color = [&](Rect tab, std::uint32_t color) {
                                for (int y = 10; y < static_cast<int>(band.height - 8); ++y)
                                    for (int x = 12; x < 85; ++x)
                                        if (pixel(custom_pixels, {band.x + tab.x + x, band.y + y}, dpi) == color) return true;
                                return false;
                            };
                            require(has_text_color(selected_tab, *custom.selected_text) &&
                                has_text_color(inactive_tab, *custom.inactive_text), "Selected and inactive text colors reach glyph rendering");
                        }
                        SendMessageW(tab_peer, WM_MOUSEMOVE, 0, MAKELPARAM(MulDiv(8, dpi, 96), MulDiv(12, dpi, 96)));
                        flush(hwnd); custom_pixels = owned_window_capture::capture(hwnd);
                        require(pixel(custom_pixels, {band.x + 8, band.y + 12}, dpi) ==
                            expected(*custom.hover_background, palette.hover), "Custom hover color respects system high contrast");
                        require(tabs->selected() == 2 && peer(hwnd, L"Title bar tabs") == tab_peer,
                            "Color changes preserve selected identity and the native peer");
                        TabColors row_only;
                        row_only.row_background = custom.row_background;
                        tabs->set_colors(row_only);
                        SendMessageW(tab_peer, WM_MOUSELEAVE, 0, 0); flush(hwnd);
                        custom_pixels = owned_window_capture::capture(hwnd);
                        require(pixel(custom_pixels, {band.x + inactive_tab.x + 8, band.y + 12}, dpi) ==
                            expected(*custom.row_background, palette.background) &&
                            pixel(custom_pixels, {band.x + selected_tab.x + 8, band.y + 12}, dpi) == rgb(palette.background),
                            "Clearing individual overrides restores inheritance while preserving the row color");
                        tabs->set_colors({});
                        SendMessageW(tab_peer, WM_MOUSELEAVE, 0, 0); flush(hwnd);
                        custom_pixels = owned_window_capture::capture(hwnd);
                        require(pixel(custom_pixels, {band.x + band.width - 8, band.y + 12}, dpi) == rgb(palette.background),
                            "Clearing colors restores the current theme without recreating the control");
                        tabs->select(2); flush(hwnd);
                        const auto close = tabs->close_bounds(1);
                        const auto at = [&](float x, float y) {
                            return MAKELPARAM(static_cast<short>(std::lround(x * dpi / 96)),
                                static_cast<short>(std::lround(y * dpi / 96)));
                        };
                        const auto inactive = tabs->tab_bounds(0);
                        SendMessageW(tab_peer, WM_MOUSEMOVE, 0, at(inactive.x + 8, 10)); flush(hwnd);
                        auto pixels = owned_window_capture::capture(hwnd);
                        const auto b = paint_bounds(hwnd, tab_peer, dpi);
                        require(pixel(pixels, {b.x + inactive.x + 8, b.y + 10}, dpi) == rgb(palette.hover) &&
                            pixel(pixels, {b.x + inactive.x + 28, b.y + b.height - 0.5f}, dpi) == rgb(palette.border),
                            "An inactive hover keeps its bottom edge closed");
                        SendMessageW(tab_peer, WM_MOUSEMOVE, 0, at(close.x + 3, close.y + 3)); flush(hwnd);
                        pixels = owned_window_capture::capture(hwnd);
                        require(pixel(pixels, {b.x + close.x + 3, b.y + close.y + 3}, dpi) ==
                            rgb(palette.high_contrast ? palette.selection : palette.hover), "Close hover paints only its target");
                        const auto before = closed;
                        const auto before_activation = activated, before_focus = tab_focus;
                        SendMessageW(tab_peer, WM_LBUTTONDOWN, MK_LBUTTON, at(inactive.x + 12, 20));
                        require(tabs->selected() == 1 && editor->focused() && !tabs->focused() &&
                            activated == before_activation + 1 && tab_focus == before_focus,
                            "Clicking another tab focuses content without transient tab focus");
                        SendMessageW(tab_peer, WM_LBUTTONDOWN, MK_LBUTTON, at(inactive.x + 12, 20));
                        require(editor->focused() && activated == before_activation + 2 && tab_focus == before_focus,
                            "Clicking the current tab reactivates content without tab focus");
                        tabs->select(2);
                        SendMessageW(tab_peer, WM_LBUTTONDOWN, MK_LBUTTON, at(close.x + 12, 1));
                        require(closed == before, "The top of a tab is not part of its close button");
                        SendMessageW(tab_peer, WM_LBUTTONDOWN, MK_LBUTTON, at(close.x + 12, close.y + 12));
                        require(closed == before + 1 && closed_id == 2, "The painted close target dispatches its stable identity");
                        tabs->set_enabled(false);
                        SendMessageW(tab_peer, WM_LBUTTONDOWN, MK_LBUTTON, at(close.x + 12, close.y + 12));
                        require(closed == before + 1, "Disabled tabs do not close");
                        tabs->set_enabled(true);
                        require(window.focus(*standalone), "Move native focus away from the disabled tab peer");
                        require(window.focus(*tabs), "Keyboard focus reaches the tab strip");
                        SendMessageW(tab_peer, WM_KEYDOWN, VK_RIGHT, 0);
                        require(tabs->selected() == 3 && tabs->focused(), "Arrow keys select tabs and retain focus");
                        flush(hwnd);
                        pixels = owned_window_capture::capture(hwnd);
                        const auto focused = tabs->tab_bounds(2);
                        require(pixel(pixels, {b.x + focused.x + 28, b.y + b.height - 0.5f}, dpi) == rgb(palette.background),
                            "The focus ring does not close the attached bottom edge");
                        const Point ring{b.x + focused.x + 28, b.y + 5};
                        require(pixel(pixels, ring, dpi) != rgb(palette.background), "Keyboard tab focus has a visible rectangle");
                        SendMessageW(tab_peer, WM_LBUTTONDOWN, MK_LBUTTON, at(focused.x + 28, 20));
                        flush(hwnd); pixels = owned_window_capture::capture(hwnd);
                        require(editor->focused() && !tabs->focused() && pixel(pixels, ring, dpi) == rgb(palette.background),
                            "A pointer click removes the keyboard rectangle and returns focus to content");
                        for (auto key : {VK_RETURN, VK_SPACE}) {
                            require(window.focus(*tabs), "Focus the strip for keyboard activation");
                            SendMessageW(tab_peer, WM_KEYDOWN, key, 0);
                            require(editor->focused() && !tabs->focused(), "Enter and Space focus selected tab content");
                        }
                        const auto saved_tabs = standalone->tabs();
                        const auto saved_title_tabs = tabs->tabs();
                        standalone->set_tabs({}, {}); tabs->set_tabs({}, {}); flush(hwnd);
                        pixels = owned_window_capture::capture(hwnd);
                        const auto empty = paint_bounds(hwnd, standalone_peer, dpi);
                        require(pixel(pixels, {empty.x + 28, empty.y + 12}, dpi) == rgb(palette.surface),
                            "An empty tab strip inherits its parent surface");
                        require_open_bottom(pixels, hwnd, standalone_peer, {0, 0, empty.width, empty.height}, dpi, rgb(palette.surface));
                        require_open_bottom(pixels, hwnd, tab_peer, {0, 0, b.width, b.height}, dpi, rgb(palette.background));
                        standalone->set_tabs(saved_tabs, 2);
                        tabs->set_tabs(saved_title_tabs, 2);
                    }
            std::vector<TabItem> many;
            for (std::uint64_t id = 1; id <= 16; ++id) many.push_back({id, L"Folder " + std::to_wstring(id)});
            tabs->set_tabs(many, 16); flush(hwnd);
            require(tabs->tab_bounds(15).width >= 120 && tabs->close_bounds(15).width == 24,
                "Overflow reveals the selected tab and its close target");
            const TabColors persistent{0x122334, 0x244668, 0xffdd33};
            tabs->set_colors(persistent);
            window.set_visual_style(VisualStyle::classic); window.set_theme(ThemeMode::dark);
            flush(hwnd);
            require(tabs->selected() == 16 && tabs->tabs().size() == 16, "Style changes preserve tab state");
            require(tabs->colors() == persistent, "Theme and style changes preserve authored colors");
            tabs->set_colors({});
            auto second_tabs = window.titlebar()->secondary_tabs();
            second_tabs->set_visible(true);
            window.titlebar()->leading()->set_visible(true);
            window.titlebar()->set_tab_panes(first_pane, second_pane);
            tabs->set_tabs({{101, L"First folder"}, {102, L"Other folder"}}, 101);
            second_tabs->set_tabs({{201, L"Second folder"}, {202, L"Other folder"}}, 202);
            tabs->set_new_tab_button_visible(true);
            second_tabs->set_new_tab_button_visible(true);
            for (auto style : {VisualStyle::classic, VisualStyle::winui})
                for (auto theme : {ThemeMode::dark, ThemeMode::light, ThemeMode::high_contrast})
                    for (UINT dpi : {96u, 120u, 144u, 168u, 192u}) {
                        window.set_visual_style(style); window.set_theme(theme);
                        RECT rectangle{}; GetWindowRect(hwnd, &rectangle);
                        rectangle.right = rectangle.left + MulDiv(960, dpi, 96);
                        rectangle.bottom = rectangle.top + MulDiv(440, dpi, 96);
                        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rectangle));
                        flush(hwnd);
                        const auto palette = Palette::system(theme, style);
                        const auto first_band = paint_bounds(hwnd, tab_peer, dpi);
                        const auto second_peer = peer(hwnd, L"Secondary title bar tabs");
                        const auto second_band = paint_bounds(hwnd, second_peer, dpi);
                        const auto pixels = owned_window_capture::capture(hwnd);
                        const float bottom = first_band.y + first_band.height;
                        for (float x : {12.0f, 22.0f, first_band.x - 4,
                            (first_band.x + first_band.width + second_band.x) / 2,
                            second_band.x + second_band.width + 4, window.titlebar()->bounds().width - 2})
                            require(pixel(pixels, {x, bottom - .5f}, dpi) == rgb(palette.border),
                                "The titlebar baseline spans navigation, split and caption gaps");
                        require_open_bottom(pixels, hwnd, tab_peer, tabs->tab_bounds(0), dpi, rgb(palette.background));
                        require_open_bottom(pixels, hwnd, second_peer, second_tabs->tab_bounds(1), dpi, rgb(palette.background));
                        require(tabs->bounds().x == first_pane->bounds().x && second_tabs->bounds().x == second_pane->bounds().x,
                            "The baseline preserves independent pane alignment");
                        const auto add = tabs->new_tab_button_bounds(), last = tabs->tab_bounds(1);
                        require(add.x == last.x + last.width && add.x + add.width <= tabs->bounds().width,
                            "New tab appears directly after the last visible tab");
                        require(window.titlebar()->hit_test({tabs->bounds().x + add.x + add.width / 2,
                            tabs->bounds().y + add.y + add.height / 2}) == CaptionHit::client,
                            "The new-tab action is not a window-drag region");
                    }
            const auto new_button = peer(tab_peer, L"New tab");
            const auto before_created = created.load();
            SendMessageW(new_button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
            require(GetCapture() == new_button && created == before_created, "New tab uses standard press capture without premature activation");
            SendMessageW(new_button, WM_CANCELMODE, 0, 0);
            SendMessageW(new_button, WM_LBUTTONUP, 0, MAKELPARAM(8, 8));
            require(created == before_created && GetCapture() != new_button, "Cancelled new-tab clicks do not dispatch");
            SendMessageW(new_button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
            SendMessageW(new_button, WM_LBUTTONUP, 0, MAKELPARAM(8, 8));
            require(created == before_created + 1 && tabs->selected() == 101, "Pointer release dispatches a new tab without changing selection");
            tabs->set_enabled(false); flush(hwnd);
            SendMessageW(new_button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
            SendMessageW(new_button, WM_LBUTTONUP, 0, MAKELPARAM(8, 8));
            require(created == before_created + 1, "Disabled strips disable the new-tab action");
            tabs->set_enabled(true); flush(hwnd);
            new_tab_accessibility(hwnd, tab_peer, created);
            require(tabs->selected() == 101, "UIA button focus and invocation preserve selected identity");
            const auto before_keys = created.load();
            for (auto key : {VK_RETURN, VK_SPACE}) {
                SendMessageW(new_button, WM_KEYDOWN, key, 0);
                SendMessageW(new_button, WM_KEYUP, key, 0);
            }
            require(created == before_keys + 2, "Enter and Space each invoke the focused native new-tab button once");
            tabs->set_new_tab_button_visible(false); flush(hwnd);
            require(!IsWindowVisible(new_button), "Opt-out hides the retained native action");
            complete = true;
        } catch (...) { failure = std::current_exception(); }
        window.close();
        return true;
    });
    const auto started = GetTickCount64();
    const auto timer = SetTimer(nullptr, 0, 20, [](HWND, UINT, UINT_PTR timer, DWORD) {
        if (auto hwnd = FindWindowW(L"Xui.Window.1", title); hwnd && IsWindowVisible(hwnd)) {
            KillTimer(nullptr, timer);
            PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0);
        }
    });
    require(timer != 0, "Start the tab fixture");
    const auto result = Application::run(window);
    KillTimer(nullptr, timer);
    if (failure) std::rethrow_exception(failure);
    require(result == 0 && complete && GetTickCount64() - started < 300000, "Complete the attached tab fixture");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        const auto directory = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{};
        if (!directory.empty()) std::filesystem::create_directories(directory);
        run(directory);
        std::cout << "Attached tab pixels, themes, DPI, close targets, keyboard focus and overflow passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
