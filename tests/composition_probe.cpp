// Measures one XUI presentation configuration per process. Prints one JSON object on stdout.
// Usage: xui_composition_probe <classic|classic-acrylic|composition|composition-card> <width> <height> [resize-cycles]
#include "xui/application.hpp"
#include "xui/control_styling.hpp"
#include "xui/styling.hpp"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <dxgi1_4.h>
#include <dwmapi.h>
#include "xui/theme.hpp"
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace xui;
using Clock = std::chrono::steady_clock;
constexpr UINT metrics_message = WM_APP + 60;

double ms(Clock::duration value) { return std::chrono::duration<double, std::milli>(value).count(); }
double cpu_ms() {
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    const auto value = [](FILETIME time) { return (ULONGLONG(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
    return (value(kernel) + value(user)) / 1e4;
}
double mcycles() {
    ULONG64 cycles{};
    QueryProcessCycleTime(GetCurrentProcess(), &cycles);
    return cycles / 1e6;
}
int threads() {
    int count{};
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry{sizeof(entry)};
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
        if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++count;
    CloseHandle(snapshot);
    return count;
}
// A separate animated GDI window behind the measured window makes DWM recompute backdrop blur.
LRESULT CALLBACK animate_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    static int phase{};
    if (message == WM_TIMER) { ++phase; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        const COLORREF colors[]{RGB(220, 30, 30), RGB(30, 200, 30), RGB(30, 30, 230), RGB(240, 220, 20)};
        const int band = 80;
        for (int x = -band * 4; x < client.right + band; x += band) {
            RECT r{x + (phase * 4) % (band * 4), 0, x + (phase * 4) % (band * 4) + band, client.bottom};
            const HBRUSH brush = CreateSolidBrush(colors[((x / band) % 4 + 4) % 4]);
            FillRect(dc, &r, brush);
            DeleteObject(brush);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
int animate(int x, int y, int width, int height, int seconds) {
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = animate_proc;
    cls.lpszClassName = L"Xui.CompositionProbeAnimation";
    RegisterClassW(&cls);
    const HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, L"",
        WS_POPUP, x, y, width, height, nullptr, nullptr, cls.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOWNA);
    SetTimer(hwnd, 1, 16, nullptr);
    SetTimer(nullptr, 0, seconds * 1000, nullptr);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        if (message.message == WM_TIMER && !message.hwnd) break;
        DispatchMessageW(&message);
    }
    return 0;
}
struct Memory { double private_mib, working_set_mib, gpu_local_mib, gpu_nonlocal_mib; };
Memory memory() {
    PROCESS_MEMORY_COUNTERS_EX counters{sizeof(counters)};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    Memory result{counters.PrivateUsage / 1048576.0, counters.WorkingSetSize / 1048576.0, 0, 0};
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
            if (FAILED(adapter.As(&adapter3))) continue;
            DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
            adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
            adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal);
            result.gpu_local_mib += local.CurrentUsage / 1048576.0;
            result.gpu_nonlocal_mib += nonlocal.CurrentUsage / 1048576.0;
        }
    }
    return result;
}
bool loaded(const wchar_t* name) { return GetModuleHandleW(name) != nullptr; }
void pump_until(const std::function<bool()>& done, int timeout_ms = 5000) {
    const auto end = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!done() && Clock::now() < end) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
    }
}
void pump_for(int milliseconds) {
    // Block between messages so idle measurements count only the window's own work.
    const auto end = Clock::now() + std::chrono::milliseconds(milliseconds);
    for (;;) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
        if (remaining <= 0) return;
        MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(remaining), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}
LRESULT metric(HWND hwnd, WPARAM key) { return SendMessageW(hwnd, metrics_message, key, 0); }
// Observed on Windows build 26200 while another application was foreground: SetWindowPos(HWND_TOPMOST)
// took effect only in a process that already owned a window created with WS_EX_TOPMOST.
// A hidden helper lets the measured window rise above the animated background.
void enable_topmost_promotion() {
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = L"Xui.CompositionProbeTopmost";
    RegisterClassW(&cls);
    CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, L"", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, cls.hInstance, nullptr);
}
double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, static_cast<std::size_t>(fraction * (values.size() - 1) + 0.5))];
}
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring kind = argc > 1 ? argv[1] : L"classic";
    if (kind == L"animate")
        return animate(_wtoi(argv[2]), _wtoi(argv[3]), _wtoi(argv[4]), _wtoi(argv[5]), _wtoi(argv[6]));
    const float width = argc > 2 ? static_cast<float>(_wtof(argv[2])) : 800;
    const float height = argc > 3 ? static_cast<float>(_wtof(argv[3])) : 600;
    const int cycles = argc > 4 ? _wtoi(argv[4]) : 20;
    // Hold mode keeps the window still for N seconds so an external sampler can read DWM counters.
    const int hold_seconds = argc > 5 ? _wtoi(argv[5]) : 0;
    const bool composition = kind.starts_with(L"composition");
    const bool card = kind == L"composition-card";
    const bool acrylic = kind != L"classic";
    const auto launched = Clock::now();
    const auto entry_memory = memory();

    Application app;
    WindowOptions options;
    options.title = L"XUI composition measurement";
    options.size = {width, height};
    options.visual_style = VisualStyle::winui;
#ifndef XUI_PROBE_BASELINE
    options.composition = composition;
#endif
    options.transparent = card;
    auto owned = app.create_window(options);
    auto& window = *owned;
    enable_topmost_promotion();
    // A solid magenta window behind the measured window distinguishes the backdrop material from a
    // hole whose backdrop visual has not caught up: raw magenta means no material was composed.
    const HWND magenta = [&] {
        WNDCLASSW cls{};
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpfnWndProc = DefWindowProcW;
        cls.hbrBackground = CreateSolidBrush(RGB(255, 0, 255));
        cls.lpszClassName = L"Xui.CompositionProbeMagenta";
        RegisterClassW(&cls);
        const int dpi = static_cast<int>(GetDpiForSystem());
        const HWND result = hold_seconds > 0 ? nullptr : CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            cls.lpszClassName, L"", WS_POPUP, 0, 0, static_cast<int>((width + 400) * dpi / 96),
            static_cast<int>((height + 300) * dpi / 96), nullptr, nullptr, cls.hInstance, nullptr);
        if (result) ShowWindow(result, SW_SHOWNA);
        return result;
    }();
    window.on_native_created([](HWND hwnd) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    });
    window.set_placement({40, 40, static_cast<int>(width * GetDpiForSystem() / 96), static_cast<int>(height * GetDpiForSystem() / 96), false});
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({24, 24, 24, 24});
    root->set_spacing(12);
    root->add(std::make_shared<Label>(L"Measurement workload"));
    auto panel = std::make_shared<Stack>(Axis::vertical);
    panel->set_padding({16, 16, 16, 16});
    panel->set_spacing(8);
#ifdef XUI_PROBE_BASELINE
    if (acrylic) panel->set_material(SurfaceMaterial::acrylic);
#else
    if (acrylic) panel->set_material(composition ? SurfaceMaterial::backdrop_acrylic : SurfaceMaterial::acrylic);
#endif
    else panel->set_surface(true);
    PartStyleValues rounded;
    rounded.corner_radius = 20.0f;
    panel->set_control_style_values(StylePart::root, rounded);
    for (int i = 0; i < 6; ++i) panel->add(std::make_shared<Label>(L"Row " + std::to_wstring(i + 1) + L" of retained text over the material"));
    auto editor = std::make_shared<TextInput>(L"Measured editor");
    editor->set_caption_visible(false);
    editor->set_text(L"Native text");
    panel->add(editor);
    panel->add(std::make_shared<Button>(L"Measured action"));
    root->add(panel, 1);
    window.set_content(root);

    app.show(window);
    const HWND hwnd = window.native_window();
    pump_until([&] { return metric(hwnd, 0) >= 1; });
    const double first_paint_ms = ms(Clock::now() - launched);
    const double first_paint_cpu = cpu_ms();
    pump_for(500);
    const auto shown_memory = memory();
    if (hold_seconds > 0) {
        RECT bounds{};
        GetWindowRect(hwnd, &bounds);
        const POINT center{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
        const HWND hit = WindowFromPoint(center);
        std::printf("{\"ready\":true}\n");
        std::fflush(stdout);
        const auto start_cycles = mcycles();
        const auto start_paints = metric(hwnd, 0);
        pump_for(hold_seconds * 1000);
        std::printf("{\"kind\":\"%ls\",\"hold_seconds\":%d,\"on_top\":%d,\"hold_mcycles\":%.2f,\"hold_paints\":%lld,"
            "\"private_mib\":%.2f,\"gpu_local_mib\":%.2f,\"backdrop_visuals\":%lld,\"threads\":%d}\n",
            kind.c_str(), hold_seconds, hit == hwnd || IsChild(hwnd, hit), mcycles() - start_cycles,
            static_cast<long long>(metric(hwnd, 0) - start_paints), memory().private_mib, memory().gpu_local_mib,
            static_cast<long long>(metric(hwnd, 39)), threads());
        std::fflush(stdout);
        window.close();
        pump_for(200);
        return 0;
    }

    // Paint latency: synchronous full invalidation and paint, timed on the UI thread.
    std::vector<double> paints;
    for (int i = 0; i < 120; ++i) {
        const auto start = Clock::now();
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
        paints.push_back(ms(Clock::now() - start));
    }
    const auto paint_cpu_start = cpu_ms();
    const auto paint_cycles_start = mcycles();
    for (int i = 0; i < 60; ++i) RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
    const double paint_cpu_per_frame = (cpu_ms() - paint_cpu_start) / 60;
    const double paint_mcycles_per_frame = (mcycles() - paint_cycles_start) / 60;

    // Idle: no repaint work should occur.
    const auto idle_paints = metric(hwnd, 0);
    const auto idle_cycles = mcycles();
    pump_for(2000);
    const double idle_mcycles = mcycles() - idle_cycles;
    const auto idle_paint_count = metric(hwnd, 0) - idle_paints;

    // Resize: alternate sizes, timing each synchronous resize plus repaint.
    std::vector<double> resizes;
    RECT outer{};
    GetWindowRect(hwnd, &outer);
    const int base_w = outer.right - outer.left, base_h = outer.bottom - outer.top;
    const auto resize_cycles_start = mcycles();
    for (int i = 0; i < cycles; ++i) {
        const int grow = (i % 2) ? 0 : 160;
        const auto start = Clock::now();
        SetWindowPos(hwnd, nullptr, 0, 0, base_w + grow, base_h + grow / 2, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateWindow(hwnd);
        pump_until([&] { return GetQueueStatus(QS_PAINT | QS_POSTMESSAGE) == 0; }, 200);
        resizes.push_back(ms(Clock::now() - start));
    }
    const double resize_mcycles_per_cycle = (mcycles() - resize_cycles_start) / std::max(1, cycles);
    pump_for(500);
    const auto resized_memory = memory();

    // Resize artifacts: after each growth step, count composed DWM frames until the newly exposed
    // edge shows the window background. Messages are pumped between frames so asynchronous
    // composition commits can complete. Zero means the first composed frame was already correct.
    int artifact_steps{}, artifact_misses{}, artifact_frames{}, backdrop_lag_frames{};
    COLORREF first_inside{};
    {
        const COLORREF expected = card ? CLR_INVALID : [&] {
            const auto colors = theme_colors(ThemeMode::dark, VisualStyle::winui);
            return RGB((colors.background >> 16) & 255, (colors.background >> 8) & 255, colors.background & 255);
        }();
        GetWindowRect(hwnd, &outer);
        const HDC screen = GetDC(nullptr);
        const int dpi = static_cast<int>(GetDpiForWindow(hwnd));
        for (int i = 0; i < 20 && !card; ++i) {
            const int w = outer.right - outer.left + 8 * (i + 1), h = outer.bottom - outer.top + 4 * (i + 1);
            SetWindowPos(hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            UpdateWindow(hwnd);
            ++artifact_steps;
            RECT client{};
            GetClientRect(hwnd, &client);
            POINT origin{};
            ClientToScreen(hwnd, &origin);
            // Sample at the panel's right edge in physical pixels; layout has already run for this size.
            const auto panel_bounds = panel->bounds();
            const int right = origin.x + MulDiv(static_cast<int>(panel_bounds.x + panel_bounds.width) + 24, dpi, 96);
            const int middle = origin.y + MulDiv(static_cast<int>(panel_bounds.y + panel_bounds.height / 2), dpi, 96);
            int frames{};
            for (; frames < 10; ++frames) {
                DwmFlush();
                const COLORREF seen = GetPixel(screen, right - MulDiv(8, dpi, 96), middle);
                const int difference = std::abs(GetRValue(seen) - GetRValue(expected)) +
                    std::abs(GetGValue(seen) - GetGValue(expected)) + std::abs(GetBValue(seen) - GetBValue(expected));
                // Inside the growing acrylic panel, raw magenta means its backdrop visual lags the content.
                const COLORREF inside = GetPixel(screen, right - MulDiv(24 + 4, dpi, 96), middle);
                // Tinted magenta is the composed material; unblended magenta means the backdrop visual is missing.
                const bool raw = GetRValue(inside) > 240 && GetGValue(inside) < 16 && GetBValue(inside) > 240;
                if (i == 0 && frames == 0) first_inside = inside;
                if (difference <= 12 && !raw) break;
                if (raw) ++backdrop_lag_frames;
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            }
            if (frames) ++artifact_misses;
            artifact_frames += frames;
        }
        ReleaseDC(nullptr, screen);
    }
    if (magenta) DestroyWindow(magenta);

    std::printf("{\"kind\":\"%ls\",\"width\":%g,\"height\":%g,\"dpi\":%u,"
        "\"first_paint_ms\":%.2f,\"first_paint_cpu_ms\":%.2f,"
        "\"paint_p50_ms\":%.3f,\"paint_p95_ms\":%.3f,\"paint_cpu_ms\":%.3f,\"paint_mcycles\":%.3f,"
        "\"idle_mcycles\":%.3f,\"idle_paints\":%lld,"
        "\"resize_p50_ms\":%.3f,\"resize_p95_ms\":%.3f,\"resize_max_ms\":%.3f,\"resize_mcycles\":%.3f,"
        "\"entry_private_mib\":%.2f,\"shown_private_mib\":%.2f,\"resized_private_mib\":%.2f,"
        "\"shown_ws_mib\":%.2f,\"resized_ws_mib\":%.2f,"
        "\"shown_gpu_local_mib\":%.2f,\"resized_gpu_local_mib\":%.2f,\"shown_gpu_nonlocal_mib\":%.2f,"
        "\"backdrop_visuals\":%lld,\"composition\":%lld,\"threads\":%d,\"artifact_steps\":%d,\"artifact_misses\":%d,\"artifact_frames\":%d,\"backdrop_lag_frames\":%d,\"panel_sample\":\"%02x%02x%02x\","
        "\"d3d11\":%d,\"coremessaging\":%d,\"dcomp\":%d,\"windows_ui\":%d,\"modules\":%d}\n",
        kind.c_str(), width, height, GetDpiForWindow(hwnd),
        first_paint_ms, first_paint_cpu,
        percentile(paints, 0.5), percentile(paints, 0.95), paint_cpu_per_frame, paint_mcycles_per_frame,
        idle_mcycles, static_cast<long long>(idle_paint_count),
        percentile(resizes, 0.5), percentile(resizes, 0.95), percentile(resizes, 1.0), resize_mcycles_per_cycle,
        entry_memory.private_mib, shown_memory.private_mib, resized_memory.private_mib,
        shown_memory.working_set_mib, resized_memory.working_set_mib,
        shown_memory.gpu_local_mib, resized_memory.gpu_local_mib, shown_memory.gpu_nonlocal_mib,
        static_cast<long long>(metric(hwnd, 39)), static_cast<long long>(metric(hwnd, 40)), threads(), artifact_steps, artifact_misses, artifact_frames, backdrop_lag_frames, GetRValue(first_inside), GetGValue(first_inside), GetBValue(first_inside),
        loaded(L"d3d11.dll"), loaded(L"CoreMessaging.dll"), loaded(L"dcomp.dll"), loaded(L"Windows.UI.dll"),
        [] {
            HMODULE modules[1024]; DWORD needed{};
            EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed);
            return static_cast<int>(needed / sizeof(HMODULE));
        }());
    std::fflush(stdout);
    window.close();
    pump_for(200);
    return 0;
}
