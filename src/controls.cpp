#include "xui/controls.hpp"
#include "layout_styling.hpp"
#include <algorithm>
#include <cmath>

namespace xui {
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
    invalidate(Invalidation::layout);
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
        const auto padding = values->padding.value_or(Insets{metrics.button_padding, 6, metrics.button_padding,
            visual_style() == VisualStyle::winui ? 7.0f : 6.0f});
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
    const auto metrics = style_metrics(visual_style());
    const auto padding = values.padding.value_or(Insets{metrics.button_padding, 6, metrics.button_padding,
        visual_style() == VisualStyle::winui ? 7.0f : 6.0f});
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
    const auto padding = values.padding.value_or(Insets{metrics.button_padding, 6, metrics.button_padding,
        visual_style() == VisualStyle::winui ? 7.0f : 6.0f});
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
    if (checked_ == checked) return;
    checked_ = checked;
    invalidate_state();
}
void Toggle::activate() {
    set_checked(!checked_);
    const auto callback = change_;
    if (callback) callback(checked_);
}
Toggle::Layout Toggle::layout_metrics() const {
    const auto* root = effective_style_values(StylePart::root);
    const auto* indicator = effective_style_values(StylePart::indicator);
    const bool winui = visual_style() == VisualStyle::winui;
    Layout layout;
    layout.gap = winui ? 9.0f : 12.0f;
    layout.indicator_size = indicator && indicator->size ? *indicator->size : (winui ? 19.0f : 18.0f);
    layout.padding = root && root->padding ? *root->padding : Insets{winui ? 0.0f : 12.0f, 0, 12, 0};
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
    return {content.x, content.y + std::max(0.0f, (content.height - layout.indicator_size) / 2),
        layout.indicator_size, layout.indicator_size};
}
Rect Toggle::content_bounds(Rect bounds) const {
    const auto layout = layout_metrics();
    return inset_rect(inset_rect(bounds, layout.border), layout.padding);
}
Rect Toggle::label_bounds(Rect bounds) const {
    auto content = content_bounds(bounds);
    const auto layout = layout_metrics();
    const float prefix = layout.indicator_size + layout.gap;
    content.x += prefix;
    content.width = std::max(0.0f, content.width - prefix);
    return content;
}
Rect Toggle::mark_bounds(Rect bounds) const {
    return inset_rect(indicator_bounds(bounds), layout_metrics().indicator_border);
}
Size Toggle::measure(Size available) {
    if (!visible()) return {};
    const auto* root = effective_style_values(StylePart::root);
    const auto* indicator = effective_style_values(StylePart::indicator);
    const bool layout_affecting = (root && (root->padding || root->border_thickness)) || (indicator && indicator->size);
    if (!auto_size() || !layout_affecting) return Control::measure(available);
    const auto text = measured_text();
    const auto layout = layout_metrics();
    const float width = layout.padding.left + layout.border.left + layout.indicator_size + layout.gap + text.width +
        layout.padding.right + layout.border.right;
    const float height = std::max(text.height, layout.indicator_size) + layout.padding.top + layout.padding.bottom +
        layout.border.top + layout.border.bottom;
    return constrain({width, height}, available);
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
void TabStrip::set_new_tab_button_visible(bool visible) {
    if (new_button_visible_ == visible) return;
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
    for (std::size_t i = first_; i < tabs_.size(); ++i) {
        const auto tab = tab_bounds(i);
        if (tab.width <= 0) break;
        right = tab.x + tab.width;
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
        if (tabs[i].icon < ButtonIcon::none || tabs[i].icon > ButtonIcon::drive ||
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
    tabs_ = std::move(tabs);
    ++tabs_revision_;
    selected_ = selected;
    reveal_selected();
    arrange_new_button();
    invalidate(new_button_visible_ ? Invalidation::layout : Invalidation::paint);
}
bool TabStrip::select(std::uint64_t id) {
    if (!enabled() || std::none_of(tabs_.begin(), tabs_.end(), [&](const auto& tab) { return tab.id == id; })) return false;
    if (selected_ == id) return true;
    selected_ = id;
    reveal_selected();
    arrange_new_button();
    invalidate(new_button_visible_ ? Invalidation::layout : Invalidation::paint);
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
    if (index >= tabs_.size() || index < first_) return {};
    const auto content = content_bounds();
    const auto viewport = tab_viewport_width();
    const auto* tab = effective_control_style_values(StylePart::tab);
    const auto width = std::min(tab && tab->width ? *tab->width : 180.0f,
        viewport / std::max(1.0f, std::min(3.0f, static_cast<float>(tabs_.size()))));
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
    for (std::size_t i = first_; i < tabs_.size(); ++i) {
        const auto rect = tab_bounds(i);
        if (x >= rect.x && x < rect.x + rect.width) return i;
    }
    return {};
}
std::optional<std::size_t> TabStrip::hit_test(Point point) const {
    const auto content = content_bounds();
    return point.y >= content.y && point.y < content.y + content.height ? hit_test(point.x) : std::nullopt;
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
    if (!selected_) return;
    for (std::size_t i = 0; i < tabs_.size(); ++i) if (tabs_[i].id == selected_) {
        if (i < first_) first_ = i;
        const auto* tab = effective_control_style_values(StylePart::tab);
        const float minimum = std::min({120.0f, tab && tab->width ? *tab->width : 180.0f, tab_viewport_width()});
        while (first_ < i && tab_bounds(i).width < minimum) ++first_;
        break;
    }
}
void TabStrip::arrange(Rect rect) { Element::arrange(rect); reveal_selected(); arrange_new_button(); }
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
    style_dragging_ = value;
    if (!invalidate_control_style_state()) invalidate(Invalidation::paint);
}
float SplitView::effective_divider_width() const {
    const auto* values = effective_control_style_values(StylePart::divider);
    return values ? values->width.value_or(divider_width) : divider_width;
}
Rect SplitView::pane_area() const { return layout_style::content(*this, bounds()); }
bool SplitView::expanded() const { return secondary_visible_ && pane_area().width >= 2 * minimum_pane_width + effective_divider_width(); }
Rect SplitView::divider() const {
    if (!expanded()) return {};
    const auto b = pane_area();
    const auto divider_extent = effective_divider_width();
    const float width = b.width - divider_extent;
    const float left = std::clamp(width * ratio_, minimum_pane_width, width - minimum_pane_width);
    return {b.x + left, b.y, divider_extent, b.height};
}
void SplitView::arrange(Rect rect) {
    Element::arrange(rect);
    const auto b = pane_area();
    const auto d = divider();
    first_->arrange(layout_style::content(*this, {b.x, b.y, expanded() ? d.x - b.x : b.width, b.height}, StylePart::first_pane));
    second_->arrange(layout_style::content(*this, {expanded() ? d.x + d.width : b.x + b.width, b.y,
        expanded() ? b.x + b.width - d.x - d.width : 0, expanded() ? b.height : 0}, StylePart::second_pane));
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
    ratio_ = value;
    invalidate(Invalidation::layout);
}
void SplitView::set_secondary_visible(bool value) {
    if (secondary_visible_ == value) return;
    secondary_visible_ = value;
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
