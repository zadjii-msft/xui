#include "shell_dispatch.hpp"
#include <windows.h>
#include <shellapi.h>

namespace xui::explorer {
std::wstring launch_associated_file(const std::wstring& path) {
    SHELLEXECUTEINFOW action{sizeof(action)};
    action.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    action.lpVerb = L"open";
    action.lpFile = path.c_str();
    action.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&action)) return {};
    const auto error = GetLastError();
    wchar_t* text{};
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::wstring message = text ? text : L"Windows error " + std::to_wstring(error);
    if (text) LocalFree(text);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) message.pop_back();
    return L"Cannot open " + path + L": " + message;
}
}
