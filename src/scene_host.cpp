#include "scene_host.hpp"
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
SceneHostPeer::SceneHostPeer(ControlHostServices& owner, std::shared_ptr<Control> model)
    : ControlHostPeer(owner, HostFamily::scene, std::move(model)) {
    if (!dynamic_cast<Image*>(control.get()) && !dynamic_cast<VectorCanvas*>(control.get()) &&
        !dynamic_cast<RuntimeHost*>(control.get()) && !dynamic_cast<SwapChainPanel*>(control.get()))
        throw std::invalid_argument("A scene host requires an image, scene, runtime or swap-chain model");
    if (auto* image = dynamic_cast<Image*>(control.get())) image_ = std::make_unique<ImagePeer>(*image);
}
SceneHostPeer::~SceneHostPeer() {
    detach_owner();
    runtime_.reset();
    swap_chain_.reset();
}
void SceneHostPeer::attach(HWND parent, int id) {
    ControlHostPeer::attach(parent, id);
    if (auto runtime = std::dynamic_pointer_cast<RuntimeHost>(control))
        runtime_ = std::make_unique<NativeRuntimeHost>(runtime, window,
            [owner = &services] { owner->peer_failure(); },
            [owner = &services] { return owner->native_activation_allowed(true); });
    if (auto panel = std::dynamic_pointer_cast<SwapChainPanel>(control))
        swap_chain_ = std::make_unique<NativeSwapChainHost>(panel, window,
            [owner = &services] { return owner->native_activation_allowed(false); },
            services.composition_presenter(), services.owner_window());
}
void SceneHostPeer::detach_owner() {
    if (image_) image_->detach();
    if (runtime_) runtime_->cancel_owner();
    if (swap_chain_) swap_chain_->cancel_owner();
    if (auto* map = dynamic_cast<MapView*>(control.get())) map->cancel_request();
}
void SceneHostPeer::reconcile_input() {
    if (auto* map = dynamic_cast<MapView*>(control.get());
        map && (!host.input_visible(*this) || !IsWindowVisible(services.owner_window()))) map->cancel_request();
}
void SceneHostPeer::cancel_input() {
    dragging_ = false;
    ControlHostPeer::cancel_input();
}
std::optional<LRESULT> SceneHostPeer::input(UINT message, WPARAM wparam, LPARAM lparam,
    const HostInputContext& context) {
    auto* map = dynamic_cast<MapView*>(control.get());
    switch (message) {
    case runtime_event_message:
        if (runtime_) runtime_->event();
        return 0;
    case WM_MOUSEWHEEL:
        if (map) {
            if (!host.input_enabled(*this)) return 0;
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(window, &point);
            map->zoom_at(GET_WHEEL_DELTA_WPARAM(wparam) / 120.0,
                {point.x * 96.0f / context.dpi, point.y * 96.0f / context.dpi});
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (map && dragging_) {
            const float x = GET_X_LPARAM(lparam) * 96.0f / context.dpi;
            const float y = GET_Y_LPARAM(lparam) * 96.0f / context.dpi;
            map->pan(x - drag_x_, y - drag_y_);
            drag_x_ = x;
            drag_y_ = y;
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        if (auto* vector = dynamic_cast<VectorCanvas*>(control.get())) {
            SetFocus(window);
            const Point point{GET_X_LPARAM(lparam) * 96.0f / context.dpi,
                GET_Y_LPARAM(lparam) * 96.0f / context.dpi};
            const auto area = vector->canvas_bounds();
            if (point.x < area.x || point.y < area.y || point.x >= area.x + area.width ||
                point.y >= area.y + area.height) return 0;
            if (const auto id = vector->hit_test(point)) { vector->select(*id); return 0; }
            if (map) {
                dragging_ = true;
                drag_x_ = point.x;
                drag_y_ = point.y;
                SetCapture(window);
            }
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        dragging_ = false;
        break;
    case WM_KEYDOWN:
        if (map) {
            if (wparam == VK_LEFT || wparam == VK_RIGHT) { map->pan(wparam == VK_LEFT ? 32 : -32, 0); return 0; }
            if (wparam == VK_UP || wparam == VK_DOWN) { map->pan(0, wparam == VK_UP ? 32 : -32); return 0; }
            if (wparam == VK_ADD || wparam == VK_OEM_PLUS || wparam == VK_SUBTRACT || wparam == VK_OEM_MINUS) {
                const auto bounds = map->canvas_bounds();
                map->zoom_at(wparam == VK_ADD || wparam == VK_OEM_PLUS ? 1 : -1,
                    {bounds.x + bounds.width / 2, bounds.y + bounds.height / 2});
                return 0;
            }
            if (wparam == VK_HOME) { map->set_view({}, 1); return 0; }
        }
        break;
    case WM_NCDESTROY:
        runtime_.reset();
        swap_chain_.reset();
        break;
    }
    return ControlHostPeer::input(message, wparam, lparam, context);
}

namespace {
D2D1_COLOR_F argb_color(uint32_t value) {
    return D2D1::ColorF(value & 0xffffff, static_cast<float>(value >> 24) / 255);
}
}

void SceneHostPeer::paint(const HostPaintContext& context) {
    auto& peer = *this;
    auto& canvas = context.canvas;
    const auto& palette = context.palette;
    auto& control = *peer.control;
    const auto bounds = peer.paint_bounds;
    const auto presentation = services.control_presentation(peer);
    const auto foundation_text = context.enabled ? palette.text : palette.disabled;
    const bool fluent = palette.style == VisualStyle::winui;
    const bool focus_visible = context.focus_visible;
    const auto paint_host_text = [&](const Control& value, StylePart part, std::wstring_view text, Rect area, D2D1_COLOR_F ink) {
        xui::paint_host_text(canvas, palette, value, part, text, area, ink);
    };

    if (control.role() == ControlRole::swap_chain_panel) {
        // Composed producers can be translucent; leave the content beneath them visible.
        if (!(presentation.composition && peer.swap_chain_host() && peer.swap_chain_host()->active()))
            canvas.fill({0, 0, bounds.width, bounds.height}, palette.field);
        return;
    }
    if (auto* runtime = dynamic_cast<RuntimeHost*>(&control)) {
        const auto area = runtime->visual_bounds();
        const auto* root_values = runtime->effective_control_style_values(StylePart::root);
        if (root_values) canvas.styled_surface({0, 0, bounds.width, bounds.height}, palette, *root_values,
            palette.field, palette.border, fluent ? 4.0f : 0.0f, fluent ? Insets{1, 1, 1, 1} : Insets{});
        else canvas.fill(area, palette.field);
        if (!peer.runtime_host() || !peer.runtime_host()->active()) {
            const bool rounded_clip = root_values && !palette.high_contrast && canvas.push_rounded_clip(
                {0, 0, bounds.width, bounds.height}, root_values->corner_radius.value_or(fluent ? 4.0f : 0.0f));
            canvas.push_clip(area);
            const auto* caption = runtime->effective_control_style_values(StylePart::caption);
            const bool has_caption = caption && !caption->empty();
            if (has_caption) paint_host_text(control, StylePart::caption, runtime->name(),
                {area.x + 12, area.y + 8, std::max(0.0f, area.width - 24), 24}, palette.text);
            paint_host_text(control, runtime->state() == HostState::error ? StylePart::error : StylePart::placeholder,
                runtime->state() == HostState::error ? std::wstring_view(runtime->error()) :
                L"Explicit load required. Native runtime is not active.",
                {area.x + 12, area.y + (has_caption ? 36.0f : 12.0f), std::max(0.0f, area.width - 24), 48},
                runtime->state() == HostState::error ? palette.error : palette.secondary);
            canvas.pop_clip();
            if (rounded_clip) canvas.pop_rounded_clip();
        }
        if (!root_values && fluent && (!peer.runtime_host() || !peer.runtime_host()->active()))
            canvas.rounded(area, palette.border, 4, true);
        if (focus_visible) canvas.focus_ring(area, palette);
        return;
    }
    if (auto* vector = dynamic_cast<VectorCanvas*>(&control)) {
        const auto area = vector->canvas_bounds();
        const auto* root_values = vector->effective_control_style_values(StylePart::root);
        if (root_values) canvas.styled_surface({0, 0, bounds.width, bounds.height}, palette, *root_values,
            palette.field, palette.border, fluent ? 4.0f : 0.0f, fluent ? Insets{1, 1, 1, 1} : Insets{});
        else canvas.fill(area, palette.field);
        const bool rounded_clip = root_values && !palette.high_contrast && canvas.push_rounded_clip(
            {0, 0, bounds.width, bounds.height}, root_values->corner_radius.value_or(fluent ? 4.0f : 0.0f));
        canvas.push_clip(area);
        const auto* selection = vector->effective_control_style_values(StylePart::selection);
        const auto highlight = selection && selection->foreground && !palette.high_contrast ?
            D2D1::ColorF(selection->foreground->resolve(palette.mode)) : palette.accent;
        canvas.origin(bounds.x + area.x, bounds.y + area.y);
        canvas.scene(vector->scene(), vector->selected(), highlight);
        canvas.origin(bounds.x, bounds.y);
        if (auto* map = dynamic_cast<MapView*>(vector)) {
            const auto center = map->center();
            paint_host_text(control, StylePart::coordinate, L"Offline coordinate map | " + std::to_wstring(center.latitude).substr(0, 7) + L", " +
                std::to_wstring(center.longitude).substr(0, 8) + L" | zoom " + std::to_wstring(map->zoom()).substr(0, 4),
                {area.x + 8, area.y + 4, std::max(0.0f, area.width - 16), 24}, palette.text);
            if (!map->error().empty()) paint_host_text(control, StylePart::error, map->error(),
                {area.x + 8, area.y + 30, std::max(0.0f, area.width - 16), 32}, palette.error);
        } else if ((!vector->scene() || vector->scene()->shapes().empty()) && vector->has_control_styling()) {
            paint_host_text(control, StylePart::empty, L"No shapes",
                {area.x + 8, area.y + 4, std::max(0.0f, area.width - 16), 32}, palette.secondary);
        }
        canvas.pop_clip();
        if (rounded_clip) canvas.pop_rounded_clip();
        if (!root_values && fluent) canvas.rounded(area, palette.border, 4, true);
        if ((fluent && control.focused()) || focus_visible) canvas.focus_ring(area, palette);
        return;
    }
    if (peer.image_peer()) {
        const auto& image = static_cast<const Image&>(control);
        const auto* root_values = image.effective_control_style_values(StylePart::root);
        const auto area = image.content_bounds();
        if (root_values) canvas.styled_surface({0, 0, bounds.width, bounds.height}, palette, *root_values,
            palette.surface, palette.border, fluent ? 4.0f : 0.0f, fluent ? Insets{1, 1, 1, 1} : Insets{});
        else canvas.fill({0, 0, bounds.width, bounds.height}, palette.surface);
        const bool rounded_clip = root_values && !palette.high_contrast && canvas.push_rounded_clip(
            {0, 0, bounds.width, bounds.height}, root_values->corner_radius.value_or(fluent ? 4.0f : 0.0f));
        if (root_values) canvas.push_clip(area);
        peer.image_peer()->paint(canvas, area);
        if (image.status() != ImageStatus::ready)
            paint_host_text(control, image.status() == ImageStatus::error ? StylePart::error : StylePart::placeholder,
                image.status() == ImageStatus::error ? image.error() :
                image.status() == ImageStatus::loading ? L"Loading image..." : L"No image",
                root_values ? Rect{area.x + 6, area.y, std::max(0.0f, area.width - 12), area.height} :
                    Rect{8, 0, std::max(0.0f, bounds.width - 16), bounds.height},
                image.status() == ImageStatus::error ? palette.error : palette.secondary);
        if (root_values) canvas.pop_clip();
        if (rounded_clip) canvas.pop_rounded_clip();
        if (!root_values && fluent) canvas.rounded({1, 1, std::max(0.0f, bounds.width - 2), std::max(0.0f, bounds.height - 2)}, palette.border, 4, true);
        if (focus_visible) canvas.focus_ring({0, 0, bounds.width, bounds.height}, palette);
        return;
    }
}

}
