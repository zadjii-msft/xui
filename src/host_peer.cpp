#include "host_peer.hpp"
#include <windowsx.h>
#include <cmath>
#include <utility>

namespace xui {

HostPeer::HostPeer(PeerHostServices& owner, HostFamily kind, std::shared_ptr<Control> value)
    : host(owner), family(kind), control(std::move(value)) {}

HostPeer::~HostPeer() {
    if (provider) provider->Release();
}

void HostPeer::track_pointer_leave() {
    if (!tracking) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
        tracking = TrackMouseEvent(&track) != 0;
    }
}

void HostPeer::retire_native() {
    host.forget_peer(*this);
    if (window && IsWindow(window)) DestroyWindow(window);
    window = nullptr;
}

double RowWheelMotion::scroll(double current, double maximum,
    const std::shared_ptr<const CollectionIndex>& current_source, double delta, bool precise, bool allowed) {
    const double next = std::clamp((active && current == last ? target : current) + delta, 0.0, maximum);
    if (precise || !allowed) {
        active = false;
        return next;
    }
    start = last = current;
    target = next;
    started = Animation::Clock::now();
    source = current_source;
    active = current != target;
    return current;
}

std::optional<double> RowWheelMotion::advance(double current, const CollectionIndex* current_source,
    Animation::Clock::time_point now, bool settle, bool tick) {
    if (!active) return {};
    if (current != last || source.lock().get() != current_source) {
        active = false;
        return {};
    }
    const double t = settle ? 1 : std::clamp(
        std::chrono::duration<double, std::milli>(now - started).count() / 140.0, 0.0, 1.0);
    if (!tick && !settle) return {};
    last = start + (target - start) * (1 - std::pow(1 - t, 3));
    active = t < 1;
    return last;
}

SelectionGesture row_keyboard_gesture() {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    return shift ? (ctrl ? SelectionGesture::add_range : SelectionGesture::extend) :
        ctrl ? SelectionGesture::focus_only : SelectionGesture::replace;
}

bool row_pointer_inside(HWND window, LPARAM point) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    return PtInRect(&bounds, {GET_X_LPARAM(point), GET_Y_LPARAM(point)}) != 0;
}

}
