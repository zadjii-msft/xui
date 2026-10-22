#pragma once

#include "control_host.hpp"
#include "native_runtime_host.hpp"
#include "native_swap_chain_host.hpp"

namespace xui {

class SceneHostPeer final : public ControlHostPeer {
    std::unique_ptr<NativeRuntimeHost> runtime_;
    std::unique_ptr<NativeSwapChainHost> swap_chain_;
    std::unique_ptr<ImagePeer> image_;
    bool dragging_{};
    float drag_x_{}, drag_y_{};
public:
    SceneHostPeer(ControlHostServices&, std::shared_ptr<Control>);
    ~SceneHostPeer() override;
    void attach(HWND, int) override;
    void detach_owner() override;
    void reconcile_input() override;
    void cancel_input() override;
    bool capture_active() const override { return dragging_; }
    void paint(const HostPaintContext&) override;
    std::optional<LRESULT> input(UINT, WPARAM, LPARAM, const HostInputContext&) override;
    NativeRuntimeHost* runtime_host() const override { return runtime_.get(); }
    NativeSwapChainHost* swap_chain_host() const override { return swap_chain_.get(); }
    ImagePeer* image_peer() const override { return image_.get(); }
    bool image_sync_required() const override { return bool(image_); }
};

}
