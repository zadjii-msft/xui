#include "text_input_host.hpp"
#include "platform.hpp"
#include "window_host.hpp"
#include <windowsx.h>

namespace xui {

TextInputHostPeer::TextInputHostPeer(PeerHostServices& host, NativeFieldServices& fields,
    std::shared_ptr<TextInput> input) : NativeFieldHostPeer(host, fields, std::move(input)) {
    edit_.on_failure([&host] { host.peer_failure(); });
}
TextInputHostPeer::~TextInputHostPeer() {
    edit_.dismiss_suggestions();
    retire_native();
    if (caption_ && IsWindow(caption_)) DestroyWindow(caption_);
    disconnect_accessibility();
    if (caption_accessibility_) disconnect_caption();
    if (caption_provider_) caption_provider_->Release();
    if (clear_button_) {
        clear_button_->set_invalidator({});
        clear_button_->on_click({});
    }
}
void TextInputHostPeer::attach(HWND parent, int id) {
    auto& model = input();
    caption_ = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", model.name().c_str(),
        WS_CHILD | (model.caption_visible() ? WS_VISIBLE : 0) | SS_LEFT | SS_NOPREFIX,
        0, 0, 1, 1, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    win32_require(caption_ != nullptr, "Create text input label");
    caption_text_ = model.name();
    edit_.set_font_family(fields.native_font_family());
    edit_.set_placeholder(model.placeholder());
    edit_.set_insets({model.search_style() ? 40.0f : 12.0f, 10,
        model.shortcut_hint().empty() ? 12.0f : 78.0f, 10});
    edit_.attach(parent, id);
    window = edit_.window();
    attach_native_override();
    caption_accessibility_ = std::make_shared<NativeFieldAccessibility>();
    caption_accessibility_->snapshot.window = caption_;
    caption_accessibility_->snapshot.id = control->id();
    caption_provider_ = create_native_field_provider(caption_accessibility_);
    attach_caption_override(caption_);
}
void TextInputHostPeer::disconnect_caption() {
    disconnect_native_field(caption_accessibility_, caption_provider_);
}
void TextInputHostPeer::theme_native(UINT dpi, const Palette&, HFONT caption_font) {
    edit_.set_font_family(fields.native_font_family());
    edit_.set_dpi(dpi);
    SendMessageW(caption_, WM_SETFONT, reinterpret_cast<WPARAM>(caption_font), TRUE);
    InvalidateRect(window, nullptr, FALSE);
}
D2D1_COLOR_F TextInputHostPeer::ink(const Palette& palette, const NativeFieldEmbedding& embedding) const {
    if (!palette.high_contrast) {
        const auto* text = control->effective_control_style_values(StylePart::text);
        if (text && text->foreground) return D2D1::ColorF(text->foreground->resolve(palette.mode));
        if (embedding.field_style && embedding.field_style->foreground)
            return D2D1::ColorF(embedding.field_style->foreground->resolve(palette.mode));
    }
    return host.input_enabled(*this) ? palette.text : palette.disabled;
}
D2D1_COLOR_F TextInputHostPeer::fill(const Palette& palette, const NativeFieldEmbedding& embedding) const {
    if (!palette.high_contrast) {
        const auto* root = control->effective_control_style_values(StylePart::root);
        if (root && root->background) return D2D1::ColorF(root->background->resolve(palette.mode));
        if (embedding.field_style && embedding.field_style->background)
            return D2D1::ColorF(embedding.field_style->background->resolve(palette.mode));
    }
    return palette.input_fill(embedding.enabled, embedding.focused || (clear_window_ && GetFocus() == clear_window_),
        embedding.hovered, embedding.surface);
}
NativeFieldColors TextInputHostPeer::native_colors(bool caption, const Palette& palette) const {
    if (caption) return NativeFieldHostPeer::native_colors(true, palette);
    const auto embedding = fields.native_embedding(*this);
    return {ink(palette, embedding), fill(palette, embedding),
        control->has_control_styling() || embedding.styled, surface};
}
void TextInputHostPeer::sync(UINT, const Palette& palette) {
    const auto enabled = host.input_enabled(*this);
    if ((IsWindowEnabled(caption_) != FALSE) != enabled) EnableWindow(caption_, enabled);
    auto& model = input();
    edit_.sync_suggestions(model);
    edit_.set_suggestion_colors(platform::native_color(palette.field), platform::native_color(palette.text),
        platform::native_color(palette.secondary));
    SendMessageW(window, EM_SETLIMITTEXT, model.maximum_length(), 0);
    edit_.set_placeholder(model.placeholder());
    const auto* placeholder = model.effective_control_style_values(StylePart::placeholder);
    edit_.set_placeholder_color(platform::native_color(placeholder && placeholder->foreground && !palette.high_contrast ?
        D2D1::ColorF(placeholder->foreground->resolve(palette.mode)) : enabled ? palette.secondary : palette.disabled));
    const auto embedding = fields.native_embedding(*this);
    edit_.set_colors(platform::native_color(ink(palette, embedding)), platform::native_color(fill(palette, embedding)));
    const auto* text = model.effective_control_style_values(StylePart::text);
    const PartStyleValues defaults;
    const auto font = Drawing::font_descriptor(text ? *text : defaults, fields.native_font_family(), 14.0f);
    edit_.set_font(font.family_name(), font.size, static_cast<int>(font.weight), font.style == StyleFontStyle::italic);
    const auto* header = model.effective_control_style_values(StylePart::header);
    edit_.set_caption_font(caption_, header, fields.native_caption_font(), fields.native_font_family(),
        palette.style == VisualStyle::winui ? VisualMetrics::body_size : VisualMetrics::caption_size);
    edit_.set_caption_color(caption_, platform::native_color(header && header->foreground && !palette.high_contrast ?
        D2D1::ColorF(header->foreground->resolve(palette.mode)) : enabled ? palette.text : palette.disabled));
    if (!edit_.composing() && edit_.text() != model.text()) edit_.set_model_text(model.text());
    if (caption_text_ != model.name()) {
        SetWindowTextW(caption_, model.name().c_str());
        caption_text_ = model.name();
    }
    if (provider) publish_accessibility();
}
void TextInputHostPeer::native_change() {
    if (edit_.composing()) return;
    const auto model = std::static_pointer_cast<TextInput>(control);
    model->commit_text(edit_.text());
    if (host.owner_open() && window && IsWindow(window)) edit_.text_changed();
}
void TextInputHostPeer::cancel_input() {
    edit_.dismiss_suggestions();
    control->cancel();
}
bool TextInputHostPeer::can_clear() const {
    const auto& model = input();
    return model.visual_style() == VisualStyle::winui && !edit_.composing() && !model.search_style() &&
        !model.shortcut_visible(model.bounds().width) && (!parent ||
            (parent->control->role() != ControlRole::numeric_input && parent->control->role() != ControlRole::combo_box));
}
void TextInputHostPeer::synchronize_clear() {
    if (!clear_button_) return;
    const auto& model = input();
    const bool focused = GetFocus() == window || (clear_window_ && GetFocus() == clear_window_);
    const bool show = can_clear() && host.input_enabled(*this) && model.visible() && focused &&
        !model.text().empty() && model.bounds().width >= 5 * VisualMetrics::body_size;
    clear_button_->set_visible(show);
    clear_button_->set_enabled(show);
    const auto bounds = model.bounds();
    const float header = model.caption_extent();
    clear_button_->arrange({bounds.x + std::max(0.0f, bounds.width - 31), bounds.y + header + 1,
        std::min(30.0f, bounds.width), std::max(0.0f, bounds.height - header - 2)});
}
void TextInputHostPeer::collect_clear() {
    if (!clear_button_ && can_clear() && GetFocus() == window && !input().text().empty()) {
        clear_button_ = std::make_shared<Button>(L"Clear " + control->name());
        clear_button_->set_icon(ButtonIcon::close);
        clear_button_->set_appearance(ButtonAppearance::subtle);
        clear_button_->set_tab_stop(false);
        clear_button_->set_automation_id(L"xui-text-clear-" + (control->automation_id().empty() ?
            std::to_wstring(control->id()) : control->automation_id()));
        const std::weak_ptr<NativeFieldServices> owner = fields.acquire_native_lease();
        clear_button_->set_invalidator([owner](Invalidation kind) {
            if (const auto services = owner.lock()) services->invalidate_native_field(kind);
        });
        clear_button_->on_click([owner, id = control->id()] {
            if (const auto services = owner.lock()) services->activate_text_clear(id);
        });
    }
    if (!clear_button_) return;
    synchronize_clear();
    fields.collect_text_clear(*this);
}
void TextInputHostPeer::clear_text() {
    if (!can_clear() || !host.input_enabled(*this) || !host.input_visible(*this)) return;
    const auto edit = window;
    edit_.focus(false);
    if (!host.owner_open() || !IsWindow(edit) || GetFocus() != edit) return;
    SendMessageW(edit, EM_SETSEL, 0, -1);
    SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
}
void TextInputHostPeer::arrange_native(Rect bounds, UINT dpi, bool shown, const Palette& palette) {
    const auto& model = input();
    const bool winui = palette.style == VisualStyle::winui;
    const auto embedding = fields.native_embedding(*this);
    const auto* root = model.effective_control_style_values(StylePart::root);
    auto insets = model.field_insets({model.search_style() ? 40.0f : winui && !embedding.combo ? 11.0f : 12.0f,
        winui ? 6.0f : 10.0f, model.shortcut_visible(bounds.width) ? model.shortcut_size().width + 28.0f :
            clear_button_ && clear_button_->visible() ? 37.0f :
            winui ? embedding.reserved_end ? 1.0f : 7.0f : 12.0f, winui ? 7.0f : 10.0f});
    if (root && root->padding) {
        if (model.search_style()) insets.left += 28;
        if (model.shortcut_visible(bounds.width)) insets.right += model.shortcut_size().width + 16;
        else if (clear_button_ && clear_button_->visible()) insets.right += 30;
    }
    edit_.set_insets(insets);
    const bool caption_visible = model.caption_visible() && shown;
    if (((GetWindowLongPtrW(caption_, GWL_STYLE) & WS_VISIBLE) != 0) != caption_visible)
        ShowWindow(caption_, caption_visible ? SW_SHOWNA : SW_HIDE);
    if (model.caption_visible()) {
        platform::place(caption_, {bounds.x, bounds.y, bounds.width, std::min(model.caption_height(), bounds.height)}, dpi);
        bounds.y += model.caption_extent();
        bounds.height = std::max(0.0f, bounds.height - model.caption_extent());
    }
    edit_.arrange(bounds);
}
void TextInputHostPeer::paint(const HostPaintContext& context) {
    auto& canvas = context.canvas;
    const auto& palette = context.palette;
    const auto& model = input();
    auto bounds = model.bounds();
    if (model.caption_visible()) {
        const auto* header = model.effective_control_style_values(StylePart::header);
        auto typography = header ? *header : PartStyleValues{};
        typography.horizontal_alignment = StyleAlignment::start;
        typography.vertical_alignment = StyleAlignment::start;
        typography.wrapping = true;
        const auto ink = header && header->foreground && !palette.high_contrast ?
            D2D1::ColorF(header->foreground->resolve(palette.mode)) : context.enabled ? palette.text : palette.disabled;
        canvas.styled_text(model.name(), {bounds.x, bounds.y, bounds.width, std::min(model.caption_height(), bounds.height)},
            ink, typography, palette.style == VisualStyle::winui ? TextStyle::body : TextStyle::caption);
        bounds.y += model.caption_extent();
        bounds.height = std::max(0.0f, bounds.height - model.caption_extent());
    }
    canvas.push_clip(bounds);
    const auto embedding = fields.native_embedding(*this);
    const bool focused = GetFocus() == window || (clear_window_ && GetFocus() == clear_window_);
    const bool composite = embedding.composite && (palette.style == VisualStyle::winui || embedding.styled);
    const auto* root = model.effective_control_style_values(StylePart::root);
    if (!composite || root) {
        const Rect frame = palette.style == VisualStyle::winui ? Rect{bounds.x + 0.5f, bounds.y + 0.5f,
            std::max(0.0f, bounds.width - 1), std::max(0.0f, bounds.height - 1)} : bounds;
        if (root) {
            canvas.styled_surface(frame, palette, *root, fill(palette, embedding), palette.border, 4, {1, 1, 1, 1});
            if (focused) canvas.styled_field_focus(frame, palette, *root);
        } else canvas.field_frame(frame, palette, focused, context.enabled, embedding.invalid, fill(palette, embedding));
    }
    const auto ink = [&](StylePart part, D2D1_COLOR_F fallback) {
        const auto* values = model.effective_control_style_values(part);
        return values && values->foreground && !palette.high_contrast ?
            D2D1::ColorF(values->foreground->resolve(palette.mode)) : context.enabled ? fallback : palette.disabled;
    };
    const auto padding = root && root->padding ? *root->padding : Insets{12, 0, 12, 0};
    const auto border = root && root->border_thickness ? *root->border_thickness : Insets{};
    if (model.search_style()) canvas.search_icon({bounds.x + padding.left + border.left,
        bounds.y + (bounds.height - 18) / 2, 18, 18}, ink(StylePart::icon, palette.secondary));
    if (model.shortcut_visible(bounds.width)) {
        const auto size = model.shortcut_size();
        const Rect badge{bounds.x + std::max(0.0f, bounds.width - size.width - padding.right - border.right),
            bounds.y + (bounds.height - size.height) / 2, size.width, size.height};
        const auto* shortcut = model.effective_control_style_values(StylePart::shortcut);
        if (shortcut) canvas.styled_surface(badge, palette, *shortcut, fill(palette, embedding), palette.border, 4, {1, 1, 1, 1});
        else canvas.rounded(badge, palette.border, 4, true);
        const Rect text{badge.x + 6, badge.y, std::max(0.0f, badge.width - 8), badge.height};
        if (shortcut) canvas.styled_text(model.shortcut_hint(), text, ink(StylePart::shortcut, palette.secondary),
            *shortcut, TextStyle::caption);
        else canvas.text(model.shortcut_hint(), text, palette.secondary, true);
    }
    canvas.pop_clip();
}

TextClearHostPeer::TextClearHostPeer(PeerHostServices& host, std::shared_ptr<Button> button,
    std::weak_ptr<TextInput> owner) : HostPeer(host, HostFamily::text_clear, std::move(button)), owner_(std::move(owner)) {}
TextClearHostPeer::~TextClearHostPeer() {
    retire_native();
    disconnect_accessibility();
}
void TextClearHostPeer::attach(HWND parent, int id) {
    window = host.create_input_surface(*this, parent, id);
    publish_accessibility();
    provider = create_text_clear_provider(accessibility_);
}
void TextClearHostPeer::publish_accessibility() {
    publish_text_clear(accessibility_, provider, *control, window);
}
void TextClearHostPeer::disconnect_accessibility() {
    disconnect_native_field(accessibility_, provider);
}
void TextClearHostPeer::activated(bool invoked) {
    host.update_peer_state();
    if (invoked && host.owner_open()) raise_text_clear_invoked(provider);
}
std::optional<LRESULT> TextClearHostPeer::input(UINT message, WPARAM wp, LPARAM lp, const HostInputContext&) {
    switch (message) {
    case WM_MOUSEMOVE:
        track_pointer_leave();
        control->pointer_move(row_pointer_inside(window, lp));
        return 0;
    case WM_MOUSELEAVE:
        tracking = false;
        control->pointer_move(false);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (!host.input_enabled(*this) || !host.input_visible(*this)) return 0;
        control->pointer_move(row_pointer_inside(window, lp));
        if (control->pointer_down()) SetCapture(window);
        return 0;
    case WM_LBUTTONUP:
        if (std::exchange(suppress_popup_click, false)) return 0;
        activated(control->pointer_up(row_pointer_inside(window, lp)));
        if (GetCapture() == window) ReleaseCapture();
        return 0;
    case WM_CANCELMODE:
        control->cancel();
        if (GetCapture() == window) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        control->cancel();
        host.invalidate_peer_paint();
        return 0;
    case control_action_message:
        if (wp != control->id()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (lp != 0) return {};
        if (!host.input_enabled(*this) || !host.input_visible(*this)) return UIA_E_ELEMENTNOTENABLED;
        activated(control->invoke());
        return S_OK;
    }
    return {};
}
void TextClearHostPeer::paint(const HostPaintContext& context) {
    const auto owner = owner_.lock();
    const auto& button = static_cast<const Button&>(*control);
    auto& canvas = context.canvas;
    const auto& palette = context.palette;
    const bool winui = palette.style == VisualStyle::winui;
    const Rect bounds{0, 0, paint_bounds.width, paint_bounds.height};
    if (owner && (owner->has_control_styling() || control->has_control_styling())) {
        const auto state = (control->focused() ? style_states::focused : 0) |
            (control->hovered() ? style_states::hovered : 0) | (control->pressed() ? style_states::pressed : 0) |
            (!context.enabled ? style_states::disabled : 0) | (owner->text().empty() ? style_states::empty : 0);
        const auto defaults = owner->resolve_control_style_part(StylePart::clear_action, state);
        if (!defaults.empty() || control->has_control_styling()) {
            canvas.styled_button(button, bounds, palette, context.enabled, context.focus_visible, {}, 0, {}, &defaults,
                winui ? std::optional{Symbol::clear} : std::nullopt);
            return;
        }
    }
    const float inset = winui ? 0.5f : 2.0f;
    const Rect box{inset, inset, std::max(0.0f, bounds.width - 2 * inset), std::max(0.0f, bounds.height - 2 * inset)};
    auto ink = control->pressed() ? palette.selection_text : context.enabled ? palette.text : palette.disabled;
    if (winui) ink = canvas.button_face(box, palette, button.appearance(), context.enabled,
        control->hovered(), control->pressed(), false);
    else {
        canvas.rounded(box, control->pressed() ? palette.selection : control->hovered() ? palette.hover : palette.surface);
        canvas.rounded(box, palette.border, 6, true);
    }
    if (winui) canvas.symbol(Symbol::clear, bounds, ink, 12);
    else canvas.button_icon({(bounds.width - 16) / 2, (bounds.height - 16) / 2, 16, 16}, ink, ButtonIcon::close);
    if (context.focus_visible) {
        if (winui) canvas.focus_ring(box, palette);
        else canvas.rounded(box, palette.high_contrast && control->pressed() ? palette.selection_text : palette.accent, 6, true);
    }
}

}
