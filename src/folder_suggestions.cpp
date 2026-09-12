#include "xui/suggestions.hpp"
#include "folder_suggestions.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>

namespace xui {
namespace {
class FindFolderCursor final : public detail::FolderCursor {
public:
    FindFolderCursor(HANDLE handle, const WIN32_FIND_DATAW& data) : handle_(handle), data_(data) {}
    ~FindFolderCursor() override { FindClose(handle_); }
    bool next(detail::FolderEntry& entry, DWORD& error) override {
        if (!first_ && !FindNextFileW(handle_, &data_)) { error = GetLastError(); return false; }
        first_ = false;
        entry = {data_.cFileName, (data_.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0};
        return true;
    }
private:
    HANDLE handle_;
    WIN32_FIND_DATAW data_;
    bool first_{true};
};
class FolderSuggestions final : public SuggestionSource {
public:
    explicit FolderSuggestions(detail::OpenFolder open) : open_(std::move(open)) {}
    SuggestionResult suggest(const SuggestionRequest& request,
        const std::function<bool()>& cancelled) override {
        SuggestionResult result;
        if (cancelled()) return result;
        auto text = request.text;
        if (text.size() > SuggestionRequest::maximum_text || text.find(L'\0') != text.npos) {
            result.status = L"Folder path is too long or invalid.";
            return result;
        }
        if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"')
            text = text.substr(1, text.size() - 2);
        std::replace(text.begin(), text.end(), L'/', L'\\');
        if (text.empty() && !request.explicit_request) return result;
        if (text.find_first_of(L"*?\"<>|") != text.npos && !text.starts_with(L"\\\\?\\")) {
            result.status = L"Enter a folder path without wildcards.";
            return result;
        }
        const auto invalid_start = text.starts_with(L"\\\\?\\") ? 4u : 0u;
        if (text.find_first_of(L"*?\"<>|", invalid_start) != text.npos) {
            result.status = L"Enter a folder path without wildcards.";
            return result;
        }
        if (text.empty() && request.context.empty()) {
            const DWORD drives = GetLogicalDrives();
            for (unsigned i = 0; i < 26; ++i)
                if (drives & (1u << i)) result.items.push_back(std::wstring(1, wchar_t(L'A' + i)) + L":\\");
            result.status = result.items.empty() ? L"No drives available." : L"";
            return result;
        }
        std::filesystem::path typed(text), base(request.context);
        if (typed.has_root_name() && !typed.has_root_directory()) {
            // A drive designator is useful before the user types its slash.
            if (text.size() == 2 && text[1] == L':') {
                const auto letter = text[0] >= L'a' && text[0] <= L'z' ? text[0] - (L'a' - L'A') : text[0];
                if (letter >= L'A' && letter <= L'Z' && (GetLogicalDrives() & (1u << (letter - L'A'))))
                    result.items.push_back(text + L"\\");
                else result.status = L"No matching drive.";
            } else result.status = L"Use an absolute drive path.";
            return result;
        }
        if (!typed.is_absolute() && !base.is_absolute()) {
            result.status = L"An absolute base folder is required.";
            return result;
        }
        auto path = typed.is_absolute() ? typed : base / typed;
        if (text == L"." || text == L".." || text.ends_with(L"\\.") || text.ends_with(L"\\.."))
            path /= L"";
        const auto prefix = path.filename().wstring();
        auto parent = path.parent_path().lexically_normal();
        if (parent.empty()) parent = path.root_path();
        // A server name is not a directory. Share discovery is deliberately excluded.
        const auto parent_text = parent.wstring();
        if (parent_text.starts_with(L"\\\\") && !parent_text.starts_with(L"\\\\?\\") &&
            parent.relative_path().empty()) {
            result.status = L"Enter a network share, such as \\\\server\\share\\.";
            return result;
        }
        if (parent_text.size() + prefix.size() + 3 > SuggestionRequest::maximum_text) {
            result.status = L"Folder path is too long.";
            return result;
        }
        const auto started = std::chrono::steady_clock::now();
        const auto pattern = (parent / (prefix + L"*")).wstring();
        DWORD error{};
        auto cursor = open_(pattern, error);
        if (!cursor) {
            result.status = error == ERROR_FILE_NOT_FOUND || error == ERROR_NO_MORE_FILES ?
                L"No matching folders." : L"Cannot read this folder (Windows error " + std::to_wstring(error) + L").";
            return result;
        }
        std::size_t scanned{};
        for (;;) {
            if (cancelled()) return {};
            detail::FolderEntry entry;
            if (!cursor->next(entry, error)) {
                if (error != ERROR_NO_MORE_FILES)
                    result.status = L"Cannot finish reading this folder (Windows error " + std::to_wstring(error) + L").";
                break;
            }
            // Windows wildcard matching has DOS aliases: check the literal prefix too.
            if (entry.directory && entry.name != L"." && entry.name != L".." &&
                entry.name.size() >= prefix.size() &&
                (prefix.empty() || CompareStringOrdinal(entry.name.data(), static_cast<int>(prefix.size()),
                    prefix.c_str(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL)) {
                auto item = (parent / entry.name).lexically_normal().wstring();
                if (item.size() < SuggestionRequest::maximum_text) result.items.push_back(std::move(item));
            }
            if (result.items.size() == SuggestionRequest::maximum_results || ++scanned >= 4096 ||
                std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(100)) {
                result.status = L"More folders may match. Type more to narrow the list.";
                break;
            }
        }
        std::sort(result.items.begin(), result.items.end(), [](const auto& a, const auto& b) {
            return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });
        if (result.items.empty() && result.status.empty()) result.status = L"No matching folders.";
        return result;
    }
private:
    detail::OpenFolder open_;
};
}
std::shared_ptr<SuggestionSource> detail::folder_suggestions_with(OpenFolder open) {
    return std::make_shared<FolderSuggestions>(std::move(open));
}
std::shared_ptr<SuggestionSource> folder_suggestions() {
    return detail::folder_suggestions_with([](const std::wstring& pattern, DWORD& error) -> std::unique_ptr<detail::FolderCursor> {
        WIN32_FIND_DATAW data{};
        HANDLE handle = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
        if (handle == INVALID_HANDLE_VALUE) { error = GetLastError(); return {}; }
        try { return std::make_unique<FindFolderCursor>(handle, data); }
        catch (...) { FindClose(handle); throw; }
    });
}
}
