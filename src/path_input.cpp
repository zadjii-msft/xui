#include "xui/path_input.hpp"
#include <windows.h>

namespace xui {
PathInput expand_path_input(std::wstring_view input) {
    constexpr std::size_t maximum = 32767;
    const auto error = [](std::wstring message) { return PathInput{{}, std::move(message)}; };
    if (input.size() >= maximum || input.find(L'\0') != input.npos)
        return error(L"Folder path is too long or invalid.");
    if (input.size() >= 2 && input.front() == L'"' && input.back() == L'"')
        input = input.substr(1, input.size() - 2);
    std::wstring result;
    for (std::size_t i = 0; i < input.size();) {
        if (input[i] != L'%') {
            result += input[i++];
        } else {
            const auto end = input.find(L'%', i + 1);
            if (end == input.npos) {
                // A percent inside a filename is literal. A new component can start a reference.
                const bool component_start = !i || input[i - 1] == L'\\' || input[i - 1] == L'/';
                if (component_start)
                    return error(L"Complete the environment reference with a closing %.");
                result.append(input.substr(i));
                break;
            }
            if (end == i + 1) {
                result += L"%%";
            } else {
                const std::wstring name(input.substr(i + 1, end - i - 1));
                SetLastError(ERROR_SUCCESS);
                const DWORD required = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
                if (!required) {
                    const auto code = GetLastError();
                    if (code == ERROR_ENVVAR_NOT_FOUND)
                        return error(L"Unknown environment variable: %" + name + L"%.");
                    if (code != ERROR_SUCCESS)
                        return error(L"Cannot read the environment (Windows error " + std::to_wstring(code) + L").");
                } else {
                    if (required > maximum || result.size() + required > maximum)
                        return error(L"Expanded folder path is too long.");
                    std::wstring value(required, L'\0');
                    SetLastError(ERROR_SUCCESS);
                    const DWORD written = GetEnvironmentVariableW(name.c_str(), value.data(), required);
                    const DWORD read_error = GetLastError();
                    if (written >= required)
                        return error(L"The environment variable changed. Enter the path again.");
                    if (!written && read_error != ERROR_SUCCESS)
                        return error(L"Cannot read the environment (Windows error " + std::to_wstring(read_error) + L").");
                    value.resize(written);
                    result += value;
                }
            }
            i = end + 1;
        }
        if (result.size() >= maximum) return error(L"Expanded folder path is too long.");
    }
    if (result.size() >= maximum) return error(L"Expanded folder path is too long.");
    return {std::move(result), {}};
}
}
