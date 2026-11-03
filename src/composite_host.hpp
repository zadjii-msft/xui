#pragma once

#include "control_host.hpp"

namespace xui {

class CompositeHostPeer final : public ControlHostPeer {
public:
    std::unique_ptr<Drawing> overlay_drawing;
    HWND scroll_content{};
    RECT scroll_placed_bounds{};
    bool scroll_placed{}, dragging{};
    float drag_y{}, drag_offset{};

    CompositeHostPeer(ControlHostServices&, std::shared_ptr<Control>);
    void attach(HWND, int) override;
    void prepare_overlay(VisualStyle);
    void reconcile_input() override;
    void cancel_input() override;
    bool capture_active() const override { return dragging; }
    void paint(const HostPaintContext&) override;
    std::optional<LRESULT> input(UINT, WPARAM, LPARAM, const HostInputContext&) override;
    std::optional<LRESULT> accessibility_action(LPARAM) override;
    HRESULT foundation_action(const FoundationAction&) override;
    Drawing* overlay_surface() const override { return overlay_drawing.get(); }
    HWND child_content_window() const override { return scroll_content; }
};

CompositeHostPeer* composite_peer(HostPeer&);

}
