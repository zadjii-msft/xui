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
#include <vector>
#include "uia_events.hpp"
#include "suggestion_capture.hpp"
#include "../demo/gallery_catalog.hpp"
#include "../demo/gallery_reference.hpp"
#include "../demo/gallery_urls.hpp"

using Microsoft::WRL::ComPtr;
namespace {
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void check(HRESULT result, const char* text) {
    if (FAILED(result)) throw std::runtime_error(std::string(text) + " HRESULT=" + std::to_string(result));
}
bool eventually(const std::function<bool()>& predicate) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
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
ComPtr<IUIAutomationElement> named(IUIAutomation* automation, IUIAutomationElement* root, const wchar_t* name,
    CONTROLTYPEID type = 0) {
    VARIANT value{};
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(name);
    ComPtr<IUIAutomationCondition> condition;
    const auto result = automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition);
    VariantClear(&value);
    check(result, "Create name condition");
    if (type) {
        value = {};
        value.vt = VT_I4;
        value.lVal = type;
        ComPtr<IUIAutomationCondition> role, combined;
        check(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, value, &role), "Create role condition");
        check(automation->CreateAndCondition(condition.Get(), role.Get(), &combined), "Combine name and role");
        condition = combined;
    }
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
ComPtr<IUIAutomationElement> identified(IUIAutomation* automation, IUIAutomationElement* root, const std::wstring& id) {
    require(root != nullptr, "Missing root for stable navigation lookup");
    // Virtual tree traversal exposes the viewport, not every item in the source.
    if (!id.empty() && id.front() >= L'0' && id.front() <= L'9') {
        VARIANT role{};
        role.vt = VT_I4;
        role.lVal = UIA_TreeControlTypeId;
        ComPtr<IUIAutomationCondition> condition;
        check(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, role, &condition), "Find navigation sections");
        ComPtr<IUIAutomationElementArray> sections;
        check(root->FindAll(TreeScope_Descendants, condition.Get(), &sections), "Read navigation sections");
        int count{};
        check(sections->get_Length(&count), "Read navigation section count");
        for (int i = 0; i < count; ++i) {
            ComPtr<IUIAutomationElement> section, item;
            check(sections->GetElement(i, &section), "Read navigation section");
            auto container = pattern<IUIAutomationItemContainerPattern>(section.Get(), UIA_ItemContainerPatternId);
            VARIANT value{};
            value.vt = VT_BSTR;
            value.bstrVal = SysAllocString(id.c_str());
            const auto result = container->FindItemByProperty(nullptr, UIA_AutomationIdPropertyId, value, &item);
            VariantClear(&value);
            check(result, "Find virtual navigation item");
            if (item) return item;
        }
        return {};
    }
    VARIANT value{};
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(id.c_str());
    ComPtr<IUIAutomationCondition> condition;
    const auto result = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
    VariantClear(&value);
    check(result, "Create automation ID condition");
    ComPtr<IUIAutomationElement> element;
    check(root->FindFirst(TreeScope_Descendants, condition.Get(), &element), "Find stable navigation item");
    return element;
}
int selectable_count(IUIAutomationElement* root) {
    if (!root) return 0;
    auto container = pattern<IUIAutomationItemContainerPattern>(root, UIA_ItemContainerPatternId);
    ComPtr<IUIAutomationElement> previous;
    int count{};
    for (;;) {
        ComPtr<IUIAutomationElement> item;
        check(container->FindItemByProperty(previous.Get(), 0, VARIANT{}, &item), "Read virtual navigation item");
        if (!item) return count;
        ComPtr<IUnknown> selection;
        check(item->GetCurrentPattern(UIA_SelectionItemPatternId, &selection), "Read navigation selection support");
        if (selection) ++count;
        previous = item;
    }
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
    if (!arrived) {
        UIA_HWND native{};
        const auto native_result = element->get_CurrentNativeWindowHandle(&native);
        const auto target = reinterpret_cast<HWND>(native);
        DWORD target_process{}, foreground_process{};
        const auto target_thread = target ? GetWindowThreadProcessId(target, &target_process) : 0;
        const auto foreground = GetForegroundWindow();
        GetWindowThreadProcessId(foreground, &foreground_process);
        GUITHREADINFO info{sizeof(info)};
        const auto thread_result = target_thread ? GetGUIThreadInfo(target_thread, &info) : FALSE;
        BOOL uia_focus{};
        const auto focus_result = element->get_CurrentHasKeyboardFocus(&uia_focus);
        std::cerr << "Focus timeout: " << message << "; target=" << target << " target_pid=" << target_process
            << " native_hr=" << native_result << " gui_ok=" << thread_result << " thread_focus=" << info.hwndFocus
            << " thread_active=" << info.hwndActive << " foreground=" << foreground
            << " foreground_pid=" << foreground_process << " test_pid=" << GetCurrentProcessId()
            << " uia_focus=" << uia_focus << " focus_hr=" << focus_result << '\n';
    }
    require(arrived, message);
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
void click_at(HWND host, POINT point) {
    require(GetAncestor(WindowFromPoint(point), GA_ROOT) == host, "Mouse input targets only the owned gallery window");
    require(SetCursorPos(point.x, point.y) != 0, "Move mouse to gallery control");
    INPUT input[2]{};
    input[0].type = input[1].type = INPUT_MOUSE;
    input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    require(SendInput(2, input, sizeof(INPUT)) == 2, "Click gallery through the native mouse queue");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
void click_element(HWND host, IUIAutomationElement* element) {
    require(element != nullptr, "Mouse target exists");
    RECT bounds{};
    check(element->get_CurrentBoundingRectangle(&bounds), "Read mouse target geometry");
    click_at(host, {(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2});
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
struct NativeFocusEvents {
    static inline thread_local NativeFocusEvents* current{};
    HWND target{};
    size_t count{};
    HWINEVENTHOOK hook{};
    explicit NativeFocusEvents(DWORD process) {
        current = this;
        hook = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr,
            [](HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG, DWORD, DWORD) {
                if (current && window == current->target && object == OBJID_CLIENT) ++current->count;
            }, process, 0, WINEVENT_OUTOFCONTEXT);
        require(hook != nullptr, "Subscribe process-scoped native focus events");
    }
    ~NativeFocusEvents() { UnhookWinEvent(hook); current = nullptr; }
};
void search_disclosure(IUIAutomation* automation, IUIAutomationElement* root) {
    ComPtr<IUIAutomationElement> search;
    require(eventually([&] { search = named(automation, root, L"Search controls", UIA_EditControlTypeId); return search != nullptr; }),
        "Find navigation search");
    auto value = pattern<IUIAutomationValuePattern>(search.Get(), UIA_ValuePatternId);
    auto catalog = identified(automation, root, L"gallery-catalog");
    auto group = identified(automation, catalog.Get(), L"1002:1");
    auto disclosure = pattern<IUIAutomationExpandCollapsePattern>(group.Get(), UIA_ExpandCollapsePatternId);
    const auto count = [&] { return selectable_count(identified(automation, catalog.Get(), L"gallery-catalog-items").Get()); };
    const auto expanded = [&] {
        ExpandCollapseState state{};
        check(disclosure->get_CurrentExpandCollapseState(&state), "Read search group disclosure");
        return state == ExpandCollapseState_Expanded;
    };
    check(disclosure->Collapse(), "Collapse category before search");
    set_value(value.Get(), L"collections");
    require(eventually([&] { return expanded() && count() == 6; }), "Search expands a collapsed category with matches");
    check(disclosure->Collapse(), "Collapse catalog category during search");
    require(eventually([&] { return !expanded() && count() == 0; }), "Collapsed search category hides its example rows");
    set_value(value.Get(), L"COLLECTION");
    require(eventually([&] { return !expanded() && count() == 0; }), "Query edits retain manual category collapse");
    const auto filtered_count = L"6 of " + std::to_wstring(gallery::entries.size()) + L" examples";
    require(eventually([&] {
        return named(automation, root, filtered_count.c_str(), UIA_TextControlTypeId) &&
            identified(automation, root, L"gallery-page-files");
    }), "Collapsed matches retain their count and page preview instead of showing no results");
    set_value(value.Get(), L"no-such-control-zz");
    require(eventually([&] { return named(automation, root, L"No matching controls") != nullptr; }), "Empty query results show the empty page");
    set_value(value.Get(), L"collections");
    require(eventually([&] { return !expanded() && count() == 0; }), "A returning match retains manual collapse");
    require(eventually([&] { return identified(automation, root, L"gallery-page-files") != nullptr; }),
        "Returning matches restore a page preview without reopening the category");
    check(disclosure->Expand(), "Reopen catalog category during search");
    require(eventually([&] { return expanded() && count() == 6; }), "Search category expands through UIA");
    set_value(value.Get(), L"");
    require(eventually([&] { return !expanded(); }), "Clearing search restores the previously collapsed category");
    set_value(value.Get(), L"collections");
    require(eventually([&] { return expanded() && count() == 6; }), "A new search starts without manual overrides");
    set_value(value.Get(), L"");
    check(disclosure->Expand(), "Restore original catalog disclosure");
    require(eventually([&] { return count() == gallery::entries.size(); }), "Search disclosure check restores the complete catalog");
}
}
void palette_smoke(IUIAutomation* automation, IUIAutomationElement* root, HWND hwnd, const std::filesystem::path& captures) {
    struct CursorRestore {
        POINT point{};
        CursorRestore() { require(GetCursorPos(&point) != 0, "Save mouse position"); }
        ~CursorRestore() { SetCursorPos(point.x, point.y); }
    } cursor;
    auto open = pattern<IUIAutomationInvokePattern>(named(automation, root, L"Open command palette", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId);
    for (const auto* theme : {L"dark", L"light"}) {
        if (std::wstring_view(theme) == L"light")
            check(pattern<IUIAutomationInvokePattern>(named(automation, root, L"Theme", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Switch palette theme");
        check(open->Invoke(), "Open gallery palette");
        ComPtr<IUIAutomationElement> palette, menu, search;
        require(eventually([&] {
            palette = named(automation, root, L"Gallery commands", UIA_PaneControlTypeId);
            menu = named(automation, root, L"Gallery commands", UIA_MenuControlTypeId);
            search = named(automation, root, L"Search commands", UIA_EditControlTypeId);
            return palette && menu && search;
        }), "Gallery palette exposes popup, results, and native search");
        const auto has_sections = [&] {
            return named(automation, menu.Get(), L"Sample commands", UIA_HeaderControlTypeId) &&
                named(automation, menu.Get(), L"Other actions", UIA_HeaderControlTypeId);
        };
        require(eventually(has_sections), "Gallery palette shows section headers");
        RECT search_rect{};
        check(search->get_CurrentBoundingRectangle(&search_rect), "Read palette search geometry");
        const POINT search_point{(search_rect.left + search_rect.right) / 2, (search_rect.top + search_rect.bottom) / 2};
        require(WindowFromPoint(search_point) == handle(automation, search.Get()),
            "Mouse hit testing reaches the palette search instead of the underlying page");
        click_element(hwnd, named(automation, menu.Get(), L"Disabled command", UIA_MenuItemControlTypeId).Get());
        require(SendMessageW(hwnd, WM_APP + 60, 24, 0) == 1, "Clicking a disabled command does not dismiss the palette");
        click_at(hwnd, search_point);
        require(eventually([&] { return focused(search.Get()); }), "Clicking native search text focuses the palette editor");
        click_element(hwnd, named(automation, menu.Get(), L"Sample commands", UIA_HeaderControlTypeId).Get());
        require(SendMessageW(hwnd, WM_APP + 60, 24, 0) == 1, "Clicking a section does not dismiss the palette");
        RECT disabled_bounds{}, next_section{};
        check(named(automation, menu.Get(), L"Disabled command", UIA_MenuItemControlTypeId)->get_CurrentBoundingRectangle(&disabled_bounds),
            "Read command before the separator");
        check(named(automation, menu.Get(), L"Other actions", UIA_HeaderControlTypeId)->get_CurrentBoundingRectangle(&next_section),
            "Read section after the separator");
        require(next_section.top > disabled_bounds.bottom, "A separator slot exists between command groups");
        click_at(hwnd, {(disabled_bounds.left + disabled_bounds.right) / 2, (disabled_bounds.bottom + next_section.top) / 2});
        require(SendMessageW(hwnd, WM_APP + 60, 24, 0) == 1, "Clicking a separator does not dismiss the palette");
        const auto scale = GetDpiForWindow(hwnd) / 96.0;
        click_at(hwnd, {search_rect.left - static_cast<LONG>(22 * scale), search_point.y});
        require(eventually([&] { return focused(search.Get()); }), "Clicking search icon padding focuses the palette editor");
        RECT full{}, client{};
        check(palette->get_CurrentBoundingRectangle(&full), "Read full palette geometry");
        click_at(hwnd, {full.left + 3, full.top + 20});
        require(SendMessageW(hwnd, WM_APP + 60, 24, 0) == 1, "Clicking popup padding does not dismiss the palette");
        GetClientRect(hwnd, &client); MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&client), 2);
        require(std::abs((full.left + full.right) - (client.left + client.right)) <= 2, "Gallery palette centers in the client");
        RECT outer{}; GetWindowRect(hwnd, &outer);
        require(SetWindowPos(hwnd, nullptr, 0, 0, outer.right - outer.left + 80, outer.bottom - outer.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != 0, "Resize gallery with an open palette");
        require(eventually([&] {
            RECT resized{}, viewport{};
            GetClientRect(hwnd, &viewport); MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&viewport), 2);
            return SUCCEEDED(palette->get_CurrentBoundingRectangle(&resized)) && resized.left > full.left &&
                std::abs((resized.left + resized.right) - (viewport.left + viewport.right)) <= 2;
        }), "An open palette recenters when the window resizes");
        require(SetWindowPos(hwnd, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != 0, "Restore gallery size");
        require(eventually([&] {
            RECT restored{};
            return SUCCEEDED(palette->get_CurrentBoundingRectangle(&restored)) && EqualRect(&restored, &full);
        }), "Restoring window size restores palette geometry");
        suggestion_capture::bitmap(hwnd, nullptr, captures / (L"palette-full-" + std::wstring(theme) + L".bmp"));
        auto value = pattern<IUIAutomationValuePattern>(search.Get(), UIA_ValuePatternId);
        set_value(value.Get(), L"Open");
        RECT filtered{};
        require(eventually([&] {
            return SUCCEEDED(palette->get_CurrentBoundingRectangle(&filtered)) && filtered.bottom < full.bottom;
        }), "Filtering shrinks the real gallery palette");
        require(filtered.top == full.top && filtered.left == full.left && filtered.right == full.right,
            "Filtering preserves gallery search position");
        require(named(automation, menu.Get(), L"Sample commands", UIA_HeaderControlTypeId) &&
            !named(automation, menu.Get(), L"Other actions", UIA_HeaderControlTypeId), "Filtering removes empty gallery sections");
        SendMessageW(hwnd, WM_ACTIVATE, WA_INACTIVE, 0);
        require(SendMessageW(hwnd, WM_APP + 60, 24, 0) == 1, "Gallery palette survives deactivation");
        BSTR text{}; check(value->get_CurrentValue(&text), "Read preserved palette query");
        const bool preserved = text && std::wstring_view(text) == L"Open"; SysFreeString(text);
        require(preserved, "Deactivation preserves the search query");
        suggestion_capture::bitmap(hwnd, nullptr, captures / (L"palette-filtered-" + std::wstring(theme) + L".bmp"));
        set_value(value.Get(), L"no matching command");
        require(eventually([&] { return named(automation, menu.Get(), L"Open sample", UIA_MenuItemControlTypeId) == nullptr; }),
            "Empty search removes gallery command results");
        set_value(value.Get(), L"");
        require(eventually(has_sections), "Clearing the query restores gallery sections");
        click_element(hwnd, named(automation, root, L"Close command palette", UIA_ButtonControlTypeId).Get());
        require(eventually([&] { return SendMessageW(hwnd, WM_APP + 60, 24, 0) == 0; }), "Close dismisses the gallery palette");
        check(open->Invoke(), "Reopen palette for mouse command activation");
        click_element(hwnd, named(automation, root, L"Pin", UIA_ButtonControlTypeId).Get());
        require(eventually([&] { return named(automation, root, L"Events: Pin only. Primary did not run.") != nullptr; }),
            "Mouse pin invokes only its independent action");
        require(SendMessageW(hwnd, WM_APP + 60, 24, 0) == 1, "Pin keeps the palette open");
        click_element(hwnd, named(automation, root, L"Open sample", UIA_MenuItemControlTypeId).Get());
        require(eventually([&] { return named(automation, root, L"Events: Open sample.") != nullptr &&
            SendMessageW(hwnd, WM_APP + 60, 24, 0) == 0; }), "Mouse command activation invokes and dismisses the palette");
        check(open->Invoke(), "Reopen palette for submenu mouse activation");
        click_element(hwnd, named(automation, root, L"More actions", UIA_MenuItemControlTypeId).Get());
        ComPtr<IUIAutomationElement> nested;
        require(eventually([&] { nested = named(automation, root, L"Nested action", UIA_MenuItemControlTypeId); return nested != nullptr; }),
            "Mouse activation opens the submenu");
        click_element(hwnd, nested.Get());
        require(eventually([&] { return named(automation, root, L"Events: nested action.") != nullptr &&
            SendMessageW(hwnd, WM_APP + 60, 24, 0) == 0; }), "Nested popup receives the click and invokes its command");
        check(open->Invoke(), "Reopen palette for outside click");
        click_at(hwnd, {client.left + 12, client.top + 70});
        require(eventually([&] { return SendMessageW(hwnd, WM_APP + 60, 24, 0) == 0; }), "Outside clicks still dismiss the palette");
    }
}
void winui_smoke(IUIAutomation* automation, IUIAutomationElement* root, HWND hwnd,
    const std::filesystem::path& captures) {
    const auto control = [&](const wchar_t* id) {
        auto element = identified(automation, root, id);
        require(element != nullptr, "WinUI experiment exposes its named control");
        return element;
    };
    const auto invoke = [&](const wchar_t* id) {
        check(pattern<IUIAutomationInvokePattern>(control(id).Get(), UIA_InvokePatternId)->Invoke(), "Invoke experiment action");
    };
    auto input = control(L"winui-input");
    auto value = pattern<IUIAutomationValuePattern>(input.Get(), UIA_ValuePatternId);
    set_value(value.Get(), L"Retained native text");
    const auto retained_text = [&] {
        BSTR text{};
        check(value->get_CurrentValue(&text), "Read experiment input");
        const bool matches = text && std::wstring_view(text) == L"Retained native text";
        SysFreeString(text);
        return matches;
    };
    const auto peers = SendMessageW(hwnd, WM_APP + 60, 14, 0);
    require(SendMessageW(hwnd, WM_APP + 60, 11, 0) == 1, "Experiment uses one shared target");
    BOOL enabled{};
    check(control(L"winui-disabled")->get_CurrentIsEnabled(&enabled), "Read disabled example");
    require(!enabled, "Disabled example remains disabled");
    auto checkbox = pattern<IUIAutomationTogglePattern>(control(L"winui-toggle").Get(), UIA_TogglePatternId);
    ToggleState before{}, after{};
    check(checkbox->get_CurrentToggleState(&before), "Read initial checkbox state");
    check(checkbox->Toggle(), "Toggle live checkbox");
    require(eventually([&] { return SUCCEEDED(checkbox->get_CurrentToggleState(&after)) && before != after; }),
        "Checkbox uses real model state");
    for (const auto* id : {L"winui-standard", L"winui-accent", L"winui-subtle"}) {
        const auto previous = name(control(L"winui-events").Get());
        invoke(id);
        require(eventually([&] { return name(control(L"winui-events").Get()) != previous; }), "Buttons update the event output");
    }
    check(checkbox->get_CurrentToggleState(&after), "Read checkbox after sample actions");
    if (after != before) check(checkbox->Toggle(), "Restore edit permission");
    require(eventually([&] {
        BOOL editable{};
        return SUCCEEDED(input->get_CurrentIsEnabled(&editable)) && editable;
    }), "Native input is enabled before the style comparison");
    set_value(value.Get(), L"Retained native text");
    for (int cycle = 0; cycle < 3; ++cycle) {
        const auto previous = name(control(L"winui-style").Get());
        invoke(L"winui-style");
        require(eventually([&] { return name(control(L"winui-style").Get()) != previous; }), "Style selector reports the new style");
        invoke(L"winui-style");
        require(eventually([&] { return name(control(L"winui-style").Get()) == previous; }), "Style selector returns to WinUI");
        invoke(L"winui-theme");
        require(eventually([&] { return SendMessageW(hwnd, WM_APP + 60, 6, 0) == (cycle % 2 == 0 ? 1 : 0); }),
            "Theme selector applies light and dark themes");
        require(retained_text(), "Style and theme changes preserve native text");
        require(SendMessageW(hwnd, WM_APP + 60, 11, 0) == 1 &&
            SendMessageW(hwnd, WM_APP + 60, 14, 0) == peers, "Style switches do not create targets or control peers");
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        suggestion_capture::bitmap(hwnd, nullptr, captures / (cycle % 2 == 0 ? L"winui-light.bmp" : L"winui-dark.bmp"));
    }
    auto high_contrast = pattern<IUIAutomationTogglePattern>(control(L"winui-high-contrast").Get(), UIA_TogglePatternId);
    check(high_contrast->Toggle(), "Enable explicit high contrast");
    require(eventually([&] {
        ToggleState state{};
        return SUCCEEDED(high_contrast->get_CurrentToggleState(&state)) && state == ToggleState_On;
    }), "High-contrast selector reflects the active override");
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    suggestion_capture::bitmap(hwnd, nullptr, captures / L"winui-high-contrast.bmp");
    check(high_contrast->Toggle(), "Restore regular theme");
    require(eventually([&] {
        ToggleState state{};
        return SUCCEEDED(high_contrast->get_CurrentToggleState(&state)) && state == ToggleState_Off;
    }), "High-contrast selector restores the regular theme");
    require(retained_text(), "High contrast preserves native text");
    invoke(L"winui-more");
    require(eventually([&] { return SendMessageW(hwnd, WM_APP + 60, 24, 0) == 1; }), "Experiment opens a real command popup");
    require(control(L"winui-command-menu") != nullptr, "Command popup is accessible");
    suggestion_capture::bitmap(hwnd, nullptr, captures / L"winui-flyout.bmp");
}
void catalog_style_roundtrip(IUIAutomation* automation, IUIAutomationElement* root,
    IUIAutomationElement* example, HWND hwnd) {
    require(example != nullptr, "Materialized catalog example exists before style switches");
    auto style = identified(automation, root, L"gallery-style");
    require(style && name(style.Get()) == L"Style: WinUI", "Full catalog starts each style sweep in WinUI");
    auto invoke = pattern<IUIAutomationInvokePattern>(style.Get(), UIA_InvokePatternId);
    ComPtr<IUIAutomationCacheRequest> cache;
    check(automation->CreateCacheRequest(&cache), "Create catalog state cache");
    const PROPERTYID properties[]{UIA_AutomationIdPropertyId, UIA_ControlTypePropertyId, UIA_NativeWindowHandlePropertyId,
        UIA_IsEnabledPropertyId, UIA_IsPasswordPropertyId, UIA_ValueValuePropertyId,
        UIA_ValueIsReadOnlyPropertyId, UIA_ToggleToggleStatePropertyId,
        UIA_SelectionItemIsSelectedPropertyId, UIA_ExpandCollapseExpandCollapseStatePropertyId,
        UIA_RangeValueValuePropertyId, UIA_RangeValueIsReadOnlyPropertyId,
        UIA_GridRowCountPropertyId, UIA_GridColumnCountPropertyId};
    for (const auto property : properties) check(cache->AddProperty(property), "Cache catalog control state");
    ComPtr<IUIAutomationCondition> all;
    check(automation->CreateTrueCondition(&all), "Create catalog subtree condition");
    const auto elements = [&] {
        ComPtr<IUIAutomationElementArray> found;
        check(example->FindAllBuildCache(TreeScope_Subtree, all.Get(), cache.Get(), &found), "Snapshot catalog peers");
        int count{};
        check(found->get_Length(&count), "Read catalog subtree size");
        std::vector<ComPtr<IUIAutomationElement>> result;
        for (int i = 0; i < count; ++i) {
            ComPtr<IUIAutomationElement> element;
            check(found->GetElement(i, &element), "Read catalog snapshot element");
            BSTR id{};
            check(element->get_CachedAutomationId(&id), "Read catalog snapshot identity");
            // A WinUI-only affordance can hide; application controls and their state must remain.
            const bool clear_action = std::wstring_view(id ? id : L"").starts_with(L"xui-text-clear-");
            SysFreeString(id);
            if (!clear_action) result.push_back(std::move(element));
        }
        return result;
    };
    const auto property_equal = [](IUIAutomationElement* before, IUIAutomationElement* after, PROPERTYID property) {
        VARIANT a{}, b{};
        const auto first = before->GetCachedPropertyValue(property, &a);
        const auto second = after->GetCachedPropertyValue(property, &b);
        bool equal = a.vt == b.vt;
        if (equal) {
            switch (a.vt) {
            case VT_EMPTY: case VT_UNKNOWN: break; // UIA's reserved unsupported-property value.
            case VT_I4: equal = a.lVal == b.lVal; break;
            case VT_BOOL: equal = a.boolVal == b.boolVal; break;
            case VT_R8: equal = a.dblVal == b.dblVal; break;
            case VT_BSTR:
                equal = std::wstring_view(a.bstrVal ? a.bstrVal : L"") ==
                    std::wstring_view(b.bstrVal ? b.bstrVal : L"");
                break;
            default: equal = false; break;
            }
        }
        VariantClear(&a); VariantClear(&b);
        check(first, "Read original cached catalog property");
        check(second, "Read current cached catalog property");
        return equal;
    };
    const auto document_state = [](IUIAutomationElement* element) {
        std::vector<std::wstring> contents;
        BOOL password{};
        check(element->get_CurrentIsPassword(&password), "Read catalog password policy");
        if (password) return contents;
        ComPtr<IUnknown> supported;
        check(element->GetCurrentPattern(UIA_TextPatternId, &supported), "Read catalog document support");
        if (!supported) return contents;
        ComPtr<IUIAutomationTextPattern> text;
        check(supported.As(&text), "Read catalog Text pattern");
        const auto append = [&](IUIAutomationTextRange* range) {
            BSTR value{};
            const auto result = range->GetText(-1, &value);
            contents.emplace_back(value ? value : L"");
            SysFreeString(value);
            check(result, "Read retained catalog document text");
        };
        ComPtr<IUIAutomationTextRange> document;
        check(text->get_DocumentRange(&document), "Read catalog document range");
        append(document.Get());
        ComPtr<IUIAutomationTextRangeArray> selections;
        check(text->GetSelection(&selections), "Read catalog text selection");
        int count{};
        check(selections->get_Length(&count), "Read catalog selection count");
        for (int i = 0; i < count; ++i) {
            ComPtr<IUIAutomationTextRange> selected;
            check(selections->GetElement(i, &selected), "Read catalog selected range");
            append(selected.Get());
        }
        return contents;
    };
    SendMessageW(hwnd, WM_APP + 12, 0, 0);
    const auto before = elements();
    const auto count = before.size();
    require(count > 0, "Style sweep snapshots real catalog controls");
    std::vector<std::vector<std::wstring>> documents;
    for (const auto& element : before) documents.push_back(document_state(element.Get()));
    const auto peers = SendMessageW(hwnd, WM_APP + 60, 14, 0);
    for (const auto* expected : {L"Style: Classic", L"Style: WinUI"}) {
        const auto paints = SendMessageW(hwnd, WM_APP + 60, 0, 0);
        check(invoke->Invoke(), "Switch full catalog visual style through UIA");
        require(eventually([&] {
            return name(style.Get()) == expected && SendMessageW(hwnd, WM_APP + 60, 0, 0) > paints;
        }), "Full catalog reports the new style and paints it");
        require(SendMessageW(hwnd, WM_APP + 60, 11, 0) == 1 &&
            SendMessageW(hwnd, WM_APP + 60, 14, 0) == peers, "Style switches retain one target and the existing peers");
        const auto after = elements();
        require(after.size() == count, "Style switches preserve the catalog's application controls");
        for (std::size_t i = 0; i < count; ++i) {
            const auto& original = before[i];
            const auto& current = after[i];
            BOOL same{};
            check(automation->CompareElements(original.Get(), current.Get(), &same), "Compare catalog control identities");
            require(same, "Style switches retain accessible control identities");
            for (const auto property : properties)
                require(property_equal(original.Get(), current.Get(), property),
                    "Style switches preserve catalog roles, native handles, values, selection and enabled state");
            require(document_state(current.Get()) == documents[static_cast<std::size_t>(i)],
                "Style switches preserve native document contents and text selection");
        }
    }
}
void miller_smoke(IUIAutomation* automation, IUIAutomationElement* root) {
    auto page = identified(automation, root, L"gallery-page-miller-columns");
    require(page != nullptr, "Miller gallery page loads directly");
    auto deep = identified(automation, root, L"gallery-miller-deep-path");
    check(pattern<IUIAutomationInvokePattern>(deep.Get(), UIA_InvokePatternId)->Invoke(), "Show the deep Miller hierarchy");
    auto columns = identified(automation, root, L"gallery-miller-columns");
    auto scroll = pattern<IUIAutomationScrollPattern>(columns.Get(), UIA_ScrollPatternId);
    require(eventually([&] {
        BOOL horizontal{};
        check(scroll->get_CurrentHorizontallyScrollable(&horizontal), "Read horizontal gallery range");
        return horizontal != FALSE;
    }), "The deep gallery path overflows horizontally");
    check(scroll->SetScrollPercent(0, UIA_ScrollPatternNoScroll), "Reveal the first gallery column");
    double percent{};
    check(scroll->get_CurrentHorizontalScrollPercent(&percent), "Read first-column position");
    require(percent == 0, "Gallery scrolling does not snap back to the active descendant");
    check(scroll->SetScrollPercent(100, UIA_ScrollPatternNoScroll), "Reveal the last gallery column");
    check(scroll->get_CurrentHorizontalScrollPercent(&percent), "Read last-column position");
    require(percent == 100, "The gallery can scroll through the complete path");
    auto reset = named(automation, root, L"Reset path", UIA_ButtonControlTypeId);
    check(pattern<IUIAutomationInvokePattern>(reset.Get(), UIA_InvokePatternId)->Invoke(), "Reset the gallery path");
    require(eventually([&] {
        BOOL horizontal{};
        check(scroll->get_CurrentHorizontallyScrollable(&horizontal), "Read reset gallery range");
        return !horizontal;
    }), "Reset removes horizontal overflow without replacing the control");
}
void choose_gallery_page(IUIAutomation* automation, IUIAutomationElement* root, std::size_t index) {
    auto catalog = identified(automation, root, L"gallery-catalog");
    auto item = identified(automation, catalog.Get(), std::to_wstring(index + 1) + L":1");
    check(pattern<IUIAutomationVirtualizedItemPattern>(item.Get(), UIA_VirtualizedItemPatternId)->Realize(),
        "Realize new control gallery page");
    check(pattern<IUIAutomationSelectionItemPattern>(item.Get(), UIA_SelectionItemPatternId)->Select(),
        "Select new control gallery page");
    require(eventually([&] {
        return identified(automation, root, L"gallery-page-" + std::wstring(gallery::entries[index].id)) != nullptr;
    }), "New control gallery page materialized");
}
void winui_controls_smoke(IUIAutomation* automation, IUIAutomationElement* root, HWND window) {
    const auto choose = [&](std::size_t index) { choose_gallery_page(automation, root, index); };
    for (const auto index : {48u, 49u}) {
        choose(index);
        const bool is_switch = index == 48;
        auto control = identified(automation, root, is_switch ? L"gallery-toggle-switch" : L"gallery-toggle-button");
        auto toggle = pattern<IUIAutomationTogglePattern>(control.Get(), UIA_TogglePatternId);
        ToggleState state{};
        check(toggle->get_CurrentToggleState(&state), "Read new toggle initial state");
        require(state == ToggleState_On, "New gallery toggle starts checked");
        check(toggle->Toggle(), "Change new gallery toggle");
        check(toggle->get_CurrentToggleState(&state), "Read changed gallery toggle");
        require(state == ToggleState_Off, "New gallery toggle changed");
        require(eventually([&] {
            return named(automation, root, is_switch ? L"Events: notifications off." : L"Events: preview unpinned.") != nullptr;
        }), "New gallery toggle reports its event");
        auto enable = named(automation, root, is_switch ? L"Enable switch" : L"Enable pin action");
        auto enable_toggle = pattern<IUIAutomationTogglePattern>(enable.Get(), UIA_TogglePatternId);
        check(enable_toggle->Toggle(), "Disable new gallery toggle");
        require(!enabled(control.Get()) && toggle->Toggle() == UIA_E_ELEMENTNOTENABLED,
            "Disabled gallery toggles reject changes");
        check(enable_toggle->Toggle(), "Enable new gallery toggle");
    }
    for (const auto index : {25u, 50u}) {
        choose(index);
        auto progress = identified(automation, root, index == 50 ? L"gallery-progress-ring" : L"foundation-progress");
        require(progress != nullptr, "Gallery exposes real progress control");
        check(pattern<IUIAutomationInvokePattern>(named(automation, root, L"Advance").Get(), UIA_InvokePatternId)->Invoke(),
            "Set determinate progress");
        auto range = pattern<IUIAutomationRangeValuePattern>(progress.Get(), UIA_RangeValuePatternId);
        double value{}; check(range->get_CurrentValue(&value), "Read progress value");
        require(value == 50, "Progress example advances its real value");
        BOOL read_only{}; check(range->get_CurrentIsReadOnly(&read_only), "Read progress permissions");
        require(read_only, "Progress is read-only");
        check(pattern<IUIAutomationInvokePattern>(named(automation, root, L"Indeterminate").Get(), UIA_InvokePatternId)->Invoke(),
            "Set indeterminate progress");
        auto visibility = pattern<IUIAutomationTogglePattern>(
            named(automation, root, L"Show indicator").Get(), UIA_TogglePatternId);
        check(visibility->Toggle(), "Hide progress indicator");
        require(eventually([&] {
            return !identified(automation, root, index == 50 ? L"gallery-progress-ring" : L"foundation-progress");
        }), "Hidden progress indicator leaves the accessible tree");
        check(visibility->Toggle(), "Restore progress indicator");
        require(eventually([&] {
            return identified(automation, root, index == 50 ? L"gallery-progress-ring" : L"foundation-progress") != nullptr;
        }), "Restored progress indicator returns to the accessible tree");
    }
    choose(48);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto paints = SendMessageW(window, WM_APP + 60, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    require(SendMessageW(window, WM_APP + 60, 0, 0) == paints, "Hidden progress gallery pages do not repaint");
}
void parity_controls_smoke(IUIAutomation* automation, IUIAutomationElement* root) {
    choose_gallery_page(automation, root, 51);
    auto checkbox = identified(automation, root, L"gallery-checkbox");
    auto toggle = pattern<IUIAutomationTogglePattern>(checkbox.Get(), UIA_TogglePatternId);
    ToggleState state{};
    check(toggle->get_CurrentToggleState(&state), "Read mixed checkbox state");
    require(state == ToggleState_Indeterminate, "Gallery checkbox starts mixed");
    for (int i = 0; i < 3; ++i) check(toggle->Toggle(), "Cycle three-state checkbox");
    check(toggle->get_CurrentToggleState(&state), "Read cycled checkbox state");
    require(state == ToggleState_Indeterminate, "Three-state cycle returns to mixed");
    auto enable_check = pattern<IUIAutomationTogglePattern>(
        named(automation, root, L"Enable checkbox").Get(), UIA_TogglePatternId);
    check(enable_check->Toggle(), "Disable checkbox");
    require(toggle->Toggle() == UIA_E_ELEMENTNOTENABLED, "Disabled mixed checkbox rejects changes");
    check(enable_check->Toggle(), "Restore checkbox");

    choose_gallery_page(automation, root, 52);
    auto link = identified(automation, root, L"gallery-hyperlink-button");
    CONTROLTYPEID role{};
    check(link->get_CurrentControlType(&role), "Read hyperlink role");
    require(role == UIA_HyperlinkControlTypeId, "Hyperlink has link semantics, not button semantics");
    auto invoke = pattern<IUIAutomationInvokePattern>(link.Get(), UIA_InvokePatternId);
    check(invoke->Invoke(), "Activate callback-only hyperlink");
    require(eventually([&] {
        return named(automation, root, L"Events: help requested. No browser was opened.") != nullptr;
    }), "Hyperlink invokes the application callback");
    auto enable_link = pattern<IUIAutomationTogglePattern>(
        named(automation, root, L"Enable help link").Get(), UIA_TogglePatternId);
    check(enable_link->Toggle(), "Disable hyperlink");
    require(invoke->Invoke() == UIA_E_ELEMENTNOTENABLED, "Disabled hyperlink rejects activation");
    check(enable_link->Toggle(), "Restore hyperlink");

    choose_gallery_page(automation, root, 53);
    auto selector = identified(automation, root, L"gallery-selector-bar");
    auto selection = pattern<IUIAutomationSelectionPattern>(selector.Get(), UIA_SelectionPatternId);
    BOOL multiple{};
    check(selection->get_CurrentCanSelectMultiple(&multiple), "Read selector selection mode");
    require(!multiple, "Selector bar is exclusive");
    auto active = named(automation, selector.Get(), L"Active");
    check(pattern<IUIAutomationSelectionItemPattern>(active.Get(), UIA_SelectionItemPatternId)->Select(),
        "Select a gallery filter");
    require(eventually([&] { return named(automation, root, L"Events: task filter ID 2") != nullptr; }),
        "Selector emits stable choice identity");
    auto archived = named(automation, selector.Get(), L"Archived");
    require(!enabled(archived.Get()) &&
        pattern<IUIAutomationSelectionItemPattern>(archived.Get(), UIA_SelectionItemPatternId)->Select() == UIA_E_ELEMENTNOTENABLED,
        "Disabled selector item cannot become selected");

    choose_gallery_page(automation, root, 54);
    auto badge = identified(automation, root, L"gallery-info-badge");
    BOOL focusable{};
    check(badge->get_CurrentIsKeyboardFocusable(&focusable), "Read badge focus policy");
    require(!focusable, "InfoBadge is not a keyboard target");
    ComPtr<IUnknown> badge_action;
    check(badge->GetCurrentPattern(UIA_InvokePatternId, &badge_action), "Read badge action policy");
    require(!badge_action, "InfoBadge has no command action");
    check(pattern<IUIAutomationInvokePattern>(named(automation, root, L"Add notification").Get(),
        UIA_InvokePatternId)->Invoke(), "Update badge count");
    require(eventually([&] { return named(automation, root, L"Events: notification count 8") != nullptr; }),
        "Gallery updates the real badge count");
    check(pattern<IUIAutomationInvokePattern>(named(automation, root, L"Show dot").Get(),
        UIA_InvokePatternId)->Invoke(), "Show dot badge");
    check(pattern<IUIAutomationInvokePattern>(named(automation, root, L"Show icon").Get(),
        UIA_InvokePatternId)->Invoke(), "Show icon badge");

    choose_gallery_page(automation, root, 55);
    auto menu = identified(automation, root, L"gallery-menu-bar");
    check(menu->get_CurrentControlType(&role), "Read menu bar role");
    require(role == UIA_MenuBarControlTypeId, "MenuBar has menu semantics");
    auto file = named(automation, menu.Get(), L"File");
    check(pattern<IUIAutomationExpandCollapsePattern>(file.Get(), UIA_ExpandCollapsePatternId)->Expand(),
        "Open File menu");
    ComPtr<IUIAutomationElement> save;
    require(eventually([&] { save = named(automation, root, L"Save sample"); return save != nullptr; }),
        "MenuBar opens its retained command menu");
    check(pattern<IUIAutomationInvokePattern>(save.Get(), UIA_InvokePatternId)->Invoke(), "Invoke File command");
    require(eventually([&] { return named(automation, root, L"Events: Sample saved in memory.") != nullptr; }),
        "MenuBar invokes its command snapshot callback");
    auto publish = named(automation, menu.Get(), L"Publish");
    require(publish && !enabled(publish.Get()), "Disabled menu heading is exposed as disabled");
}
std::wstring normalized_code(std::wstring text) {
    for (std::size_t pos = 0; pos < text.size(); ++pos) {
        if (text[pos] != L'\r') continue;
        if (pos + 1 < text.size() && text[pos + 1] == L'\n') text.erase(pos + 1, 1);
        text[pos] = L'\n';
    }
    return text;
}
std::wstring clipboard_text() {
    require(eventually([] { return OpenClipboard(nullptr) != FALSE; }), "Open gallery clipboard");
    const auto data = GetClipboardData(CF_UNICODETEXT);
    const auto value = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
    std::wstring text = value ? value : L"";
    if (value) GlobalUnlock(data);
    CloseClipboard();
    return normalized_code(std::move(text));
}
void reference_smoke(IUIAutomation* automation, IUIAutomationElement* root) {
    for (std::size_t i = 0; i < gallery::entries.size(); ++i) {
        const auto& entry = gallery::entries[i];
        const auto& reference = gallery::references[i];
        const auto suffix = std::wstring(entry.id);
        auto item = identified(automation, root, std::to_wstring(i + 1) + L":1");
        check(pattern<IUIAutomationSelectionItemPattern>(item.Get(), UIA_SelectionItemPatternId)->Select(),
            "Select reference page");
        ComPtr<IUIAutomationElement> example;
        require(eventually([&] { example = identified(automation, root, L"gallery-example-" + suffix); return example != nullptr; }),
            "Reference page materializes");
        auto language = identified(automation, root, L"gallery-language-" + suffix);
        require(language != nullptr, "Every reference page has language tabs");
        auto scroll = pattern<IUIAutomationScrollPattern>(example.Get(), UIA_ScrollPatternId);
        const auto selected_tab = [&](const wchar_t* title) {
            auto tab = named(automation, language.Get(), title, UIA_TabItemControlTypeId);
            BOOL selected{};
            check(pattern<IUIAutomationSelectionItemPattern>(tab.Get(), UIA_SelectionItemPatternId)->get_CurrentIsSelected(&selected),
                "Read active language tab");
            return selected != FALSE;
        };
        require(selected_tab(i == 0 ? L".xui" : L"Rust"),
            "The first page defaults to .xui; eager and deferred pages preserve the selected language");
        require(!identified(automation, root, L"gallery-language-guide-" + suffix), "Language guides are not repeated on pages");
        const wchar_t* names[]{L".xui", L"C#", L"C++", L"Rust"};
        const wchar_t* snippets[]{reference.xui, reference.csharp, entry.code, reference.rust};
        for (int selected = 0; selected != 4; ++selected) {
            auto choice = named(automation, language.Get(), names[selected], UIA_TabItemControlTypeId);
            require(choice != nullptr, "Language tab exists");
            check(pattern<IUIAutomationSelectionItemPattern>(choice.Get(), UIA_SelectionItemPatternId)->Select(),
                "Select example language");
            require(selected_tab(names[selected]), "Selected language tab is active");
            check(scroll->SetScrollPercent(UIA_ScrollPatternNoScroll, 100), "Reveal code after language selection");
            ComPtr<IUIAutomationElement> code;
            const bool updated = eventually([&] {
                check(scroll->SetScrollPercent(UIA_ScrollPatternNoScroll, 100), "Reveal code after deferred layout");
                const auto code_name = std::wstring(entry.title) + L" " + names[selected] + L" code";
                code = named(automation, root, code_name.c_str());
                return code != nullptr;
            });
            if (!updated) std::wcerr << entry.id << L": expected " << names[selected] <<
                L" document, actual: " << (code ? name(code.Get()) : L"(not exposed)") << std::endl;
            require(updated, "Language choice updates accessible document name");
            auto document = pattern<IUIAutomationTextPattern>(code.Get(), UIA_TextPatternId);
            ComPtr<IUIAutomationTextRange> range;
            check(document->get_DocumentRange(&range), "Read selected language document");
            BSTR text{};
            check(range->GetText(-1, &text), "Read complete language excerpt");
            const auto actual = normalized_code(text ? text : L"");
            SysFreeString(text);
            const std::wstring expected = snippets[selected];
            require(actual == expected || actual == expected + L"\n", "Language excerpt matches its reference content");
            VARIANT attribute{};
            check(range->GetAttributeValue(UIA_IsReadOnlyAttributeId, &attribute), "Read code edit policy");
            const bool readonly = attribute.vt == VT_BOOL && attribute.boolVal == VARIANT_TRUE;
            VariantClear(&attribute);
            require(readonly, "Language selection preserves native read-only code");
            auto copy = identified(automation, root, L"gallery-copy-code-" + suffix);
            require(enabled(copy.Get()) == !expected.empty(), "Copy availability matches snippet availability");
            if (!expected.empty()) {
                check(pattern<IUIAutomationInvokePattern>(copy.Get(), UIA_InvokePatternId)->Invoke(), "Copy selected language");
                const auto copied = clipboard_text();
                if (copied != expected && copied != expected + L"\n") {
                    const auto status = identified(automation, root, L"gallery-reference-status-" + suffix);
                    std::wcerr << entry.id << L" " << names[selected] << L": copy status: " <<
                        (status ? name(status.Get()) : L"(not exposed)") <<
                        L"; expected " << expected.size() << L" characters, received " << copied.size() << std::endl;
                }
                require(copied == expected || copied == expected + L"\n", "Copy uses the selected language");
            }
        }
        auto docs = identified(automation, root, L"gallery-docs-" + suffix);
        require(pattern<IUIAutomationInvokePattern>(docs.Get(), UIA_InvokePatternId) != nullptr,
            "Control documentation is an accessible action");
        auto copy_link = identified(automation, root, L"gallery-copy-docs-" + suffix);
        check(pattern<IUIAutomationInvokePattern>(copy_link.Get(), UIA_InvokePatternId)->Invoke(), "Copy documentation URL");
        require(clipboard_text() == gallery::documentation_url(reference.docs),
            "Documentation URL targets the control's handbook page");
        std::wcout << entry.id << L": language examples and documentation passed\n";
    }
    auto first = identified(automation, root, L"1:1");
    check(pattern<IUIAutomationSelectionItemPattern>(first.Get(), UIA_SelectionItemPatternId)->Select(), "Revisit the first example");
    auto language = identified(automation, root, L"gallery-language-forms");
    auto rust = named(automation, language.Get(), L"Rust", UIA_TabItemControlTypeId);
    BOOL selected{};
    check(pattern<IUIAutomationSelectionItemPattern>(rust.Get(), UIA_SelectionItemPatternId)->get_CurrentIsSelected(&selected),
        "Read language on a revisited page");
    require(selected != FALSE, "Revisited pages retain the shared language");
    auto links = identified(automation, root, L"20000:1");
    check(pattern<IUIAutomationExpandCollapsePattern>(links.Get(), UIA_ExpandCollapsePatternId)->Expand(), "Expand Other links");
    for (const auto& link : gallery::navigation_links) {
        auto item = identified(automation, root, std::to_wstring(link.key.id) + L":1");
        require(item && name(item.Get()) == link.title, "Each handbook guide appears under Other links");
        // Selection must not open the browser or replace the active example.
        check(pattern<IUIAutomationSelectionItemPattern>(item.Get(), UIA_SelectionItemPatternId)->Select(), "Select a handbook link");
        require(identified(automation, root, L"gallery-page-forms") != nullptr, "Link selection preserves the example");
        require(pattern<IUIAutomationInvokePattern>(item.Get(), UIA_InvokePatternId) != nullptr, "Handbook links support explicit activation");
    }
}
int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
    const bool global_focus_events = argc == 3 && std::wstring_view(argv[2]) == L"--focus-events";
    const bool search_only = argc == 3 && std::wstring_view(argv[2]) == L"--search-disclosure";
    const bool palette_only = argc == 3 && std::wstring_view(argv[2]) == L"--palette";
    const bool winui_only = argc == 3 && std::wstring_view(argv[2]) == L"--winui";
    const bool winui_catalog = argc == 3 && std::wstring_view(argv[2]) == L"--winui-catalog";
    const bool miller_only = argc == 3 && std::wstring_view(argv[2]) == L"--miller-only";
    const bool controls_only = argc == 3 && std::wstring_view(argv[2]) == L"--controls-only";
    const bool parity_only = argc == 3 && (std::wstring_view(argv[2]) == L"--parity-only" ||
        std::wstring_view(argv[2]) == L"--parity-classic");
    const bool reference_only = argc == 3 && std::wstring_view(argv[2]) == L"--reference-only";
    if (argc != 2 && !global_focus_events && !search_only && !palette_only && !winui_only && !winui_catalog &&
        !miller_only && !controls_only && !parity_only && !reference_only) {
        std::cerr << "Supply xui_gallery.exe [--focus-events | --search-disclosure | --palette | --winui | --winui-catalog | --miller-only | --controls-only | --parity-only | --parity-classic | --reference-only]\n";
        return 1;
    }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 1;
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    int result = 1;
    try {
        Process process;
        std::wstring command = L"\"" + std::wstring(argv[1]) + L"\"";
        if (palette_only) command += L" --page commands";
        if (miller_only) command += L" --page miller-columns";
        if (controls_only) command += L" --winui-catalog --page toggle-switch";
        if (parity_only) {
            command += L" --page checkbox";
            if (std::wstring_view(argv[2]) == L"--parity-only") command += L" --winui-catalog";
        }
        if (winui_only) command += L" --winui";
        if (winui_catalog) command += L" --winui-catalog";
        STARTUPINFOW startup{sizeof(startup)};
        require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
            nullptr, &startup, &process.info) != 0, "Start gallery");
        require(eventually([&] {
            EnumWindows(find_window, reinterpret_cast<LPARAM>(&process));
            return process.window && IsWindowVisible(process.window);
        }), "Find gallery window");
        if (!search_only && !miller_only && !controls_only && !parity_only && !reference_only) require(SetWindowPos(process.window, HWND_TOPMOST, 40, 40, 0, 0,
            SWP_NOSIZE | SWP_NOACTIVATE) != 0, "Protect the owned test window from unrelated occlusion");
        ComPtr<IUIAutomation> automation;
        check(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&automation)), "Create automation");
        ComPtr<IUIAutomationElement> root;
        check(automation->ElementFromHandle(process.window, &root), "Read gallery root");
        if (parity_only) {
            parity_controls_smoke(automation.Get(), root.Get());
            std::cout << "CheckBox, HyperlinkButton, SelectorBar, InfoBadge and MenuBar gallery checks passed\n";
            return 0;
        }
        if (controls_only) {
            winui_controls_smoke(automation.Get(), root.Get(), process.window);
            std::cout << "ToggleSwitch, ToggleButton, ProgressRing and progress gallery checks passed\n";
            return 0;
        }
        if (reference_only) {
            reference_smoke(automation.Get(), root.Get());
            std::cout << "Gallery reference language, code copy, and documentation checks passed\n";
            return 0;
        }
        if (miller_only) {
            miller_smoke(automation.Get(), root.Get());
            std::cout << "Miller gallery deep-path, horizontal scrolling, and reset checks passed\n";
            return 0;
        }
        if (winui_catalog) {
            auto style = identified(automation.Get(), root.Get(), L"gallery-style");
            require(style && name(style.Get()) == L"Style: WinUI", "Full gallery launches initially in WinUI");
        }
        if (winui_only) {
            winui_smoke(automation.Get(), root.Get(), process.window,
                std::filesystem::path(argv[1]).parent_path().parent_path() / L"winui-captures");
            std::cout << "WinUI gallery interaction, native text, theme, and resource checks passed\n";
            return 0;
        }
        if (search_only) {
            search_disclosure(automation.Get(), root.Get());
            std::cout << "Gallery search disclosure UIA checks passed\n";
            return 0;
        }
        if (palette_only) {
            palette_smoke(automation.Get(), root.Get(), process.window,
                std::filesystem::path(argv[1]).parent_path().parent_path() / L"gallery-captures");
            std::cout << "Gallery palette layout, sections, filtering, deactivation, and dismissal passed\n";
            return 0;
        }
        std::cout << "Gallery root ready\n";
        std::cout << "Gallery initial peers=" << SendMessageW(process.window, WM_APP + 60, 14, 0)
            << " GDI=" << GetGuiResources(process.info.hProcess, GR_GDIOBJECTS)
            << " USER=" << GetGuiResources(process.info.hProcess, GR_USEROBJECTS) << '\n';
        uia_test::Subscription subscription{automation};
        ComPtr<uia_test::Events> events;
        events.Attach(new uia_test::Events(process.info.dwProcessId));
        ComPtr<IUIAutomationCacheRequest> event_cache;
        check(automation->CreateCacheRequest(&event_cache), "Create event cache");
        check(event_cache->AddProperty(UIA_ProcessIdPropertyId), "Cache event process");
        check(event_cache->AddProperty(UIA_NamePropertyId), "Cache event name");
        NativeFocusEvents native_focus(process.info.dwProcessId);
        if (global_focus_events)
            check(automation->AddFocusChangedEventHandler(event_cache.Get(), events.Get()), "Subscribe desktop-wide UIA focus events");
        PROPERTYID properties[]{UIA_NamePropertyId, UIA_IsEnabledPropertyId,
            UIA_ToggleToggleStatePropertyId, UIA_ValueValuePropertyId, UIA_HasKeyboardFocusPropertyId};
        check(automation->AddPropertyChangedEventHandlerNativeArray(root.Get(), TreeScope_Subtree,
            event_cache.Get(), events.Get(), properties, static_cast<int>(std::size(properties))), "Subscribe property events");
        check(automation->AddAutomationEventHandler(UIA_Invoke_InvokedEventId, root.Get(),
            TreeScope_Subtree, event_cache.Get(), events.Get()), "Subscribe invoke events");
        std::cout << "Gallery events ready\n";
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

        auto edit = named(automation.Get(), root.Get(), L"Your name", UIA_EditControlTypeId);
        require(edit != nullptr, "Find native edit through the automation tree");
        const HWND edit_hwnd = handle(automation.Get(), edit.Get());
        native_focus.target = edit_hwnd;
        require(edit_hwnd != nullptr, "Find named native edit");
        require(name(edit.Get()) == L"Your name", "Native edit accessible name");
        auto value = pattern<IUIAutomationValuePattern>(edit.Get(), UIA_ValuePatternId);
        std::cout << "Gallery native input ready\n";
        set_value(value.Get(), L"\u65e5\u672c Alex");
        require(eventually([&] { return enabled(save.Get()); }), "Text callback enables Save");
        check(invoke->Invoke(), "Invoke Save");
        require(eventually([&] { return name(greeting.Get()) == L"Hello, \u65e5\u672c Alex!"; }), "Retained label provider updates");
        require(eventually([&] {
            return events->count(UIA_Invoke_InvokedEventId, L"Save greeting") &&
                events->count(UIA_NamePropertyId, L"Hello, \u65e5\u672c Alex!") &&
                events->boolean(UIA_IsEnabledPropertyId, L"Save greeting", true) &&
                events->count(UIA_ValueValuePropertyId, L"Your name");
        }), "External client receives invoke, name, enabled and native value events");
        std::cout << "Gallery initial events received\n";
        auto renamed = named(automation.Get(), root.Get(), L"Hello, \u65e5\u672c Alex!");
        BOOL same{};
        check(automation->CompareElements(greeting.Get(), renamed.Get(), &same), "Compare updated label identity");
        require(same != FALSE, "Property updates preserve accessible identity");
        require(named(automation.Get(), root.Get(), L"Greeting saved for this session.") != nullptr, "Status callback");
        const auto save_hwnd = handle(automation.Get(), save.Get());
        const auto allow_hwnd = handle(automation.Get(), allow.Get());
        const auto light_hwnd = handle(automation.Get(), light.Get());
        require(save_hwnd && allow_hwnd && light_hwnd, "Native host handles");
        for (int cycle = 0; cycle < 12; ++cycle) {
            focus(allow.Get(), "Focus checkbox before external native focus");
            const auto focus_events = native_focus.count;
            if (cycle % 3 == 1) ShowWindow(process.window, SW_MINIMIZE);
            const auto other = CreateWindowExW(0, L"STATIC", L"XUI owned activation probe",
                WS_OVERLAPPEDWINDOW, 0, 0, 160, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            require(other != nullptr, "Create owned activation window");
            ShowWindow(other, SW_SHOW);
            SetForegroundWindow(other);
            try {
                focus(edit.Get(), "External EDIT focus persists after owned window activation");
                require(!IsIconic(process.window), "Native UIA focus restores a minimized host");
            } catch (...) {
                GUITHREADINFO info{sizeof(info)};
                GetGUIThreadInfo(process.info.dwThreadId, &info);
                std::cerr << "server focus=" << info.hwndFocus << " edit=" << edit_hwnd
                    << " foreground=" << GetForegroundWindow() << " host=" << process.window << '\n';
                DestroyWindow(other);
                throw;
            }
            const auto foreground_before_destroy = GetForegroundWindow();
            DestroyWindow(other);
            const auto foreground_after_destroy = GetForegroundWindow();
            try {
                focus(edit.Get(), "External EDIT focus persists after custom focus or minimization");
            } catch (...) {
                std::cerr << "External focus cycle=" << cycle << " minimized=" << (cycle % 3 == 1)
                    << " host=" << process.window << " probe=" << other
                    << " foreground_before_destroy=" << foreground_before_destroy
                    << " foreground_after_destroy=" << foreground_after_destroy << '\n';
                throw;
            }
            require(eventually([&] {
                return native_focus.count > focus_events;
            }), "External native SetFocus delivers an EDIT focus event");
            require(GetForegroundWindow() == process.window, "Native UIA focus activates the application");
            SendMessageW(edit_hwnd, EM_SETSEL, 0, -1);
            constexpr std::wstring_view queued = L"Q\U0001f642";
            INPUT input[6]{};
            for (size_t i = 0; i < queued.size(); ++i) {
                input[2 * i].type = input[2 * i + 1].type = INPUT_KEYBOARD;
                input[2 * i].ki.wScan = input[2 * i + 1].ki.wScan = queued[i];
                input[2 * i].ki.dwFlags = KEYEVENTF_UNICODE;
                input[2 * i + 1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            }
            require(SendInput(6, input, sizeof(INPUT)) == 6, "Send real queued Unicode keyboard input");
            require(eventually([&] {
                BSTR text{};
                check(value->get_CurrentValue(&text), "Read native value after queued input");
                const bool matches = text && std::wstring_view(text) == queued;
                SysFreeString(text);
                return matches;
            }), "Native UIA focus receives eventual queued keyboard input");
        }
        require(eventually([&] { return events->boolean(UIA_HasKeyboardFocusPropertyId, L"Allow greeting updates", true); }),
            "External client receives custom UIA keyboard-focus property events");
        if (global_focus_events)
            require(eventually([&] { return events->count(UIA_AutomationFocusChangedEventId, L"Your name") != 0; }),
                "External client receives native UIA AutomationFocusChanged events");
        focus(allow.Get(), "Focus checkbox before activation restoration");
        const auto activation = CreateWindowExW(0, L"STATIC", L"XUI owned restore probe",
            WS_OVERLAPPEDWINDOW, 0, 0, 160, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(activation != nullptr, "Create activation restoration probe");
        ShowWindow(activation, SW_SHOW);
        SetForegroundWindow(activation);
        SetForegroundWindow(process.window);
        DestroyWindow(activation);
        require(eventually([&] { return focused(allow.Get()); }), "Reactivation restores the last child without a focus request");
        focus(edit.Get(), "Focus native input");
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
        require(eventually([&] { return events->count(UIA_ToggleToggleStatePropertyId, L"Allow greeting updates"); }),
            "External client receives toggle events");
        require(invoke->Invoke() == UIA_E_ELEMENTNOTENABLED, "Disabled callback rejected after property update");
        focus(edit.Get(), "Focus input for disabled traversal");
        key(edit_hwnd, VK_TAB);
        require(eventually([&] { return focused(allow.Get()); }), "Tab skips disabled Save");
        check(toggle->Toggle(), "Toggle preference with UIA");
        require(eventually([&] { return enabled(save.Get()); }), "UIA callback enables Save");
        set_value(value.Get(), L"Keyboard");
        focus(edit.Get(), "Native value update settles before keyboard action");
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
        focus(edit.Get(), "Native value update settles before pointer action");
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
        focus(edit.Get(), "Focus edit for native character input");
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
        auto search = named(automation.Get(), root.Get(), L"Search controls", UIA_EditControlTypeId);
        auto search_value = pattern<IUIAutomationValuePattern>(search.Get(), UIA_ValuePatternId);
        auto catalog_element = identified(automation.Get(), root.Get(), L"gallery-catalog");
        auto catalog_items = identified(automation.Get(), catalog_element.Get(), L"gallery-catalog-items");
        check(catalog_items->get_CurrentControlType(&type), "Read catalog tree role");
        require(type == UIA_TreeControlTypeId, "Catalog uses a real navigation tree");
        auto catalog_selection = pattern<IUIAutomationSelectionPattern>(catalog_items.Get(), UIA_SelectionPatternId);
        BOOL multiple{};
        check(catalog_selection->get_CurrentCanSelectMultiple(&multiple), "Read catalog selection mode");
        require(!multiple, "Navigation is single-select");
        auto next_element = named(automation.Get(), root.Get(), L"Next example");
        auto next = pattern<IUIAutomationInvokePattern>(next_element.Get(), UIA_InvokePatternId);
        const auto catalog_count = [&] {
            return selectable_count(
                identified(automation.Get(), catalog_element.Get(), L"gallery-catalog-items").Get());
        };
        require(catalog_count() == gallery::entries.size(), "Every implemented example has a catalog entry");
        const auto catalog_row = [&](std::size_t index) {
            return identified(automation.Get(), catalog_element.Get(), std::to_wstring(index + 1) + L":1");
        };
        const auto choose = [&](int row) {
            auto cell = catalog_row(static_cast<std::size_t>(row));
            check(pattern<IUIAutomationVirtualizedItemPattern>(cell.Get(), UIA_VirtualizedItemPatternId)->Realize(),
                "Reveal the virtual catalog item");
            auto selection = pattern<IUIAutomationSelectionItemPattern>(cell.Get(), UIA_SelectionItemPatternId);
            check(selection->Select(), "Select real catalog row");
        };
        auto input_group = identified(automation.Get(), catalog_element.Get(), L"1000:1");
        ComPtr<IUnknown> group_selection;
        input_group->GetCurrentPattern(UIA_SelectionItemPatternId, &group_selection);
        require(!group_selection, "Category groups cannot be selected");
        ComPtr<IUIAutomationTreeWalker> walker;
        check(automation->get_RawViewWalker(&walker), "Read navigation tree walker");
        ComPtr<IUIAutomationElement> parent;
        check(walker->GetParentElement(catalog_row(0).Get(), &parent), "Read nested example parent");
        require(name(parent.Get()) == L"Input", "Examples are UIA children of their category");
        auto disclosure = pattern<IUIAutomationExpandCollapsePattern>(input_group.Get(), UIA_ExpandCollapsePatternId);
        check(disclosure->Collapse(), "Collapse actual catalog category");
        require(eventually([&] { return !catalog_row(0); }), "Collapse removes category descendants");
        check(disclosure->Expand(), "Expand actual catalog category");
        require(eventually([&] { return catalog_row(0) != nullptr; }), "Expand restores stable example IDs");
        search_disclosure(automation.Get(), root.Get());
        focus(search.Get(), "Focus catalog search");
        set_value(search_value.Get(), L"collections");
        require(eventually([&] { return catalog_count() == 6; }), "Search filters category names");
        require(identified(automation.Get(), root.Get(), L"gallery-page-files") != nullptr,
            "A hidden selected item previews the first matching example");
        const auto filtered_count = L"6 of " + std::to_wstring(gallery::entries.size()) + L" examples";
        require(named(automation.Get(), root.Get(), filtered_count.c_str(), UIA_TextControlTypeId) != nullptr,
            "Filtered count retains the complete main catalog denominator");
        require(focused(search.Get()), "Search never moves focus per keystroke");
        set_value(search_value.Get(), L"no-such-control-zz");
        require(eventually([&] { return catalog_count() == 0; }), "Empty search exposes no fake controls");
        require(named(automation.Get(), root.Get(), L"No matching controls") != nullptr, "Empty search has a useful state");
        auto home = identified(automation.Get(), catalog_element.Get(), L"10000:1");
        auto appearance = identified(automation.Get(), catalog_element.Get(), L"10001:1");
        require(home && appearance, "Pinned shortcuts survive an empty main filter");
        check(pattern<IUIAutomationSelectionItemPattern>(home.Get(), UIA_SelectionItemPatternId)->Select(), "Select pinned Home");
        require(named(automation.Get(), root.Get(), L"Your name", UIA_EditControlTypeId) != nullptr, "Home opens Forms");
        check(pattern<IUIAutomationSelectionItemPattern>(appearance.Get(), UIA_SelectionItemPatternId)->Select(), "Select pinned Appearance");
        BOOL pinned_selected{};
        check(pattern<IUIAutomationSelectionItemPattern>(home.Get(), UIA_SelectionItemPatternId)->get_CurrentIsSelected(&pinned_selected),
            "Read shared pinned selection");
        require(!pinned_selected, "Footer selection clears header selection");
        require(named(automation.Get(), root.Get(), L"Themes and accessibility", UIA_TextControlTypeId) != nullptr,
            "Appearance opens Themes");
        set_value(search_value.Get(), L"buttons");
        require(eventually([&] { return catalog_count() == 1; }), "Search filters control names");
        require(identified(automation.Get(), root.Get(), L"gallery-page-buttons") != nullptr,
            "A pinned selection previews the first matching main example");
        auto action = named(automation.Get(), root.Get(), L"Run action");
        auto action_invoke = pattern<IUIAutomationInvokePattern>(action.Get(), UIA_InvokePatternId);
        check(action_invoke->Invoke(), "Invoke interactive button example");
        require(named(automation.Get(), root.Get(), L"Events: invoked 1 times.") != nullptr, "Example publishes event output");
        auto enable_action = pattern<IUIAutomationTogglePattern>(
            named(automation.Get(), root.Get(), L"Enable action").Get(), UIA_TogglePatternId);
        check(enable_action->Toggle(), "Change example property");
        require(!enabled(action.Get()) && action_invoke->Invoke() == UIA_E_ELEMENTNOTENABLED, "Example property disables the real control");
        set_value(search_value.Get(), L"");
        require(eventually([&] { return catalog_count() == gallery::entries.size(); }), "Clear restores the catalog");
        choose(0);
        check(next->Invoke(), "Next example uses the filtered catalog");
        auto second_cell = catalog_row(1);
        auto second_selection = pattern<IUIAutomationSelectionItemPattern>(second_cell.Get(), UIA_SelectionItemPatternId);
        BOOL selected{};
        check(second_selection->get_CurrentIsSelected(&selected), "Read next-example selection");
        require(selected != FALSE, "Next example preserves stable selection");
        check(second_selection->RemoveFromSelection(), "Remove the current navigation selection through UIA");
        check(second_selection->get_CurrentIsSelected(&selected), "Read cleared navigation selection");
        require(selected == FALSE, "UIA removal clears the selected navigation item");
        check(second_selection->Select(), "Restore navigation selection through UIA");
        check(second_selection->get_CurrentIsSelected(&selected), "Read restored navigation selection");
        require(selected != FALSE, "UIA selection restores the same stable navigation item");
        focus(catalog_items.Get(), "Focus catalog for real keyboard navigation");
        key(handle(automation.Get(), catalog_items.Get()), VK_DOWN);
        require(eventually([&] {
            auto cell = catalog_row(2);
            if (!cell) return false;
            auto selection = pattern<IUIAutomationSelectionItemPattern>(cell.Get(), UIA_SelectionItemPatternId);
            BOOL selected{}; selection->get_CurrentIsSelected(&selected); return selected != FALSE;
        }), "Arrow key selects another catalog page");
        key(handle(automation.Get(), catalog_items.Get()), VK_RETURN);
        auto choice = named(automation.Get(), root.Get(), L"Allow updates");
        require(choice && eventually([&] { return focused(choice.Get()); }), "Enter reveals and focuses the selected example");
        auto collapse = named(automation.Get(), catalog_element.Get(), L"Collapse navigation");
        check(pattern<IUIAutomationInvokePattern>(collapse.Get(), UIA_InvokePatternId)->Invoke(), "Collapse gallery pane");
        require(eventually([&] {
            return named(automation.Get(), catalog_element.Get(), L"Expand navigation") &&
                !named(automation.Get(), catalog_element.Get(), L"Search controls", UIA_EditControlTypeId);
        }), "Collapsed pane hides the native search");
        require(identified(automation.Get(), catalog_element.Get(), L"10000:1") &&
            identified(automation.Get(), catalog_element.Get(), L"10001:1"), "Collapsed pane keeps pinned shortcuts");
        focus(next_element.Get(), "Focus gallery before Ctrl+F");
        INPUT shortcut[4]{};
        for (auto& input : shortcut) input.type = INPUT_KEYBOARD;
        shortcut[0].ki.wVk = VK_CONTROL;
        shortcut[1].ki.wVk = 'F';
        shortcut[2].ki.wVk = 'F'; shortcut[2].ki.dwFlags = KEYEVENTF_KEYUP;
        shortcut[3].ki.wVk = VK_CONTROL; shortcut[3].ki.dwFlags = KEYEVENTF_KEYUP;
        require(SendInput(4, shortcut, sizeof(INPUT)) == 4, "Send Ctrl+F shortcut");
        require(eventually([&] { return focused(search.Get()); }), "Ctrl+F expands navigation and focuses the real search EDIT");
        choose(0);
        focus(next_element.Get(), "Stop native caret before gallery navigation");
        const auto captures = std::filesystem::path(argv[1]).parent_path().parent_path() / L"gallery-captures";
        for (std::size_t i = 0; i < gallery::entries.size(); ++i) {
            choose(static_cast<int>(i));
            require(eventually([&] { return named(automation.Get(), root.Get(), gallery::entries[i].title, UIA_TextControlTypeId) != nullptr; }),
                "Foundation page builds on first use");
            const auto& entry = gallery::entries[i];
            const auto example_name = std::wstring(entry.title) + L" example";
            auto example = named(automation.Get(), root.Get(), example_name.c_str());
            auto scroll = pattern<IUIAutomationScrollPattern>(example.Get(), UIA_ScrollPatternId);
            BOOL scrollable{};
            check(scroll->get_CurrentVerticallyScrollable(&scrollable), "Read example scrolling");
            if (i == 0) {
                auto language = identified(automation.Get(), root.Get(), L"gallery-language-forms");
                auto cpp = named(automation.Get(), language.Get(), L"C++", UIA_TabItemControlTypeId);
                check(pattern<IUIAutomationSelectionItemPattern>(cpp.Get(), UIA_SelectionItemPatternId)->Select(),
                    "Choose C++ for the catalog excerpt sweep");
            }
            if (scrollable) check(scroll->SetScrollPercent(UIA_ScrollPatternNoScroll, 100), "Reveal the code block");
            const auto code_name = std::wstring(entry.title) + L" C++ code";
            ComPtr<IUIAutomationElement> code;
            require(eventually([&] { code = named(automation.Get(), root.Get(), code_name.c_str()); return code != nullptr; }),
                "Every gallery page exposes a code document");
            auto code_text = pattern<IUIAutomationTextPattern>(code.Get(), UIA_TextPatternId);
            ComPtr<IUIAutomationTextRange> range;
            check(code_text->get_DocumentRange(&range), "Read code document range");
            BSTR source{};
            check(range->GetText(-1, &source), "Read code text");
            std::wstring actual = source ? source : L"";
            SysFreeString(source);
            for (std::size_t pos = 0; pos < actual.size(); ++pos) {
                if (actual[pos] != L'\r') continue;
                if (pos + 1 < actual.size() && actual[pos + 1] == L'\n') actual.erase(pos + 1, 1);
                actual[pos] = L'\n';
            }
            const std::wstring expected = entry.code;
            require(actual == expected || actual == expected + L"\n", "Code blocks preserve the complete excerpt and line breaks");
            VARIANT font{}, readonly{};
            check(range->GetAttributeValue(UIA_FontNameAttributeId, &font), "Read code font");
            const bool monospace = font.vt == VT_BSTR && std::wstring_view(font.bstrVal) == L"Consolas";
            VariantClear(&font);
            require(monospace, "Every code block uses a real monospace font");
            check(range->GetAttributeValue(UIA_IsReadOnlyAttributeId, &readonly), "Read code edit policy");
            const bool is_readonly = readonly.vt == VT_BOOL && readonly.boolVal == VARIANT_TRUE;
            VariantClear(&readonly);
            require(is_readonly, "Code blocks are read-only, not disabled");
            if (i == 0 || i == 17) {
                check(range->MoveEndpointByRange(TextPatternRangeEndpoint_End, range.Get(), TextPatternRangeEndpoint_Start),
                    "Collapse code selection");
                int moved{};
                check(range->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, 4, &moved),
                    "Select a code fragment");
                require(moved == 4, "Code selection advances by characters");
                check(range->Select(), "Select code text");
                ComPtr<IUIAutomationTextRangeArray> selections;
                check(code_text->GetSelection(&selections), "Read native code selection");
                ComPtr<IUIAutomationTextRange> selected_range;
                check(selections->GetElement(0, &selected_range), "Read selected code fragment");
                BSTR selected_text{};
                check(selected_range->GetText(-1, &selected_text), "Read selected code text");
                const bool matches = selected_text && std::wstring_view(selected_text) == expected.substr(0, 4);
                SysFreeString(selected_text);
                require(matches, "Eager and deferred code blocks support partial text selection");
                const auto code_hwnd = handle(automation.Get(), code.Get());
                SendMessageW(code_hwnd, WM_COPY, 0, 0);
                require(eventually([&] { return OpenClipboard(nullptr) != FALSE; }), "Open copied code");
                const auto data = GetClipboardData(CF_UNICODETEXT);
                const auto copied = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
                const bool copied_selection = copied && std::wstring_view(copied) == expected.substr(0, 4);
                if (copied) GlobalUnlock(data);
                CloseClipboard();
                require(copied_selection, "Native copy copies only selected code");
            }
            if (scrollable) check(scroll->SetScrollPercent(UIA_ScrollPatternNoScroll, 0), "Restore the example viewport");
            if (winui_catalog) catalog_style_roundtrip(automation.Get(), root.Get(), example.Get(), process.window);
        }
        choose(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const auto peers = SendMessageW(process.window, WM_APP + 60, 14, 0);
        auto gdi = GetGuiResources(process.info.hProcess, GR_GDIOBJECTS);
        auto user = GetGuiResources(process.info.hProcess, GR_USEROBJECTS);
        std::cout << "Gallery warm peers=" << peers << " GDI=" << gdi << " USER=" << user << '\n';
        for (int round = 0; round < 3; ++round) {
            for (std::size_t i = 0; i < gallery::entries.size(); ++i) {
                const auto page_gdi_before = GetGuiResources(process.info.hProcess, GR_GDIOBJECTS);
                choose(static_cast<int>(i));
                if (round && GetGuiResources(process.info.hProcess, GR_GDIOBJECTS) != page_gdi_before)
                    std::cout << "Page " << i << " GDI " << page_gdi_before << " -> " << GetGuiResources(process.info.hProcess, GR_GDIOBJECTS) << '\n';
                require(eventually([&] {
                    BOOL offscreen{};
                    greeting->get_CurrentIsOffscreen(&offscreen);
                    return (offscreen != FALSE) == (i != 0);
                }), "Inactive page is hidden from the viewport");
                if (i == 9 && round == 0) {
                    auto grid = named(automation.Get(), root.Get(), L"Synthetic data");
                    auto grid_pattern = pattern<IUIAutomationGridPattern>(grid.Get(), UIA_GridPatternId);
                    int rows{}; check(grid_pattern->get_CurrentRowCount(&rows), "Read synthetic rows");
                    require(rows == 100000, "Grid example exposes 100,000 provider rows");
                    ComPtr<IUIAutomationElement> last;
                    check(grid_pattern->GetItem(99999, 1, &last), "Get virtual last cell");
                    require(name(last.Get()) == L"1600000 bytes", "Grid calculates the last row without retained cells");
                }
                if (!round && std::wstring_view(gallery::entries[i].id) == L"miller-columns")
                    miller_smoke(automation.Get(), root.Get());
                if (!round && (i == 0 || i == 9 || i == 15 || i >= 17)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    suggestion_capture::bitmap(process.window, nullptr, captures / (std::wstring(gallery::entries[i].id) + L"-dark.bmp"));
                }
                if (!round && i == 17) {
                    auto group = named(automation.Get(), root.Get(), L"View mode", UIA_GroupControlTypeId);
                    auto selection = pattern<IUIAutomationSelectionPattern>(group.Get(), UIA_SelectionPatternId);
                    auto details = named(automation.Get(), group.Get(), L"Details", UIA_RadioButtonControlTypeId);
                    check(pattern<IUIAutomationSelectionItemPattern>(details.Get(), UIA_SelectionItemPatternId)->Select(), "Gallery radio selection");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: view ID 20") != nullptr; }), "Radio gallery event");
                }
                if (i == 18) {
                    auto combo = named(automation.Get(), root.Get(), L"Output format", UIA_ComboBoxControlTypeId);
                    check(pattern<IUIAutomationExpandCollapsePattern>(combo.Get(), UIA_ExpandCollapsePatternId)->Expand(), "Gallery combo expands");
                    auto choices = named(automation.Get(), root.Get(), L"Output format choices", UIA_ListControlTypeId);
                    require(choices != nullptr, "Gallery combo uses accessible retained list");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"combo-open-dark.bmp");
                    key(handle(automation.Get(), choices.Get()), VK_HOME);
                    key(handle(automation.Get(), choices.Get()), VK_DOWN);
                    key(handle(automation.Get(), choices.Get()), VK_RETURN);
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: committed format ID 22") != nullptr; }), "Gallery combo commits ID");
                }
                if (i == 19) {
                    auto anchor = named(automation.Get(), root.Get(), L"Open retained popup");
                    check(pattern<IUIAutomationInvokePattern>(anchor.Get(), UIA_InvokePatternId)->Invoke(), "Gallery retained popup opens");
                    auto popup_edit = named(automation.Get(), root.Get(), L"Popup native input", UIA_EditControlTypeId);
                    require(popup_edit != nullptr, "Popup contains real native EDIT");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"popup-open-dark.bmp");
                    key(handle(automation.Get(), popup_edit.Get()), VK_ESCAPE);
                    require(eventually([&] { return SendMessageW(process.window, WM_APP + 60, 24, 0) == 0; }), "Gallery popup cancels");
                }
                if (!round && i == 20) {
                    auto help = named(automation.Get(), root.Get(), L"Hover or focus for help");
                    focus(help.Get(), "Focus gallery tooltip target");
                    require(eventually([&] { return SendMessageW(process.window, WM_APP + 60, 25, 0) != 0; }), "Gallery tooltip delay fires");
                    require(focused(help.Get()), "Gallery tooltip never takes focus");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"tooltip-open-dark.bmp");
                    focus(next_element.Get(), "Dismiss help before navigation");
                }
                if (!round && i == 21) {
                    auto toggle_action = named(automation.Get(), root.Get(), L"Toggle action", UIA_ButtonControlTypeId);
                    check(pattern<IUIAutomationTogglePattern>(toggle_action.Get(), UIA_TogglePatternId)->Toggle(), "Gallery toggle action");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: action on.") != nullptr; }), "Toggle gallery event");
                }
                if (!round && i == 22) {
                    auto number = named(automation.Get(), root.Get(), L"Copies", UIA_SpinnerControlTypeId);
                    set_value(pattern<IUIAutomationValuePattern>(number.Get(), UIA_ValuePatternId).Get(), L"8");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: copies 8.000000") != nullptr; }), "Numeric gallery event");
                }
                if (!round && i == 23) {
                    auto range = named(automation.Get(), root.Get(), L"Horizontal scale", UIA_SliderControlTypeId);
                    check(pattern<IUIAutomationRangeValuePattern>(range.Get(), UIA_RangeValuePatternId)->SetValue(65), "Gallery range changes");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: committed 65.000000") != nullptr; }), "Range gallery event");
                }
                if (!round && i == 24) {
                    auto expander = named(automation.Get(), root.Get(), L"Details", UIA_GroupControlTypeId);
                    auto expand = pattern<IUIAutomationExpandCollapsePattern>(expander.Get(), UIA_ExpandCollapsePatternId);
                    check(expand->Collapse(), "Gallery disclosure collapses");
                    check(expand->Expand(), "Gallery disclosure expands");
                }
                if (!round && i == 25) {
                    auto capacity = named(automation.Get(), root.Get(), L"Storage capacity", UIA_ProgressBarControlTypeId);
                    auto range = pattern<IUIAutomationRangeValuePattern>(capacity.Get(), UIA_RangeValuePatternId);
                    double capacity_value{}; check(range->get_CurrentValue(&capacity_value), "Gallery capacity value");
                    require(capacity_value == 48, "Gallery uses real capacity API");
                }
                if (!round && (i == 48 || i == 49)) {
                    const bool is_switch = i == 48;
                    auto control = identified(automation.Get(), root.Get(),
                        is_switch ? L"gallery-toggle-switch" : L"gallery-toggle-button");
                    auto gallery_toggle = pattern<IUIAutomationTogglePattern>(control.Get(), UIA_TogglePatternId);
                    ToggleState gallery_state{};
                    check(gallery_toggle->get_CurrentToggleState(&gallery_state), "Read gallery toggle initial state");
                    require(gallery_state == ToggleState_On, "New gallery toggles start checked");
                    check(gallery_toggle->Toggle(), "Change new gallery toggle");
                    require(eventually([&] {
                        return named(automation.Get(), root.Get(),
                            is_switch ? L"Events: notifications off." : L"Events: preview unpinned.") != nullptr;
                    }), "New gallery toggles report accepted state changes");
                    auto enable = named(automation.Get(), root.Get(), is_switch ? L"Enable switch" : L"Enable pin action");
                    check(pattern<IUIAutomationTogglePattern>(enable.Get(), UIA_TogglePatternId)->Toggle(), "Disable new gallery toggle");
                    require(!enabled(control.Get()) && gallery_toggle->Toggle() == UIA_E_ELEMENTNOTENABLED,
                        "Disabled gallery toggles reject automation changes");
                    check(pattern<IUIAutomationTogglePattern>(enable.Get(), UIA_TogglePatternId)->Toggle(), "Restore new gallery toggle");
                }
                if (!round && i == 50) {
                    auto ring = identified(automation.Get(), root.Get(), L"gallery-progress-ring");
                    require(ring != nullptr, "Gallery exposes progress ring");
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Advance").Get(),
                        UIA_InvokePatternId)->Invoke(), "Advance ring to determinate progress");
                    auto range = pattern<IUIAutomationRangeValuePattern>(ring.Get(), UIA_RangeValuePatternId);
                    double ring_value{}; check(range->get_CurrentValue(&ring_value), "Read progress ring value");
                    require(ring_value == 50, "Ring and bar share the range model");
                    BOOL read_only{}; check(range->get_CurrentIsReadOnly(&read_only), "Read ring range permissions");
                    require(read_only, "Progress ring is read-only");
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Indeterminate").Get(),
                        UIA_InvokePatternId)->Invoke(), "Restore indeterminate ring");
                    auto visible = pattern<IUIAutomationTogglePattern>(
                        named(automation.Get(), root.Get(), L"Show indicator").Get(), UIA_TogglePatternId);
                    check(visible->Toggle(), "Hide ring through the gallery");
                    require(eventually([&] { return !identified(automation.Get(), root.Get(), L"gallery-progress-ring"); }),
                        "Hidden ring leaves the accessible tree");
                    check(visible->Toggle(), "Show ring through the gallery");
                }
                if (!round && i == 26) {
                    auto items = named(automation.Get(), root.Get(), L"Synthetic items", UIA_ListControlTypeId);
                    auto container = pattern<IUIAutomationItemContainerPattern>(items.Get(), UIA_ItemContainerPatternId);
                    VARIANT id{}; id.vt = VT_BSTR; id.bstrVal = SysAllocString(L"9:1");
                    ComPtr<IUIAutomationElement> item;
                    const auto hr = container->FindItemByProperty(nullptr, UIA_AutomationIdPropertyId, id, &item); VariantClear(&id);
                    check(hr, "Gallery virtual stable identity");
                    check(pattern<IUIAutomationSelectionItemPattern>(item.Get(), UIA_SelectionItemPatternId)->Select(), "Gallery item selection");
                    focus(items.Get(), "Focus collection for inline action");
                    key(handle(automation.Get(), items.Get()), VK_F2);
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: inline item 9") != nullptr; }), "Gallery inline action has separate output");
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Tiles", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Gallery tile presentation");
                    BOOL item_selected{}; pattern<IUIAutomationSelectionItemPattern>(item.Get(), UIA_SelectionItemPatternId)->get_CurrentIsSelected(&item_selected);
                    require(item_selected, "Presentation change preserves selected identity");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"items-tiles-dark.bmp");
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Groups", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Gallery grouped presentation");
                }
                if (!round && i == 27) {
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Expand first branch", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Gallery lazy branch");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: cached branch 1, 100,000 virtual children.") != nullptr; }), "Gallery lazy provider reports output");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"tree-expanded-dark.bmp");
                }
                if (!round && i == 28) {
                    auto navigation = named(automation.Get(), root.Get(), L"Adaptive navigation", UIA_ListControlTypeId);
                    const auto peer = handle(automation.Get(), navigation.Get());
                    check(pattern<IUIAutomationTogglePattern>(named(automation.Get(), root.Get(), L"Compact recipe", UIA_CheckBoxControlTypeId).Get(), UIA_TogglePatternId)->Toggle(), "Gallery compact layout");
                    check(pattern<IUIAutomationTogglePattern>(named(automation.Get(), root.Get(), L"Overlay navigation", UIA_CheckBoxControlTypeId).Get(), UIA_TogglePatternId)->Toggle(), "Gallery overlay layout");
                    require(handle(automation.Get(), navigation.Get()) == peer, "Adaptive gallery reuses the navigation HWND");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"adaptive-overlay-dark.bmp");
                }
                if (!round && i == 29) {
                    auto grid = named(automation.Get(), root.Get(), L"Filterable data", UIA_DataGridControlTypeId);
                    auto header_filter = named(automation.Get(), grid.Get(), L"Item filter", UIA_EditControlTypeId);
                    check(pattern<IUIAutomationInvokePattern>(header_filter.Get(), UIA_InvokePatternId)->Invoke(), "Gallery header filter popup");
                    auto editor = named(automation.Get(), root.Get(), L"Header filter", UIA_EditControlTypeId);
                    set_value(pattern<IUIAutomationValuePattern>(editor.Get(), UIA_ValuePatternId).Get(), L"even");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"grid-filter-open-dark.bmp");
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Apply filter", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Gallery applies header filter");
                    auto table = pattern<IUIAutomationGridPattern>(grid.Get(), UIA_GridPatternId);
                    int rows{}; check(table->get_CurrentRowCount(&rows), "Filtered table count"); require(rows == 50000, "Gallery filter has a real data effect");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: even-ID filter, 50,000 rows.") != nullptr; }), "Gallery filter output");
                }
                if (!round && i == 30) {
                    auto open = named(automation.Get(), root.Get(), L"Open command palette", UIA_ButtonControlTypeId);
                    focus(open.Get(), "Focus palette opener");
                    auto open_action = pattern<IUIAutomationInvokePattern>(open.Get(), UIA_InvokePatternId);
                    check(open_action->Invoke(), "Gallery command palette opens");
                    auto command_search = named(automation.Get(), root.Get(), L"Search commands", UIA_EditControlTypeId);
                    require(eventually([&] { return focused(command_search.Get()); }), "Palette immediately focuses native search");
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Close command palette", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Palette close button dismisses");
                    require(eventually([&] { return focused(open.Get()); }), "Palette close returns focus to opener");
                    check(open_action->Invoke(), "Gallery palette reopens");
                    auto menu = named(automation.Get(), root.Get(), L"Gallery commands", UIA_MenuControlTypeId);
                    require(menu != nullptr, "Gallery rich menu has native UIA Menu role");
                    auto pin = named(automation.Get(), menu.Get(), L"Pin", UIA_ButtonControlTypeId);
                    check(pattern<IUIAutomationInvokePattern>(pin.Get(), UIA_InvokePatternId)->Invoke(), "Gallery pin action");
                    command_search = named(automation.Get(), root.Get(), L"Search commands", UIA_EditControlTypeId);
                    set_value(pattern<IUIAutomationValuePattern>(command_search.Get(), UIA_ValuePatternId).Get(), L"Nested");
                    require(eventually([&] { return named(automation.Get(), menu.Get(), L"Nested action", UIA_MenuItemControlTypeId) != nullptr; }), "Native command search filters by stable record");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"commands-search-dark.bmp");
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), menu.Get(), L"Nested action", UIA_MenuItemControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Search result invokes shared command");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: nested action.") != nullptr; }), "Command gallery event output");
                    auto more = named(automation.Get(), root.Get(), L"More commands", UIA_ButtonControlTypeId);
                    check(pattern<IUIAutomationInvokePattern>(more.Get(), UIA_InvokePatternId)->Invoke(), "Toolbar overflow opens");
                    auto overflow = named(automation.Get(), root.Get(), L"Toolbar overflow", UIA_MenuControlTypeId);
                    require(overflow != nullptr, "Overflow uses shared menu API");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"commands-overflow-dark.bmp");
                    key(handle(automation.Get(), overflow.Get()), VK_ESCAPE);
                }
                if (!round && i == 31) {
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Choose location", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Rich location picker opens");
                    auto editor = named(automation.Get(), root.Get(), L"Find a location", UIA_EditControlTypeId);
                    set_value(pattern<IUIAutomationValuePattern>(editor.Get(), UIA_ValuePatternId).Get(), L"sample");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Item 4", UIA_ListItemControlTypeId) != nullptr; }), "Location provider supplies rich virtual rows");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"breadcrumb-picker-dark.bmp");
                    key(handle(automation.Get(), editor.Get()), VK_ESCAPE);
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: ready") != nullptr; }), "Cancel sends no location command");
                }
                if (!round && i == 32) {
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"View settings", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "View recipe opens");
                    auto choices = named(automation.Get(), root.Get(), L"Presentation", UIA_GroupControlTypeId);
                    auto tiles = named(automation.Get(), choices.Get(), L"Tiles", UIA_RadioButtonControlTypeId);
                    check(pattern<IUIAutomationSelectionItemPattern>(tiles.Get(), UIA_SelectionItemPatternId)->Select(), "View recipe selects tiles");
                    auto size = named(automation.Get(), root.Get(), L"Item size", UIA_SliderControlTypeId);
                    check(pattern<IUIAutomationRangeValuePattern>(size.Get(), UIA_RangeValuePatternId)->SetValue(80), "View recipe changes actual item size");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"navigation-view-picker-dark.bmp");
                    key(handle(automation.Get(), size.Get()), VK_ESCAPE);
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"navigation-tiles-dark.bmp");
                }
                if (!round && i == 33) {
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Discover synthetic Shell commands", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Synthetic Shell discovery");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: 2 synthetic commands discovered. No verb ran.") != nullptr; }), "Discovery never invokes a verb");
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Invoke synthetic inspect", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Explicit synthetic Shell action");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: synthetic verb 1") != nullptr; }), "Synthetic Shell verb identity");
                }
                if (!round && i == 34) {
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Select Preview tab", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Real titlebar tab selection");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: titlebar tab 2") != nullptr; }), "Titlebar reuses TabStrip selection callback");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"titlebar-selected-dark.bmp");
                }
                if (!round && i == 35) {
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Open content dialog", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Gallery opens a modal document dialog");
                    auto dialog = named(automation.Get(), root.Get(), L"Save document", UIA_WindowControlTypeId);
                    BOOL modal{}; check(pattern<IUIAutomationWindowPattern>(dialog.Get(), UIA_WindowPatternId)->get_CurrentIsModal(&modal), "Gallery dialog Window pattern");
                    require(modal, "Gallery dialog isolates owner input");
                    auto accept = named(automation.Get(), dialog.Get(), L"OK", UIA_ButtonControlTypeId);
                    check(pattern<IUIAutomationInvokePattern>(accept.Get(), UIA_InvokePatternId)->Invoke(), "Invalid gallery dialog submission");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Error: Enter a document title.", UIA_StatusBarControlTypeId) != nullptr; }), "Gallery validation has visible and accessible state");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"dialog-invalid-dark.bmp");
                    auto editor = named(automation.Get(), dialog.Get(), L"Document title", UIA_EditControlTypeId);
                    set_value(pattern<IUIAutomationValuePattern>(editor.Get(), UIA_ValuePatternId).Get(), L"Owned gallery fixture");
                    check(pattern<IUIAutomationInvokePattern>(accept.Get(), UIA_InvokePatternId)->Invoke(), "Valid gallery dialog submission");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: dialog saved") != nullptr; }), "Gallery dialog result callback");
                }
                if (!round && i == 36) {
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Show warning", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Gallery status warning");
                    auto status = named(automation.Get(), root.Get(), L"Warning: The document has unsaved changes.", UIA_StatusBarControlTypeId);
                    require(status != nullptr, "Gallery severity is accessible");
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), status.Get(), L"Inspect", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Independent status action");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: status action") != nullptr; }), "Status action callback");
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), status.Get(), L"Dismiss message", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Dismiss gallery status");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: status dismissed") != nullptr; }), "Status dismiss callback");
                }
                if (!round && i == 37) {
                    auto editor = named(automation.Get(), root.Get(), L"Multiline notes");
                    require(pattern<IUIAutomationTextPattern>(editor.Get(), UIA_TextPatternId) != nullptr, "Gallery uses native RichEdit Text pattern");
                    focus(editor.Get(), "Focus multiline document"); PostMessageW(handle(automation.Get(), editor.Get()), WM_CHAR, L'X', 0);
                    const auto length = std::wstring(L"Native Unicode notes\rSecond paragraph: \u65e5\u672c\u8a9e \U0001f642\rSelect, edit, undo, and scroll.").size() + 1;
                    require(eventually([&] { return named(automation.Get(), root.Get(), (L"Events: document length " + std::to_wstring(length)).c_str()) != nullptr; }), "Gallery native document callback");
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Undo document edit", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Gallery native undo");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"multiline-edited-dark.bmp");
                }
                if (!round && i == 38) {
                    auto password = named(automation.Get(), root.Get(), L"Test password", UIA_EditControlTypeId);
                    auto password_value = pattern<IUIAutomationValuePattern>(password.Get(), UIA_ValuePatternId);
                    set_value(password_value.Get(), L"gallery-fixture");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: password changed (value hidden)") != nullptr; }), "Gallery password callback has no value");
                    check(pattern<IUIAutomationTogglePattern>(named(automation.Get(), root.Get(), L"Reveal test password", UIA_CheckBoxControlTypeId).Get(), UIA_TogglePatternId)->Toggle(), "Explicit gallery reveal");
                    BOOL secure{}; check(password->get_CurrentIsPassword(&secure), "Revealed gallery password retains native password semantics"); require(secure, "Gallery reveal is not a plaintext EDIT");
                    BSTR secret{}; password_value->get_CurrentValue(&secret); const bool empty = !secret || SysStringLen(secret) == 0; SysFreeString(secret);
                    require(empty, "Gallery password UIA cannot read the reveal preview");
                    check(pattern<IUIAutomationInvokePattern>(named(automation.Get(), root.Get(), L"Clear password", UIA_ButtonControlTypeId).Get(), UIA_InvokePatternId)->Invoke(), "Clear the owned password fixture");
                }
                if (!round && i == 39) {
                    auto rich = named(automation.Get(), root.Get(), L"Rich document");
                    auto document = pattern<IUIAutomationTextPattern>(rich.Get(), UIA_TextPatternId);
                    ComPtr<IUIAutomationTextRange> range; check(document->get_DocumentRange(&range), "Gallery native rich range");
                    ComPtr<IUIAutomationTextRange> heading;
                    const auto title = SysAllocString(L"Styled document");
                    const auto found = range->FindText(title, FALSE, FALSE, &heading); SysFreeString(title);
                    check(found, "Find native styled run"); require(heading != nullptr, "Native heading exists");
                    VARIANT weight{}; check(heading->GetAttributeValue(UIA_FontWeightAttributeId, &weight), "Native styled run font weight");
                    require(weight.vt == VT_I4 && weight.lVal >= 700, "Rich gallery uses actual bold formatting"); VariantClear(&weight);
                    check(pattern<IUIAutomationTogglePattern>(named(automation.Get(), root.Get(), L"Edit rich document", UIA_CheckBoxControlTypeId).Get(), UIA_TogglePatternId)->Toggle(), "Gallery switches native read-only mode");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"rich-editable-dark.bmp");
                }
                if (!round && i == 40) {
                    auto date = named(automation.Get(), root.Get(), L"Document date");
                    focus(date.Get(), "Focus native gallery date"); key(handle(automation.Get(), date.Get()), VK_UP);
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: date day 13") != nullptr; }), "Gallery date uses actual native editing");
                    auto calendar = named(automation.Get(), root.Get(), L"Document calendar");
                    focus(calendar.Get(), "Focus native calendar"); key(handle(automation.Get(), calendar.Get()), VK_RIGHT);
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: calendar day 14") != nullptr; }), "Gallery calendar changes the retained date");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"date-edited-dark.bmp");
                }
                if (!round && i == 41) {
                    auto red = named(automation.Get(), root.Get(), L"Red", UIA_SpinnerControlTypeId);
                    check(pattern<IUIAutomationRangeValuePattern>(red.Get(), UIA_RangeValuePatternId)->SetValue(42), "Gallery red channel");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: RGBA 42, 100, 230, 180") != nullptr; }), "Gallery retained RGBA callback");
                    auto alpha = named(automation.Get(), root.Get(), L"Alpha", UIA_SpinnerControlTypeId);
                    check(pattern<IUIAutomationRangeValuePattern>(alpha.Get(), UIA_RangeValuePatternId)->SetValue(80), "Gallery alpha channel");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: RGBA 42, 100, 230, 80") != nullptr; }), "Alpha changes actual color state");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"color-alpha-dark.bmp");
                }
                if (!round && i == 42) {
                    auto item = named(automation.Get(), root.Get(), L"Gold ellipse");
                    check(pattern<IUIAutomationSelectionItemPattern>(item.Get(), UIA_SelectionItemPatternId)->Select(), "Select gallery vector semantic child");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: selected shape 12") != nullptr; }), "Gallery vector callback identity");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"vector-selected-dark.bmp");
                }
                if (!round && i == 43) {
                    auto item = named(automation.Get(), root.Get(), L"Authored west marker: 10 N, 179 W");
                    check(pattern<IUIAutomationSelectionItemPattern>(item.Get(), UIA_SelectionItemPatternId)->Select(), "Select gallery geographic marker");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: selected map marker 102") != nullptr; }), "Gallery map callback identity");
                    auto zoom = named(automation.Get(), root.Get(), L"Map zoom in"); focus(zoom.Get(), "Focus gallery map zoom");
                    check(pattern<IUIAutomationInvokePattern>(zoom.Get(), UIA_InvokePatternId)->Invoke(), "Zoom offline map");
                    suggestion_capture::bitmap(process.window, nullptr, captures / L"map-selected-dark.bmp");
                }
                if (!round && i == 44) {
                    auto volume = named(automation.Get(), root.Get(), L"Playback volume");
                    check(pattern<IUIAutomationRangeValuePattern>(volume.Get(), UIA_RangeValuePatternId)->SetValue(0), "Mute owned audio fixture");
                    auto load = named(automation.Get(), root.Get(), L"Load owned tone"); focus(load.Get(), "Focus explicit media load");
                    check(pattern<IUIAutomationInvokePattern>(load.Get(), UIA_InvokePatternId)->Invoke(), "Load owned WAV");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: media state 2") != nullptr; }), "Gallery Media Foundation ready");
                    auto play = named(automation.Get(), root.Get(), L"Play"); focus(play.Get(), "Focus explicit playback");
                    check(pattern<IUIAutomationInvokePattern>(play.Get(), UIA_InvokePatternId)->Invoke(), "Play owned WAV");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: media state 3") != nullptr; }), "Gallery actual media playback");
                    auto unload = named(automation.Get(), root.Get(), L"Unload media");
                    check(pattern<IUIAutomationInvokePattern>(unload.Get(), UIA_InvokePatternId)->Invoke(), "Unload gallery media");
                }
                if (!round && i == 46) {
                    auto workspace = identified(automation.Get(), root.Get(), L"gallery-navigation-view");
                    auto reports = identified(automation.Get(), workspace.Get(), L"2:1");
                    auto report = identified(automation.Get(), workspace.Get(), L"3:1");
                    auto archived = identified(automation.Get(), workspace.Get(), L"4:1");
                    require(reports && report && archived && !enabled(archived.Get()),
                        "Navigation demo contains nested and disabled entries");
                    ComPtr<IUIAutomationElement> report_parent;
                    check(walker->GetParentElement(report.Get(), &report_parent), "Read report group");
                    require(name(report_parent.Get()) == L"Reports", "Demo exposes its second hierarchy level");
                    auto report_disclosure = pattern<IUIAutomationExpandCollapsePattern>(reports.Get(), UIA_ExpandCollapsePatternId);
                    check(report_disclosure->Collapse(), "Collapse nested Reports group");
                    require(eventually([&] { return !identified(automation.Get(), workspace.Get(), L"3:1"); }),
                        "Nested collapse hides reports");
                    check(report_disclosure->Expand(), "Expand nested Reports group");
                    require(eventually([&] { return identified(automation.Get(), workspace.Get(), L"3:1") != nullptr; }),
                        "Nested expansion restores reports");
                    auto reference = identified(automation.Get(), workspace.Get(), L"6:1");
                    auto reference_disclosure = pattern<IUIAutomationExpandCollapsePattern>(reference.Get(), UIA_ExpandCollapsePatternId);
                    ExpandCollapseState reference_state{};
                    check(reference_disclosure->get_CurrentExpandCollapseState(&reference_state), "Read initially collapsed reference");
                    require(reference_state == ExpandCollapseState_Collapsed, "Demo preserves initial collapsed state");
                    check(reference_disclosure->Expand(), "Expand reference");
                    require(identified(automation.Get(), workspace.Get(), L"7:1") != nullptr, "Reference child is real");
                    auto workspace_search = named(automation.Get(), workspace.Get(), L"Filter workspace", UIA_EditControlTypeId);
                    auto workspace_value = pattern<IUIAutomationValuePattern>(workspace_search.Get(), UIA_ValuePatternId);
                    set_value(workspace_value.Get(), L"summary");
                    require(eventually([&] {
                        return identified(automation.Get(), workspace.Get(), L"3:1") &&
                            !identified(automation.Get(), workspace.Get(), L"5:1");
                    }), "Workspace keywords filter nested items");
                    require(identified(automation.Get(), workspace.Get(), L"1:1") &&
                        identified(automation.Get(), workspace.Get(), L"2:1"), "Filtering retains ancestors");
                    require(identified(automation.Get(), workspace.Get(), L"101:1") &&
                        identified(automation.Get(), workspace.Get(), L"102:1"), "Filtering retains pinned sections");
                    set_value(workspace_value.Get(), L"");
                    check(pattern<IUIAutomationSelectionItemPattern>(
                        identified(automation.Get(), workspace.Get(), L"102:1").Get(), UIA_SelectionItemPatternId)->Select(),
                        "Select workspace footer");
                    require(named(automation.Get(), root.Get(), L"Events: workspace selected 102") != nullptr,
                        "Workspace selection calls its public callback");
                    focus(next_element.Get(), "Leave workspace search before navigation");
                }
#ifdef XUI_ENABLE_WEBVIEW2
                if (!round && i == 45) {
                    auto load = named(automation.Get(), root.Get(), L"Load owned HTML"); focus(load.Get(), "Focus owned HTML load");
                    check(pattern<IUIAutomationInvokePattern>(load.Get(), UIA_InvokePatternId)->Invoke(), "Load owned web content");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: web state 2") != nullptr; }), "Gallery real WebView2 ready");
                    auto read = named(automation.Get(), root.Get(), L"Read DOM");
                    check(pattern<IUIAutomationInvokePattern>(read.Get(), UIA_InvokePatternId)->Invoke(), "Query gallery DOM");
                    require(eventually([&] { return named(automation.Get(), root.Get(), L"Events: \"Owned WebView2 content\"") != nullptr; }), "Gallery engine evaluates owned DOM");
                    auto unload = named(automation.Get(), root.Get(), L"Unload web");
                    check(pattern<IUIAutomationInvokePattern>(unload.Get(), UIA_InvokePatternId)->Invoke(), "Unload gallery web");
                }
#endif
                if (!round && i == 15) {
                    auto target = named(automation.Get(), root.Get(), L"Context menu target");
                    focus(target.Get(), "Focus gallery menu target");
                    const auto target_hwnd = handle(automation.Get(), target.Get());
                    PostMessageW(target_hwnd, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(target_hwnd), -1);
                    HWND popup{};
                    require(eventually([&] {
                        struct Search { DWORD process; HWND window{}; } data{process.info.dwProcessId};
                        EnumWindows([](HWND h, LPARAM parameter) -> BOOL {
                            auto& data = *reinterpret_cast<Search*>(parameter);
                            DWORD id{}; GetWindowThreadProcessId(h, &id);
                            wchar_t name[32]{}; GetClassNameW(h, name, 32);
                            if (id == data.process && std::wstring_view(name) == L"#32768" && IsWindowVisible(h)) data.window = h;
                            return TRUE;
                        }, reinterpret_cast<LPARAM>(&data));
                        popup = data.window; return popup != nullptr;
                    }), "Gallery opens its real native menu");
                    ComPtr<IUIAutomationElement> popup_root;
                    check(automation->ElementFromHandle(popup, &popup_root), "Read menu accessibility root");
                    auto unavailable = named(automation.Get(), popup_root.Get(), L"Unavailable action");
                    require(unavailable && !enabled(unavailable.Get()), "Gallery menu exposes disabled state");
                    auto details = named(automation.Get(), popup_root.Get(), L"Show details");
                    auto legacy = pattern<IUIAutomationLegacyIAccessiblePattern>(details.Get(), UIA_LegacyIAccessiblePatternId);
                    DWORD menu_state{}; check(legacy->get_CurrentState(&menu_state), "Read gallery checked menu state");
                    require((menu_state & STATE_SYSTEM_CHECKED) != 0, "Gallery menu exposes its checked command");
                    suggestion_capture::bitmap(process.window, popup, captures / L"menu-open-dark.bmp");
                    key(target_hwnd, VK_ESCAPE);
                    require(eventually([&] { return !IsWindowVisible(popup); }), "Escape closes gallery menu");
                    focus(next_element.Get(), "Restore navigation focus after menu");
                }
                if (winui_catalog && !round) {
                    require(eventually([&] { return SendMessageW(process.window, WM_APP + 60, 24, 0) == 0; }),
                        "Catalog interaction dismisses popups before the style sweep");
                    auto theme_button = named(automation.Get(), root.Get(), L"Theme", UIA_ButtonControlTypeId);
                    auto cycle = pattern<IUIAutomationInvokePattern>(theme_button.Get(), UIA_InvokePatternId);
                    require(SendMessageW(process.window, WM_APP + 60, 6, 0) == 0, "Catalog sweep starts in dark theme");
                    check(cycle->Invoke(), "Cycle catalog to light theme");
                    require(eventually([&] {
                        ToggleState checked{};
                        return SUCCEEDED(theme->get_CurrentToggleState(&checked)) && checked == ToggleState_On &&
                            SendMessageW(process.window, WM_APP + 60, 6, 0) == 1;
                    }), "Catalog light theme is active");
                    check(cycle->Invoke(), "Cycle catalog to explicit high contrast");
                    require(eventually([&] {
                        ToggleState checked{};
                        return SUCCEEDED(theme->get_CurrentToggleState(&checked)) && checked == ToggleState_Off &&
                            SendMessageW(process.window, WM_APP + 60, 6, 0) == 1;
                    }), "Catalog high contrast is active without changing OS settings");
                    const auto example_name = std::wstring(gallery::entries[i].title) + L" example";
                    auto example = named(automation.Get(), root.Get(), example_name.c_str());
                    catalog_style_roundtrip(automation.Get(), root.Get(), example.Get(), process.window);
                    check(cycle->Invoke(), "Restore dark theme after catalog sweep");
                    require(eventually([&] { return SendMessageW(process.window, WM_APP + 60, 6, 0) == 0; }),
                        "Catalog sweep restores the theme for existing smoke checks");
                }
            }
            choose(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            if (!round) {
                gdi = GetGuiResources(process.info.hProcess, GR_GDIOBJECTS);
                user = GetGuiResources(process.info.hProcess, GR_USEROBJECTS);
                std::cout << "Gallery after first popup/tooltip use GDI=" << gdi << " USER=" << user << '\n';
            } else {
                std::cout << "Gallery repeat round=" << round << " GDI=" << GetGuiResources(process.info.hProcess, GR_GDIOBJECTS)
                    << " USER=" << GetGuiResources(process.info.hProcess, GR_USEROBJECTS) << '\n';
                require(GetGuiResources(process.info.hProcess, GR_GDIOBJECTS) <= gdi + 2, "Repeated popup and page use has bounded GDI objects");
                require(GetGuiResources(process.info.hProcess, GR_USEROBJECTS) <= user + 2, "Repeated popup and page use has bounded USER objects");
            }
        }
        choose(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        require(SendMessageW(process.window, WM_APP + 60, 14, 0) == peers, "Repeated page switches retain a fixed peer count");
        std::cout << "Gallery settled GDI=" << GetGuiResources(process.info.hProcess, GR_GDIOBJECTS)
            << " USER=" << GetGuiResources(process.info.hProcess, GR_USEROBJECTS) << '\n';
        require(GetGuiResources(process.info.hProcess, GR_GDIOBJECTS) <= gdi + 2, "Page switches do not leak GDI objects");
        require(GetGuiResources(process.info.hProcess, GR_USEROBJECTS) <= user + 2, "Page switches do not leak USER objects");
        require(SendMessageW(process.window, WM_APP + 60, 15, 0) == 0 &&
            SendMessageW(process.window, WM_APP + 60, 17, 0) == 0, "Gallery starts no filesystem task or periodic sampler");
        focus(next_element.Get(), "Stop native caret after all examples");
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const auto settled_paints = SendMessageW(process.window, WM_APP + 60, 0, 0);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        require(SendMessageW(process.window, WM_APP + 60, 0, 0) == settled_paints, "All examples remain idle after repeated navigation");
        for (const UINT dpi : {96u, 144u, 192u}) {
            RECT rectangle{40, 40, 40 + static_cast<LONG>(760 * dpi / 96), 40 + static_cast<LONG>(640 * dpi / 96)};
            SendMessageW(process.window, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&rectangle));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            suggestion_capture::bitmap(process.window, nullptr, captures / (L"narrow-dpi-" + std::to_wstring(dpi) + L".bmp"));
        }
        check(theme->Toggle(), "Capture light theme");
        suggestion_capture::bitmap(process.window, nullptr, captures / L"forms-light.bmp");
        focus(save.Get(), "Focus form action for theme shortcut");
        key(save_hwnd, VK_F6);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        suggestion_capture::bitmap(process.window, nullptr, captures / L"forms-contrast.bmp");
        suggestion_capture::memory(process.info.hProcess, "Gallery after repeated page switches");
        std::cout << "Gallery catalog: " << gallery::entries.size() << " pages, search/empty state, real events, 100k grid, fixed peers, GDI/USER, synthetic DPI captures passed\n";
        if (winui_catalog) std::cout << "WinUI full catalog: per-page style roundtrips, high contrast, UIA state and native text preservation passed\n";
        PostMessageW(process.window, WM_CLOSE, 0, 0);
        require(WaitForSingleObject(process.info.hProcess, 5000) == WAIT_OBJECT_0, "Orderly gallery shutdown");
        DWORD exit{};
        require(GetExitCodeProcess(process.info.hProcess, &exit) != 0 && exit == 0, "Gallery successful exit");
        require(invoke->Invoke() == UIA_E_ELEMENTNOTAVAILABLE, "Retained button provider invalidated");
        require(toggle->Toggle() == UIA_E_ELEMENTNOTAVAILABLE, "Retained checkbox provider invalidated");
        BSTR closed_text = SysAllocString(L"Closed");
        require(closed_text != nullptr, "Allocate closed-provider probe value");
        const auto closed_value = value->SetValue(closed_text);
        SysFreeString(closed_text);
        require(closed_value == UIA_E_ELEMENTNOTAVAILABLE, "Native value provider invalidated after shutdown");
        const auto guard = CreateWindowExW(0, L"STATIC", L"XUI stale focus guard",
            WS_OVERLAPPEDWINDOW, 0, 0, 160, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(guard != nullptr, "Create owned stale-focus guard");
        ShowWindow(guard, SW_SHOW);
        SetForegroundWindow(guard);
        SetFocus(guard);
        const auto closed_focus = edit->SetFocus();
        const bool untouched = GetFocus() == guard && GetForegroundWindow() == guard && !IsWindow(edit_hwnd);
        DestroyWindow(guard);
        // Windows can return S_OK from its cached HWND focus proxy after exit.
        // The native value provider must disconnect, and stale focus must do nothing.
        require((closed_focus == S_OK || closed_focus == UIA_E_ELEMENTNOTAVAILABLE) && untouched,
            "Stale native focus cannot move focus or reactivate a closed window");
        std::cout << "Closed native focus HRESULT=" << closed_focus << "; value=UIA_E_ELEMENTNOTAVAILABLE; focus unchanged\n";
        BSTR unavailable{};
        const auto stale = greeting->get_CurrentName(&unavailable);
        SysFreeString(unavailable);
        require(stale == UIA_E_ELEMENTNOTAVAILABLE, "Retained label provider invalidated");
        std::cout << "Gallery smoke passed: UIA, native input, keyboard, capture, themes, idle, shutdown\n";
        result = 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return result;
}
