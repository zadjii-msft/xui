#pragma once
#include "host_peer.hpp"
#include "grid_accessibility.hpp"

namespace xui {

class GridHostPeer final : public HostPeer {
public:
    GridHostPeer(PeerHostServices&, std::shared_ptr<DataGrid>);
    ~GridHostPeer() override;
    void attach(HWND, int) override;
    void sync(UINT, const Palette&) override;
    void publish_accessibility() override;
    void disconnect_accessibility() override;
    void detach_owner() override;
    void cancel_input() override;
    bool capture_active() const override { return grid_drag != 0; }
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
    DataGrid& grid_;
    std::shared_ptr<GridAccessibility> accessibility_{std::make_shared<GridAccessibility>()};
    std::unique_ptr<RowImages> row_images_;
    RowWheelMotion wheel_;
    int grid_drag{};
    bool grid_single_click{};
    std::weak_ptr<const GridSource> grid_press_source;
    std::optional<RowKey> grid_press_key;
    Microsoft::WRL::ComPtr<IDropTarget> file_target;
    std::size_t grid_drop{}, grid_column{};
    float drag_y{}, drag_offset{};
    HRESULT accessibility_action(WPARAM, LPARAM);
    void wheel_scroll(double delta, bool precise);
};

}
