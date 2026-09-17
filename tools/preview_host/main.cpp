#include "../../src/preview_policy.hpp"
#include <shobjidl.h>
#include <shlguid.h>
#include <shlwapi.h>
#include <propsys.h>
#include <ocidl.h>
#include <array>
#include <string>
#include <cwchar>
#include <cstring>
#include <new>

using namespace xui;
using namespace xui::preview;
using Microsoft::WRL::ComPtr;
namespace {
struct Key {
    HKEY value{};
    ~Key() { if (value) RegCloseKey(value); }
};
std::wstring registry_string(HKEY root, const std::wstring& key, const wchar_t* name) {
    std::array<wchar_t, 2048> value{};
    DWORD bytes = static_cast<DWORD>(sizeof(value));
    const auto result = RegGetValueW(root, key.c_str(), name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes);
    if (result == ERROR_FILE_NOT_FOUND) return {};
    require(HRESULT_FROM_WIN32(result), PreviewReason::unsupported_provider, PreviewPhase::activation);
    return value.data();
}
bool registry_flag(HKEY root, const std::wstring& key, const wchar_t* name) {
    DWORD value{}, bytes = sizeof(value);
    const auto result = RegGetValueW(root, key.c_str(), name, RRF_RT_REG_DWORD, nullptr, &value, &bytes);
    if (result == ERROR_FILE_NOT_FOUND) return false;
    require(HRESULT_FROM_WIN32(result), PreviewReason::unsupported_provider, PreviewPhase::activation);
    return value != 0;
}
void approve_machine(const std::wstring& path) {
    if (!local_path_syntax(path) || GetDriveTypeW(path.substr(0, 3).c_str()) != DRIVE_FIXED)
        throw Failure{PreviewReason::unsupported_provider, PreviewPhase::activation, E_ACCESSDENIED};
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) throw Failure{PreviewReason::missing_provider, PreviewPhase::activation, HRESULT_FROM_WIN32(GetLastError())};
    IMAGE_DOS_HEADER dos{};
    DWORD bytes{};
    if (!ReadFile(file.value, &dos, sizeof(dos), &bytes, nullptr) || bytes != sizeof(dos) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof(dos) || dos.e_lfanew > 1048576)
        throw Failure{PreviewReason::unsupported_provider, PreviewPhase::activation, HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT)};
    LARGE_INTEGER offset{}; offset.QuadPart = dos.e_lfanew;
    struct Header { DWORD signature; IMAGE_FILE_HEADER file; } header{};
    if (!SetFilePointerEx(file.value, offset, nullptr, FILE_BEGIN) ||
        !ReadFile(file.value, &header, sizeof(header), &bytes, nullptr) || bytes != sizeof(header) ||
        header.signature != IMAGE_NT_SIGNATURE)
        throw Failure{PreviewReason::unsupported_provider, PreviewPhase::activation, HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT)};
#if defined(_M_ARM64)
    const bool supported = header.file.Machine == IMAGE_FILE_MACHINE_ARM64 || header.file.Machine == 0xa64e; // ARM64X
#else
    const bool supported = header.file.Machine == IMAGE_FILE_MACHINE_AMD64;
#endif
    if (!supported) throw Failure{PreviewReason::unsupported_architecture, PreviewPhase::activation,
        HRESULT_FROM_WIN32(ERROR_EXE_MACHINE_TYPE_MISMATCH)};
}
void approve_registration(const std::wstring& clsid) {
    const auto key = L"CLSID\\" + clsid;
    const auto library = registry_string(HKEY_CLASSES_ROOT, key + L"\\InprocServer32", nullptr);
    if (library.empty())
        throw Failure{PreviewReason::missing_provider, PreviewPhase::activation, REGDB_E_CLASSNOTREG};
    if (registry_flag(HKEY_CLASSES_ROOT, key, L"DisableLowILProcessIsolation"))
        throw Failure{PreviewReason::unsupported_provider, PreviewPhase::activation, E_ACCESSDENIED};
    for (const auto* server : {L"LocalServer32", L"LocalServer"}) {
        Key opened;
        const auto error = RegOpenKeyExW(HKEY_CLASSES_ROOT, (key + L"\\" + server).c_str(), 0, KEY_READ, &opened.value);
        if (error != ERROR_FILE_NOT_FOUND)
            throw Failure{PreviewReason::unsupported_provider, PreviewPhase::activation, E_ACCESSDENIED};
    }
    const auto appid = registry_string(HKEY_CLASSES_ROOT, key, L"AppID");
    if (appid.empty()) throw Failure{PreviewReason::unsupported_provider, PreviewPhase::activation, REGDB_E_CLASSNOTREG};
    const auto app = L"AppID\\" + appid;
    const auto surrogate = registry_string(HKEY_CLASSES_ROOT, app, L"DllSurrogate");
    std::array<wchar_t, MAX_PATH> system{};
    const auto length = GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
    if (!length || length >= system.size())
        throw Failure{PreviewReason::unsupported_provider, PreviewPhase::activation, HRESULT_FROM_WIN32(GetLastError())};
    const auto system_surrogate = std::wstring(system.data()) + L"\\prevhost.exe";
    if ((_wcsicmp(surrogate.c_str(), L"prevhost.exe") != 0 && _wcsicmp(surrogate.c_str(), system_surrogate.c_str()) != 0) ||
        !registry_string(HKEY_CLASSES_ROOT, app, L"LocalService").empty() ||
        !registry_string(HKEY_CLASSES_ROOT, app, L"RemoteServerName").empty() ||
        !registry_string(HKEY_CLASSES_ROOT, app, L"DllSurrogateExecutable").empty() ||
        !registry_string(HKEY_CLASSES_ROOT, app, L"RunAs").empty())
        throw Failure{PreviewReason::unsupported_provider, PreviewPhase::activation, E_ACCESSDENIED};
    for (const auto root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        Key blocked;
        const auto error = RegOpenKeyExW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked",
            0, KEY_QUERY_VALUE, &blocked.value);
        if (error == ERROR_FILE_NOT_FOUND) continue;
        require(HRESULT_FROM_WIN32(error), PreviewReason::restricted, PreviewPhase::policy);
        const auto found = RegQueryValueExW(blocked.value, clsid.c_str(), nullptr, nullptr, nullptr, nullptr);
        if (found != ERROR_FILE_NOT_FOUND) throw Failure{PreviewReason::restricted, PreviewPhase::policy, E_ACCESSDENIED};
    }
    DWORD show{}, bytes = sizeof(show);
    const auto setting = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
        L"ShowPreviewHandlers", RRF_RT_REG_DWORD, nullptr, &show, &bytes);
    if ((setting == ERROR_SUCCESS && !show) || (setting != ERROR_SUCCESS && setting != ERROR_FILE_NOT_FOUND))
        throw Failure{PreviewReason::restricted, PreviewPhase::policy, E_ACCESSDENIED};
    approve_machine(library);
}
class Site final : public IPreviewHandlerFrame, public IObjectWithSite {
    LONG refs_{1};
    Channel& channel_;
    HANDLE changed_;
public:
    Site(Channel& channel, HANDLE changed) : channel_(channel), changed_(changed) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IPreviewHandlerFrame) *out = static_cast<IPreviewHandlerFrame*>(this);
        if (iid == IID_IObjectWithSite) *out = static_cast<IObjectWithSite*>(this);
        if (!*out) return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { auto n = InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID, void**) override { return E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE GetWindowContext(PREVIEWHANDLERFRAMEINFO* info) override {
        if (!info) return E_POINTER;
        *info = {};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(MSG* message) override {
        if (!message) return E_POINTER;
        if (message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN) return S_FALSE;
        LONG action{};
        if (message->wParam == VK_ESCAPE) { PostQuitMessage(0); return S_OK; }
        else if (message->wParam == VK_TAB) action = (GetKeyState(VK_SHIFT) & 0x8000) ?
            FocusAction::leave_reverse : FocusAction::leave_forward;
        if (!action) return S_FALSE;
        InterlockedOr(&channel_.focus_actions, action);
        if (!SetEvent(changed_)) return HRESULT_FROM_WIN32(GetLastError());
        return S_OK;
    }
};
class Preview {
    Channel& channel_;
    HANDLE changed_;
    HWND bridge_{};
    EligibleFile file_;
    ComPtr<IStream> stream_;
    ComPtr<IPreviewHandler> handler_;
    ComPtr<IObjectWithSite> sited_;
    ComPtr<Site> site_;
    bool initialized_{};
    bool ready_{};
    void phase(PreviewPhase value) { InterlockedExchange(&channel_.phase, static_cast<LONG>(value)); }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Preview*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Preview*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == WM_CLOSE) {
            PostQuitMessage(0);
            return 0;
        }
        if (self && self->ready_ && message == WM_SIZE) {
            self->phase(PreviewPhase::resize);
            RECT rect{}; GetClientRect(window, &rect);
            const auto hr = self->handler_->SetRect(&rect);
            if (FAILED(hr)) {
                self->channel_.status = {self->channel_.generation, PreviewState::failed,
                    PreviewReason::render_failed, PreviewPhase::resize, hr};
                PostQuitMessage(0);
            }
            return 0;
        }
        return DefWindowProcW(window, message, wp, lp);
    }
public:
    Preview(Channel& channel, HANDLE changed) : channel_(channel), changed_(changed) {}
    ~Preview() { if (bridge_) DestroyWindow(bridge_); }
    PreviewStatus load() {
        phase(PreviewPhase::policy);
        file_ = open_eligible(channel_.path);
        phase(PreviewPhase::discovery);
        ComPtr<IShellItem> item;
        require(SHCreateItemFromParsingName(file_.path.c_str(), nullptr, IID_PPV_ARGS(&item)),
            PreviewReason::initialization_failed, PreviewPhase::discovery);
        ComPtr<IQueryAssociations> associations;
        require(item->BindToHandler(nullptr, BHID_AssociationArray, IID_PPV_ARGS(&associations)),
            PreviewReason::initialization_failed, PreviewPhase::discovery);
        std::array<wchar_t, 128> id{};
        DWORD count = static_cast<DWORD>(id.size());
        auto association = associations->GetString(ASSOCF_NOTRUNCATE, ASSOCSTR_SHELLEXTENSION,
            L"{8895b1c6-b41f-4c1c-a562-0d564250836f}", id.data(), &count);
#ifdef XUI_PREVIEW_FIXTURES
        const bool fixture = file_.path.ends_with(L".xui-preview-test");
        if (fixture) {
            wcscpy_s(id.data(), id.size(), L"{BD781D03-6D7F-4AA4-A90B-8AC256166125}");
            association = S_OK;
        }
#endif
        if (association == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION) || association == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
            return {channel_.generation, PreviewState::unsupported, PreviewReason::no_handler, PreviewPhase::discovery, association};
        require(association, PreviewReason::initialization_failed, PreviewPhase::discovery);
        CLSID clsid{};
        require(CLSIDFromString(id.data(), &clsid), PreviewReason::missing_provider, PreviewPhase::discovery);
        wcscpy_s(channel_.provider, id.data());
#ifdef XUI_PREVIEW_FIXTURES
        // Only the separately named fixture broker can select the registered test class.
        if (!fixture)
#endif
        approve_registration(id.data());
        phase(PreviewPhase::activation);
        wchar_t local[] = L"localhost";
        COSERVERINFO server{}; server.pwszName = local;
        MULTI_QI query{&IID_IPreviewHandler};
        const auto activated = CoCreateInstanceEx(clsid, nullptr,
            CLSCTX_LOCAL_SERVER | CLSCTX_NO_CODE_DOWNLOAD | CLSCTX_DISABLE_AAA, &server, 1, &query);
        ComPtr<IUnknown> instance; instance.Attach(query.pItf);
        require(activated, activated == REGDB_E_CLASSNOTREG ? PreviewReason::missing_provider :
            PreviewReason::activation_failed, PreviewPhase::activation);
        require(query.hr, PreviewReason::unsupported_provider, PreviewPhase::activation);
        require(instance.As(&handler_), PreviewReason::unsupported_provider, PreviewPhase::activation);
        require(instance.As(&sited_), PreviewReason::unsupported_provider, PreviewPhase::initialize);
        ComPtr<IInitializeWithStream> initialize;
        require(instance.As(&initialize), PreviewReason::unsupported_provider, PreviewPhase::initialize);
        site_.Attach(new Site(channel_, changed_));
        require(sited_->SetSite(static_cast<IPreviewHandlerFrame*>(site_.Get())), PreviewReason::initialization_failed, PreviewPhase::initialize);
        phase(PreviewPhase::initialize);
        stream_ = readonly_stream(file_.file.value);
        require(initialize->Initialize(stream_.Get(), STGM_READ), PreviewReason::initialization_failed, PreviewPhase::initialize);
        initialized_ = true;
        WNDCLASSW host{};
        host.lpfnWndProc = procedure; host.hInstance = GetModuleHandleW(nullptr);
        host.lpszClassName = L"XuiIsolatedPreview"; host.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassW(&host)) throw Failure{PreviewReason::render_failed, PreviewPhase::render, HRESULT_FROM_WIN32(GetLastError())};
        auto outer = channel_.rect;
        AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0);
        // Foreign provider children never enter XUI's window tree or input queue.
        bridge_ = CreateWindowExW(0, host.lpszClassName, L"XUI installed preview - third-party content",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
            outer.right - outer.left, outer.bottom - outer.top, nullptr,
            nullptr, GetModuleHandleW(nullptr), this);
        if (!bridge_) throw Failure{PreviewReason::render_failed, PreviewPhase::render, HRESULT_FROM_WIN32(GetLastError())};
        require(handler_->SetWindow(bridge_, &channel_.rect), PreviewReason::render_failed, PreviewPhase::render);
        phase(PreviewPhase::render);
        require(handler_->DoPreview(), PreviewReason::render_failed, PreviewPhase::render);
        ready_ = true;
        ShowWindow(bridge_, SW_SHOWNOACTIVATE);
        ComPtr<IOleWindow> window;
        if (SUCCEEDED(handler_.As(&window))) {
            HWND child{};
            if (SUCCEEDED(window->GetWindow(&child)) && child) GetWindowThreadProcessId(child, &channel_.server_process);
        }
        return {channel_.generation, PreviewState::accepted};
    }
    void resize() {
        phase(PreviewPhase::resize);
        if (!handler_ || !bridge_) return;
        auto outer = channel_.rect;
        AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0);
        if (!SetWindowPos(bridge_, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) throw Failure{PreviewReason::render_failed, PreviewPhase::resize, HRESULT_FROM_WIN32(GetLastError())};
    }
    void focus() {
        phase(PreviewPhase::focus);
        if (!handler_) return;
        BYTE keyboard[256]{};
        if (!GetKeyboardState(keyboard)) throw Failure{PreviewReason::render_failed, PreviewPhase::focus, HRESULT_FROM_WIN32(GetLastError())};
        BYTE changed[256]; std::memcpy(changed, keyboard, sizeof(changed));
        changed[VK_SHIFT] = channel_.reverse ? 0x80 : 0;
        SetKeyboardState(changed);
        const auto result = handler_->SetFocus();
        SetKeyboardState(keyboard);
        require(result, PreviewReason::render_failed, PreviewPhase::focus);
    }
    void unload() {
        phase(PreviewPhase::unload);
        ready_ = false;
        HRESULT failure = S_OK;
        if (handler_ && initialized_) failure = handler_->Unload();
        if (sited_) {
            const auto result = sited_->SetSite(nullptr);
            if (SUCCEEDED(failure)) failure = result;
        }
        handler_.Reset(); sited_.Reset(); stream_.Reset(); site_.Reset(); file_ = {};
        if (bridge_) { DestroyWindow(bridge_); bridge_ = nullptr; }
        require(failure, PreviewReason::render_failed, PreviewPhase::unload);
    }
};
int run(Channel& channel, HANDLE request, HANDLE response) {
    if (channel.version != protocol_version || channel.bytes != sizeof(Channel)) return ERROR_REVISION_MISMATCH;
    const HRESULT com = OleInitialize(nullptr);
    if (FAILED(com)) return static_cast<int>(com);
    int exit{};
    {
        Preview preview(channel, response);
        bool stop{};
        while (!stop) {
            const auto wait = MsgWaitForMultipleObjectsEx(1, &request, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (wait == WAIT_FAILED) { exit = static_cast<int>(GetLastError()); break; }
            if (wait == WAIT_OBJECT_0) {
                const LONG issued = InterlockedCompareExchange(&channel.issued, 0, 0);
                auto status = channel.status;
                try {
                    switch (channel.command) {
                    case Command::load: status = preview.load(); break;
                    case Command::resize: preview.resize(); break;
                    case Command::focus: preview.focus(); break;
                    case Command::ping: break;
                    case Command::unload:
                        preview.unload(); status.cleanup = PreviewCleanup::unloaded; stop = true; break;
                    }
                } catch (const Failure& failure) {
                    status = {channel.generation,
                        failure.reason == PreviewReason::restricted || failure.reason == PreviewReason::unsupported_provider ||
                        failure.reason == PreviewReason::unsupported_architecture ?
                            PreviewState::unsupported : PreviewState::failed,
                        failure.reason, failure.phase, failure.hr};
                } catch (const std::bad_alloc&) {
                    status = {channel.generation, PreviewState::failed, PreviewReason::resource_limit,
                        static_cast<PreviewPhase>(channel.phase), E_OUTOFMEMORY};
                } catch (...) {
                    status = {channel.generation, PreviewState::failed, PreviewReason::broker_failed,
                        static_cast<PreviewPhase>(channel.phase), E_UNEXPECTED};
                }
                channel.status = status;
                InterlockedExchange(&channel.completed, issued);
                if (!SetEvent(response)) { exit = static_cast<int>(GetLastError()); break; }
            }
            MSG message{};
            for (int i = 0; i < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++i) {
                if (message.message == WM_QUIT) { stop = true; break; }
                TranslateMessage(&message); DispatchMessageW(&message);
            }
        }
        if (channel.command != Command::unload) {
            try {
                preview.unload();
                if (channel.status.state != PreviewState::failed)
                    channel.status = {channel.generation, PreviewState::unsupported, PreviewReason::cancelled};
                channel.status.cleanup = PreviewCleanup::unloaded;
            } catch (const Failure& failure) {
                channel.status = {channel.generation, PreviewState::failed, failure.reason, failure.phase,
                    failure.hr, PreviewCleanup::provider_unknown};
            }
            InterlockedExchange(&channel.completed, InterlockedCompareExchange(&channel.issued, 0, 0));
            SetEvent(response);
        }
    }
    OleUninitialize();
    return exit;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return ERROR_INVALID_PARAMETER;
    HANDLE handles[3]{};
    for (int i = 0; i < 3; ++i) {
        wchar_t* end{};
        const auto value = _wcstoui64(argv[i + 1], &end, 16);
        if (!value || !end || *end) return ERROR_INVALID_PARAMETER;
        handles[i] = reinterpret_cast<HANDLE>(value);
    }
    Handle mapping(handles[0]), request(handles[1]), response(handles[2]);
    auto* channel = static_cast<Channel*>(MapViewOfFile(mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Channel)));
    if (!channel) return static_cast<int>(GetLastError());
    const auto result = run(*channel, request.value, response.value);
    UnmapViewOfFile(channel);
    return result;
}
