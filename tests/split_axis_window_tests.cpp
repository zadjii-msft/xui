#include "xui/application.hpp"
#include <windows.h>
#include <cmath>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
HWND peer(HWND root, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result{}; } search{name};
    EnumChildWindows(root, [](HWND hwnd, LPARAM context) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(context);
        wchar_t name[80]{};
        GetWindowTextW(hwnd, name, 80);
        if (std::wstring_view(name) == search.name) { search.result = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Find owned split peer");
    return search.result;
}
LPARAM point(const xui::SplitView& split, UINT dpi, float ratio) {
    const auto area = split.pane_area(), bounds = split.bounds();
    const auto width = split.effective_divider_width();
    const bool horizontal = split.axis() == xui::Axis::horizontal;
    const float x = area.x - bounds.x + (horizontal ? (area.width - width) * ratio + width / 2 : area.width / 2);
    const float y = area.y - bounds.y + (horizontal ? area.height / 2 : (area.height - width) * ratio + width / 2);
    return MAKELPARAM(static_cast<int>(std::lround(x * dpi / 96)), static_cast<int>(std::lround(y * dpi / 96)));
}
void flush(HWND window) { SendMessageW(window, WM_APP + 12, 0, 0); }
void exercise(HWND root, xui::SplitView& split, HWND hwnd, HWND foreground) {
    const auto dpi = GetDpiForWindow(root);
    split.set_ratio(.5f);
    flush(root);
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, point(split, dpi, .5f));
    require(GetCapture() == hwnd && split.divider_dragging(), "Divider captures a press");
    SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, point(split, dpi, .37f));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, point(split, dpi, .37f));
    flush(root);
    require(std::abs(split.ratio() - .37f) < .005f, "Axis-specific pointer movement changes ratio");
    require(GetCapture() != hwnd && !split.divider_dragging(), "Release ends divider capture");
    const bool horizontal = split.axis() == xui::Axis::horizontal;
    const auto before = split.ratio();
    SendMessageW(hwnd, WM_KEYDOWN, horizontal ? VK_DOWN : VK_RIGHT, 0);
    require(split.ratio() == before, "Orthogonal arrow leaves ratio unchanged");
    SendMessageW(hwnd, WM_KEYDOWN, horizontal ? VK_RIGHT : VK_DOWN, 0);
    require(std::abs(split.ratio() - before - .025f) < .0001f, "Axis arrow resizes the divider");
    SendMessageW(hwnd, WM_KEYDOWN, VK_HOME, 0);
    flush(root);
    require(split.ratio() == .5f, "Home restores the midpoint");
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, point(split, dpi, .5f));
    SendMessageW(hwnd, WM_CANCELMODE, 0, 0);
    SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, point(split, dpi, .8f));
    require(split.ratio() == .5f && GetCapture() != hwnd, "Cancellation stops later movement");
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, point(split, dpi, .5f));
    split.set_layout(horizontal ? xui::Axis::vertical : xui::Axis::horizontal, 48);
    flush(root);
    require(GetCapture() != hwnd && !split.divider_dragging(), "Layout replacement cancels capture");
    require(GetForegroundWindow() == foreground, "Owned divider messages do not activate the fixture");
}
}

int main() {
    try {
        xui::Application application;
        xui::WindowOptions options;
        options.title = L"XUI background split axis contracts";
        options.size = {960, 640};
        options.show_activated = false;
        auto window = application.create_window(options);
        auto first = std::make_shared<xui::Label>(L"First terminal placeholder");
        auto top = std::make_shared<xui::Label>(L"Top terminal placeholder");
        auto bottom = std::make_shared<xui::Label>(L"Bottom terminal placeholder");
        auto rows = std::make_shared<xui::SplitView>(top, bottom, L"Row divider");
        rows->set_layout(xui::Axis::vertical, 48);
        auto columns = std::make_shared<xui::SplitView>(first, rows, L"Column divider");
        columns->set_layout(xui::Axis::horizontal, 48);
        int changes{};
        columns->on_ratio_changed([&](float) { ++changes; });
        auto content = std::make_shared<xui::Stack>(xui::Axis::vertical);
        content->add(columns, 1);
        window->set_content(content);
        const auto foreground = GetForegroundWindow();
        application.show(*window);
        const auto root = FindWindowW(L"Xui.Window.1", options.title.c_str());
        DWORD process{};
        GetWindowThreadProcessId(root, &process);
        require(root && process == GetCurrentProcessId(), "Fixture owns the target window");
        require(GetForegroundWindow() == foreground, "Fixture creation does not activate");
        const auto column_peer = peer(root, L"Column divider");
        const auto row_peer = peer(root, L"Row divider");
        exercise(root, *columns, column_peer, foreground);
        columns->set_layout(xui::Axis::horizontal, 48);
        flush(root);
        exercise(root, *rows, row_peer, foreground);
        rows->set_layout(xui::Axis::vertical, 48);
        flush(root);
        const auto a = columns->first()->bounds(), b = rows->bounds();
        const auto c = rows->first()->bounds(), d = rows->second()->bounds();
        require(a.x + a.width < b.x && a.y == b.y, "Nested columns keep their divider gap");
        require(c.y + c.height < d.y && c.x == d.x, "Nested rows keep their divider gap");
        require(changes >= 3, "Pointer and keyboard updates publish ratio changes");
        columns->set_ratio(.5f);
        flush(root);
        SendMessageW(column_peer, WM_LBUTTONDOWN, MK_LBUTTON, point(*columns, GetDpiForWindow(root), .5f));
        columns->set_secondary_visible(false);
        flush(root);
        require(GetCapture() != column_peer && !columns->divider_dragging(), "Hiding a pane cancels capture");
        require(GetForegroundWindow() == foreground, "Fixture keeps the original foreground window");
        window->close();
        require(application.run() == 0, "Close the owned fixture");
        std::cout << "Background split pointer, nested axes, keyboard, capture cancellation and callbacks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
