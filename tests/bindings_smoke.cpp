#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using Microsoft::WRL::ComPtr;
namespace {
int assertions{};
void require(bool b, const char* message) { if (!b) throw std::runtime_error(message); ++assertions; }
void check(HRESULT hr, const char* message) { require(SUCCEEDED(hr), message); }
bool eventually(const std::function<bool()>& predicate) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
struct Process {
    PROCESS_INFORMATION info{}; HWND window{};
    ~Process() {
        if (!info.hProcess) return;
        if (IsWindow(window)) PostMessageW(window, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(info.hProcess, 5000) == WAIT_TIMEOUT) TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hProcess); CloseHandle(info.hThread);
    }
    void start(std::wstring command) {
        STARTUPINFOW startup{sizeof(startup)};
        require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
            &startup, &info) != 0, "Create binding client");
        require(eventually([&] {
            EnumWindows([](HWND h, LPARAM data) -> BOOL {
                auto& p = *reinterpret_cast<Process*>(data); DWORD id{}; GetWindowThreadProcessId(h, &id);
                wchar_t cls[80]{}; GetClassNameW(h, cls, 80);
                if (id == p.info.dwProcessId && std::wstring_view(cls) == L"Xui.Window.1") p.window = h;
                return TRUE;
            }, reinterpret_cast<LPARAM>(this));
            return window && IsWindowVisible(window) && SendMessageW(window, WM_APP + 60, 0, 0) > 0;
        }), "Wait for first paint");
        SetWindowPos(window, HWND_TOPMOST, 40, 40, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    }
};
ComPtr<IUIAutomationElement> find(IUIAutomation* a, IUIAutomationElement* root,
    const wchar_t* name, PROPERTYID property = UIA_AutomationIdPropertyId) {
    VARIANT v{}; v.vt = VT_BSTR; v.bstrVal = SysAllocString(name);
    ComPtr<IUIAutomationCondition> condition;
    auto hr = a->CreatePropertyCondition(property, v, &condition); VariantClear(&v); check(hr, "Create UIA condition");
    ComPtr<IUIAutomationElement> element;
    check(root->FindFirst(TreeScope_Descendants, condition.Get(), &element), "Find UIA control");
    require(element != nullptr, "The binding control exists");
    return element;
}
template<class T> ComPtr<T> pattern(IUIAutomationElement* e, PATTERNID id) {
    ComPtr<T> result;
    check(e->GetCurrentPatternAs(id, __uuidof(T), reinterpret_cast<void**>(result.GetAddressOf())), "Get UIA pattern");
    if (!result) throw std::runtime_error("Missing UIA pattern " + std::to_string(id));
    ++assertions; return result;
}
std::wstring name(IUIAutomationElement* e) {
    BSTR value{}; check(e->get_CurrentName(&value), "Read accessible name");
    std::wstring text = value ? value : L""; SysFreeString(value); return text;
}
void key(HWND h, UINT value) { PostMessageW(h, WM_KEYDOWN, value, 0); PostMessageW(h, WM_KEYUP, value, 0); }
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Initialize UIA COM");
    try {
        Process p;
        std::wstring command = L"\"" + std::wstring(argv[1]) + L"\"";
        if (argc > 2) command += L" \"" + std::wstring(argv[2]) + L"\"";
        const bool failure = argc > 3 && std::wstring_view(argv[3]) == L"--failure";
        if (failure) command += L" --callback-fail";
        p.start(command);
        USHORT process_machine{}, native_machine{};
        require(IsWow64Process2(p.info.hProcess, &process_machine, &native_machine) &&
            process_machine == IMAGE_FILE_MACHINE_UNKNOWN && native_machine == IMAGE_FILE_MACHINE_ARM64, "Native ARM64 client");
        require(SendMessageW(p.window, WM_APP + 60, 11, 0) == 1, "Exactly one render target");
        ComPtr<IUIAutomation> a; check(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&a)), "Create UIA");
        ComPtr<IUIAutomationElement> root; check(a->ElementFromHandle(p.window, &root), "Get root");
        auto status = find(a.Get(), root.Get(), L"status");
        require(name(status.Get()).find(L"日本語 😀") != std::wstring::npos, "Long Unicode label");
        auto apply = find(a.Get(), root.Get(), L"apply");
        auto invoke = pattern<IUIAutomationInvokePattern>(apply.Get(), UIA_InvokePatternId);
        const auto invoked = invoke->Invoke();
        if (failure) {
            require(WaitForSingleObject(p.info.hProcess, 10000) == WAIT_OBJECT_0, "Callback failure closes the client");
            DWORD exit{}; GetExitCodeProcess(p.info.hProcess, &exit);
            require(exit == 1, "Callback failure returns a nonzero application result");
            invoke.Reset(); apply.Reset(); status.Reset(); root.Reset(); a.Reset();
            CoUninitialize();
            std::cout << "GUI callback failure surfaced without crossing the ABI.\n";
            return 0;
        }
        check(invoked, "Invoke native button");
        require(eventually([&] { return name(status.Get()) == L"Applied"; }), "Foreign button callback changed retained label");
        auto toggle = find(a.Get(), root.Get(), L"toggle");
        auto toggle_pattern = pattern<IUIAutomationTogglePattern>(toggle.Get(), UIA_TogglePatternId);
        check(toggle_pattern->Toggle(), "Toggle native checkbox");
        require(eventually([&] { return name(status.Get()) == L"Enabled"; }), "Foreign toggle callback");
        ToggleState state{}; check(toggle_pattern->get_CurrentToggleState(&state), "Read toggle state");
        require(state == ToggleState_On, "Native toggle state");
        HWND edit = FindWindowExW(p.window, nullptr, L"EDIT", nullptr); require(edit != nullptr, "Native EDIT exists");
        ComPtr<IUIAutomationElement> input; check(a->ElementFromHandle(edit, &input), "Get native edit provider");
        auto value = pattern<IUIAutomationValuePattern>(input.Get(), UIA_ValuePatternId);
        ComPtr<IUIAutomationTextPattern> native_text;
        check(input->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&native_text)), "Query available native text pattern");
        std::cout << "Native EDIT TextPattern available: " << (native_text != nullptr) << '\n';
        BSTR changed = SysAllocString(L"Changed 日本語 😀");
        const auto changed_result = value->SetValue(changed); SysFreeString(changed);
        check(changed_result, "Set native Unicode input");
        require(eventually([&] { return name(status.Get()) == L"Edited"; }), "Foreign native input callback");
        BSTR input_text{}; check(value->get_CurrentValue(&input_text), "Read native input");
        require(std::wstring_view(input_text) == L"Changed 日本語 😀", "Unicode edit round trip"); SysFreeString(input_text);
        check(input->SetFocus(), "Focus real native input");
        require(eventually([&] { BOOL focused{}; input->get_CurrentHasKeyboardFocus(&focused); return focused != FALSE; }),
            "Stable native keyboard focus");
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SendMessageW(edit, WM_CHAR, L'A', 0);
        SendMessageW(edit, WM_CHAR, 0xd83d, 0);
        SendMessageW(edit, WM_CHAR, 0xde00, 0);
        require(eventually([&] {
            BSTR typed{};
            const auto hr = value->get_CurrentValue(&typed);
            const bool correct = SUCCEEDED(hr) && typed && std::wstring_view(typed) == L"A😀";
            SysFreeString(typed);
            return correct && name(status.Get()) == L"Edited";
        }), "Keyboard surrogate pair survives foreign text callbacks");
        key(edit, VK_RETURN);
        require(eventually([&] { return name(status.Get()) == L"Submitted"; }), "Foreign submit callback");
        key(p.window, VK_F6);
        require(eventually([&] { return name(status.Get()) == L"Keyboard"; }), "Foreign keyboard callback");
        auto scroll = find(a.Get(), root.Get(), L"scroll");
        auto scrolling = pattern<IUIAutomationScrollPattern>(scroll.Get(), UIA_ScrollPatternId);
        BOOL scrollable{}; check(scrolling->get_CurrentVerticallyScrollable(&scrollable), "Read scroll support");
        require(scrollable != FALSE, "Retained viewport scrolls");
        check(scrolling->SetScrollPercent(UIA_ScrollPatternNoScroll, 100), "Scroll retained content");
        double percent{}; check(scrolling->get_CurrentVerticalScrollPercent(&percent), "Read scroll offset");
        require(percent > 99, "Scroll offset applied");
        auto image = find(a.Get(), root.Get(), L"image");
        CONTROLTYPEID image_type{}; check(image->get_CurrentControlType(&image_type), "Read image role");
        require(image_type == UIA_ImageControlTypeId, "Native accessible image");
        if (argc > 2) {
            require(eventually([&] { key(p.window, VK_F7); return name(status.Get()) == L"Image ready"; }), "Native image decode and upload");
        }
        auto list = find(a.Get(), root.Get(), L"files");
        pattern<IUIAutomationSelectionPattern>(list.Get(), UIA_SelectionPatternId);
        require(SendMessageW(p.window, WM_APP + 60, 9, 0) == 60, "Sixty native virtual rows");
        const auto paints = SendMessageW(p.window, WM_APP + 60, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const auto settled = SendMessageW(p.window, WM_APP + 60, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        require(SendMessageW(p.window, WM_APP + 60, 0, 0) == settled && settled >= paints, "No idle repaint loop");
        if (argc > 2) { key(p.window, VK_F8); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        key(p.window, VK_F12);
        require(WaitForSingleObject(p.info.hProcess, 10000) == WAIT_OBJECT_0, "Callback close completed");
        DWORD exit{}; GetExitCodeProcess(p.info.hProcess, &exit); require(exit == 0, "Client returned success");
        require(FAILED(invoke->Invoke()), "Retained UIA provider rejects after teardown");
        std::cout << assertions << " binding UIA assertions passed (native ARM64).\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; CoUninitialize(); return 1; }
    CoUninitialize(); return 0;
}
