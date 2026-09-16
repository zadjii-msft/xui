#include "file_transfer.hpp"
#include "platform.hpp"
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <utility>

namespace xui::files {
using Microsoft::WRL::ComPtr;
namespace {
constexpr std::size_t maximum_bytes = 16 * 1024 * 1024;
struct Apartment {
    Apartment() { hr_require(OleInitialize(nullptr), "Initialize file transfer STA"); }
    ~Apartment() { OleUninitialize(); }
};
struct Medium {
    STGMEDIUM value{};
    ~Medium() { if (value.tymed) ReleaseStgMedium(&value); }
};
struct Lock {
    HGLOBAL handle;
    void* data;
    explicit Lock(HGLOBAL value) : handle(value), data(GlobalLock(value)) {
        win32_require(data != nullptr, "Lock file transfer data");
    }
    ~Lock() { GlobalUnlock(handle); }
};
FORMATETC format(CLIPFORMAT id) { return {id, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}; }
template<class F> HRESULT clipboard_call(F callback) {
    // Clipboard monitors can briefly hold the shared clipboard after a change.
    // Retry only contention, never a format, ownership, or transfer error.
    for (unsigned attempt = 0;; ++attempt) {
        const auto result = callback();
        if (result != CLIPBRD_E_CANT_OPEN || attempt == 100) return result;
        Sleep(20);
    }
}
CLIPFORMAT registered(const wchar_t* name) {
    const auto id = RegisterClipboardFormatW(name);
    win32_require(id != 0, "Register file transfer format");
    return static_cast<CLIPFORMAT>(id);
}
class FileData final : public IDataObject {
    struct Entry { CLIPFORMAT id; std::vector<std::byte> bytes; };
    std::atomic<ULONG> references{1};
    std::vector<Entry> entries;
    static HRESULT supported(const FORMATETC* f) {
        if (!f) return E_POINTER;
        if (f->ptd) return DV_E_DVTARGETDEVICE;
        if (f->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
        if (f->lindex != -1) return DV_E_LINDEX;
        return (f->tymed & TYMED_HGLOBAL) ? S_OK : DV_E_TYMED;
    }
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IDataObject) return E_NOINTERFACE;
        *out = static_cast<IDataObject*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --references; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f) override {
        const auto hr = supported(f);
        if (FAILED(hr)) return hr;
        return std::any_of(entries.begin(), entries.end(), [&](const auto& entry) { return entry.id == f->cfFormat; }) ?
            S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f, STGMEDIUM* output) override {
        if (!output) return E_POINTER;
        *output = {};
        const auto hr = QueryGetData(f);
        if (FAILED(hr)) return hr;
        try {
            const auto& bytes = std::find_if(entries.begin(), entries.end(),
                [&](const auto& entry) { return entry.id == f->cfFormat; })->bytes;
            Medium medium;
            medium.value.tymed = TYMED_HGLOBAL;
            medium.value.hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes.size());
            if (!medium.value.hGlobal) return E_OUTOFMEMORY;
            { Lock lock(medium.value.hGlobal); std::memcpy(lock.data, bytes.data(), bytes.size()); }
            *output = std::exchange(medium.value, {});
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* output) override {
        if (!output) return E_POINTER;
        output->ptd = nullptr; return DATA_S_SAMEFORMATETC;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC* f, STGMEDIUM* medium, BOOL release) override {
        const auto hr = supported(f);
        if (FAILED(hr)) return hr;
        if (!medium) return E_POINTER;
        if (medium->tymed != TYMED_HGLOBAL || !medium->hGlobal) return DV_E_TYMED;
        const auto size = GlobalSize(medium->hGlobal);
        if (!size || size > maximum_bytes) return STG_E_MEDIUMFULL;
        try {
            auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& value) { return value.id == f->cfFormat; });
            if (entry == entries.end() && entries.size() >= 64) return STG_E_MEDIUMFULL;
            std::size_t total = size;
            for (const auto& value : entries) if (value.id != f->cfFormat) total += value.bytes.size();
            if (total > 2 * maximum_bytes) return STG_E_MEDIUMFULL;
            std::vector<std::byte> bytes(size);
            { Lock lock(medium->hGlobal); std::memcpy(bytes.data(), lock.data, size); }
            if (entry == entries.end()) entries.push_back({f->cfFormat, std::move(bytes)});
            else entry->bytes = std::move(bytes);
            if (release) ReleaseStgMedium(medium);
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (direction != DATADIR_GET) return E_NOTIMPL;
        try {
            std::vector<FORMATETC> formats;
            for (const auto& entry : entries) formats.push_back(format(entry.id));
            return SHCreateStdEnumFmtEtc(static_cast<UINT>(formats.size()), formats.data(), output);
        } catch (...) { return E_OUTOFMEMORY; }
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD* connection) override {
        if (connection) *connection = 0;
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** output) override {
        if (output) *output = nullptr;
        return OLE_E_ADVISENOTSUPPORTED;
    }
};
void set_bytes(IDataObject* object, CLIPFORMAT id, const void* bytes, std::size_t length) {
    Medium medium;
    medium.value.tymed = TYMED_HGLOBAL;
    medium.value.hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, length);
    win32_require(medium.value.hGlobal != nullptr, "Allocate file transfer data");
    { Lock lock(medium.value.hGlobal); std::memcpy(lock.data, bytes, length); }
    auto f = format(id);
    hr_require(clipboard_call([&] { return object->SetData(&f, &medium.value, TRUE); }), "Set file transfer data");
    medium.value = {};
}
std::optional<DWORD> get_effect(IDataObject* object, const wchar_t* name) {
    auto f = format(registered(name));
    if (object->QueryGetData(&f) != S_OK) return {};
    Medium medium;
    hr_require(clipboard_call([&] { return object->GetData(&f, &medium.value); }), "Read file transfer effect");
    if (medium.value.tymed != TYMED_HGLOBAL || !medium.value.hGlobal || GlobalSize(medium.value.hGlobal) < sizeof(DWORD))
        throw std::invalid_argument("Malformed file transfer effect");
    Lock lock(medium.value.hGlobal);
    DWORD value; std::memcpy(&value, lock.data, sizeof(value));
    return value;
}
bool single_effect(FileTransferEffect effect) {
    return effect == FileTransferEffect::copy || effect == FileTransferEffect::move;
}
void validate_effect(FileTransferEffect effect) {
    if (!single_effect(effect)) throw std::invalid_argument("File transfer requires Copy or Move, not both");
}
void validate_path(const std::wstring& path) {
    const bool drive = path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') ||
        (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' && path[2] == L'\\';
    const bool unc = path.size() >= 5 && path[0] == L'\\' && path[1] == L'\\' &&
        path[2] != L'.' && path.find(L'\\', 2) != std::wstring::npos;
    if ((!drive && !unc) || path.size() > 32767 || path.find(L'\0') != std::wstring::npos)
        throw std::invalid_argument("File transfers require absolute filesystem paths of at most 32767 UTF-16 units");
}
// SetData is optional for CF_HDROP sources. Never request source-side deletion:
// IFileOperation has already performed the entire (optimized) move.
void notify_completed(IDataObject* object, FileTransferEffect effect, bool pasted) {
    const DWORD performed = effect == FileTransferEffect::move ? DROPEFFECT_NONE : DROPEFFECT_COPY;
    const DWORD logical = static_cast<DWORD>(effect);
    bool no_source_delete = effect != FileTransferEffect::move;
    try {
        set_bytes(object, registered(CFSTR_PERFORMEDDROPEFFECT), &performed, sizeof(performed));
        no_source_delete = true;
    } catch (...) {}
    try { set_bytes(object, registered(CFSTR_LOGICALPERFORMEDDROPEFFECT), &logical, sizeof(logical)); } catch (...) {}
    // A stale MOVE value in an uncooperative source must not trigger a second deletion.
    if (pasted && no_source_delete) {
        try { set_bytes(object, registered(CFSTR_PASTESUCCEEDED), &logical, sizeof(logical)); } catch (...) {}
    }
}
bool cancelled_result(HRESULT hr) {
    return hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || hr == COPYENGINE_E_USER_CANCELLED ||
        hr == COPYENGINE_E_CANCELLED || hr == E_ABORT;
}
class Progress final : public IFileOperationProgressSink {
    std::atomic<ULONG> references{1};
    std::function<bool()> cancelled;
    HRESULT before() noexcept {
        try { return cancelled && cancelled() ? E_ABORT : S_OK; } catch (...) { return E_ABORT; }
    }
    HRESULT after(HRESULT hr, IShellItem* item) noexcept {
        if (FAILED(hr)) {
            if (cancelled_result(hr)) incomplete = true;
            else if (SUCCEEDED(failure)) failure = hr;
        } else if (!item || hr == COPYENGINE_S_USER_IGNORED) incomplete = true;
        else ++completed;
        return S_OK;
    }
public:
    HRESULT failure{S_OK};
    bool incomplete{};
    std::size_t completed{};
    explicit Progress(std::function<bool()> stop) : cancelled(std::move(stop)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IFileOperationProgressSink) return E_NOINTERFACE;
        *out = static_cast<IFileOperationProgressSink*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --references; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE StartOperations() override { return before(); }
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT hr) override {
        if (FAILED(hr) && !cancelled_result(hr) && SUCCEEDED(failure)) failure = hr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return before(); }
    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT hr, IShellItem* item) override { return after(hr, item); }
    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return before(); }
    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT hr, IShellItem* item) override { return after(hr, item); }
    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return before(); }
    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT hr, IShellItem* item) override { return after(hr, item); }
    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD, IShellItem*) override { return before(); }
    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem*, HRESULT hr, IShellItem* item) override { return after(hr, item); }
    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return before(); }
    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT hr, IShellItem* item) override { return after(hr, item); }
    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override { return before(); }
    HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }
};
thread_local IDataObject* active_drag{};
thread_local bool transferring{};
struct TransferScope {
    TransferScope() {
        if (transferring) throw std::logic_error("A file transfer is already active on this UI thread");
        transferring = true;
    }
    ~TransferScope() { transferring = false; }
};
class Source final : public IDropSource {
    std::atomic<ULONG> references{1};
    HWND window;
    std::function<bool()> cancelled;
public:
    Source(HWND hwnd, std::function<bool()> stop) : window(hwnd), cancelled(std::move(stop)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IDropSource) return E_NOINTERFACE;
        *out = static_cast<IDropSource*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --references; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        try {
            if (escape || !IsWindow(window) || (cancelled && cancelled()) || (keys & MK_RBUTTON)) return DRAGDROP_S_CANCEL;
            return keys & MK_LBUTTON ? S_OK : DRAGDROP_S_DROP;
        } catch (...) { return DRAGDROP_S_CANCEL; }
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};
class Target final : public IDropTarget {
    std::atomic<ULONG> references{1};
    HWND window;
    std::shared_ptr<DataGrid> grid;
    std::function<bool()> available;
    std::function<void(std::function<void()>)> dispatch;
    ComPtr<IDataObject> object;
    std::optional<FileClipboardContent> content;
    DWORD allowed{};
    bool busy{};
    Point point(POINTL p) const {
        POINT local{p.x, p.y};
        if (!ScreenToClient(window, &local)) return {-1, -1};
        const auto dpi = GetDpiForWindow(window);
        return {local.x * 96.0f / (dpi ? dpi : 96), local.y * 96.0f / (dpi ? dpi : 96)};
    }
    FileTransferEffect query(DWORD keys, POINTL p) const {
        if (!content || !available() || !IsWindowVisible(window) || !IsWindowEnabled(window) ||
            ((keys & MK_CONTROL) && (keys & MK_SHIFT)) || (keys & MK_ALT)) return FileTransferEffect::none;
        auto wanted = content->effect;
        if (object.Get() == active_drag) wanted = FileTransferEffect::move;
        if (keys & MK_CONTROL) wanted = FileTransferEffect::copy;
        else if (keys & MK_SHIFT) wanted = FileTransferEffect::move;
        if (!(allowed & static_cast<DWORD>(wanted))) {
            if (keys & (MK_CONTROL | MK_SHIFT)) return FileTransferEffect::none;
            wanted = (allowed & DROPEFFECT_COPY) ? FileTransferEffect::copy : FileTransferEffect::move;
        }
        const auto result = grid->query_file_drop(point(p), wanted);
        return available() && single_effect(result) && (allowed & static_cast<DWORD>(result)) ? result : FileTransferEffect::none;
    }
    template<class F> HRESULT guarded(DWORD* effect, F action) noexcept {
        if (!effect) return E_POINTER;
        *effect = DROPEFFECT_NONE;
        if (busy) return S_OK;
        ComPtr<IDropTarget> keep(this);
        busy = true;
        bool completed{};
        try { dispatch([&] { action(); completed = true; }); }
        catch (...) {}
        busy = false;
        if (!completed) { *effect = DROPEFFECT_NONE; return E_FAIL; }
        return S_OK;
    }
public:
    Target(HWND hwnd, std::shared_ptr<DataGrid> value, std::function<bool()> ready,
        std::function<void(std::function<void()>)> invoke) :
        window(hwnd), grid(std::move(value)), available(std::move(ready)), dispatch(std::move(invoke)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IDropTarget) return E_NOINTERFACE;
        *out = static_cast<IDropTarget*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --references; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* value, DWORD keys, POINTL p, DWORD* effect) override {
        if (!effect) return E_POINTER;
        const DWORD mask = *effect & (DROPEFFECT_COPY | DROPEFFECT_MOVE);
        return guarded(effect, [&] {
            object = value; content.reset(); allowed = mask;
            if (value) content = read_object(value);
            *effect = static_cast<DWORD>(query(keys, p));
        });
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL p, DWORD* effect) override {
        return guarded(effect, [&] { *effect = static_cast<DWORD>(query(keys, p)); });
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        if (!busy) { content.reset(); object.Reset(); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* value, DWORD keys, POINTL p, DWORD* effect) override {
        if (!effect) return E_POINTER;
        allowed &= *effect & (DROPEFFECT_COPY | DROPEFFECT_MOVE);
        const auto result = guarded(effect, [&] {
            auto wanted = query(keys, p);
            if (!single_effect(wanted) || !value) return;
            const auto source = grid->source();
            std::optional<RowKey> key;
            if (!grid->file_drop_hit(point(p), key)) return;
            auto snapshot = read_object(value);
            std::optional<RowKey> current;
            if (!snapshot || !available() || grid->source() != source ||
                !grid->file_drop_hit(point(p), current) || key != current) return;
            const auto done = grid->drop_files(point(p), snapshot->paths, wanted);
            if (done != FileTransferEffect::none && done != wanted)
                throw std::invalid_argument("File drop completion must match the accepted effect");
            if (single_effect(done)) {
                notify_completed(value, done, false);
                // Optimized move: do not ask an external source to delete a second time.
                *effect = done == FileTransferEffect::move ? DROPEFFECT_NONE : DROPEFFECT_COPY;
            }
        });
        content.reset(); object.Reset();
        return result;
    }
};
}
void validate_paths(const std::vector<std::wstring>& paths) {
    if (paths.empty() || paths.size() > maximum_transfer_paths)
        throw std::invalid_argument("File transfers require 1 to 4096 paths");
    std::size_t bytes = sizeof(DROPFILES) + sizeof(wchar_t);
    for (const auto& path : paths) {
        validate_path(path);
        bytes += (path.size() + 1) * sizeof(wchar_t);
        if (bytes > maximum_bytes) throw std::invalid_argument("File transfer paths exceed 16 MiB");
    }
}
std::vector<std::byte> serialize_paths(const std::vector<std::wstring>& paths) {
    validate_paths(paths);
    std::size_t size = sizeof(DROPFILES) + sizeof(wchar_t);
    for (const auto& path : paths) size += (path.size() + 1) * sizeof(wchar_t);
    std::vector<std::byte> bytes(size);
    const DROPFILES header{sizeof(DROPFILES), {}, FALSE, TRUE};
    std::memcpy(bytes.data(), &header, sizeof(header));
    auto offset = sizeof(header);
    for (const auto& path : paths) {
        const auto length = (path.size() + 1) * sizeof(wchar_t);
        std::memcpy(bytes.data() + offset, path.c_str(), length); offset += length;
    }
    return bytes;
}
std::vector<std::wstring> parse_paths(std::span<const std::byte> bytes) {
    if (bytes.size() < sizeof(DROPFILES) || bytes.size() > maximum_bytes)
        throw std::invalid_argument("Invalid CF_HDROP size");
    DROPFILES header; std::memcpy(&header, bytes.data(), sizeof(header));
    const std::size_t unit = header.fWide ? sizeof(wchar_t) : 1;
    if (header.pFiles < sizeof(header) || header.pFiles > bytes.size() ||
        bytes.size() - header.pFiles < 2 * unit)
        throw std::invalid_argument("Invalid CF_HDROP offset");
    std::vector<std::wstring> paths;
    std::size_t offset = header.pFiles;
    auto read = [&](std::size_t index) {
        wchar_t value{};
        if (index + unit > bytes.size()) throw std::invalid_argument("Unterminated CF_HDROP paths");
        std::memcpy(&value, bytes.data() + index, unit);
        return value;
    };
    while (read(offset)) {
        const auto start = offset;
        while (read(offset)) {
            offset += unit;
            if ((offset - start) / unit > (header.fWide ? 32767u : 65534u))
                throw std::invalid_argument("CF_HDROP path is too long");
        }
        std::wstring path;
        if (header.fWide) {
            path.resize((offset - start) / unit); std::memcpy(path.data(), bytes.data() + start, offset - start);
        } else {
            const auto* text = reinterpret_cast<const char*>(bytes.data() + start);
            const int length = static_cast<int>(offset - start);
            const int count = MultiByteToWideChar(CP_ACP, 0, text, length, nullptr, 0);
            if (!count) throw std::invalid_argument("Invalid ANSI CF_HDROP path");
            path.resize(count); MultiByteToWideChar(CP_ACP, 0, text, length, path.data(), count);
        }
        paths.push_back(std::move(path));
        if (paths.size() > maximum_transfer_paths) throw std::invalid_argument("CF_HDROP exceeds 4096 paths");
        offset += unit;
    }
    validate_paths(paths);
    return paths;
}
ComPtr<IDataObject> data_object(const std::vector<std::wstring>& paths, FileTransferEffect preferred) {
    const auto bytes = serialize_paths(paths);
    ComPtr<IDataObject> object;
    object.Attach(new FileData());
    set_bytes(object.Get(), CF_HDROP, bytes.data(), bytes.size());
    if (preferred != FileTransferEffect::none) {
        validate_effect(preferred);
        const DWORD effect = static_cast<DWORD>(preferred);
        set_bytes(object.Get(), registered(CFSTR_PREFERREDDROPEFFECT), &effect, sizeof(effect));
    }
    return object;
}
std::optional<FileClipboardContent> read_object(IDataObject* object) {
    if (!object) return {};
    auto f = format(CF_HDROP);
    const auto query = object->QueryGetData(&f);
    if (query == DV_E_FORMATETC || query == DV_E_CLIPFORMAT || query == DV_E_TYMED || query == S_FALSE) return {};
    hr_require(query, "Query clipboard files");
    Medium medium;
    hr_require(clipboard_call([&] { return object->GetData(&f, &medium.value); }), "Read clipboard files");
    if (medium.value.tymed != TYMED_HGLOBAL || !medium.value.hGlobal) throw std::invalid_argument("Invalid CF_HDROP medium");
    Lock lock(medium.value.hGlobal);
    auto paths = parse_paths({static_cast<const std::byte*>(lock.data), GlobalSize(medium.value.hGlobal)});
    const auto preferred = get_effect(object, CFSTR_PREFERREDDROPEFFECT).value_or(DROPEFFECT_COPY);
    return FileClipboardContent{std::move(paths), preferred == DROPEFFECT_MOVE ? FileTransferEffect::move : FileTransferEffect::copy};
}
void set_clipboard(const std::vector<std::wstring>& paths, FileTransferEffect effect) {
    Apartment apartment;
    validate_effect(effect);
    auto object = data_object(paths, effect);
    hr_require(clipboard_call([&] { return OleSetClipboard(object.Get()); }), "Set file clipboard");
    hr_require(clipboard_call([] { return OleFlushClipboard(); }), "Persist file clipboard");
}
void set_text(const std::wstring& text) {
    Apartment apartment;
    if (text.find(L'\0') != std::wstring::npos || text.size() > 1048576)
        throw std::invalid_argument("Clipboard text contains NUL or exceeds 1 Mi UTF-16 units");
    ComPtr<IDataObject> object;
    object.Attach(new FileData());
    set_bytes(object.Get(), CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    hr_require(clipboard_call([&] { return OleSetClipboard(object.Get()); }), "Set clipboard text");
    hr_require(clipboard_call([] { return OleFlushClipboard(); }), "Persist clipboard text");
}
std::optional<FileClipboardContent> get_clipboard() {
    Apartment apartment;
    ComPtr<IDataObject> object;
    hr_require(clipboard_call([&] { return OleGetClipboard(object.ReleaseAndGetAddressOf()); }), "Open file clipboard");
    return read_object(object.Get());
}
bool transfer(HWND owner, const std::vector<std::wstring>& paths, const std::wstring& destination,
    FileTransferEffect effect, const std::function<bool()>& cancelled) {
    Apartment apartment;
    TransferScope scope;
    validate_paths(paths); validate_path(destination); validate_effect(effect);
    if (cancelled && cancelled()) return false;
    ComPtr<IFileOperation> operation;
    hr_require(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&operation)), "Create Shell file operation");
    hr_require(operation->SetOwnerWindow(owner), "Set file operation owner");
    hr_require(operation->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR | FOFX_ADDUNDORECORD |
        FOFX_SHOWELEVATIONPROMPT | FOFX_EARLYFAILURE), "Set file operation options");
    ComPtr<IShellItem> target;
    hr_require(SHCreateItemFromParsingName(destination.c_str(), nullptr, IID_PPV_ARGS(&target)), "Open destination folder");
    SFGAOF attributes{};
    hr_require(target->GetAttributes(SFGAO_FOLDER | SFGAO_FILESYSTEM, &attributes), "Inspect destination folder");
    if ((attributes & (SFGAO_FOLDER | SFGAO_FILESYSTEM)) != (SFGAO_FOLDER | SFGAO_FILESYSTEM))
        throw std::invalid_argument("File transfer destination is not a filesystem folder");
    // Resolve every source before queueing any destructive work.
    std::vector<ComPtr<IShellItem>> items(paths.size());
    for (std::size_t i = 0; i < paths.size(); ++i)
        hr_require(SHCreateItemFromParsingName(paths[i].c_str(), nullptr, IID_PPV_ARGS(&items[i])), "Open source file");
    ComPtr<Progress> progress;
    progress.Attach(new Progress(cancelled));
    DWORD cookie{};
    hr_require(operation->Advise(progress.Get(), &cookie), "Observe file operation completion");
    for (auto& item : items) {
        const auto hr = effect == FileTransferEffect::move ?
            operation->MoveItem(item.Get(), target.Get(), nullptr, nullptr) :
            operation->CopyItem(item.Get(), target.Get(), nullptr, nullptr);
        hr_require(hr, "Queue Shell file operation");
    }
    const auto hr = operation->PerformOperations();
    BOOL aborted{};
    const auto abort_hr = operation->GetAnyOperationsAborted(&aborted);
    operation->Unadvise(cookie);
    hr_require(progress->failure, "Shell file operation failed");
    if (cancelled_result(hr)) return false;
    hr_require(hr, "Perform Shell file operation");
    hr_require(abort_hr, "Read Shell cancellation status");
    return !aborted && !progress->incomplete && progress->completed >= paths.size();
}
std::optional<bool> paste(HWND owner, const std::wstring& destination, const std::function<bool()>& cancelled) {
    Apartment apartment;
    const auto sequence = GetClipboardSequenceNumber();
    ComPtr<IDataObject> object;
    hr_require(clipboard_call([&] { return OleGetClipboard(object.ReleaseAndGetAddressOf()); }), "Open file clipboard");
    auto content = read_object(object.Get());
    if (!content) return {};
    if (!transfer(owner, content->paths, destination, content->effect, cancelled)) return false;
    notify_completed(object.Get(), content->effect, true);
    if (content->effect == FileTransferEffect::move && sequence && GetClipboardSequenceNumber() == sequence) {
        // Compare while holding the clipboard, not before a later OleSetClipboard call.
        hr_require(clipboard_call([&] { return OpenClipboard(owner) ? S_OK : CLIPBRD_E_CANT_OPEN; }), "Open completed cut clipboard");
        const bool same = GetClipboardSequenceNumber() == sequence;
        const bool cleared = !same || EmptyClipboard() != 0;
        const bool closed = CloseClipboard() != 0;
        win32_require(cleared, "Clear completed cut clipboard");
        win32_require(closed, "Close completed cut clipboard");
    }
    return true;
}
FileTransferEffect drag(HWND source, const std::vector<std::wstring>& paths, const std::function<bool()>& cancelled) {
    Apartment apartment;
    if (active_drag) throw std::logic_error("A file drag is already active on this UI thread");
    auto object = data_object(paths, FileTransferEffect::none);
    ComPtr<IDropSource> provider; provider.Attach(new Source(source, cancelled));
    active_drag = object.Get();
    struct Reset { ~Reset() { active_drag = nullptr; } } reset;
    DWORD effect{};
    const auto hr = DoDragDrop(object.Get(), provider.Get(), DROPEFFECT_COPY | DROPEFFECT_MOVE, &effect);
    if (hr == DRAGDROP_S_CANCEL) return FileTransferEffect::none;
    hr_require(hr, "Drag files");
    if (hr != DRAGDROP_S_DROP) return FileTransferEffect::none;
    const auto logical = get_effect(object.Get(), CFSTR_LOGICALPERFORMEDDROPEFFECT);
    const auto performed = get_effect(object.Get(), CFSTR_PERFORMEDDROPEFFECT);
    // A conventional target asks its source to delete originals after copying.
    // This path-only source never deletes them: report Copy rather than an unfinished Move.
    if (effect == DROPEFFECT_MOVE && performed == DWORD{DROPEFFECT_MOVE}) return FileTransferEffect::copy;
    const auto result = static_cast<FileTransferEffect>(logical.value_or(effect));
    return single_effect(result) ? result : FileTransferEffect::none;
}
ComPtr<IDropTarget> drop_target(HWND window, std::shared_ptr<DataGrid> grid, std::function<bool()> available,
    std::function<void(std::function<void()>)> dispatch) {
    ComPtr<IDropTarget> target;
    target.Attach(new Target(window, std::move(grid), std::move(available), std::move(dispatch)));
    hr_require(RegisterDragDrop(window, target.Get()), "Register grid file drop target");
    return target;
}
}
