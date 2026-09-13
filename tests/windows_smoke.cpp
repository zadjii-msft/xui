#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct AutomationError : std::runtime_error {
    HRESULT status;
    AutomationError(HRESULT value, const char* message)
        : std::runtime_error(std::string(message) + " (HRESULT " + std::to_string(value) + ")"), status(value) {}
};
void check(HRESULT result, const char* message) {
    if (FAILED(result)) throw AutomationError(result, message);
}
bool eventually(const std::function<bool()>& predicate, int milliseconds = 10000) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        try {
            if (predicate()) return true;
        } catch (const AutomationError& error) {
            // A view can change between two UIA calls in a polling probe.
            // Retry only an invalidated element, not other automation failures.
            if (error.status != UIA_E_ELEMENTNOTAVAILABLE) throw;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < limit);
    return false;
}
void focus(IUIAutomationElement* element, const char* message) {
    check(element->SetFocus(), message);
    auto stable = std::chrono::steady_clock::now();
    require(eventually([&] {
        BOOL focused{};
        check(element->get_CurrentHasKeyboardFocus(&focused), "Read settled keyboard target");
        if (!focused) { stable = std::chrono::steady_clock::now(); return false; }
        return std::chrono::steady_clock::now() - stable >= std::chrono::milliseconds(150);
    }), message);
}
struct Fixture {
    std::filesystem::path folder;
    std::vector<std::filesystem::path> files;
    Fixture() {
        folder = std::filesystem::current_path() /
            (L"xui-smoke-" + std::to_wstring(GetCurrentProcessId()));
        require(std::filesystem::create_directory(folder), "Create unique fixture directory");
        for (int i = 0; i < 3000; ++i) {
            wchar_t name[64];
            swprintf_s(name, L"file-%04d.txt", i);
            files.push_back(folder / name);
            std::ofstream stream(files.back());
            require(stream.good(), "Create fixture file");
        }
        files.push_back(folder / L"\u65e5\u672c\u8a9e-\U0001f642.txt");
        std::ofstream stream(files.back());
        require(stream.good(), "Create Unicode fixture file");
    }
    ~Fixture() {
        std::error_code error;
        for (const auto& file : files) {
            std::filesystem::remove(file, error);
            if (error) std::cerr << "Fixture cleanup: " << error.message() << '\n';
        }
        std::filesystem::remove(folder, error);
        if (error) std::cerr << "Fixture directory cleanup: " << error.message() << '\n';
    }
};
struct Process {
    PROCESS_INFORMATION info{};
    HWND window{};
    ~Process() {
        if (!info.hProcess) return;
        if (window && IsWindow(window)) PostMessageW(window, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(info.hProcess, 5000) == WAIT_TIMEOUT) {
            std::cerr << "Demo did not stop after WM_CLOSE. Terminating this test's process.\n";
            TerminateProcess(info.hProcess, 1);
        }
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
    }
};
struct WindowSearch { DWORD process_id; HWND window{}; };
BOOL CALLBACK find_window(HWND window, LPARAM parameter) {
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);
    DWORD process_id{};
    GetWindowThreadProcessId(window, &process_id);
    wchar_t name[128]{};
    GetClassNameW(window, name, 128);
    if (process_id == search.process_id && std::wstring(name) == L"Xui.Window.1") {
        search.window = window;
        return FALSE;
    }
    return TRUE;
}
HWND native_child(HWND root, const wchar_t* cls, int ordinal = 0) {
    struct Search { const wchar_t* cls; int ordinal; HWND result{}; } search{cls, ordinal};
    EnumChildWindows(root, [](HWND hwnd, LPARAM data) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(data);
        wchar_t name[128]{};
        GetClassNameW(hwnd, name, 128);
        if (_wcsicmp(name, s.cls) == 0 && s.ordinal-- == 0) { s.result = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
BOOL CALLBACK find_popup(HWND window, LPARAM parameter) {
    wchar_t name[32]{};
    GetClassNameW(window, name, 32);
    if (std::wstring(name) == L"#32768" && IsWindowVisible(window)) {
        *reinterpret_cast<HWND*>(parameter) = window;
        return FALSE;
    }
    return TRUE;
}
ComPtr<IUIAutomationElement> child(IUIAutomation* automation, IUIAutomationElement* parent,
                                   PROPERTYID property, const wchar_t* value) {
    VARIANT expected{};
    expected.vt = VT_BSTR;
    expected.bstrVal = SysAllocString(value);
    require(expected.bstrVal != nullptr, "Allocate property text");
    ComPtr<IUIAutomationCondition> condition;
    const HRESULT created = automation->CreatePropertyCondition(property, expected, &condition);
    VariantClear(&expected);
    check(created, "Create UIA condition");
    ComPtr<IUIAutomationElement> element;
    check(parent->FindFirst(TreeScope_Descendants, condition.Get(), &element), "Find UIA element");
    return element;
}
template<class T>
ComPtr<T> pattern(IUIAutomationElement* element, PATTERNID id) {
    ComPtr<T> result;
    check(element->GetCurrentPatternAs(id, __uuidof(T),
        reinterpret_cast<void**>(result.GetAddressOf())), "Get UIA pattern");
    require(result != nullptr, "Missing UIA pattern");
    return result;
}
enum class RowStep { first, last, next };
HRESULT row_relative(IUIAutomationTreeWalker* walker, IUIAutomationElement* element,
                     RowStep step, IUIAutomationElement** result) {
    *result = nullptr;
    ComPtr<IUIAutomationElement> row;
    HRESULT status = step == RowStep::first ? walker->GetFirstChildElement(element, &row)
        : step == RowStep::last ? walker->GetLastChildElement(element, &row)
        : walker->GetNextSiblingElement(element, &row);
    while (SUCCEEDED(status) && row) {
        CONTROLTYPEID type{};
        status = row->get_CurrentControlType(&type);
        if (FAILED(status)) return status;
        if (type == UIA_ListItemControlTypeId) {
            *result = row.Detach();
            return S_OK;
        }
        ComPtr<IUIAutomationElement> sibling;
        status = step == RowStep::last ? walker->GetPreviousSiblingElement(row.Get(), &sibling)
                                     : walker->GetNextSiblingElement(row.Get(), &sibling);
        row = std::move(sibling);
    }
    return status;
}
std::wstring name(IUIAutomationElement* element) {
    BSTR text{};
    check(element->get_CurrentName(&text), "Read accessible name");
    std::wstring result = text ? text : L"";
    SysFreeString(text);
    return result;
}
std::vector<int> runtime_id(IUIAutomationElement* element) {
    SAFEARRAY* id{};
    check(element->GetRuntimeId(&id), "Read runtime ID");
    LONG first{}, last{};
    SafeArrayGetLBound(id, 1, &first);
    SafeArrayGetUBound(id, 1, &last);
    std::vector<int> result;
    for (LONG index = first; index <= last; ++index) {
        int value{};
        check(SafeArrayGetElement(id, &index, &value), "Read runtime ID component");
        result.push_back(value);
    }
    SafeArrayDestroy(id);
    return result;
}
int selection_count(IUIAutomationSelectionPattern* selection) {
    ComPtr<IUIAutomationElementArray> elements;
    check(selection->GetCurrentSelection(&elements), "Read list selection");
    int length{};
    check(elements->get_Length(&length), "Read selection length");
    return length;
}
void set_text(IUIAutomationValuePattern* field, const wchar_t* text) {
    BSTR value = SysAllocString(text);
    require(value != nullptr, "Allocate search text");
    const HRESULT result = field->SetValue(value);
    SysFreeString(value);
    check(result, "Set native search text");
}
LRESULT counter(HWND window, WPARAM which) {
    DWORD_PTR value{};
    require(SendMessageTimeoutW(window, WM_APP + 60, which, 0, SMTO_ABORTIFHUNG, 2000, &value) != 0,
            "Read diagnostic counter");
    return static_cast<LRESULT>(value);
}
uint64_t cpu_time(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    require(GetProcessTimes(process, &created, &exited, &kernel, &user) != 0, "Read CPU time");
    auto ticks = [](FILETIME time) {
        return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    return ticks(kernel) + ticks(user);
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "Usage: xui_windows_smoke <xui_demo.exe>\n"; return 1; }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 1;
    int exit_code{};
    try {
        Fixture fixture;
        Process process;
        const auto executable = std::filesystem::absolute(argv[1]);
        std::wstring command = L"\"" + executable.wstring() + L"\" \"" + fixture.folder.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
            nullptr, &startup, &process.info) != 0, "Start demo");
        require(eventually([&] {
            WindowSearch search{process.info.dwProcessId};
            EnumWindows(find_window, reinterpret_cast<LPARAM>(&search));
            process.window = search.window;
            return search.window && IsWindowVisible(search.window);
        }), "Demo window did not appear");

        ComPtr<IUIAutomation> automation;
        check(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&automation)), "Create UIA client");
        ComPtr<IUIAutomationElement> root;
        check(automation->ElementFromHandle(process.window, &root), "Read host UIA element");
        auto list = child(automation.Get(), root.Get(), UIA_AutomationIdPropertyId, L"browser-files");
        require(list != nullptr, "Custom list not in UIA tree");
        CONTROLTYPEID type{};
        check(list->get_CurrentControlType(&type), "Read list role");
        require(type == UIA_ListControlTypeId, "Custom control must expose List role");
        auto edit = child(automation.Get(), root.Get(), UIA_AutomationIdPropertyId, L"browser-search");
        require(edit != nullptr, "Native search field not in UIA tree");
        require(!name(edit.Get()).empty(), "Search field needs an accessible name");
        auto value = pattern<IUIAutomationValuePattern>(edit.Get(), UIA_ValuePatternId);
        auto theme_button = child(automation.Get(), root.Get(), UIA_AutomationIdPropertyId, L"browser-theme");
        auto theme_action = pattern<IUIAutomationInvokePattern>(theme_button.Get(), UIA_InvokePatternId);
        auto selection = pattern<IUIAutomationSelectionPattern>(list.Get(), UIA_SelectionPatternId);
        BOOL required{};
        check(selection->get_CurrentIsSelectionRequired(&required), "Read selection requirement");
        require(required == FALSE, "UIA must allow an empty selection");
        auto scrolling = pattern<IUIAutomationScrollPattern>(list.Get(), UIA_ScrollPatternId);
        ComPtr<IUIAutomationTreeWalker> walker;
        check(automation->get_ControlViewWalker(&walker), "Create UIA tree walker");
        ComPtr<IUIAutomationElement> first, last;
        require(eventually([&] {
            first.Reset();
            return SUCCEEDED(row_relative(walker.Get(), list.Get(), RowStep::first, &first)) &&
                   first && name(first.Get()) == L"file-0000.txt";
        }), "Folder scan did not expose first row");
        check(row_relative(walker.Get(), list.Get(), RowStep::last, &last), "Read last list item");
        require(last && name(last.Get()) == L"\u65e5\u672c\u8a9e-\U0001f642.txt",
                "Unicode filename must survive enumeration and UIA");

        const auto identity = runtime_id(first.Get());
        auto first_selection = pattern<IUIAutomationSelectionItemPattern>(first.Get(), UIA_SelectionItemPatternId);
        const auto pointer_list = native_child(process.window, L"Xui.FileList.1");
        require(pointer_list != nullptr, "Find public list pointer target");
        SendMessageW(pointer_list, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 16));
        SendMessageW(pointer_list, WM_LBUTTONUP, 0, MAKELPARAM(20, 16));
        require(eventually([&] {
            BOOL selected{};
            return SUCCEEDED(first_selection->get_CurrentIsSelected(&selected)) && selected;
        }), "Pointer input selects a public list row");
        check(first_selection->Select(), "Select first item through UIA");
        require(selection_count(selection.Get()) == 1, "Selection pattern must return selected item");
        check(first_selection->RemoveFromSelection(), "Remove selected item through UIA");
        require(selection_count(selection.Get()) == 0, "Removal must clear the selection");
        check(first_selection->AddToSelection(), "Add first item to empty selection");
        require(selection_count(selection.Get()) == 1, "AddToSelection must select the item");
        check(last->SetFocus(), "Focus an unselected item");
        BOOL first_still_selected{};
        check(first_selection->get_CurrentIsSelected(&first_still_selected), "Read selection after focus change");
        require(first_still_selected != FALSE, "Item focus must not change selection");
        auto last_selection = pattern<IUIAutomationSelectionItemPattern>(last.Get(), UIA_SelectionItemPatternId);
        const HRESULT second_add = last_selection->AddToSelection();
        require(FAILED(second_add), "Single selection must reject adding a second item");
        require(selection_count(selection.Get()) == 1, "A client add must never create multiple selections");
        check(first_selection->Select(), "Restore first selection");
        check(last_selection->RemoveFromSelection(), "Removing an unselected row is a no-op");
        require(selection_count(selection.Get()) == 1, "Removing another row must preserve selection");
        check(first->SetFocus(), "Focus list item through UIA");
        BOOL focused{};
        check(first->get_CurrentHasKeyboardFocus(&focused), "Read item focus");
        require(focused != FALSE, "Selected row must expose keyboard focus");

        BOOL offscreen{};
        check(last->get_CurrentIsOffscreen(&offscreen), "Read offscreen state");
        require(offscreen != FALSE, "Last row must initially be offscreen");
        {
            HWND viewport = native_child(process.window, L"Xui.FileList.1");
            require(viewport != nullptr, "Find custom scrollbar viewport");
            RECT client{};
            require(GetClientRect(viewport, &client) != 0, "Read custom scrollbar bounds");
            const int x = client.right - 8;
            require(PostMessageW(viewport, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, client.bottom - 8)) != 0,
                    "Page through custom scrollbar track");
            require(PostMessageW(viewport, WM_LBUTTONUP, 0, MAKELPARAM(x, client.bottom - 8)) != 0,
                    "Release scrollbar track");
            require(eventually([&] {
                double scroll{};
                return SUCCEEDED(scrolling->get_CurrentVerticalScrollPercent(&scroll)) && scroll > 0;
            }), "Scrollbar track must page the viewport");
            check(scrolling->SetScrollPercent(UIA_ScrollPatternNoScroll, 0), "Reset scrollbar position");
            require(PostMessageW(viewport, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, 10)) != 0,
                    "Capture custom scrollbar thumb");
            require(PostMessageW(viewport, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, client.bottom - 8)) != 0,
                    "Drag custom scrollbar thumb");
            require(PostMessageW(viewport, WM_LBUTTONUP, 0, MAKELPARAM(x, client.bottom - 8)) != 0,
                    "Release custom scrollbar thumb");
            require(eventually([&] {
                double scroll{};
                return SUCCEEDED(scrolling->get_CurrentVerticalScrollPercent(&scroll)) && scroll > 99;
            }), "Custom scrollbar drag must reach the end");
            check(first_selection->get_CurrentIsSelected(&first_still_selected), "Read selection after dragging");
            require(first_still_selected != FALSE, "Scrollbar interaction must preserve selection");
            check(scrolling->SetScrollPercent(UIA_ScrollPatternNoScroll, 0), "Reset after scrollbar drag");
        }
        auto reveal = pattern<IUIAutomationScrollItemPattern>(last.Get(), UIA_ScrollItemPatternId);
        check(reveal->ScrollIntoView(), "Reveal virtual item");
        check(last->get_CurrentIsOffscreen(&offscreen), "Read revealed row state");
        require(offscreen == FALSE, "ScrollIntoView must reveal the last row");
        double percent{};
        check(scrolling->get_CurrentVerticalScrollPercent(&percent), "Read scroll percentage");
        require(percent > 99, "Last row must scroll to the end");

        set_text(value.Get(), L"file-0000");
        std::wstring filtered_first, filtered_next;
        const bool filtered = eventually([&] {
            ComPtr<IUIAutomationElement> row;
            if (FAILED(row_relative(walker.Get(), list.Get(), RowStep::first, &row)) || !row) return false;
            ComPtr<IUIAutomationElement> next;
            filtered_first = name(row.Get());
            const HRESULT sibling = row_relative(walker.Get(), row.Get(), RowStep::next, &next);
            filtered_next = next ? name(next.Get()) : L"(none)";
            return filtered_first == L"file-0000.txt" && SUCCEEDED(sibling) && !next;
        });
        if (!filtered) {
            BSTR actual{};
            check(value->get_CurrentValue(&actual), "Read failed search value");
            auto status = child(automation.Get(), root.Get(), UIA_AutomationIdPropertyId, L"browser-status");
            std::wcerr << L"Search value: " << actual << L"; first: " << filtered_first
                       << L"; next: " << filtered_next
                       << L"; status: " << (status ? name(status.Get()) : L"(missing)") << L'\n';
            SysFreeString(actual);
        }
        require(filtered, "Search did not filter to one item");
        require(runtime_id(first.Get()) == identity, "Item runtime ID must remain stable after filtering");
        require(selection_count(selection.Get()) == 1, "Visible selection must survive filtering");
        require(counter(process.window, 6) == 0, "Default visual theme must be dark");
        check(theme_action->Invoke(), "Switch to light theme");
        require(eventually([&] { return counter(process.window, 6) == 1; }), "Theme command must apply the light theme");
        BSTR themed_text{};
        check(value->get_CurrentValue(&themed_text), "Read search after theme change");
        const bool text_preserved = std::wstring(themed_text) == L"file-0000";
        SysFreeString(themed_text);
        require(text_preserved && selection_count(selection.Get()) == 1,
                "Theme changes must preserve text and selection");
        require(runtime_id(first.Get()) == identity, "Theme changes must preserve item identity");
        check(theme_action->Invoke(), "Restore dark theme");
        require(eventually([&] { return counter(process.window, 6) == 0; }), "Theme command must restore the dark theme");

        set_text(value.Get(), L"file-0001");
        ComPtr<IUIAutomationElement> other;
        require(eventually([&] {
            other.Reset();
            return SUCCEEDED(row_relative(walker.Get(), list.Get(), RowStep::first, &other)) &&
                   other && name(other.Get()) == L"file-0001.txt";
        }), "Filter must support a nonempty result without visible selection");
        require(selection_count(selection.Get()) == 0, "Nonempty filtered list can have no visible selection");
        auto other_selection = pattern<IUIAutomationSelectionItemPattern>(other.Get(), UIA_SelectionItemPatternId);
        check(other_selection->RemoveFromSelection(), "Remove unselected row while selection is hidden");
        set_text(value.Get(), L"");
        require(eventually([&] { return selection_count(selection.Get()) == 1; }),
                "Unselected removal must preserve hidden selection identity");

        set_text(value.Get(), L"no-matching-file");
        require(eventually([&] {
            ComPtr<IUIAutomationElement> row;
            return SUCCEEDED(row_relative(walker.Get(), list.Get(), RowStep::first, &row)) && !row;
        }), "Search must expose no children for no matches");
        require(selection_count(selection.Get()) == 0, "Hidden selection must not appear in UIA selection");
        const HRESULT stale_action = first_selection->Select();
        require(FAILED(stale_action), "Hidden item action must reject stale selection");
        require(FAILED(first_selection->RemoveFromSelection()), "Hidden row removal must not clear stored selection");

        set_text(value.Get(), L"");
        require(eventually([&] { return selection_count(selection.Get()) == 1; }),
                "Selection must return after clearing the filter");
        HWND list_window = native_child(process.window, L"Xui.FileList.1");
        require(list_window != nullptr, "Find list HWND");
        HWND edit_window = native_child(process.window, L"EDIT", 1);
        require(edit_window != nullptr, "Find native EDIT HWND");
        focus(first.Get(), "Focus custom list before keyboard traversal");
        auto first_tabs = child(automation.Get(), root.Get(), UIA_AutomationIdPropertyId, L"browser-tabs");
        require(PostMessageW(list_window, WM_KEYDOWN, VK_TAB, 0) != 0, "Tab wraps to the first tab strip");
        require(eventually([&] {
            BOOL has_focus{};
            return SUCCEEDED(first_tabs->get_CurrentHasKeyboardFocus(&has_focus)) && has_focus;
        }), "Tab must wrap to the first enabled header control");
        focus(edit.Get(), "Focus native search before keyboard traversal");
        require(PostMessageW(edit_window, WM_KEYDOWN, VK_TAB, 0) != 0, "Tab to custom list");
        require(eventually([&] {
            BOOL has_focus{};
            return SUCCEEDED(first->get_CurrentHasKeyboardFocus(&has_focus)) && has_focus;
        }), "Tab must move keyboard focus back to the custom list");
        auto settled = [&] {
            return counter(process.window, 7) == counter(process.window, 3) &&
                counter(process.window, 4) == 0 && counter(process.window, 5) == 0 &&
                counter(process.window, 10) == 0;
        };
        require(eventually(settled), "Initial filter must settle before live input");
        focus(edit.Get(), "Focus native EDIT for live typing");
        double maximum_input_ms{};
        for (wchar_t character : std::wstring(L"file-0299")) {
            const auto start = std::chrono::steady_clock::now();
            DWORD_PTR ignored{};
            require(SendMessageTimeoutW(edit_window, WM_CHAR, character, 1,
                SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 2000, &ignored) != 0,
                "Native typing must stay responsive during filtering");
            maximum_input_ms = std::max(maximum_input_ms,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            if (character == L'-')
                require(PostMessageW(list_window, WM_KEYDOWN, VK_F5, 0) != 0,
                    "Refresh the source during live typing");
            require(counter(process.window, 7) <= counter(process.window, 3),
                "Applied generation must never exceed the requested generation");
        }
        require(eventually([&] { return settled() && counter(process.window, 8) == 1; }),
            "Live typing must apply its latest query");
        ComPtr<IUIAutomationElement> typed_row;
        check(row_relative(walker.Get(), list.Get(), RowStep::first, &typed_row), "Read live typing result");
        require(typed_row && name(typed_row.Get()) == L"file-0299.txt", "Live input must preserve every character");

        const auto before_composition = counter(process.window, 3);
        SendMessageW(edit_window, WM_IME_STARTCOMPOSITION, 0, 0);
        set_text(value.Get(), L"file-0200");
        require(counter(process.window, 3) == before_composition,
            "A simulated IME composition must not submit intermediate text");
        SendMessageW(edit_window, WM_IME_ENDCOMPOSITION, 0, 0);
        require(eventually([&] { return settled() && counter(process.window, 3) > before_composition; }),
            "The simulated IME end must submit committed text");

        for (int request = 0; request < 80; ++request) {
            set_text(value.Get(), request % 2 ? L"file-0000" : L"no-matching-file");
            if (request % 8 == 0)
                require(PostMessageW(list_window, WM_KEYDOWN, VK_F5, 0) != 0,
                    "Refresh during rapid query replacement");
            if (request == 20 || request == 60)
                check(theme_action->Invoke(), "Change the theme while worker requests are active");
        }
        set_text(value.Get(), L"file-0123");
        require(eventually([&] {
            return settled() && counter(process.window, 8) == 1 && counter(process.window, 6) == 0;
        }), "Rapid queries, refreshes and themes must settle on the latest state");
        typed_row.Reset();
        check(row_relative(walker.Get(), list.Get(), RowStep::first, &typed_row), "Read latest-only result");
        require(typed_row && name(typed_row.Get()) == L"file-0123.txt", "A stale filter must not replace the latest query");
        const auto final_generation = counter(process.window, 7);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        require(counter(process.window, 7) == final_generation && counter(process.window, 8) == 1,
            "Late worker results must not overwrite the final view");
        set_text(value.Get(), L"");
        require(eventually([&] { return settled() && counter(process.window, 8) == 3001; }),
            "Clearing live input must restore the complete source");
        require(selection_count(selection.Get()) == 1 && runtime_id(first.Get()) == identity,
            "Concurrent filter and refresh requests must retain selection identity");
        std::cout << "Live typing: 9 characters; maximum native dispatch: " << maximum_input_ms
                  << " ms; rapid replacements: 80; latest generation: " << final_generation << '\n';
        const auto generation = counter(process.window, 3);
        for (int request = 0; request < 20; ++request)
            require(PostMessageW(list_window, WM_KEYDOWN, VK_F5, 0) != 0, "Request repeated refresh");
        require(eventually([&] {
            return counter(process.window, 3) == generation + 20 && counter(process.window, 4) == 0;
        }), "Repeated refresh must finish the latest generation");
        require(runtime_id(first.Get()) == identity, "Refresh must preserve existing item identity");
        check(first_selection->Select(), "Existing provider must still select after refresh");
        const auto menu_generation = counter(process.window, 3);
        require(PostMessageW(list_window, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(list_window), -1) != 0,
                "Open keyboard-positioned context menu");
        HWND popup{};
        require(eventually([&] {
            EnumThreadWindows(GetWindowThreadProcessId(list_window, nullptr), find_popup,
                              reinterpret_cast<LPARAM>(&popup));
            return popup != nullptr;
        }), "Native context menu must open");
        ComPtr<IUIAutomationElement> menu;
        check(automation->ElementFromHandle(popup, &menu), "Read context menu through UIA");
        ComPtr<IUIAutomationCondition> menu_condition;
        check(automation->CreateTrueCondition(&menu_condition), "Create menu condition");
        ComPtr<IUIAutomationElementArray> menu_items;
        check(menu->FindAll(TreeScope_Descendants, menu_condition.Get(), &menu_items), "Read menu items");
        int menu_count{};
        check(menu_items->get_Length(&menu_count), "Read menu item count");
        ComPtr<IUIAutomationElement> refresh;
        for (int index = 0; index < menu_count; ++index) {
            ComPtr<IUIAutomationElement> item;
            check(menu_items->GetElement(index, &item), "Read menu item");
            if (name(item.Get()).starts_with(L"Refresh")) refresh = item;
        }
        require(refresh != nullptr, "Context menu must expose Refresh");
        auto invoke_refresh = pattern<IUIAutomationInvokePattern>(refresh.Get(), UIA_InvokePatternId);
        check(invoke_refresh->Invoke(), "Invoke Refresh from native context menu");
        require(eventually([&] {
            return counter(process.window, 3) == menu_generation + 1 && counter(process.window, 4) == 0;
        }), "Context menu and keyboard must use the same refresh command");
        require(PostMessageW(list_window, WM_KEYDOWN, VK_END, 0) != 0, "Post End key");
        require(eventually([&] {
            ComPtr<IUIAutomationElementArray> selected;
            ComPtr<IUIAutomationElement> row;
            return SUCCEEDED(selection->GetCurrentSelection(&selected)) &&
                   SUCCEEDED(selected->GetElement(0, &row)) && row &&
                   name(row.Get()) == L"\u65e5\u672c\u8a9e-\U0001f642.txt";
        }), "End key must select final item");
        require(PostMessageW(list_window, WM_KEYDOWN, VK_HOME, 0) != 0, "Post Home key");
        require(eventually([&] {
            BOOL selected{};
            return SUCCEEDED(first_selection->get_CurrentIsSelected(&selected)) && selected;
        }), "Home key must select first item");
        require(PostMessageW(list_window, WM_KEYDOWN, VK_NEXT, 0) != 0, "Post Page Down key");
        require(eventually([&] {
            BOOL selected{};
            return SUCCEEDED(first_selection->get_CurrentIsSelected(&selected)) && !selected;
        }), "Page Down must move selection");

        const auto previous_paints = counter(process.window, 0);
        require(PostMessageW(process.window, WM_DISPLAYCHANGE, 0, 0) != 0, "Request target recreation");
        require(eventually([&] { return counter(process.window, 0) > previous_paints; }),
                "Display change must recreate and repaint graphics targets");
        require(SetWindowPos(process.window, nullptr, 0, 0, 700, 500,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != 0, "Resize demo");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const auto rows = counter(process.window, 1);
        require(rows > 0 && rows < 40, "Virtualized drawing must not draw all 3001 items");
        LRESULT before{}, after{};
        uint64_t cpu_before{}, cpu_after{};
        int intervals{};
        // A shared desktop can deliver a late hover/focus transition after resize.
        // Require a fully quiet interval, with a bounded retry that still rejects a render loop.
        do {
            before = counter(process.window, 0);
            cpu_before = cpu_time(process.info.hProcess);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            cpu_after = cpu_time(process.info.hProcess);
            after = counter(process.window, 0);
            ++intervals;
        } while (after != before && intervals < 3);
        std::cout << "Virtual rows submitted: " << rows << " / 3001\n"
                  << "Idle interval: 2000 ms; paint delta: " << after - before
                  << "; process CPU: " << (cpu_after - cpu_before) / 10000.0
                  << " ms; sampled intervals: " << intervals << '\n';
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        require(GetProcessMemoryInfo(process.info.hProcess,
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) != 0,
            "Read idle demo memory");
        std::cout << "Idle memory: private_bytes=" << memory.PrivateUsage
                  << "; working_set_bytes=" << memory.WorkingSetSize << '\n';
        require(after == before, "Idle custom UI must not repaint continuously");
        bool observed_scan{};
        for (int attempt = 0; attempt < 10 && !observed_scan; ++attempt) {
            require(PostMessageW(list_window, WM_KEYDOWN, VK_F5, 0) != 0, "Request scan before shutdown");
            const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
            do {
                observed_scan = counter(process.window, 5) != 0;
                if (observed_scan) break;
                std::this_thread::yield();
            } while (std::chrono::steady_clock::now() < limit);
        }
        require(observed_scan, "Observe active directory work before shutdown");
        const auto shutdown_start = std::chrono::steady_clock::now();
        require(PostMessageW(process.window, WM_CLOSE, 0, 0) != 0, "Close demo");
        require(WaitForSingleObject(process.info.hProcess, 10000) == WAIT_OBJECT_0,
                "Demo must stop without hanging");
        DWORD code{};
        require(GetExitCodeProcess(process.info.hProcess, &code) && code == 0, "Demo must exit successfully");
        std::cout << "Shutdown after observed work: "
                  << std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - shutdown_start).count() << " ms\n";
        BOOL stale{};
        require(FAILED(first_selection->get_CurrentIsSelected(&stale)),
                "Providers retained after window shutdown must become unavailable");

        Process invalid_folder;
        command = L"\"" + executable.wstring() + L"\" \"" +
            (fixture.folder / L"missing-folder").wstring() + L"\"";
        require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
            nullptr, &startup, &invalid_folder.info) != 0, "Start invalid-folder demo");
        require(eventually([&] {
            WindowSearch search{invalid_folder.info.dwProcessId};
            EnumWindows(find_window, reinterpret_cast<LPARAM>(&search));
            invalid_folder.window = search.window;
            return search.window && IsWindowVisible(search.window) && counter(search.window, 4) == 0;
        }), "Invalid-folder scan must finish without a hang");
        ComPtr<IUIAutomationElement> error_root;
        check(automation->ElementFromHandle(invalid_folder.window, &error_root), "Read error demo UIA");
        auto error_status = child(automation.Get(), error_root.Get(), UIA_AutomationIdPropertyId, L"browser-status");
        require(error_status && name(error_status.Get()).find(L"Cannot read folder:") == 0,
                "Folder errors must remain visible and accessible");
        require(PostMessageW(invalid_folder.window, WM_CLOSE, 0, 0) != 0, "Close error demo");
        require(WaitForSingleObject(invalid_folder.info.hProcess, 10000) == WAIT_OBJECT_0,
                "Invalid-folder demo must stop without a hang");
        std::cout << "Windows smoke passed: native edit, Unicode, UIA, selection, keyboard, scroll, "
                     "live typing, latest-only filtering, simulated IME boundary, theme switching, "
                     "scrollbar drag, context command, refresh cancellation, "
                     "target recreation, resize, idle, errors, shutdown.\n";
    } catch (const std::exception& error) {
        std::cerr << "Windows smoke failed: " << error.what() << '\n';
        exit_code = 1;
    }
    CoUninitialize();
    return exit_code;
}
