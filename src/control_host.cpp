#include "control_host.hpp"
#include "context_menu.hpp"
#include "workspace_accessibility.hpp"
#include "xui/menu_bar.hpp"
#include <windowsx.h>
#include <utility>

namespace xui {

ControlHostPeer::ControlHostPeer(ControlHostServices& owner, HostFamily family, std::shared_ptr<Control> model)
    : HostPeer(owner, family, std::move(model)), services(owner) {
    if (!control) throw std::invalid_argument("A control host requires a model");
}

ControlHostPeer::~ControlHostPeer() {
    retire_native();
    disconnect_accessibility();
}

void ControlHostPeer::attach(HWND parent, int id) {
    window = host.create_input_surface(*this, parent, id);
    publish_control(accessibility, nullptr, *control, window);
    const auto role = control->role();
    provider = role == ControlRole::tab_strip || role == ControlRole::split_view || role >= ControlRole::popup ||
        dynamic_cast<MenuBar*>(control.get()) || dynamic_cast<MenuBar::Heading*>(control.get()) ?
        create_workspace_provider(accessibility) : create_control_provider(accessibility);
}

void ControlHostPeer::sync(UINT, const Palette&) {
    reconcile_input();
    publish_accessibility();
}
void ControlHostPeer::publish_accessibility() { publish_control(accessibility, provider, *control, window); }
void ControlHostPeer::disconnect_accessibility() { disconnect_control(accessibility, provider); }
void ControlHostPeer::publish_invoked() { raise_control_invoked(provider); }
void ControlHostPeer::cancel_input() { control->cancel(); }
std::optional<LRESULT> ControlHostPeer::accessibility_action(LPARAM) { return {}; }
HRESULT ControlHostPeer::foundation_action(const FoundationAction&) { return UIA_E_INVALIDOPERATION; }

std::optional<LRESULT> ControlHostPeer::input(UINT message, WPARAM wparam, LPARAM lparam,
    const HostInputContext& context) {
    const auto inside = [&] { return row_pointer_inside(window, lparam); };
    switch (message) {
    case WM_KILLFOCUS:
        cancel_input();
        return 0;
    case WM_MOUSEMOVE:
        control->pointer_move(inside());
        track_pointer_leave();
        return 0;
    case WM_MOUSELEAVE:
        tracking = false;
        control->pointer_move(false);
        return 0;
    case WM_LBUTTONDOWN:
        if (!host.input_enabled(*this) || !host.input_visible(*this)) return 0;
        control->pointer_move(inside());
        if (control->pointer_down()) {
            SetFocus(window);
            SetCapture(window);
            start_repeat(true);
        }
        return 0;
    case WM_LBUTTONUP:
        if (std::exchange(suppress_popup_click, false)) return 0;
        if (services.caption_control(*control)) control->pointer_move(inside());
        services.activate_control(*this, control->pointer_up(inside()));
        if (GetCapture() == window) ReleaseCapture();
        return 0;
    case WM_LBUTTONDBLCLK:
        return input(WM_LBUTTONDOWN, wparam, lparam, context);
    case WM_CANCELMODE:
        cancel_input();
        if (GetCapture() == window) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        cancel_input();
        host.invalidate_peer_paint();
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            cancel_input();
            if (GetCapture() == window) ReleaseCapture();
            return 0;
        }
        if (wparam == VK_SPACE || wparam == VK_RETURN) {
            const auto accepted = control->key_down(wparam == VK_SPACE ? ActivationKey::space : ActivationKey::enter,
                (lparam & (1LL << 30)) != 0);
            if (accepted && wparam == VK_SPACE) start_repeat(true);
            services.activate_control(*this, accepted && wparam == VK_RETURN);
            return 0;
        }
        break;
    case WM_CONTEXTMENU:
        show_control_menu(*control, window, lparam, context.palette, context.dpi);
        return 0;
    case WM_CHAR:
        if (wparam == VK_SPACE || wparam == VK_RETURN) return 0;
        break;
    case control_action_message:
        if (const auto result = accessibility_action(lparam)) return result;
        if (!control->invoke()) return UIA_E_INVALIDOPERATION;
        services.activate_control(*this, true);
        return S_OK;
    case foundation_action_message: {
        if (wparam != control->id()) return UIA_E_ELEMENTNOTAVAILABLE;
        FoundationAction action;
        {
            std::lock_guard lock(accessibility->mutex);
            const auto it = accessibility->foundation_actions.find(static_cast<std::uint64_t>(lparam));
            if (it == accessibility->foundation_actions.end()) return UIA_E_ELEMENTNOTAVAILABLE;
            action = std::move(it->second);
            accessibility->foundation_actions.erase(it);
        }
        if (!host.input_enabled(*this) || !host.input_visible(*this)) return UIA_E_ELEMENTNOTENABLED;
        const auto result = foundation_action(action);
        if (SUCCEEDED(result)) host.update_peer_state();
        return result;
    }
    case grid_action_message: return UIA_E_INVALIDOPERATION;
    }
    return {};
}

D2D1_COLOR_F host_input_fill(const HostPeer& peer, const Palette& palette, bool enabled) {
    if (peer.control->role() == ControlRole::numeric_input || peer.control->role() == ControlRole::combo_box) {
        const auto* field = peer.control->effective_control_style_values(StylePart::field);
        if (field && field->background && !palette.high_contrast)
            return D2D1::ColorF(field->background->resolve(palette.mode));
    }
    bool hovered = peer.control->hovered();
    for (const auto& child : peer.control->owned_children())
        if (const auto* control = dynamic_cast<const Control*>(child.get())) hovered = hovered || control->hovered();
    const auto focused = GetFocus() == peer.window || IsChild(peer.window, GetFocus());
    return palette.input_fill(enabled, focused, hovered, peer.surface);
}

D2D1_COLOR_F host_status_fill(StatusSeverity severity, const Palette& palette) {
    if (palette.high_contrast || palette.style == VisualStyle::classic) return palette.surface;
    const auto colors = winui_status_colors(palette.mode);
    switch (severity) {
    case StatusSeverity::success: return D2D1::ColorF(colors.success_fill);
    case StatusSeverity::warning: return D2D1::ColorF(colors.warning_fill);
    case StatusSeverity::error: return D2D1::ColorF(colors.error_fill);
    default: return D2D1::ColorF(colors.information_fill);
    }
}

void paint_host_text(Drawing& canvas, const Palette& palette, const Control& control, StylePart part,
    std::wstring_view text, Rect area, D2D1_COLOR_F fallback) {
    const auto* values = control.effective_control_style_values(part);
    const auto ink = values && values->foreground && !palette.high_contrast ?
        D2D1::ColorF(values->foreground->resolve(palette.mode)) : fallback;
    if (values) canvas.styled_text(text, area, ink, *values, TextStyle::caption);
    else canvas.text(text, area, ink, true);
}

}
