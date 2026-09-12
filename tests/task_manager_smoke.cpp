#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
int assertions{};
void require(bool result, const char* message) { ++assertions; if (!result) throw std::runtime_error(message); }
void check(HRESULT result, const char* message) { require(SUCCEEDED(result), message); }
bool wait(const std::function<bool()>& test, int ms = 10000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do { if (test()) return true; Sleep(25); } while (std::chrono::steady_clock::now() < end);
    return false;
}
std::wstring name(IUIAutomationElement* element) {
    BSTR text{}; check(element->get_CurrentName(&text), "Read accessible name");
    std::wstring value = text ? text : L""; SysFreeString(text); return value;
}
template<class T> ComPtr<T> pattern(IUIAutomationElement* element, PATTERNID id) {
    ComPtr<T> value; check(element->GetCurrentPatternAs(id, __uuidof(T), reinterpret_cast<void**>(value.GetAddressOf())), "Get supported pattern");
    require(value != nullptr, "Pattern is present"); return value;
}
ComPtr<IUIAutomationElement> find(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationCondition> condition;
    const auto result = uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
    VariantClear(&value); check(result, "Create automation ID condition");
    ComPtr<IUIAutomationElement> element;
    check(root->FindFirst(TreeScope_Descendants, condition.Get(), &element), "Find control");
    require(element != nullptr, "Required control exists"); return element;
}
HWND child(HWND root, std::wstring_view text) {
    struct Search { std::wstring_view text; HWND found{}; } s{text};
    EnumChildWindows(root, [](HWND h, LPARAM p) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(p);
        wchar_t text[256]{}; GetWindowTextW(h, text, 256);
        if (s.text == text) { s.found = h; return FALSE; } return TRUE;
    }, reinterpret_cast<LPARAM>(&s)); return s.found;
}
HWND top(DWORD pid, const wchar_t* cls) {
    struct Search { DWORD pid; const wchar_t* cls; HWND found{}; } s{pid, cls};
    EnumWindows([](HWND h, LPARAM p) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(p);
        DWORD pid{}; GetWindowThreadProcessId(h, &pid);
        wchar_t cls[64]{}; GetClassNameW(h, cls, 64);
        if (s.pid == pid && std::wstring_view(cls) == s.cls) { s.found = h; return FALSE; } return TRUE;
    }, reinterpret_cast<LPARAM>(&s)); return s.found;
}
struct Process {
    PROCESS_INFORMATION info{}; HWND window{};
    ~Process() {
        if (!info.hProcess) return;
        if (window) PostMessageW(window, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(info.hProcess, 4000) == WAIT_TIMEOUT) TerminateProcess(info.hProcess, 1);
        WaitForSingleObject(info.hProcess, 3000); CloseHandle(info.hThread); CloseHandle(info.hProcess);
    }
    void start(const wchar_t* exe, const wchar_t* args = L"") {
        std::wstring command = L"\"" + std::filesystem::absolute(exe).wstring() + L"\" " + args;
        STARTUPINFOW startup{sizeof(startup)};
        require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info), "Launch owned process");
    }
};
void set_value(IUIAutomationValuePattern* input, const std::wstring& value) {
    auto text = SysAllocString(value.c_str()); auto result = input->SetValue(text); SysFreeString(text); check(result, "Set search value");
}
void press(HWND window, WORD key) {
    require(GetForegroundWindow() == window, "Keyboard test owns foreground");
    INPUT input[2]{}; input[0].type = input[1].type = INPUT_KEYBOARD;
    input[0].ki.wVk = input[1].ki.wVk = key; input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    require(SendInput(2, input, sizeof(INPUT)) == 2, "Send owned keyboard gesture");
    Sleep(100);
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    try {
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Initialize UIA client");
        struct Uninit { ~Uninit() { CoUninitialize(); } } uninit;
        Process fixture; fixture.start(argv[2], L"--fixture");
        Process app; app.start(argv[1]);
        require(wait([&] { app.window = top(app.info.dwProcessId, L"Xui.Window.1"); return app.window != nullptr; }), "Task Manager window opens");
        ComPtr<IUIAutomation> uia; check(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia)), "Create UIA client");
        ComPtr<IUIAutomationElement> root; check(uia->ElementFromHandle(app.window, &root), "Get app tree");
        auto search = find(uia.Get(), root.Get(), L"process-search");
        auto search_value = pattern<IUIAutomationValuePattern>(search.Get(), UIA_ValuePatternId);
        set_value(search_value.Get(), std::to_wstring(fixture.info.dwProcessId));
        auto grid = find(uia.Get(), root.Get(), L"process-grid");
        CONTROLTYPEID type{}; check(grid->get_CurrentControlType(&type), "Read grid role");
        require(type == UIA_DataGridControlTypeId, "Grid has DataGrid role");
        auto gp = pattern<IUIAutomationGridPattern>(grid.Get(), UIA_GridPatternId);
        require(wait([&] { int rows{}; return SUCCEEDED(gp->get_CurrentRowCount(&rows)) && rows == 1; }), "Search finds the real owned fixture");
        auto status = find(uia.Get(), root.Get(), L"sample-status");
        const auto invoke = [&](const wchar_t* id) {
            auto element = find(uia.Get(), root.Get(), id);
            check(pattern<IUIAutomationInvokePattern>(element.Get(), UIA_InvokePatternId)->Invoke(), "Invoke command");
        };
        auto pause = find(uia.Get(), root.Get(), L"pause");
        auto pause_pattern = pattern<IUIAutomationInvokePattern>(pause.Get(), UIA_InvokePatternId);
        int columns{}; check(gp->get_CurrentColumnCount(&columns), "Read columns"); require(columns == 7, "Seven useful process columns");
        ComPtr<IUIAutomationElement> pid_cell; check(gp->GetItem(0, 1, &pid_cell), "Get virtual PID cell");
        require(name(pid_cell.Get()) == std::to_wstring(fixture.info.dwProcessId), "PID cell contains actual process PID");
        auto item = pattern<IUIAutomationGridItemPattern>(pid_cell.Get(), UIA_GridItemPatternId);
        auto stale_selection = pattern<IUIAutomationSelectionItemPattern>(pid_cell.Get(), UIA_SelectionItemPatternId);
        check(stale_selection->Select(), "Cell selection");
        check(stale_selection->RemoveFromSelection(), "Selection can be cleared");
        BOOL removed{}; check(stale_selection->get_CurrentIsSelected(&removed), "Read cleared selection");
        require(!removed, "RemoveFromSelection changes actual grid selection");
        int row{}, column{}; check(item->get_CurrentRow(&row), "Cell row"); check(item->get_CurrentColumn(&column), "Cell column");
        require(row == 0 && column == 1, "GridItem coordinates");
        check(pattern<IUIAutomationSelectionItemPattern>(pid_cell.Get(), UIA_SelectionItemPatternId)->Select(), "Select by virtual identity");
        auto selected = pattern<IUIAutomationSelectionPattern>(grid.Get(), UIA_SelectionPatternId);
        ComPtr<IUIAutomationElementArray> selection; check(selected->GetCurrentSelection(&selection), "Get selected row");
        int length{}; check(selection->get_Length(&length), "Selection count"); require(length == 1, "Single row selection");
        ComPtr<IUIAutomationElement> selected_row; check(selection->GetElement(0, &selected_row), "Selected row provider");
        const auto stable_name = name(selected_row.Get());
        auto table = pattern<IUIAutomationTablePattern>(grid.Get(), UIA_TablePatternId);
        ComPtr<IUIAutomationElementArray> headers; check(table->GetCurrentColumnHeaders(&headers), "Get sortable column headers");
        ComPtr<IUIAutomationElement> name_header; check(headers->GetElement(0, &name_header), "Name header");
        RECT before_header{}; check(name_header->get_CurrentBoundingRectangle(&before_header), "Header rectangle before drag");
        const auto resize_grid = child(app.window, L"Processes");
        const auto resize_scale = GetDpiForWindow(app.window) / 96.0;
        SendMessageW(resize_grid, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(static_cast<int>(260 * resize_scale), static_cast<int>(18 * resize_scale)));
        SendMessageW(resize_grid, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(static_cast<int>(300 * resize_scale), static_cast<int>(18 * resize_scale)));
        SendMessageW(resize_grid, WM_LBUTTONUP, 0, MAKELPARAM(static_cast<int>(300 * resize_scale), static_cast<int>(18 * resize_scale)));
        require(wait([&] { RECT after{}; name_header->get_CurrentBoundingRectangle(&after);
            return after.right - after.left > before_header.right - before_header.left + 20; }), "Native header drag resizes column with capture");
        ComPtr<IUIAutomationElement> cpu_header; check(headers->GetElement(2, &cpu_header), "CPU header");
        check(pattern<IUIAutomationInvokePattern>(cpu_header.Get(), UIA_InvokePatternId)->Invoke(), "UIA sort matches header gesture");
        require(name(cpu_header.Get()).find(L"ascending") != std::wstring::npos, "Sort direction is accessible");
        BOOL chosen{}; check(pattern<IUIAutomationSelectionItemPattern>(selected_row.Get(), UIA_SelectionItemPatternId)->get_CurrentIsSelected(&chosen), "Selection after sort");
        require(chosen, "Sort preserves process identity");
        check(grid->SetFocus(), "Set grid focus");
        check(selected_row->SetFocus(), "Set focus on selected row rather than the last sorted header");
        require(wait([&] { BOOL focused{}; return GetForegroundWindow() == app.window && SUCCEEDED(grid->get_CurrentHasKeyboardFocus(&focused)) && focused; }), "External focus reaches grid");
        press(app.window, VK_F6); Sleep(100);
        require(SendMessageW(app.window, WM_APP + 60, 21, 0) == 1, "F6 moves row focus to headers");
        press(app.window, VK_RIGHT); press(app.window, VK_RETURN); press(app.window, VK_F6);
        check(search->SetFocus(), "Set native EDIT focus");
        press(app.window, VK_TAB);
        require(wait([&] { BOOL focus{}; grid->get_CurrentHasKeyboardFocus(&focus); return focus != FALSE; }), "Tab transfers native EDIT focus to grid");
        require(wait([&] { return name(status.Get()).find(L"Sample 3") != std::wstring::npos || name(status.Get()).find(L"Sample 4") != std::wstring::npos ||
            name(status.Get()).find(L"Sample 5") != std::wstring::npos; }, 7000), "Live snapshots advance");
        check(pause_pattern->Invoke(), "Pause sampling"); Sleep(250);
        const auto paused_status = name(status.Get());
        const auto paints = SendMessageW(app.window, WM_APP + 60, 0, 0);
        Sleep(1250);
        require(name(status.Get()) == paused_status, "Paused state delivers no samples");
        require(SendMessageW(app.window, WM_APP + 60, 0, 0) == paints, "Paused idle performs zero paints");
        set_value(search_value.Get(), L"no-such-fixture-process");
        require(wait([&] { return name(status.Get()).find(L"0 shown") != std::wstring::npos; }), "Paused filtering updates the visible-row status");
        set_value(search_value.Get(), std::to_wstring(fixture.info.dwProcessId));
        require(wait([&] { int rows{}; gp->get_CurrentRowCount(&rows); return rows == 1; }), "Restored filter preserves fixture identity");
        check(pattern<IUIAutomationInvokePattern>(selected_row.Get(), UIA_InvokePatternId)->Invoke(), "Row activation opens details");
        auto details = find(uia.Get(), root.Get(), L"details-title");
        require(name(details.Get()).find(L"xui_task_manager_tests") != std::wstring::npos, "Details describes selected fixture");
        auto path = find(uia.Get(), root.Get(), L"details-path");
        require(!name(path.Get()).empty(), "Details metadata is accessible");
        invoke(L"back-processes");
        auto pages = find(uia.Get(), root.Get(), L"task-pages");
        auto performance_tab = find(uia.Get(), pages.Get(), L"task-pages-tab-2");
        check(pattern<IUIAutomationSelectionItemPattern>(performance_tab.Get(), UIA_SelectionItemPatternId)->Select(), "Performance page opens");
        auto chart = find(uia.Get(), root.Get(), L"cpu-history");
        require(name(chart.Get()).find(L"0\u2013100%") != std::wstring::npos, "Chart exposes numeric scale as accessible text");
        auto ram = find(uia.Get(), root.Get(), L"performance-memory");
        require(name(ram.Get()).find(L"available") != std::wstring::npos, "Real RAM metrics on Performance page");
        check(pause_pattern->Invoke(), "Resume sampling");
        require(wait([&] { return name(status.Get()) != paused_status && name(status.Get()).starts_with(L"Live"); }), "Resume delivers new baseline");
        const auto chart_before = name(chart.Get()); Sleep(1500);
        require(name(chart.Get()) != chart_before, "CPU history value changes after resumed baseline");
        ShowWindow(app.window, SW_MINIMIZE); Sleep(250);
        const auto minimized = name(status.Get()), minimized_chart = name(chart.Get());
        Sleep(1250);
        require(name(status.Get()) == minimized && name(chart.Get()) == minimized_chart, "Minimized sample suspends ownership and updates");
        ShowWindow(app.window, SW_RESTORE);
        require(wait([&] { return name(status.Get()) != minimized; }), "Restore resumes sampling with reset baseline");
        auto process_tab = find(uia.Get(), pages.Get(), L"task-pages-tab-1");
        check(pattern<IUIAutomationSelectionItemPattern>(process_tab.Get(), UIA_SelectionItemPatternId)->Select(), "Return to Processes");
        check(pause_pattern->Invoke(), "Pause for deterministic action test");
        // The selected process is only the fixture created by this test.
        require(name(pid_cell.Get()) == std::to_wstring(fixture.info.dwProcessId), "Recheck fixture before destructive test");
        const auto end_button = child(app.window, L"End task");
        require(end_button != nullptr, "End task native peer");
        const auto scale = GetDpiForWindow(app.window) / 96.0;
        const LPARAM point = MAKELPARAM(static_cast<int>(10 * scale), static_cast<int>(10 * scale));
        PostMessageW(end_button, WM_LBUTTONDOWN, MK_LBUTTON, point); PostMessageW(end_button, WM_LBUTTONUP, 0, point);
        HWND dialog{};
        require(wait([&] { dialog = top(app.info.dwProcessId, L"#32770"); return dialog != nullptr; }), "End task opens owned confirmation");
        require(child(dialog, (L"End xui_task_manager_tests.exe (PID " + std::to_wstring(fixture.info.dwProcessId) +
            L")?\n\nUnsaved work can be lost. The process identity and critical-process status will be checked before termination.")) != nullptr,
            "Confirmation contains selected process name and PID");
        PostMessageW(dialog, WM_COMMAND, IDNO, 0);
        require(wait([&] { return !IsWindow(dialog); }), "Cancel closes confirmation");
        require(WaitForSingleObject(fixture.info.hProcess, 0) == WAIT_TIMEOUT, "Cancel preserves owned fixture");
        auto action = find(uia.Get(), root.Get(), L"action-status");
        require(name(action.Get()) == L"End task cancelled.", "Cancellation is visible");
        const auto grid_hwnd = child(app.window, L"Processes");
        PostMessageW(grid_hwnd, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(grid_hwnd), -1);
        require(wait([&] { return top(app.info.dwProcessId, L"#32768") != nullptr; }), "Selected row context menu opens");
        press(app.window, VK_ESCAPE);
        require(wait([&] { return top(app.info.dwProcessId, L"#32768") == nullptr; }), "Escape cancels row context menu");
        PostMessageW(end_button, WM_LBUTTONDOWN, MK_LBUTTON, point); PostMessageW(end_button, WM_LBUTTONUP, 0, point);
        require(wait([&] { dialog = top(app.info.dwProcessId, L"#32770"); return dialog != nullptr; }), "Second explicit confirmation");
        PostMessageW(dialog, WM_COMMAND, IDYES, 0);
        require(WaitForSingleObject(fixture.info.hProcess, 5000) == WAIT_OBJECT_0, "Confirmed action terminates only owned benign fixture");
        invoke(L"refresh");
        require(wait([&] { int rows{}; gp->get_CurrentRowCount(&rows); return rows == 0; }), "Exited fixture disappears from snapshot");
        require(FAILED(stale_selection->Select()), "Stale process provider rejects actions");
        set_value(search_value.Get(), L"");
        require(wait([&] { int rows{}; gp->get_CurrentRowCount(&rows); return rows > 10; }), "Clear search restores live real process list");
        auto scroll = pattern<IUIAutomationScrollPattern>(grid.Get(), UIA_ScrollPatternId);
        check(scroll->SetScrollPercent(100, 100), "Grid supports both-axis UIA scrolling");
        double position{}; check(scroll->get_CurrentVerticalScrollPercent(&position), "Read vertical scroll");
        require(position > 99, "Virtual grid reaches end");
        require(SendMessageW(app.window, WM_APP + 60, 11, 0) == 1, "Task Manager shares one Direct2D target");
        PostMessageW(app.window, WM_CLOSE, 0, 0);
        require(WaitForSingleObject(app.info.hProcess, 5000) == WAIT_OBJECT_0, "App closes without pending sampler polling");
        DWORD exit{}; GetExitCodeProcess(app.info.hProcess, &exit); require(exit == 0, "Task Manager clean shutdown");
        std::cout << assertions << " Task Manager desktop UIA assertions passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL after " << assertions << " assertions: " << e.what() << '\n'; return 1; }
}
