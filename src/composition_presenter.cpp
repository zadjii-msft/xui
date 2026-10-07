#include "composition_presenter.hpp"
#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <algorithm>
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
    WUC::ContainerVisual panels{nullptr};
    WUC::SpriteVisual overlay{nullptr};
    WUC::ContainerVisual popup_panels{nullptr};
    WUC::CompositionBrush backdrop{nullptr};
    WUC::CompositionColorBrush tint{nullptr};
    std::vector<WUC::SpriteVisual> sprites;
    std::size_t visible{};
    std::uint64_t generation{};
    std::uint64_t overlay_generation{};
    std::uint32_t tint_value{};
    struct Panel {
        std::uint64_t id{};
        WUC::SpriteVisual sprite{nullptr};
        bool content{};
        bool shown{};
        bool above_overlay{};
    };
    WUC::ContainerVisual& layer(const Panel& item) { return item.above_overlay ? popup_panels : panels; }
    std::vector<Panel> panel_list;
    std::uint64_t next_panel{1};
    Panel* panel(std::uint64_t id) {
        for (auto& item : panel_list) if (item.id == id) return &item;
        return nullptr;
    }
    WUC::CompositionSurfaceBrush surface_brush(const WUC::ICompositionSurface& surface) const {
        auto brush = compositor.CreateSurfaceBrush(surface);
        // Producer and overlay pixels keep their native size at the top-left corner.
        brush.Stretch(WUC::CompositionStretch::None);
        brush.HorizontalAlignmentRatio(0);
        brush.VerticalAlignmentRatio(0);
        return brush;
    }
    WUC::ICompositionSurface swap_chain_surface(IDXGISwapChain1* swap_chain) const {
        auto interop = compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>();
        WUC::ICompositionSurface surface{nullptr};
        winrt::check_hresult(interop->CreateCompositionSurfaceForSwapChain(swap_chain,
            reinterpret_cast<ABI::Windows::UI::Composition::ICompositionSurface**>(winrt::put_abi(surface))));
        return surface;
    }
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
        s.panels = s.compositor.CreateContainerVisual();
        s.panels.RelativeSizeAdjustment({1, 1});
        s.root.Children().InsertAtTop(s.panels);
        s.overlay = s.compositor.CreateSpriteVisual();
        s.overlay.RelativeSizeAdjustment({1, 1});
        s.overlay.IsVisible(false);
        s.root.Children().InsertAtTop(s.overlay);
        s.popup_panels = s.compositor.CreateContainerVisual();
        s.popup_panels.RelativeSizeAdjustment({1, 1});
        s.root.Children().InsertAtTop(s.popup_panels);
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
            s.content.Brush(s.surface_brush(s.swap_chain_surface(swap_chain)));
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

std::uint64_t CompositionPresenter::add_panel() {
    try {
        auto& s = *impl_;
        Impl::Panel item{s.next_panel++, s.compositor.CreateSpriteVisual()};
        item.sprite.IsVisible(false);
        s.panels.Children().InsertAtTop(item.sprite);
        s.panel_list.push_back(std::move(item));
        return s.panel_list.back().id;
    } catch (const winrt::hresult_error& error) {
        rethrow(error, "Create swap chain panel visual");
    }
}

void CompositionPresenter::panel_content(std::uint64_t id, IDXGISwapChain1* swap_chain, HANDLE surface) {
    try {
        auto& s = *impl_;
        auto* item = s.panel(id);
        if (!item) throw std::logic_error("Unknown swap chain panel visual");
        if (swap_chain) item->sprite.Brush(s.surface_brush(s.swap_chain_surface(swap_chain)));
        else if (surface) {
            auto interop = s.compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>();
            WUC::ICompositionSurface imported{nullptr};
            winrt::check_hresult(interop->CreateCompositionSurfaceForHandle(surface,
                reinterpret_cast<ABI::Windows::UI::Composition::ICompositionSurface**>(winrt::put_abi(imported))));
            item->sprite.Brush(s.surface_brush(imported));
        } else item->sprite.Brush(nullptr);
        item->content = swap_chain || surface;
        item->sprite.IsVisible(item->content && item->shown);
    } catch (const winrt::hresult_error& error) {
        rethrow(error, "Attach swap chain panel content");
    }
}

void CompositionPresenter::place_panel(std::uint64_t id, POINT origin, SIZE size, RECT clip, bool visible,
    bool above_overlay) {
    try {
        auto& s = *impl_;
        auto* item = s.panel(id);
        if (!item) throw std::logic_error("Unknown swap chain panel visual");
        if (item->above_overlay != above_overlay) {
            s.layer(*item).Children().Remove(item->sprite);
            item->above_overlay = above_overlay;
            s.layer(*item).Children().InsertAtTop(item->sprite);
        }
        item->sprite.Offset({static_cast<float>(origin.x), static_cast<float>(origin.y), 0});
        item->sprite.Size({static_cast<float>(size.cx), static_cast<float>(size.cy)});
        item->sprite.Clip(s.compositor.CreateInsetClip(static_cast<float>(clip.left), static_cast<float>(clip.top),
            static_cast<float>(size.cx - clip.right), static_cast<float>(size.cy - clip.bottom)));
        item->shown = visible;
        item->sprite.IsVisible(item->content && visible);
    } catch (const winrt::hresult_error& error) {
        rethrow(error, "Place swap chain panel visual");
    }
}

void CompositionPresenter::remove_panel(std::uint64_t id) noexcept {
    try {
        auto& s = *impl_;
        const auto found = std::find_if(s.panel_list.begin(), s.panel_list.end(), [&](const auto& item) { return item.id == id; });
        if (found == s.panel_list.end()) return;
        found->sprite.Brush(nullptr);
        s.layer(*found).Children().Remove(found->sprite);
        s.panel_list.erase(found);
    } catch (...) {
        OutputDebugStringW(L"XUI: Cannot remove a swap chain panel visual.\n");
    }
}

std::size_t CompositionPresenter::panel_visuals() const {
    return static_cast<std::size_t>(std::count_if(impl_->panel_list.begin(), impl_->panel_list.end(),
        [](const auto& item) { return item.content && item.shown; }));
}

void CompositionPresenter::overlay(IDXGISwapChain1* swap_chain, std::uint64_t generation, bool visible) {
    try {
        auto& s = *impl_;
        if (visible && swap_chain && generation != s.overlay_generation) {
            s.overlay.Brush(s.surface_brush(s.swap_chain_surface(swap_chain)));
            s.overlay_generation = generation;
        }
        s.overlay.IsVisible(visible && swap_chain);
    } catch (const winrt::hresult_error& error) {
        rethrow(error, "Update composition overlay");
    }
}

bool CompositionPresenter::overlay_visible() const { return impl_->overlay.IsVisible(); }

}
