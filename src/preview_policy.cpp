#include "preview_policy.hpp"
#include <urlmon.h>
#include <shlwapi.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <new>

namespace xui::preview {
using Microsoft::WRL::ComPtr;
namespace {
[[noreturn]] void restricted(DWORD error = ERROR_ACCESS_DENIED) {
    throw Failure{PreviewReason::restricted, PreviewPhase::policy, HRESULT_FROM_WIN32(error)};
}
class ReadStream final : public IStream {
    LONG refs_{1};
    Handle file_;
public:
    explicit ReadStream(HANDLE file) : file_(file) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ISequentialStream || iid == IID_IStream) *out = static_cast<IStream*>(this);
        if (!*out) return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Read(void* data, ULONG count, ULONG* read) override {
        DWORD actual{};
        if (read) *read = 0;
        if (!data && count) return STG_E_INVALIDPOINTER;
        if (!ReadFile(file_.value, data, count, &actual, nullptr)) return HRESULT_FROM_WIN32(GetLastError());
        if (read) *read = actual;
        return actual == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* result) override {
        if (origin > STREAM_SEEK_END) return STG_E_INVALIDFUNCTION;
        LARGE_INTEGER at{};
        if (!SetFilePointerEx(file_.value, move, &at, origin)) return HRESULT_FROM_WIN32(GetLastError());
        if (result) result->QuadPart = at.QuadPart;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream* target, ULARGE_INTEGER count, ULARGE_INTEGER* read, ULARGE_INTEGER* written) override {
        if (!target) return STG_E_INVALIDPOINTER;
        if (read) read->QuadPart = 0;
        if (written) written->QuadPart = 0;
        std::array<BYTE, 16384> buffer{};
        while (count.QuadPart) {
            ULONG actual{}, sent{};
            auto hr = Read(buffer.data(), static_cast<ULONG>(std::min<ULONGLONG>(count.QuadPart, buffer.size())), &actual);
            if (FAILED(hr)) return hr;
            if (read) read->QuadPart += actual;
            const auto write = target->Write(buffer.data(), actual, &sent);
            if (written) written->QuadPart += sent;
            if (FAILED(write)) return write;
            if (sent != actual) return STG_E_MEDIUMFULL;
            count.QuadPart -= actual;
            if (hr == S_FALSE) return S_FALSE;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
        if (!stat) return STG_E_INVALIDPOINTER;
        *stat = {};
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file_.value, &size)) return HRESULT_FROM_WIN32(GetLastError());
        stat->type = STGTY_STREAM; stat->cbSize.QuadPart = size.QuadPart; stat->grfMode = STGM_READ;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }
};
}
bool local_path_syntax(std::wstring_view path) {
    const bool syntax = path.size() >= 3 && path.size() < max_path && path[1] == L':' && path[2] == L'\\' &&
        ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
        path.find_first_of(L"\r\n/") == path.npos && path.find(L'\0') == path.npos &&
        path.find(L':', 2) == path.npos;
    if (!syntax) return false;
    for (std::size_t at = 3; at < path.size();) {
        const auto end = path.find(L'\\', at);
        const auto part = path.substr(at, end == path.npos ? end : end - at);
        if (part.empty() || part == L"." || part == L"..") return false;
        if (end == path.npos) break;
        at = end + 1;
    }
    return path.back() != L'\\';
}
bool allowed_origin(std::string_view zone) {
    if (zone.empty()) return false;
    if (zone.starts_with("\xef\xbb\xbf")) zone.remove_prefix(3);
    bool section{}, found{};
    while (!zone.empty()) {
        const auto end = zone.find('\n');
        auto line = zone.substr(0, end);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.find('\0') != line.npos) return false;
        auto trimmed = line;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) trimmed.remove_prefix(1);
        if (trimmed == "[ZoneTransfer]") section = true;
        else if (trimmed.starts_with("[")) section = false;
        else if (trimmed.size() >= 6 && _strnicmp(trimmed.data(), "ZoneId", 6) == 0) {
            if (!section || found || line != "ZoneId=0") return false;
            found = true;
        }
        if (end == zone.npos) break;
        zone.remove_prefix(end + 1);
    }
    return found;
}
EligibleFile open_eligible(const std::wstring& path) {
    if (!local_path_syntax(path)) restricted(ERROR_BAD_PATHNAME);
    const auto root = path.substr(0, 3);
    if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) restricted();
    DWORD flags{};
    if (!GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, &flags, nullptr, 0) ||
        !(flags & FILE_NAMED_STREAMS)) restricted();
    EligibleFile result;
    for (std::size_t end = 3; end < path.size(); ++end) {
        if (path[end] != L'\\') continue;
        const auto parent = path.substr(0, end);
        const DWORD attributes = GetFileAttributesW(parent.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) restricted();
        Handle held(CreateFileW(parent.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION pinned{};
        if (!held || !GetFileInformationByHandle(held.value, &pinned) ||
            (pinned.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) restricted();
        result.ancestors.push_back(std::move(held));
    }
    const auto attributes = GetFileAttributesW(path.c_str());
    constexpr DWORD denied = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE |
        FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS;
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & denied)) restricted();
    if (path.size() >= 4 && _wcsicmp(path.c_str() + path.size() - 4, L".lnk") == 0) restricted();
    for (const auto extension : {L".url", L".website", L".search-ms", L".library-ms", L".exe", L".dll", L".msi"})
        if (path.size() >= wcslen(extension) && _wcsicmp(path.c_str() + path.size() - wcslen(extension), extension) == 0) restricted();
    result.file.reset(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!result.file) restricted(GetLastError());
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(result.file.value, &info)) restricted(GetLastError());
    if (info.dwFileAttributes & denied) restricted();
    if (((std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow) > max_file_bytes)
        throw Failure{PreviewReason::resource_limit, PreviewPhase::policy, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)};
    std::array<wchar_t, max_path> final{};
    const auto length = GetFinalPathNameByHandleW(result.file.value, final.data(), static_cast<DWORD>(final.size()), FILE_NAME_NORMALIZED);
    if (!length || length >= final.size() || std::wstring_view(final.data(), length).substr(0, 4) != L"\\\\?\\") restricted();
    result.path.assign(final.data() + 4, length - 4);
    if (!local_path_syntax(result.path) || _wcsicmp(result.path.c_str(), path.c_str()) != 0) restricted();

    ComPtr<IInternetSecurityManager> security;
    require(CoInternetCreateSecurityManager(nullptr, &security, 0), PreviewReason::restricted, PreviewPhase::policy);
    DWORD zone{};
    require(security->MapUrlToZone(result.path.c_str(), &zone, MUTZ_ISFILE | MUTZ_DONT_USE_CACHE),
        PreviewReason::restricted, PreviewPhase::policy);
    if (zone != URLZONE_LOCAL_MACHINE) restricted();
    result.origin.reset(CreateFileW((result.path + L":Zone.Identifier").c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!result.origin) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) restricted(GetLastError());
    } else {
        std::array<char, 4097> bytes{};
        DWORD count{};
        if (!ReadFile(result.origin.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)) restricted(GetLastError());
        if (count == bytes.size() || !allowed_origin(std::string_view(bytes.data(), count))) restricted();
    }
    return result;
}
ComPtr<IStream> readonly_stream(HANDLE file) {
    HANDLE duplicate{};
    if (!DuplicateHandle(GetCurrentProcess(), file, GetCurrentProcess(), &duplicate, GENERIC_READ, FALSE, 0))
        throw Failure{PreviewReason::initialization_failed, PreviewPhase::initialize, HRESULT_FROM_WIN32(GetLastError())};
    ComPtr<IStream> stream;
    auto* value = new (std::nothrow) ReadStream(duplicate);
    if (!value) { CloseHandle(duplicate); throw std::bad_alloc(); }
    stream.Attach(value);
    return stream;
}
}
