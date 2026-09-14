#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using Microsoft::WRL::ComPtr;

namespace {
void check(HRESULT result, const char* message) {
    if (FAILED(result)) throw std::runtime_error(message);
}

struct ComScope {
    ComScope() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Cannot initialize UI Automation."); }
    ~ComScope() { CoUninitialize(); }
};

HWND find_window(DWORD process) {
    struct Search { DWORD process; HWND window{}; } search{process};
    EnumWindows([](HWND window, LPARAM context) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(context);
        DWORD process{};
        GetWindowThreadProcessId(window, &process);
        wchar_t name[64]{};
        GetClassNameW(window, name, 64);
        if (process == search.process && IsWindowVisible(window) &&
            std::wstring_view(name) == L"Xui.Window.1") {
            search.window = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    if (!search.window) throw std::runtime_error("The process has no visible XUI window.");
    return search.window;
}

ComPtr<IUIAutomationElement> find_control(IUIAutomation* automation, IUIAutomationElement* root,
                                        const wchar_t* id) {
    VARIANT value{};
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(id);
    if (!value.bstrVal) throw std::bad_alloc{};
    ComPtr<IUIAutomationCondition> condition;
    const auto result = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
    VariantClear(&value);
    check(result, "Cannot create the automation-ID condition.");
    ComPtr<IUIAutomationElement> control;
    check(root->FindFirst(TreeScope_Descendants, condition.Get(), &control), "Cannot find the control.");
    if (!control) throw std::runtime_error("The automation ID does not exist.");
    return control;
}

template<class T> ComPtr<T> pattern(IUIAutomationElement* control, PATTERNID id) {
    ComPtr<T> result;
    check(control->GetCurrentPatternAs(id, __uuidof(T),
        reinterpret_cast<void**>(result.GetAddressOf())), "The control does not expose the requested pattern.");
    if (!result) throw std::runtime_error("The requested pattern is null.");
    return result;
}

std::string utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!length) throw std::runtime_error("Cannot encode the control text.");
    std::string result(static_cast<std::size_t>(length), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), length, nullptr, nullptr))
        throw std::runtime_error("Cannot encode the control text.");
    return result;
}

void print_text(BSTR value) {
    const std::wstring text(value ? value : L"", value ? SysStringLen(value) : 0);
    SysFreeString(value);
    std::cout << utf8(text) << '\n';
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::cerr << "Usage: xui_language_probe PID window|close|name|bounds|invoke|value|set-value [automation-id] [text]\n";
        return 2;
    }
    try {
        std::size_t consumed{};
        const auto process = std::stoul(argv[1], &consumed);
        if (!process || consumed != std::wstring_view(argv[1]).size())
            throw std::runtime_error("Invalid process ID.");
        const auto window = find_window(process);
        const std::wstring_view command(argv[2]);
        if (command == L"window") {
            std::cout << reinterpret_cast<std::uintptr_t>(window) << '\n';
            return 0;
        }
        if (command == L"close") {
            if (!PostMessageW(window, WM_CLOSE, 0, 0)) throw std::runtime_error("Cannot close the window.");
            return 0;
        }
        if (argc < 4) throw std::runtime_error("An automation ID is required.");
        ComScope com;
        ComPtr<IUIAutomation> automation;
        check(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&automation)), "Cannot create UI Automation.");
        ComPtr<IUIAutomationElement> root;
        check(automation->ElementFromHandle(window, &root), "Cannot read the window.");
        const auto control = find_control(automation.Get(), root.Get(), argv[3]);
        if (command == L"name") {
            BSTR value{};
            check(control->get_CurrentName(&value), "Cannot read the control name.");
            print_text(value);
        } else if (command == L"bounds") {
            RECT bounds{};
            check(control->get_CurrentBoundingRectangle(&bounds), "Cannot read the control bounds.");
            std::cout << "{\"left\":" << bounds.left << ",\"top\":" << bounds.top
                << ",\"width\":" << bounds.right - bounds.left
                << ",\"height\":" << bounds.bottom - bounds.top << "}\n";
        } else if (command == L"invoke") {
            check(pattern<IUIAutomationInvokePattern>(control.Get(), UIA_InvokePatternId)->Invoke(),
                "Cannot invoke the control.");
        } else if (command == L"value") {
            BSTR value{};
            check(pattern<IUIAutomationValuePattern>(control.Get(), UIA_ValuePatternId)->get_CurrentValue(&value),
                "Cannot read the control value.");
            print_text(value);
        } else if (command == L"set-value") {
            if (argc != 5) throw std::runtime_error("A replacement value is required.");
            check(pattern<IUIAutomationValuePattern>(control.Get(), UIA_ValuePatternId)->SetValue(argv[4]),
                "Cannot set the control value.");
        } else {
            throw std::runtime_error("Unknown probe command.");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
