#include "directory.hpp"
#include <windows.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace xui {
namespace {
struct FindHandle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~FindHandle() { if (value != INVALID_HANDLE_VALUE) FindClose(value); }
};
struct Cancelled {};
std::wstring system_message(DWORD error) {
    wchar_t* text{};
    const auto size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::wstring result = size ? std::wstring(text, size) : L"Error " + std::to_wstring(error);
    if (text) LocalFree(text);
    return result;
}
}

SourceResult DirectorySource::scan(const CancelCheck& cancel) {
    SourceResult result;
    try {
        auto items = std::make_shared<std::vector<FileItem>>();
        std::unordered_map<std::wstring, ItemId> next_identities;
        WIN32_FIND_DATAW entry{};
        const auto pattern = folder_ / L"*";
        FindHandle find{FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &entry,
            FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH)};
        if (find.value == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND) result.error = L"Cannot read folder: " + system_message(error);
        } else {
            for (;;) {
                if (cancel()) return {};
                const std::wstring name(entry.cFileName);
                if (name != L"." && name != L"..") {
                    auto path = (folder_ / name).wstring();
                    const auto previous = identities_.find(path);
                    if (previous == identities_.end() && next_id_ == std::numeric_limits<ItemId>::max())
                        throw std::overflow_error("File item ID space exhausted");
                    const ItemId id = previous == identities_.end() ? next_id_++ : previous->second;
                    next_identities.emplace(path, id);
                    items->push_back({id, name, std::move(path),
                        (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
                }
                if (!FindNextFileW(find.value, &entry)) {
                    const DWORD error = GetLastError();
                    if (error != ERROR_NO_MORE_FILES)
                        result.error = L"Folder scan is incomplete: " + system_message(error);
                    break;
                }
            }
        }
        std::size_t comparisons{};
        std::sort(items->begin(), items->end(), [&](const FileItem& a, const FileItem& b) {
            if ((comparisons++ & 4095) == 0 && cancel()) throw Cancelled{};
            if (a.directory != b.directory) return a.directory > b.directory;
            const int order = CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE);
            return order == CSTR_LESS_THAN || (order == CSTR_EQUAL && a.name < b.name);
        });
        result.source = FileSnapshot::build(std::move(items), cancel);
        if (cancel() || !result.source) return {};
        identities_ = std::move(next_identities);
    } catch (const Cancelled&) { return {}; }
    return result;
}
}
