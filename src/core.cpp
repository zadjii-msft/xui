#include "xui/core.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <utility>

namespace xui {
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
void Element::adopt(const std::shared_ptr<Element>& child) {
    if (!child) throw std::invalid_argument("Content must not be null");
    if (!child->invalidation_->parent.expired())
        throw std::invalid_argument("Content already has a parent");
    for (auto ancestor = invalidation_; ancestor; ancestor = ancestor->parent.lock())
        if (ancestor == child->invalidation_) throw std::invalid_argument("Content must not contain a cycle");
    child->invalidation_->parent = invalidation_;
}

Stack::Stack(Axis axis) : axis_(axis) {}

void Stack::set_spacing(float spacing) {
    spacing = dimension(spacing);
    if (spacing_ == spacing) return;
    spacing_ = spacing;
    invalidate(Invalidation::layout);
}

void Stack::set_padding(Insets padding) {
    padding = {dimension(padding.left), dimension(padding.top),
        dimension(padding.right), dimension(padding.bottom)};
    if (padding_.left == padding.left && padding_.top == padding.top &&
        padding_.right == padding.right && padding_.bottom == padding.bottom) return;
    padding_ = padding;
    invalidate(Invalidation::layout);
}

void Stack::add(std::shared_ptr<Element> child, float flex) {
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
    available = normalized(available);
    const bool horizontal = axis_ == Axis::horizontal;
    const double main = horizontal ? available.width : available.height;
    const float cross = horizontal ? available.height : available.width;
    const double gaps = children_.empty() ? 0.0 :
        static_cast<double>(spacing_) * static_cast<double>(children_.size() - 1);
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
    available = normalized(available);
    const double padding_width = static_cast<double>(padding_.left) + padding_.right;
    const double padding_height = static_cast<double>(padding_.top) + padding_.bottom;
    const Size inner{dimension(available.width - padding_width),
        dimension(available.height - padding_height)};
    const auto sizes = layout_children(inner);
    double main = sizes.empty() ? 0.0 :
        static_cast<double>(spacing_) * static_cast<double>(sizes.size() - 1);
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
    Element::arrange(rectangle);
    rectangle = bounds();
    const float left = (std::min)(padding_.left, rectangle.width);
    const float top = (std::min)(padding_.top, rectangle.height);
    const Size inner{
        dimension(static_cast<double>(rectangle.width) - left - padding_.right),
        dimension(static_cast<double>(rectangle.height) - top - padding_.bottom)};
    const auto sizes = layout_children(inner);
    const bool horizontal = axis_ == Axis::horizontal;
    const double main_limit = horizontal ? inner.width : inner.height;
    double position = 0.0;
    for (std::size_t index = 0; index < children_.size(); ++index) {
        const float length = dimension((std::min)(
            static_cast<double>(horizontal ? sizes[index].width : sizes[index].height),
            (std::max)(0.0, main_limit - position)));
        const double x = static_cast<double>(rectangle.x) + left + (horizontal ? position : 0.0);
        const double y = static_cast<double>(rectangle.y) + top + (horizontal ? 0.0 : position);
        children_[index].element->arrange({coordinate(x), coordinate(y),
            horizontal ? length : inner.width, horizontal ? inner.height : length});
        position = (std::min)(main_limit, position + length + spacing_);
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
