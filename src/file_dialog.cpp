#include "xui/file_dialog.hpp"
#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace xui {
namespace {
void valid_text(std::wstring_view value, std::size_t maximum) {
    if (value.size() > maximum) throw std::invalid_argument("File dialog text exceeds its length limit");
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned>(value[i]);
        if (c < 32 || (c >= 0x7f && c <= 0x9f) || c > 0xffff)
            throw std::invalid_argument("File dialog text contains a control character or invalid UTF-16");
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == value.size() || value[i] < 0xdc00 || value[i] > 0xdfff)
                throw std::invalid_argument("File dialog text contains an unpaired surrogate");
        } else if (c >= 0xdc00 && c <= 0xdfff) throw std::invalid_argument("File dialog text contains an unpaired surrogate");
    }
}
bool extension(std::wstring_view value) {
    return !value.empty() && value.front() != L'.' && value.back() != L'.' &&
        value.find_first_of(L" <>:\"/\\|?*;") == std::wstring_view::npos;
}
void valid_pattern(std::wstring_view value) {
    if (value.empty()) throw std::invalid_argument("File dialog filter pattern is empty");
    while (true) {
        const auto separator = value.find(L';');
        const auto item = value.substr(0, separator);
        if (item != L"*" && item != L"*.*" && !(item.starts_with(L"*.") && extension(item.substr(2))))
            throw std::invalid_argument("File dialog filters require semicolon-separated *.extension patterns");
        if (separator == std::wstring_view::npos) break;
        value.remove_prefix(separator + 1);
    }
}
bool absolute_directory(std::wstring_view value) {
    const bool drive = value.size() >= 3 &&
        ((value[0] >= L'A' && value[0] <= L'Z') || (value[0] >= L'a' && value[0] <= L'z')) &&
        value[1] == L':' && value[2] == L'\\';
    if (drive) return value.find_first_of(L"<>\"|?*") == std::wstring_view::npos && value.find(L':', 2) == std::wstring_view::npos;
    if (!value.starts_with(L"\\\\") || value.size() < 5 || value[2] == L'.' || value[2] == L'?') return false;
    const auto separator = value.find(L'\\', 2);
    return separator != std::wstring_view::npos && separator > 2 && separator + 1 < value.size() &&
        value[separator + 1] != L'\\' && value.find_first_of(L"<>:\"|?*") == std::wstring_view::npos;
}
}
void FileDialogOptions::validate() const {
    valid_text(title, 256);
    valid_text(default_extension, 64);
    valid_text(suggested_name, 255);
    valid_text(initial_directory, 32767);
    if (!default_extension.empty() && !extension(default_extension))
        throw std::invalid_argument("File dialog default extension must omit the leading dot and wildcards");
    if (!suggested_name.empty() && (suggested_name.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos ||
        suggested_name == L"." || suggested_name == L".." || suggested_name.back() == L'.' || suggested_name.back() == L' '))
        throw std::invalid_argument("File dialog suggested name must be a filename, not a path");
    if (!initial_directory.empty() && !absolute_directory(initial_directory))
        throw std::invalid_argument("File dialog initial directory must be an absolute drive or UNC path");
    if (filters.size() > 32) throw std::invalid_argument("File dialogs support at most 32 filters");
    for (const auto& filter : filters) {
        valid_text(filter.name, 128);
        valid_text(filter.pattern, 512);
        if (filter.name.empty()) throw std::invalid_argument("File dialog filter name is empty");
        valid_pattern(filter.pattern);
    }
}
}
