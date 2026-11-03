#pragma once

#include "control_host.hpp"

namespace xui {

class NavigationHostPeer final : public ControlHostPeer {
public:
    std::unique_ptr<RowImages> row_images;
    bool dragging{};
    float drag_y{}, drag_offset{};
    std::optional<Point> tab_pointer;
    std::optional<std::uint64_t> pressed_tab;
    POINT tab_press{};

    NavigationHostPeer(ControlHostServices&, std::shared_ptr<Control>);
    ~NavigationHostPeer() override;
    void attach(HWND, int) override;
    void detach_owner() override;
    void cancel_input() override;
    bool capture_active() const override { return dragging || pressed_tab.has_value(); }
    void paint(const HostPaintContext&) override;
    std::optional<LRESULT> input(UINT, WPARAM, LPARAM, const HostInputContext&) override;
    std::optional<LRESULT> accessibility_action(LPARAM) override;
    HRESULT foundation_action(const FoundationAction&) override;
    bool image_sync_required() const override;
    bool image_work(RowImageWork&) override;
    bool sync_images(RowImageWork, UINT, const std::shared_ptr<TaskWake>&,
        std::vector<std::uint64_t>&, std::size_t&) override;
    std::size_t metric(PeerMetric) const override;
};

NavigationHostPeer* navigation_peer(HostPeer&);

}
