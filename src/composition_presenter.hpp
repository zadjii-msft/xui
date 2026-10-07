#pragma once

#include <windows.h>
#include <dxgi1_2.h>
#include <cstdint>
#include <memory>
#include <span>

namespace xui::platform {

// One physical-pixel backdrop area: full element bounds and the part left visible by clips.
struct BackdropRegion {
    RECT bounds;
    RECT visible;
    float radius;
};

// Windows.UI.Composition tree for one opt-in composition window: system backdrop
// sprites below the window's premultiplied swap chain. UI thread only.
class CompositionPresenter final {
public:
    explicit CompositionPresenter(HWND window);
    ~CompositionPresenter();
    CompositionPresenter(const CompositionPresenter&) = delete;
    CompositionPresenter& operator=(const CompositionPresenter&) = delete;
    // Rebinds the swap chain when its generation changes, then mirrors the frame's backdrop holes.
    // tint is 0xAARRGGBB, drawn over the blurred backdrop inside each region.
    void update(IDXGISwapChain1* swap_chain, std::uint64_t generation, SIZE pixels,
        std::span<const BackdropRegion> regions, std::uint32_t tint);
    std::size_t backdrop_visuals() const;
    // Swap chain panels compose above window content and below the overlay layer; panels inside
    // popups compose above the overlay, which holds their popup faces.
    std::uint64_t add_panel();
    // A composition swap chain, a DCompositionCreateSurfaceHandle handle, or neither to detach.
    void panel_content(std::uint64_t id, IDXGISwapChain1* swap_chain, HANDLE surface);
    // Window-client physical pixels. The clip is panel-local; producer pixels keep their native size.
    void place_panel(std::uint64_t id, POINT origin, SIZE size, RECT clip, bool visible, bool above_overlay);
    void remove_panel(std::uint64_t id) noexcept;
    std::size_t panel_visuals() const;
    // Popups and tooltips over panels: a second premultiplied swap chain above them.
    void overlay(IDXGISwapChain1* swap_chain, std::uint64_t generation, bool visible);
    bool overlay_visible() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
