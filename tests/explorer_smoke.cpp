#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include "uia_events.hpp"
#include "suggestion_capture.hpp"
#include "environment_fixture.hpp"

using Microsoft::WRL::ComPtr;
namespace {
int assertions{};
void require(bool result, const char* message) { ++assertions; if (!result) throw std::runtime_error(message); }
void check(HRESULT result, const char* message) { require(SUCCEEDED(result), message); }
HRESULT set_value(IUIAutomationValuePattern* pattern, const wchar_t* text) {
    auto value = SysAllocString(text);
    if (!value) return E_OUTOFMEMORY;
    const auto result = pattern->SetValue(value);
    SysFreeString(value);
    return result;
}
bool wait(const std::function<bool()>& test, int ms = 8000) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do { if (test()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    while (std::chrono::steady_clock::now() < limit);
    return false;
}
HWND native(HWND root, const wchar_t* cls, int ordinal = 0) {
    struct Search { const wchar_t* cls; int index; HWND result{}; } search{cls, ordinal};
    EnumChildWindows(root, [](HWND hwnd, LPARAM data) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(data);
        wchar_t name[128]{}; GetClassNameW(hwnd, name, 128);
        if (_wcsicmp(name, s.cls) == 0 && s.index-- == 0) { s.result = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
void press(HWND owner, WORD key, bool control = false, bool shift = false) {
    require(GetForegroundWindow() == owner, "Keyboard test owns foreground; no input sent to another application");
    std::vector<INPUT> input;
    const auto add = [&](WORD code, bool up) { INPUT i{}; i.type = INPUT_KEYBOARD; i.ki.wVk = code; i.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0; input.push_back(i); };
    if (control) add(VK_CONTROL, false);
    if (shift) add(VK_SHIFT, false);
    add(key, false); add(key, true);
    if (shift) add(VK_SHIFT, true);
    if (control) add(VK_CONTROL, true);
    require(SendInput(static_cast<UINT>(input.size()), input.data(), sizeof(INPUT)) == input.size(), "Send owned keyboard gesture");
}
HWND popup_for(DWORD process) {
    struct Search { DWORD process; HWND popup{}; } search{process};
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(data);
        DWORD process{}; GetWindowThreadProcessId(hwnd, &process);
        wchar_t cls[64]{}; GetClassNameW(hwnd, cls, 64);
        if (process == s.process && std::wstring_view(cls) == L"#32768") { s.popup = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.popup;
}
HWND suggestions_for(DWORD process) {
    struct Search { DWORD process; HWND popup{}; } search{process};
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(data);
        DWORD process{}; GetWindowThreadProcessId(hwnd, &process);
        wchar_t cls[64]{}; GetClassNameW(hwnd, cls, 64);
        if (process == s.process && std::wstring_view(cls) == L"Xui.Suggestions.1" && IsWindowVisible(hwnd)) {
            s.popup = hwnd; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.popup;
}
std::wstring name(IUIAutomationElement* element) {
    BSTR text{}; check(element->get_CurrentName(&text), "Read UIA name");
    std::wstring result = text ? text : L""; SysFreeString(text); return result;
}
ComPtr<IUIAutomationElement> find(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationCondition> condition;
    const auto result = uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
    VariantClear(&value); check(result, "Create semantic condition");
    ComPtr<IUIAutomationElement> element;
    check(root->FindFirst(TreeScope_Descendants, condition.Get(), &element), "Find semantic control");
    require(element != nullptr, "Required semantic control exists"); return element;
}
template<class T> ComPtr<T> pattern(IUIAutomationElement* element, PATTERNID id) {
    ComPtr<T> result;
    check(element->GetCurrentPatternAs(id, __uuidof(T), reinterpret_cast<void**>(result.GetAddressOf())), "Get UIA pattern");
    require(result != nullptr, "Pattern exists"); return result;
}
struct Process {
    PROCESS_INFORMATION info{}; HWND window{};
    ~Process() {
        if (!info.hProcess) return;
        if (window) PostMessageW(window, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(info.hProcess, 5000) == WAIT_TIMEOUT) TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hThread); CloseHandle(info.hProcess);
    }
};
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    bool global_focus_events{}, capture_suggestions{}, capture_header{}, winui{};
    for (int i = 2; i < argc; ++i) {
        const std::wstring_view argument(argv[i]);
        if (argument == L"--global-focus-events") global_focus_events = true;
        else if (argument == L"--suggestion-capture") capture_suggestions = true;
        else if (argument == L"--header-capture") capture_header = true;
        else if (argument == L"--style=winui") winui = true;
        else { std::cerr << "Unknown explorer smoke option\n"; return 2; }
    }
    const auto captures = std::filesystem::absolute(argv[1]).parent_path().parent_path() / L"captures";
    const auto folder = std::filesystem::current_path() / (L"explorer-desktop-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{folder};
    try {
        require(std::filesystem::create_directory(folder), "Create fixture directory");
        for (const auto& child : {L"alpha", L"beta", L"日本-🙂"}) std::filesystem::create_directory(folder / child);
        for (int i = 0; i < 60; ++i) { std::ofstream file(folder / (L"file-" + std::to_wstring(i) + L".txt")); file << "fixture"; }
        { std::ofstream file(folder / L"alpha" / L"alpha-only.txt"); file << "fixture"; }
        { std::ofstream file(folder / L"beta" / L"beta-only.txt"); file << "fixture"; }
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Initialize client COM");
        struct Uninit { ~Uninit() { CoUninitialize(); } } uninit;
        Process process;
        EnvironmentFixture environment(folder.c_str()), missing_environment(nullptr);
        std::wstring command = L"\"" + std::filesystem::absolute(argv[1]).wstring() + L"\" \"" + folder.wstring() + L"\"";
        if (winui) command += L" --style=winui";
        STARTUPINFOW startup{sizeof(startup)};
        require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process.info), "Launch explorer");
        require(wait([&] {
            EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
                auto& p = *reinterpret_cast<Process*>(data);
                DWORD id{}; GetWindowThreadProcessId(hwnd, &id);
                wchar_t cls[64]{}; GetClassNameW(hwnd, cls, 64);
                if (id == p.info.dwProcessId && std::wstring_view(cls) == L"Xui.Window.1") { p.window = hwnd; return FALSE; }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&process));
            return process.window && SendMessageW(process.window, WM_APP + 60, 9, 0) == 63;
        }), "Initial enumeration");
        ComPtr<IUIAutomation> uia;
        check(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia)), "Create UIA client");
        ComPtr<IUIAutomationElement> root;
        check(uia->ElementFromHandle(process.window, &root), "Read application tree");
        uia_test::Subscription subscription{uia};
        ComPtr<uia_test::Events> events;
        events.Attach(new uia_test::Events(process.info.dwProcessId));
        ComPtr<IUIAutomationCacheRequest> cache;
        check(uia->CreateCacheRequest(&cache), "Create event cache");
        check(cache->AddProperty(UIA_ProcessIdPropertyId), "Cache event process");
        check(cache->AddProperty(UIA_NamePropertyId), "Cache event name");
        check(uia->AddAutomationEventHandler(UIA_SelectionItem_ElementSelectedEventId, root.Get(),
            TreeScope_Subtree, cache.Get(), events.Get()), "Subscribe to selection events");
        check(uia->AddStructureChangedEventHandler(root.Get(), TreeScope_Subtree, cache.Get(), events.Get()), "Subscribe to structure events");
        PROPERTYID focus_property[]{UIA_HasKeyboardFocusPropertyId};
        check(uia->AddPropertyChangedEventHandlerNativeArray(root.Get(), TreeScope_Subtree,
            cache.Get(), events.Get(), focus_property, 1), "Subscribe to scoped focus property events");
        if (global_focus_events)
            check(uia->AddFocusChangedEventHandler(cache.Get(), events.Get()), "Subscribe to desktop-wide focus events");
        auto address = find(uia.Get(), root.Get(), L"browser-address");
        auto search = find(uia.Get(), root.Get(), L"browser-search");
        auto list = find(uia.Get(), root.Get(), L"browser-files");
        auto tabs = find(uia.Get(), root.Get(), L"browser-tabs");
        auto status = find(uia.Get(), root.Get(), L"browser-status");
        auto address_value = pattern<IUIAutomationValuePattern>(address.Get(), UIA_ValuePatternId);
        auto search_value = pattern<IUIAutomationValuePattern>(search.Get(), UIA_ValuePatternId);
        auto tab_selection = pattern<IUIAutomationSelectionPattern>(tabs.Get(), UIA_SelectionPatternId);
        const auto title = [&] {
            wchar_t text[32768]{};
            GetWindowTextW(process.window, text, 32768);
            return std::wstring(text);
        };
        const auto title_is = [&](const std::filesystem::path& path) { return title() == L"XUI/Files - " + path.wstring(); };
        require(title_is(folder), "Startup title identifies the committed folder");
        require(name(address.Get()) == L"Folder address", "Captionless native EDIT retains its accessible name");
        ComPtr<IUIAutomationTextPattern> address_text, search_text;
        check(address->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&address_text)), "Read address native TextPattern support");
        check(search->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&search_text)), "Read search native TextPattern support");
        require(bool(address_text) == bool(search_text), "Compact address preserves the platform EDIT TextPattern support");
        std::cout << "Native EDIT TextPattern available=" << bool(address_text) << "; editable ValuePattern available=1\n";
        const auto bounds_of = [&](const wchar_t* id) {
            auto element = find(uia.Get(), root.Get(), id);
            RECT bounds{}; check(element->get_CurrentBoundingRectangle(&bounds), "Read header bounds");
            return bounds;
        };
        const auto header_bounds = [&](float dpi_scale, bool expanded) {
            const auto tab = bounds_of(L"browser-tabs");
            for (const auto* id : {L"browser-new-tab", L"browser-split", L"browser-theme"}) {
                const auto b = bounds_of(id);
                require(std::abs(b.top - tab.top) <= 1 && std::abs(b.bottom - tab.bottom) <= 1 &&
                    b.left >= tab.right, "Tab commands share one band without overlap");
            }
            for (int side = 0; side < (expanded ? 2 : 1); ++side) {
                const auto address = bounds_of(side ? L"browser-right-address" : L"browser-address");
                const auto pane_tab = bounds_of(side ? L"browser-right-tabs" : L"browser-tabs");
                require(pane_tab.top == tab.top && pane_tab.bottom == tab.bottom, "Independent pane tab rows align");
                require(address.right - address.left >= static_cast<LONG>(100 * dpi_scale), "Narrow pane retains readable native address width");
                for (const auto* action : {L"back", L"forward", L"up", L"refresh"}) {
                    const auto id = std::wstring(side ? L"browser-right-" : L"browser-") + action;
                    const auto b = bounds_of(id.c_str());
                    // WinUI's 6/7-DIP field insets raise the native text center by half a DIP.
                    const float optical_offset = winui ? dpi_scale : 0;
                    require(std::abs((b.top + b.bottom) - (address.top + address.bottom) - optical_offset) <= 2 &&
                        b.right <= address.left && b.top > tab.bottom, "Navigation icons and native address share one band");
                }
            }
            bool noise{};
            EnumChildWindows(process.window, [](HWND hwnd, LPARAM data) -> BOOL {
                if (!IsWindowVisible(hwnd)) return TRUE;
                wchar_t text[256]{}; GetWindowTextW(hwnd, text, 256);
                const std::wstring_view value(text);
                if (value == L"XUI  /  FILES" || value.starts_with(L"LEFT  /") || value.starts_with(L"RIGHT  /") ||
                    value == L"Folder address" || value == L"Right folder address") *reinterpret_cast<bool*>(data) = true;
                return TRUE;
            }, reinterpret_cast<LPARAM>(&noise));
            require(!noise, "Brand, active labels and visible address captions are absent");
        };
        header_bounds(GetDpiForWindow(process.window) / 96.0f, false);
        if (capture_header) suggestion_capture::bitmap(process.window, nullptr, captures / L"single-dark.bmp");
        const auto peer_count = SendMessageW(process.window, WM_APP + 60, 14, 0);
        CONTROLTYPEID type{};
        check(tabs->get_CurrentControlType(&type), "Read tab role");
        require(type == UIA_TabControlTypeId, "TabStrip exposes Tab");
        const auto invoke = [&](const wchar_t* id) {
            auto target = find(uia.Get(), root.Get(), id);
            auto action = pattern<IUIAutomationInvokePattern>(target.Get(), UIA_InvokePatternId);
            check(action->Invoke(), "Invoke command");
        };
        const auto tab_items = [&] {
            ComPtr<IUIAutomationCondition> condition;
            VARIANT expected{}; expected.vt = VT_I4; expected.lVal = UIA_TabItemControlTypeId;
            check(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, expected, &condition), "Create tab role condition");
            ComPtr<IUIAutomationElementArray> items;
            check(tabs->FindAll(TreeScope_Children, condition.Get(), &items), "Read tabs");
            return items;
        };
        const auto count_tabs = [&] { auto items = tab_items(); int count{}; check(items->get_Length(&count), "Count tabs"); return count; };
        const auto current_path = [&] {
            BSTR text{}; check(address_value->get_CurrentValue(&text), "Read address");
            std::wstring result = text ? text : L""; SysFreeString(text); return result;
        };
        const auto settled = [&] { return SendMessageW(process.window, WM_APP + 60, 4, 0) == 0; };
        const auto go = [&](const std::filesystem::path& path, bool success = true) {
            check(address->SetFocus(), "Focus the address before a navigation gesture");
            require(wait([&] { return settled() && title_is(current_path()); }), "Prior committed title settles before typed address");
            const auto generation = SendMessageW(process.window, WM_APP + 60, 3, 0);
            const auto committed_title = title();
            check(set_value(address_value.Get(), path.c_str()), "Enter address");
            require(title() == committed_title, "Typing an address does not change the committed title");
            PostMessageW(native(process.window, L"EDIT"), WM_KEYDOWN, VK_RETURN, 0);
            const auto completed = wait([&] { return SendMessageW(process.window, WM_APP + 60, 3, 0) > generation && settled() &&
                (success ? current_path() == path.wstring() : current_path() != path.wstring()); });
            if (!completed) std::wcerr << L"navigation path=" << path.wstring() << L" actual=" << current_path() <<
                L" generation=" << generation << L" -> " << SendMessageW(process.window, WM_APP + 60, 3, 0) <<
                L" status=" << name(status.Get()) << L'\n';
            require(completed, "Address Enter completes latest folder request");
            require(wait([&] { return success ? title_is(path) : title() == committed_title; }),
                "Only successful navigation changes the window title");
        };
        const auto go_environment = [&](const std::wstring& input, const std::filesystem::path& expected) {
            check(address->SetFocus(), "Focus address for environment input");
            const auto generation = SendMessageW(process.window, WM_APP + 60, 3, 0);
            check(set_value(address_value.Get(), input.c_str()), "Enter environment address through UIA");
            PostMessageW(native(process.window, L"EDIT"), WM_KEYDOWN, VK_RETURN, 0);
            require(wait([&] { return settled() && current_path() == expected.wstring() && title_is(expected); }),
                "Environment address commits its expanded path and caption");
            require(SendMessageW(process.window, WM_APP + 60, 3, 0) == generation + 1,
                "Environment address submits exactly one request");
        };
        wchar_t windows_path[32768]{};
        require(GetWindowsDirectoryW(windows_path, 32768) != 0, "Read Windows path without changing the environment");
        go_environment(L"%SystemRoot%\\System32", std::filesystem::path(windows_path) / L"System32");
        go(folder);
        go_environment(L"\"" + environment.reference() + L"\\日本-🙂\"", folder / L"日本-🙂");
        go(folder);
        const auto invalid_generation = SendMessageW(process.window, WM_APP + 60, 3, 0);
        check(set_value(address_value.Get(), missing_environment.reference().c_str()), "Enter unknown environment variable");
        PostMessageW(native(process.window, L"EDIT"), WM_KEYDOWN, VK_RETURN, 0);
        require(wait([&] { return name(status.Get()).starts_with(L"Unknown environment variable:"); }),
            "Unknown environment variable reports a visible nonfatal error");
        require(SendMessageW(process.window, WM_APP + 60, 3, 0) == invalid_generation && title_is(folder),
            "Unknown reference starts no scan and preserves the committed caption");
        check(set_value(address_value.Get(), folder.c_str()), "Restore address after invalid environment input");
        const auto suggestion_list = [&] {
            HWND popup{}, list{};
            const bool ready = wait([&] {
                popup = suggestions_for(process.info.dwProcessId);
                list = popup ? FindWindowExW(popup, nullptr, L"LISTBOX", nullptr) : nullptr;
                return list && SendMessageW(list, LB_GETCOUNT, 0, 0) > 0;
            });
            if (!ready) {
                wchar_t message[256]{};
                if (popup) GetWindowTextW(FindWindowExW(popup, nullptr, L"STATIC", nullptr), message, 256);
                GUITHREADINFO info{sizeof(info)};
                GetGUIThreadInfo(GetWindowThreadProcessId(process.window, nullptr), &info);
                wchar_t focus_class[128]{}, focus_text[128]{};
                GetClassNameW(info.hwndFocus, focus_class, 128); GetWindowTextW(info.hwndFocus, focus_text, 128);
                std::wcerr << L"popup=" << popup << L" status=" << message << L" focus=" << info.hwndFocus <<
                    L" class=" << focus_class << L" text=" << focus_text << L" left_edit=" << native(process.window, L"EDIT") <<
                    L" right_edit=" << native(process.window, L"EDIT", 2) << L'\n';
            }
            require(ready, "Native autosuggest list has matching folders");
            return list;
        };
        if (capture_suggestions) suggestion_capture::memory(process.info.hProcess, "suggestions_unfocused");
        check(list->SetFocus(), "Focus left pane before address suggestions");
        require(wait([&] { return GetForegroundWindow() == process.window; }), "Explorer owns suggestion keyboard input");
        press(process.window, 'L', true);
        BOOL address_focused{};
        require(wait([&] { return SUCCEEDED(address->get_CurrentHasKeyboardFocus(&address_focused)) && address_focused; }),
            "Native left address reports keyboard focus");
        std::cout << "Left address suggestions\n";
        const auto cold_popup_start = std::chrono::steady_clock::now();
        check(set_value(address_value.Get(), (environment.reference() + L"\\al").c_str()), "Type environment folder prefix");
        require(title_is(folder), "Autosuggest prefix does not replace the window title");
        const auto suggestions = suggestion_list();
        std::cout << "browser_cold_popup_ms=" << std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - cold_popup_start).count() << '\n';
        ComPtr<IUIAutomationElement> suggestion_element;
        check(uia->ElementFromHandle(suggestions, &suggestion_element), "Native popup has UIA provider");
        check(suggestion_element->get_CurrentControlType(&type), "Read native suggestion role");
        require(type == UIA_ListControlTypeId, "Suggestions expose native UIA List");
        auto suggestion_selection = pattern<IUIAutomationSelectionPattern>(suggestion_element.Get(), UIA_SelectionPatternId);
        ComPtr<IUIAutomationCondition> all_suggestions;
        check(uia->CreateTrueCondition(&all_suggestions), "Create suggestion item condition");
        ComPtr<IUIAutomationElement> candidate;
        check(suggestion_element->FindFirst(TreeScope_Children, all_suggestions.Get(), &candidate), "Find native folder candidate");
        require(candidate != nullptr && name(candidate.Get()) == (folder / L"alpha").wstring(), "UIA candidate has complete Unicode-capable path name");
        auto candidate_selection = pattern<IUIAutomationSelectionItemPattern>(candidate.Get(), UIA_SelectionItemPatternId);
        check(candidate_selection->Select(), "Select a native suggestion through UIA");
        BOOL candidate_selected{};
        check(candidate_selection->get_CurrentIsSelected(&candidate_selected), "Read native candidate selection");
        require(candidate_selected != FALSE, "UIA suggestion selection is published");
        if (capture_suggestions) {
            suggestion_capture::memory(process.info.hProcess, "suggestions_open");
            suggestion_capture::bitmap(process.window, suggestions_for(process.info.dwProcessId), L"build\\autosuggest\\browser-dark.bmp");
            const auto paints = SendMessageW(process.window, WM_APP + 60, 0, 0);
            press(process.window, VK_F6, true);
            require(wait([&] { return SendMessageW(process.window, WM_APP + 60, 0, 0) > paints; }), "Light popup theme repaint");
            suggestion_capture::bitmap(process.window, suggestions_for(process.info.dwProcessId), L"build\\autosuggest\\browser-light.bmp");
            const auto light_paints = SendMessageW(process.window, WM_APP + 60, 0, 0);
            press(process.window, VK_F6, true);
            require(wait([&] { return SendMessageW(process.window, WM_APP + 60, 0, 0) > light_paints; }), "Dark popup theme repaint");
            require(suggestions_for(process.info.dwProcessId) && SendMessageW(suggestions, LB_GETCURSEL, 0, 0) == 0,
                "Theme capture preserves suggestion selection");
        }
        const auto initial_generation = SendMessageW(process.window, WM_APP + 60, 3, 0);
        PostMessageW(native(process.window, L"EDIT"), WM_KEYDOWN, VK_RETURN, 0);
        require(wait([&] { return settled() && current_path() == (folder / L"alpha").wstring() &&
            SendMessageW(process.window, WM_APP + 60, 3, 0) > initial_generation; }), "Selected suggestion Enter navigates");
        require(SendMessageW(process.window, WM_APP + 60, 3, 0) == initial_generation + 1, "Suggestion Enter starts exactly one navigation");
        go(folder);
        press(process.window, 'L', true);
        require(wait([&] { return SUCCEEDED(address->get_CurrentHasKeyboardFocus(&address_focused)) && address_focused; }),
            "Left address refocus completes");
        check(set_value(address_value.Get(), L"al"), "Type prefix before Escape");
        suggestion_list();
        PostMessageW(native(process.window, L"EDIT"), WM_KEYDOWN, VK_ESCAPE, 0);
        require(wait([&] { return !suggestions_for(process.info.dwProcessId); }) && current_path() == L"al",
            "Suggestion Escape hides dropdown without browser path restore");
        PostMessageW(native(process.window, L"EDIT"), WM_KEYDOWN, VK_ESCAPE, 0);
        require(wait([&] { return current_path() == folder.wstring(); }), "Second Escape restores browser address");
        check(set_value(address_value.Get(), L"al"), "Type prefix for accessible default action");
        const auto invoke_list = suggestion_list();
        ComPtr<IUIAutomationElement> invoke_element, invoke_candidate;
        check(uia->ElementFromHandle(invoke_list, &invoke_element), "Read list for accessible default action");
        check(invoke_element->FindFirst(TreeScope_Children, all_suggestions.Get(), &invoke_candidate), "Read candidate for default action");
        auto default_action = pattern<IUIAutomationLegacyIAccessiblePattern>(invoke_candidate.Get(), UIA_LegacyIAccessiblePatternId);
        const auto default_generation = SendMessageW(process.window, WM_APP + 60, 3, 0);
        check(default_action->DoDefaultAction(), "Invoke native candidate default action");
        require(wait([&] { return settled() && current_path() == (folder / L"alpha").wstring(); }), "Accessible default action navigates selected folder");
        require(SendMessageW(process.window, WM_APP + 60, 3, 0) == default_generation + 1, "Accessible default action starts one navigation");
        go(folder);
        go(folder / L"alpha");
        require(SendMessageW(process.window, WM_APP + 60, 9, 0) == 1, "Address navigation updates list");
        invoke(L"browser-back");
        require(wait([&] { return settled() && current_path() == folder.wstring(); }), "Back restores location");
        require(wait([&] { return title_is(folder); }), "Back updates window title");
        invoke(L"browser-forward");
        require(wait([&] { return settled() && current_path() == (folder / L"alpha").wstring(); }), "Forward restores location");
        require(wait([&] { return title_is(folder / L"alpha"); }), "Forward updates window title");
        invoke(L"browser-up");
        require(wait([&] { return settled() && current_path() == folder.wstring(); }), "Up returns to parent");
        const auto list_hwnd = native(process.window, L"Xui.FileList.1");
        const auto scale = GetDpiForWindow(process.window) / 96.0f;
        SendMessageW(list_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(static_cast<int>(20 * scale), static_cast<int>(16 * scale)));
        SendMessageW(list_hwnd, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(static_cast<int>(20 * scale), static_cast<int>(16 * scale)));
        require(wait([&] { return settled() && current_path() == (folder / L"alpha").wstring(); }), "Folder double-click navigates");
        invoke(L"browser-back");
        require(wait([&] { return settled() && current_path() == folder.wstring(); }), "Back after activation");
        SendMessageW(list_hwnd, WM_KEYDOWN, VK_RETURN, 0);
        require(wait([&] { return settled() && current_path() == (folder / L"alpha").wstring(); }), "Enter uses same folder activation");
        go(folder);
        check(set_value(search_value.Get(), L"alpha"), "Filter before tab switch");
        require(wait([&] { return settled() && SendMessageW(process.window, WM_APP + 60, 8, 0) == 1; }), "Filter applied");
        invoke(L"browser-new-tab");
        require(wait([&] { return settled() && count_tabs() == 2; }), "Dynamic new tab");
        require(SendMessageW(process.window, WM_APP + 60, 14, 0) == peer_count, "New tabs allocate no native control trees");
        require(wait([&] { return events->count(UIA_StructureChangedEventId) > 0 &&
            events->count(UIA_SelectionItem_ElementSelectedEventId) > 0; }), "Tab changes publish structure and selection events");
        auto items = tab_items();
        ComPtr<IUIAutomationElement> first_tab, second_tab;
        check(items->GetElement(0, &first_tab), "Get first tab");
        check(items->GetElement(1, &second_tab), "Get second tab");
        auto first_select = pattern<IUIAutomationSelectionItemPattern>(first_tab.Get(), UIA_SelectionItemPatternId);
        auto second_select = pattern<IUIAutomationSelectionItemPattern>(second_tab.Get(), UIA_SelectionItemPatternId);
        go(folder / L"beta");
        check(first_select->Select(), "Select first tab");
        require(wait([&] { return settled() && current_path() == folder.wstring() &&
            SendMessageW(process.window, WM_APP + 60, 8, 0) == 1; }), "Tab restores its independent path and filter");
        require(wait([&] { return title_is(folder); }), "Tab switch updates committed title");
        check(second_select->Select(), "Select second tab");
        require(wait([&] { return settled() && current_path() == (folder / L"beta").wstring(); }), "Other tab preserves its history");
        require(wait([&] { return title_is(folder / L"beta"); }), "Second tab restores its title");
        go(folder / L"missing", false);
        require(wait([&] { return name(status.Get()).find(L"Cannot read folder") != std::wstring::npos; }), "Invalid address reports specific error");
        require(current_path() == (folder / L"beta").wstring() && SendMessageW(process.window, WM_APP + 60, 9, 0) == 1,
            "Failed navigation preserves prior view and address");
        check(set_value(search_value.Get(), L"beta"), "Filter after failed navigation");
        require(wait([&] { return settled() && SendMessageW(process.window, WM_APP + 60, 8, 0) == 1 &&
            name(status.Get()).find(L"Cannot") == std::wstring::npos; }), "Filter recovers against last valid folder");
        invoke(L"browser-split");
        auto right_address = find(uia.Get(), root.Get(), L"browser-right-address");
        auto right_value = pattern<IUIAutomationValuePattern>(right_address.Get(), UIA_ValuePatternId);
        auto right_suggestion_list = find(uia.Get(), root.Get(), L"browser-right-files");
        check(right_suggestion_list->SetFocus(), "Focus right pane before independent suggestions");
        press(process.window, 'L', true);
        require(wait([&] { return SUCCEEDED(right_address->get_CurrentHasKeyboardFocus(&address_focused)) && address_focused; }),
            "Right address focus completes");
        std::cout << "Right address suggestions\n";
        check(set_value(right_value.Get(), L"日本"), "Type right Unicode prefix");
        const auto right_suggestions = suggestion_list();
        require(SendMessageW(right_suggestions, LB_GETCOUNT, 0, 0) == 1, "Right pane suggests against its own current folder");
        if (capture_suggestions)
            suggestion_capture::bitmap(process.window, suggestions_for(process.info.dwProcessId), L"build\\autosuggest\\browser-right.bmp");
        PostMessageW(native(process.window, L"EDIT", 2), WM_KEYDOWN, VK_DOWN, 0);
        PostMessageW(native(process.window, L"EDIT", 2), WM_KEYDOWN, VK_TAB, 0);
        require(wait([&] {
            BSTR value{}; const auto hr = right_value->get_CurrentValue(&value);
            const bool same = SUCCEEDED(hr) && value && std::wstring_view(value) == (folder / L"日本-🙂").wstring();
            SysFreeString(value); return same && !suggestions_for(process.info.dwProcessId);
        }), "Right Tab accepts Unicode folder without navigation");
        check(set_value(right_value.Get(), (folder / L"日本-🙂").c_str()), "Unicode right-pane address");
        PostMessageW(native(process.window, L"EDIT", 2), WM_KEYDOWN, VK_RETURN, 0);
        auto right_status = find(uia.Get(), root.Get(), L"browser-right-status");
        require(wait([&] { return name(right_status.Get()).starts_with(L"0 of 0"); }), "Independent second pane scans Unicode location");
        require(current_path() == (folder / L"beta").wstring(), "Right navigation leaves left pane unchanged");
        require(wait([&] { return title_is(folder / L"日本-🙂"); }), "Active right pane updates title");
        check(list->SetFocus(), "Activate left pane for caption");
        require(wait([&] { return title_is(folder / L"beta"); }), "Pane focus restores left caption");
        check(right_suggestion_list->SetFocus(), "Activate right pane for caption");
        require(wait([&] { return title_is(folder / L"日本-🙂"); }), "Pane focus restores right caption");
        const auto right_path = [&] {
            BSTR text{}; check(right_value->get_CurrentValue(&text), "Read right address");
            std::wstring result = text ? text : L""; SysFreeString(text); return result;
        };
        const auto mouse_history = [&](HWND target, WORD button, int pane, const std::filesystem::path& expected,
            bool available = true) {
            const auto generation = SendMessageW(process.window, WM_APP + 60, 3, pane);
            PostMessageW(target, WM_XBUTTONDOWN, MAKEWPARAM(button == XBUTTON1 ? MK_XBUTTON1 : MK_XBUTTON2, button), MAKELPARAM(12, 12));
            PostMessageW(target, WM_XBUTTONUP, MAKEWPARAM(0, button), MAKELPARAM(12, 12));
            require(wait([&] {
                return SendMessageW(process.window, WM_APP + 60, 4, pane) == 0 &&
                    (pane ? right_path() : current_path()) == expected.wstring() && title_is(expected);
            }), "Mouse history uses the pane under the native target");
            // Drain a barrier after the posted click, including the unavailable-history case.
            SendMessageW(target, WM_NULL, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            require(SendMessageW(process.window, WM_APP + 60, 3, pane) == generation + (available ? 1 : 0),
                "Mouse history starts exactly one request, or none without history");
        };
        const auto right_edit = native(process.window, L"EDIT", 2);
        mouse_history(native(process.window, L"EDIT"), XBUTTON1, 0, folder);
        require(right_path() == (folder / L"日本-🙂").wstring(), "Left mouse Back leaves the right location unchanged");
        mouse_history(native(process.window, L"EDIT", 1), XBUTTON2, 0, folder / L"beta");
        BSTR restored_query{}; check(search_value->get_CurrentValue(&restored_query), "Read restored mouse history query");
        require(restored_query && std::wstring_view(restored_query) == L"beta", "Mouse Forward restores the query");
        SysFreeString(restored_query);
        mouse_history(right_edit, XBUTTON1, 1, folder);
        mouse_history(right_edit, XBUTTON1, 1, folder, false);
        require(current_path() == (folder / L"beta").wstring(), "Right mouse Back leaves the left location unchanged");
        check(list->SetFocus(), "Leave keyboard focus in the opposite pane");
        mouse_history(native(process.window, L"Xui.FileList.1", 1), XBUTTON2, 1, folder / L"日本-🙂");
        mouse_history(native(process.window, L"EDIT", 3), XBUTTON2, 1, folder / L"日本-🙂", false);
        check(list->SetFocus(), "Keep focus in the left pane for a right application command");
        const auto right_generation = SendMessageW(process.window, WM_APP + 60, 3, 1);
        require(SendMessageW(right_edit, WM_APPCOMMAND, reinterpret_cast<WPARAM>(right_edit),
            MAKELPARAM(0, APPCOMMAND_BROWSER_BACKWARD)) == TRUE, "Native EDIT browser command reports handled");
        require(wait([&] { return right_path() == folder.wstring() && title_is(folder) &&
            SendMessageW(process.window, WM_APP + 60, 4, 1) == 0; }), "Browser command uses its native source pane");
        require(SendMessageW(process.window, WM_APP + 60, 3, 1) == right_generation + 1,
            "Browser command does not duplicate a history request");
        mouse_history(right_edit, XBUTTON2, 1, folder / L"日本-🙂");
        header_bounds(scale, true);
        if (capture_header) suggestion_capture::bitmap(process.window, nullptr, captures / L"split-dark.bmp");
        auto divider = find(uia.Get(), root.Get(), L"browser-divider");
        auto range = pattern<IUIAutomationRangeValuePattern>(divider.Get(), UIA_RangeValuePatternId);
        check(range->SetValue(60), "Accessible splitter resize");
        double ratio{};
        require(wait([&] { return SUCCEEDED(range->get_CurrentValue(&ratio)) && ratio > 59.9; }), "RangeValue publishes splitter ratio");
        RECT d{}; check(divider->get_CurrentBoundingRectangle(&d), "Read splitter bounds");
        require(d.right - d.left <= static_cast<int>(12 * scale), "Splitter UIA geometry is the divider, not both panes");
        const auto split_hwnd = GetParent(GetParent(list_hwnd));
        POINT origin{d.left + 4, d.top + 20}; ScreenToClient(split_hwnd, &origin);
        SendMessageW(split_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(origin.x, origin.y));
        SendMessageW(split_hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(origin.x - static_cast<int>(50 * scale), origin.y));
        SendMessageW(split_hwnd, WM_LBUTTONUP, 0, MAKELPARAM(origin.x - static_cast<int>(50 * scale), origin.y));
        require(wait([&] { return SUCCEEDED(range->get_CurrentValue(&ratio)) && ratio < 59; }), "Pointer drag updates split ratio");
        const auto dragged = ratio;
        SendMessageW(split_hwnd, WM_KEYDOWN, VK_RIGHT, 0);
        require(wait([&] { return SUCCEEDED(range->get_CurrentValue(&ratio)) && ratio > dragged; }), "Keyboard splitter resize");
        RECT outer{}; GetWindowRect(process.window, &outer);
        auto theme_control = find(uia.Get(), root.Get(), L"browser-theme");
        check(theme_control->SetFocus(), "Keep global command focus during narrow collapse");
        SetWindowPos(process.window, nullptr, 0, 0, static_cast<int>(500 * scale), static_cast<int>(600 * scale), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        require(wait([&] { return !IsWindowVisible(native(process.window, L"EDIT", 2)); }), "Narrow window hides secondary native EDIT");
        require(wait([&] { return title_is(folder / L"beta"); }), "Automatic collapse activates the visible pane even with global command focus");
        SetWindowPos(process.window, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        require(wait([&] { return IsWindowVisible(native(process.window, L"EDIT", 2)) != FALSE; }), "Widening restores secondary native EDIT");
        check(first_select->Select(), "Return to first tab");
        require(wait(settled), "Tab restoration settles");
        for (int i = 0; i < 20; ++i) {
            check(set_value(address_value.Get(), (folder / (i % 2 ? L"alpha" : L"beta")).c_str()), "Rapid address update");
            PostMessageW(native(process.window, L"EDIT"), WM_KEYDOWN, VK_RETURN, 0);
            check(set_value(search_value.Get(), i % 2 ? L"alpha" : L"beta"), "Rapid filtering");
            check(second_select->Select(), "Rapid tab switch");
            check(first_select->Select(), "Rapid tab restore");
        }
        check(second_select->Select(), "Select tab before close");
        const auto tab_hwnd = FindWindowExW(GetParent(list_hwnd), nullptr, L"Xui.Control.1", L"Left pane tabs");
        require(tab_hwnd != nullptr, "Find tab keyboard target");
        SendMessageW(tab_hwnd, WM_KEYDOWN, VK_DELETE, 0);
        require(wait([&] { return count_tabs() == 1 && settled(); }), "Closing active tab restores remaining tab");
        require(FAILED(second_select->Select()), "Closed tab provider rejects action");
        const auto cycle_peers = SendMessageW(process.window, WM_APP + 60, 14, 0);
        require(cycle_peers <= peer_count + 2, "Only current-path breadcrumb depth adds native controls");
        for (int i = 0; i < 6; ++i) { invoke(L"browser-split"); invoke(L"browser-split"); }
        require(SendMessageW(process.window, WM_APP + 60, 14, 0) == cycle_peers &&
            SendMessageW(process.window, WM_APP + 60, 15, 0) <= 2 &&
            SendMessageW(process.window, WM_APP + 60, 16, 0) <= 2, "Pane cycles retain a bounded native tree and task registry");
        go(folder / L"日本-🙂");
        require(SendMessageW(process.window, WM_APP + 60, 9, 0) == 0, "Latest tab request defeats stale completions");
        std::cout << "Explorer: starting keyboard workflows" << std::endl;
        check(tabs->SetFocus(), "Focus accessible tab strip");
        require(wait([&] { return events->boolean(UIA_HasKeyboardFocusPropertyId, L"Left pane tabs", true); }),
            "TabStrip publishes scoped UIA keyboard focus property events");
        check(list->SetFocus(), "Focus left list for keyboard workflows");
        require(wait([&] { return GetForegroundWindow() == process.window; }), "Explorer owns keyboard focus");
        press(process.window, 'L', true);
        BOOL focused{};
        require(wait([&] { return SUCCEEDED(address->get_CurrentHasKeyboardFocus(&focused)) && focused; }), "Ctrl+L focuses native address");
        press(process.window, 'F', true);
        require(wait([&] { return SUCCEEDED(search->get_CurrentHasKeyboardFocus(&focused)) && focused; }), "Ctrl+F focuses native search");
        press(process.window, 'T', true);
        require(wait([&] { return count_tabs() == 2 && settled(); }), "Ctrl+T creates tab");
        press(process.window, VK_TAB, true);
        require(wait([&] { BOOL selected{}; return SUCCEEDED(first_select->get_CurrentIsSelected(&selected)) && selected && settled(); }),
            "Ctrl+Tab switches to first tab");
        press(process.window, VK_TAB, true, true);
        require(wait([&] { BOOL selected{}; return SUCCEEDED(first_select->get_CurrentIsSelected(&selected)) && !selected && settled(); }),
            "Ctrl+Shift+Tab switches back");
        press(process.window, 'W', true);
        require(wait([&] { return count_tabs() == 1 && settled(); }), "Ctrl+W closes active tab");
        std::cout << "Explorer: address/search/tab shortcuts passed" << std::endl;
        check(list->SetFocus(), "Focus left list before pane switch");
        press(process.window, VK_F6);
        auto right_list = find(uia.Get(), root.Get(), L"browser-right-files");
        require(wait([&] { return SUCCEEDED(right_list->get_CurrentHasKeyboardFocus(&focused)) && focused; }), "F6 switches independent panes");
        press(process.window, VK_F6);
        require(wait([&] { return SUCCEEDED(list->get_CurrentHasKeyboardFocus(&focused)) && focused; }), "F6 returns to left pane");
        std::cout << "Explorer: pane shortcuts passed" << std::endl;
        go(folder);
        SendMessageW(list_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(static_cast<int>(20 * scale), static_cast<int>(16 * scale)));
        check(list->SetFocus(), "Focus directory for keyboard context menu");
        press(process.window, VK_F10, false, true);
        HWND popup{};
        require(wait([&] { popup = popup_for(process.info.dwProcessId); return popup != nullptr; }), "Shift+F10 opens native context menu");
        suggestion_capture::bitmap(process.window, popup, captures / L"explorer-menu.bmp");
        suggestion_capture::bitmap(popup, nullptr, captures / L"explorer-menu-detail.bmp");
        std::cout << "Explorer: keyboard context menu opened" << std::endl;
        ComPtr<IUIAutomationElement> menu;
        check(uia->ElementFromHandle(popup, &menu), "Read native menu");
        VARIANT menu_name{}; menu_name.vt = VT_BSTR; menu_name.bstrVal = SysAllocString(L"Open folder in new tab");
        ComPtr<IUIAutomationCondition> menu_condition;
        check(uia->CreatePropertyCondition(UIA_NamePropertyId, menu_name, &menu_condition), "Find folder menu command");
        VariantClear(&menu_name);
        ComPtr<IUIAutomationElement> command_item;
        check(menu->FindFirst(TreeScope_Descendants, menu_condition.Get(), &command_item), "Read folder command");
        require(command_item != nullptr, "Folder context command exists");
        check(pattern<IUIAutomationInvokePattern>(command_item.Get(), UIA_InvokePatternId)->Invoke(), "Invoke context command");
        std::cout << "Explorer: context command invoked" << std::endl;
        require(wait([&] { return count_tabs() == 2 && settled() && current_path() == (folder / L"alpha").wstring(); }),
            "Keyboard menu opens selected folder in new tab through shared command");
        SendMessageW(list_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(static_cast<int>(20 * scale), static_cast<int>(16 * scale)));
        SendMessageW(list_hwnd, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(static_cast<int>(20 * scale), static_cast<int>(80 * scale)));
        auto selection = pattern<IUIAutomationSelectionPattern>(list.Get(), UIA_SelectionPatternId);
        ComPtr<IUIAutomationElementArray> selected_items;
        check(selection->GetCurrentSelection(&selected_items), "Read empty-area context selection");
        int selected_count{};
        check(selected_items->get_Length(&selected_count), "Count selection after empty-area context");
        require(selected_count == 0, "Empty-area context never reuses a previous selection");
        auto deep = folder / L"日本-🙂";
        for (int i = 0; i < 4; ++i) {
            deep /= std::wstring(70, L'日') + std::to_wstring(i);
            require(std::filesystem::create_directory(deep), "Create owned long Unicode directory");
        }
        go(deep);
        require(current_path().size() > 260 && current_path() == deep.wstring() &&
            SendMessageW(process.window, WM_APP + 60, 9, 0) == 0, "Native address opens exact long Unicode folder");
        for (int i = count_tabs(); i < 16; ++i) invoke(L"browser-new-tab");
        require(wait([&] { return settled() && count_tabs() == 16; }), "Many long-named tabs retain bounded controls");
        RECT original{}; GetWindowRect(process.window, &original);
        const auto original_dpi = GetDpiForWindow(process.window);
        const auto layout_matrix = [&] {
            for (auto dpi : {96u, 144u, 192u}) {
                for (auto width : {924, 600, 460}) {
                    RECT requested{original.left, original.top, original.left + MulDiv(width, dpi, 96),
                        original.top + MulDiv(641, dpi, 96)};
                    const auto layouts = SendMessageW(process.window, WM_APP + 60, 2, 0);
                    SendMessageW(process.window, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&requested));
                    require(wait([&] { return SendMessageW(process.window, WM_APP + 60, 2, 0) > layouts; }), "Header DPI layout settles");
                    header_bounds(dpi / 96.0f, width == 924);
                    require((IsWindowVisible(native(process.window, L"EDIT", 2)) != FALSE) == (width == 924),
                        "Narrow layout hides the secondary native field");
                    if (capture_header) {
                        const auto filename = std::to_wstring(dpi) + L"-" + std::to_wstring(width) +
                            (SendMessageW(process.window, WM_APP + 60, 6, 0) ? L"-light.bmp" : L"-dark.bmp");
                        suggestion_capture::bitmap(process.window, nullptr, captures / filename);
                    }
                }
            }
        };
        layout_matrix();
        invoke(L"browser-theme");
        layout_matrix();
        SendMessageW(process.window, WM_DPICHANGED, MAKELONG(original_dpi, original_dpi), reinterpret_cast<LPARAM>(&original));
        require(wait([&] { return IsWindowVisible(native(process.window, L"EDIT", 2)) != FALSE; }), "Original split layout restored");
        std::cout << "Header: 18 DPI/width/theme cases; native address, compact commands, long paths and 16 tabs passed\n";
        if (global_focus_events)
            require(wait([&] { return events->count(UIA_AutomationFocusChangedEventId) > 0; }), "Keyboard workflows publish global focus events");
        std::cout << "Explorer: keyboard and empty-area context passed" << std::endl;
        require(SendMessageW(process.window, WM_APP + 60, 11, 0) == 1, "Dual panes share one renderer");
        const auto paints = SendMessageW(process.window, WM_APP + 60, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const auto before_idle = SendMessageW(process.window, WM_APP + 60, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        require(SendMessageW(process.window, WM_APP + 60, 0, 0) == before_idle, "Idle rendering stops with dual panes");
        (void)paints;
        PostMessageW(process.window, WM_CLOSE, 0, 0);
        require(WaitForSingleObject(process.info.hProcess, 5000) == WAIT_OBJECT_0, "Close completes promptly");
        BOOL selected{};
        require(FAILED(first_select->get_CurrentIsSelected(&selected)), "Tab provider revoked after window close");
        std::cout << "Explorer desktop: " << assertions << " assertions passed; navigation, tabs, split, stale requests, lifetime; no files launched\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "Explorer desktop: " << e.what() << '\n'; return 1; }
}
