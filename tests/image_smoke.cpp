#include "image_fixtures.hpp"
#include <UIAutomation.h>
#include <chrono>
#include <iostream>
#include <thread>

using Microsoft::WRL::ComPtr;
namespace {
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void hr(HRESULT result) { if (FAILED(result)) throw std::runtime_error("Image automation operation failed"); }
template<class F> void wait(F predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (!predicate()) {
        check(std::chrono::steady_clock::now() < deadline, "Image automation deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}
struct Process {
    PROCESS_INFORMATION info{};
    HWND window{};
    ~Process() {
        if (!info.hProcess) return;
        if (window) PostMessageW(window, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(info.hProcess, 5000) == WAIT_TIMEOUT) TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hProcess); CloseHandle(info.hThread);
    }
};
BOOL CALLBACK find(HWND hwnd, LPARAM data) {
    auto& process = *reinterpret_cast<Process*>(data);
    DWORD id{}; GetWindowThreadProcessId(hwnd, &id);
    wchar_t name[80]{}; GetClassNameW(hwnd, name, 80);
    if (id == process.info.dwProcessId && std::wstring_view(name) == L"Xui.Window.1" && IsWindowVisible(hwnd)) {
        process.window = hwnd; return FALSE;
    }
    return TRUE;
}
ComPtr<IUIAutomationElement> named(IUIAutomation* automation, IUIAutomationElement* root, const wchar_t* text, CONTROLTYPEID type = 0) {
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(text);
    ComPtr<IUIAutomationCondition> condition;
    const auto result = automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition);
    VariantClear(&value); hr(result);
    if (type) {
        value.vt = VT_I4; value.lVal = type;
        ComPtr<IUIAutomationCondition> role, combined;
        hr(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, value, &role));
        hr(automation->CreateAndCondition(condition.Get(), role.Get(), &combined));
        condition = combined;
    }
    ComPtr<IUIAutomationElement> element;
    hr(root->FindFirst(TreeScope_Descendants, condition.Get(), &element));
    return element;
}
std::wstring name(IUIAutomationElement* element) {
    BSTR text{}; hr(element->get_CurrentName(&text));
    std::wstring value = text ? text : L""; SysFreeString(text); return value;
}
template<class T> ComPtr<T> pattern(IUIAutomationElement* element, PATTERNID id) {
    check(element != nullptr, "Expected image sample control");
    ComPtr<T> result;
    hr(element->GetCurrentPatternAs(id, IID_PPV_ARGS(&result)));
    check(result != nullptr, "Expected image sample pattern");
    return result;
}
void input(IUIAutomation* automation, IUIAutomationElement* root, const wchar_t* control, const std::wstring& value) {
    auto element = named(automation, root, control, UIA_EditControlTypeId);
    auto edit = pattern<IUIAutomationValuePattern>(element.Get(), UIA_ValuePatternId);
    auto text = SysAllocString(value.c_str());
    check(text != nullptr, "Allocate automation input text");
    const auto result = edit->SetValue(text);
    SysFreeString(text);
    hr(result);
}
void invoke(IUIAutomation* automation, IUIAutomationElement* root, const wchar_t* control) {
    auto element = named(automation, root, control);
    auto action = pattern<IUIAutomationInvokePattern>(element.Get(), UIA_InvokePatternId);
    hr(action->Invoke());
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        check(argc >= 3, "Pass an executable and a project-local fixture directory");
        hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        const auto directory = std::filesystem::absolute(argv[2]);
        image_fixture::create(directory, 3);
        const bool gallery = argc > 3 && std::wstring_view(argv[3]) == L"--gallery";
        Process process;
        std::wstring command = L"\"" + std::wstring(argv[1]) + L"\"";
        if (gallery) command += L" --page images";
        if (!gallery) command += L" \"" + directory.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process.info) != 0,
            "Start image sample process");
        wait([&] { EnumWindows(find, reinterpret_cast<LPARAM>(&process)); return process.window != nullptr; });
        ComPtr<IUIAutomation> automation;
        hr(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)));
        ComPtr<IUIAutomationElement> root; hr(automation->ElementFromHandle(process.window, &root));
        if (gallery) {
            input(automation.Get(), root.Get(), L"Preview image path", (directory / L"image-0.png").wstring());
            auto image = named(automation.Get(), root.Get(), L"Workspace image preview. No image.");
            auto reveal = pattern<IUIAutomationScrollItemPattern>(image.Get(), UIA_ScrollItemPatternId);
            hr(reveal->ScrollIntoView());
            invoke(automation.Get(), root.Get(), L"Load image");
        }
        ComPtr<IUIAutomationElement> image;
        wait([&] {
            image = named(automation.Get(), root.Get(), gallery ? L"Workspace image preview" : L"image-0.png");
            return image != nullptr;
        });
        CONTROLTYPEID type{}; hr(image->get_CurrentControlType(&type));
        check(type == UIA_ImageControlTypeId, "The public Image has the semantic image role");
        BOOL focusable{}; hr(image->get_CurrentIsKeyboardFocusable(&focusable));
        check(!focusable && image->SetFocus() == UIA_E_INVALIDOPERATION, "Read-only images expose no false focus action");
        if (gallery) {
            input(automation.Get(), root.Get(), L"Preview image path", (directory / L"corrupt.png").wstring());
            invoke(automation.Get(), root.Get(), L"Load image");
            wait([&] { return name(image.Get()).find(L"Cannot decode") != std::wstring::npos; });
            invoke(automation.Get(), root.Get(), L"Unload image");
            wait([&] { return name(image.Get()) == L"Workspace image preview. No image."; });
        } else {
            auto viewport = named(automation.Get(), root.Get(), L"Image thumbnails");
            check(viewport != nullptr, "Find the image viewport");
            hr(viewport->SetFocus());
            auto scroll = pattern<IUIAutomationScrollPattern>(viewport.Get(), UIA_ScrollPatternId);
            BOOL enabled{}; hr(scroll->get_CurrentVerticallyScrollable(&enabled));
            check(!enabled, "A short folder does not advertise useless scrolling");
            wait([&] { return named(automation.Get(), root.Get(), L"corrupt.png. Cannot decode the image file.") != nullptr; });
            input(automation.Get(), root.Get(), L"Image folder", (directory / L"missing-folder").wstring());
            invoke(automation.Get(), root.Get(), L"Open folder");
            wait([&] { return named(automation.Get(), root.Get(), L"View update failed: Cannot open the image folder.") != nullptr; });
            input(automation.Get(), root.Get(), L"Image folder", directory.wstring());
            invoke(automation.Get(), root.Get(), L"Open folder");
            wait([&] { return named(automation.Get(), root.Get(), L"image-0.png") != nullptr; });
            invoke(automation.Get(), root.Get(), L"Unload");
            wait([&] { return named(automation.Get(), root.Get(), L"Enter a folder path to load images.") != nullptr; });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto paints = SendMessageW(process.window, WM_APP + 60, 0, 0);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        check(paints == SendMessageW(process.window, WM_APP + 60, 0, 0), "Sample image controls are idle after unload");
        PostMessageW(process.window, WM_CLOSE, 0, 0);
        check(WaitForSingleObject(process.info.hProcess, 5000) == WAIT_OBJECT_0, "Image sample closes promptly");
        DWORD result{}; GetExitCodeProcess(process.info.hProcess, &result);
        check(result == 0, "Image sample returns success");
        BSTR closed{}; const auto stale = image->get_CurrentName(&closed); SysFreeString(closed);
        check(stale == UIA_E_ELEMENTNOTAVAILABLE, "Retained image provider disconnects at window close");
        std::cout << (gallery ? "Gallery image" : "Thumbnail sample") << " UIA, file errors, unloading, idle and shutdown passed\n";
        CoUninitialize();
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
