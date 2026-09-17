#include "native_swap_chain_host.hpp"
#include <dcomp.h>
#include <wrl/client.h>
#include <algorithm>
#include <system_error>
#include <utility>

namespace xui {
using Microsoft::WRL::ComPtr;
namespace {
void require_composition(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::system_error(static_cast<int>(result), std::system_category(), operation);
}
}

struct SwapChainPanel::State {
    DWORD thread{GetCurrentThreadId()};
    HWND window{};
    SwapChainPanelMetrics metrics;
    std::function<void(const SwapChainPanelMetrics&)> changed;
    std::function<bool()> can_activate;
    ComPtr<IDCompositionDesktopDevice> device;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual2> visual;
    ComPtr<IUnknown> content;
    D2D_RECT_F clip{};
    bool clipped{};
    bool active{}, disposed{}, native_input{};

    void check_thread() const {
        if (thread != GetCurrentThreadId())
            throw std::logic_error("Swap chain panel operations require the creating UI thread");
    }
    void ensure_device() {
        if (disposed) throw std::logic_error("The swap chain panel host is closed");
        if (device) return;
        ComPtr<IDCompositionDesktopDevice> next_device;
        ComPtr<IDCompositionVisual2> next_visual;
        require_composition(DCompositionCreateDevice2(nullptr, IID_PPV_ARGS(&next_device)), "Create composition device");
        require_composition(next_device->CreateVisual(&next_visual), "Create swap chain visual");
        if (clipped) require_composition(next_visual->SetClip(clip), "Clip swap chain visual");
        device = std::move(next_device);
        visual = std::move(next_visual);
    }
    void apply(bool shown) {
        if (!device || !window || disposed) return;
        const bool show = shown && content && (!can_activate || can_activate());
        if (show == active) return;
        if (!target) {
            require_composition(device->CreateTargetForHwnd(window, TRUE, &target), "Create swap chain target");
        }
        require_composition(target->SetRoot(show ? visual.Get() : nullptr), "Set swap chain target root");
        require_composition(device->Commit(), "Commit swap chain composition");
        active = show;
    }
    void replace(ComPtr<IUnknown> next) {
        if (!next) {
            // Detachment also works after device loss and permits a new device on reattachment.
            active = false;
            target.Reset();
            visual.Reset();
            content.Reset();
            device.Reset();
            return;
        }
        ensure_device();
        // Commit the new content before releasing the previous producer reference.
        require_composition(visual->SetContent(next.Get()), "Attach swap chain content");
        require_composition(device->Commit(), "Commit swap chain content");
        content = std::move(next);
        apply(metrics.visible);
    }
    void publish(SwapChainPanelMetrics next) {
        if (metrics == next) return;
        metrics = next;
        const auto callback = changed;
        if (callback) callback(next);
    }
    void close() noexcept {
        // Releasing the target disconnects the visual tree, without a fallible Commit in teardown.
        disposed = true;
        active = false;
        window = nullptr;
        metrics = {};
        can_activate = {};
        target.Reset();
        visual.Reset();
        content.Reset();
        device.Reset();
    }
};

SwapChainPanel::SwapChainPanel(std::wstring name)
    : Control(ControlRole::swap_chain_panel, std::move(name), {640, 480}), state_(std::make_unique<State>()) {
    set_tab_stop(false);
}
SwapChainPanel::~SwapChainPanel() = default;
void SwapChainPanel::set_swap_chain(IDXGISwapChain* swap_chain) {
    state_->check_thread();
    ComPtr<IUnknown> content;
    if (swap_chain) {
        ComPtr<IDXGISwapChain1> chain;
        require_composition(swap_chain->QueryInterface(IID_PPV_ARGS(&chain)), "Query composition swap chain");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        require_composition(chain->GetDesc1(&desc), "Read composition swap chain");
        HWND window{};
        if (SUCCEEDED(chain->GetHwnd(&window)) && window)
            throw std::invalid_argument("An HWND swap chain cannot be attached to a swap chain panel");
        if (desc.SwapEffect != DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || desc.Scaling != DXGI_SCALING_STRETCH ||
            desc.SampleDesc.Count != 1)
            throw std::invalid_argument("Use a flip-sequential composition swap chain with stretch scaling and no multisampling");
        content = chain;
    }
    state_->replace(std::move(content));
    invalidate(Invalidation::paint);
}
void SwapChainPanel::set_swap_chain_handle(HANDLE surface) {
    state_->check_thread();
    ComPtr<IUnknown> content;
    if (surface) {
        state_->ensure_device();
        require_composition(state_->device->CreateSurfaceFromHandle(surface, &content), "Open composition surface handle");
    }
    state_->replace(std::move(content));
    invalidate(Invalidation::paint);
}
bool SwapChainPanel::has_content() const { return state_->content != nullptr; }
const SwapChainPanelMetrics& SwapChainPanel::metrics() const { return state_->metrics; }
HWND SwapChainPanel::native_window() const { return state_->window; }
void SwapChainPanel::on_metrics_changed(std::function<void(const SwapChainPanelMetrics&)> callback) {
    state_->check_thread();
    state_->changed = std::move(callback);
}
void SwapChainPanel::set_native_input(bool enabled) {
    state_->check_thread();
    state_->native_input = enabled;
    set_tab_stop(enabled);
}
bool SwapChainPanel::native_input() const { return state_->native_input; }

NativeSwapChainHost::NativeSwapChainHost(std::shared_ptr<SwapChainPanel> model, HWND window,
    std::function<bool()> can_activate) : model_(std::move(model)) {
    auto& state = *model_->state_;
    state.check_thread();
    if (state.window) throw std::logic_error("Swap chain panel already has a native host");
    state.window = window;
    state.disposed = false;
    state.can_activate = std::move(can_activate);
}
NativeSwapChainHost::~NativeSwapChainHost() { cancel_owner(); }
void NativeSwapChainHost::sync(bool visible, UINT dpi, Rect clip) {
    auto& state = *model_->state_;
    if (state.disposed) return;
    RECT bounds{};
    if (!GetClientRect(state.window, &bounds))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Read swap chain panel size");
    SwapChainPanelMetrics next{static_cast<std::uint32_t>(std::max(0L, bounds.right)),
        static_cast<std::uint32_t>(std::max(0L, bounds.bottom)), dpi / 96.0f,
        visible && bounds.right > 0 && bounds.bottom > 0 && (!state.can_activate || state.can_activate())};
    const auto scale = next.rasterization_scale;
    const D2D_RECT_F pixels{clip.x * scale, clip.y * scale,
        (clip.x + clip.width) * scale, (clip.y + clip.height) * scale};
    if (!state.clipped || state.clip.left != pixels.left || state.clip.top != pixels.top ||
        state.clip.right != pixels.right || state.clip.bottom != pixels.bottom) {
        if (state.visual) {
            require_composition(state.visual->SetClip(pixels), "Clip swap chain visual");
            require_composition(state.device->Commit(), "Commit swap chain clip");
        }
        state.clip = pixels;
        state.clipped = true;
    }
    state.apply(next.visible);
    state.publish(next);
}
void NativeSwapChainHost::suspend() {
    auto& state = *model_->state_;
    if (state.disposed) return;
    state.apply(false);
    auto next = state.metrics;
    next.visible = false;
    state.publish(next);
}
void NativeSwapChainHost::cancel_owner() noexcept { model_->state_->close(); }
bool NativeSwapChainHost::active() const { return model_->state_->active; }
}
