#include "collections_fixture.hpp"
#include "xui/application.hpp"
#include "xui/adaptive_layout.hpp"
#include "../src/drawing.hpp"
#include "suggestion_capture.hpp"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <psapi.h>
#include <iostream>
#include <thread>

namespace {
using namespace xui;
using namespace collections_test;
using Microsoft::WRL::ComPtr;
constexpr UINT metrics = WM_APP + 60, update = WM_APP + 12;
void success(HRESULT hr, const char* message) { if (FAILED(hr)) throw std::runtime_error(std::string(message) + " HRESULT=" + std::to_string(hr)); }
void flush(HWND hwnd) { SendMessageW(hwnd, update, 0, 0); UpdateWindow(hwnd); }
HWND native(HWND root, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result{}; } search{name};
    EnumChildWindows(root, [](HWND child, LPARAM p) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(p); wchar_t name[128]{}; GetWindowTextW(child, name, 128);
        if (std::wstring_view(name) == s.name) { s.result = child; return FALSE; } return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Owned native peer exists"); return search.result;
}
template<class T> ComPtr<T> pattern(IUIAutomationElement* element, PATTERNID id) {
    ComPtr<T> value; success(element->GetCurrentPatternAs(id, IID_PPV_ARGS(&value)), "Read collection UIA pattern"); return value;
}
ComPtr<IUIAutomationElement> find(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationCondition> condition; const auto hr = uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition); VariantClear(&value);
    success(hr, "Create scoped UIA condition"); ComPtr<IUIAutomationElement> result;
    success(root->FindFirst(TreeScope_Subtree, condition.Get(), &result), "Find scoped collection");
    if (!result) {
        ComPtr<IUIAutomationTreeWalker> walker;
        uia->get_RawViewWalker(&walker);
        ComPtr<IUIAutomationElement> child;
        walker->GetFirstChildElement(root, &child);
        while (child) {
            BSTR name{}, identity{};
            child->get_CurrentName(&name); child->get_CurrentAutomationId(&identity);
            std::wcerr << L"UIA child: " << (name ? name : L"") << L" [" << (identity ? identity : L"") << L"]\n";
            SysFreeString(name); SysFreeString(identity);
            ComPtr<IUIAutomationElement> next;
            walker->GetNextSiblingElement(child.Get(), &next); child = std::move(next);
        }
        std::string identity;
        for (const auto* character = id; *character; ++character) identity += static_cast<char>(*character);
        throw std::runtime_error("Missing collection UIA identity: " + identity);
    }
    return result;
}
ComPtr<IUIAutomationElement> virtual_item(IUIAutomationItemContainerPattern* container, const wchar_t* id) {
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationElement> result;
    const auto hr = container->FindItemByProperty(nullptr, UIA_AutomationIdPropertyId, value, &result); VariantClear(&value);
    success(hr, "Find virtual identity without row scan"); require(result != nullptr, "Virtual item exists"); return result;
}
void automation(HWND hwnd) {
    success(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "UIA apartment");
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    ComPtr<IUIAutomation> uia; success(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia)), "UIA client");
    ComPtr<IUIAutomationElement> root; success(uia->ElementFromHandle(hwnd, &root), "Owned host provider");
    auto items = find(uia.Get(), root.Get(), L"items");
    auto container = pattern<IUIAutomationItemContainerPattern>(items.Get(), UIA_ItemContainerPatternId);
    auto last = virtual_item(container.Get(), L"1000000:1");
    BOOL offscreen{}; success(last->get_CurrentIsOffscreen(&offscreen), "Virtual item offscreen state"); require(offscreen, "Unrealized last item has no visible bounds");
    success(pattern<IUIAutomationVirtualizedItemPattern>(last.Get(), UIA_VirtualizedItemPatternId)->Realize(), "Realize last virtual item");
    auto selected = pattern<IUIAutomationSelectionItemPattern>(last.Get(), UIA_SelectionItemPatternId);
    success(selected->Select(), "Select last stable identity on UI thread");
    auto previous = virtual_item(container.Get(), L"999999:1");
    success(pattern<IUIAutomationSelectionItemPattern>(previous.Get(), UIA_SelectionItemPatternId)->AddToSelection(), "Add virtual item without replacing selection");
    BOOL value{}; selected->get_CurrentIsSelected(&value); require(value, "SelectionItem Add retains previous item");
    auto selection = pattern<IUIAutomationSelectionPattern>(items.Get(), UIA_SelectionPatternId);
    selection->get_CurrentCanSelectMultiple(&value); require(value, "Collection exposes multiple selection");
    ComPtr<IUIAutomationElementArray> selected_items; success(selection->GetCurrentSelection(&selected_items), "Enumerate bounded selected identities");
    int count{}; selected_items->get_Length(&count); require(count == 2, "Selected array contains two stable items");
    auto inline_action = find(uia.Get(), last.Get(), L"1000000:1:action");
    success(pattern<IUIAutomationInvokePattern>(inline_action.Get(), UIA_InvokePatternId)->Invoke(), "Invoke distinct inline action");
    success(pattern<IUIAutomationSelectionItemPattern>(previous.Get(), UIA_SelectionItemPatternId)->RemoveFromSelection(), "Remove only one selected item");
    auto tree = find(uia.Get(), root.Get(), L"tree");
    CONTROLTYPEID role{}; tree->get_CurrentControlType(&role); require(role == UIA_TreeControlTypeId, "Tree exposes a hierarchy, not a table");
    auto tree_container = pattern<IUIAutomationItemContainerPattern>(tree.Get(), UIA_ItemContainerPatternId);
    auto branch = virtual_item(tree_container.Get(), L"1:1");
    auto disclosure = pattern<IUIAutomationExpandCollapsePattern>(branch.Get(), UIA_ExpandCollapsePatternId);
    success(disclosure->Expand(), "UIA starts lazy child query");
    auto child = virtual_item(tree_container.Get(), L"1000001:1");
    child->get_CurrentControlType(&role); require(role == UIA_TreeItemControlTypeId, "Virtual child has tree-item semantics");
    ComPtr<IUIAutomationTreeWalker> walker; success(uia->get_ControlViewWalker(&walker), "Hierarchy walker");
    ComPtr<IUIAutomationElement> parent; success(walker->GetParentElement(child.Get(), &parent), "Child parent navigation");
    BSTR id{}; parent->get_CurrentAutomationId(&id); const bool correct = id && std::wstring_view(id) == L"1:1"; SysFreeString(id);
    require(correct, "Tree child provider navigates to its real parent");
    success(disclosure->Collapse(), "UIA collapses branch");
    require(FAILED(child->get_CurrentName(&id)), "Collapsed virtual provider rejects stale visible access");
    auto grid = find(uia.Get(), root.Get(), L"table");
    auto table = pattern<IUIAutomationGridPattern>(grid.Get(), UIA_GridPatternId);
    table->get_CurrentRowCount(&count); require(count == 100000, "DataGrid source compatibility preserves virtual count");
    auto check = find(uia.Get(), grid.Get(), L"table-5-0-0-0");
    auto toggle = pattern<IUIAutomationTogglePattern>(check.Get(), UIA_TogglePatternId);
    success(toggle->Toggle(), "Header select-all action");
    ToggleState state{}; toggle->get_CurrentToggleState(&state); require(state == ToggleState_On, "Header exposes all state");
    ComPtr<IUIAutomationElement> cell; success(table->GetItem(10, 0, &cell), "Virtual check cell");
    success(pattern<IUIAutomationTogglePattern>(cell.Get(), UIA_TogglePatternId)->Toggle(), "Checkbox cell toggles independently of sort");
    success(toggle->get_CurrentToggleState(&state), "Read mixed header state");
    require(state == ToggleState_Indeterminate, "Header exposes mixed state");
    auto filter = find(uia.Get(), grid.Get(), L"table-4-0-0-0");
    auto filter_value = pattern<IUIAutomationValuePattern>(filter.Get(), UIA_ValuePatternId);
    auto query = SysAllocString(L"even"); const auto filter_result = filter_value->SetValue(query); SysFreeString(query);
    success(filter_result, "Header filter Value marshals external query");
    table->get_CurrentRowCount(&count); require(count == 50000, "Accepted header filter replaces source");
    query = SysAllocString(L""); const auto clear_result = filter_value->SetValue(query); SysFreeString(query);
    success(clear_result, "Clear filter"); table->get_CurrentRowCount(&count); require(count == 100000, "Filter clear restores full source");
    Sleep(150); const auto paints = SendMessageW(hwnd, metrics, 0, 0); Sleep(200);
    require(SendMessageW(hwnd, metrics, 0, 0) == paints, "Collections have zero idle paints");
    require(SendMessageW(hwnd, metrics, 14, 0) == 8, "Closed popup peers retire after input dispatch");
    PostMessageW(hwnd, WM_KEYDOWN, VK_F11, 0);
    const auto deadline = GetTickCount64() + 3000;
    while (IsWindow(hwnd) && GetTickCount64() < deadline) Sleep(10);
    require(!IsWindow(hwnd) && FAILED(selected->Select()), "Retained UIA child rejects actions after owner teardown");
}
void run(ThemeMode theme, UINT dpi) {
    Window window({L"XUI collection contracts", {920, 760}, theme});
    auto root = std::make_shared<Stack>(Axis::vertical); root->set_spacing(6); root->set_padding({10, 10, 10, 10});
    auto source = std::make_shared<Items>();
    auto items = std::make_shared<ItemsView>(L"Items"); items->set_automation_id(L"items"); items->set_items(source, source);
    items->set_help_text(L"Collection help"); items->set_tooltip_delay(100);
    auto tree = std::make_shared<TreeView>(L"Tree"); tree->set_automation_id(L"tree"); tree->set_tree(std::make_shared<Tree>());
    auto detail = std::make_shared<Stack>(Axis::vertical); detail->add(tree, 1);
    auto overlay_edit = std::make_shared<TextInput>(L"Overlay native field"); overlay_edit->set_caption_visible(false);
    overlay_edit->set_preferred_size({600, 44}); overlay_edit->set_text(L"Native field below the adaptive overlay"); detail->add(overlay_edit);
    auto panes = std::make_shared<AdaptiveLayout>(items, detail); panes->set_preferred_size({850, 260}); panes->set_navigation_extent(420);
    root->add(panes);
    auto grid = std::make_shared<DataGrid>(L"Table"); grid->set_automation_id(L"table");
    grid->set_columns({{L"Name", 280, false, true, true}, {L"Value", 160, true}});
    grid->set_source(std::make_shared<Rows>()); grid->set_preferred_size({850, 200}); root->add(grid);
    auto anchor = std::make_shared<Button>(L"Popup anchor"); root->add(anchor);
    auto edit = std::make_shared<TextInput>(L"Native field"); edit->set_text(L"Atomic native composition"); root->add(edit);
    auto wrap = std::make_shared<Wrap>(); wrap->set_preferred_size({850, 40});
    wrap->add(std::make_shared<Button>(L"Wrapped one")); wrap->add(std::make_shared<Button>(L"Wrapped two")); root->add(wrap);
    int actions{}, sorts{};
    items->on_action([&](ItemKey key) { require(key.id == 1000000, "Inline action retains stable ID"); ++actions; });
    tree->on_request([tree = tree.get()](TreeRequest request) {
        tree->complete(request, std::make_shared<Items>(1000000, request.node.id * 1000000 + 1));
    });
    grid->on_filter([grid = grid.get()](GridFilterRequest request) {
        const bool even = request.filters[0] == L"even"; grid->complete_filter(request, std::make_shared<Rows>(even ? 50000 : 100000, false, even ? 2 : 1));
    });
    grid->on_sort([&](auto, auto) { ++sorts; });
    window.set_content(root);
    std::atomic<bool> native_done{}, driver_done{}, driver_exited{}; std::wstring driver_error;
    std::optional<GridFilterRequest> closing_filter;
    std::optional<TreeRequest> closing_tree;
    window.on_key([&](const KeyEvent& event) {
        if (event.key == Key::f11) {
            require(actions == 1 && !sorts, "UIA inline/filter/check actions never invoke unrelated actions");
            grid->on_filter([&](GridFilterRequest request) { closing_filter = request; });
            tree->on_request([&](TreeRequest request) { closing_tree = request; });
            grid->filter(0, L"pending"); tree->disclose({2, 1}, true);
            window.close(); return true;
        }
        if (event.key != Key::f12) return false;
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI collection contracts"); require(hwnd != nullptr, "Own collection host");
        RECT rect{}; GetWindowRect(hwnd, &rect); const auto actual = GetDpiForWindow(hwnd);
        rect.right = rect.left + MulDiv(rect.right - rect.left, dpi, actual); rect.bottom = rect.top + MulDiv(rect.bottom - rect.top, dpi, actual);
        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rect)); flush(hwnd);
        const auto items_hwnd = native(hwnd, L"Items"), tree_hwnd = native(hwnd, L"Tree"), grid_hwnd = native(hwnd, L"Table");
        require(window.focus(*items), "Owned items receive native focus");
        SendMessageW(items_hwnd, WM_KEYDOWN, VK_HOME, 0); SendMessageW(items_hwnd, WM_KEYDOWN, VK_DOWN, 0);
        require(items->selection().focused() == ItemKey{2, 1}, "Native arrows select item IDs");
        BYTE keyboard[256]{}; GetKeyboardState(keyboard); const auto restore = [&] { SetKeyboardState(keyboard); };
        BYTE shift[256]{}; std::copy(std::begin(keyboard), std::end(keyboard), std::begin(shift)); shift[VK_SHIFT] = 0x80; SetKeyboardState(shift);
        SendMessageW(items_hwnd, WM_KEYDOWN, VK_DOWN, 0); restore();
        require(items->selection().contains({2, 1}) && items->selection().contains({3, 1}), "Native Shift+arrow selects range");
        items->set_presentation(ItemsPresentation::tiles); items->set_offset(0); flush(hwnd);
        const auto cols = items->columns(); require(cols >= 2, "Wide items have tile columns");
        const auto start = items->item_bounds(0), finish = items->item_bounds(cols + 1);
        SendMessageW(items_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(static_cast<int>((start.x + 35) * dpi / 96), static_cast<int>((start.y + 10) * dpi / 96)));
        SendMessageW(items_hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(static_cast<int>((finish.x + 35) * dpi / 96), static_cast<int>((finish.y + 10) * dpi / 96)));
        SendMessageW(items_hwnd, WM_LBUTTONUP, 0, 0);
        require(items->selection().contains(source->key(cols + 1)) && items->selection().storage_size() == 1, "Native rectangle uses one selection term");
        items->set_presentation(ItemsPresentation::list);
        require(window.focus(*tree), "Tree receives native focus"); SendMessageW(tree_hwnd, WM_KEYDOWN, VK_HOME, 0);
        SendMessageW(tree_hwnd, WM_KEYDOWN, VK_RIGHT, 0); SendMessageW(tree_hwnd, WM_KEYDOWN, VK_RIGHT, 0);
        require(tree->selection().focused() == ItemKey{1000001, 1}, "Native Right expands and enters child");
        SendMessageW(tree_hwnd, WM_KEYDOWN, VK_LEFT, 0); SendMessageW(tree_hwnd, WM_KEYDOWN, VK_LEFT, 0);
        require(!tree->expanded({1, 1}), "Native Left returns to and collapses parent");
        window.focus(*grid); SendMessageW(grid_hwnd, WM_KEYDOWN, VK_F6, 0);
        SendMessageW(grid_hwnd, WM_KEYDOWN, VK_F4, 0); SendMessageW(grid_hwnd, WM_KEYDOWN, VK_F4, 0);
        SendMessageW(grid_hwnd, WM_KEYDOWN, VK_SPACE, 0);
        require(grid->check_state() == SelectionState::all && sorts == 0, "Header check keyboard target does not sort");
        grid->toggle_check(); grid->focus_header(false);
        grid->reorder_column(0, 1); grid->set_column_width(1, 300); require(grid->source_column(1) == 0, "Native table retains logical identity");
        grid->reorder_column(1, 0);
        const auto focus_id = items->selection().focused(); window.focus(*items);
        panes->set_breakpoint(10000); flush(hwnd);
        panes->set_compact_navigation(CompactNavigation::overlay); flush(hwnd);
        require(panes->overlay_active() && GetFocus() == items_hwnd, "Inline-to-overlay keeps the native navigation peer focused");
        const auto underlying_edit = native(hwnd, L"Native field below the adaptive overlay");
        RECT clipped{}; require(GetWindowRgnBox(underlying_edit, &clipped) != ERROR, "Adaptive overlay clips the underlying native EDIT");
        panes->set_breakpoint(640); flush(hwnd);
        require(GetWindowRgnBox(underlying_edit, &clipped) == ERROR, "Inline transition restores the native EDIT region");
        require(items->selection().focused() == focus_id && GetFocus() == items_hwnd, "Compact/inline transition keeps selected and native focused control");
        items->set_enabled(false); const auto before = items->selection(); SendMessageW(items_hwnd, WM_KEYDOWN, VK_END, 0);
        require(before == items->selection(), "Disabled native input is rejected"); items->set_enabled(true);
        window.focus(*items); SendMessageW(items_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10)); SendMessageW(hwnd, WM_TIMER, 41, 0); flush(hwnd);
        require(SendMessageW(hwnd, metrics, 25, 0) && GetFocus() == items_hwnd, "Collection tooltip shares no-focus timing");
        SendMessageW(items_hwnd, WM_MOUSELEAVE, 0, 0); flush(hwnd);
        const auto peer_count = SendMessageW(hwnd, metrics, 14, 0);
        const auto gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        auto popup_items = std::make_shared<ItemsView>(L"Popup items"); popup_items->set_items(source); popup_items->set_preferred_size({300, 150});
        auto popup = std::make_shared<Popup>(popup_items); popup->set_preferred_size({320, 180});
        for (int i = 0; i < 30; ++i) {
            window.show_popup(popup, *anchor, popup_items.get()); flush(hwnd);
            require(Drawing::live_targets() == 1, "Popup collection shares the root render target");
            window.dismiss_popup(*popup); flush(hwnd);
            items->set_offset((i % 2) ? items->maximum_offset() : 0); flush(hwnd);
        }
        require(SendMessageW(hwnd, metrics, 14, 0) <= peer_count + 2 &&
            GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi + 2 && GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= user + 3,
            "Repeated virtual scroll and popup cycles keep peers and GDI bounded");
        auto lazy = std::make_shared<TreeView>(L"Popup lazy tree"); lazy->set_tree(std::make_shared<Tree>());
        std::optional<TreeRequest> pending;
        lazy->on_request([&](TreeRequest request) { pending = request; });
        auto lazy_popup = std::make_shared<Popup>(lazy); lazy_popup->set_preferred_size({320, 150});
        window.show_popup(lazy_popup, *anchor, lazy.get()); lazy->disclose({1, 1}, true);
        require(pending.has_value(), "Popup tree starts application-owned work");
        window.dismiss_popup(*lazy_popup); flush(hwnd);
        require(pending->cancellation.stop_requested() && !lazy->complete(*pending, source), "Popup dismissal cancels lazy child work");
        lazy->on_request({});
        items->set_offset(0); tree->set_offset(0); window.focus(*anchor); flush(hwnd);
        SendMessageW(hwnd, WM_DISPLAYCHANGE, 0, 0);
        for (int attempt = 0; attempt < 3 && Drawing::live_targets() != 1; ++attempt) { InvalidateRect(hwnd, nullptr, FALSE); flush(hwnd); }
        require(Drawing::live_targets() == 1, "Collections recover a lost root target");
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
        const auto path = std::filesystem::path(executable).parent_path().parent_path() / L"collection-captures";
        suggestion_capture::bitmap(hwnd, nullptr, path / (L"collections-" + std::to_wstring(static_cast<int>(theme)) + L"-" + std::to_wstring(dpi) + L".bmp"));
        std::cout << "Collections theme=" << static_cast<int>(theme) << " DPI=" << dpi << " peers=" << peer_count <<
            " GDI=" << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) << " USER=" << GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) << " root targets=1\n";
        native_done = true; return true;
    });
    std::jthread driver([&] {
        struct Exit { std::atomic<bool>& value; ~Exit() { value = true; } } completion{driver_exited};
        HWND hwnd{}; const auto deadline = GetTickCount64() + 20000;
        while (GetTickCount64() < deadline) { hwnd = FindWindowW(L"Xui.Window.1", L"XUI collection contracts"); if (hwnd && IsWindowVisible(hwnd)) break; Sleep(10); }
        if (!hwnd) { driver_error = L"Native host did not start"; return; }
        PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0);
        while (!native_done && IsWindow(hwnd) && GetTickCount64() < deadline) Sleep(10);
        if (!native_done) { driver_error = L"Native phase failed"; PostMessageW(hwnd, WM_CLOSE, 0, 0); return; }
        wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
        std::wstring command = L"\"" + std::wstring(exe) + L"\" --automation " + std::to_wstring(reinterpret_cast<std::uintptr_t>(hwnd));
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        if (CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
            const auto wait = WaitForSingleObject(process.hProcess, 30000); DWORD exit{1};
            if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &exit); else TerminateProcess(process.hProcess, 1);
            CloseHandle(process.hThread); CloseHandle(process.hProcess); driver_done = exit == 0;
            if (!driver_done) driver_error = L"Isolated UIA client failed";
        } else driver_error = L"UIA client did not start";
        PostMessageW(hwnd, WM_KEYDOWN, VK_F11, 0);
    });
    const auto result = Application::run(window);
    while (!driver_exited) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    driver.join();
    if (result || !driver_done) std::wcerr << window.error() << L" / " << driver_error << L'\n';
    require(result == 0 && driver_done, "Native and UIA collection matrix passes");
    require(closing_filter && closing_filter->cancellation.stop_requested() && closing_tree && closing_tree->cancellation.stop_requested(),
        "Actual owner closure cancels filter and child requests");
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--automation") { automation(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(std::stoull(argv[2])))); return 0; }
        success(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Keep the server apartment alive until retained UIA clients exit");
        struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
        if (argc == 4 && std::string_view(argv[1]) == "--case") { run(static_cast<ThemeMode>(std::stoi(argv[2])), static_cast<UINT>(std::stoul(argv[3]))); return 0; }
        for (auto theme : {ThemeMode::dark, ThemeMode::light, ThemeMode::high_contrast}) for (UINT dpi : {96u, 144u, 192u}) run(theme, dpi);
        require(Drawing::live_targets() == 0, "All collection targets retire");
        std::cout << "Collection native/UIA matrix, resources, popup, tooltip, and DPI contracts passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
