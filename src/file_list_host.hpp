#pragma once
#include "host_peer.hpp"
#include "list_peer.hpp"

namespace xui {

class FileListHostPeer final : public HostPeer {
public:
    FileListHostPeer(PeerHostServices&, std::shared_ptr<FileList>);
    ~FileListHostPeer() override;
    void attach(HWND, int) override;
    void sync(UINT, const Palette&) override;
    void set_theme(UINT, const Palette&) override;
    void publish_accessibility() override;
    void disconnect_accessibility() override {}
    void detach_owner() override;
    void cancel_input() override { control->cancel(); }
    bool capture_active() const override { return true; }
    void paint(const HostPaintContext&) override;
    std::optional<LRESULT> input(UINT, WPARAM, LPARAM, const HostInputContext&) override { return {}; }
    bool image_sync_required() const override;
    bool image_work(RowImageWork&) override;
    bool sync_images(RowImageWork, UINT, const std::shared_ptr<TaskWake>&,
        std::vector<std::uint64_t>&, std::size_t&) override;
    std::size_t metric(PeerMetric) const override;
    void native_moved() override;
private:
    ListPeer list_;
};

}
