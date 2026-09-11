#include "browser.hpp"
#include <windows.h>
#include <shellapi.h>
#include <filesystem>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count{};
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) {
        MessageBoxW(nullptr, L"Cannot read command-line arguments.", L"XUI", MB_ICONERROR);
        return 1;
    }
    xui::BrowserOptions options;
    try {
        options.folder = count > 1 ? std::filesystem::path(args[1]) : std::filesystem::current_path();
        options.folder = std::filesystem::absolute(options.folder).lexically_normal();
    } catch (const std::filesystem::filesystem_error&) {
        LocalFree(args);
        MessageBoxW(nullptr, L"Cannot resolve the folder path.", L"XUI", MB_ICONERROR);
        return 1;
    }
    LocalFree(args);
    return xui::run_file_browser(options);
}
