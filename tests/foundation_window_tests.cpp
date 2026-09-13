#include "xui/application.hpp"
#include "../src/drawing.hpp"
#include <windows.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <psapi.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace xui;
using Microsoft::WRL::ComPtr;
constexpr UINT metrics = WM_APP + 60, update = WM_APP + 12;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void success(HRESULT value, const char* message) {
    if (FAILED(value)) throw std::runtime_error(std::string(message) + " HRESULT=" + std::to_string(value));
}
HWND native(HWND root, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND found{}; } search{name};
    EnumChildWindows(root, [](HWND child, LPARAM data) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(data); wchar_t text[256]{};
        GetWindowTextW(child, text, 256);
        if (std::wstring_view(text) == s.name) { s.found = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.found != nullptr, "Native control exists");
    return search.found;
}
void flush(HWND root) {
    SendMessageW(root, update, 0, 0);
    UpdateWindow(root);
}
ComPtr<IUIAutomationElement> element(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationCondition> condition;
    const auto hr = uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition); VariantClear(&value);
    success(hr, "Create UIA ID condition");
    ComPtr<IUIAutomationElement> result; success(root->FindFirst(TreeScope_Subtree, condition.Get(), &result), "Find UIA element");
    require(result != nullptr, "UIA ID exists"); return result;
}
void run_window(ThemeMode theme, UINT dpi) {
    Window window({L"XUI foundation contracts", {740, 700}, theme});
    auto root = std::make_shared<Stack>(Axis::vertical); root->set_spacing(4); root->set_padding({10, 10, 10, 10});
    auto radio = std::make_shared<RadioGroup>(L"Radio"); radio->set_automation_id(L"radio"); radio->set_preferred_size({320, 102});
    radio->set_items({{10, L"Alpha"}, {20, L"Disabled", false}, {30, L"Beta"}}, 10); root->add(radio);
    auto combo = std::make_shared<ComboBox>(L"Combo"); combo->set_automation_id(L"combo");
    combo->set_items({{10, L"Alpha"}, {30, L"Beta"}}, 10); root->add(combo);
    auto range = std::make_shared<RangeInput>(L"Range"); range->set_automation_id(L"range"); range->set_range({-10, 100, 5, 20}); root->add(range);
    auto number = std::make_shared<NumericInput>(L"Number"); number->set_automation_id(L"number");
    number->set_locale(std::locale::classic()); root->add(number);
    auto progress = std::make_shared<Progress>(L"Capacity"); progress->set_automation_id(L"progress"); progress->set_capacity(48, 128, L"GB"); root->add(progress);
    auto repeat = std::make_shared<Button>(L"Repeat"); repeat->set_behavior(ButtonBehavior::repeat); root->add(repeat);
    auto help = std::make_shared<Button>(L"Help"); help->set_help_text(L"Delayed accessible help"); help->set_tooltip_delay(100); root->add(help);
    auto content = std::make_shared<Stack>(Axis::vertical);
    auto anchor = std::make_shared<Button>(L"Anchor"); content->add(anchor);
    auto detail = std::make_shared<TextInput>(L"Detail"); content->add(detail);
    auto expander = std::make_shared<Expander>(L"Details", content); expander->set_automation_id(L"expander");
    expander->set_preferred_size({500, 150}); root->add(expander);
    window.set_content(root);
    std::atomic<bool> native_done{}, driver_done{};
    std::wstring driver_error;
    int repeat_count{}, commits{};
    repeat->on_click([&] { ++repeat_count; });
    range->on_change([&](double) { ++commits; });
    window.on_key([&](const KeyEvent& key) {
        if (key.key == Key::f11) { window.close(); return true; }
        if (key.key != Key::f12) return false;
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI foundation contracts");
        require(hwnd != nullptr, "Own host exists");
        RECT rect{}; GetWindowRect(hwnd, &rect);
        const auto actual_dpi = GetDpiForWindow(hwnd);
        rect.right = rect.left + MulDiv(rect.right - rect.left, dpi, actual_dpi);
        rect.bottom = rect.top + MulDiv(rect.bottom - rect.top, dpi, actual_dpi);
        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rect)); flush(hwnd);
        const auto radio_hwnd = native(hwnd, L"Radio"), range_hwnd = native(hwnd, L"Range");
        require(window.focus(*radio), "Radio receives keyboard focus");
        SendMessageW(radio_hwnd, WM_KEYDOWN, VK_DOWN, 0);
        require(radio->selected() == 30, "Native radio arrows skip disabled items");
        const auto combo_hwnd = native(hwnd, L"Combo");
        window.focus(*combo);
        SendMessageW(combo_hwnd, WM_KEYDOWN, VK_F4, 0); flush(hwnd);
        require(combo->popup()->is_open(), "Combo opens retained popup");
        const auto choices = native(hwnd, L"Combo choices");
        SendMessageW(choices, WM_KEYDOWN, VK_DOWN, 0);
        require(combo->selected() == 10, "Arrow preview cannot commit combo");
        SendMessageW(choices, WM_KEYDOWN, VK_ESCAPE, 0); flush(hwnd);
        require(!combo->popup()->is_open() && combo->selected() == 10 && GetFocus() == combo_hwnd, "Cancel restores ID and focus");
        SendMessageW(combo_hwnd, WM_CHAR, L'b', 0);
        require(combo->selected() == 30, "Collapsed type-ahead commits stable ID");
        SendMessageW(combo_hwnd, WM_KEYDOWN, VK_F4, 0); flush(hwnd);
        SendMessageW(native(hwnd, L"Combo choices"), WM_KEYDOWN, VK_UP, 0);
        SendMessageW(native(hwnd, L"Combo choices"), WM_KEYDOWN, VK_RETURN, 0); flush(hwnd);
        require(combo->selected() == 10 && !combo->popup()->is_open(), "Enter commits preview and closes");
        window.focus(*range);
        SendMessageW(range_hwnd, WM_KEYDOWN, VK_END, 0);
        require(range->value() == 100, "Native range End reaches maximum");
        SendMessageW(range_hwnd, WM_KEYDOWN, VK_NEXT, 0);
        require(range->value() == 80, "Native range PageDown uses large step");
        const auto previous = range->value();
        SendMessageW(range_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(14 * dpi / 96, 20 * dpi / 96));
        require(GetCapture() == range_hwnd && range->dragging(), "Range captures pointer");
        SendMessageW(range_hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(150 * dpi / 96, 20 * dpi / 96));
        SendMessageW(range_hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
        require(range->value() == previous && !range->dragging() && GetCapture() != range_hwnd, "Escape cancels native capture");
        window.focus(*number->editor());
        SetWindowTextW(GetFocus(), L"not a number"); flush(hwnd);
        require(!number->valid() && number->editor()->text() == L"not a number", "Native invalid input remains visible");
        number->step(1); flush(hwnd); require(number->valid(), "Step repairs native invalid input");
        auto overlay_range = std::make_shared<RangeInput>(L"Overlay range");
        auto overlay = std::make_shared<Popup>(overlay_range);
        overlay->set_preferred_size({320, 260});
        window.focus(*combo); window.show_popup(overlay, *combo); flush(hwnd);
        const auto number_edit = native(hwnd, L"Number text");
        // The caption has the name; the native EDIT is its sibling.
        HWND native_number{};
        EnumChildWindows(native(hwnd, L"Number"), [](HWND child, LPARAM data) -> BOOL {
            wchar_t klass[32]{}; GetClassNameW(child, klass, 32);
            if (_wcsicmp(klass, L"EDIT") == 0) { *reinterpret_cast<HWND*>(data) = child; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&native_number));
        require(native_number && number_edit, "Numeric native field exists");
        RECT region_box{};
        require(GetWindowRgnBox(native_number, &region_box) != ERROR, "Popup clips the underlying native field");
        const auto overlay_hwnd = native(hwnd, L"Overlay range");
        int cancellations{};
        overlay_range->on_cancel([&](double value) {
            ++cancellations;
            require(!overlay->is_open() && value == 0, "Popup cancellation observes a closed owner");
        });
        SendMessageW(overlay_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 20));
        require(overlay_range->dragging(), "Popup range captures input");
        window.dismiss_popup(*overlay); flush(hwnd);
        require(!overlay_range->dragging() && overlay_range->value() == 0 && cancellations == 1,
            "Popup dismissal cancels range preview exactly once");
        overlay_range->on_cancel({});
        require(GetWindowRgnBox(native_number, &region_box) == ERROR, "Dismissal restores the native field region");
        window.show_popup(overlay, *combo); flush(hwnd);
        ShowWindow(hwnd, SW_HIDE);
        require(!overlay->is_open(), "Host hide closes the popup immediately");
        ShowWindow(hwnd, SW_SHOWNOACTIVATE); flush(hwnd);
        const auto base_peers = SendMessageW(hwnd, metrics, 14, 0);
        const auto base_user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        const auto base_gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int i = 0; i < 40; ++i) {
            auto panel = std::make_shared<Stack>(Axis::vertical);
            auto input = std::make_shared<TextInput>(L"Transient native input"); input->set_text(L"Native text stays visible");
            panel->add(input);
            auto nested_anchor = std::make_shared<Button>(L"Nested anchor"); panel->add(nested_anchor);
            auto slider = std::make_shared<RangeInput>(L"Transient slider"); panel->add(slider);
            auto popup = std::make_shared<Popup>(panel); popup->set_preferred_size({320, 160});
            window.focus(*anchor); window.show_popup(popup, *anchor, input.get()); flush(hwnd);
            const auto generation = popup->generation();
            require(popup->current(generation) && SendMessageW(hwnd, metrics, 24, 0) == 1, "Popup generation starts");
            require(GetFocus() == native(hwnd, L"Transient native input") || input->focused(), "Popup focuses native EDIT");
            auto nested_content = std::make_shared<Stack>(Axis::vertical);
            auto nested_button = std::make_shared<Button>(L"Nested action"); nested_content->add(nested_button);
            auto nested = std::make_shared<Popup>(nested_content); nested->set_preferred_size({220, 60});
            nested->set_placement(PopupPlacement::right);
            window.show_popup(nested, *nested_anchor); flush(hwnd);
            require(SendMessageW(hwnd, metrics, 24, 0) == 2 && Drawing::live_targets() == 1, "Nested popup shares one target");
            bool stale_before_callback{};
            nested->on_dismiss([&](auto) { stale_before_callback = !popup->current(generation); });
            window.dismiss_popup(*popup); flush(hwnd);
            require(stale_before_callback && !nested->is_open() && !popup->is_open(), "All nested generations expire before callbacks");
            // Cleanup runs after input dispatch. Explicit queued updates exercise that path below.
        }
        PostMessageW(hwnd, update, 0, 0);
        std::cout << "Popup cycles=40 base peers=" << base_peers << " base USER=" << base_user << " base GDI=" << base_gdi << '\n';
        auto popup_content = std::make_shared<Stack>(Axis::vertical);
        popup_content->add(std::make_shared<Button>(L"Collapse popup child"));
        auto popup = std::make_shared<Popup>(popup_content); window.show_popup(popup, *anchor);
        expander->set_expanded(false); flush(hwnd);
        require(!popup->is_open() && !window.focus(*detail), "Collapsing anchor closes popup and rejects hidden input");
        require(GetFocus() == native(hwnd, L"Details"), "Collapse repairs focus to header");
        expander->set_expanded(true); flush(hwnd);
        require(window.focus(*detail), "Expanded native child receives focus");
        expander->set_expanded(false); flush(hwnd);
        require(GetFocus() == native(hwnd, L"Details"), "Direct collapse repairs native EDIT focus");
        expander->set_expanded(true); flush(hwnd);
        auto help_hwnd = native(hwnd, L"Help"); window.focus(*help);
        SendMessageW(help_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
        SendMessageW(hwnd, WM_TIMER, 41, 0); flush(hwnd);
        require(SendMessageW(hwnd, metrics, 25, 0) && GetFocus() == help_hwnd, "Tooltip has no focus theft");
        SendMessageW(help_hwnd, WM_MOUSELEAVE, 0, 0); flush(hwnd);
        require(!SendMessageW(hwnd, metrics, 25, 0), "Tooltip cancels on leave");
        const auto repeat_hwnd = native(hwnd, L"Repeat");
        SendMessageW(repeat_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
        require(repeat_count == 1, "Repeat activates immediately");
        SendMessageW(repeat_hwnd, WM_TIMER, 42, 0);
        require(repeat_count == 2, "Repeat timer activates once");
        SendMessageW(repeat_hwnd, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
        SendMessageW(repeat_hwnd, WM_TIMER, 42, 0);
        require(repeat_count == 2, "Release has no extra repeat");
        progress->set_state(ProgressState::indeterminate); flush(hwnd);
        const auto paints = SendMessageW(hwnd, metrics, 0, 0);
        Sleep(150); flush(hwnd);
        // flush deliberately requests a paint; the posted updates must settle afterward.
        require(SendMessageW(hwnd, metrics, 0, 0) <= paints + 1, "Static indeterminate has no animation loop");
        SendMessageW(hwnd, WM_DISPLAYCHANGE, 0, 0);
        // A recreated target can itself be lost before presentation. Check recovery on subsequent frames.
        for (int attempt = 0; attempt < 3 && Drawing::live_targets() != 1; ++attempt) {
            InvalidateRect(hwnd, nullptr, FALSE);
            flush(hwnd);
        }
        require(Drawing::live_targets() == 1, "Foundation controls survive target recreation");
        progress->set_state(ProgressState::determinate);
        native_done = true; return true;
    });
    std::jthread driver([&] {
        HWND hwnd{};
        const auto deadline = GetTickCount64() + 15000;
        while (GetTickCount64() < deadline) {
            hwnd = FindWindowW(L"Xui.Window.1", L"XUI foundation contracts");
            if (hwnd && IsWindowVisible(hwnd)) break;
            Sleep(10);
        }
        if (!hwnd) { driver_error = L"Test window did not start"; return; }
        PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0);
        while (!native_done && IsWindow(hwnd) && GetTickCount64() < deadline) Sleep(10);
        if (!native_done) { driver_error = L"Native phase failed"; PostMessageW(hwnd, WM_CLOSE, 0, 0); return; }
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
        std::wstring command = L"\"" + std::wstring(executable) + L"\" --automation " +
            std::to_wstring(reinterpret_cast<std::uintptr_t>(hwnd)) + L" " +
            std::to_wstring(static_cast<int>(theme)) + L" " + std::to_wstring(dpi);
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        if (CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
            const auto wait = WaitForSingleObject(process.hProcess, 30000);
            DWORD exit{1};
            if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &exit);
            else TerminateProcess(process.hProcess, 1);
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
            driver_done = exit == 0;
            if (!driver_done) driver_error = L"Isolated UIA client failed";
        } else driver_error = L"Could not start isolated UIA client";
        PostMessageW(hwnd, WM_KEYDOWN, VK_F11, 0);
    });
    const auto result = Application::run(window); driver.join();
    if (result || !driver_done) std::wcerr << window.error() << L" / " << driver_error << L'\n';
    require(result == 0 && driver_done, "Native foundation window and UIA contracts pass");
}
void automation(HWND hwnd, ThemeMode theme, UINT dpi) {
    success(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Initialize automation client");
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
            ComPtr<IUIAutomation> uia; success(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia)), "Create automation");
            ComPtr<IUIAutomationElement> host; success(uia->ElementFromHandle(hwnd, &host), "Read automation host");
            auto range_element = element(uia.Get(), host.Get(), L"range");
            ComPtr<IUIAutomationRangeValuePattern> range_pattern;
            success(range_element->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&range_pattern)), "Range exposes RangeValue");
            double minimum{}, maximum{}, small_change{}, large_change{};
            success(range_pattern->get_CurrentMinimum(&minimum), "Range minimum");
            success(range_pattern->get_CurrentMaximum(&maximum), "Range maximum");
            success(range_pattern->get_CurrentSmallChange(&small_change), "Range small change");
            success(range_pattern->get_CurrentLargeChange(&large_change), "Range large change");
            require(minimum == -10 && maximum == 100 && small_change == 5 && large_change == 20, "Range bounds and steps match public model");
            success(range_pattern->SetValue(45), "UIA range value action");
            require(FAILED(range_pattern->SetValue(101)), "UIA rejects out-of-range value");
            auto radio_element = element(uia.Get(), host.Get(), L"radio");
            ComPtr<IUIAutomationSelectionPattern> selection;
            success(radio_element->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&selection)), "Radio exposes Selection");
            ComPtr<IUIAutomationElementArray> selected; success(selection->GetCurrentSelection(&selected), "Radio selected child");
            int count{}; selected->get_Length(&count); require(count == 1, "Exactly one selected radio");
            ComPtr<IUIAutomationElement> selected_item; selected->GetElement(0, &selected_item);
            CONTROLTYPEID role{}; selected_item->get_CurrentControlType(&role);
            require(role == UIA_RadioButtonControlTypeId, "Radio child is not a checkbox or button");
            auto alpha = element(uia.Get(), host.Get(), L"radio-tab-10");
            ComPtr<IUIAutomationSelectionItemPattern> alpha_selection;
            success(alpha->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&alpha_selection)), "Radio child exposes SelectionItem");
            success(alpha_selection->Select(), "UIA selects stable radio identity");
            auto numeric = element(uia.Get(), host.Get(), L"number");
            ComPtr<IUIAutomationValuePattern> value; success(numeric->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value)), "Number exposes Value");
            auto text = SysAllocString(L"12.5"); const auto valid_result = value->SetValue(text); SysFreeString(text);
            success(valid_result, "UIA numeric locale input");
            text = SysAllocString(L"bad"); const auto invalid_result = value->SetValue(text); SysFreeString(text);
            require(FAILED(invalid_result), "UIA numeric invalid input fails clearly");
            auto disclosure = element(uia.Get(), host.Get(), L"expander");
            ComPtr<IUIAutomationExpandCollapsePattern> expand;
            success(disclosure->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand)), "Expander exposes ExpandCollapse");
            success(expand->Collapse(), "UIA collapses content"); success(expand->Expand(), "UIA expands content");
            auto capacity = element(uia.Get(), host.Get(), L"progress");
            ComPtr<IUIAutomationRangeValuePattern> capacity_value;
            success(capacity->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&capacity_value)), "Capacity exposes value");
            BOOL read_only{}; capacity_value->get_CurrentIsReadOnly(&read_only);
            require(read_only && FAILED(capacity_value->SetValue(30)), "Capacity is read-only");
            Sleep(150);
            const auto settled = SendMessageW(hwnd, metrics, 0, 0);
            Sleep(250);
            require(SendMessageW(hwnd, metrics, 0, 0) == settled, "Idle controls issue no paints");
            const auto peers = SendMessageW(hwnd, metrics, 14, 0);
            require(peers < 30 && SendMessageW(hwnd, metrics, 24, 0) == 0, "Dismissed peers are reclaimed");
            DWORD process_id{}; GetWindowThreadProcessId(hwnd, &process_id);
            const auto process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process_id);
            std::cout << "Theme=" << static_cast<int>(theme) << " DPI=" << dpi << " settled peers=" << peers
                << " USER=" << GetGuiResources(process, GR_USEROBJECTS)
                << " GDI=" << GetGuiResources(process, GR_GDIOBJECTS) << " idle paints=0\n";
            if (process) CloseHandle(process);
}
void popup_callback_lifetimes() {
    for (int mode = 0; mode < 4; ++mode) {
        auto window = std::make_unique<Window>(WindowOptions{L"XUI popup callback lifetime"});
        auto root = std::make_shared<Stack>(Axis::vertical);
        auto anchor = std::make_shared<Button>(L"Anchor");
        root->add(anchor); window->set_content(root);
        auto content = std::make_shared<Stack>(Axis::vertical);
        auto action = std::make_shared<Button>(L"Popup callback");
        content->add(action);
        auto popup = std::make_shared<Popup>(content);
        std::weak_ptr<Popup> weak = popup;
        bool invoked{};
        window->on_key([&](const KeyEvent& key) {
            if (key.key != Key::f12) return false;
            window->focus(*anchor);
            window->show_popup(popup, *anchor);
            const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI popup callback lifetime");
            if (mode == 3) {
                bool reopened{};
                popup->on_dismiss([&](auto) {
                    if (!reopened) { reopened = true; window->show_popup(popup, *anchor); }
                });
                window->dismiss_popup(*popup);
                require(popup->is_open() && reopened, "Dismiss callback can reopen the retained popup");
                popup->on_dismiss({});
                window->dismiss_popup(*popup);
                // Once the message unwinds, a retained child must not keep a dead text measurer.
                PostMessageW(hwnd, WM_KEYDOWN, VK_F11, 0);
                invoked = true;
                return true;
            }
            action->on_click([&] {
                invoked = true;
                if (mode == 0) window->close();
                else if (mode == 1) throw std::runtime_error("Expected popup action failure");
                else window.reset();
                popup.reset();
            });
            const auto child = native(hwnd, L"Popup callback");
            SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
            SendMessageW(child, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
            return true;
        });
        std::jthread driver([&] {
            HWND hwnd{};
            const auto deadline = GetTickCount64() + 10000;
            while (GetTickCount64() < deadline) {
                hwnd = FindWindowW(L"Xui.Window.1", L"XUI popup callback lifetime");
                if (hwnd && IsWindowVisible(hwnd)) break;
                Sleep(10);
            }
            if (!hwnd) return;
            PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0);
            if (mode == 3) {
                Sleep(150);
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
        });
        const auto result = Application::run(*window); driver.join();
        require(invoked && result == (mode == 1 ? 1 : 0), "Popup callback closes, throws, deletes, or reopens safely");
        popup.reset(); window.reset();
        require(weak.expired(), "Popup content ownership is released");
        action->set_name(L"Retained after popup destruction");
        action->measured_text();
        action->measure({400, 40});
        action->on_click({});
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 5 && std::string_view(argv[1]) == "--automation") {
            automation(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(std::stoull(argv[2]))),
                static_cast<ThemeMode>(std::stoi(argv[3])), static_cast<UINT>(std::stoul(argv[4])));
            return 0;
        }
        for (const auto mode : {ThemeMode::dark, ThemeMode::light, ThemeMode::high_contrast})
            for (const UINT dpi : {96u, 144u, 192u}) run_window(mode, dpi);
        popup_callback_lifetimes();
        require(Drawing::live_targets() == 0, "No target survives window destruction");
        std::cout << "Foundation native/UIA contracts passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
