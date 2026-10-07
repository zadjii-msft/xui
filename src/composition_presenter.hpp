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
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
