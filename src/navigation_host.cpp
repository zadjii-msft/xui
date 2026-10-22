#include "navigation_host.hpp"
#include "context_menu.hpp"
#include "xui/commands.hpp"
#include "xui/titlebar.hpp"
#include "xui/navigation.hpp"
#include "xui/miller_columns.hpp"
#include "xui/map_view.hpp"
#include "xui/reveal.hpp"
#include <windowsx.h>
#include <cmath>
#include <utility>

namespace xui {
NavigationHostPeer::NavigationHostPeer(ControlHostServices& owner, std::shared_ptr<Control> model)
    : ControlHostPeer(owner, HostFamily::navigation, std::move(model)) {}
NavigationHostPeer::~NavigationHostPeer() { detach_owner(); }
NavigationHostPeer* navigation_peer(HostPeer& peer) {
    return peer.family == HostFamily::navigation ? static_cast<NavigationHostPeer*>(&peer) : nullptr;
}
void NavigationHostPeer::attach(HWND parent, int id) {
    ControlHostPeer::attach(parent, id);
    if (auto* columns = dynamic_cast<MillerColumns*>(control.get())) {
        const std::weak_ptr<ControlHostServices> owner =
            std::static_pointer_cast<ControlHostServices>(host.acquire_owner_lease());
        columns->on_focus_column([owner](const std::shared_ptr<VirtualCollection>& list) {
            if (const auto host = owner.lock()) host->focus_owned_control(*list);
        });
    }
}
void NavigationHostPeer::detach_owner() {
    if (row_images) row_images->clear();
    if (auto* columns = dynamic_cast<MillerColumns*>(control.get())) columns->on_focus_column({});
}
void NavigationHostPeer::cancel_input() {
    dragging = false;
    pressed_tab.reset();
    ControlHostPeer::cancel_input();
}
bool NavigationHostPeer::image_sync_required() const { return dynamic_cast<const TabStrip*>(control.get()) != nullptr; }
bool NavigationHostPeer::image_work(RowImageWork& work) {
    const auto* tabs = dynamic_cast<TabStrip*>(control.get());
    if (!tabs) return false;
    if (!row_images) row_images = std::make_unique<RowImages>();
    auto rect = work.clip;
    rect.x -= tabs->bounds().x; rect.y -= tabs->bounds().y;
    if (work.shown) for (std::size_t i = 0; i < tabs->tabs().size(); ++i) {
        const auto bounds = tabs->tab_bounds(i);
        if (bounds.width <= 0 || bounds.height <= 0 || bounds.x + bounds.width <= rect.x ||
            bounds.x >= rect.x + rect.width || bounds.y + bounds.height <= rect.y ||
            bounds.y >= rect.y + rect.height) continue;
        const auto& tab = tabs->tabs()[i];
        work.rows.push_back({{tab.id, 0}, {tab.icon, tab.image_path}});
        if (work.rows.size() == RowImages::maximum_rows) break;
    }
    work.clip = rect;
    work.tabs = true;
    return true;
}
bool NavigationHostPeer::sync_images(RowImageWork work, UINT dpi, const std::shared_ptr<TaskWake>& wake,
    std::vector<std::uint64_t>& retained, std::size_t&) {
    return row_images->sync_visuals(std::move(work.rows), dpi, wake, retained, 16);
}
std::size_t NavigationHostPeer::metric(PeerMetric metric) const {
    return metric == PeerMetric::retained_images && row_images ? row_images->count() : 0;
}
std::optional<LRESULT> NavigationHostPeer::input(UINT message, WPARAM wparam, LPARAM lparam,
    const HostInputContext& context) {
    auto* tabs = dynamic_cast<TabStrip*>(control.get());
    auto* columns = dynamic_cast<MillerColumns*>(control.get());
    const Point point{GET_X_LPARAM(lparam) * 96.0f / context.dpi, GET_Y_LPARAM(lparam) * 96.0f / context.dpi};
    switch (message) {
    case WM_CONTEXTMENU:
        if (tabs) {
            if (!host.input_enabled(*this) || !host.input_visible(*this)) return 0;
            std::optional<Point> position;
            if (lparam != -1) {
                POINT native{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(window, &native);
                position = Point{native.x * 96.0f / context.dpi, native.y * 96.0f / context.dpi};
            }
            if (!tabs->prepare_context_menu(position)) return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (pressed_tab) {
            POINT native{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            win32_require(ClientToScreen(window, &native) != 0, "Locate dragged tab");
            if (!(wparam & MK_LBUTTON) || !host.input_enabled(*this) || !host.input_visible(*this) ||
                !services.tab_gesture_allowed(*this)) {
                pressed_tab.reset();
                if (GetCapture() == window) ReleaseCapture();
                return 0;
            }
            if (std::abs(native.x - tab_press.x) >= GetSystemMetricsForDpi(SM_CXDRAG, context.dpi) ||
                std::abs(native.y - tab_press.y) >= GetSystemMetricsForDpi(SM_CYDRAG, context.dpi))
                services.begin_tab_gesture(*this, native);
            return 0;
        }
        if (columns && dragging) {
            const auto track = columns->horizontal_track(), thumb = columns->horizontal_thumb();
            const float distance = track.width - thumb.width;
            if (distance > 0 && host.input_enabled(*this) && host.input_visible(*this))
                columns->set_horizontal_offset(std::clamp(drag_offset +
                    (point.x - drag_y) * columns->maximum_horizontal() / distance, 0.0, columns->maximum_horizontal()));
            return 0;
        }
        if (tabs && (!tab_pointer || tab_pointer->x != point.x || tab_pointer->y != point.y)) {
            tab_pointer = point;
            host.invalidate_peer_paint();
        }
        break;
    case WM_MOUSELEAVE:
        tab_pointer.reset();
        break;
    case WM_LBUTTONDOWN:
        if (columns) {
            const auto track = columns->horizontal_track(), thumb = columns->horizontal_thumb();
            if (track.width > 0 && point.x >= 0 && point.x < track.width &&
                point.y >= track.y && point.y < track.y + track.height) {
                if (point.x >= thumb.x && point.x < thumb.x + thumb.width) {
                    dragging = true;
                    drag_y = point.x;
                    drag_offset = static_cast<float>(columns->horizontal_offset());
                    SetCapture(window);
                    host.invalidate_peer_paint();
                } else columns->scroll_horizontal((point.x < thumb.x ? -1 : 1) * columns->bounds().width * 0.9);
            }
            return 0;
        }
        if (tabs) {
            services.set_keyboard_focus_visible(false);
            host.invalidate_peer_paint();
            if (const auto index = tabs->hit_test(point)) {
                const auto close = tabs->close_bounds(*index);
                const auto id = tabs->tabs()[*index].id;
                if (close.width > 0 && point.x >= close.x && point.x < close.x + close.width &&
                    point.y >= close.y && point.y < close.y + close.height) tabs->request_close(id);
                else {
                    tabs->activate_tab(id);
                    if (host.owner_open() && services.tab_gesture_allowed(*this) && tabs->selected() == id &&
                        host.input_visible(*this) && host.input_enabled(*this)) {
                        pressed_tab = id;
                        tab_press = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                        win32_require(ClientToScreen(window, &tab_press) != 0, "Locate tab press");
                        SetCapture(window);
                    }
                }
            }
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (pressed_tab) {
            pressed_tab.reset();
            if (GetCapture() == window) ReleaseCapture();
            return 0;
        }
        dragging = false;
        break;
    case WM_KEYDOWN:
        if (tabs) {
            services.set_keyboard_focus_visible(true);
            host.invalidate_peer_paint();
            if (wparam == VK_LEFT || wparam == VK_RIGHT) { tabs->step(wparam == VK_LEFT ? -1 : 1); return 0; }
            if ((wparam == VK_RETURN || wparam == VK_SPACE) && !(lparam & (1LL << 30)) && tabs->selected()) {
                tabs->activate_tab(*tabs->selected());
                return 0;
            }
            if (wparam == VK_DELETE && !(lparam & (1LL << 30)) && tabs->selected()) {
                tabs->request_close(*tabs->selected());
                return 0;
            }
        }
        break;
    }
    return ControlHostPeer::input(message, wparam, lparam, context);
}
std::optional<LRESULT> NavigationHostPeer::accessibility_action(LPARAM action) {
    if (auto* tabs = dynamic_cast<TabStrip*>(control.get())) {
        if (action < 100) return UIA_E_INVALIDOPERATION;
        if (!tabs->select(static_cast<std::uint64_t>(action - 100))) return UIA_E_ELEMENTNOTAVAILABLE;
        host.update_peer_state();
        return S_OK;
    }
    if (auto* columns = dynamic_cast<MillerColumns*>(control.get())) {
        if (action >= 1000 && action <= 11000)
            columns->set_horizontal_offset(columns->maximum_horizontal() * (action - 1000) / 10000.0);
        else if (action >= 2 && action <= 5)
            columns->scroll_horizontal((action % 2 ? 1 : -1) * (action < 4 ? 96 : columns->bounds().width * 0.9));
        else return E_INVALIDARG;
        host.update_peer_state();
        return S_OK;
    }
    return {};
}
HRESULT NavigationHostPeer::foundation_action(const FoundationAction& action) {
    if (dynamic_cast<MenuBar::Heading*>(control.get()) &&
        (action.kind == FoundationAction::expand || action.kind == FoundationAction::collapse))
        return services.expand_menu_heading(*this, action.kind == FoundationAction::expand) ? S_OK : UIA_E_INVALIDOPERATION;
    return UIA_E_INVALIDOPERATION;
}

namespace {
D2D1_COLOR_F argb_color(uint32_t value) {
    return D2D1::ColorF(value & 0xffffff, static_cast<float>(value >> 24) / 255);
}
}

void NavigationHostPeer::paint(const HostPaintContext& context) {
    auto& peer = *this;
    auto& canvas = context.canvas;
    const auto& palette = context.palette;
    auto& control = *peer.control;
    const auto bounds = peer.paint_bounds;
    const auto presentation = services.control_presentation(peer);
    const auto foundation_text = context.enabled ? palette.text : palette.disabled;
    const bool fluent = palette.style == VisualStyle::winui;
    const bool focus_visible = context.focus_visible;
    const auto enabled = [&](const HostPeer& value) { return host.input_enabled(value); };
    const auto visible = [&](const HostPeer& value) { return host.input_visible(value); };
    const auto find_peer = [&](const Control* value) { return services.presented_peer(value); };
    const auto dpi = presentation.dpi;
    const bool keyboard_focus_visible = presentation.keyboard_focus_visible;
    using Peer = HostPeer;

    if (auto* columns = dynamic_cast<MillerColumns*>(&control)) {
        for (std::size_t i = 0; i < columns->columns().size(); ++i) {
            auto separator = columns->separator_bounds(i);
            if (separator.width > 0 && separator.height > 0) {
                const float scale = dpi / 96.0f;
                const float right = std::round((separator.x + separator.width) * scale) / scale;
                separator.x = std::round(separator.x * scale) / scale;
                separator.width = std::max(0.0f, right - separator.x);
                canvas.fill(separator, palette.border);
            }
        }
        const auto track = columns->horizontal_track(), thumb = columns->horizontal_thumb();
        if (track.width > 0) {
            canvas.fill(track, palette.surface);
            canvas.line(track.x, track.y, track.x + track.width, track.y, palette.border);
            if (fluent) canvas.scrollbar_thumb(thumb, palette, peer.dragging || control.hovered(), enabled(peer));
            else canvas.rounded(thumb, palette.secondary, 3);
        }
        return;
    }
    if (auto* bar = dynamic_cast<TitleBar*>(&control)) {
        std::vector<Peer*> strips;
        for (const auto& strip : {bar->tabs(), bar->secondary_tabs()})
            if (auto* target = find_peer(strip.get()); target && visible(*target) && !strip->tabs().empty())
                strips.push_back(target);
        std::sort(strips.begin(), strips.end(), [](const auto* a, const auto* b) {
            return a->paint_bounds.x < b->paint_bounds.x;
        });
        float left{};
        auto border = palette.border;
        for (const auto* strip : strips) {
            const auto color = static_cast<const TabStrip&>(*strip->control).colors().border;
            border = color && !palette.high_contrast ? D2D1::ColorF(*color) : palette.border;
            const float right = std::clamp(strip->paint_bounds.x - bounds.x, left, bounds.width);
            if (right > left) canvas.fill({left, std::max(0.0f, bounds.height - 1), right - left, 1}, border);
            left = std::clamp(strip->paint_bounds.x + strip->paint_bounds.width - bounds.x, right, bounds.width);
        }
        if (!strips.empty() && left < bounds.width)
            canvas.fill({left, std::max(0.0f, bounds.height - 1), bounds.width - left, 1}, border);
        return;
    }
    if (control.role() == ControlRole::tab_strip) {
        const auto& strip = static_cast<TabStrip&>(control);
        canvas.tab_strip(strip, bounds, palette, enabled(peer), peer.surface,
            control.focused() && keyboard_focus_visible, peer.tab_pointer, peer.row_images.get());
        return;
    }
}

}
