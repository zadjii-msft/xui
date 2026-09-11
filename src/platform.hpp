#pragma once

#include <windows.h>
#include <string>
#include <stdexcept>
#include <system_error>

namespace xui {

inline std::wstring system_message(DWORD error) {
    wchar_t* buffer{};
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = length ? std::wstring(buffer, length)
                                 : L"Windows error " + std::to_wstring(error);
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
        message.pop_back();
    return message;
}

inline void win32_require(bool success, const char* operation) {
    if (!success) {
        const DWORD error = GetLastError();
        if (error == ERROR_SUCCESS)
            throw std::runtime_error(std::string(operation) + ": Windows did not provide an error code");
        throw std::system_error(static_cast<int>(error), std::system_category(), operation);
    }
}

inline void hr_require(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::system_error(static_cast<int>(result),
                                               std::system_category(), operation);
}

inline std::wstring exception_message(const std::exception& error) {
    const std::string message = error.what();
    const int length = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
    if (!length) return L"A platform operation failed. Error text is unavailable.";
    std::wstring text(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, text.data(), length);
    text.pop_back();
    return text;
}

}
