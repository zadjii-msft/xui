#include "native_swap_chain_host.hpp"
#include "composition_presenter.hpp"
#include <dcomp.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
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
    Rect visible_pixel_bounds{};
    std::function<void(const SwapChainPanelMetrics&)> changed;
    std::function<bool()> can_activate;
    ComPtr<IDCompositionDesktopDevice> device;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual2> visual;
    ComPtr<IUnknown> content;
    D2D_RECT_F clip{};
    bool clipped{};
    bool active{}, disposed{}, native_input{};
    // Producer content in both presentations: a composition swap chain or a duplicated surface handle.
    ComPtr<IDXGISwapChain1> chain;
    HANDLE handle{};
    // Composition windows place the producer in the window's composition tree instead of a
    // DirectComposition target on the peer HWND.
    std::weak_ptr<platform::CompositionPresenter> presenter;
    HWND root{};
    std::uint64_t panel_visual{};
    bool composed{};
    bool popup_layer{};
    POINT composed_origin{};
    SIZE composed_size{};
    RECT composed_clip{};

    ~State() { if (handle) CloseHandle(handle); }
    bool has_producer() const { return chain || handle; }
    void set_handle(HANDLE next) {
        if (handle && handle != next) CloseHandle(handle);
        handle = next;
    }
    void bind_composed() {
        if (auto owner = presenter.lock()) owner->panel_content(panel_visual, chain.Get(), handle);
    }

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
        if (composed) {
            if (!window || disposed) return;
            const bool show = shown && has_producer() && (!can_activate || can_activate());
            if (auto owner = presenter.lock())
                owner->place_panel(panel_visual, composed_origin, composed_size, composed_clip, show, popup_layer);
            active = show;
            return;
        }
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
    // Recreates DirectComposition content after a composition host released it.
    void restore_classic_content() {
        if (content || !has_producer()) return;
        if (chain) { replace(chain); return; }
        ensure_device();
        ComPtr<IUnknown> surface;
        require_composition(device->CreateSurfaceFromHandle(handle, &surface), "Open composition surface handle");
        replace(std::move(surface));
    }
    void publish(SwapChainPanelMetrics next, Rect visible_bounds) {
        if (metrics == next && visible_pixel_bounds.x == visible_bounds.x &&
            visible_pixel_bounds.y == visible_bounds.y && visible_pixel_bounds.width == visible_bounds.width &&
            visible_pixel_bounds.height == visible_bounds.height) return;
        metrics = next;
        visible_pixel_bounds = visible_bounds;
        const auto callback = changed;
        if (callback) callback(next);
    }
    void close() noexcept {
        // Releasing the target disconnects the visual tree, without a fallible Commit in teardown.
        if (composed)
            if (auto owner = presenter.lock()) owner->remove_panel(panel_visual);
        composed = false;
        popup_layer = false;
        panel_visual = 0;
        presenter.reset();
        root = nullptr;
        chain.Reset();
        set_handle(nullptr);
        disposed = true;
        active = false;
        window = nullptr;
        metrics = {};
        visible_pixel_bounds = {};
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
    auto& state = *state_;
    ComPtr<IDXGISwapChain1> next_chain;
    if (content) require_composition(content.As(&next_chain), "Query composition swap chain");
    if (state.composed) {
        const auto previous = std::exchange(state.chain, next_chain);
        const HANDLE previous_handle = std::exchange(state.handle, nullptr);
        try { state.bind_composed(); }
        catch (...) { state.chain = previous; state.handle = previous_handle; throw; }
        if (previous_handle) CloseHandle(previous_handle);
        state.apply(state.metrics.visible);
    } else {
        state.replace(content);
        state.chain = next_chain;
        state.set_handle(nullptr);
    }
    invalidate(Invalidation::paint);
}
void SwapChainPanel::set_swap_chain_handle(HANDLE surface) {
    auto& state = *state_;
    state.check_thread();
    // Keep a duplicate: composition windows import the handle when they attach the producer,
    // and the caller may close its handle after this call.
    HANDLE duplicate{};
    if (surface && !DuplicateHandle(GetCurrentProcess(), surface, GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Duplicate composition surface handle");
    try {
        if (state.composed) {
            const HANDLE previous = std::exchange(state.handle, duplicate);
            const auto previous_chain = std::exchange(state.chain, nullptr);
            try { state.bind_composed(); }
            catch (...) { state.handle = previous; state.chain = previous_chain; throw; }
            if (previous) CloseHandle(previous);
            state.apply(state.metrics.visible);
        } else {
            ComPtr<IUnknown> content;
            if (surface) {
                state.ensure_device();
                require_composition(state.device->CreateSurfaceFromHandle(surface, &content), "Open composition surface handle");
            }
            state.replace(std::move(content));
            state.chain.Reset();
            state.set_handle(duplicate);
        }
    } catch (...) {
        if (duplicate && duplicate != state.handle) CloseHandle(duplicate);
        throw;
    }
    invalidate(Invalidation::paint);
}
bool SwapChainPanel::has_content() const { return state_->has_producer(); }
const SwapChainPanelMetrics& SwapChainPanel::metrics() const { return state_->metrics; }
Rect SwapChainPanel::visible_pixel_bounds() const { return state_->visible_pixel_bounds; }
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
    std::function<bool()> can_activate, std::weak_ptr<platform::CompositionPresenter> presenter, HWND root)
    : model_(std::move(model)) {
    auto& state = *model_->state_;
    state.check_thread();
    if (state.window) throw std::logic_error("Swap chain panel already has a native host");
    state.window = window;
    state.disposed = false;
    state.can_activate = std::move(can_activate);
    try {
        if (auto target = presenter.lock()) {
            // The window's composition tree presents the producer; release any DirectComposition objects.
            state.active = false;
            state.target.Reset();
            state.visual.Reset();
            state.content.Reset();
            state.device.Reset();
            state.presenter = presenter;
            state.root = root;
            state.panel_visual = target->add_panel();
            state.composed = true;
            state.bind_composed();
        } else state.restore_classic_content();
    } catch (...) {
        // Keep the producer so that another host can attach it; release this host's binding.
        auto chain = std::move(state.chain);
        const HANDLE handle = std::exchange(state.handle, nullptr);
        state.close();
        state.chain = std::move(chain);
        state.handle = handle;
        throw;
    }
}
NativeSwapChainHost::~NativeSwapChainHost() { cancel_owner(); }
void NativeSwapChainHost::sync(bool visible, UINT dpi, Rect clip, bool popup_layer) {
    auto& state = *model_->state_;
    if (state.disposed) return;
    state.popup_layer = popup_layer;
    RECT bounds{};
    if (!GetClientRect(state.window, &bounds))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Read swap chain panel size");
    SwapChainPanelMetrics next{static_cast<std::uint32_t>(std::max(0L, bounds.right)),
        static_cast<std::uint32_t>(std::max(0L, bounds.bottom)), dpi / 96.0f,
        visible && bounds.right > 0 && bounds.bottom > 0 && (!state.can_activate || state.can_activate())};
    const auto scale = next.rasterization_scale;
    D2D_RECT_F pixels{};
    if (next.visible) {
        pixels.left = std::clamp(clip.x * scale, 0.0f, static_cast<float>(next.pixel_width));
        pixels.top = std::clamp(clip.y * scale, 0.0f, static_cast<float>(next.pixel_height));
        pixels.right = std::clamp((clip.x + clip.width) * scale, pixels.left, static_cast<float>(next.pixel_width));
        pixels.bottom = std::clamp((clip.y + clip.height) * scale, pixels.top, static_cast<float>(next.pixel_height));
        if (pixels.right <= pixels.left || pixels.bottom <= pixels.top) {
            next.visible = false;
            pixels = {};
        }
    }
    if (state.composed) {
        POINT origin{};
        MapWindowPoints(state.window, state.root, &origin, 1);
        state.composed_origin = origin;
        state.composed_size = {static_cast<LONG>(next.pixel_width), static_cast<LONG>(next.pixel_height)};
        state.composed_clip = {static_cast<LONG>(std::floor(pixels.left)), static_cast<LONG>(std::floor(pixels.top)),
            static_cast<LONG>(std::ceil(pixels.right)), static_cast<LONG>(std::ceil(pixels.bottom))};
        state.clip = pixels;
        state.clipped = true;
    } else if (!state.clipped || state.clip.left != pixels.left || state.clip.top != pixels.top ||
        state.clip.right != pixels.right || state.clip.bottom != pixels.bottom) {
        if (state.visual) {
            require_composition(state.visual->SetClip(pixels), "Clip swap chain visual");
            require_composition(state.device->Commit(), "Commit swap chain clip");
        }
        state.clip = pixels;
        state.clipped = true;
    }
    state.apply(next.visible);
    state.publish(next, {pixels.left, pixels.top, pixels.right - pixels.left, pixels.bottom - pixels.top});
}
void NativeSwapChainHost::suspend() {
    auto& state = *model_->state_;
    if (state.disposed) return;
    state.apply(false);
    auto next = state.metrics;
    next.visible = false;
    state.publish(next, {});
}
void NativeSwapChainHost::cancel_owner() noexcept { model_->state_->close(); }
bool NativeSwapChainHost::active() const { return model_->state_->active; }
}
