#include "native_field_host.hpp"
#include "platform.hpp"
#include "window_host.hpp"
#include <cmath>

namespace xui {

NativeFieldHostPeer::NativeFieldHostPeer(PeerHostServices& host, NativeFieldServices& services,
    std::shared_ptr<Control> control) : HostPeer(host, HostFamily::native_field, std::move(control)), fields(services) {
    if (!this->control) throw std::invalid_argument("A native field requires a control");
}
void NativeFieldHostPeer::publish_accessibility() {
    publish_native_field(accessibility, *control, window);
}
void NativeFieldHostPeer::disconnect_accessibility() {
    disconnect_native_field(accessibility, provider);
}
void NativeFieldHostPeer::attach_native_override() {
    publish_accessibility();
    provider = create_native_field_provider(accessibility);
    win32_require(SetWindowSubclass(window, subclass, 2, reinterpret_cast<DWORD_PTR>(this)) != FALSE,
        "Attach native field focus and clipping provider");
}
void NativeFieldHostPeer::attach_caption_override(HWND caption) {
    win32_require(SetWindowSubclass(caption, subclass, 2, reinterpret_cast<DWORD_PTR>(this)) != FALSE,
        "Attach native caption clipping provider");
}
LRESULT CALLBACK NativeFieldHostPeer::subclass(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR, DWORD_PTR data) noexcept {
    auto& peer = *reinterpret_cast<NativeFieldHostPeer*>(data);
    return peer.fields.dispatch_native_field(peer, hwnd, message, wp, lp);
}
LRESULT NativeFieldHostPeer::native_message(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR id, bool composing_native) {
    const bool caption = hwnd == caption_window();
    if (caption && message == WM_ERASEBKGND) return 1;
    if (caption && message == WM_PAINT) { ValidateRect(hwnd, nullptr); return 0; }
    if (caption && (message == WM_PRINTCLIENT || message == WM_PRINT)) return 0;
    auto* native_provider = caption ? caption_provider() : provider;
    if ((message == WM_PRINTCLIENT || message == WM_PRINT) && native_occluded && !composing_native) {
        const auto dc = reinterpret_cast<HDC>(wp);
        const auto region = CreateRectRgn(0, 0, 0, 0);
        const auto saved = SaveDC(dc);
        if (!region || !saved) {
            if (region) DeleteObject(region);
            if (saved) RestoreDC(dc, saved);
            return 0;
        }
        if (GetWindowRgn(hwnd, region) != ERROR) {
            POINT viewport{}, origin{};
            GetViewportOrgEx(dc, &viewport); GetWindowOrgEx(dc, &origin);
            OffsetRgn(region, viewport.x - origin.x, viewport.y - origin.y);
            ExtSelectClipRgn(dc, region, RGN_AND);
        }
        const auto result = DefSubclassProc(hwnd, message, wp, lp);
        RestoreDC(dc, saved); DeleteObject(region);
        return result;
    }
    if (message == WM_GETOBJECT && static_cast<LONG>(lp) == UiaRootObjectId &&
        control->role() == ControlRole::document_text) return DefSubclassProc(hwnd, message, wp, lp);
    if (message == WM_GETOBJECT && static_cast<LONG>(lp) == UiaRootObjectId && native_provider)
        return UiaReturnRawElementProvider(hwnd, wp, lp, native_provider);
    if (message == control_action_message) {
        if (wp != control->id()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (caption) return UIA_E_INVALIDOPERATION;
        if (!host.input_enabled(*this)) return UIA_E_ELEMENTNOTENABLED;
        return host.focus_peer(*this, true) ? S_OK : UIA_E_INVALIDOPERATION;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, subclass, id);
        if (caption) disconnect_caption();
        else disconnect_accessibility();
        UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);
    }
    return DefSubclassProc(hwnd, message, wp, lp);
}
void NativeFieldHostPeer::theme_native(UINT dpi, const Palette& palette, HFONT) {
    sync(dpi, palette);
}
void NativeFieldHostPeer::arrange_document(Rect bounds, UINT dpi, bool shown, const Palette& palette, bool inset) {
    const auto* style = control->effective_control_style_values(StylePart::root);
    if (style) {
        const auto padding = style->padding.value_or(Insets{4, 3, 4, 3});
        const auto border = style->border_thickness.value_or(Insets{});
        bounds.x += padding.left + border.left; bounds.y += padding.top + border.top;
        bounds.width = std::max(0.0f, bounds.width - padding.left - padding.right - border.left - border.right);
        bounds.height = std::max(0.0f, bounds.height - padding.top - padding.bottom - border.top - border.bottom);
    } else if (inset && palette.style == VisualStyle::winui) {
        bounds.x += 4; bounds.y += 3;
        bounds.width = std::max(0.0f, bounds.width - 8);
        bounds.height = std::max(0.0f, bounds.height - 6);
    }
    if (!shown) return;
    const float scale = dpi / 96.0f;
    const RECT target{static_cast<LONG>(std::lround(bounds.x * scale)), static_cast<LONG>(std::lround(bounds.y * scale)),
        static_cast<LONG>(std::lround(bounds.width * scale)), static_cast<LONG>(std::lround(bounds.height * scale))};
    if (!placed || !EqualRect(&target, &placed_bounds)) {
        platform::place(window, bounds, dpi);
        placed_bounds = target;
        placed = true;
    }
}
void NativeFieldHostPeer::paint_document_frame(const HostPaintContext& context, bool inset) {
    auto& canvas = context.canvas;
    const auto& palette = context.palette;
    const auto bounds = control->bounds();
    if (const auto* style = control->effective_control_style_values(StylePart::root)) {
        canvas.styled_surface(bounds, palette, *style, palette.field, palette.border, 4, {1, 1, 1, 1});
        if (control->focused()) canvas.styled_field_focus(bounds, palette, *style);
    } else if (inset && palette.style == VisualStyle::winui) {
        canvas.field_frame({bounds.x + 1, bounds.y + 1, std::max(0.0f, bounds.width - 2),
            std::max(0.0f, bounds.height - 2)}, palette, control->focused(), context.enabled);
    }
}
NativeFieldColors NativeFieldHostPeer::native_colors(bool caption, const Palette& palette) const {
    auto ink = IsWindowEnabled(caption ? caption_window() : window) ? palette.text : palette.disabled;
    const auto* text = control->effective_control_style_values(caption ? StylePart::header : StylePart::text);
    if (text && text->foreground && !palette.high_contrast) ink = D2D1::ColorF(text->foreground->resolve(palette.mode));
    auto fill = palette.field;
    if (!caption) {
        const auto* root = control->effective_control_style_values(StylePart::root);
        if (root && root->background && !palette.high_contrast) fill = D2D1::ColorF(root->background->resolve(palette.mode));
    }
    return {ink, fill, !caption && control->has_control_styling(), surface};
}

}
