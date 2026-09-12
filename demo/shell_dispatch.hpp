#pragma once
#include "xui/core.hpp"
#include <functional>

namespace xui::explorer {
using ShellDispatcher = std::function<std::wstring(const std::wstring&)>;
inline std::wstring launch_file(const FileItem& item, const ShellDispatcher& dispatcher) {
    if (item.directory) return L"Navigate into a folder instead of launching it.";
    if (item.path.empty()) return L"The file path is empty.";
    return dispatcher(item.path);
}
std::wstring launch_associated_file(const std::wstring& path);
}
