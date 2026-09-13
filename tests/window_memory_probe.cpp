#include "xui/application.hpp"
#include <windows.h>
#include <d2d1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <psapi.h>
#include <iostream>
#include <string>
#include "resource_probe.hpp"
#include "../src/control_accessibility.hpp"
#define wWinMain SampleMain
#include "../demo/task_manager.cpp"
#undef wWinMain

using Microsoft::WRL::ComPtr;
static ComPtr<ID2D1Factory> factory;
static ComPtr<ID2D1HwndRenderTarget> target;
static ComPtr<ID2D1RenderTarget> drawing_target;
static ComPtr<ID3D11Device> device;
static ComPtr<IDXGISwapChain1> swap_chain;
static bool flip{};
static ComPtr<ID2D1SolidColorBrush> brush;
static int mode{};
static int saved_mode{};
static long paints{};
static long sizes{};
static int requested_width{}, requested_height{};
static D2D1_RENDER_TARGET_USAGE target_usage = D2D1_RENDER_TARGET_USAGE_NONE;
static D2D1_PRESENT_OPTIONS present_options = D2D1_PRESENT_OPTIONS_NONE;
static D2D1_FEATURE_LEVEL feature_level = D2D1_FEATURE_LEVEL_DEFAULT;
static void resources(const char* phase) {
    std::cout << "phase=" << phase << ' ';
    resource_probe::heaps();
    DWORD handles{};
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    std::cout << "handles=" << handles << " gdi=" << GetGuiResources(GetCurrentProcess(), 0)
              << " user=" << GetGuiResources(GetCurrentProcess(), 1) << " raw_targets=" << !!drawing_target;
    if (drawing_target) {
        const auto hardware = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_HARDWARE);
        std::cout << " hardware_supported=" << drawing_target->IsSupported(&hardware);
    }
    std::cout << '\n' << std::flush;
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
static LRESULT CALLBACK heap_hook(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0 && reinterpret_cast<CWPSTRUCT*>(lp)->message == WM_APP + 62)
        resources("requested-xui-heap-snapshot");
    return CallNextHookEx(nullptr, code, wp, lp);
}

static void require(HRESULT result) {
    if (FAILED(result)) { std::cerr << "HRESULT=" << std::hex << result << '\n'; ExitProcess(2); }
}
static void paint(HWND window) {
    RECT rect{};
    GetClientRect(window, &rect);
    const auto size = D2D1::SizeU(rect.right, rect.bottom);
    if (flip && !swap_chain) {
        require(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr));
        ComPtr<IDXGIDevice> dxgi; require(device.As(&dxgi));
        ComPtr<IDXGIAdapter> adapter; require(dxgi->GetAdapter(&adapter));
        ComPtr<IDXGIFactory2> factory2; require(adapter->GetParent(IID_PPV_ARGS(&factory2)));
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = size.width; description.Height = size.height;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        require(factory2->CreateSwapChainForHwnd(device.Get(), window, &description, nullptr, nullptr, &swap_chain));
        require(factory2->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER));
    }
    if (flip && drawing_target && (drawing_target->GetPixelSize().width != size.width ||
        drawing_target->GetPixelSize().height != size.height)) {
        brush.Reset(); drawing_target.Reset();
        require(swap_chain->ResizeBuffers(0, size.width, size.height, DXGI_FORMAT_UNKNOWN, 0));
    }
    if (flip && !drawing_target) {
        ComPtr<IDXGISurface> surface; require(swap_chain->GetBuffer(0, IID_PPV_ARGS(&surface)));
        require(factory->CreateDxgiSurfaceRenderTarget(surface.Get(),
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_HARDWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)), &drawing_target));
        require(drawing_target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush));
        resources("flip-target-created-before-first-draw");
    } else if (!flip && !target) {
        auto properties = D2D1::RenderTargetProperties();
        properties.usage = target_usage;
        properties.minLevel = feature_level;
        require(factory->CreateHwndRenderTarget(properties,
            D2D1::HwndRenderTargetProperties(window, size, present_options), &target));
        require(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush));
        drawing_target = target;
        resources("target-created-before-first-draw");
    } else if (!flip && (target->GetPixelSize().width != size.width || target->GetPixelSize().height != size.height)) {
        require(target->Resize(size));
    }
    drawing_target->BeginDraw();
    drawing_target->Clear(D2D1::ColorF(0x181818));
    if (mode >= 2) {
        if (mode != 4)
            drawing_target->PushAxisAlignedClip(D2D1::RectF(20, 20, float(rect.right - 20), float(rect.bottom - 20)), D2D1_ANTIALIAS_MODE_ALIASED);
        drawing_target->FillRectangle(D2D1::RectF(30, 30, 200, 70), brush.Get());
        if (mode == 3) drawing_target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(30, 90, 200, 130), 6, 6), brush.Get());
        if (mode != 4)
            drawing_target->PopAxisAlignedClip();
    }
    require(drawing_target->EndDraw());
    if (flip) require(swap_chain->Present(1, 0));
    ++paints;
}
static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{}; BeginPaint(window, &ps);
        if (mode) paint(window);
        EndPaint(window, &ps); return 0;
    }
    if (message == WM_SIZE) { ++sizes; InvalidateRect(window, nullptr, FALSE); return 0; }
    if (message == WM_APP + 60) {
        if (wp == 40 || wp == 41) {
            if (!drawing_target) return 0;
            const auto size = drawing_target->GetPixelSize();
            return wp == 40 ? size.width : size.height;
        }
        return wp == 0 ? paints : wp == 2 ? sizes : wp == 11 ? !!drawing_target : 0;
    }
    if (message == WM_APP + 61) {
        brush.Reset(); drawing_target.Reset(); target.Reset(); swap_chain.Reset(); device.Reset(); factory.Reset();
        mode = 0;
        resources("target-and-factory-released");
        return 0;
    }
    if (message == WM_APP + 62) {
        resources("requested-heap-snapshot");
        return 0;
    }
    if (message == WM_APP + 63) {
        brush.Reset(); drawing_target.Reset(); target.Reset(); swap_chain.Reset(); device.Reset();
        mode = 0;
        resources("target-released-factory-retained");
        return 0;
    }
    if (message == WM_APP + 64) {
        if (!factory) require(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
        mode = saved_mode;
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
        resources("target-recreated");
        return 0;
    }
    if (message == WM_TIMER) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}
int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring kind = argc > 1 ? argv[1] : L"clear";
    flip = kind == L"flip";
    const int width = argc > 2 ? _wtoi(argv[2]) : 800, height = argc > 3 ? _wtoi(argv[3]) : 780;
    const int lifetimes = argc > 8 ? _wtoi(argv[8]) : 1;
    if (width < 1 || width > 8192 || height < 1 || height > 8192 || lifetimes < 1 || lifetimes > 50) return 5;
    if (argc > 5) target_usage = static_cast<D2D1_RENDER_TARGET_USAGE>(_wtoi(argv[5]));
    if (argc > 6) present_options = static_cast<D2D1_PRESENT_OPTIONS>(_wtoi(argv[6]));
    if (argc > 7) feature_level = static_cast<D2D1_FEATURE_LEVEL>(_wtoi(argv[7]));
    resources("entry");
    std::cout << "control_snapshot_bytes=" << sizeof(xui::ControlSnapshot)
        << " control_accessibility_bytes=" << sizeof(xui::ControlAccessibility)
        << " collection_selection_bytes=" << sizeof(xui::CollectionSelection) << '\n';
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
        const auto hook = SetWindowsHookExW(WH_CALLWNDPROC, heap_hook, nullptr, GetCurrentThreadId());
        if (!hook) return 4;
        for (int life = 0; life < lifetimes; ++life) {
            {
                xui::Window window({L"Blank XUI memory probe", {float(width), float(height)}});
                window.set_content(std::make_shared<xui::Stack>(xui::Axis::vertical));
                const int result = xui::Application::run(window);
                std::wcerr << L"result=" << result << L" error=" << window.error() << L'\n';
                if (result) { UnhookWindowsHookEx(hook); return result; }
            }
            resources("blank-xui-destroyed");
            if (life + 1 < lifetimes) Sleep(2000);
        }
        UnhookWindowsHookEx(hook);
        return 0;
    }
    mode = kind == L"native" ? 0 : kind == L"clear" ? 1 : kind == L"clip" ? 2 : kind == L"rounded" || flip ? 3 : 4;
    saved_mode = mode;
    require(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    if (mode) require(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{}; cls.lpfnWndProc = proc; cls.hInstance = instance; cls.lpszClassName = L"Xui.MemoryProbe";
    RegisterClassW(&cls);
    RECT outer{0, 0, width, height};
    AdjustWindowRectExForDpi(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0, 96);
    for (int life = 0; life < lifetimes; ++life) {
    auto window = CreateWindowExW(0, cls.lpszClassName, L"Direct2D memory probe", WS_OVERLAPPEDWINDOW,
        20, 20, outer.right - outer.left, outer.bottom - outer.top, nullptr, nullptr, instance, nullptr);
    if (!window) return 3;
    resources("native-window-before-paint");
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    resources("first-paint");
    if (argc > 4 && _wtoi(argv[4])) SetTimer(window, 1, 2500, nullptr);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    brush.Reset(); drawing_target.Reset(); target.Reset(); swap_chain.Reset(); device.Reset();
    resources("native-window-and-target-destroyed-factory-retained");
    if (life + 1 < lifetimes) { mode = saved_mode; Sleep(2000); }
    }
    factory.Reset();
    resources("native-window-and-d2d-closed");
    CoUninitialize();
}
