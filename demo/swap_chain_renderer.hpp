#pragma once

#include "xui/swap_chain_panel.hpp"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace swap_chain_sample {
inline void check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        char code[16]{};
        std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
        throw std::runtime_error(std::string(operation) + " failed (HRESULT " + code + ")");
    }
}

class SurfaceHandle final {
public:
    SurfaceHandle() = default;
    ~SurfaceHandle() { reset(); }
    SurfaceHandle(const SurfaceHandle&) = delete;
    SurfaceHandle& operator=(const SurfaceHandle&) = delete;
    HANDLE get() const { return value_; }
    HANDLE* put() { reset(); return &value_; }
    void reset() { if (value_) CloseHandle(std::exchange(value_, nullptr)); }
private:
    HANDLE value_{};
};

// A producer, not a panel implementation. All methods run on the UI thread.
class Renderer final {
public:
    static constexpr std::uint32_t blue = 0x1452c8, gold = 0xf4b12a;
    static constexpr std::uint32_t pink = 0xb82163, cyan = 0x25dbc5;

    explicit Renderer(bool surface_handle = false, bool force_warp = false) {
        auto result = force_warp ? E_FAIL : create_device(D3D_DRIVER_TYPE_HARDWARE);
        if (FAILED(result)) {
            device_.Reset(); context_.Reset();
            check(create_device(D3D_DRIVER_TYPE_WARP), "Create WARP D3D11 device");
            warp_ = true;
        }
        check(context_.As(&context1_), "Query D3D11.1 context");
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        check(device_.As(&dxgi), "Query DXGI device");
        check(dxgi->GetAdapter(&adapter), "Get DXGI adapter");
        check(adapter->GetParent(IID_PPV_ARGS(&factory_)), "Get DXGI factory");
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = description.Height = 64;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (surface_handle) {
            check(DCompositionCreateSurfaceHandle(COMPOSITIONOBJECT_ALL_ACCESS, nullptr, surface_.put()),
                "Create composition surface handle");
            Microsoft::WRL::ComPtr<IDXGIFactoryMedia> media;
            check(factory_.As(&media), "Query composition surface factory");
            check(media->CreateSwapChainForCompositionSurfaceHandle(device_.Get(), surface_.get(),
                &description, nullptr, &chain_), "Create surface-handle swap chain");
        } else {
            check(factory_->CreateSwapChainForComposition(device_.Get(), &description, nullptr, &chain_),
                "Create composition swap chain");
        }
    }
    IDXGISwapChain1* chain() const { return chain_.Get(); }
    IDXGIFactory2* factory() const { return factory_.Get(); }
    ID3D11Device* device() const { return device_.Get(); }
    HANDLE surface_handle() const { return surface_.get(); }
    void close_surface_handle() { surface_.reset(); }
    bool warp() const { return warp_; }
    unsigned presents() const { return presents_; }
    unsigned resizes() const { return resizes_; }

    void render(const xui::SwapChainPanelMetrics& metrics, bool alternate = false) {
        if (!prepare(metrics)) return;
        const auto background = color(alternate ? pink : blue);
        const auto accent = color(alternate ? cyan : gold);
        context_->ClearRenderTargetView(target_.Get(), background.data());
        const D3D11_RECT rectangle{
            static_cast<LONG>(metrics.pixel_width / 4), static_cast<LONG>(metrics.pixel_height / 4),
            static_cast<LONG>(metrics.pixel_width * 3 / 4), static_cast<LONG>(metrics.pixel_height * 3 / 4)};
        context1_->ClearView(target_.Get(), accent.data(), &rectangle, 1);
        present();
    }
    void render_triangle(const xui::SwapChainPanelMetrics& metrics, float angle) {
        if (!std::isfinite(angle)) throw std::invalid_argument("Triangle angle must be finite");
        if (!prepare(metrics)) return;
        if (!vertex_shader_) create_triangle();
        const float extent = static_cast<float>(std::min(metrics.pixel_width, metrics.pixel_height));
        const std::array<float, 4> transform{
            std::cos(angle), std::sin(angle), extent / metrics.pixel_width, extent / metrics.pixel_height};
        context_->UpdateSubresource(transform_.Get(), 0, nullptr, transform.data(), 0, 0);
        const auto constants = transform_.Get();
        const auto target = target_.Get();
        const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(metrics.pixel_width),
            static_cast<float>(metrics.pixel_height), 0, 1};
        constexpr float background[]{0.025f, 0.03f, 0.045f, 1};
        context_->ClearRenderTargetView(target, background);
        context_->OMSetRenderTargets(1, &target, nullptr);
        context_->RSSetViewports(1, &viewport);
        context_->RSSetState(rasterizer_.Get());
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
        context_->VSSetConstantBuffers(0, 1, &constants);
        context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
        context_->Draw(3, 0);
        present();
    }
private:
    bool prepare(const xui::SwapChainPanelMetrics& metrics) {
        if (!metrics.visible || !metrics.pixel_width || !metrics.pixel_height) return false;
        DXGI_SWAP_CHAIN_DESC1 description{};
        check(chain_->GetDesc1(&description), "Read producer buffer dimensions");
        if (description.Width != metrics.pixel_width || description.Height != metrics.pixel_height) {
            context_->OMSetRenderTargets(0, nullptr, nullptr);
            target_.Reset();
            check(chain_->ResizeBuffers(0, metrics.pixel_width, metrics.pixel_height, DXGI_FORMAT_UNKNOWN, 0),
                "Resize producer buffers");
            ++resizes_;
        }
        if (!target_) {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
            check(chain_->GetBuffer(0, IID_PPV_ARGS(&buffer)), "Get producer back buffer");
            check(device_->CreateRenderTargetView(buffer.Get(), nullptr, &target_), "Create producer render target");
        }
        return true;
    }
    void present() {
        check(chain_->Present(1, 0), "Present producer frame");
        ++presents_;
    }
    void create_triangle() {
        constexpr char shader[] = R"(
cbuffer Transform : register(b0) { float cosine; float sine; float2 aspect; };
struct Vertex { float4 position : SV_Position; float3 color : COLOR; };
Vertex vertex_main(uint id : SV_VertexID) {
    static const float2 positions[] = {
        float2(0, 0.75), float2(0.649519, -0.375), float2(-0.649519, -0.375)
    };
    static const float3 colors[] = { float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1) };
    float2 p = positions[id];
    float3 rotated = float3(cosine * p.x, p.y, -sine * p.x);
    // Perspective camera at distance 3, with near/far planes at 1 and 5.
    float depth = 3 + rotated.z;
    Vertex vertex;
    vertex.position = float4(rotated.xy * aspect * 3, (depth - 1) * 1.25, depth);
    vertex.color = colors[id];
    return vertex;
}
float4 pixel_main(Vertex vertex) : SV_Target { return float4(vertex.color, 1); }
)";
        const auto compile = [&](const char* entry, const char* profile) {
            Microsoft::WRL::ComPtr<ID3DBlob> bytecode, errors;
            const auto result = D3DCompile(shader, sizeof(shader) - 1, "swap_chain_triangle", nullptr, nullptr,
                entry, profile, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors);
            if (FAILED(result) && errors)
                throw std::runtime_error(std::string("Compile triangle shader: ") +
                    std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
            check(result, "Compile triangle shader");
            return bytecode;
        };
        const auto vertex = compile("vertex_main", "vs_5_0");
        const auto pixel = compile("pixel_main", "ps_5_0");
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
        Microsoft::WRL::ComPtr<ID3D11Buffer> transform;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer;
        check(device_->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &vertex_shader),
            "Create triangle vertex shader");
        check(device_->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &pixel_shader),
            "Create triangle pixel shader");
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = 4 * sizeof(float);
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(device_->CreateBuffer(&buffer, nullptr, &transform), "Create triangle transform buffer");
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        check(device_->CreateRasterizerState(&raster, &rasterizer), "Create triangle rasterizer");
        pixel_shader_ = std::move(pixel_shader);
        transform_ = std::move(transform);
        rasterizer_ = std::move(rasterizer);
        vertex_shader_ = std::move(vertex_shader);
    }
    HRESULT create_device(D3D_DRIVER_TYPE driver) {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        return D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            &level, 1, D3D11_SDK_VERSION, &device_, nullptr, &context_);
    }
    static std::array<float, 4> color(std::uint32_t rgb) {
        return {float((rgb >> 16) & 255) / 255, float((rgb >> 8) & 255) / 255, float(rgb & 255) / 255, 1};
    }
    SurfaceHandle surface_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> chain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> transform_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
    bool warp_{};
    unsigned presents_{}, resizes_{};
};
}
