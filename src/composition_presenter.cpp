#include "composition_presenter.hpp"
#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace xui::platform {
namespace {
namespace WUC = winrt::Windows::UI::Composition;
namespace WUCD = winrt::Windows::UI::Composition::Desktop;
using CreateController = HRESULT(WINAPI*)(DispatcherQueueOptions, PDISPATCHERQUEUECONTROLLER*);

[[noreturn]] void rethrow(const winrt::hresult_error& error, const char* operation) {
    throw std::system_error(static_cast<int>(error.code()), std::system_category(), operation);
}

// Windows.UI.Composition requires a DispatcherQueue on the UI thread. XUI's message loop
// services it. CoreMessaging loads only when the first composition window is created.
void ensure_dispatcher_queue() {
    if (winrt::Windows::System::DispatcherQueue::GetForCurrentThread()) return;
    thread_local winrt::com_ptr<ABI::Windows::System::IDispatcherQueueController> controller;
    const HMODULE module = LoadLibraryExW(L"CoreMessaging.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Load CoreMessaging");
    const auto create = reinterpret_cast<CreateController>(GetProcAddress(module, "CreateDispatcherQueueController"));
    if (!create) throw std::runtime_error("CreateDispatcherQueueController is unavailable");
    const DispatcherQueueOptions options{sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
    const HRESULT result = create(options, controller.put());
    if (FAILED(result)) throw std::system_error(static_cast<int>(result), std::system_category(), "Create composition dispatcher");
}

WUC::SpriteVisual backdrop_sprite(const WUC::Compositor& compositor, const WUC::CompositionBrush& backdrop,
    const WUC::CompositionBrush& tint_brush) {
    auto sprite = compositor.CreateSpriteVisual();
    sprite.Brush(backdrop);
    auto tint = compositor.CreateSpriteVisual();
    tint.RelativeSizeAdjustment({1, 1});
    tint.Brush(tint_brush);
    sprite.Children().InsertAtTop(tint);
    return sprite;
}
}

struct CompositionPresenter::Impl {
    WUC::Compositor compositor{nullptr};
    WUCD::DesktopWindowTarget target{nullptr};
    WUC::ContainerVisual root{nullptr};
    WUC::ContainerVisual backdrops{nullptr};
    WUC::SpriteVisual content{nullptr};
    WUC::CompositionBrush backdrop{nullptr};
    WUC::CompositionColorBrush tint{nullptr};
    std::vector<WUC::SpriteVisual> sprites;
    std::size_t visible{};
    std::uint64_t generation{};
    std::uint32_t tint_value{};
};

CompositionPresenter::CompositionPresenter(HWND window) : impl_(std::make_unique<Impl>()) {
    try {
        ensure_dispatcher_queue();
        auto& s = *impl_;
        s.compositor = WUC::Compositor();
        auto interop = s.compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
        // The tree sits behind the swap chain content; child HWNDs still draw above it.
        winrt::check_hresult(interop->CreateDesktopWindowTarget(window, FALSE,
            reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(winrt::put_abi(s.target))));
        s.root = s.compositor.CreateContainerVisual();
        s.root.RelativeSizeAdjustment({1, 1});
        s.target.Root(s.root);
        s.backdrops = s.compositor.CreateContainerVisual();
        s.backdrops.RelativeSizeAdjustment({1, 1});
        s.root.Children().InsertAtBottom(s.backdrops);
        s.content = s.compositor.CreateSpriteVisual();
        // Relative sizing keeps resizes free of composition commits, which land a frame late.
        s.content.RelativeSizeAdjustment({1, 1});
        s.root.Children().InsertAtTop(s.content);
        s.backdrop = s.compositor.CreateHostBackdropBrush();
        s.tint = s.compositor.CreateColorBrush();
    } catch (const winrt::hresult_error& error) {
        rethrow(error, "Create composition presentation");
    }
}

CompositionPresenter::~CompositionPresenter() {
    try {
        if (impl_ && impl_->target) impl_->target.Root(nullptr);
    } catch (...) {
        OutputDebugStringW(L"XUI: Cannot detach the composition tree.\n");
    }
}

std::size_t CompositionPresenter::backdrop_visuals() const { return impl_->visible; }

void CompositionPresenter::update(IDXGISwapChain1* swap_chain, std::uint64_t generation, SIZE pixels,
    std::span<const BackdropRegion> regions, std::uint32_t tint) {
    try {
        auto& s = *impl_;
        if (generation != s.generation) {
            auto interop = s.compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>();
            WUC::ICompositionSurface surface{nullptr};
            winrt::check_hresult(interop->CreateCompositionSurfaceForSwapChain(swap_chain,
                reinterpret_cast<ABI::Windows::UI::Composition::ICompositionSurface**>(winrt::put_abi(surface))));
            auto brush = s.compositor.CreateSurfaceBrush(surface);
            brush.Stretch(WUC::CompositionStretch::None);
            brush.HorizontalAlignmentRatio(0);
            brush.VerticalAlignmentRatio(0);
            s.content.Brush(brush);
            s.generation = generation;
        }
        static_cast<void>(pixels);
        if (tint != s.tint_value || !s.visible) {
            s.tint.Color({static_cast<std::uint8_t>(tint >> 24), static_cast<std::uint8_t>(tint >> 16),
                static_cast<std::uint8_t>(tint >> 8), static_cast<std::uint8_t>(tint)});
            s.tint_value = tint;
        }
        while (s.sprites.size() < regions.size()) {
            auto sprite = backdrop_sprite(s.compositor, s.backdrop, s.tint);
            s.backdrops.Children().InsertAtTop(sprite);
            s.sprites.push_back(sprite);
        }
        for (std::size_t i = 0; i < s.sprites.size(); ++i) {
            auto& sprite = s.sprites[i];
            const bool shown = i < regions.size();
            sprite.IsVisible(shown);
            if (!shown) continue;
            const auto& region = regions[i];
            const auto& b = region.bounds;
            const auto& v = region.visible;
            sprite.Offset({static_cast<float>(b.left), static_cast<float>(b.top), 0});
            sprite.Size({static_cast<float>(b.right - b.left), static_cast<float>(b.bottom - b.top)});
            // Rounded corners stay only where the visible area still contains them.
            const float left = static_cast<float>(v.left - b.left), top = static_cast<float>(v.top - b.top);
            const float right = static_cast<float>(v.right - b.left), bottom = static_cast<float>(v.bottom - b.top);
            const auto corner = [&](bool kept) {
                return winrt::Windows::Foundation::Numerics::float2{kept ? region.radius : 0, kept ? region.radius : 0};
            };
            const bool at_left = v.left == b.left, at_top = v.top == b.top;
            const bool at_right = v.right == b.right, at_bottom = v.bottom == b.bottom;
            sprite.Clip(s.compositor.CreateRectangleClip(left, top, right, bottom,
                corner(at_left && at_top), corner(at_right && at_top),
                corner(at_right && at_bottom), corner(at_left && at_bottom)));
        }
        s.visible = regions.size();
    } catch (const winrt::hresult_error& error) {
        rethrow(error, "Update composition presentation");
    }
}

}
