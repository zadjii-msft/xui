#pragma once

#include "resource.h"
#include <windows.h>
#include <stdexcept>
#include <string>

namespace xui::demo {
inline std::wstring application_icon_source() {
    const auto module = GetModuleHandleW(nullptr);
    if (!FindResource(module, MAKEINTRESOURCE(IDI_ZOEY), RT_GROUP_ICON))
        throw std::runtime_error("The application icon resource is missing.");
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        throw std::runtime_error("Cannot read the application icon path.");
    path.resize(length);
    return path;
}

inline std::string utf8(const std::wstring& value) {
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("Cannot encode the application icon text.");
    std::string result(count, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count, nullptr, nullptr))
        throw std::runtime_error("Cannot encode the application icon text.");
    return result;
}

template<typename Window>
void set_application_icon(Window& window) {
    window.on_icon_error([](const std::wstring& error) {
        throw std::runtime_error(utf8(error));
    });
    window.set_icon_source(application_icon_source());
}
}
