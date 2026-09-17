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
    return constrain(preferred_, available);
}

Size Element::constrain(Size desired, Size available) const {
    available = normalized(available);
    desired = normalized(desired);
    return {std::min(available.width, std::clamp(desired.width, minimum_.width, maximum_.width)),
        std::min(available.height, std::clamp(desired.height, minimum_.height, maximum_.height))};
}

void Element::arrange(Rect bounds) {
    bounds_ = {coordinate(bounds.x), coordinate(bounds.y),
        std::min(dimension(bounds.width), maximum_.width), std::min(dimension(bounds.height), maximum_.height)};
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
    if (preferred_explicit_) return;
    size = normalized(size);
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
    if (dynamic_cast<ContentHost*>(this))
        throw std::logic_error("Change ContentHost content with Window::replace_content");
    if (!child) throw std::invalid_argument("Stack child must not be null");
    if (!child->invalidation_->parent.expired()) {
        throw std::invalid_argument("Stack child already has a parent");
    }
    for (auto ancestor = invalidation_; ancestor; ancestor = ancestor->parent.lock()) {
        if (ancestor == child->invalidation_) {
            throw std::invalid_argument("Stack must not contain a cycle");
        }
    }
    flex = std::isfinite(flex) && flex > 0.0f ? flex : 0.0f;
    children_.push_back({child, flex});
    child->invalidation_->parent = invalidation_;
    invalidate(Invalidation::layout);
}

std::vector<Size> Stack::layout_children(Size available) {
    const auto spacing = effective_spacing();
    available = normalized(available);
    const bool horizontal = axis_ == Axis::horizontal;
    const double main = horizontal ? available.width : available.height;
    const float cross = horizontal ? available.height : available.width;
    const double gaps = children_.empty() ? 0.0 :
        static_cast<double>(spacing) * static_cast<double>(children_.size() - 1);
    double remaining = (std::max)(0.0, main - gaps);
    double total_flex = 0.0;
    std::vector<Size> sizes(children_.size());
    for (std::size_t index = 0; index < children_.size(); ++index) {
        const auto& child = children_[index];
        if (child.flex > 0.0f && main < maximum) {
            total_flex += child.flex;
            continue;
        }
        const Size constraint = horizontal ? Size{dimension(remaining), cross} :
            Size{cross, dimension(remaining)};
        auto measured = normalized(child.element->measure(constraint));
        measured.width = (std::min)(measured.width, constraint.width);
        measured.height = (std::min)(measured.height, constraint.height);
        sizes[index] = measured;
        remaining = (std::max)(0.0, remaining -
            static_cast<double>(horizontal ? measured.width : measured.height));
    }
    for (std::size_t index = 0; index < children_.size(); ++index) {
        const auto& child = children_[index];
        if (child.flex <= 0.0f || main >= maximum) continue;
        const float share = dimension(remaining * (static_cast<double>(child.flex) / total_flex));
        const Size constraint = horizontal ? Size{share, cross} : Size{cross, share};
        auto measured = normalized(child.element->measure(constraint));
        // Flex owns its main-axis allocation even when its preferred size is zero.
        sizes[index] = horizontal ?
            Size{share, (std::min)(measured.height, cross)} :
            Size{(std::min)(measured.width, cross), share};
    }
    return sizes;
}

Size Stack::measure(Size available) {
    if (preferred_size_explicit() && !auto_size()) return Element::measure(available);
    const auto padding = effective_layout_insets();
    const auto spacing = effective_spacing();
    available = normalized(available);
    const double padding_width = static_cast<double>(padding.left) + padding.right;
    const double padding_height = static_cast<double>(padding.top) + padding.bottom;
    const Size inner{dimension(available.width - padding_width),
        dimension(available.height - padding_height)};
    const auto sizes = layout_children(inner);
    double main = sizes.empty() ? 0.0 :
        static_cast<double>(spacing) * static_cast<double>(sizes.size() - 1);
    double cross = 0.0;
    for (const auto size : sizes) {
        main += axis_ == Axis::horizontal ? size.width : size.height;
        cross = (std::max)(cross, static_cast<double>(
            axis_ == Axis::horizontal ? size.height : size.width));
    }
    const Size desired = axis_ == Axis::horizontal ?
        Size{dimension(main + padding_width), dimension(cross + padding_height)} :
        Size{dimension(cross + padding_width), dimension(main + padding_height)};
    return constrain(desired, available);
}

void Stack::arrange(Rect rectangle) {
    const auto padding = effective_layout_insets();
    const auto spacing = effective_spacing();
    Element::arrange(rectangle);
    rectangle = bounds();
    const float left = (std::min)(padding.left, rectangle.width);
    const float top = (std::min)(padding.top, rectangle.height);
    const Size inner{
        dimension(static_cast<double>(rectangle.width) - left - padding.right),
        dimension(static_cast<double>(rectangle.height) - top - padding.bottom)};
    const auto sizes = layout_children(inner);
    const bool horizontal = axis_ == Axis::horizontal;
    const double main_limit = horizontal ? inner.width : inner.height;
    double position = 0.0;
    const auto* style = effective_control_style_values(StylePart::root);
    const auto alignment = style ? (horizontal ? style->horizontal_alignment : style->vertical_alignment) : std::nullopt;
    if (alignment && (*alignment == StyleAlignment::center || *alignment == StyleAlignment::end)) {
        double total = sizes.empty() ? 0 : spacing * (sizes.size() - 1);
        for (const auto& size : sizes) total += horizontal ? size.width : size.height;
        position = std::max(0.0, main_limit - total) / (*alignment == StyleAlignment::center ? 2 : 1);
    }
    for (std::size_t index = 0; index < children_.size(); ++index) {
        const float length = dimension((std::min)(
            static_cast<double>(horizontal ? sizes[index].width : sizes[index].height),
            (std::max)(0.0, main_limit - position)));
        const double x = static_cast<double>(rectangle.x) + left + (horizontal ? position : 0.0);
        const double y = static_cast<double>(rectangle.y) + top + (horizontal ? 0.0 : position);
        Rect child_bounds{coordinate(x), coordinate(y), horizontal ? length : inner.width, horizontal ? inner.height : length};
        if (style) {
            auto cross_style = *style;
            if (horizontal) cross_style.horizontal_alignment.reset();
            else cross_style.vertical_alignment.reset();
            child_bounds = layout_style::aligned(child_bounds, sizes[index], &cross_style);
        }
        children_[index].element->arrange(child_bounds);
        position = (std::min)(main_limit, position + length + spacing);
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
