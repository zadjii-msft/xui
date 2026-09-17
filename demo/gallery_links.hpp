#pragma once
#include <windows.h>
#include <shellapi.h>
#include "gallery_urls.hpp"

namespace gallery {
inline std::wstring open_url(const std::wstring& url) {
    SHELLEXECUTEINFOW action{sizeof(action)};
    action.fMask = SEE_MASK_FLAG_NO_UI;
    action.lpVerb = L"open";
    action.lpFile = url.c_str();
    action.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&action)) {
        return L"Documentation did not open (Windows error " +
            std::to_wstring(GetLastError()) + L"). Use Copy documentation link.";
    }
    return L"Documentation opened in the default browser.";
}
inline std::wstring open_documentation(std::wstring_view path) {
    return open_url(documentation_url(path));
}
}
