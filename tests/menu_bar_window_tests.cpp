#include <windows.h>
#include <ole2.h>
#include "xui/application.hpp"
#include "xui/menu_bar.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace xui;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
CommandRecord group(CommandId id, std::wstring label, CommandId parent = 0, bool enabled = true) {
    CommandRecord record{id, parent, std::move(label)};
    record.kind = CommandKind::submenu;
    record.enabled = enabled;
    return record;
}
HWND child(HWND window, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result{}; } search{name};
    EnumChildWindows(window, [](HWND hwnd, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        wchar_t name[256]{};
        GetWindowTextW(hwnd, name, 256);
        if (std::wstring_view(name) == search.name && IsWindowVisible(hwnd)) { search.result = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Owned heading native peer exists");
    return search.result;
}
void flush(HWND window) { SendMessageW(window, WM_APP + 12, 0, 0); }
void key(HWND target, WPARAM value) { SendMessageW(target, WM_KEYDOWN, value, 0); }

void run_case(VisualStyle style) {
    WindowOptions options{L"XUI menu bar native contracts", {680, 480}, ThemeMode::light};
    options.visual_style = style;
    options.show_activated = false;
    Window window(options);
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto before = std::make_shared<Button>(L"Before menu");
    auto bar = std::make_shared<MenuBar>(L"Main menu");
    auto after = std::make_shared<TextInput>(L"After menu");
    int actions{};
    auto snapshot = std::make_shared<CommandSet>(std::vector<CommandRecord>{
        group(1, L"&File"), {10, 1, L"Open", [&] { require(!bar->expanded(), "Dismiss before command action"); ++actions; }},
        group(11, L"Recent", 1), {12, 11, L"Nested action", [&] { ++actions; }},
        group(2, L"&Disabled", 0, false), group(3, L"&Edit"), {30, 3, L"Copy", [&] { ++actions; }}});
    bar->set_commands(snapshot);
    root->add(before); root->add(bar); root->add(after);
    window.set_content(root);
    std::string error;
    std::jthread worker([&] {
        const auto ui = [&](std::function<void()> callback) {
            auto done = std::make_shared<std::promise<void>>();
            auto result = done->get_future();
            require(window.post([done, callback = std::move(callback)] {
                try { callback(); done->set_value(); } catch (...) { done->set_exception(std::current_exception()); }
            }), "Post menu bar native test");
            require(result.wait_for(std::chrono::seconds(15)) == std::future_status::ready, "Native test completed on UI thread");
            result.get();
        };
        try {
            HWND hwnd{}, file{}, edit{};
            ui([&] {
                hwnd = FindWindowW(L"Xui.Window.1", options.title.c_str());
                require(hwnd != nullptr, "Owned menu bar window exists");
                flush(hwnd);
                file = child(hwnd, L"File"); edit = child(hwnd, L"Edit");
                require(GetWindowLongPtrW(file, GWL_STYLE) & WS_TABSTOP, "First heading is a native tab stop");
                require(!(GetWindowLongPtrW(edit, GWL_STYLE) & WS_TABSTOP), "Other headings are not native tab stops");
                require(window.focus(*before), "Initial focus before menu");
                key(GetFocus(), VK_F10);
                require(GetFocus() == file && !bar->expanded(), "F10 enters the heading without opening");
                key(file, VK_RIGHT);
                require(GetFocus() == edit && bar->current() == 3, "Heading arrow skips disabled groups");
                key(edit, VK_LEFT);
                key(file, VK_DOWN);
                require(bar->expanded() == 1 && GetFocus() != file, "Down automatically opens and focuses the menu");
                key(GetFocus(), VK_DOWN);
                key(GetFocus(), VK_RIGHT);
                const auto nested = GetFocus();
                require(bar->expanded() == 1, "Nested submenu preserves top-level expansion");
                key(nested, VK_LEFT);
                require(GetFocus() != nested && bar->expanded() == 1, "Left returns from nested submenu");
                key(GetFocus(), VK_LEFT);
                require(bar->expanded() == 3, "Left at root switches the open heading");
                key(GetFocus(), VK_ESCAPE);
                require(!bar->expanded() && GetFocus() == edit, "Escape returns to the current heading");
                key(edit, VK_ESCAPE);
                require(before->focused(), "Second Escape returns to the pre-menu focus");
                require(window.focus(*before), "Restore ordinary focus");
                require(PostMessageW(GetFocus(), WM_SYSKEYDOWN, 'F', 1LL << 29), "Queue Alt mnemonic through native translation");
            });
            ui([&] {
                require(bar->expanded() == 1, "Alt mnemonic opens the corresponding heading");
                key(GetFocus(), VK_RETURN);
                require(actions == 1 && !bar->expanded() && GetFocus() == file, "Enter dismisses then invokes the shared action");
                key(file, VK_RETURN);
                require(bar->expanded() == 1, "Enter opens from the heading");
                SendMessageW(hwnd, WM_LBUTTONDOWN, 0, MAKELPARAM(650, 450));
                require(!bar->expanded() && GetFocus() == file, "Outside dismissal returns focus to the heading");
                key(file, VK_DOWN);
                bar->set_commands(std::make_shared<CommandSet>(std::vector{
                    group(1, L"&File", 0, false), group(3, L"&Edit")}));
                flush(hwnd);
                require(!bar->expanded() && bar->current() == 3 && !bar->heading(1)->enabled(), "Replacement disables and dismisses an open root");
                bar->set_commands(snapshot); flush(hwnd);
                require(window.focus(*bar->heading(1)), "Focus current retained heading");
                key(file, VK_DOWN);
                bar->set_enabled(false); flush(hwnd);
                require(!bar->expanded(), "Disabling the owner dismisses its menu");
                bar->set_enabled(true); flush(hwnd);
                require(window.focus(*bar->heading(1)), "Reenabled heading accepts focus");
                key(file, VK_DOWN);
                key(GetFocus(), VK_TAB);
                require(!bar->expanded() && after->focused(), "Tab exits the open menu through the one bar stop");
            });
        } catch (const std::exception& failure) { error = failure.what(); }
        window.post([&] { window.close(); });
    });
    const auto result = Application::run(window);
    worker.join();
    require(result == 0 && error.empty(), error.empty() ? "Menu bar native loop succeeds" : error.c_str());
}
}
int main() {
    try {
        run_case(VisualStyle::classic);
        run_case(VisualStyle::winui);
        std::cout << "Menu bar native contracts passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
