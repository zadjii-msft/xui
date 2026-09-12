#pragma once
#include "xui/suggestions.hpp"
#include <windows.h>
#include <string_view>

namespace xui::detail {
struct FolderEntry {
    std::wstring_view name;
    bool directory{};
};
class FolderCursor {
public:
    virtual ~FolderCursor() = default;
    virtual bool next(FolderEntry& entry, DWORD& error) = 0;
};
using OpenFolder = std::function<std::unique_ptr<FolderCursor>(const std::wstring& pattern, DWORD& error)>;
std::shared_ptr<SuggestionSource> folder_suggestions_with(OpenFolder open);
}
