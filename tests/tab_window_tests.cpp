#include "xui/application.hpp"
#include "xui/titlebar.hpp"
#include "../src/drawing.hpp"
#include "owned_window_capture.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using namespace xui;
constexpr wchar_t title[] = L"XUI attached tab contracts";
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void flush(HWND hwnd) {
    SendMessageW(hwnd, WM_APP + 12, 0, 0);
    require(RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW),
        "Paint attached tabs");
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
    window.set_content(root);
    auto tabs = window.titlebar()->tabs();
    window.titlebar()->set_title_visible(false);
    tabs->set_tabs({{1, L"Documents"}, {2, L"Pictures"}, {3, L"A long folder name that must stay within its tab"}}, 2);
    int closed{}; std::uint64_t closed_id{};
    tabs->on_close([&](auto id) { ++closed; closed_id = id; });
    std::exception_ptr failure;
    bool complete{};
    window.on_key([&](const KeyEvent& event) {
        if (event.key != Key::f12) return false;
        try {
            const auto hwnd = FindWindowW(L"Xui.Window.1", title);
            require(hwnd != nullptr, "Find the owned tab fixture");
            const auto tab_peer = peer(hwnd, L"Title bar tabs");
            for (auto style : {VisualStyle::classic, VisualStyle::winui})
                for (auto theme : {ThemeMode::dark, ThemeMode::light, ThemeMode::high_contrast})
                    for (UINT dpi : {96u, 144u, 192u}) {
                        window.set_visual_style(style); window.set_theme(theme);
                        RECT rectangle{}; GetWindowRect(hwnd, &rectangle);
                        rectangle.right = rectangle.left + MulDiv(960, dpi, 96);
                        rectangle.bottom = rectangle.top + MulDiv(440, dpi, 96);
                        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rectangle));
                        tabs->set_enabled(true); tabs->set_focused(false);
                        SendMessageW(tab_peer, WM_MOUSELEAVE, 0, 0);
                        const auto palette = Palette::system(theme, style);
                        for (std::uint64_t selected : {1u, 2u, 3u}) {
                            tabs->select(selected); flush(hwnd);
                            const auto b = tabs->bounds(), selected_bounds = tabs->tab_bounds(selected - 1);
                            require(b.y + b.height == root->bounds().y, "Title tabs attach without a layout gap");
                            const auto pixels = owned_window_capture::capture(hwnd);
                            const float bottom = b.y + b.height;
                            require(pixel(pixels, {b.x + selected_bounds.x + 28, bottom - 0.5f}, dpi) == rgb(palette.background) &&
                                pixel(pixels, {b.x + selected_bounds.x + 28, bottom + 2}, dpi) == rgb(palette.background),
                                "Selected tab has no bottom border or contrasting gap into the content");
                            const auto inactive = tabs->tab_bounds(selected == 1 ? 1 : 0);
                            require(pixel(pixels, {b.x + inactive.x + 28, bottom - 0.5f}, dpi) == rgb(palette.border),
                                "The baseline remains under inactive tabs");
                            const auto s = standalone->bounds(), active = standalone->tab_bounds(1);
                            require(pixel(pixels, {s.x + active.x + 28, s.y + s.height - 0.5f}, dpi) == rgb(palette.surface) &&
                                pixel(pixels, {s.x + active.x + 28, s.y + s.height + 2}, dpi) == rgb(palette.surface),
                                "Tabs on a card join the card surface, not the window background");
                            if (!captures.empty() && dpi == 96 && selected == 2)
                                save(pixels, captures / (std::to_wstring(static_cast<int>(style)) + L"-" +
                                    std::to_wstring(static_cast<int>(theme)) + L".bmp"));
                        }
                        tabs->select(2); flush(hwnd);
                        const auto close = tabs->close_bounds(1);
                        const auto at = [&](float x, float y) {
                            return MAKELPARAM(static_cast<short>(std::lround(x * dpi / 96)),
                                static_cast<short>(std::lround(y * dpi / 96)));
                        };
                        const auto inactive = tabs->tab_bounds(0);
                        SendMessageW(tab_peer, WM_MOUSEMOVE, 0, at(inactive.x + 8, 10)); flush(hwnd);
                        auto pixels = owned_window_capture::capture(hwnd);
                        const auto b = tabs->bounds();
                        require(pixel(pixels, {b.x + inactive.x + 8, b.y + 10}, dpi) == rgb(palette.hover) &&
                            pixel(pixels, {b.x + inactive.x + 28, b.y + b.height - 0.5f}, dpi) == rgb(palette.border),
                            "An inactive hover keeps its bottom edge closed");
                        SendMessageW(tab_peer, WM_MOUSEMOVE, 0, at(close.x + 3, close.y + 3)); flush(hwnd);
                        pixels = owned_window_capture::capture(hwnd);
                        require(pixel(pixels, {b.x + close.x + 3, b.y + close.y + 3}, dpi) ==
                            rgb(palette.high_contrast ? palette.selection : palette.hover), "Close hover paints only its target");
                        const auto before = closed;
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
                    }
            std::vector<TabItem> many;
            for (std::uint64_t id = 1; id <= 16; ++id) many.push_back({id, L"Folder " + std::to_wstring(id)});
            tabs->set_tabs(many, 16); flush(hwnd);
            require(tabs->tab_bounds(15).width >= 120 && tabs->close_bounds(15).width == 24,
                "Overflow reveals the selected tab and its close target");
            window.set_visual_style(VisualStyle::classic); window.set_theme(ThemeMode::dark);
            flush(hwnd);
            require(tabs->selected() == 16 && tabs->tabs().size() == 16, "Style changes preserve tab state");
            complete = true;
        } catch (...) { failure = std::current_exception(); }
        window.close();
        return true;
    });
    const auto started = GetTickCount64();
    const auto timer = SetTimer(nullptr, 0, 20, [](HWND, UINT, UINT_PTR, DWORD) {
        if (auto hwnd = FindWindowW(L"Xui.Window.1", title); hwnd && IsWindowVisible(hwnd))
            PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0);
    });
    require(timer != 0, "Start the tab fixture");
    const auto result = Application::run(window);
    KillTimer(nullptr, timer);
    if (failure) std::rethrow_exception(failure);
    require(result == 0 && complete && GetTickCount64() - started < 180000, "Complete the attached tab fixture");
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
