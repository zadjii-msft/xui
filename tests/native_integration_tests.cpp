#include "xui/application.hpp"
#include "xui/native_edit.hpp"
#include "../src/drawing.hpp"
#include <windows.h>
#include <imm.h>
#include <msctf.h>
#include <wrl/client.h>
#include <commctrl.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace xui {
struct DrawingTestAccess {
    static void lose_next_frame() { Drawing::end_result_override_ = D2DERR_RECREATE_TARGET; }
};
}
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void native_vertical_centering() {
    const auto host = CreateWindowExW(0, L"STATIC", L"", WS_POPUP,
        0, 0, 800, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(host != nullptr, "Create native geometry host");
    struct Host { HWND value; ~Host() { DestroyWindow(value); } } lifetime{host};
    xui::NativeEditBridge bridge;
    bridge.attach(host, 100);
    bridge.set_insets({10, 10, 10, 10});
    bridge.set_placeholder(L"Ag");
    const xui::Rect field{20, 16, 300, 48};
    bridge.arrange(field);
    const auto edit = bridge.window();
    const auto ink_rows = [&] {
        RECT client{};
        require(GetClientRect(edit, &client), "Read native line client");
        const auto screen = GetDC(edit);
        const auto dc = CreateCompatibleDC(screen);
        const auto bitmap = CreateCompatibleBitmap(screen, client.right, client.bottom);
        ReleaseDC(edit, screen);
        require(dc && bitmap, "Create native line capture");
        const auto previous = SelectObject(dc, bitmap);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        SendMessageW(edit, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND);
        const auto background = GetPixel(dc, client.right - 1, 0);
        int first = client.bottom, last = -1;
        for (int y = 0; y < client.bottom; ++y) {
            for (int x = 0; x < client.right; ++x) {
                if (GetPixel(dc, x, y) != background) {
                    first = std::min(first, y);
                    last = y;
                }
            }
        }
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
        require(last >= first, "Native line capture contains visible text");
        return std::pair{first, last};
    };
    for (const UINT dpi : {96u, 144u, 192u}) {
        bridge.set_dpi(dpi);
        const auto font = reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0));
        const auto dc = GetDC(edit);
        const auto previous = SelectObject(dc, font);
        TEXTMETRICW metrics{};
        const auto measured = GetTextMetricsW(dc, &metrics);
        SelectObject(dc, previous);
        ReleaseDC(edit, dc);
        require(measured, "Read actual native line metrics");
        RECT rect{}, format{};
        require(GetWindowRect(edit, &rect), "Read native line bounds");
        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&rect), 2);
        SendMessageW(edit, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
        require(rect.bottom - rect.top == metrics.tmHeight, "Native EDIT height matches its measured line");
        const int center = MulDiv(static_cast<int>(2 * field.y + field.height), dpi, 96);
        require(std::abs(rect.top + rect.bottom - center) <= 1,
            "Native line is vertically centered in the 48 DIP field at each DPI");
        require(bridge.bounds().y == field.y && bridge.bounds().height == field.height,
            "Native line centering preserves external field bounds");
        const int placeholder_baseline = rect.top + format.top + metrics.tmAscent;
        SetWindowTextW(edit, L"");
        const auto placeholder = ink_rows();
        for (const wchar_t ch : std::wstring_view(L"Ag")) SendMessageW(edit, WM_CHAR, ch, 0);
        const auto position = SendMessageW(edit, EM_POSFROMCHAR, 0, 0);
        require(position != -1 && rect.top + static_cast<short>(HIWORD(position)) + metrics.tmAscent ==
            placeholder_baseline, "Placeholder and native text share the measured baseline");
        require(ink_rows() == placeholder, "Placeholder and typed text have identical vertical ink bounds");
        SetWindowTextW(edit, L"");
        bridge.arrange({field.x, field.y, field.width, 22});
        RECT clipped{};
        GetClientRect(edit, &clipped);
        require(clipped.bottom <= MulDiv(2, dpi, 96), "Short fields clamp the line to available inset height");
        bridge.arrange(field);
    }
}
template<class F> void wait(F&& predicate, const char* message) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (!predicate()) {
        require(std::chrono::steady_clock::now() < end, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
BOOL CALLBACK find(HWND hwnd, LPARAM data) {
    DWORD process{};
    GetWindowThreadProcessId(hwnd, &process);
    wchar_t title[80]{};
    GetWindowTextW(hwnd, title, 80);
    if (process == GetCurrentProcessId() && std::wstring_view(title) == L"XUI native integration") {
        *reinterpret_cast<HWND*>(data) = hwnd;
        return FALSE;
    }
    return TRUE;
}
std::wstring text(HWND hwnd) {
    wchar_t value[2048]{};
    GetWindowTextW(hwnd, value, static_cast<int>(std::size(value)));
    return value;
}
LRESULT CALLBACK ime_observer(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    if (message == WM_IME_STARTCOMPOSITION || message == WM_IME_COMPOSITION ||
        message == WM_IME_ENDCOMPOSITION || message == WM_IME_NOTIFY || message == WM_IME_SETCONTEXT)
        ++*reinterpret_cast<int*>(data);
    return DefSubclassProc(hwnd, message, wp, lp);
}
struct PrivateDesktop {
    HWINSTA original_station{GetProcessWindowStation()}, station{};
    HDESK original_desktop{GetThreadDesktop(GetCurrentThreadId())}, desktop{};
    PrivateDesktop() {
        try {
            station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
            require(station && SetProcessWindowStation(station), "Create private clipboard window station");
            desktop = CreateDesktopW(L"XuiClipboard", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
            require(desktop && SetThreadDesktop(desktop), "Create private clipboard desktop");
        } catch (...) { release(); throw; }
    }
    ~PrivateDesktop() { release(); }
    void release() {
        if (!SetThreadDesktop(original_desktop) || !SetProcessWindowStation(original_station))
            std::cerr << "Cannot restore the isolated test desktop: " << GetLastError() << '\n';
        if (desktop && !CloseDesktop(desktop)) std::cerr << "Cannot close the isolated test desktop\n";
        if (station && !CloseWindowStation(station)) std::cerr << "Cannot close the isolated test station\n";
    }
};
void clipboard(HWND host, HWND edit, xui::Window& window) {
    const std::wstring expected = L"Copy \u65e5\u672c \U0001f642";
    window.copy_text(expected);
    require(OpenClipboard(host), "Open isolated clipboard");
    const auto storage = GetClipboardData(CF_UNICODETEXT);
    const auto data = storage ? static_cast<const wchar_t*>(GlobalLock(storage)) : nullptr;
    const bool matches = data && expected == data;
    if (data) GlobalUnlock(storage);
    CloseClipboard();
    require(matches, "Public clipboard API preserves Unicode");
    SetWindowTextW(edit, L"");
    SendMessageW(edit, WM_PASTE, 0, 0);
    require(text(edit) == expected, "Native EDIT pastes Unicode clipboard text");
    SendMessageW(edit, EM_SETSEL, 5, static_cast<LPARAM>(expected.size()));
    SendMessageW(edit, WM_COPY, 0, 0);
    SetWindowTextW(edit, L"");
    SendMessageW(edit, WM_PASTE, 0, 0);
    require(text(edit) == expected.substr(5), "Native copy preserves selection and surrogate pairs");
    SendMessageW(edit, EM_SETSEL, 0, -1);
    SendMessageW(edit, WM_CUT, 0, 0);
    require(text(edit).empty() && SendMessageW(edit, EM_CANUNDO, 0, 0), "Native cut supports undo");
    SendMessageW(edit, WM_UNDO, 0, 0);
    require(text(edit) == expected.substr(5), "Native undo restores cut Unicode text");
    SetWindowTextW(edit, std::wstring(1020, L'a').c_str());
    SendMessageW(edit, EM_SETSEL, 1020, 1020);
    SendMessageW(edit, WM_PASTE, 0, 0);
    require(text(edit) == std::wstring(1020, L'a') + L"\u65e5\u672c ",
        "Native paste respects the UTF-16 limit without a truncated surrogate pair");
}
void run_window(HDESK private_desktop = nullptr) {
    xui::Window window({L"XUI native integration", {500, 390}});
    auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto input = std::make_shared<xui::TextInput>(L"Native text");
    auto disabled = std::make_shared<xui::TextInput>(L"Disabled text");
    disabled->set_text(L"Keep selection");
    disabled->set_enabled(false);
    auto button = std::make_shared<xui::Button>(L"Focus anchor");
    auto toggle = std::make_shared<xui::Toggle>(L"Contrast check");
    toggle->set_checked(true);
    root->add(input);
    root->add(disabled);
    root->add(button);
    root->add(toggle);
    window.set_content(root);
    std::atomic<int> completed{};
    int changes{}, submits{}, shortcuts{};
    input->on_change([&](const std::wstring&) { ++changes; });
    input->on_submit([&] { ++submits; });
    HWND edit{}, disabled_edit{};
    int forwarding{};
    window.on_key([&](const xui::KeyEvent& event) {
        if (event.key == xui::Key::f6) { ++shortcuts; return true; }
        if (event.key != xui::Key::f1 && event.key != xui::Key::f2 && event.key != xui::Key::f3) return false;
        HWND host{};
        EnumWindows(find, reinterpret_cast<LPARAM>(&host));
        edit = FindWindowExW(host, nullptr, L"EDIT", nullptr);
        disabled_edit = FindWindowExW(host, edit, L"EDIT", nullptr);
        require(edit && disabled_edit, "Find both native inputs");
        if (event.key == xui::Key::f1) {
            require(window.focus(*input), "Native application focus succeeds");
            SendMessageW(disabled_edit, EM_SETSEL, 2, 4);
            require(!window.focus(*disabled, true), "Disabled select-all focus fails before native side effects");
            DWORD start{}, end{};
            SendMessageW(disabled_edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            require(start == 2 && end == 4 && GetFocus() == edit, "Rejected focus preserves selection and keyboard focus");
            if (private_desktop) {
                clipboard(host, edit, window);
                completed = 1;
                window.close();
                return true;
            }
            const std::wstring unicode = L"A\u65e5\U0001f642";
            for (const auto ch : unicode) SendMessageW(edit, WM_CHAR, ch, 0);
            require(input->text() == unicode && text(edit) == unicode, "Native WM_CHAR preserves UTF-16 surrogate input");
            SendMessageW(edit, EM_SETSEL, 1, 2);
            SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\u672c"));
            require(text(edit) == L"A\u672c\U0001f642", "Native selection replacement preserves surrounding surrogates");
            require(SendMessageW(edit, EM_CANUNDO, 0, 0), "Native replacement records undo");
            SendMessageW(edit, WM_UNDO, 0, 0);
            require(text(edit) == unicode && input->text() == unicode, "Native undo updates retained committed text");
            const auto previous = changes;
            SendMessageW(edit, EM_SETSEL, 1, 3);
            window.set_theme(xui::ThemeMode::high_contrast);
            SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            require(start == 1 && end == 3 && input->text() == unicode && changes == previous,
                "Theme changes preserve native text, selection and committed callback count");
            require(SetWindowSubclass(edit, ime_observer, 90, reinterpret_cast<DWORD_PTR>(&forwarding)),
                "Attach native IME message observer");
            SendMessageW(edit, WM_IME_STARTCOMPOSITION, 0, 0);
            SendMessageW(edit, WM_IME_COMPOSITION, 0, 0);
            SendMessageW(edit, WM_IME_NOTIFY, IMN_CLOSESTATUSWINDOW, 0);
            SetWindowTextW(edit, L"Preedit \u65e5");
            require(changes == previous && input->text() == unicode, "Composition does not publish preedit text");
            completed = 1;
        } else if (event.key == xui::Key::f2) {
            require(changes > 0 && submits == 0 && shortcuts == 0 && GetFocus() == edit,
                "Composition suppresses shortcuts, submit and Tab traversal");
            const auto before = changes;
            SendMessageW(edit, WM_IME_ENDCOMPOSITION, 0, 0);
            require(changes == before + 1 && input->text() == text(edit), "Composition end publishes exactly one committed change");
            SendMessageW(edit, WM_IME_ENDCOMPOSITION, 0, 0);
            require(changes == before + 1 && forwarding >= 5, "Repeated composition end is quiet and IME messages reach the EDIT HWND");
            require(RemoveWindowSubclass(edit, ime_observer, 90), "Remove IME message observer");
            require(window.focus(*button), "Move focus away from native input");
            completed = 2;
        } else {
            require(shortcuts == 1 && submits == 1, "Ordinary shortcuts and submit resume after composition");
            const auto palette = xui::Palette::system(xui::ThemeMode::high_contrast);
            const auto matches = [](D2D1_COLOR_F color, int index) {
                const auto native = GetSysColor(index);
                return std::lround(color.r * 255) == GetRValue(native) &&
                    std::lround(color.g * 255) == GetGValue(native) && std::lround(color.b * 255) == GetBValue(native);
            };
            require(palette.high_contrast && matches(palette.selection, COLOR_HIGHLIGHT) &&
                matches(palette.selection_text, COLOR_HIGHLIGHTTEXT) && matches(palette.accent, COLOR_HIGHLIGHT) &&
                matches(palette.disabled, COLOR_GRAYTEXT), "Explicit contrast uses system selection, focus and disabled colors");
            xui::DrawingTestAccess::lose_next_frame();
            button->set_name(L"Recreated target");
            completed = 3;
        }
        return true;
    });
    std::exception_ptr failure;
    std::jthread driver([&] {
        HWND host{};
        try {
            if (private_desktop) require(SetThreadDesktop(private_desktop), "Attach driver to private clipboard desktop");
            wait([&] { EnumWindows(find, reinterpret_cast<LPARAM>(&host)); return host != nullptr; }, "Find native integration host");
            PostMessageW(host, WM_KEYDOWN, VK_F1, 0);
            wait([&] { return completed.load() >= 1; }, "Complete native editing checks");
            if (private_desktop) return;
            const HWND native = FindWindowExW(host, nullptr, L"EDIT", nullptr);
            PostMessageW(native, WM_KEYDOWN, VK_F6, 0);
            PostMessageW(native, WM_KEYDOWN, VK_RETURN, 0);
            PostMessageW(native, WM_KEYDOWN, VK_TAB, 0);
            PostMessageW(host, WM_KEYDOWN, VK_F2, 0);
            wait([&] { return completed.load() >= 2; }, "Complete deterministic composition checks");
            PostMessageW(native, WM_KEYDOWN, VK_F6, 0);
            PostMessageW(native, WM_KEYDOWN, VK_RETURN, 0);
            const auto paints = SendMessageW(host, WM_APP + 60, 0, 0);
            const auto layouts = SendMessageW(host, WM_APP + 60, 12, 0);
            PostMessageW(host, WM_KEYDOWN, VK_F3, 0);
            wait([&] {
                return completed.load() >= 3 && SendMessageW(host, WM_APP + 60, 0, 0) >= paints + 2 &&
                    SendMessageW(host, WM_APP + 60, 11, 0) == 1;
            }, "Injected EndDraw device loss recreates one target and schedules a replacement frame");
            require(SendMessageW(host, WM_APP + 60, 12, 0) == layouts + 1,
                "Device recreation retains unchanged DirectWrite layouts");
            for (const UINT dpi : {96u, 120u, 144u, 192u}) {
                const auto prior = SendMessageW(host, WM_APP + 60, 2, 0);
                RECT suggested{40, 40, 40 + MulDiv(500, dpi, 96), 40 + MulDiv(390, dpi, 96)};
                SendMessageW(host, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
                wait([&] { return SendMessageW(host, WM_APP + 60, 2, 0) > prior; }, "DPI layout completes");
                LOGFONTW font{};
                const auto handle = reinterpret_cast<HFONT>(SendMessageW(native, WM_GETFONT, 0, 0));
                require(GetObjectW(handle, sizeof(font), &font) == sizeof(font) &&
                    font.lfHeight == -MulDiv(static_cast<int>(xui::VisualMetrics::body_size), dpi, 96),
                    "DPI changes install the exact native pixel font size");
                require(text(native).starts_with(L"Preedit"), "DPI changes retain native committed text");
            }
            SendMessageW(host, WM_APP + 12, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            const auto idle = SendMessageW(host, WM_APP + 60, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            require(SendMessageW(host, WM_APP + 60, 0, 0) == idle, "Device recovery returns to event-driven idle");
        } catch (...) { failure = std::current_exception(); }
        if (host) PostMessageW(host, WM_CLOSE, 0, 0);
    });
    const auto result = xui::Application::run(window);
    driver.join();
    if (failure) std::rethrow_exception(failure);
    if (result) std::wcerr << window.error() << '\n';
    require(result == 0 && xui::Drawing::live_targets() == 0, "Native integration window closes and releases its target");
}
void isolated_process(const wchar_t* argument) {
    const auto before = GetClipboardSequenceNumber();
    wchar_t executable[32768]{};
    require(GetModuleFileNameW(nullptr, executable, 32768), "Locate clipboard subprocess");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" " + argument;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    const auto job = CreateJobObjectW(nullptr, nullptr);
    require(job != nullptr, "Create isolated-test process lifetime");
    struct Job { HANDLE value; ~Job() { CloseHandle(value); } } lifetime{job};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    require(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
        "Bound the isolated test process tree");
    require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &startup, &process),
        "Start private clipboard subprocess");
    const bool assigned = AssignProcessToJobObject(job, process.hProcess) != FALSE;
    const bool resumed = assigned && ResumeThread(process.hThread) != static_cast<DWORD>(-1);
    if (!resumed) TerminateProcess(process.hProcess, 1);
    const auto stopped = WaitForSingleObject(process.hProcess, 20000);
    if (stopped == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 1);
    DWORD result{1};
    GetExitCodeProcess(process.hProcess, &result);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    require(resumed && stopped == WAIT_OBJECT_0 && result == 0, "Isolated Windows integration checks pass");
    require(GetClipboardSequenceNumber() == before, "Clipboard tests leave the interactive clipboard unchanged");
}
void input_environment() {
    const int count = GetKeyboardLayoutList(0, nullptr);
    require(count > 0, "Read installed keyboard layouts");
    std::vector<HKL> layouts(static_cast<size_t>(count));
    require(GetKeyboardLayoutList(count, layouts.data()) == count, "Read keyboard layout handles");
    for (const auto layout : layouts)
        std::cout << "keyboard_layout=" << layout << " imm_ime=" << (ImmIsIME(layout) != FALSE) << '\n';
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "Initialize input profile inspection");
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    Microsoft::WRL::ComPtr<ITfInputProcessorProfiles> profiles;
    require(SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&profiles))), "Read installed TSF profiles");
    int enabled_keyboards{};
    for (const LANGID language : {LANGID{0x0409}, LANGID{0x0411}, LANGID{0x0804}}) {
        Microsoft::WRL::ComPtr<IEnumTfLanguageProfiles> enumerator;
        require(SUCCEEDED(profiles->EnumLanguageProfiles(language, &enumerator)), "Enumerate TSF language profiles");
        TF_LANGUAGEPROFILE profile{};
        ULONG fetched{};
        HRESULT next{};
        while ((next = enumerator->Next(1, &profile, &fetched)) == S_OK && fetched) {
            if (profile.catid != GUID_TFCAT_TIP_KEYBOARD) continue;
            BOOL enabled{};
            require(SUCCEEDED(profiles->IsEnabledLanguageProfile(profile.clsid, language, profile.guidProfile, &enabled)),
                "Read TSF profile enabled state");
            std::cout << "tsf_language=" << language << " enabled=" << enabled << " active=" << profile.fActive << '\n';
            if (enabled) ++enabled_keyboards;
        }
        require(SUCCEEDED(next), "Complete TSF profile enumeration");
    }
    std::cout << "enabled_tsf_keyboards=" << enabled_keyboards
        << "; real IME candidate/composition and Narrator sessions require manual coverage\n";
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && std::wstring_view(argv[1]) == L"--geometry") {
            native_vertical_centering();
            std::cout << "Native vertical centering passed at 96, 144 and 192 DPI\n";
            return 0;
        }
        if (argc == 2 && std::wstring_view(argv[1]) == L"--clipboard") {
            PrivateDesktop desktop;
            run_window(desktop.desktop);
            std::cout << "Private-window-station clipboard: Unicode copy/paste/cut/undo passed\n";
            return 0;
        }
        xui::Window unopened;
        bool rejected{};
        try { unopened.copy_text(L"Must not change the clipboard"); }
        catch (const std::logic_error&) { rejected = true; }
        require(rejected, "Clipboard API rejects a window without a clipboard owner");
        input_environment();
        native_vertical_centering();
        run_window();
        isolated_process(L"--clipboard");
        std::cout << "Native integration passed: editing, composition guards, DPI fonts, contrast, injected device loss, isolated clipboard\n";
        return 0;
    } catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; return 1; }
}
