#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <iterator>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using Microsoft::WRL::ComPtr;
namespace {
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void check(HRESULT result, const char* text) {
    if (FAILED(result)) throw std::runtime_error(std::string(text) + " HRESULT=" + std::to_string(result));
}
bool eventually(const std::function<bool()>& predicate) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    do {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    } while (std::chrono::steady_clock::now() < limit);
    return false;
}
struct Process {
    PROCESS_INFORMATION info{};
    HWND window{};
    ~Process() {
        if (!info.hProcess) return;
        if (window && IsWindow(window)) PostMessageW(window, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(info.hProcess, 5000) == WAIT_TIMEOUT) TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
    }
};
BOOL CALLBACK find_window(HWND window, LPARAM data) {
    auto& process = *reinterpret_cast<Process*>(data);
    DWORD id{};
    GetWindowThreadProcessId(window, &id);
    wchar_t name[80]{};
    GetClassNameW(window, name, 80);
    if (id == process.info.dwProcessId && std::wstring(name) == L"Xui.Window.1") {
        process.window = window;
        return FALSE;
    }
    return TRUE;
}
ComPtr<IUIAutomationElement> named(IUIAutomation* automation, IUIAutomationElement* root, const wchar_t* name) {
    VARIANT value{};
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(name);
    ComPtr<IUIAutomationCondition> condition;
    const auto result = automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition);
    VariantClear(&value);
    check(result, "Create name condition");
    ComPtr<IUIAutomationElement> element;
    check(root->FindFirst(TreeScope_Descendants, condition.Get(), &element), "Find named control");
    return element;
}
template<class T> ComPtr<T> pattern(IUIAutomationElement* element, PATTERNID id) {
    ComPtr<T> result;
    require(element != nullptr, "Missing control");
    check(element->GetCurrentPatternAs(id, __uuidof(T), reinterpret_cast<void**>(result.GetAddressOf())), "Get pattern");
    require(result != nullptr, "Missing pattern");
    return result;
}
std::wstring name(IUIAutomationElement* element) {
    BSTR text{};
    check(element->get_CurrentName(&text), "Read name");
    const std::wstring result = text ? text : L"";
    SysFreeString(text);
    return result;
}
bool focused(IUIAutomationElement* element) {
    BOOL value{};
    return SUCCEEDED(element->get_CurrentHasKeyboardFocus(&value)) && value;
}
void focus(IUIAutomationElement* element, const char* message) {
    check(element->SetFocus(), message);
    // Require stable UIA focus before posting keyboard messages.
    auto stable = std::chrono::steady_clock::now();
    const bool arrived = eventually([&] {
        if (!focused(element)) { stable = std::chrono::steady_clock::now(); return false; }
        return std::chrono::steady_clock::now() - stable >= std::chrono::milliseconds(150);
    });
    require(arrived, message);
}
void focus_native(HWND window, HWND edit, IUIAutomationElement* element, const char* message) {
    // Exercise the host's native focus route, then query the real EDIT provider.
    SendMessageW(window, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(edit), TRUE);
    auto stable = std::chrono::steady_clock::now();
    require(eventually([&] {
        if (!focused(element)) { stable = std::chrono::steady_clock::now(); return false; }
        return std::chrono::steady_clock::now() - stable >= std::chrono::milliseconds(150);
    }), message);
}
bool enabled(IUIAutomationElement* element) {
    BOOL value{};
    check(element->get_CurrentIsEnabled(&value), "Read enabled");
    return value != FALSE;
}
HWND handle(IUIAutomation* automation, IUIAutomationElement* element) {
    UIA_HWND value{};
    check(element->get_CurrentNativeWindowHandle(&value), "Read control HWND");
    if (!value) {
        ComPtr<IUIAutomationTreeWalker> walker;
        check(automation->get_RawViewWalker(&walker), "Read raw tree walker");
        ComPtr<IUIAutomationElement> parent;
        check(walker->GetParentElement(element, &parent), "Read control host");
        require(parent != nullptr, "Control has an HWND host");
        check(parent->get_CurrentNativeWindowHandle(&value), "Read host HWND");
    }
    return reinterpret_cast<HWND>(value);
}
void key(HWND window, UINT key) {
    require(PostMessageW(window, WM_KEYDOWN, key, 0) != 0, "Post key down");
    require(PostMessageW(window, WM_KEYUP, key, 0) != 0, "Post key up");
}
void set_value(IUIAutomationValuePattern* pattern, const wchar_t* text) {
    BSTR value = SysAllocString(text);
    require(value != nullptr, "Allocate edit text");
    const auto result = pattern->SetValue(value);
    SysFreeString(value);
    check(result, "Set native text");
}
std::uint64_t cpu(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    require(GetProcessTimes(process, &creation, &exit, &kernel, &user) != 0, "Read CPU counters");
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return k.QuadPart + u.QuadPart;
}
struct ShiftState {
    DWORD thread;
    BYTE old[256]{};
    explicit ShiftState(DWORD target) : thread(target) {
        require(AttachThreadInput(GetCurrentThreadId(), thread, TRUE) != 0, "Attach test keyboard queue");
        if (!GetKeyboardState(old)) {
            AttachThreadInput(GetCurrentThreadId(), thread, FALSE);
            throw std::runtime_error("Read keyboard state");
        }
    }
    void press() {
        BYTE state[256]{};
        std::copy(std::begin(old), std::end(old), std::begin(state));
        state[VK_SHIFT] = state[VK_LSHIFT] = 0x80;
        require(SetKeyboardState(state) != 0, "Set test Shift state");
    }
    ~ShiftState() {
        SetKeyboardState(old);
        AttachThreadInput(GetCurrentThreadId(), thread, FALSE);
    }
};
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "Supply xui_gallery.exe\n"; return 1; }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 1;
    int result = 1;
    try {
        Process process;
        std::wstring command = L"\"" + std::wstring(argv[1]) + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
            nullptr, &startup, &process.info) != 0, "Start gallery");
        require(eventually([&] {
            EnumWindows(find_window, reinterpret_cast<LPARAM>(&process));
            return process.window && IsWindowVisible(process.window);
        }), "Find gallery window");
        ComPtr<IUIAutomation> automation;
        check(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&automation)), "Create automation");
        ComPtr<IUIAutomationElement> root;
        check(automation->ElementFromHandle(process.window, &root), "Read gallery root");
        ComPtr<IUIAutomationElement> save;
        require(eventually([&] { save = named(automation.Get(), root.Get(), L"Save greeting"); return save != nullptr; }),
            "Find Save button");
        auto allow = named(automation.Get(), root.Get(), L"Allow greeting updates");
        auto light = named(automation.Get(), root.Get(), L"Use light theme");
        auto greeting = named(automation.Get(), root.Get(), L"Your greeting will appear here.");
        require(allow && light && greeting, "Find gallery controls");
        CONTROLTYPEID type{};
        check(save->get_CurrentControlType(&type), "Read button role");
        require(type == UIA_ButtonControlTypeId, "Button role");
        check(allow->get_CurrentControlType(&type), "Read checkbox role");
        require(type == UIA_CheckBoxControlTypeId, "Checkbox role");
        auto invoke = pattern<IUIAutomationInvokePattern>(save.Get(), UIA_InvokePatternId);
        auto toggle = pattern<IUIAutomationTogglePattern>(allow.Get(), UIA_TogglePatternId);
        auto theme = pattern<IUIAutomationTogglePattern>(light.Get(), UIA_TogglePatternId);
        require(!enabled(save.Get()), "Save initially disabled");
        require(invoke->Invoke() == UIA_E_ELEMENTNOTENABLED, "Disabled Invoke rejected");
        ToggleState state{};
        check(toggle->get_CurrentToggleState(&state), "Read toggle state");
        require(state == ToggleState_On, "Initial checkbox state");

        const HWND edit_hwnd = FindWindowExW(process.window, nullptr, L"EDIT", nullptr);
        require(edit_hwnd != nullptr, "Find native edit");
        ComPtr<IUIAutomationElement> edit;
        check(automation->ElementFromHandle(edit_hwnd, &edit), "Read native edit provider");
        require(name(edit.Get()) == L"Your name", "Native edit accessible name");
        auto value = pattern<IUIAutomationValuePattern>(edit.Get(), UIA_ValuePatternId);
        set_value(value.Get(), L"\u65e5\u672c Alex");
        require(eventually([&] { return enabled(save.Get()); }), "Text callback enables Save");
        check(invoke->Invoke(), "Invoke Save");
        require(eventually([&] { return name(greeting.Get()) == L"Hello, \u65e5\u672c Alex!"; }), "Retained label provider updates");
        auto renamed = named(automation.Get(), root.Get(), L"Hello, \u65e5\u672c Alex!");
        BOOL same{};
        check(automation->CompareElements(greeting.Get(), renamed.Get(), &same), "Compare updated label identity");
        require(same != FALSE, "Property updates preserve accessible identity");
        require(named(automation.Get(), root.Get(), L"Greeting saved for this session.") != nullptr, "Status callback");
        const auto save_hwnd = handle(automation.Get(), save.Get());
        const auto allow_hwnd = handle(automation.Get(), allow.Get());
        const auto light_hwnd = handle(automation.Get(), light.Get());
        require(save_hwnd && allow_hwnd && light_hwnd, "Native host handles");
        focus_native(process.window, edit_hwnd, edit.Get(), "Focus native input");
        key(edit_hwnd, VK_TAB);
        const bool tabbed = eventually([&] { return focused(save.Get()); });
        require(tabbed, "Tab advances to Save");
        {
            ShiftState shift(GetWindowThreadProcessId(process.window, nullptr));
            // Attaching input queues can change their focus. Restore the target
            // before changing modifier state on the shared test queue.
            focus(save.Get(), "Restore button focus after input queue attachment");
            shift.press();
            key(save_hwnd, VK_TAB);
            require(eventually([&] { return focused(edit.Get()); }), "Shift+Tab returns to input");
        }
        focus(allow.Get(), "Focus checkbox");
        key(allow_hwnd, VK_SPACE);
        require(eventually([&] { return !enabled(save.Get()); }), "Space toggles preference and disables Save");
        require(invoke->Invoke() == UIA_E_ELEMENTNOTENABLED, "Disabled callback rejected after property update");
        focus_native(process.window, edit_hwnd, edit.Get(), "Focus input for disabled traversal");
        key(edit_hwnd, VK_TAB);
        require(eventually([&] { return focused(allow.Get()); }), "Tab skips disabled Save");
        check(toggle->Toggle(), "Toggle preference with UIA");
        require(eventually([&] { return enabled(save.Get()); }), "UIA callback enables Save");
        set_value(value.Get(), L"Keyboard");
        focus_native(process.window, edit_hwnd, edit.Get(), "Native value update settles before keyboard action");
        focus(save.Get(), "Focus button");
        key(save_hwnd, VK_RETURN);
        const auto entered = eventually([&] { return name(greeting.Get()) == L"Hello, Keyboard!"; });
        if (!entered) {
            const auto actual = name(greeting.Get());
            std::string ascii;
            for (const auto ch : actual) ascii += ch < 128 ? static_cast<char>(ch) : '?';
            std::cerr << "After Enter: " << ascii << ", button focus=" << focused(save.Get())
                << ", edit focus=" << focused(edit.Get()) << '\n';
        }
        require(entered, "Enter invokes focused button");
        set_value(value.Get(), L"Pointer");
        focus_native(process.window, edit_hwnd, edit.Get(), "Native value update settles before pointer action");
        SendMessageW(save_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
        SendMessageW(save_hwnd, WM_CANCELMODE, 0, 0);
        SendMessageW(save_hwnd, WM_LBUTTONUP, 0, MAKELPARAM(20, 20));
        require(name(greeting.Get()) == L"Hello, Keyboard!", "Cancelled pointer press must not invoke");
        SendMessageW(save_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
        SendMessageW(process.window, WM_CANCELMODE, 0, 0);
        SendMessageW(save_hwnd, WM_LBUTTONUP, 0, MAKELPARAM(20, 20));
        require(name(greeting.Get()) == L"Hello, Keyboard!", "Host cancellation must cancel child capture");
        SendMessageW(save_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
        SendMessageW(save_hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-5, -5));
        SendMessageW(save_hwnd, WM_LBUTTONUP, 0, MAKELPARAM(-5, -5));
        require(name(greeting.Get()) == L"Hello, Keyboard!", "Release outside must not invoke");
        SendMessageW(save_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
        SendMessageW(save_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
        SendMessageW(save_hwnd, WM_LBUTTONUP, 0, MAKELPARAM(20, 20));
        require(eventually([&] { return name(greeting.Get()) == L"Hello, Pointer!"; }), "Pointer release invokes");
        SendMessageW(save_hwnd, WM_MOUSELEAVE, 0, 0);
        check(theme->Toggle(), "Switch to light theme");
        require(SendMessageW(process.window, WM_APP + 60, 6, 0) == 1, "Light theme applied");
        check(theme->Toggle(), "Switch to dark theme");
        require(SendMessageW(process.window, WM_APP + 60, 6, 0) == 0, "Dark theme applied");
        BSTR text{};
        check(value->get_CurrentValue(&text), "Read preserved input");
        require(std::wstring(text) == L"Pointer", "Theme preserves text");
        SysFreeString(text);
        focus_native(process.window, edit_hwnd, edit.Get(), "Focus edit for native character input");
        SendMessageW(edit_hwnd, EM_SETSEL, 0, -1);
        for (const wchar_t ch : std::wstring(L"Native")) SendMessageW(edit_hwnd, WM_CHAR, ch, 0);
        check(invoke->Invoke(), "Save native character input");
        require(name(greeting.Get()) == L"Hello, Native!", "Native character callback");
        focus(save.Get(), "Stop native caret for idle sample");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        bool quiet{};
        for (int attempt = 0; attempt < 3 && !quiet; ++attempt) {
            const auto before = SendMessageW(process.window, WM_APP + 60, 0, 0);
            const auto cpu_before = cpu(process.info.hProcess);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            const auto delta = SendMessageW(process.window, WM_APP + 60, 0, 0) - before;
            const auto cpu_delta = cpu(process.info.hProcess) - cpu_before;
            std::cout << "Gallery idle: " << delta << " paints, " << cpu_delta / 10000.0 << " ms CPU\n";
            quiet = delta == 0;
        }
        require(quiet, "Gallery must stop custom painting at idle");
        PostMessageW(process.window, WM_CLOSE, 0, 0);
        require(WaitForSingleObject(process.info.hProcess, 5000) == WAIT_OBJECT_0, "Orderly gallery shutdown");
        DWORD exit{};
        require(GetExitCodeProcess(process.info.hProcess, &exit) != 0 && exit == 0, "Gallery successful exit");
        require(invoke->Invoke() == UIA_E_ELEMENTNOTAVAILABLE, "Retained button provider invalidated");
        require(toggle->Toggle() == UIA_E_ELEMENTNOTAVAILABLE, "Retained checkbox provider invalidated");
        BSTR unavailable{};
        const auto stale = greeting->get_CurrentName(&unavailable);
        SysFreeString(unavailable);
        require(stale == UIA_E_ELEMENTNOTAVAILABLE, "Retained label provider invalidated");
        std::cout << "Gallery smoke passed: UIA, native input, keyboard, capture, themes, idle, shutdown\n";
        result = 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    CoUninitialize();
    return result;
}
