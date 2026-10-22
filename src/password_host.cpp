#include "password_host.hpp"

namespace xui {

PasswordHostPeer::PasswordHostPeer(PeerHostServices& host, NativeFieldServices& fields,
    std::shared_ptr<PasswordInput> password) : NativeFieldHostPeer(host, fields, password), password_(std::move(password)) {
    password_.on_failure([&host] { host.peer_failure(); });
}
PasswordHostPeer::~PasswordHostPeer() {
    retire_native();
    disconnect_accessibility();
}
void PasswordHostPeer::attach(HWND parent, int id) {
    password_.attach(parent, id);
    window = password_.window();
    attach_native_override();
}
void PasswordHostPeer::sync(UINT dpi, const Palette& palette) {
    password_.update(dpi, palette);
    publish_accessibility();
}
void PasswordHostPeer::arrange_native(Rect bounds, UINT dpi, bool shown, const Palette& palette) {
    const auto& password = static_cast<const PasswordInput&>(*control);
    if (password.revealed()) bounds.height = std::max(0.0f, bounds.height - password.reveal_extent());
    arrange_document(bounds, dpi, shown, palette, true);
}
void PasswordHostPeer::paint(const HostPaintContext& context) {
    paint_document_frame(context, true);
    const auto& password = static_cast<const PasswordInput&>(*control);
    if (!password.revealed()) return;
    auto& canvas = context.canvas;
    const auto& palette = context.palette;
    const auto bounds = password.bounds();
    const auto* root = password.effective_control_style_values(StylePart::root);
    const float inset = palette.style == VisualStyle::winui ? 4.0f : 0.0f;
    const Rect preview{bounds.x + inset, bounds.y + std::max(0.0f, bounds.height - password.reveal_extent()),
        std::max(0.0f, bounds.width - 2 * inset), std::min(password.reveal_extent() - inset, bounds.height)};
    canvas.push_clip(preview);
    canvas.fill(preview, root && root->background && !palette.high_contrast ?
        D2D1::ColorF(root->background->resolve(palette.mode)) :
        palette.style == VisualStyle::winui ? palette.field : palette.surface);
    const auto* text = password.effective_control_style_values(StylePart::text);
    const auto ink = text && text->foreground && !palette.high_contrast ?
        D2D1::ColorF(text->foreground->resolve(palette.mode)) : context.enabled ? palette.text : palette.disabled;
    password.with_password([&](std::wstring_view value) {
        const Rect bounds{preview.x + 8, preview.y, std::max(0.0f, preview.width - 16), preview.height};
        if (text) {
            auto native_text = *text;
            if (!native_text.font_family) {
                static const auto family = make_style_font_family("Segoe UI");
                native_text.font_family = family;
            }
            native_text.font_size = native_text.font_size.value_or(14.0f);
            native_text.font_weight = native_text.font_weight.value_or(FW_NORMAL);
            native_text.font_style = native_text.font_style.value_or(StyleFontStyle::normal);
            canvas.private_text(value, bounds, ink, native_text);
        } else canvas.text(value, bounds, ink);
    });
    canvas.pop_clip();
}

}
