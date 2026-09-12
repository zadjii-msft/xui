#include "xui/application.hpp"
#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <psapi.h>
#include <iostream>
#include <string>
#include "resource_probe.hpp"
#define wWinMain SampleMain
#include "../demo/task_manager.cpp"
#undef wWinMain

using Microsoft::WRL::ComPtr;
static ComPtr<ID2D1Factory> factory;
static ComPtr<ID2D1HwndRenderTarget> target;
static ComPtr<ID2D1SolidColorBrush> brush;
static int mode{};
static long paints{};
static int requested_width{}, requested_height{};
static void resources(const char* phase) {
    std::cout << "phase=" << phase << ' ';
    resource_probe::heaps();
    DWORD handles{};
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    std::cout << "handles=" << handles << " gdi=" << GetGuiResources(GetCurrentProcess(), 0)
              << " user=" << GetGuiResources(GetCurrentProcess(), 1) << " raw_targets=" << !!target << '\n' << std::flush;
}
// The thread-local CBT hook changes only the initial extent, before the unmodified sample first paints.
static LRESULT CALLBACK create_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_CREATEWND) {
        wchar_t name[128]{};
        GetClassNameW(reinterpret_cast<HWND>(wp), name, 128);
        if (std::wstring_view(name) == L"Xui.Window.1") {
            auto* create = reinterpret_cast<CBT_CREATEWNDW*>(lp)->lpcs;
            RECT outer{0, 0, requested_width, requested_height};
            AdjustWindowRectExForDpi(&outer, create->style, FALSE, create->dwExStyle, 96);
            create->cx = outer.right - outer.left;
            create->cy = outer.bottom - outer.top;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static void require(HRESULT result) {
    if (FAILED(result)) { std::cerr << "HRESULT=" << std::hex << result << '\n'; ExitProcess(2); }
}
static void paint(HWND window) {
    RECT rect{};
    GetClientRect(window, &rect);
    const auto size = D2D1::SizeU(rect.right, rect.bottom);
    if (!target) {
        require(factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(window, size), &target));
        require(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush));
        resources("target-created-before-first-draw");
    } else if (target->GetPixelSize().width != size.width || target->GetPixelSize().height != size.height) {
        require(target->Resize(size));
    }
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0x181818));
    if (mode >= 2) {
        if (mode != 4)
            target->PushAxisAlignedClip(D2D1::RectF(20, 20, float(rect.right - 20), float(rect.bottom - 20)), D2D1_ANTIALIAS_MODE_ALIASED);
        target->FillRectangle(D2D1::RectF(30, 30, 200, 70), brush.Get());
        if (mode == 3) target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(30, 90, 200, 130), 6, 6), brush.Get());
        if (mode != 4)
            target->PopAxisAlignedClip();
    }
    require(target->EndDraw());
    ++paints;
}
static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{}; BeginPaint(window, &ps);
        if (mode) paint(window);
        EndPaint(window, &ps); return 0;
    }
    if (message == WM_SIZE) { InvalidateRect(window, nullptr, FALSE); return 0; }
    if (message == WM_APP + 60) return wp == 0 ? paints : wp == 11 ? !!target : 0;
    if (message == WM_APP + 61) {
        brush.Reset(); target.Reset(); factory.Reset();
        mode = 0;
        resources("target-and-factory-released");
        return 0;
    }
    if (message == WM_TIMER) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}
int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring kind = argc > 1 ? argv[1] : L"clear";
    const int width = argc > 2 ? _wtoi(argv[2]) : 800, height = argc > 3 ? _wtoi(argv[3]) : 780;
    resources("entry");
    if (kind == L"task") {
        requested_width = width; requested_height = height;
        const auto hook = SetWindowsHookExW(WH_CBT, create_hook, nullptr, GetCurrentThreadId());
        if (!hook) return 4;
        const int result = SampleMain(GetModuleHandleW(nullptr), nullptr, nullptr, SW_SHOWNORMAL);
        UnhookWindowsHookEx(hook);
        resources("sample-closed");
        return result;
    }
    if (kind == L"xui") {
        xui::Window window({L"Blank XUI memory probe", {float(width), float(height)}});
        window.set_content(std::make_shared<xui::Stack>(xui::Axis::vertical));
        const int result = xui::Application::run(window);
        std::wcerr << L"result=" << result << L" error=" << window.error() << L'\n';
        resources("blank-xui-closed");
        return result;
    }
    mode = kind == L"native" ? 0 : kind == L"clear" ? 1 : kind == L"clip" ? 2 : kind == L"rounded" ? 3 : 4;
    require(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    if (mode) require(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{}; cls.lpfnWndProc = proc; cls.hInstance = instance; cls.lpszClassName = L"Xui.MemoryProbe";
    RegisterClassW(&cls);
    RECT outer{0, 0, width, height};
    AdjustWindowRectExForDpi(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0, 96);
    auto window = CreateWindowExW(0, cls.lpszClassName, L"Direct2D memory probe", WS_OVERLAPPEDWINDOW,
        20, 20, outer.right - outer.left, outer.bottom - outer.top, nullptr, nullptr, instance, nullptr);
    if (!window) return 3;
    resources("native-window-before-paint");
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    resources("first-paint");
    if (argc > 4) SetTimer(window, 1, 2500, nullptr);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    brush.Reset(); target.Reset(); factory.Reset();
    resources("native-window-and-d2d-closed");
    CoUninitialize();
}
