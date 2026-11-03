#include "composite_host.hpp"
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
CompositeHostPeer::CompositeHostPeer(ControlHostServices& owner, std::shared_ptr<Control> model)
    : ControlHostPeer(owner, HostFamily::composite, std::move(model)) {}
CompositeHostPeer* composite_peer(HostPeer& peer) {
    return peer.family == HostFamily::composite ? static_cast<CompositeHostPeer*>(&peer) : nullptr;
}
void CompositeHostPeer::prepare_overlay(VisualStyle style) {
    if (overlay_drawing) return;
    auto drawing = std::make_unique<Drawing>();
    drawing->initialize(style);
    const auto flags = GetWindowLongPtrW(window, GWL_EXSTYLE);
    win32_require(SetWindowLongPtrW(window, GWL_EXSTYLE, flags & ~WS_EX_TRANSPARENT) != 0,
        "Enable popup rendering surface");
    overlay_drawing = std::move(drawing);
}
void CompositeHostPeer::reconcile_input() {
    if (auto* split = dynamic_cast<SplitView*>(control.get()); split && dragging &&
        (!split->divider_dragging() || split->divider().width <= 0)) {
        dragging = false;
        split->set_style_dragging(false);
    }
}
void CompositeHostPeer::cancel_input() {
    dragging = false;
    if (auto* split = dynamic_cast<SplitView*>(control.get())) split->set_style_dragging(false);
    if (auto* scroll = dynamic_cast<ScrollView*>(control.get())) scroll->set_style_dragging(false);
    ControlHostPeer::cancel_input();
}
std::optional<LRESULT> CompositeHostPeer::input(UINT message, WPARAM wparam, LPARAM lparam,
    const HostInputContext& context) {
    auto* split = dynamic_cast<SplitView*>(control.get());
    auto* scroll = dynamic_cast<ScrollView*>(control.get());
    auto* expander = dynamic_cast<Expander*>(control.get());
    const float x = GET_X_LPARAM(lparam) * 96.0f / context.dpi;
    const float y = GET_Y_LPARAM(lparam) * 96.0f / context.dpi;
    const auto inside_header = [&] {
        return row_pointer_inside(window, lparam) && (!expander ||
            context.palette.style != VisualStyle::winui || y < expander->effective_header_height());
    };
    switch (message) {
    case WM_SETCURSOR:
        if (split && host.input_enabled(*this) && split->expanded() &&
            reinterpret_cast<HWND>(wparam) == window && LOWORD(lparam) == HTCLIENT) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            const float px = point.x * 96.0f / context.dpi + split->bounds().x;
            const float py = point.y * 96.0f / context.dpi + split->bounds().y;
            const auto divider = split->divider();
            if (dragging || (px >= divider.x && px < divider.x + divider.width &&
                py >= divider.y && py < divider.y + divider.height)) {
                SetCursor(LoadCursorW(nullptr, split->axis() == Axis::horizontal ? IDC_SIZEWE : IDC_SIZENS));
                return TRUE;
            }
        }
        break;
    case WM_MOUSEMOVE:
        if (dragging && split) {
            if (split->divider().width <= 0 || !split->divider_dragging()) {
                dragging = false;
                split->set_style_dragging(false);
                if (GetCapture() == window) ReleaseCapture();
                return 0;
            }
            const auto area = split->pane_area();
            const bool horizontal = split->axis() == Axis::horizontal;
            const float extent = (horizontal ? area.width : area.height) - split->effective_divider_width();
            const auto position = horizontal ? x : y;
            const auto origin = horizontal ? area.x - split->bounds().x : area.y - split->bounds().y;
            if (extent > 0) split->set_ratio((position - drag_offset - origin) / extent);
            host.update_peer_state();
            return 0;
        }
        if (dragging && scroll) {
            const float track = scroll->scrollbar_thumb_track().height - scroll->thumb().height;
            if (track > 0) scroll->set_offset(drag_offset + (y - drag_y) * scroll->maximum_offset() / track);
            host.update_peer_state();
            return 0;
        }
        if (expander) {
            control->pointer_move(inside_header());
            track_pointer_leave();
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        if (expander && y >= expander->effective_header_height()) return 0;
        if (split && host.input_enabled(*this) && split->expanded()) {
            const auto divider = split->divider();
            if (x >= divider.x - split->bounds().x && x < divider.x - split->bounds().x + divider.width &&
                y >= divider.y - split->bounds().y && y < divider.y - split->bounds().y + divider.height) {
                if (GetForegroundWindow() == services.owner_window()) SetFocus(window);
                dragging = true;
                split->set_style_dragging(true);
                drag_offset = split->axis() == Axis::horizontal ?
                    x - (divider.x - split->bounds().x) : y - (divider.y - split->bounds().y);
                SetCapture(window);
            }
            return 0;
        }
        if (scroll && control->enabled()) {
            SetFocus(window);
            const auto thumb = scroll->thumb();
            const auto track = scroll->scrollbar_track();
            if (x >= track.x - scroll->bounds().x && x < track.x + track.width - scroll->bounds().x &&
                y >= track.y - scroll->bounds().y && y < track.y + track.height - scroll->bounds().y && thumb.height) {
                if (y >= thumb.y - scroll->bounds().y && y < thumb.y - scroll->bounds().y + thumb.height) {
                    dragging = true;
                    scroll->set_style_dragging(true);
                    drag_y = y;
                    drag_offset = scroll->offset();
                    SetCapture(window);
                } else scroll->scroll_by(y < thumb.y - scroll->bounds().y ?
                    -scroll->viewport().height : scroll->viewport().height);
                host.update_peer_state();
            }
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        dragging = false;
        if (split) split->set_style_dragging(false);
        if (scroll) scroll->set_style_dragging(false);
        if (expander) {
            if (std::exchange(suppress_popup_click, false)) return 0;
            services.activate_control(*this, control->pointer_up(inside_header()));
            if (GetCapture() == window) ReleaseCapture();
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (expander && (wparam == VK_LEFT || wparam == VK_RIGHT)) {
            if (expander->expanded() != (wparam == VK_RIGHT)) control->invoke();
            return 0;
        }
        if (split && host.input_enabled(*this) && split->divider().width > 0) {
            const auto previous = split->axis() == Axis::horizontal ? VK_LEFT : VK_UP;
            const auto next = split->axis() == Axis::horizontal ? VK_RIGHT : VK_DOWN;
            if (wparam == previous || wparam == next) {
                split->set_ratio(split->ratio() + (wparam == previous ? -0.025f : 0.025f));
                return 0;
            }
            if (wparam == VK_HOME) { split->set_ratio(0.5f); return 0; }
        }
        if (scroll && services.scroll_host_key(*this, wparam)) return 0;
        break;
    }
    return ControlHostPeer::input(message, wparam, lparam, context);
}
std::optional<LRESULT> CompositeHostPeer::accessibility_action(LPARAM action) {
    if (auto* split = dynamic_cast<SplitView*>(control.get())) {
        if (!split->primary_visible()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (action < 1000 || action > 11000) return E_INVALIDARG;
        split->set_ratio(static_cast<float>(action - 1000) / 10000);
        host.update_peer_state();
        return S_OK;
    }
    if (auto* scroll = dynamic_cast<ScrollView*>(control.get())) {
        if (action >= 1000 && action <= 11000)
            scroll->set_offset(scroll->maximum_offset() * (action - 1000) / 10000.0f);
        else if (action >= 2 && action <= 5)
            services.scroll_host_key(*this, action == 2 ? VK_UP : action == 3 ? VK_DOWN : action == 4 ? VK_PRIOR : VK_NEXT);
        else return UIA_E_INVALIDOPERATION;
        host.update_peer_state();
        return S_OK;
    }
    return {};
}
HRESULT CompositeHostPeer::foundation_action(const FoundationAction& action) {
    if (auto* popup = dynamic_cast<Popup*>(control.get()); popup && popup->dialog_surface() &&
        action.kind == FoundationAction::collapse) {
        services.dismiss_host_popup(*popup, PopupDismissReason::cancel);
        return S_OK;
    }
    if (auto* expander = dynamic_cast<Expander*>(control.get()); expander &&
        (action.kind == FoundationAction::expand || action.kind == FoundationAction::collapse)) {
        if (expander->expanded() != (action.kind == FoundationAction::expand)) expander->invoke();
        return S_OK;
    }
    return UIA_E_INVALIDOPERATION;
}

namespace {
D2D1_COLOR_F argb_color(uint32_t value) {
    return D2D1::ColorF(value & 0xffffff, static_cast<float>(value >> 24) / 255);
}
}

void CompositeHostPeer::paint(const HostPaintContext& context) {
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

    if (control.role() == ControlRole::popup) return;
    if (auto* expander = dynamic_cast<Expander*>(&control)) {
        const auto local = [&](Rect value) { value.x -= control.bounds().x; value.y -= control.bounds().y; return value; };
        const bool regular_winui = fluent && !palette.high_contrast;
        const auto brushes = winui_card_brushes(palette.mode);
        const auto header_fill = regular_winui ? argb_color(brushes.fill) :
            control.pressed() && fluent ? palette.selection : control.hovered() ? palette.hover : palette.surface;
        const auto paint_disclosure = [&](D2D1_COLOR_F ink) {
            const auto area = local(expander->disclosure_bounds());
            if (regular_winui) {
                const auto fill = winui_button_brushes(palette.mode, ButtonAppearance::subtle,
                    enabled(peer), control.hovered(), control.pressed(), false).fill;
                if (fill >> 24) canvas.rounded(area, argb_color(fill), 4);
            }
            if (fluent)
                canvas.symbol(expander->expanded() ? Symbol::chevron_up : Symbol::chevron_down, area, ink, 12);
            else canvas.chevron(area, ink, expander->expanded());
        };
        if (fluent || expander->has_control_styling()) {
            const PartStyleValues empty;
            const auto* header = expander->effective_control_style_values(StylePart::header);
            const auto* text = expander->effective_control_style_values(StylePart::text);
            const auto* disclosure = expander->effective_control_style_values(StylePart::disclosure);
            const auto face = local(expander->header_bounds());
            canvas.styled_surface(face, palette, header ? *header : empty,
                header_fill,
                regular_winui ? argb_color(brushes.stroke) : palette.border,
                fluent ? 4.0f : 6.0f, fluent ? Insets{1, 1, 1, 1} : Insets{},
                fluent && expander->body_presented() ? Drawing::SurfaceCorners::top : Drawing::SurfaceCorners::all);
            const auto ink = [&](const PartStyleValues* values) {
                return !palette.high_contrast && values && values->foreground ?
                    D2D1::ColorF(values->foreground->resolve(palette.mode)) : foundation_text;
            };
            canvas.styled_text(control.name(), local(expander->header_text_bounds()), ink(text), text ? *text : empty);
            paint_disclosure(ink(disclosure));
            if (focus_visible) {
                if (fluent) {
                    const float radius = !palette.high_contrast && header ? header->corner_radius.value_or(4.0f) : 4.0f;
                    canvas.winui_focus_ring(face, palette, radius, 3,
                        expander->body_presented() ? Drawing::SurfaceCorners::top : Drawing::SurfaceCorners::all);
                }
                else canvas.rounded(face, palette.accent, 6, true);
            }
            return;
        }
        const auto height = expander->effective_header_height();
        const float edge = 1.0f;
        const Rect header{edge, edge, std::max(0.0f, bounds.width - 2 * edge),
            std::max(0.0f, std::min(bounds.height, height) - 2 * edge)};
        const auto fill = header_fill;
        canvas.rounded(header, fill, 6);
        canvas.text(expander->expanded() ? L"\u25be" : L"\u25b8", {10, 0, 24, height}, foundation_text);
        canvas.text(control.name(), {36, 0, std::max(0.0f, bounds.width - 48), height}, foundation_text);
        if (focus_visible) canvas.rounded(header, palette.accent, 6, true);
        return;
    }
    if (control.role() == ControlRole::content_view) return;
    if (control.role() == ControlRole::split_view) {
        const auto& split = static_cast<SplitView&>(control);
        auto d = split.divider();
        if (!d.width) return;
        d.x -= bounds.x; d.y -= bounds.y;
        const auto* divider_style = split.effective_control_style_values(StylePart::divider);
        const auto* grip_style = split.effective_control_style_values(StylePart::grip);
        const PartStyleValues empty;
        canvas.styled_surface(d, palette, divider_style ? *divider_style : empty, palette.background, palette.border, 0, {});
        const bool horizontal = split.axis() == Axis::horizontal;
        const auto width = std::min(horizontal ? 4.0f : 44.0f, d.width);
        const auto height = std::min(horizontal ? 44.0f : 4.0f, d.height);
        canvas.styled_surface({d.x + (d.width - width) / 2, d.y + (d.height - height) / 2, width, height},
            palette, grip_style ? *grip_style : empty,
            control.focused() || peer.dragging ? palette.accent : fluent ? palette.secondary : palette.border,
            palette.border, 2, {});
        if (fluent && control.focused()) canvas.focus_ring(d, palette, 2);
        return;
    }
    if (control.role() == ControlRole::scroll_view) {
        const auto& scroll = static_cast<ScrollView&>(control);
        if (scroll.passthrough()) return;
        auto thumb = scroll.thumb();
        thumb.x -= bounds.x;
        thumb.y -= bounds.y;
        const auto* track_style = scroll.effective_control_style_values(StylePart::scrollbar_track);
        const auto* thumb_style = scroll.effective_control_style_values(StylePart::scrollbar_thumb);
        if (!scroll.overlay_scrollbar() || track_style) {
            auto track = scroll.scrollbar_track(); track.x -= bounds.x; track.y -= bounds.y;
            const PartStyleValues empty;
            canvas.styled_surface(track, palette, track_style ? *track_style : empty, palette.background, palette.border, 0, {});
        }
        if (thumb.height) {
            if (thumb_style) canvas.styled_surface(thumb, palette, *thumb_style,
                !enabled(peer) ? palette.border : peer.dragging || control.hovered() ? palette.accent : palette.secondary,
                palette.border, 4, {});
            else if (fluent) canvas.scrollbar_thumb(thumb, palette, peer.dragging || control.hovered(), enabled(peer));
            else canvas.rounded(thumb, control.enabled() ?
                (peer.dragging || control.hovered() ? palette.accent : palette.secondary) : palette.border, 4);
        }
        if (focus_visible) {
            const Rect face{0.5f, 0.5f, std::max(0.0f, bounds.width - 1), std::max(0.0f, bounds.height - 1)};
            if (fluent) canvas.focus_ring(face, palette);
            else canvas.outline(face, palette.accent);
        }
        return;
    }
}

}

namespace xui {
void CompositeHostPeer::attach(HWND parent, int id) {
    ControlHostPeer::attach(parent, id);
    if (control->role() == ControlRole::scroll_view) scroll_content = services.create_scroll_surface(*this);
    auto* peer = this;
    auto* added = this;
    const auto role = control->role();
        if (role == ControlRole::expander)
            peer->control->set_text_measurer([this, added](std::wstring_view text, TextStyle style) {
                Size measured{};
                const auto& control = *added->control;
                const auto* values = control.has_control_styling() ?
                    control.effective_control_style_values(StylePart::text) : nullptr;
                if (values) services.control_drawing().styled_layout(text, style, *values, measured);
                else services.control_drawing().layout(text, style, measured);
                return measured;
            });
}
}
