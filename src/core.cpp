#include "xui/core.hpp"
#include "xui/control_styling.hpp"
#include "layout_styling.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <utility>

namespace xui {
StyleStateMask Element::control_style_state_bits() const {
    return control_style_ && !control_style_->context_enabled() ? style_states::disabled : 0;
}
StyleStateMask Element::effective_control_style_state_bits() const {
    const auto target = control_style_target();
    if (!target) return 0;
    StyleStateMask supported{};
    for (const auto& part : control_style_schema(*target).parts) supported |= part.states;
    return control_style_state_bits() & supported;
}
bool Element::invalidate_control_style_state() {
    if (!control_style_) return false;
    control_style_changed(control_style_->state_changed(effective_control_style_state_bits()));
    return true;
}
void Element::set_control_style(std::shared_ptr<const ControlStyle> style) {
    const auto target = control_style_target();
    if (style && (!target || *target != style->target()))
        throw std::invalid_argument("Element does not support this style target");
    if (!control_style_ && !style) return;
    if (control_style_ && control_style_->style() == style) return;
    auto next = control_style_ ? nullptr : std::make_unique<ControlStyleAttachment>(*target);
    const auto result = (next ? next.get() : control_style_.get())->assign_style(std::move(style), effective_control_style_state_bits());
    if (next) control_style_ = std::move(next);
    if (control_style_->fully_empty()) control_style_.reset();
    if (result) control_style_changed(*result);
}
std::shared_ptr<const ControlStyle> Element::control_style() const {
    return control_style_ ? control_style_->style() : nullptr;
}
void Element::set_control_style_values(StylePart part, PartStyleValues values) {
    const auto target = control_style_target();
    if (!target) throw std::invalid_argument("Element does not support control styling");
    validate_part_values(*target, part, values);
    if (!control_style_ && values.empty()) return;
    auto next = control_style_ ? nullptr : std::make_unique<ControlStyleAttachment>(*target);
    const auto result = (next ? next.get() : control_style_.get())->assign_local(part, std::move(values), effective_control_style_state_bits());
    if (next) control_style_ = std::move(next);
    if (control_style_->fully_empty()) control_style_.reset();
    if (result) control_style_changed(*result);
}
void Element::set_control_style_projection(StylePart part, PartStyleValues values) {
    const auto target = control_style_target();
    if (!target) throw std::invalid_argument("Element does not support control styling");
    validate_part_values(*target, part, values);
    if (!control_style_ && values.empty()) return;
    auto next = control_style_ ? nullptr : std::make_unique<ControlStyleAttachment>(*target);
    const auto result = (next ? next.get() : control_style_.get())->assign_projection(
        part, std::move(values), effective_control_style_state_bits());
    if (next) control_style_ = std::move(next);
    if (control_style_->fully_empty()) control_style_.reset();
    if (result) control_style_changed(*result);
}
const PartStyleValues& Element::control_style_projection_values(StylePart part) const {
    if (control_style_) return control_style_->projection(part);
    return control_style_values(part);
}
PartStyleValues Element::own_control_style_values(StylePart part) const {
    const auto target = control_style_target();
    if (!target) throw std::invalid_argument("Element does not support control styling");
    validate_part(*target, part);
    const auto mask = effective_control_style_state_bits();
    return control_style_ ? control_style_->resolve_transient(part, mask, mask, false) : PartStyleValues{};
}
const PartStyleValues& Element::control_style_values(StylePart part) const {
    static const PartStyleValues empty_values;
    if (control_style_) return control_style_->local(part);
    const auto target = control_style_target();
    if (!target) throw std::invalid_argument("Element does not support control styling");
    validate_part(*target, part);
    return empty_values;
}
const PartStyleValues* Element::effective_control_style_values(StylePart part) const {
    if (control_style_) return control_style_->effective(part, effective_control_style_state_bits());
    const auto target = control_style_target();
    if (!target) throw std::invalid_argument("Element does not support control styling");
    validate_part(*target, part);
    return nullptr;
}
PartStyleValues Element::resolve_control_style_part(StylePart part, StyleStateMask item_state) const {
    const auto target = control_style_target();
    if (!target) throw std::invalid_argument("Element does not support control styling");
    validate_part(*target, part);
    if (control_style_) return control_style_->resolve_transient(part, item_state, effective_control_style_state_bits());
    const auto& schema = control_style_schema(*target);
    StyleStateMask supported{};
    for (const auto& p : schema.parts) supported |= p.states;
    if (item_state & ~supported) throw std::invalid_argument("Unsupported transient style state");
    return {};
}
void Element::set_control_style_context_enabled(bool enabled) {
    if (!control_style_ || control_style_->context_enabled() == enabled) return;
    control_style_->set_context_enabled(enabled);
    invalidate_control_style_state();
}
namespace {

constexpr float maximum = (std::numeric_limits<float>::max)();

float dimension(double value) {
    if (!(value > 0.0)) return 0.0f;
    return static_cast<float>((std::min)(value, static_cast<double>(maximum)));
}

float coordinate(double value) {
    if (std::isnan(value)) return 0.0f;
    return static_cast<float>(std::clamp(value,
        -static_cast<double>(maximum), static_cast<double>(maximum)));
}

Size normalized(Size size) {
    return {dimension(size.width), dimension(size.height)};
}

double valid_row_height(float height) {
    return std::isfinite(height) && height > 0.0f ? static_cast<double>(height) : 0.0;
}

std::size_t bounded_index(double value, std::size_t count) {
    if (!(value > 0.0)) return 0;
    // Check before casting: the floating representation of SIZE_MAX can round up.
    if (value >= static_cast<double>(count)) return count;
    return static_cast<std::size_t>(value);
}

} // namespace

struct Element::InvalidationState {
    std::function<void(Invalidation)> callback;
    std::weak_ptr<InvalidationState> parent;

    void notify(Invalidation kind) {
        const auto listener = callback;
        const auto owner = parent.lock();
        if (listener) listener(kind);
        if (owner) owner->notify(kind);
    }
};
struct Element::AxisConstraintState {
    std::optional<AxisConstraints> width, height;
};

Element::Element() : invalidation_(std::make_shared<InvalidationState>()) {
    static std::atomic<std::uint64_t> next{1};
    auto candidate = next.load(std::memory_order_relaxed);
    for (;;) {
        if (candidate == (std::numeric_limits<std::uint64_t>::max)()) {
            throw std::overflow_error("Element ID space exhausted");
        }
        if (next.compare_exchange_weak(candidate, candidate + 1,
            std::memory_order_relaxed)) {
            id_ = candidate;
            break;
        }
    }
}

Element::~Element() {
    invalidation_->callback = {};
    invalidation_->parent.reset();
}
std::uint64_t Element::id() const { return id_; }

Size Element::measure(Size available) {
    return measure_axes(available, [&](Size offered, bool natural) { return base_measure(offered, natural); });
}
Size Element::measure_with_context(Size available, LayoutContext) { return measure(available); }
Size Element::base_measure(Size available, bool natural) const {
    return constrain_measure(natural ? default_size_ : preferred_, available, natural);
}
Size Element::constrain_measure(Size desired, Size available, bool natural) const {
    if (!natural) return constrain(desired, available);
    desired = normalized(desired); available = normalized(available);
    return {std::min(desired.width, available.width), std::min(desired.height, available.height)};
}
bool Element::supports_axis_constraints() const { return typeid(*this) == typeid(Element); }
std::optional<AxisConstraints> Element::width_constraints() const {
    return axis_constraints_ ? axis_constraints_->width : std::nullopt;
}
std::optional<AxisConstraints> Element::height_constraints() const {
    return axis_constraints_ ? axis_constraints_->height : std::nullopt;
}
void Element::set_axis_constraints(std::optional<AxisConstraints> width, std::optional<AxisConstraints> height) {
    if (!supports_axis_constraints()) throw std::invalid_argument("This element does not support axis constraints");
    for (const auto& axis : {width, height}) if (axis) {
        const auto upper = axis->maximum.value_or((std::numeric_limits<float>::max)());
        if (!std::isfinite(axis->minimum) || axis->minimum < 0 || !std::isfinite(upper) ||
            upper < axis->minimum || (axis->length && (!std::isfinite(*axis->length) ||
                *axis->length < axis->minimum || *axis->length > upper)))
            throw std::invalid_argument("Axis constraints require finite consistent lengths and bounds");
    }
    if (width_constraints() == width && height_constraints() == height) return;
    auto next = width || height ? std::make_unique<AxisConstraintState>(AxisConstraintState{width, height}) : nullptr;
    axis_constraints_ = std::move(next);
    invalidate(Invalidation::layout);
}
Size Element::axis_measurement_available(Size available) const {
    available = normalized(available);
    const auto extent = [](float offered, float legacy_maximum, const auto& axis) {
        return std::min(offered, axis ? axis->length.value_or(axis->maximum.value_or(
            (std::numeric_limits<float>::max)())) : legacy_maximum);
    };
    return {extent(available.width, maximum_.width, axis_constraints_->width),
        extent(available.height, maximum_.height, axis_constraints_->height)};
}
Size Element::resolve_axis_measurement(Size legacy, Size natural, Size available) const {
    available = normalized(available); natural = normalized(natural);
    const auto extent = [](float inherited, float measured, float offered, const auto& axis) {
        return axis ? std::min(offered, std::clamp(axis->length.value_or(measured), axis->minimum,
            axis->maximum.value_or((std::numeric_limits<float>::max)()))) : inherited;
    };
    return {extent(legacy.width, natural.width, available.width, axis_constraints_->width),
        extent(legacy.height, natural.height, available.height, axis_constraints_->height)};
}

Size Element::constrain(Size desired, Size available) const {
    available = normalized(available);
    desired = normalized(desired);
    return {std::min(available.width, std::clamp(desired.width, minimum_.width, maximum_.width)),
        std::min(available.height, std::clamp(desired.height, minimum_.height, maximum_.height))};
}

void Element::arrange(Rect bounds) {
    const auto limit = [](float inherited, const auto& axis) {
        return axis ? axis->length.value_or(axis->maximum.value_or((std::numeric_limits<float>::max)())) : inherited;
    };
    const auto maximum_width = axis_constraints_ ? limit(maximum_.width, axis_constraints_->width) : maximum_.width;
    const auto maximum_height = axis_constraints_ ? limit(maximum_.height, axis_constraints_->height) : maximum_.height;
    bounds_ = {coordinate(bounds.x), coordinate(bounds.y),
        std::min(dimension(bounds.width), maximum_width), std::min(dimension(bounds.height), maximum_height)};
}
void Element::arrange_with_context(Rect bounds, LayoutContext) { arrange(bounds); }
void Element::arrange_unbounded(Rect bounds, Axis axis) {
    arrange_with_context(bounds, {axis == Axis::horizontal, axis == Axis::vertical});
}
bool Element::fixed_arrangement_axis(Axis axis) const {
    const auto constraint = axis == Axis::horizontal ? width_constraints() : height_constraints();
    return constraint ? constraint->length.has_value() : preferred_explicit_ && !auto_size_;
}
LayoutContext Element::constrain_layout_context(LayoutContext context) const {
    if (fixed_arrangement_axis(Axis::horizontal)) context.unbounded_width = false;
    if (fixed_arrangement_axis(Axis::vertical)) context.unbounded_height = false;
    return context;
}

Rect Element::bounds() const { return bounds_; }

void Element::set_invalidator(std::function<void(Invalidation)> callback) {
    invalidation_->callback = std::move(callback);
}

void Element::invalidate(Invalidation kind) {
    const auto state = invalidation_;
    state->notify(kind);
}

void Element::set_preferred_size(Size size) {
    const bool was_explicit = preferred_explicit_;
    preferred_explicit_ = true;
    size = normalized(size);
    if (was_explicit && !auto_size_ && preferred_.width == size.width && preferred_.height == size.height) return;
    auto_size_ = false;
    preferred_ = size;
    invalidate(Invalidation::layout);
}

void Element::set_default_size(Size size) {
    size = normalized(size);
    const bool changed = default_size_.width != size.width || default_size_.height != size.height;
    default_size_ = size;
    if (preferred_explicit_) {
        if (changed && has_axis_constraints()) invalidate(Invalidation::layout);
        return;
    }
    if (preferred_.width == size.width && preferred_.height == size.height) return;
    preferred_ = size;
    invalidate(Invalidation::layout);
}

void Element::set_auto_size(bool value) {
    if (auto_size_ == value) return;
    auto_size_ = value;
    invalidate(Invalidation::layout);
}
void Element::set_fixed_size(Size size) {
    preferred_explicit_ = true;
    size = normalized(size);
    preferred_ = minimum_ = maximum_ = size;
    auto_size_ = false;
    invalidate(Invalidation::layout);
}
void Element::set_minimum_size(Size size) {
    minimum_ = normalized(size);
    maximum_.width = std::max(maximum_.width, minimum_.width);
    maximum_.height = std::max(maximum_.height, minimum_.height);
    invalidate(Invalidation::layout);
}
void Element::set_maximum_size(Size size) {
    maximum_ = normalized(size);
    minimum_.width = std::min(minimum_.width, maximum_.width);
    minimum_.height = std::min(minimum_.height, maximum_.height);
    invalidate(Invalidation::layout);
}
void Element::validate_adoption(const std::shared_ptr<Element>& child) const {
    if (!child) throw std::invalid_argument("Content must not be null");
    if (!child->invalidation_->parent.expired())
        throw std::invalid_argument("Content already has a parent");
    for (auto ancestor = invalidation_; ancestor; ancestor = ancestor->parent.lock())
        if (ancestor == child->invalidation_) throw std::invalid_argument("Content must not contain a cycle");
}
void Element::adopt(const std::shared_ptr<Element>& child) {
    validate_adoption(child);
    child->invalidation_->parent = invalidation_;
}

Stack::Stack(Axis axis) : axis_(axis) {}

ContentHost::ContentHost(std::shared_ptr<Element> content) : Stack(Axis::vertical) {
    replace(std::move(content));
}
void ContentHost::arrange(Rect bounds) { arrange_with_context(bounds, {}); }
void ContentHost::arrange_with_context(Rect bounds, LayoutContext context) {
    Stack::arrange_with_context(bounds, context);
    const auto inner = layout_content_bounds();
    allocated_content_size_ = {inner.width, inner.height};
}
const std::shared_ptr<Element>& ContentHost::content() const noexcept {
    static const std::shared_ptr<Element> empty;
    return children_.empty() ? empty : children_.front().element;
}
void ContentHost::replace(std::shared_ptr<Element> content) {
    if (this->content() == content) return;
    if (content) validate_adoption(content);
    children_.reserve(1);
    if (!children_.empty()) children_.front().element->invalidation_->parent.reset();
    children_.clear();
    if (content) {
        content->invalidation_->parent = invalidation_;
        children_.push_back({std::move(content), 1});
    }
    invalidate(Invalidation::layout);
}

std::optional<StyleTarget> Stack::control_style_target() const { return StyleTarget::stack; }
Insets Stack::effective_layout_insets() const {
    auto result = layout_style::insets(effective_control_style_values(StylePart::root), padding_, padding_explicit_);
    result.bottom += effective_separator_inset();
    return result;
}
float Stack::effective_separator_inset() const {
    if (!separator_inset_enabled_) return 0;
    if (const auto* separator = effective_separator_style()) {
        for (const auto& part : control_style_schema(*control_style_target()).parts)
            if (part.part == StylePart::separator && (part.allowed & style_property(StyleProperty::thickness))) {
                return separator->thickness.value_or(1.0f);
            }
    }
    return 0;
}
void Stack::set_separator_inset_enabled(bool value) {
    if (separator_inset_enabled_ == value) return;
    separator_inset_enabled_ = value;
    invalidate(Invalidation::layout);
}
const PartStyleValues* Stack::effective_separator_style() const {
    if (!has_control_styling()) return nullptr;
    const auto target = control_style_target();
    if (!target) return nullptr;
    for (const auto& part : control_style_schema(*target).parts)
        if (part.part == StylePart::separator) return effective_control_style_values(StylePart::separator);
    return nullptr;
}
float Stack::effective_spacing() const {
    const auto* values = effective_control_style_values(StylePart::root);
    return !spacing_explicit_ && values && values->spacing ? *values->spacing : spacing_;
}
Rect Stack::layout_content_bounds() const { return layout_style::inset(bounds(), effective_layout_insets()); }

void Stack::set_default_spacing(float spacing) {
    if (spacing_explicit_) return;
    spacing = dimension(spacing);
    if (spacing_ == spacing) return;
    spacing_ = spacing;
    invalidate(Invalidation::layout);
}
void Stack::set_default_padding(Insets padding) {
    if (padding_explicit_) return;
    padding = {dimension(padding.left), dimension(padding.top),
        dimension(padding.right), dimension(padding.bottom)};
    if (padding_.left == padding.left && padding_.top == padding.top &&
        padding_.right == padding.right && padding_.bottom == padding.bottom) return;
    padding_ = padding;
    invalidate(Invalidation::layout);
}
void Stack::set_spacing(float spacing) {
    spacing = dimension(spacing);
    if (spacing_explicit_ && spacing_ == spacing) return;
    spacing_explicit_ = true;
    spacing_ = spacing;
    invalidate(Invalidation::layout);
}

void Stack::set_padding(Insets padding) {
    padding = {dimension(padding.left), dimension(padding.top),
        dimension(padding.right), dimension(padding.bottom)};
    if (padding_explicit_ && padding_.left == padding.left && padding_.top == padding.top &&
        padding_.right == padding.right && padding_.bottom == padding.bottom) return;
    padding_explicit_ = true;
    padding_ = padding;
    invalidate(Invalidation::layout);
}

void Stack::add(std::shared_ptr<Element> child, float flex) {
    insert(children_.size(), std::move(child), flex);
}
void Stack::insert(std::size_t index, std::shared_ptr<Element> child, float flex) {
    if (dynamic_cast<ContentHost*>(this))
        throw std::logic_error("Change ContentHost content with Window::replace_content");
    if (index > children_.size()) throw std::invalid_argument("Stack insertion index is out of range");
    validate_adoption(child);
    flex = std::isfinite(flex) && flex > 0.0f ? flex : 0.0f;
    children_.insert(children_.begin() + index, {child, flex});
    child->invalidation_->parent = invalidation_;
    invalidate(Invalidation::layout);
}
std::size_t Stack::index_of(const Element& child) const {
    const auto found = std::find_if(children_.begin(), children_.end(),
        [&](const auto& value) { return value.element.get() == &child; });
    if (found == children_.end()) throw std::invalid_argument("Element is not a direct Stack child");
    return static_cast<std::size_t>(found - children_.begin());
}
void Stack::remove(const Element& child) {
    const auto index = index_of(child);
    children_[index].element->invalidation_->parent.reset();
    children_.erase(children_.begin() + index);
    invalidate(Invalidation::layout);
}
void Stack::move(const Element& child, std::size_t index) {
    const auto previous = index_of(child);
    if (index >= children_.size()) throw std::invalid_argument("Stack move index is out of range");
    if (previous == index) return;
    if (previous < index)
        std::rotate(children_.begin() + previous, children_.begin() + previous + 1, children_.begin() + index + 1);
    else
        std::rotate(children_.begin() + index, children_.begin() + previous, children_.begin() + previous + 1);
    invalidate(Invalidation::layout);
}

struct Stack::LayoutScratch {
    std::vector<Size>& storage;
    std::vector<Size> sizes;

    explicit LayoutScratch(std::vector<Size>& storage) : storage(storage) {
        sizes.swap(storage);
    }
    ~LayoutScratch() {
        // Nested layout must not overwrite an outer pass, even during exception unwinding.
        if (sizes.capacity() > storage.capacity()) sizes.swap(storage);
    }
    LayoutScratch(const LayoutScratch&) = delete;
    LayoutScratch& operator=(const LayoutScratch&) = delete;
};

std::size_t Stack::layout_children(Size available, std::vector<Size>& sizes, LayoutContext context) {
    const auto spacing = effective_spacing();
    available = normalized(available);
    const bool horizontal = axis_ == Axis::horizontal;
    const double main = horizontal ? available.width : available.height;
    const float cross = horizontal ? available.height : available.width;
    const bool natural_main = horizontal ? context.unbounded_width : context.unbounded_height;
    const auto visible_count = static_cast<std::size_t>(std::count_if(children_.begin(), children_.end(),
        [](const auto& child) { return child.element->participates_in_layout(); }));
    const double gaps = visible_count ? static_cast<double>(spacing) * (visible_count - 1) : 0.0;
    double remaining = (std::max)(0.0, main - gaps);
    double total_flex = 0.0;
    sizes.resize(children_.size());
    for (std::size_t index = 0; index < children_.size(); ++index) {
        const auto& child = children_[index];
        if (!child.element->participates_in_layout()) { sizes[index] = {}; continue; }
        if (!natural_main && child.flex > 0.0f && main < maximum) {
            total_flex += child.flex;
            continue;
        }
        const auto offered = dimension(remaining);
        const Size constraint = horizontal ? Size{offered, cross} : Size{cross, offered};
        auto measured = normalized(child.element->measure_with_context(constraint, context));
        measured.width = (std::min)(measured.width, horizontal ? dimension(remaining) : cross);
        measured.height = (std::min)(measured.height, horizontal ? cross : dimension(remaining));
        sizes[index] = measured;
        remaining = (std::max)(0.0, remaining -
            static_cast<double>(horizontal ? measured.width : measured.height));
    }
    for (std::size_t index = 0; index < children_.size(); ++index) {
        const auto& child = children_[index];
        if (!child.element->participates_in_layout() || natural_main || child.flex <= 0.0f || main >= maximum) continue;
        const float share = dimension(remaining * (static_cast<double>(child.flex) / total_flex));
        const Size constraint = horizontal ? Size{share, cross} : Size{cross, share};
        auto measured = normalized(child.element->measure_with_context(constraint, context));
        // Flex owns its main-axis allocation even when its preferred size is zero.
        sizes[index] = horizontal ?
            Size{share, (std::min)(measured.height, cross)} :
            Size{(std::min)(measured.width, cross), share};
    }
    return visible_count;
}

Size Stack::measure(Size available) {
    const auto context = constrain_layout_context({available.width >= maximum, available.height >= maximum});
    return measure_axes(available, [&](Size offered, bool natural) { return measure_content(offered, natural, context); });
}
Size Stack::measure_with_context(Size available, LayoutContext context) {
    if (typeid(*this) != typeid(Stack) && typeid(*this) != typeid(ContentHost))
        return Element::measure_with_context(available, context);
    context = constrain_layout_context(context);
    return measure_axes(available, [&](Size offered, bool natural) { return measure_content(offered, natural, context); });
}
bool Stack::supports_axis_constraints() const {
    return typeid(*this) == typeid(Stack) || typeid(*this) == typeid(ContentHost);
}
Size Stack::measure_content(Size available, bool natural, LayoutContext context) {
    if (!natural && preferred_size_explicit() && !auto_size()) return base_measure(available, false);
    const auto padding = effective_layout_insets();
    const auto spacing = effective_spacing();
    available = normalized(available);
    const double padding_width = static_cast<double>(padding.left) + padding.right;
    const double padding_height = static_cast<double>(padding.top) + padding.bottom;
    const Size inner{dimension(available.width - padding_width),
        dimension(available.height - padding_height)};
    LayoutScratch scratch(layout_sizes_);
    auto& sizes = scratch.sizes;
    const auto visible_count = layout_children(inner, sizes, context);
    double main = visible_count ? static_cast<double>(spacing) * (visible_count - 1) : 0.0;
    double cross = 0.0;
    for (const auto size : sizes) {
        main += axis_ == Axis::horizontal ? size.width : size.height;
        cross = (std::max)(cross, static_cast<double>(
            axis_ == Axis::horizontal ? size.height : size.width));
    }
    const Size desired = axis_ == Axis::horizontal ?
        Size{dimension(main + padding_width), dimension(cross + padding_height)} :
        Size{dimension(cross + padding_width), dimension(main + padding_height)};
    return constrain_measure(desired, available, natural);
}

void Stack::arrange(Rect rectangle) {
    arrange_children(rectangle, {});
}
void Stack::arrange_with_context(Rect rectangle, LayoutContext context) {
    if (typeid(*this) != typeid(Stack) && typeid(*this) != typeid(ContentHost))
        return Element::arrange_with_context(rectangle, context);
    arrange_children(rectangle, constrain_layout_context(context));
}
void Stack::arrange_children(Rect rectangle, LayoutContext context) {
    const auto padding = effective_layout_insets();
    const auto spacing = effective_spacing();
    Element::arrange(rectangle);
    rectangle = bounds();
    const float left = (std::min)(padding.left, rectangle.width);
    const float top = (std::min)(padding.top, rectangle.height);
    const Size inner{
        dimension(static_cast<double>(rectangle.width) - left - padding.right),
        dimension(static_cast<double>(rectangle.height) - top - padding.bottom)};
    LayoutScratch scratch(layout_sizes_);
    auto& sizes = scratch.sizes;
    const auto visible_count = layout_children(inner, sizes, context);
    const bool horizontal = axis_ == Axis::horizontal;
    const double main_limit = horizontal ? inner.width : inner.height;
    double position = 0.0;
    const auto* style = effective_control_style_values(StylePart::root);
    const auto alignment = style ? (horizontal ? style->horizontal_alignment : style->vertical_alignment) : std::nullopt;
    if (alignment && (*alignment == StyleAlignment::center || *alignment == StyleAlignment::end)) {
        double total = visible_count ? static_cast<double>(spacing) * (visible_count - 1) : 0;
        for (const auto& size : sizes) total += horizontal ? size.width : size.height;
        position = std::max(0.0, main_limit - total) / (*alignment == StyleAlignment::center ? 2 : 1);
    }
    bool previous_visible{};
    for (std::size_t index = 0; index < children_.size(); ++index) {
        if (!children_[index].element->participates_in_layout()) {
            const double x = static_cast<double>(rectangle.x) + left + (horizontal ? position : 0.0);
            const double y = static_cast<double>(rectangle.y) + top + (horizontal ? 0.0 : position);
            children_[index].element->arrange_with_context({coordinate(x), coordinate(y), 0, 0}, context);
            continue;
        }
        if (previous_visible) position = (std::min)(main_limit, position + spacing);
        const float length = dimension((std::min)(
            static_cast<double>(horizontal ? sizes[index].width : sizes[index].height),
            (std::max)(0.0, main_limit - position)));
        const double x = static_cast<double>(rectangle.x) + left + (horizontal ? position : 0.0);
        const double y = static_cast<double>(rectangle.y) + top + (horizontal ? 0.0 : position);
        Rect child_bounds{coordinate(x), coordinate(y), horizontal ? length : inner.width, horizontal ? inner.height : length};
        child_bounds = layout_style::aligned_cross(child_bounds, sizes[index], style, axis_);
        children_[index].element->arrange_with_context(child_bounds, context);
        position = (std::min)(main_limit, position + length);
        previous_visible = true;
    }
}

void FileListModel::set_items(std::shared_ptr<const std::vector<FileItem>> items) {
    set_view(FilteredView::build(FileSnapshot::build(std::move(items)), view_->query()));
}

void FileListModel::set_filter(std::wstring filter) {
    set_view(FilteredView::build(view_->source(), std::move(filter)));
}

void FileListModel::set_view(std::shared_ptr<const FilteredView> view) {
    if (!view) throw std::invalid_argument("Filtered view must not be null");
    view_ = std::move(view);
    if (selected_ && !view_->source()->find(*selected_)) selected_.reset();
}

const std::vector<RowIndex>& FileListModel::visible_indices() const { return view_->indices(); }
std::shared_ptr<const std::vector<FileItem>> FileListModel::items() const { return view_->source()->items(); }
std::optional<ItemId> FileListModel::selected_id() const { return selected_; }

std::optional<std::size_t> FileListModel::selected_index() const {
    return selected_ ? view_->find(*selected_) : std::nullopt;
}

const FileItem* FileListModel::selected_item() const {
    if (selected_)
        if (const auto index = view_->source()->find(*selected_)) return &(*items())[*index];
    return nullptr;
}

void FileListModel::select_index(std::size_t index) {
    if (index < visible_indices().size()) selected_ = (*items())[visible_indices()[index]].id;
}

void FileListModel::clear_selection() { selected_.reset(); }

void FileListModel::move_selection(int delta) {
    const auto& visible = visible_indices();
    if (delta == 0 || visible.empty()) return;
    const auto current = selected_index();
    if (!current) {
        select_index(delta > 0 ? 0 : visible.size() - 1);
        return;
    }
    if (delta > 0) {
        const auto distance = static_cast<std::size_t>(delta);
        select_index(*current + (std::min)(distance, visible.size() - 1 - *current));
    } else {
        const auto distance = static_cast<std::size_t>(-static_cast<std::int64_t>(delta));
        select_index(*current - (std::min)(distance, *current));
    }
}

void FileListModel::select_first() { select_index(0); }
void FileListModel::select_last() {
    if (!visible_indices().empty()) select_index(visible_indices().size() - 1);
}

float clamp_scroll(std::size_t count, float row_height, float offset, float viewport_height) {
    const double height = valid_row_height(row_height);
    const double total = static_cast<double>(count) * height;
    const double limit = (std::max)(0.0, total - dimension(viewport_height));
    return dimension((std::min)(static_cast<double>(dimension(offset)), limit));
}

VisibleRange visible_range(std::size_t count, float row_height, float offset,
    float viewport_height, std::size_t overscan) {
    const double height = valid_row_height(row_height);
    const float viewport = dimension(viewport_height);
    if (count == 0 || height == 0.0 || viewport == 0.0f) return {};
    const double scroll = clamp_scroll(count, row_height, offset, viewport);
    const auto begin = bounded_index(std::floor(scroll / height), count);
    const auto end = bounded_index(std::ceil((scroll + viewport) / height), count);
    return {begin - (std::min)(begin, overscan),
        end + (std::min)(count - end, overscan)};
}

float reveal_row(std::size_t index, float row_height, float offset, float viewport_height) {
    const double height = valid_row_height(row_height);
    if (height == 0.0) return 0.0f;
    const double start = static_cast<double>(index) * height;
    const double viewport = dimension(viewport_height);
    const double scroll = dimension(offset);
    // A row taller than the viewport is aligned at its top.
    if (start < scroll || height > viewport) return dimension(start);
    if (start + height > scroll + viewport) return dimension(start + height - viewport);
    return dimension(scroll);
}

} // namespace xui
