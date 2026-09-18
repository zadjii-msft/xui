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
    options.application_icon = true;
    try {
        for (int i = 1; i < count; ++i) {
            const std::wstring_view argument(args[i]);
            if (argument == L"--style=winui") options.visual_style = xui::VisualStyle::winui;
            else if (argument == L"--style=classic") options.visual_style = xui::VisualStyle::classic;
            else if (options.folder.empty() && !argument.starts_with(L"--")) options.folder = args[i];
            else {
                LocalFree(args);
                MessageBoxW(nullptr, L"Use xui_demo.exe [folder] [--style=classic|--style=winui].", L"XUI", MB_ICONERROR);
                return 1;
            }
        }
        if (options.folder.empty()) options.folder = std::filesystem::current_path();
        options.folder = std::filesystem::absolute(options.folder).lexically_normal();
    } catch (const std::filesystem::filesystem_error&) {
        LocalFree(args);
        MessageBoxW(nullptr, L"Cannot resolve the folder path.", L"XUI", MB_ICONERROR);
        return 1;
    }
    LocalFree(args);
    return xui::run_file_browser(options);
}
