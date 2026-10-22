#pragma once

#include <windows.h>
#include <stdexcept>
#include <string>

namespace xui::test {

inline DWORD gui_resource_count(DWORD kind, DWORD count, DWORD error) {
    if (!count && error != ERROR_SUCCESS)
        throw std::runtime_error("Read owned GUI resources: kind=" + std::to_string(kind) +
            " Win32 error=" + std::to_string(error));
    return count;
}

inline DWORD gui_resources(DWORD kind) {
    SetLastError(ERROR_SUCCESS);
    const auto count = GetGuiResources(GetCurrentProcess(), kind);
    const auto error = GetLastError();
    return gui_resource_count(kind, count, error);
}

}
