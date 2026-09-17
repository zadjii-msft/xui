#include "../src/native_file_dialog.hpp"
#include "file_dialog_test_probe.hpp"
#include <commctrl.h>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace xui;
using file_dialog_tests::ModalProbe;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    bool rejected{};
    try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Expected native file dialog error");
}
struct Owner {
    HWND window{};
    Owner() {
        window = CreateWindowW(L"STATIC", L"Owned native file dialog tests", WS_OVERLAPPEDWINDOW,
            0, 0, 500, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(window != nullptr, "Create owner");
        ShowWindow(window, SW_SHOWNOACTIVATE);
    }
    ~Owner() { DestroyWindow(window); }
};
}
int main() {
    try {
        require(SUCCEEDED(OleInitialize(nullptr)), "Initialize STA");
        struct Com { ~Com() { OleUninitialize(); } } com;
        Owner owner;
        FileDialogOptions options{L"Owned cancellation", {{L"All files", L"*.*"}}};
        wchar_t temporary[32768]{};
        require(GetTempPathW(32768, temporary) != 0, "Read temporary directory");
        options.initial_directory = temporary;
        std::cout << "Native user cancellation\n" << std::flush;
        {
            NativeFileDialog dialog(false, options);
            ModalProbe probe(owner.window, [&](HWND hwnd) {
                PostMessageW(hwnd, WM_COMMAND, IDCANCEL, 0);
            });
            require(!dialog.show(owner.window), "User cancellation has no selected path");
            probe.check();
        }
        {
            std::cout << "Native programmatic cancellation\n" << std::flush;
            NativeFileDialog dialog(true, options);
            ModalProbe probe(owner.window, [&](HWND) { dialog.cancel(); });
            require(!dialog.show(owner.window), "Programmatic cancellation has no selected path");
            probe.check();
        }
        {
            std::cout << "Native rejected owners\n" << std::flush;
            NativeFileDialog dialog(false, options);
            rejects([&] { dialog.show(nullptr); });
            EnableWindow(owner.window, FALSE);
            rejects([&] { dialog.show(owner.window); });
            EnableWindow(owner.window, TRUE);
            ShowWindow(owner.window, SW_HIDE);
            rejects([&] { dialog.show(owner.window); });
            ShowWindow(owner.window, SW_SHOWNOACTIVATE);
            bool rejected{};
            std::thread worker([&] {
                try { dialog.show(owner.window); } catch (const std::logic_error&) { rejected = true; }
            });
            worker.join(); require(rejected, "Wrong-thread dialog rejected");
        }
        wchar_t executable[32768]{};
        require(GetModuleFileNameW(nullptr, executable, 32768) != 0, "Read fixture path");
        const std::filesystem::path path(executable);
        options = {L"Owned open selection", {{L"Executables", L"*.exe"}}, L"exe",
            path.filename().wstring(), path.parent_path().wstring()};
        {
            std::cout << "Native existing open selection\n" << std::flush;
            NativeFileDialog dialog(false, options);
            ModalProbe probe(owner.window, [](HWND hwnd) { PostMessageW(hwnd, WM_COMMAND, IDOK, 0); });
            require(dialog.show(owner.window) == path.wstring(), "Open returns exact existing filesystem path");
            probe.check();
        }
        options = {L"Owned save selection", {{L"Components", L"*.xui"}}, L"xui",
            L"xui-dialog-no-write-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()),
            temporary};
        const auto destination = std::filesystem::path(temporary) / (options.suggested_name + L".xui");
        require(!std::filesystem::exists(destination), "Save destination starts absent");
        {
            std::cout << "Native no-write save selection\n" << std::flush;
            NativeFileDialog dialog(true, options);
            ModalProbe probe(owner.window, [](HWND hwnd) { PostMessageW(hwnd, WM_COMMAND, IDOK, 0); });
            require(dialog.show(owner.window) == destination.wstring(), "Save returns path with requested default extension");
            probe.check();
        }
        require(!std::filesystem::exists(destination), "Save dialog does not create selected file");
        options.initial_directory = destination.wstring();
        rejects([&] { NativeFileDialog invalid(false, options); });
        std::cout << "Real owned Open/Save, cancellation, errors, affinity and no-write selection passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
