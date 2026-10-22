#pragma once
#include "host_peer.hpp"
#include "collection_accessibility.hpp"

namespace xui {

class CollectionHostPeer final : public HostPeer {
public:
    CollectionHostPeer(PeerHostServices&, std::shared_ptr<VirtualCollection>);
    ~CollectionHostPeer() override;
    void attach(HWND, int) override;
    void sync(UINT, const Palette&) override;
    void publish_accessibility() override;
    void disconnect_accessibility() override;
    void detach_owner() override;
    void cancel_input() override;
    bool capture_active() const override;
    void paint(const HostPaintContext&) override;
    std::optional<LRESULT> input(UINT, WPARAM, LPARAM, const HostInputContext&) override;
    void cancel_wheel() override { wheel_.active = false; }
    bool advance_wheel(Animation::Clock::time_point, bool, bool) override;
    bool image_sync_required() const override { return true; }
    bool image_work(RowImageWork&) override;
    bool sync_images(RowImageWork, UINT, const std::shared_ptr<TaskWake>&,
        std::vector<std::uint64_t>&, std::size_t&) override;
    std::size_t metric(PeerMetric) const override;
private:
    VirtualCollection& collection_;
    std::shared_ptr<CollectionAccessibility> accessibility_{std::make_shared<CollectionAccessibility>()};
    std::unique_ptr<RowImages> row_images_;
    RowWheelMotion wheel_;
    bool collection_drag{}, collection_scroll{}, collection_additive{};
    float drag_y{}, drag_offset{};
    std::optional<ItemKey> collection_anchor;
    std::optional<Point> command_pointer;
    CollectionSelection collection_before;
    HRESULT accessibility_action(WPARAM, LPARAM);
    void wheel_scroll(double delta, bool precise);
};

}
