#pragma once

#include "drawing.hpp"
#include "images.hpp"
#include "host_accessibility.hpp"

namespace xui {

class AdaptiveLayout;
class NativeFieldHostPeer;
class NativeRuntimeHost;
class NativeSwapChainHost;
struct HostPeer;

class PeerHostServices {
public:
    virtual ~PeerHostServices() = default;
    virtual HWND create_input_surface(HostPeer&, HWND parent, int id) = 0;
    virtual LRESULT dispatch_input(HostPeer&, HWND, UINT, WPARAM, LPARAM) noexcept = 0;
    virtual bool input_enabled(const HostPeer&) const = 0;
    virtual bool input_visible(const HostPeer&) const = 0;
    virtual bool owner_open() const = 0;
    virtual bool focus_peer(HostPeer&, bool accessibility) = 0;
    virtual void reveal_peer(HostPeer&) = 0;
    virtual void update_peer_state() = 0;
    virtual void invalidate_peer_paint() = 0;
    virtual bool wheel_animation_allowed() const = 0;
    virtual void offer_row_tooltip(HostPeer&) = 0;
    virtual bool file_drag_eligible(const Control&, HWND) const = 0;
    virtual void deliver_file_drop(std::function<void()> callback) noexcept = 0;
    virtual std::shared_ptr<PeerHostServices> acquire_owner_lease() = 0;
    virtual void forget_peer(HostPeer&) noexcept = 0;
    virtual void peer_failure() noexcept = 0;
};

enum class HostFamily { unextracted, collection, grid, file_list, native_field, text_clear };
enum class PeerMetric { retained_images, thumbnails, ready_thumbnails, painted_rows };

struct HostPaintContext {
    Drawing& canvas;
    const Palette& palette;
    bool enabled{}, focus_visible{};
};
struct HostInputContext {
    UINT dpi;
    const Palette& palette;
};
struct RowImageWork {
    HostPeer* peer{};
    Rect clip;
    bool shown{};
    std::shared_ptr<const CollectionIndex> source;
    std::vector<RowVisual> rows;
    bool tabs{};
};

struct HostPeer {
    PeerHostServices& host;
    const HostFamily family;
    std::shared_ptr<Control> control;
    HWND window{};
    IRawElementProviderSimple* provider{};
    bool tracking{}, surface{}, suppress_popup_click{};
    Rect paint_bounds{}, arranged_bounds{}, absolute_bounds{};
    RECT placed_bounds{};
    bool placed{}, arranged{};
    HostPeer* parent{};
    AdaptiveLayout* adaptive{};

    HostPeer(PeerHostServices&, HostFamily, std::shared_ptr<Control>);
    virtual ~HostPeer();
    HostPeer(const HostPeer&) = delete;
    HostPeer& operator=(const HostPeer&) = delete;
    virtual void attach(HWND parent, int id) = 0;
    virtual void sync(UINT dpi, const Palette&) = 0;
    virtual void set_theme(UINT, const Palette&) {}
    virtual void publish_accessibility() = 0;
    virtual void disconnect_accessibility() = 0;
    virtual void publish_invoked() {}
    virtual void detach_owner() {}
    virtual void cancel_input() {}
    virtual bool capture_active() const { return false; }
    virtual void paint(const HostPaintContext&) = 0;
    virtual std::optional<LRESULT> input(UINT, WPARAM, LPARAM, const HostInputContext&) = 0;
    virtual void cancel_wheel() {}
    virtual bool advance_wheel(Animation::Clock::time_point, bool, bool) { return false; }
    virtual bool image_sync_required() const { return false; }
    virtual bool image_work(RowImageWork&) { return false; }
    virtual bool sync_images(RowImageWork, UINT, const std::shared_ptr<TaskWake>&,
        std::vector<std::uint64_t>&, std::size_t&) { return false; }
    virtual std::size_t metric(PeerMetric) const { return 0; }
    virtual void native_moved() {}

    // Temporary native capabilities used by window-wide placement and composition.
    virtual NativeFieldHostPeer* native_field() const { return nullptr; }
    virtual NativeRuntimeHost* runtime_host() const { return nullptr; }
    virtual NativeSwapChainHost* swap_chain_host() const { return nullptr; }
    virtual Drawing* overlay_surface() const { return nullptr; }
    virtual ImagePeer* image_peer() const { return nullptr; }
    virtual HWND caption_window() const { return nullptr; }
    virtual HWND child_content_window() const { return nullptr; }
    bool native() const { return native_field() != nullptr; }
    void track_pointer_leave();
    void retire_native();
};

struct RowWheelMotion {
    bool active{};
    double start{}, target{}, last{};
    Animation::Clock::time_point started{};
    std::weak_ptr<const CollectionIndex> source;
    double scroll(double current, double maximum, const std::shared_ptr<const CollectionIndex>&,
        double delta, bool precise, bool allowed);
    std::optional<double> advance(double current, const CollectionIndex*,
        Animation::Clock::time_point now, bool settle, bool tick);
};

SelectionGesture row_keyboard_gesture();
bool row_pointer_inside(HWND, LPARAM);

}
