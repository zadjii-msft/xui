#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <wrl/client.h>
#include <vector>
#include <stdexcept>

namespace owned_window_capture {
struct Pixels { int width{}, height{}; std::vector<DWORD> data; };
// HWND interop selects only the owned test window. No monitor or desktop item exists.
inline Pixels capture(HWND hwnd) {
    DWORD process{}; GetWindowThreadProcessId(hwnd, &process);
    if (process != GetCurrentProcessId()) throw std::runtime_error("Capture requires a window owned by this test process");
    using namespace winrt::Windows::Graphics;
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
    ComPtr<IDXGIDevice> dxgi; winrt::check_hresult(device.As(&dxgi));
    winrt::com_ptr<IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
    const auto direct = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
    const auto interop = winrt::get_activation_factory<Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    Capture::GraphicsCaptureItem item{nullptr};
    winrt::check_hresult(interop->CreateForWindow(hwnd, winrt::guid_of<Capture::GraphicsCaptureItem>(), winrt::put_abi(item)));
    auto pool = Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(direct,
        winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, item.Size());
    auto session = pool.CreateCaptureSession(item);
    session.IsCursorCaptureEnabled(false);
    struct Close {
        Capture::GraphicsCaptureSession& session;
        Capture::Direct3D11CaptureFramePool& pool;
        ~Close() { session.Close(); pool.Close(); }
    } close{session, pool};
    session.StartCapture();
    Capture::Direct3D11CaptureFrame frame{nullptr};
    for (int i = 0; i < 250 && !frame; ++i) { frame = pool.TryGetNextFrame(); if (!frame) Sleep(20); }
    if (!frame) throw std::runtime_error("Owned Graphics Capture did not deliver a frame");
    const auto size = frame.ContentSize();
    const auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    ComPtr<ID3D11Texture2D> source; winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&source)));
    D3D11_TEXTURE2D_DESC description{}; source->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ; description.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging; winrt::check_hresult(device->CreateTexture2D(&description, nullptr, &staging));
    context->CopyResource(staging.Get(), source.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{}; winrt::check_hresult(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    struct Unmap { ID3D11DeviceContext* context; ID3D11Texture2D* texture; ~Unmap() { context->Unmap(texture, 0); } } unmap{context.Get(), staging.Get()};
    RECT bounds{}; GetWindowRect(hwnd, &bounds);
    if (bounds.right - bounds.left != size.Width || bounds.bottom - bounds.top != size.Height) {
        winrt::check_hresult(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)));
        if (bounds.right - bounds.left != size.Width || bounds.bottom - bounds.top != size.Height)
            throw std::runtime_error("Owned capture dimensions do not match this window");
    }
    RECT client{}; GetClientRect(hwnd, &client); POINT origin{}; ClientToScreen(hwnd, &origin);
    const auto x = origin.x - bounds.left, y = origin.y - bounds.top;
    if (x < 0 || y < 0 || x + client.right > size.Width || y + client.bottom > size.Height)
        throw std::runtime_error("Owned client rectangle is outside captured window");
    Pixels pixels{client.right, client.bottom}; pixels.data.resize(static_cast<std::size_t>(pixels.width) * pixels.height);
    for (int row = 0; row < pixels.height; ++row)
        memcpy(pixels.data.data() + row * pixels.width, static_cast<const BYTE*>(mapped.pData) + (row + y) * mapped.RowPitch + x * 4, pixels.width * 4);
    frame.Close();
    return pixels;
}
}
