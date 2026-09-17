#include "../src/preview_policy.hpp"
#include "../src/preview_session.hpp"
#include "xui/shell_preview.hpp"
#include <shobjidl.h>
#include <ocidl.h>
#include <wrl/client.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <UIAutomation.h>
#include <sstream>
#include <iomanip>

using namespace xui;
using namespace xui::preview;
using Microsoft::WRL::ComPtr;
namespace {
const CLSID fixture_id{0xbd781d03, 0x6d7f, 0x4aa4, {0xa9, 0x0b, 0x8a, 0xc2, 0x56, 0x16, 0x61, 0x25}};
void expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void pump() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}
bool until(const std::function<bool()>& ready, DWORD milliseconds = 8000) {
    auto end = GetTickCount64() + milliseconds;
    while (GetTickCount64() < end) { if (ready()) return true; pump(); Sleep(5); }
    return ready();
}
std::wstring executable() {
    wchar_t path[32768]{};
    expect(GetModuleFileNameW(nullptr, path, 32768) != 0, "executable path");
    return path;
}
Handle launch(std::wstring arguments, HANDLE job = nullptr) {
    auto exe = executable();
    auto command = L"\"" + exe + L"\" " + arguments;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    expect(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr,
        &startup, &process) != FALSE, "test process");
    Handle result(process.hProcess), thread(process.hThread);
    if (job && !AssignProcessToJobObject(job, result.value)) {
        TerminateProcess(result.value, 1);
        throw std::runtime_error("test job assignment");
    }
    expect(ResumeThread(thread.value) != static_cast<DWORD>(-1), "test process resume");
    return result;
}
struct Observed {
    unsigned activated{}, initialized{}, rendered{}, resized{}, focused{}, unloaded{};
    HWND child{};
    std::string mode;
    ComPtr<IPreviewHandlerFrame> frame;
} observed;
class Handler final : public IPreviewHandler, public IInitializeWithStream, public IObjectWithSite, public IOleWindow {
    LONG refs_{1};
    HWND parent_{};
    RECT rect_{};
    ComPtr<IPreviewHandlerFrame> frame_;
    ComPtr<IStream> stream_;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (id == IID_IUnknown || id == IID_IPreviewHandler) *result = static_cast<IPreviewHandler*>(this);
        else if (id == IID_IInitializeWithStream) *result = static_cast<IInitializeWithStream*>(this);
        else if (id == IID_IObjectWithSite) *result = static_cast<IObjectWithSite*>(this);
        else if (id == IID_IOleWindow) *result = static_cast<IOleWindow*>(this);
        if (!*result) return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = InterlockedDecrement(&refs_); if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE Initialize(IStream* stream, DWORD mode) override {
        ++observed.initialized;
        if (mode != STGM_READ) return E_ACCESSDENIED;
        char content[128]{};
        ULONG read{};
        const auto hr = stream->Read(content, 127, &read);
        if (FAILED(hr)) return hr;
        observed.mode.assign(content, read);
        ULONG written{};
        if (stream->Write("bad", 3, &written) != STG_E_ACCESSDENIED) return E_UNEXPECTED;
        if (observed.mode == "initialize-fail") return E_FAIL;
        stream_ = stream;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override {
        frame_.Reset();
        const auto result = site ? site->QueryInterface(IID_PPV_ARGS(&frame_)) : S_OK;
        observed.frame = frame_;
        return result;
    }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID id, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return frame_ ? frame_->QueryInterface(id, result) : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE SetWindow(HWND parent, const RECT* rect) override { parent_ = parent; rect_ = *rect; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetRect(const RECT* rect) override {
        ++observed.resized; rect_ = *rect;
        if (observed.child) SetWindowPos(observed.child, nullptr, 0, 0, rect_.right, rect_.bottom, SWP_NOACTIVATE | SWP_NOZORDER);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DoPreview() override {
        ++observed.rendered;
        if (observed.mode == "render-fail") return E_FAIL;
        if (observed.mode == "server-crash") ExitProcess(23);
        if (observed.mode == "hang") Sleep(15000);
        observed.child = CreateWindowExW(0, L"EDIT", L"XUI controlled preview content",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY, 0, 0, rect_.right, rect_.bottom,
            parent_, nullptr, GetModuleHandleW(nullptr), nullptr);
        return observed.child ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE Unload() override {
        ++observed.unloaded;
        if (observed.child) DestroyWindow(observed.child);
        observed.child = nullptr; stream_.Reset(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override { ++observed.focused; ::SetFocus(observed.child); return S_OK; }
    HRESULT STDMETHODCALLTYPE QueryFocus(HWND* window) override { if (!window) return E_POINTER; *window = GetFocus(); return S_OK; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(MSG* message) override { return frame_ ? frame_->TranslateAccelerator(message) : S_FALSE; }
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* window) override { if (!window) return E_POINTER; *window = observed.child; return S_OK; }
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
};
class Factory final : public IClassFactory {
    LONG refs_{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { auto count = InterlockedDecrement(&refs_); if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID id, void** out) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        ++observed.activated;
        auto* instance = new Handler;
        const auto result = instance->QueryInterface(id, out); instance->Release(); return result;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};
int foreign_child(HWND parent, HANDLE ready) {
    auto child = CreateWindowExW(0, L"STATIC", L"Hostile foreign child", WS_CHILD | WS_VISIBLE,
        0, 0, 40, 40, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!child) return 2;
    SetEvent(ready);
    Sleep(15000);
    return 0;
}
int foreign_parent(HANDLE reached) {
    auto parent = CreateWindowExW(0, L"STATIC", L"Isolated teardown probe", WS_OVERLAPPEDWINDOW,
        0, 0, 300, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle ready(CreateEventW(&sa, TRUE, FALSE, nullptr));
    auto child = launch(L"--foreign-child " + std::to_wstring(reinterpret_cast<UINT_PTR>(parent)) + L" " +
        std::to_wstring(reinterpret_cast<UINT_PTR>(ready.value)));
    expect(until([&] { return WaitForSingleObject(ready.value, 0) == WAIT_OBJECT_0; }), "foreign child ready");
    SetEvent(reached);
    DestroyWindow(parent);
    return 0;
}
void foreign_probe() {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle reached(CreateEventW(&sa, TRUE, FALSE, nullptr)), job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    expect(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE, "probe job");
    auto parent = launch(L"--foreign-parent " + std::to_wstring(reinterpret_cast<UINT_PTR>(reached.value)), job.value);
    expect(WaitForSingleObject(reached.value, 8000) == WAIT_OBJECT_0, "teardown probe reached");
    auto result = WaitForSingleObject(parent.value, 1500);
    expect(result == WAIT_TIMEOUT, "foreign child must reproduce teardown blocker");
    std::cout << "foreign HWND DestroyWindow: " << (result == WAIT_TIMEOUT ? "BLOCKED beyond1500ms" : "returned within1500ms") << "\n";
    TerminateJobObject(job.value, 0);
    WaitForSingleObject(parent.value, 2000);
}
PreviewStatus wait_status(const std::shared_ptr<Session>& session, bool cleanup = false) {
    PreviewStatus status{};
    const bool completed = until([&] {
        std::lock_guard lock(session->mutex);
        if (session->result) status = *session->result;
        return cleanup ? status.cleanup != PreviewCleanup::none :
            status.state == PreviewState::accepted || status.state == PreviewState::failed || status.state == PreviewState::unsupported;
    });
    if (!completed) std::cerr << "deadline status: generation=" << status.generation << " state=" << static_cast<int>(status.state)
        << " reason=" << static_cast<int>(status.reason) << " cleanup=" << static_cast<int>(status.cleanup) << "\n";
    expect(completed, "preview completion deadline");
    return status;
}
int fixture_server(HANDLE ready) {
    expect(SUCCEEDED(OleInitialize(nullptr)), "fixture server COM");
    ComPtr<Factory> factory; factory.Attach(new Factory);
    DWORD cookie{};
    expect(SUCCEEDED(CoRegisterClassObject(fixture_id, factory.Get(), CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &cookie)), "server registration");
    SetEvent(ready);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    CoRevokeClassObject(cookie); OleUninitialize(); return 0;
}
void provider_failure_probe(const std::filesystem::path& source, const std::wstring& helper, const char* mode, bool timeout) {
    { std::ofstream file(source); file << mode; }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle ready(CreateEventW(&sa, TRUE, FALSE, nullptr)), job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    expect(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE, "fixture server job");
    auto server = launch(L"--server " + std::to_wstring(reinterpret_cast<UINT_PTR>(ready.value)), job.value);
    expect(WaitForSingleObject(ready.value, 8000) == WAIT_OBJECT_0, "fixture server ready");
    auto session = start_session(helper, source.wstring(), RECT{0, 0, 320, 240}, 50);
    auto status = wait_status(session);
    std::cout << mode << " state=" << static_cast<int>(status.state) << " reason=" << static_cast<int>(status.reason)
        << " phase=" << static_cast<int>(status.phase) << " cleanup=" << static_cast<int>(status.cleanup)
        << " hr=" << std::hex << status.hresult << std::dec << "\n";
    expect(status.state == PreviewState::failed, "failed provider result");
    if (timeout) {
        expect(status.reason == PreviewReason::timed_out && status.cleanup == PreviewCleanup::provider_unknown, "hung provider timeout");
        expect(WaitForSingleObject(server.value, 0) == WAIT_TIMEOUT, "shared-like fixture server not terminated by supervisor");
        auto rejected = start_session(helper, source.wstring(), RECT{0, 0, 320, 240}, 51);
        expect(wait_status(rejected).reason == PreviewReason::resource_limit, "circuit breaker after unconfirmed provider cleanup");
    }
    session->revoke();
    TerminateJobObject(job.value, 0);
    WaitForSingleObject(server.value, 2000);
    expect(drain_sessions(), "failure worker retirement");
    expect(DeleteFileW(source.c_str()) != FALSE, "failure fixture removed");
    std::cout << mode << " bounded result reason=" << static_cast<int>(status.reason) << "\n";
}
void tests() {
    expect(SUCCEEDED(OleInitialize(nullptr)), "COM STA");
    expect(local_path_syntax(L"C:\\local\\test.txt"), "local path");
    expect(!local_path_syntax(L"\\\\server\\test.txt"), "UNC rejected");
    expect(!local_path_syntax(L"C:\\local\\test.txt:secret"), "ADS rejected");
    expect(!allowed_origin("[ZoneTransfer]\r\nZoneId=3\r\n"), "Internet zone rejected");
    expect(!allowed_origin("[ZoneTransfer]\r\nZoneId=0\r\nZoneId=3\r\n"), "ambiguous zone rejected");
    expect(!allowed_origin("[ZoneTransfer]\r\nZoneId=0\r\n zoneid=3\r\n"), "case/whitespace zone ambiguity rejected");
    expect(allowed_origin("[ZoneTransfer]\r\nZoneId=0\r\n"), "local zone accepted");
    ShellPreview model;
    const auto first = model.load_local(L"C:\\old.txt");
    const auto second = model.load_local(L"C:\\new.txt");
    expect(!model.publish({first, PreviewState::accepted}), "stale generation ignored");
    model.cancel(first);
    expect(model.status().generation == second, "obsolete cancellation ignored");
    model.cancel(second);
    expect(model.source().empty(), "current cancellation clears source");
    auto directory = std::filesystem::path(executable()).parent_path();
    const auto source = directory / L"controlled.xui-preview-test";
    const auto helper = (directory / L"xui_preview_fixture_host.exe").wstring();
    { std::ofstream file(source); file << "content"; }
    auto file = open_eligible(source.wstring());
    expect(bool(file.file), "locally authored fixture eligible"); file.file.reset();
    auto parent = CreateWindowExW(0, L"STATIC", L"Controlled preview host", WS_OVERLAPPEDWINDOW,
        0, 0, 480, 320, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    expect(parent != nullptr, "preview parent");
    DWORD cookie{};
    ComPtr<Factory> factory; factory.Attach(new Factory);
    expect(SUCCEEDED(CoRegisterClassObject(fixture_id, factory.Get(), CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &cookie)), "fixture registration");
    auto session = start_session(helper, source.wstring(), RECT{0, 0, 320, 240}, 10);
    PreviewStatus status{};
    expect(until([&] {
        std::lock_guard lock(session->mutex);
        if (session->result) status = *session->result;
        return status.state == PreviewState::accepted || status.state == PreviewState::failed || status.state == PreviewState::unsupported;
    }), "fixture result deadline");
    std::cout << "fixture state=" << static_cast<int>(status.state) << " reason=" << static_cast<int>(status.reason)
        << " phase=" << static_cast<int>(status.phase) << " hr=" << std::hex << status.hresult << std::dec << "\n";
    expect(status.state == PreviewState::accepted, "fixture accepted");
    expect(observed.activated == 1 && observed.initialized == 1 && observed.rendered == 1 && observed.child, "real COM activation/render");
    auto provider_window = GetAncestor(observed.child, GA_ROOT);
    expect(provider_window != parent && GetWindow(provider_window, GW_OWNER) == nullptr, "provider host independent from application");
    wchar_t content[128]{};
    GetWindowTextW(observed.child, content, 128);
    expect(std::wstring_view(content) == L"XUI controlled preview content", "visible native content");
    ComPtr<IUIAutomation> automation;
    expect(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))), "UIA client");
    ComPtr<IUIAutomationElement> element;
    expect(SUCCEEDED(automation->ElementFromHandle(observed.child, &element)), "provider accessibility element");
    CONTROLTYPEID type{};
    expect(SUCCEEDED(element->get_CurrentControlType(&type)) && type == UIA_EditControlTypeId, "native edit accessibility");
    { std::lock_guard lock(session->mutex); session->rect = {0, 0, 400, 270}; session->focus = false; }
    expect(until([] { return observed.resized && observed.focused; }), "resize and focus delivered");
    MSG tab{}; tab.message = WM_KEYDOWN; tab.wParam = VK_TAB;
    expect(observed.frame && observed.frame->TranslateAccelerator(&tab) == S_OK, "frame accelerator");
    expect(until([&] { std::lock_guard lock(session->mutex); return (session->focus_actions & leave_forward) != 0; }), "typed Tab traversal");
    session->revoke();
    expect(until([] { return observed.unloaded == 1; }), "provider unload");
    expect(until([&] { return DeleteFileW(source.c_str()) != FALSE; }), "checked file released");
    expect(drain_sessions(), "success worker retired");
    for (const auto mode : {"initialize-fail", "render-fail", "content"}) {
        { std::ofstream data(source); data << mode; }
        auto next = start_session(helper, source.wstring(), RECT{0, 0, 320, 240}, 20);
        const auto result = wait_status(next);
        if (std::string_view(mode) == "content") {
            expect(result.state == PreviewState::accepted, "replacement provider accepted");
            PostMessageW(GetAncestor(observed.child, GA_ROOT), WM_CLOSE, 0, 0);
            const auto closed = wait_status(next, true);
            expect(closed.reason == PreviewReason::cancelled && closed.cleanup == PreviewCleanup::unloaded, "helper caption close preserves basic");
            expect(IsWindow(parent) != FALSE, "basic parent survives helper closure");
        } else {
            expect(result.state == PreviewState::failed, "explicit provider failure");
            expect(result.reason == (std::string_view(mode) == "initialize-fail" ?
                PreviewReason::initialization_failed : PreviewReason::render_failed), "provider failure reason");
        }
        next->revoke();
        expect(until([&] { return DeleteFileW(source.c_str()) != FALSE; }), "failure file released");
        expect(drain_sessions(), "replacement retirement");
    }
    { std::ofstream data(source); data << "content"; }
    { std::ofstream origin(source.wstring() + L":Zone.Identifier"); origin << "[ZoneTransfer]\r\nZoneId=3\r\n"; }
    const auto activated = observed.activated;
    auto denied = start_session(helper, source.wstring(), RECT{0, 0, 320, 240}, 30);
    expect(wait_status(denied).reason == PreviewReason::restricted && observed.activated == activated, "restricted origin never activates provider");
    denied->revoke(); expect(drain_sessions(), "restricted retirement");
    expect(DeleteFileW(source.c_str()) != FALSE, "restricted fixture removed");
    CoRevokeClassObject(cookie);
    { std::ofstream data(source); data << "content"; }
    auto missing = start_session(helper, source.wstring(), RECT{0, 0, 320, 240}, 31);
    expect(wait_status(missing).reason == PreviewReason::missing_provider, "missing fixture provider");
    missing->revoke(); expect(drain_sessions(), "missing retirement");
    auto crashed = start_session(executable(), source.wstring(), RECT{0, 0, 320, 240}, 32);
    expect(wait_status(crashed).reason == PreviewReason::broker_failed, "owned helper crash reported");
    crashed->revoke(); expect(drain_sessions(), "crash retirement");
    auto cancelled = start_session(helper, source.wstring(), RECT{0, 0, 320, 240}, 33);
    cancelled->revoke(); expect(drain_sessions(), "cancel during activation retired");
    { std::lock_guard lock(cancelled->mutex); expect(!cancelled->result, "cancelled delivery silent"); }
    expect(DeleteFileW(source.c_str()) != FALSE, "missing fixture removed");
    DestroyWindow(parent);
    foreign_probe();
    OleUninitialize();
    std::cout << "preview fixtures passed\n";
}
int installed_probe(std::wstring_view extension) {
    expect(SUCCEEDED(OleInitialize(nullptr)), "installed probe STA");
    const auto directory = std::filesystem::path(executable()).parent_path();
    const auto source = directory / (L"xui-installed-preview-fixture" + std::wstring(extension));
    {
        std::ofstream file(source, std::ios::binary);
        if (extension == L".pdf") {
            std::ostringstream pdf;
            pdf << "%PDF-1.4\n";
            const std::string content = "BT /F1 18 Tf 40 150 Td (XUI locally authored preview fixture) Tj ET\n";
            std::vector<std::string> objects{
                "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 500 300] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
                "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
                "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream"};
            std::vector<std::streamoff> positions;
            for (std::size_t i = 0; i < objects.size(); ++i) {
                positions.push_back(pdf.tellp()); pdf << i + 1 << " 0 obj\n" << objects[i] << "\nendobj\n";
            }
            const auto start = pdf.tellp();
            pdf << "xref\n0 6\n0000000000 65535 f \n";
            for (auto position : positions) pdf << std::setw(10) << std::setfill('0') << position << " 00000 n \n";
            pdf << "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n" << start << "\n%%EOF\n";
            file << pdf.str();
        } else file << (extension == L".txt" ? "XUI locally authored preview fixture. No external references." :
            "{\\rtf1\\ansi XUI locally authored preview fixture. No external references.}");
    }
    auto session = start_session((directory / L"xui_preview_host.exe").wstring(), source.wstring(), RECT{0, 0, 600, 400}, 80);
    const auto status = wait_status(session);
    std::wcout << L"installed " << extension << L"\n";
    std::cout << "state=" << static_cast<int>(status.state) << " reason=" << static_cast<int>(status.reason)
        << " phase=" << static_cast<int>(status.phase) << " hr=" << std::hex << status.hresult << std::dec << "\n";
    { std::lock_guard lock(session->mutex); std::wcout << L"selected provider=" << session->provider << L"\n"; }
    if (status.state == PreviewState::accepted) {
        struct Windows { DWORD process{}; HWND host{}; } windows;
        { std::lock_guard lock(session->mutex); windows.process = session->broker_process; }
        EnumWindows([](HWND window, LPARAM context) -> BOOL {
            auto& found = *reinterpret_cast<Windows*>(context);
            DWORD process{}; GetWindowThreadProcessId(window, &process);
            if (process == found.process) found.host = window;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&windows));
        expect(windows.host && IsWindowVisible(windows.host), "installed preview visible host");
        std::cout << "installed provider child windows:\n";
        EnumChildWindows(windows.host, [](HWND window, LPARAM) -> BOOL {
            wchar_t name[128]{}, text[512]{};
            GetClassNameW(window, name, 128);
            DWORD_PTR result{};
            const auto received = SendMessageTimeoutW(window, WM_GETTEXT, 512, reinterpret_cast<LPARAM>(text),
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &result);
            std::wcout << L" class=" << name << L" visible=" << IsWindowVisible(window)
                << L" text=" << (received ? text : L"<timeout>") << L"\n";
            return TRUE;
        }, 0);
    }
    session->revoke();
    expect(drain_sessions(), "installed helper retirement");
    expect(DeleteFileW(source.c_str()) != FALSE, "installed fixture removed");
    OleUninitialize();
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && std::wstring_view(argv[1]) == L"--installed") return installed_probe(L".rtf");
        if (argc == 2 && std::wstring_view(argv[1]) == L"--installed-text") return installed_probe(L".txt");
        if (argc == 2 && std::wstring_view(argv[1]) == L"--installed-pdf") return installed_probe(L".pdf");
        if (argc == 3 && std::wstring_view(argv[1]) == L"--failure-probe") {
            expect(SUCCEEDED(OleInitialize(nullptr)), "failure probe STA");
            const auto directory = std::filesystem::path(executable()).parent_path();
            const bool hang = std::wstring_view(argv[2]) == L"hang";
            provider_failure_probe(directory / L"failure.xui-preview-test", (directory / L"xui_preview_fixture_host.exe").wstring(),
                hang ? "hang" : "server-crash", hang);
            OleUninitialize(); return 0;
        }
        if (argc == 4 && std::wstring_view(argv[1]) == L"--foreign-child")
            return foreign_child(reinterpret_cast<HWND>(_wcstoui64(argv[2], nullptr, 10)), reinterpret_cast<HANDLE>(_wcstoui64(argv[3], nullptr, 10)));
        if (argc == 3 && std::wstring_view(argv[1]) == L"--foreign-parent")
            return foreign_parent(reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 10)));
        if (argc == 3 && std::wstring_view(argv[1]) == L"--server")
            return fixture_server(reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 10)));
        if (argc == 4) return 17; // Deliberately failed broker executable, selected only by this test.
        tests(); return 0;
    } catch (const Failure& failure) {
        std::cerr << "policy failure " << static_cast<int>(failure.reason) << " hr=" << std::hex << failure.hr << "\n"; return 1;
    } catch (const std::exception& error) { std::cerr << error.what() << "\n"; return 1; }
}
