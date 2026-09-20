#include "xui/controls.hpp"
#include "layout_styling.hpp"
#include <algorithm>
#include <cmath>

namespace xui {
namespace {
Insets default_button_padding(VisualStyle style) {
    return style == VisualStyle::winui ? Insets{11, 5, 11, 6} :
        Insets{style_metrics(style).button_padding, 6, style_metrics(style).button_padding, 6};
}
}
void PageView::add_page(std::shared_ptr<Element> content) { add(std::make_shared<ContentView>(std::move(content), L"Page")); }
void PageView::select(std::size_t index) {
    if (index >= child_count()) throw std::out_of_range("Page index");
    if (selected_ == index) return;
    selected_ = index; invalidate(Invalidation::layout);
}
Size PageView::measure(Size available) {
    if (!auto_size()) return Element::measure(available);
    const auto p = effective_layout_insets();
    return constrain(layout_style::outer(child_count() ? child_at(selected_)->measure(layout_style::inner(available, p)) : Size{}, p), available);
}
void PageView::arrange(Rect rectangle) {
    Element::arrange(rectangle);
    rectangle = layout_content_bounds();
    for (std::size_t i = 0; i < child_count(); ++i) {
        const auto& child = child_at(i);
        if (i == selected_) {
            const auto* values = effective_control_style_values(StylePart::root);
            child->arrange(values ? layout_style::aligned(rectangle, child->measure({rectangle.width, rectangle.height}), values) : rectangle);
        }
        else if (child->bounds().width != 0 || child->bounds().height != 0)
            child->arrange({rectangle.x, rectangle.y, 0, 0});
    }
}

Control::Control(ControlRole role, std::wstring name, Size preferred)
    : role_(role), name_(std::move(name)) { set_default_size(preferred); }

void Control::set_visual_style(VisualStyle style) {
    if (style < VisualStyle::classic || style > VisualStyle::winui) throw std::invalid_argument("Invalid visual style");
    if (visual_style_ == style) return;
    visual_style_ = style;
    text_dirty_ = true;
    presentation_changed();
    invalidate(Invalidation::layout);
}

void Control::set_presentation_font_size(float value) {
    if (!std::isfinite(value) || (value != 0 && (value < 8 || value > 32)))
        throw std::invalid_argument("Invalid presentation font size");
    if (presentation_font_size_ == value) return;
    presentation_font_size_ = value;
    invalidate(Invalidation::layout);
}

void Control::set_name(std::wstring name) {
    if (name_ == name) return;
    name_ = std::move(name);
    text_changed();
}
void Control::set_help_text(std::wstring value) {
    if (help_text_ == value) return;
    help_text_ = std::move(value);
    invalidate(Invalidation::paint);
}
void Control::set_tooltip_delay(unsigned value) {
    if (value < 100 || value > 60000) throw std::invalid_argument("Tooltip delay must be 100 to 60000 milliseconds");
    if (tooltip_delay_ == value) return;
    tooltip_delay_ = value;
    invalidate(Invalidation::paint);
}
bool Control::actionable() const {
    return role_ == ControlRole::button || role_ == ControlRole::toggle ||
        role_ == ControlRole::expander || role_ == ControlRole::combo_box;
}
void Control::text_changed() {
    text_dirty_ = true;
    invalidate(auto_size() ? Invalidation::layout : Invalidation::paint);
}
void Control::control_style_changed(Invalidation kind) {
    if (kind == Invalidation::layout) {
        text_dirty_ = true;
        presentation_changed();
    }
    invalidate(kind);
}
void Control::set_text_measurer(TextMeasurer measurer) {
    measurer_ = std::move(measurer);
    text_dirty_ = true;
    if (measurer_) invalidate(auto_size() ? Invalidation::layout : Invalidation::paint);
}
Size Control::measured_text() {
    if (text_dirty_) {
        // Headless callers can inject a measurer. No guessed glyph widths in the core.
        text_size_ = measurer_ ? measurer_(name_, text_style()) : Size{};
        text_dirty_ = false;
    }
    return text_size_;
}
Size Control::measure(Size available) {
    if (!visible_) return {};
    const auto metrics = style_metrics(visual_style_);
    if (!auto_size() || !measurer_) {
        auto desired = Element::measure(available);
        if (visual_style_ == VisualStyle::winui && !preferred_size_explicit() &&
            (role_ == ControlRole::button || role_ == ControlRole::toggle ||
                role_ == ControlRole::combo_box || role_ == ControlRole::numeric_input))
            desired.height = metrics.button_height;
        return constrain(desired, available);
    }
    const auto text = measured_text();
    const bool winui = visual_style_ == VisualStyle::winui;
    float inset = role_ == ControlRole::toggle ? (winui ? 28.0f : 54.0f) : role_ == ControlRole::button ? 2 * metrics.button_padding : 0;
    if (winui && role_ == ControlRole::button && static_cast<const Button*>(this)->behavior() == ButtonBehavior::dropdown)
        inset += 20;
    const float height = role_ == ControlRole::button ? metrics.button_height : role_ == ControlRole::toggle ? 32.0f :
        winui && role_ == ControlRole::label ? 0.0f : 24.0f;
    const float vertical_chrome = winui && role_ == ControlRole::button ? 13.0f : inset ? 12.0f : 0.0f;
    return constrain({text.width + inset, std::max(height, text.height + vertical_chrome)}, available);
}

Size TextInput::measure(Size available) {
    if (!visible()) return {};
    const auto* root = effective_control_style_values(StylePart::root);
    const auto* text = effective_control_style_values(StylePart::text);
    const auto* header = effective_control_style_values(StylePart::header);
    const bool styled_metrics = (root && (root->padding || root->border_thickness)) ||
        (text && (text->font_family || text->font_size || text->font_weight || text->font_style)) ||
        (header && (header->font_family || header->font_size || header->font_weight || header->font_style));
    if (styled_metrics && !preferred_size_explicit()) {
        const auto insets = field_insets(visual_style() == VisualStyle::winui ?
            Insets{11, 6, 7, 7} : Insets{12, 10, 12, 10});
        const float line = text && text->font_size ? *text->font_size * 1.5f : 21.0f;
        return constrain({std::max(320.0f, insets.left + insets.right),
            std::max(style_metrics(visual_style()).field_height, line + insets.top + insets.bottom) + caption_extent()}, available);
    }
    if (visual_style() != VisualStyle::winui || preferred_size_explicit()) return Element::measure(available);
    return constrain({320, style_metrics(visual_style()).field_height + caption_extent()}, available);
}
float TextInput::caption_height() const {
    const auto* header = effective_control_style_values(StylePart::header);
    return header && header->font_size ? std::max(style_metrics(visual_style()).input_header_height, *header->font_size * 1.5f) :
        style_metrics(visual_style()).input_header_height;
}
float TextInput::caption_extent() const {
    return caption_visible() ? caption_height() + style_metrics(visual_style()).input_header_spacing : 0;
}
Insets TextInput::field_insets(Insets fallback) const {
    const auto* root = effective_control_style_values(StylePart::root);
    if (!root) return fallback;
    const auto padding = root->padding.value_or(fallback);
    const auto border = root->border_thickness.value_or(Insets{});
    return {padding.left + border.left, padding.top + border.top,
        padding.right + border.right, padding.bottom + border.bottom};
}
Size TextInput::shortcut_size() const {
    const auto* shortcut = effective_control_style_values(StylePart::shortcut);
    if (!shortcut || !shortcut->font_size) return {50, 20};
    return {std::max(50.0f, static_cast<float>(shortcut_.size()) * *shortcut->font_size + 8),
        std::max(20.0f, *shortcut->font_size * 1.5f)};
}
void TextInput::set_shortcut_hint(std::wstring value) {
    if (shortcut_ == value) return;
    const auto* shortcut = effective_control_style_values(StylePart::shortcut);
    const bool styled_metrics = shortcut && shortcut->font_size;
    shortcut_ = std::move(value);
    invalidate(styled_metrics ? Invalidation::layout : Invalidation::paint);
}

void Label::set_wrapping(bool value, std::size_t maximum_lines) {
    if (wrapping_explicit_ && wrapping_ == value && maximum_lines_ == maximum_lines) return;
    wrapping_explicit_ = true;
    wrapping_ = value;
    maximum_lines_ = maximum_lines;
    wrapped_valid_ = false;
    text_changed();
    invalidate(Invalidation::layout);
}
void Label::set_wrapped_text_measurer(WrappedTextMeasurer measurer) {
    wrapped_measurer_ = std::move(measurer);
    wrapped_valid_ = false;
    invalidate(Invalidation::layout);
}
bool Label::wrapping() const {
    if (wrapping_explicit_ || !has_control_styling()) return wrapping_;
    const auto* values = effective_control_style_values(text_part());
    return values && values->wrapping ? *values->wrapping : wrapping_;
}
std::size_t Label::maximum_lines() const {
    if (wrapping_explicit_ || !has_control_styling()) return maximum_lines_;
    const auto* values = effective_control_style_values(text_part());
    return values && values->maximum_lines ? *values->maximum_lines : maximum_lines_;
}
Size Label::wrapped_text(float width) {
    if (!wrapped_measurer_ || !wrapping()) return measured_text();
    width = std::isnan(width) ? 1.0f : std::clamp(width, 1.0f, 10000000.0f);
    if (!wrapped_valid_ || wrapped_width_ != width || wrapped_name_ != name() || wrapped_style_ != text_style() ||
        wrapped_lines_ != maximum_lines()) {
        wrapped_size_ = wrapped_measurer_(name(), text_style(), width, maximum_lines());
        wrapped_name_ = name();
        wrapped_style_ = text_style();
        wrapped_width_ = width;
        wrapped_lines_ = maximum_lines();
        wrapped_valid_ = true;
    }
    return wrapped_size_;
}
Size Label::measure(Size available) {
    if (!visible()) return {};
    if (!auto_size()) return Control::measure(available);
    if (!has_control_styling()) {
        if (!wrapping() || !wrapped_measurer_) return Control::measure(available);
        return constrain(wrapped_text(available.width), available);
    }
    const auto inner = content_bounds({0, 0, available.width, available.height});
    const auto* root = effective_control_style_values(StylePart::root);
    const auto* typography = effective_control_style_values(text_part());
    if ((!root || part_style_layout_equal(*root, {})) &&
        (!typography || part_style_layout_equal(*typography, {})) && !wrapping())
        return Control::measure(available);
    const auto padding = root && root->padding ? *root->padding : Insets{};
    const auto border = root && root->border_thickness ? *root->border_thickness : Insets{};
    const auto text = wrapping() && wrapped_measurer_ ? wrapped_text(inner.width) : measured_text();
    return constrain({text.width + padding.left + padding.right + border.left + border.right,
        text.height + padding.top + padding.bottom + border.top + border.bottom}, available);
}

ScrollView::ScrollView(std::shared_ptr<Element> content, std::wstring name)
    : Control(ControlRole::scroll_view, std::move(name), {320, 240}), content_(std::move(content)) {
    adopt(content_);
}
Size ScrollView::measure(Size available) {
    if (!visible()) return {};
    if (passthrough_) {
        const auto p = layout_style::insets(effective_control_style_values(StylePart::root));
        return constrain(layout_style::outer(content_->measure(layout_style::inner(available, p)), p), available);
    }
    return Element::measure(available);
}
StyleStateMask ScrollView::control_style_state_bits() const {
    return Control::control_style_state_bits() | (style_dragging_ ? style_states::dragging : 0) |
        (!passthrough_ && style_scrollable_ ? style_states::scrollable : 0);
}
void ScrollView::set_style_dragging(bool value) {
    if (style_dragging_ == value) return;
    style_dragging_ = value;
    if (!invalidate_control_style_state()) invalidate(Invalidation::paint);
}
float ScrollView::effective_bar_width() const {
    const auto* values = effective_control_style_values(StylePart::scrollbar_track);
    return values ? values->width.value_or(bar_width) : bar_width;
}
Rect ScrollView::scrollbar_track() const {
    auto result = layout_style::content(*this, bounds());
    const auto width = std::min(result.width, effective_bar_width());
    result.x += result.width - width; result.width = width;
    return passthrough_ ? Rect{} : result;
}
Rect ScrollView::scrollbar_thumb_track() const {
    return layout_style::inset(scrollbar_track(),
        layout_style::insets(effective_control_style_values(StylePart::scrollbar_track), {2, 0, 2, 0}));
}
Rect ScrollView::viewport() const {
    auto result = layout_style::content(*this, bounds());
    if (!passthrough_ && !overlay_scrollbar_) result.width = std::max(0.0f, result.width - effective_bar_width());
    return result;
}
float ScrollView::maximum_offset() const { return passthrough_ ? 0 : std::max(0.0f, extent_ - viewport().height); }
void ScrollView::arrange(Rect rectangle) {
    Element::arrange(rectangle);
    if (passthrough_) {
        const auto view = viewport();
        extent_ = view.height;
        content_->arrange(view);
        return;
    }
    const auto view = viewport();
    const auto desired = content_->measure({view.width, std::numeric_limits<float>::infinity()});
    extent_ = std::max(view.height, desired.height);
    if (style_scrollable_ != (extent_ > view.height)) {
        style_scrollable_ = extent_ > view.height;
        invalidate_control_style_state();
    }
    offset_ = std::clamp(offset_, 0.0f, maximum_offset());
    content_->arrange({view.x, view.y - offset_, view.width, extent_});
}
void ScrollView::set_offset(float value) {
    if (passthrough_) return;
    value = std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, maximum_offset());
    if (offset_ == value) return;
    offset_ = value;
    invalidate(Invalidation::scroll);
}
void ScrollView::reveal(Rect target) {
    const auto view = viewport();
    if (target.y < view.y) scroll_by(target.y - view.y);
    else if (target.y + target.height > view.y + view.height)
        scroll_by(std::min(target.y - view.y, target.y + target.height - view.y - view.height));
}
Rect ScrollView::thumb() const {
    const auto view = scrollbar_thumb_track();
    if (maximum_offset() <= 0 || view.height <= 0) return {};
    const float height = std::min(view.height, std::max(24.0f, view.height * viewport().height / extent_));
    return {view.x, view.y + (view.height - height) * offset_ / maximum_offset(), view.width, height};
}
void Control::set_enabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    if (!enabled) { cancel(); hovered_ = false; focused_ = false; }
    invalidate_state();
}
void Control::set_focused(bool focused) {
    focused = focused && focusable();
    if (focused_ == focused) return;
    focused_ = focused;
    if (!focused) cancel();
    invalidate_state();
    if (focused && focus_) { auto callback = focus_; callback(); }
}
void Control::pointer_move(bool inside) {
    inside = inside && enabled_;
    if (hovered_ == inside) return;
    hovered_ = inside;
    invalidate_state();
}
bool Control::pointer_down() {
    if (!enabled_ || !hovered_ || !actionable())
        return false;
    pointer_ = true;
    keyboard_ = false;
    invalidate_state();
    return true;
}
bool Control::pointer_up(bool inside) {
    const bool fire = pointer_ && inside;
    cancel();
    return fire && invoke();
}
void Control::cancel() {
    if (!pointer_ && !keyboard_) return;
    pointer_ = keyboard_ = false;
    invalidate_state();
}
bool Control::key_down(ActivationKey key, bool repeat) {
    if (!enabled_ || !focused_ || repeat || pointer_) return false;
    if (key == ActivationKey::enter) return role_ != ControlRole::toggle && invoke();
    if (!actionable()) return false;
    keyboard_ = true;
    invalidate_state();
    return true;
}
bool Control::key_up(ActivationKey key) {
    const bool fire = key == ActivationKey::space && keyboard_ && focused_;
    cancel();
    return fire && invoke();
}
bool Control::invoke() {
    if (!enabled_ || !actionable()) return false;
    activate();
    return true;
}
void Button::activate() {
    const auto callback = click_;
    const auto toggle = toggle_;
    const bool checked = !checked_;
    if (behavior_ == ButtonBehavior::toggle) {
        set_checked(checked);
        if (toggle) toggle(checked);
        return;
    }
    if (callback) callback();
}
void Button::set_behavior(ButtonBehavior value) {
    if (value < ButtonBehavior::momentary || value > ButtonBehavior::dropdown)
        throw std::invalid_argument("Invalid button behavior");
    if (behavior_ == value) return;
    cancel(); behavior_ = value;
    invalidate(visual_style() == VisualStyle::winui && auto_size() ? Invalidation::layout : Invalidation::paint);
}
void Button::set_appearance(ButtonAppearance value) {
    if (value < ButtonAppearance::standard || value > ButtonAppearance::subtle)
        throw std::invalid_argument("Invalid button appearance");
    if (appearance_ == value) return;
    appearance_ = value;
    invalidate(Invalidation::paint);
}
void Button::set_checked(bool value) {
    if (checked_ == value) return;
    checked_ = value;
    invalidate_state();
}
void Control::invalidate_state() {
    auto kind = Invalidation::paint;
    if (role_ == ControlRole::button) {
        auto& button = static_cast<Button&>(*this);
        if (button.style_data_) kind = button.style_state_changed();
    }
    if (!invalidate_control_style_state() || kind == Invalidation::layout) invalidate(kind);
}
StyleStateMask Control::control_style_state_bits() const {
    return (focused() ? style_states::focused : 0) | (hovered() ? style_states::hovered : 0) |
        (pressed() ? style_states::pressed : 0) |
        (!enabled() ? style_states::disabled : 0) | Element::control_style_state_bits();
}
unsigned Button::style_state_mask() const {
    return unsigned(focused()) | (unsigned(checked()) << 1) | (unsigned(hovered()) << 2) |
        (unsigned(pressed()) << 3) | (unsigned(!enabled() || (style_data_ && !style_data_->context_enabled)) << 4);
}
void Button::set_style_enabled(bool enabled) {
    if (!style_data_ || style_data_->context_enabled == enabled) return;
    style_data_->context_enabled = enabled;
    invalidate_state();
}
const ButtonStyleValues* Button::effective_style_values() const {
    if (!style_data_) return nullptr;
    const auto mask = style_state_mask();
    if (style_data_->mask != mask) {
        style_data_->effective = merge_style_values(
            style_data_->style ? style_data_->style->values(mask) : ButtonStyleValues{}, style_data_->local);
        style_data_->mask = mask;
    }
    return &style_data_->effective;
}
Invalidation Button::style_state_changed() {
    if (!style_data_) return Invalidation::paint;
    const auto previous = style_data_->effective;
    return style_layout_equal(previous, *effective_style_values()) ? Invalidation::paint : Invalidation::layout;
}
std::shared_ptr<const ButtonStyle> Button::style() const {
    return style_data_ ? style_data_->style : nullptr;
}
const ButtonStyleValues& Button::style_values() const {
    static const ButtonStyleValues empty;
    return style_data_ ? style_data_->local : empty;
}
void Button::replace_style_data(std::unique_ptr<StyleData> next) {
    const auto previous = style_data_ ? *effective_style_values() : ButtonStyleValues{};
    if (next && !next->style && next->local.empty()) next.reset();
    style_data_ = std::move(next);
    const auto current = style_data_ ? *effective_style_values() : ButtonStyleValues{};
    invalidate(style_layout_equal(previous, current) ? Invalidation::paint : Invalidation::layout);
}
void Button::set_style(std::shared_ptr<const ButtonStyle> style) {
    if ((!style_data_ && !style) || (style_data_ && style_data_->style == style)) return;
    auto next = std::make_unique<StyleData>();
    next->style = std::move(style);
    if (style_data_) {
        next->local = style_data_->local;
        next->context_enabled = style_data_->context_enabled;
    }
    replace_style_data(std::move(next));
}
void Button::set_style_values(ButtonStyleValues values) {
    validate_style_values(values);
    if (!style_data_ && values.empty()) return;
    auto next = std::make_unique<StyleData>();
    if (style_data_) {
        next->style = style_data_->style;
        next->context_enabled = style_data_->context_enabled;
    }
    next->local = std::move(values);
    replace_style_data(std::move(next));
}
Size Button::measure_styled(Size available) {
    if (!visible()) return {};
    const auto* values = effective_style_values();
    const auto metrics = style_metrics(visual_style());
    if (auto_size() && values && (values->padding || values->border_thickness)) {
        const auto text = icon_ == ButtonIcon::none ? measured_text() : Size{16, 16};
        const auto padding = values->padding.value_or(default_button_padding(visual_style()));
        const auto border = values->border_thickness.value_or(Insets{1, 1, 1, 1});
        const auto extra = behavior_ == ButtonBehavior::dropdown ? 20.0f : 0.0f;
        return constrain({text.width + padding.left + padding.right + border.left + border.right + extra,
            std::max(values->padding ? 0.0f : metrics.button_height,
                text.height + padding.top + padding.bottom + border.top + border.bottom)}, available);
    }
    return icon_ == ButtonIcon::none || !auto_size() ? Control::measure(available) :
        constrain({metrics.button_height, metrics.button_height}, available);
}
PartStyleValues Button::surface_style_values() const {
    return merge_part_values(control_style_projection_values(StylePart::root), own_surface_style_values());
}
PartStyleValues Button::own_surface_style_values() const {
    const auto convert = [](const ButtonStyleValues& values) {
        PartStyleValues result;
        result.background = values.background; result.foreground = values.foreground;
        result.border_brush = values.border_brush; result.border_thickness = values.border_thickness;
        result.padding = values.padding; result.corner_radius = values.corner_radius;
        return result;
    };
    auto result = style_data_ ? convert(*effective_style_values()) : PartStyleValues{};
    result = merge_part_values(std::move(result), own_control_style_values(StylePart::root));
    if (style_data_) result = merge_part_values(std::move(result), convert(style_data_->local));
    return merge_part_values(std::move(result), control_style_values(StylePart::root));
}
Rect Button::content_bounds(Rect bounds) const {
    const auto values = surface_style_values();
    const auto padding = values.padding.value_or(default_button_padding(visual_style()));
    const auto border = values.border_thickness.value_or(Insets{1, 1, 1, 1});
    bounds.x += padding.left + border.left; bounds.y += padding.top + border.top;
    bounds.width = std::max(0.0f, bounds.width - padding.left - padding.right - border.left - border.right);
    bounds.height = std::max(0.0f, bounds.height - padding.top - padding.bottom - border.top - border.bottom);
    return bounds;
}
PartStyleValues Button::content_style_values(StylePart part) const {
    if (part != StylePart::label && part != StylePart::icon && part != StylePart::arrow)
        throw std::invalid_argument("Button content part must be label, icon, or arrow");
    const auto root = surface_style_values();
    PartStyleValues result;
    result.foreground = root.foreground;
    if (part == StylePart::label) {
        result.font_family = root.font_family; result.font_size = root.font_size;
        result.font_weight = root.font_weight; result.font_style = root.font_style;
        result.horizontal_alignment = root.horizontal_alignment; result.vertical_alignment = root.vertical_alignment;
    }
    result = merge_part_values(std::move(result), control_style_projection_values(part));
    const auto own_root = own_surface_style_values();
    if (own_root.foreground) result.foreground = own_root.foreground;
    if (part == StylePart::label) {
        if (own_root.font_family) result.font_family = own_root.font_family;
        if (own_root.font_size) result.font_size = own_root.font_size;
        if (own_root.font_weight) result.font_weight = own_root.font_weight;
        if (own_root.font_style) result.font_style = own_root.font_style;
        if (own_root.horizontal_alignment) result.horizontal_alignment = own_root.horizontal_alignment;
        if (own_root.vertical_alignment) result.vertical_alignment = own_root.vertical_alignment;
    }
    if (const auto source = control_style())
        if (const auto authored = source->resolve(part, control_style_state_bits()))
            result = merge_part_values(std::move(result), *authored);
    return merge_part_values(std::move(result), control_style_values(part));
}
Rect Button::dropdown_bounds(Rect bounds) const {
    auto content = content_bounds(bounds);
    const auto* part = effective_control_style_values(StylePart::arrow);
    const auto padding = part && part->padding ? *part->padding : Insets{};
    const float size = part && part->size ? *part->size : 20.0f;
    const float width = std::min(content.width, size + padding.left + padding.right);
    return {content.x + content.width - width + padding.left, content.y + padding.top,
        std::max(0.0f, width - padding.left - padding.right),
        std::max(0.0f, content.height - padding.top - padding.bottom)};
}
Rect Button::icon_bounds(Rect bounds) const {
    auto content = content_bounds(bounds);
    if (behavior_ == ButtonBehavior::dropdown) {
        const auto* arrow = effective_control_style_values(StylePart::arrow);
        const auto padding = arrow && arrow->padding ? *arrow->padding : Insets{};
        content.width = std::max(0.0f, content.width - (arrow && arrow->size ? *arrow->size : 20.0f) -
            padding.left - padding.right);
    }
    const auto* part = effective_control_style_values(StylePart::icon);
    const auto padding = part && part->padding ? *part->padding : Insets{};
    const float size = part && part->size ? *part->size : 16.0f;
    content.x += padding.left; content.y += padding.top;
    content.width = std::max(0.0f, content.width - padding.left - padding.right);
    content.height = std::max(0.0f, content.height - padding.top - padding.bottom);
    const float actual = std::min({size, content.width, content.height});
    const auto root = surface_style_values();
    const auto horizontal = root.horizontal_alignment.value_or(StyleAlignment::center);
    const auto vertical = root.vertical_alignment.value_or(StyleAlignment::center);
    return {content.x + (horizontal == StyleAlignment::start || horizontal == StyleAlignment::stretch ? 0 :
        horizontal == StyleAlignment::end ? content.width - actual : (content.width - actual) / 2),
        content.y + (vertical == StyleAlignment::start || vertical == StyleAlignment::stretch ? 0 :
        vertical == StyleAlignment::end ? content.height - actual : (content.height - actual) / 2), actual, actual};
}
Size Button::measure_control_styled(Size available) {
    if (!visible()) return {};
    if (!auto_size()) return Element::measure(available);
    const auto values = surface_style_values();
    const auto* label = effective_control_style_values(StylePart::label);
    const auto* icon = effective_control_style_values(StylePart::icon);
    const auto* arrow = effective_control_style_values(StylePart::arrow);
    if (part_style_layout_equal(values, {}) && (!label || part_style_layout_equal(*label, {})) &&
        (!icon || part_style_layout_equal(*icon, {})) && (!arrow || part_style_layout_equal(*arrow, {})))
        return style_data_ ? measure_styled(available) : icon_ == ButtonIcon::none ? Control::measure(available) :
            constrain({style_metrics(visual_style()).button_height, style_metrics(visual_style()).button_height}, available);
    const auto metrics = style_metrics(visual_style());
    const auto padding = values.padding.value_or(default_button_padding(visual_style()));
    const auto border = values.border_thickness.value_or(Insets{1, 1, 1, 1});
    auto text = measured_text();
    if (icon_ != ButtonIcon::none) {
        const auto* icon_metrics = effective_control_style_values(StylePart::icon);
        const float size = icon_metrics && icon_metrics->size ? *icon_metrics->size : 16.0f;
        const auto pad = icon_metrics && icon_metrics->padding ? *icon_metrics->padding : Insets{};
        text = {size + pad.left + pad.right, size + pad.top + pad.bottom};
    }
    if (behavior_ == ButtonBehavior::dropdown) {
        const auto* arrow_metrics = effective_control_style_values(StylePart::arrow);
        const auto pad = arrow_metrics && arrow_metrics->padding ? *arrow_metrics->padding : Insets{};
        const float size = arrow_metrics && arrow_metrics->size ? *arrow_metrics->size : 20.0f;
        text.width += size + pad.left + pad.right;
        text.height = std::max(text.height, size + pad.top + pad.bottom);
    }
    return constrain({text.width + padding.left + padding.right + border.left + border.right,
        std::max(values.padding ? 0.0f : metrics.button_height,
            text.height + padding.top + padding.bottom + border.top + border.bottom)}, available);
}
void Button::set_repeat_timing(unsigned delay, unsigned interval) {
    if (delay < 100 || delay > 60000 || interval < 16 || interval > 60000)
        throw std::invalid_argument("Invalid repeat timing");
    if (repeat_delay_ == delay && repeat_interval_ == interval) return;
    cancel(); repeat_delay_ = delay; repeat_interval_ = interval;
}
void Toggle::set_checked(bool checked) {
    set_check_state(checked ? CheckState::checked : CheckState::unchecked);
}
void Toggle::set_check_state(CheckState state) {
    if (state < CheckState::unchecked || state > CheckState::indeterminate) throw std::invalid_argument("Invalid checkbox state");
    if (state_ == state) return;
    state_ = state;
    invalidate_state();
}
void Toggle::activate() {
    set_checked(!checked());
    const auto callback = change_;
    if (callback) callback(checked());
}
void CheckBox::activate() {
    set_state(state() == CheckState::unchecked ? CheckState::checked :
        state() == CheckState::checked && three_state_ ? CheckState::indeterminate : CheckState::unchecked);
    const auto callback = change_;
    if (callback) callback(state());
}
void InfoBadge::set_count(std::uint32_t value) {
    if (kind_ == InfoBadgeKind::count && count_ == value) return;
    kind_ = InfoBadgeKind::count; count_ = value; invalidate(Invalidation::layout);
}
void InfoBadge::set_icon(ButtonIcon value) {
    if (value < ButtonIcon::none || value > ButtonIcon::open) throw std::invalid_argument("Invalid badge icon");
    if (kind_ == InfoBadgeKind::icon && icon_ == value) return;
    kind_ = InfoBadgeKind::icon; icon_ = value; invalidate(Invalidation::layout);
}
void InfoBadge::set_dot() {
    if (kind_ == InfoBadgeKind::dot) return;
    kind_ = InfoBadgeKind::dot; invalidate(Invalidation::layout);
}
std::wstring InfoBadge::display_text() const {
    return kind_ == InfoBadgeKind::count ? count_ > 99 ? L"99+" : std::to_wstring(count_) : L"";
}
Size InfoBadge::measure(Size available) {
    if (!visible()) return {};
    if (!auto_size()) return Control::measure(available);
    const auto* root = effective_control_style_values(StylePart::root);
    const auto padding = root && root->padding ? *root->padding : Insets{};
    const auto border = root && root->border_thickness ? *root->border_thickness : Insets{};
    const auto* message = effective_control_style_values(StylePart::message);
    const auto* icon = effective_control_style_values(StylePart::icon);
    const float text_scale = message && message->font_size ? *message->font_size / 12.0f : 1.0f;
    const float size = kind_ == InfoBadgeKind::dot ? 8.0f : kind_ == InfoBadgeKind::icon ?
        std::max(20.0f, icon && icon->size ? *icon->size + 8 : 20.0f) : 20.0f * text_scale;
    const float width = kind_ == InfoBadgeKind::count ?
        (count_ > 99 ? 30.0f : count_ > 9 ? 24.0f : 20.0f) * text_scale : size;
    return constrain({width + padding.left + padding.right + border.left + border.right,
        size + padding.top + padding.bottom + border.top + border.bottom}, available);
}
Toggle::Layout Toggle::layout_metrics() const {
    const auto* root = effective_style_values(StylePart::root);
    const auto* indicator = effective_style_values(StylePart::indicator);
    const bool winui = visual_style() == VisualStyle::winui;
    Layout layout;
    layout.gap = winui && !switch_ ? 9.0f : 12.0f;
    layout.indicator_size = indicator && indicator->size ? *indicator->size : (switch_ ? 20.0f : winui ? 19.0f : 18.0f);
    layout.padding = root && root->padding ? *root->padding :
        winui ? Insets{} : Insets{12, 0, 12, 0};
    layout.border = root && root->border_thickness ? *root->border_thickness : Insets{};
    layout.indicator_border = indicator && indicator->border_thickness ? *indicator->border_thickness : Insets{};
    return layout;
}
namespace {
Rect inset_rect(Rect bounds, const Insets& insets) {
    return {bounds.x + insets.left, bounds.y + insets.top,
        std::max(0.0f, bounds.width - insets.left - insets.right), std::max(0.0f, bounds.height - insets.top - insets.bottom)};
}
}
Rect Label::content_bounds(Rect bounds) const {
    const auto* root = effective_control_style_values(StylePart::root);
    if (!root) return bounds;
    return inset_rect(inset_rect(bounds, root->border_thickness.value_or(Insets{})), root->padding.value_or(Insets{}));
}
Rect Toggle::indicator_bounds(Rect bounds) const {
    const auto layout = layout_metrics();
    const auto content = content_bounds(bounds);
    const float height = switch_ ? std::min(layout.indicator_size, content.height) : layout.indicator_size;
    const float width = switch_ ? std::min(layout.indicator_size * 2, content.width) : layout.indicator_size;
    return {content.x, content.y + std::max(0.0f, (content.height - height) / 2), width, height};
}
Rect Toggle::content_bounds(Rect bounds) const {
    const auto layout = layout_metrics();
    return inset_rect(inset_rect(bounds, layout.border), layout.padding);
}
Rect Toggle::label_bounds(Rect bounds) const {
    auto content = content_bounds(bounds);
    const auto layout = layout_metrics();
    const float prefix = layout.indicator_size * (switch_ ? 2 : 1) + layout.gap;
    content.x += prefix;
    content.width = std::max(0.0f, content.width - prefix);
    return content;
}
Rect Toggle::mark_bounds(Rect bounds) const {
    return mark_bounds(bounds, enabled());
}
Rect Toggle::mark_bounds(Rect bounds, bool enabled) const {
    auto mark = inset_rect(indicator_bounds(bounds), layout_metrics().indicator_border);
    if (!switch_) return mark;
    if (visual_style() == VisualStyle::winui) {
        const float cell = std::min(mark.height, mark.width / 2);
        const float scale = cell / 20;
        const float width = (!enabled || (!pressed() && !hovered()) ? 12.0f : pressed() ? 17.0f : 14.0f) * scale;
        const float height = (!enabled || (!pressed() && !hovered()) ? 12.0f : 14.0f) * scale;
        const float center = (checked() ? mark.width - cell / 2 : cell / 2) - 0.5f * scale;
        const float left = enabled && pressed() ? (checked() ? mark.width - width - 3 * scale : 3 * scale) : center - width / 2;
        return {mark.x + left, mark.y + (mark.height - height) / 2, width, height};
    }
    const float inset = std::min(pressed() ? 2.0f : hovered() ? 2.5f : 3.0f, std::min(mark.width, mark.height) / 2);
    mark = inset_rect(mark, {inset, inset, inset, inset});
    const float size = std::min(mark.height, mark.width);
    return {checked() ? mark.x + mark.width - size : mark.x, mark.y + (mark.height - size) / 2, size, size};
}
Size Toggle::measure(Size available) {
    if (!visible()) return {};
    const auto* root = effective_style_values(StylePart::root);
    const auto* indicator = effective_style_values(StylePart::indicator);
    const bool layout_affecting = (root && (root->padding || root->border_thickness)) || (indicator && indicator->size);
    if (!auto_size() || (!layout_affecting && !switch_)) return Control::measure(available);
    const auto text = measured_text();
    const auto layout = layout_metrics();
    const float width = layout.padding.left + layout.border.left + layout.indicator_size * (switch_ ? 2 : 1) + layout.gap + text.width +
        layout.padding.right + layout.border.right;
    const float height = std::max(text.height, layout.indicator_size) + layout.padding.top + layout.padding.bottom +
        layout.border.top + layout.border.bottom;
    return constrain({width, switch_ && !layout_affecting ?
        std::max(visual_style() == VisualStyle::winui ? 40.0f : 32.0f, height) : height}, available);
}
void TextInput::set_text(std::wstring text) {
    ++suggestion_revision_;
    assign_text(std::move(text));
    if (suggestions_) invalidate(Invalidation::paint);
}
TextInput::Selection TextInput::normalize_selection(std::wstring_view text, Selection value) {
    value.start = std::min(value.start, text.size());
    value.end = std::min(value.end, text.size());
    if (value.start > value.end) std::swap(value.start, value.end);
    const auto splits_pair = [&](std::size_t offset) {
        return offset > 0 && offset < text.size() &&
            text[offset - 1] >= 0xd800 && text[offset - 1] <= 0xdbff &&
            text[offset] >= 0xdc00 && text[offset] <= 0xdfff;
    };
    if (value.start == value.end) {
        if (splits_pair(value.start)) --value.start;
        value.end = value.start;
    } else {
        if (splits_pair(value.start)) --value.start;
        if (splits_pair(value.end)) ++value.end;
    }
    return value;
}
TextInput::Selection TextInput::selection() const {
    return read_selection_ ? read_selection_() : normalize_selection(text_, selection_);
}
void TextInput::set_selection(Selection value) {
    value = normalize_selection(text_, value);
    if (write_selection_) write_selection_(value);
    selection_ = value;
}
void TextInput::bind_selection(std::function<Selection()> reader, std::function<void(Selection)> writer) {
    writer(normalize_selection(text_, selection_));
    read_selection_ = std::move(reader);
    write_selection_ = std::move(writer);
}
void TextInput::assign_text(std::wstring text) {
    const auto nul = text.find(L'\0');
    if (nul != std::wstring::npos) text.resize(nul);
    if (text.size() > maximum_length_) {
        text.resize(maximum_length_);
        if constexpr (sizeof(wchar_t) == 2) {
            if (text.back() >= 0xd800 && text.back() <= 0xdbff) text.pop_back();
        }
    }
    if (text_ == text) return;
    text_ = std::move(text);
    invalidate_state();
}
void TextInput::set_maximum_length(std::size_t value) {
    maximum_length_ = std::clamp<std::size_t>(value, 1, 32767);
    set_text(text_);
    invalidate(Invalidation::paint);
}

TabStrip::TabStrip(std::wstring name) : Control(ControlRole::tab_strip, std::move(name), {320, 38}),
    new_button_(std::make_shared<Button>(L"New tab")) {
    new_button_->set_icon(ButtonIcon::add);
    new_button_->set_visible(false);
    new_button_->on_click([this] { request_new_tab(); });
    children_.push_back(new_button_);
    adopt(new_button_);
}
TabStrip::~TabStrip() { new_button_->on_click({}); }
void TabStrip::set_duration(unsigned milliseconds) {
    if (milliseconds > 10000) throw std::invalid_argument("Tab duration must be between 0 and 10000 milliseconds");
    if (duration_ == milliseconds) return;
    duration_ = milliseconds;
    settle();
}
void TabStrip::advance(Clock::time_point now) {
    if (!animating()) return;
    const float elapsed = std::chrono::duration<float, std::milli>(now - motion_started_).count();
    const float t = std::clamp(elapsed / duration_, 0.0f, 1.0f);
    if (t == 1) { settle(); return; }
    const float remaining = 1 - t;
    const float next = std::max(motion_progress_, 1 - remaining * remaining * remaining);
    if (next == motion_progress_) return;
    motion_progress_ = next;
    arrange_new_button();
    invalidate(Invalidation::placement);
}
void TabStrip::settle() {
    if (!animating()) return;
    motion_.clear();
    motion_crossing_ = false;
    scrolling_ = false;
    motion_progress_ = 1;
    arrange_new_button();
    invalidate(Invalidation::placement);
}
void TabStrip::set_new_tab_button_visible(bool visible) {
    if (new_button_visible_ == visible) return;
    settle();
    new_button_visible_ = visible;
    new_button_->set_visible(visible);
    reveal_selected();
    arrange_new_button();
    invalidate(Invalidation::layout);
}
void TabStrip::request_new_tab() {
    if (!enabled() || !visible() || !new_button_visible_) return;
    auto callback = new_tab_;
    if (callback) callback();
}
float TabStrip::tab_viewport_width() const {
    return std::max(0.0f, content_bounds().width - (new_button_visible_ ? 32.0f : 0.0f));
}
Rect TabStrip::new_tab_button_bounds() const {
    if (!new_button_visible_) return {};
    const auto content = content_bounds();
    float right = content.x;
    if (overflows()) right += tab_viewport_width();
    else if (animating()) right = new_button_from_ + (new_button_to_ - new_button_from_) * motion_progress_;
    else {
        for (std::size_t i = first_; i < tabs_.size(); ++i) {
            const auto tab = tab_bounds(i);
            if (tab.width <= 0) break;
            right = std::max(right, tab.x + tab.width);
        }
    }
    return {right, content.y + std::min(3.0f, content.height),
        std::min(32.0f, std::max(0.0f, content.x + content.width - right)), std::max(0.0f, content.height - 6)};
}
void TabStrip::arrange_new_button() {
    auto button = new_tab_button_bounds();
    button.x += bounds().x; button.y += bounds().y;
    new_button_->arrange(button);
}
void TabStrip::set_colors(TabColors colors) {
    for (const auto value : {colors.row_background, colors.selected_background, colors.selected_text,
        colors.inactive_background, colors.inactive_text, colors.hover_background, colors.border})
        if (value && *value > 0xffffff) throw std::invalid_argument("Tab colors must be 0xRRGGBB values");
    if (colors_ == colors) return;
    colors_ = colors;
    invalidate(Invalidation::paint);
}
void TabStrip::set_tabs(std::vector<TabItem> tabs, std::optional<std::uint64_t> selected) {
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        if (tabs[i].icon < ButtonIcon::none || tabs[i].icon > ButtonIcon::mixed ||
            tabs[i].image_path.size() > 32767 || tabs[i].image_path.find(L'\0') != std::wstring::npos)
            throw std::invalid_argument("Invalid tab icon or image path");
        if (!tabs[i].id || tabs[i].id > static_cast<std::uint64_t>(std::numeric_limits<std::intptr_t>::max()) - 100)
            throw std::invalid_argument("Tab identity is outside the supported range");
        for (std::size_t j = 0; j < i; ++j)
            if (tabs[j].id == tabs[i].id) throw std::invalid_argument("Duplicate tab identity");
    }
    if (selected && std::none_of(tabs.begin(), tabs.end(), [&](const auto& tab) { return tab.id == *selected; }))
        throw std::invalid_argument("Selected tab must exist");
    if (!tabs.empty() && !selected) selected = tabs.front().id;
    if (tabs == tabs_ && selected == selected_) return;
    const bool same_ids = tabs.size() == tabs_.size() &&
        std::equal(tabs.begin(), tabs.end(), tabs_.begin(), [](const auto& a, const auto& b) { return a.id == b.id; });
    const auto previous_button_x = new_tab_button_bounds().x;
    const auto previous_presented_first = presented_first();
    std::vector<TabMotion> next;
    bool crossing = motion_crossing_;
    if (!same_ids && duration_ && !tabs_.empty() && tabs.size() > tabs_.size() && tabs_fit()) {
        next.reserve(tabs.size());
        std::size_t old{};
        float x = content_bounds().x;
        for (const auto& tab : tabs) {
            const auto from = old < tabs_.size() && tabs_[old].id == tab.id ?
                tab_bounds(old++) : Rect{x, content_bounds().y, 0, content_bounds().height};
            next.push_back({tab.id, from, {}});
            x = from.x + from.width;
        }
        // Only insertion preserving every prior ID's order is compatible with the current presentation.
        if (old != tabs_.size()) next.clear();
    } else if (!same_ids && duration_ && !tabs.empty() && tabs.size() < tabs_.size() && tabs_fit()) {
        next.reserve(tabs.size());
        std::size_t old{};
        for (const auto& tab : tabs) {
            while (old < tabs_.size() && tabs_[old].id != tab.id) ++old;
            if (old == tabs_.size()) { next.clear(); break; }
            next.push_back({tab.id, tab_bounds(old++), {}});
        }
    } else if (!same_ids && duration_ && !tabs.empty() && tabs.size() == tabs_.size() && tabs_fit()) {
        next.reserve(tabs.size());
        for (const auto& tab : tabs) {
            const auto old = std::find_if(tabs_.begin(), tabs_.end(), [&](const auto& item) { return item.id == tab.id; });
            if (old == tabs_.end()) { next.clear(); break; }
            next.push_back({tab.id, tab_bounds(static_cast<std::size_t>(old - tabs_.begin())), {}});
        }
        crossing = true;
    }
    if (!same_ids) { motion_.clear(); motion_crossing_ = false; scrolling_ = false; }
    const auto previous_first = first_;
    tabs_ = std::move(tabs);
    drop_indicator_.reset();
    ++tabs_revision_;
    selected_ = selected;
    reveal_selected();
    if (first_ != previous_first || !tabs_fit()) {
        motion_.clear();
        motion_crossing_ = false;
        next.clear();
    }
    if (same_ids && first_ != previous_first) start_scroll(previous_presented_first);
    if (!next.empty()) {
        for (std::size_t i = 0; i < next.size(); ++i) next[i].to = target_tab_bounds(i);
        new_button_from_ = previous_button_x;
        new_button_to_ = new_tab_button_bounds().x;
        motion_ = std::move(next);
        motion_crossing_ = crossing;
        motion_progress_ = 0;
        motion_started_ = Clock::now();
    }
    arrange_new_button();
    invalidate(animating() ? Invalidation::placement : new_button_visible_ ? Invalidation::layout : Invalidation::paint);
}
bool TabStrip::select(std::uint64_t id) {
    if (!enabled() || std::none_of(tabs_.begin(), tabs_.end(), [&](const auto& tab) { return tab.id == id; })) return false;
    if (selected_ == id) return true;
    selected_ = id;
    const auto previous_first = first_;
    const auto previous_presented_first = presented_first();
    reveal_selected();
    if (first_ != previous_first) start_scroll(previous_presented_first);
    arrange_new_button();
    invalidate(animating() ? Invalidation::placement : new_button_visible_ ? Invalidation::layout : Invalidation::paint);
    if (select_) { auto callback = select_; callback(id); }
    return true;
}
bool TabStrip::activate_tab(std::uint64_t id) {
    if (!select(id) || selected_ != id || !enabled()) return false;
    if (activate_) { auto callback = activate_; callback(id); }
    return true;
}
void TabStrip::step(int delta) {
    if (tabs_.empty()) return;
    auto found = std::find_if(tabs_.begin(), tabs_.end(), [&](const auto& tab) { return selected_ == tab.id; });
    auto index = found == tabs_.end() ? 0 : static_cast<std::size_t>(found - tabs_.begin());
    index = delta < 0 ? (index ? index - 1 : tabs_.size() - 1) : (index + 1) % tabs_.size();
    select(tabs_[index].id);
}
void TabStrip::request_close(std::uint64_t id) {
    if (enabled() && close_ && std::any_of(tabs_.begin(), tabs_.end(), [&](const auto& tab) { return tab.id == id; })) {
        auto callback = close_;
        callback(id);
    }
}
bool TabStrip::prepare_context_menu(std::optional<Point> position) {
    context_tab_.reset();
    if (!enabled() || !visible()) return false;
    if (position) {
        if (const auto index = hit_test(*position)) context_tab_ = tabs_[*index].id;
    } else context_tab_ = selected_;
    return context_tab_.has_value();
}
Rect TabStrip::tab_bounds(std::size_t index) const {
    if (scrolling_) {
        if (index >= tabs_.size()) return {};
        const auto content = content_bounds();
        const double width = target_tab_width();
        const double first = presented_first();
        const double left = (static_cast<double>(index) - first) * width;
        const double right = std::min((static_cast<double>(index + 1) - first) * width,
            static_cast<double>(tab_viewport_width()));
        if (right <= 0 || left >= right) return {};
        const float start = content.x + static_cast<float>(std::max(0.0, left));
        const float end = content.x + static_cast<float>(right);
        float extent = std::max(0.0f, end - start);
        if (start + extent > end) extent = std::nextafter(extent, 0.0f);
        return {start, content.y, extent, content.height};
    }
    auto rect = motion_tab_bounds(index);
    if (!motion_crossing_ || rect.width <= 0) return rect;
    const double center = static_cast<double>(rect.x) + rect.width / 2.0;
    double scale = 1;
    // Disjoint rectangles keep paint, pointer, and UIA ordering equivalent during crossings.
    for (std::size_t i = 0; i < motion_.size(); ++i) if (i != index) {
        const auto other = motion_tab_bounds(i);
        if (other.width <= 0) continue;
        const double distance = std::abs(center - (static_cast<double>(other.x) + other.width / 2.0));
        scale = std::min(scale, distance / (rect.width / 2.0 + other.width / 2.0));
    }
    if (scale == 1) return rect;
    const double left = center - rect.width * scale / 2, right = center + rect.width * scale / 2;
    rect.x = static_cast<float>(left);
    float end = static_cast<float>(right);
    // Round inward so subpixel crossings cannot create competing hit targets.
    if (rect.x < left) rect.x = std::nextafter(rect.x, std::numeric_limits<float>::infinity());
    if (end > right) end = std::nextafter(end, -std::numeric_limits<float>::infinity());
    rect.width = std::max(0.0f, end - rect.x);
    if (rect.x + rect.width > end) rect.width = std::nextafter(rect.width, 0.0f);
    return rect;
}
Rect TabStrip::motion_tab_bounds(std::size_t index) const {
    if (index < motion_.size() && index < tabs_.size() && motion_[index].id == tabs_[index].id) {
        const auto& item = motion_[index];
        const auto blend = [&](float from, float to) { return from + (to - from) * motion_progress_; };
        return {blend(item.from.x, item.to.x), blend(item.from.y, item.to.y),
            blend(item.from.width, item.to.width), blend(item.from.height, item.to.height)};
    }
    return target_tab_bounds(index);
}
float TabStrip::target_tab_width() const {
    const auto* tab = effective_control_style_values(StylePart::tab);
    return std::min(tab && tab->width ? *tab->width : 180.0f,
        tab_viewport_width() / std::max(1.0f, std::min(3.0f, static_cast<float>(tabs_.size()))));
}
bool TabStrip::overflows() const {
    return tabs_.size() * target_tab_width() > tab_viewport_width() + 0.01f;
}
bool TabStrip::tabs_fit() const {
    return first_ == 0 && content_bounds().height > 0 && target_tab_width() > 0 &&
        !overflows();
}
Rect TabStrip::target_tab_bounds(std::size_t index) const {
    if (index >= tabs_.size() || index < first_) return {};
    const auto content = content_bounds();
    const auto viewport = tab_viewport_width();
    const auto width = target_tab_width();
    const float x = content.x + (index - first_) * width;
    return {x, content.y, std::max(0.0f, std::min(width, content.x + viewport - x)), content.height};
}
Rect TabStrip::content_bounds() const {
    return content_bounds({0, 0, bounds().width, bounds().height});
}
Rect TabStrip::content_bounds(Rect content) const {
    const auto* root = effective_control_style_values(StylePart::root);
    return root ? inset_rect(inset_rect(content, root->border_thickness.value_or(Insets{})),
        root->padding.value_or(Insets{})) : content;
}
std::optional<std::size_t> TabStrip::hit_test(float x) const {
    if (x < 0 || x >= bounds().width) return {};
    const auto first = scrolling_ ? static_cast<std::size_t>(presented_first()) : first_;
    for (std::size_t i = first; i < tabs_.size(); ++i) {
        const auto rect = tab_bounds(i);
        if (x >= rect.x && x < rect.x + rect.width) return i;
    }
    return {};
}
std::optional<std::size_t> TabStrip::hit_test(Point point) const {
    const auto content = content_bounds();
    return point.y >= content.y && point.y < content.y + content.height ? hit_test(point.x) : std::nullopt;
}
std::size_t TabStrip::insertion_index(float x) const {
    if (!std::isfinite(x)) throw std::invalid_argument("Tab insertion position must be finite");
    for (std::size_t i = first_; i < tabs_.size(); ++i) {
        const auto b = tab_bounds(i);
        if (b.width <= 0) return i;
        if (x < b.x + b.width / 2) return i;
    }
    return tabs_.size();
}
void TabStrip::set_drop_indicator(std::optional<std::size_t> index) {
    if (index && *index > tabs_.size()) throw std::invalid_argument("Tab insertion index is outside the strip");
    if (drop_indicator_ == index) return;
    drop_indicator_ = index;
    invalidate(Invalidation::paint);
}
Rect TabStrip::close_bounds(std::size_t index) const {
    auto b = tab_bounds(index);
    if (const auto* tab = effective_control_style_values(StylePart::tab)) {
        b = inset_rect(inset_rect(b, tab->border_thickness.value_or(Insets{})), tab->padding.value_or(Insets{}));
    }
    const auto* action = effective_control_style_values(StylePart::close_action);
    const float size = action && action->size ? *action->size : 24;
    if (!closable() || b.width < size + 24 || b.height < size) return {};
    return {b.x + b.width - size - 6, b.y + (b.height - size) / 2, size, size};
}
void TabStrip::reveal_selected() {
    first_ = std::min(first_, tabs_.empty() ? 0 : tabs_.size() - 1);
    if (!overflows()) first_ = 0;
    if (!selected_) return;
    for (std::size_t i = 0; i < tabs_.size(); ++i) if (tabs_[i].id == selected_) {
        if (i < first_) first_ = i;
        const float minimum = std::min(120.0f, target_tab_width());
        while (first_ < i && target_tab_bounds(i).width + 0.01f < minimum) ++first_;
        break;
    }
}
double TabStrip::presented_first() const {
    return scrolling_ ? std::lerp(scroll_from_, static_cast<double>(first_), static_cast<double>(motion_progress_)) :
        static_cast<double>(first_);
}
void TabStrip::start_scroll(double from) {
    motion_.clear();
    motion_crossing_ = false;
    scrolling_ = duration_ && from != static_cast<double>(first_) && target_tab_width() > 0 && content_bounds().height > 0;
    scroll_from_ = from;
    motion_progress_ = scrolling_ ? 0.0f : 1.0f;
    motion_started_ = Clock::now();
}
void TabStrip::arrange(Rect rect) {
    const auto previous = bounds();
    Element::arrange(rect);
    if (bounds().width != previous.width || bounds().height != previous.height) settle();
    const auto previous_first = first_;
    reveal_selected();
    if (first_ != previous_first) settle();
    arrange_new_button();
}
ContentView::ContentView(std::shared_ptr<Element> content, std::wstring name)
    : Control(ControlRole::content_view, std::move(name), {320, 240}), content_(std::move(content)) { adopt(content_); }
Size ContentView::measure(Size available) {
    if (!visible()) return {};
    if (!auto_size()) return Element::measure(available);
    const auto p = layout_style::insets(effective_control_style_values(StylePart::root));
    return constrain(layout_style::outer(content_->measure(layout_style::inner(available, p)), p), available);
}
Rect ContentView::content_bounds() const { return layout_style::content(*this, bounds()); }
void ContentView::arrange(Rect rect) {
    Element::arrange(rect);
    const auto area = content_bounds();
    const auto desired = content_->measure({area.width, area.height});
    content_->arrange(layout_style::aligned(area, desired, effective_control_style_values(StylePart::root)));
}
SplitView::SplitView(std::shared_ptr<Element> first, std::shared_ptr<Element> second, std::wstring name)
    : Control(ControlRole::split_view, std::move(name), {640, 480}),
      first_(std::make_shared<ContentView>(std::move(first), L"Left pane")),
      second_(std::make_shared<ContentView>(std::move(second), L"Right pane")) {
    adopt(first_); adopt(second_);
}
StyleStateMask SplitView::control_style_state_bits() const {
    return Control::control_style_state_bits() | (style_dragging_ ? style_states::dragging : 0);
}
void SplitView::set_style_dragging(bool value) {
    if (style_dragging_ == value) return;
    const auto adopted_ratio = value && ratio_animating_ ?
        std::optional<float>{ratio_presented_ / (pane_extent() - effective_divider_width())} : std::nullopt;
    if (value) settle();
    style_dragging_ = value;
    if (adopted_ratio) set_ratio(*adopted_ratio);
    if (!invalidate_control_style_state()) invalidate(Invalidation::paint);
}
float SplitView::effective_divider_width() const {
    const auto* values = effective_control_style_values(StylePart::divider);
    return values ? values->width.value_or(divider_width) : divider_width;
}
Rect SplitView::pane_area() const { return layout_style::content(*this, bounds()); }
float SplitView::pane_extent() const {
    const auto area = pane_area();
    return axis_ == Axis::horizontal ? area.width : area.height;
}
bool SplitView::expanded() const {
    return secondary_visible_ && (!primary_visible_ || pane_extent() >= 2 * minimum_extent_ + effective_divider_width());
}
float SplitView::presented_first_extent() const {
    if (ratio_animating_) return ratio_presented_;
    const float extent = pane_extent() - effective_divider_width();
    return extent >= 2 * minimum_extent_ ?
        std::clamp(extent * ratio_, minimum_extent_, extent - minimum_extent_) : extent;
}
Rect SplitView::divider() const {
    const auto b = pane_area();
    if (!primary_visible_) return {};
    const auto extent = pane_extent();
    if (extent < 2 * minimum_extent_ + effective_divider_width() || progress_ == 0) return {};
    const auto divider_extent = effective_divider_width();
    const auto offset = extent - (extent - presented_first_extent()) * progress_;
    return axis_ == Axis::horizontal ? Rect{b.x + offset, b.y, divider_extent * progress_, b.height} :
        Rect{b.x, b.y + offset, b.width, divider_extent * progress_};
}
Rect SplitView::first_pane_area() const {
    const auto b = pane_area();
    if (!primary_visible_) return {b.x, b.y, 0, 0};
    const auto extent = pane_extent();
    const bool presenting = extent >= 2 * minimum_extent_ + effective_divider_width() &&
        (secondary_visible_ || animating_ || progress_ > 0);
    const auto first = presenting ? extent - (extent - presented_first_extent()) * progress_ : extent;
    return axis_ == Axis::horizontal ? Rect{b.x, b.y, first, b.height} :
        Rect{b.x, b.y, b.width, first};
}
Rect SplitView::second_pane_area() const {
    const auto b = pane_area();
    if (!primary_visible_) return secondary_visible_ ? b : Rect{b.x, b.y, 0, 0};
    const auto extent = pane_extent();
    const bool presenting = extent >= 2 * minimum_extent_ + effective_divider_width() &&
        (secondary_visible_ || animating_ || progress_ > 0);
    const auto second = presenting ? extent - effective_divider_width() - presented_first_extent() : 0;
    const auto offset = extent - second * progress_;
    return axis_ == Axis::horizontal ? Rect{b.x + offset, b.y, second, presenting ? b.height : 0} :
        Rect{b.x, b.y + offset, presenting ? b.width : 0, second};
}
void SplitView::arrange(Rect rect) {
    Element::arrange(rect);
    const auto b = pane_area();
    if (!primary_visible_ || pane_extent() < 2 * minimum_extent_ + effective_divider_width() ||
        (ratio_animating_ && (b.width != ratio_viewport_.width || b.height != ratio_viewport_.height))) settle();
    first_->arrange(layout_style::content(*this, first_pane_area(), StylePart::first_pane));
    second_->arrange(layout_style::content(*this, second_pane_area(), StylePart::second_pane));
    const bool value = expanded();
    if (arranged_expanded_ != value) {
        arranged_expanded_ = value;
        auto callback = expanded_callback_;
        if (callback) callback(value);
    }
}
void SplitView::set_ratio(float value) {
    if (!std::isfinite(value)) return;
    value = std::clamp(value, 0.1f, 0.9f);
    if (value == ratio_) return;
    const bool animate = transition_duration_ && primary_visible_ && !style_dragging_ && expanded() && progress_ == 1 &&
        second_->bounds().width > 0 && second_->bounds().height > 0 &&
        (!animating_ || ratio_animating_);
    if (animate) {
        ratio_start_ = presented_first_extent();
        ratio_ = value;
        const auto area = pane_area();
        const float extent = pane_extent() - effective_divider_width();
        ratio_target_ = std::clamp(extent * ratio_, minimum_extent_, extent - minimum_extent_);
        ratio_presented_ = ratio_start_;
        ratio_viewport_ = {area.width, area.height};
        started_ = Clock::now();
        animating_ = ratio_animating_ = ratio_start_ != ratio_target_;
    } else {
        settle();
        ratio_ = value;
    }
    invalidate(Invalidation::layout);
    auto callback = ratio_callback_;
    if (callback) callback(value);
}
void SplitView::set_layout(Axis axis, float minimum_extent) {
    if ((axis != Axis::horizontal && axis != Axis::vertical) ||
        !std::isfinite(minimum_extent) || minimum_extent < 1 || minimum_extent > 65536)
        throw std::invalid_argument("Split layout requires a valid axis and a pane minimum from 1 to 65536 DIPs");
    if (axis_ == axis && minimum_extent_ == minimum_extent) return;
    settle();
    axis_ = axis;
    minimum_extent_ = minimum_extent;
    first_->set_name(axis == Axis::horizontal ? L"Left pane" : L"Top pane");
    second_->set_name(axis == Axis::horizontal ? L"Right pane" : L"Bottom pane");
    set_style_dragging(false);
    invalidate(Invalidation::layout);
}
void SplitView::set_secondary_visible(bool value) {
    if (secondary_visible_ == value) return;
    if (ratio_animating_) settle();
    const auto now = Clock::now();
    advance(now);
    secondary_visible_ = value;
    if (!value) set_style_dragging(false);
    start_ = progress_;
    started_ = now;
    animating_ = transition_duration_ && primary_visible_ && progress_ != (value ? 1.0f : 0.0f);
    if (!animating_) progress_ = value ? 1.0f : 0.0f;
    invalidate(Invalidation::layout);
}
void SplitView::set_transition_duration(unsigned milliseconds) {
    if (milliseconds > 10000) throw std::invalid_argument("Split transition duration must be between 0 and 10000 milliseconds");
    if (transition_duration_ == milliseconds) return;
    transition_duration_ = milliseconds;
    settle();
}
void SplitView::advance(Clock::time_point now) {
    if (!animating_) return;
    const float elapsed = std::chrono::duration<float, std::milli>(now - started_).count();
    const auto t = std::clamp(elapsed / transition_duration_, 0.0f, 1.0f);
    if (t == 1) { settle(); return; }
    if (ratio_animating_) {
        const float remaining = 1 - t;
        const float next = ratio_start_ + (ratio_target_ - ratio_start_) * (1 - remaining * remaining * remaining);
        const float presented = ratio_target_ > ratio_start_ ?
            std::max(ratio_presented_, next) : std::min(ratio_presented_, next);
        if (presented == ratio_presented_) return;
        ratio_presented_ = presented;
        invalidate(Invalidation::layout);
        return;
    }
    const float remaining = 1 - t, target = secondary_visible_ ? 1.0f : 0.0f;
    const auto next = start_ + (target - start_) * (1 - remaining * remaining * remaining);
    const auto progress = secondary_visible_ ? std::max(progress_, next) : std::min(progress_, next);
    if (progress == progress_) return;
    progress_ = progress;
    invalidate(Invalidation::layout);
}
void SplitView::settle() {
    const auto target = secondary_visible_ ? 1.0f : 0.0f;
    if (!animating_ && progress_ == target) return;
    animating_ = false;
    ratio_animating_ = false;
    progress_ = target;
    invalidate(Invalidation::layout);
}
void SplitView::set_primary_visible(bool value) {
    if (primary_visible_ == value) return;
    settle();
    primary_visible_ = value;
    if (!value) set_style_dragging(false);
    invalidate(Invalidation::layout);
}
void TextInput::commit_text(std::wstring text) {
    if (text_ == text) return;
    const auto previous = text_;
    assign_text(std::move(text));
    if (text_ == previous) return;
    const auto callback = change_;
    if (callback) {
        const auto value = text_;
        callback(value);
    }
}
Control* next_focus(std::span<Control* const> controls, const Control* current, bool reverse) {
    if (controls.empty()) return nullptr;
    const auto found = std::find(controls.begin(), controls.end(), current);
    size_t index = found == controls.end() ? (reverse ? 0 : controls.size() - 1)
                                          : static_cast<size_t>(found - controls.begin());
    for (size_t count = 0; count < controls.size(); ++count) {
        index = reverse ? (index ? index - 1 : controls.size() - 1) : (index + 1) % controls.size();
        if (controls[index]->focusable()) return controls[index];
    }
    return nullptr;
}

}
