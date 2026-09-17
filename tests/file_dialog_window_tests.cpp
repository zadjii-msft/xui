#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "file_dialog_test_probe.hpp"
#include <filesystem>
#include <iostream>
#include <thread>
using namespace xui;
using file_dialog_tests::ModalProbe;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    bool rejected{};
    try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Expected window dialog rejection");
}
FileDialogOptions options() {
    return {L"XUI owned dialog", {{L"Components", L"*.xui"}, {L"All files", L"*.*"}}, L"xui", L"Component.xui",
        std::filesystem::temp_directory_path().wstring()};
}
HWND owned(const Window& window) {
    auto hwnd = FindWindowW(L"Xui.Window.1", window.title().c_str());
    DWORD process{}; GetWindowThreadProcessId(hwnd, &process);
    require(hwnd && process == GetCurrentProcessId(), "Exact XUI owner exists in this process");
    return hwnd;
}
void operations() {
    Window window({L"XUI file dialog operations"});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto editor = std::make_shared<MultilineText>();
    auto host = std::make_shared<ContentHost>();
    editor->set_text(L"source"); root->add(editor); root->add(host); window.set_content(root);
    rejects([&] { window.show_open_file_dialog(options()); });
    bool wrong_thread{};
    std::thread worker([&] {
        try { window.show_open_file_dialog(options()); } catch (const std::logic_error&) { wrong_thread = true; }
    });
    worker.join(); require(wrong_thread, "Window checks creating thread");
    bool ran{};
    window.post([&] {
        const auto hwnd = owned(window);
        require(window.focus(*editor), "Focus native source before dialog");
        const auto focus = GetFocus();
        const auto before = editor->text();
        {
            ModalProbe probe(hwnd, [&](HWND dialog) {
                rejects([&] { window.show_save_file_dialog(options()); });
                rejects([&] { window.replace_content(*host, std::make_shared<Stack>(Axis::vertical)); });
                PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
            });
            require(!window.show_open_file_dialog(options()), "Owned cancellation is empty optional");
            probe.check();
        }
        require(GetFocus() == focus && editor->text() == before, "Cancellation restores native focus without text changes");
        auto invalid = options(); invalid.filters[0].pattern = L"not-a-pattern";
        rejects([&] { window.show_open_file_dialog(invalid); });
        EnableWindow(hwnd, FALSE); rejects([&] { window.show_open_file_dialog(options()); }); EnableWindow(hwnd, TRUE);
        ShowWindow(hwnd, SW_HIDE); rejects([&] { window.show_open_file_dialog(options()); }); ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SendMessageW(focus, WM_IME_STARTCOMPOSITION, 0, 0);
        rejects([&] { window.show_open_file_dialog(options()); });
        SendMessageW(focus, WM_IME_ENDCOMPOSITION, 0, 0);
        ran = true;
        window.close();
    });
    require(Application::run(window) == 0 && ran, "Owned window operation fixture completed");
    rejects([&] { window.show_open_file_dialog(options()); });
}
void lifetime(bool delete_owner) {
    auto window = std::make_unique<Window>(WindowOptions{delete_owner ? L"XUI file dialog deletion" : L"XUI file dialog closure"});
    window->set_content(std::make_shared<Stack>(Axis::vertical));
    bool ran{};
    window->post([&] {
        const auto hwnd = owned(*window);
        ModalProbe probe(hwnd, [&](HWND) {
            std::cout << (delete_owner ? "Deleting modal owner\n" : "Closing modal owner\n") << std::flush;
            if (delete_owner) window.reset();
            else window->close();
            require(IsWindow(hwnd), "XUI defers owner destruction until native modal loop returns");
        });
        require(!window->show_save_file_dialog(options()), "Owner closure cancels native dialog");
        std::cout << "Owner dialog returned\n" << std::flush;
        probe.check(false);
        ran = true;
    });
    const auto result = Application::run(*window);
    if (result != 0 && window) std::wcerr << window->error() << L'\n';
    require(result == 0 && ran, delete_owner ? "XUI modal deletion completed" : "XUI modal closure completed");
    require(delete_owner == !window, "Only requested owner was deleted");
}
}
int main() {
    try {
        operations();
        lifetime(false);
        lifetime(true);
        std::cout << "XUI-owned file dialog focus, modal rejection, close and deletion passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
