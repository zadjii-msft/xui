#include "file_list_host.hpp"

namespace xui {

FileListHostPeer::FileListHostPeer(PeerHostServices& owner, std::shared_ptr<FileList> value)
    : HostPeer(owner, HostFamily::file_list, value),
      list_(std::move(value), [&owner] { owner.peer_failure(); }, [this](HWND) {
          host.reveal_peer(*this);
      }) {}

FileListHostPeer::~FileListHostPeer() {
    host.forget_peer(*this);
    detach_owner();
    // ListPeer owns its HWND, including provider disconnection during destruction.
    window = nullptr;
}

void FileListHostPeer::attach(HWND parent_window, int id) {
    list_.attach(parent_window, id);
    window = list_.window();
}
void FileListHostPeer::sync(UINT dpi, const Palette& palette) { list_.update(dpi, palette); }
void FileListHostPeer::set_theme(UINT dpi, const Palette& palette) { list_.set_theme(dpi, palette); }
void FileListHostPeer::publish_accessibility() { list_.publish(); }
void FileListHostPeer::native_moved() { list_.publish(); }
void FileListHostPeer::detach_owner() { list_.detach_thumbnails(); }
void FileListHostPeer::paint(const HostPaintContext& context) { list_.paint(context.canvas); }
bool FileListHostPeer::image_work(RowImageWork& work, bool) {
    if (!static_cast<const FileList&>(*control).thumbnails() && !list_.thumbnail_count()) return false;
    work.clip.y -= control->bounds().y;
    return true;
}
bool FileListHostPeer::sync_images(RowImageWork work, UINT, const std::shared_ptr<TaskWake>& wake,
    std::vector<std::uint64_t>& retained, std::size_t& remaining) {
    return list_.sync_thumbnails(work.shown, work.clip, wake, retained, remaining);
}
std::size_t FileListHostPeer::metric(PeerMetric metric) const {
    switch (metric) {
    case PeerMetric::thumbnails: return list_.thumbnail_count();
    case PeerMetric::ready_thumbnails: return list_.thumbnail_ready();
    case PeerMetric::painted_rows: return list_.rows();
    default: return 0;
    }
}

}
