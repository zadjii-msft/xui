#include "xui/miller_columns.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <stdexcept>

namespace xui {
namespace {
// Flatten hierarchy for list accessibility without copying or enumerating the source.
class Siblings final : public ItemsSource {
public:
    explicit Siblings(std::shared_ptr<const ItemsSource> source) : source_(std::move(source)) {}
    std::size_t size() const override { return source_->size(); }
    ItemKey key(std::size_t index) const override { return source_->key(index); }
    std::optional<std::size_t> find(ItemKey key) const override { return source_->find(key); }
    bool selectable(std::size_t index) const override {
        return source_->selectable(index) && !source_->hierarchy(index).group;
    }
    ItemContent item(std::size_t index) const override { return source_->item(index); }
    ItemVisual visual(std::size_t index) const override { return source_->visual(index); }
    double row_start(std::size_t index, double height) const override { return source_->row_start(index, height); }
    std::size_t row_at(double offset, double height) const override { return source_->row_at(offset, height); }
private:
    std::shared_ptr<const ItemsSource> source_;
};
bool valid_item(const std::shared_ptr<const ItemsSource>& source, ItemKey key) {
    const auto row = source ? source->find(key) : std::nullopt;
    return row && *row < source->size() && source->key(*row) == key &&
        source->selectable(*row) && !source->hierarchy(*row).group && source->item(*row).enabled;
}
}

MillerColumnList::MillerColumnList(MillerColumns& owner, std::size_t index) :
    VirtualCollection(ControlRole::items_view, L"Column items"), owner_(&owner), index_(index) {
    set_item_size({240, 40});
    on_activate([this](ItemKey key) { if (owner_) owner_->activate_item(index_, key); });
}
bool MillerColumnList::available(ItemKey key) const {
    return owner_ && owner_->enabled() && owner_->visible() && enabled() &&
        index_ < owner_->columns().size() && valid_item(source_, key);
}
void MillerColumnList::replace(const MillerColumn& column) {
    hover_pointer({});
    const auto old_focus = selection_.focused();
    const auto old_selected = selection_.selected_keys(source_, 1);
    const bool changed_source = items_ != column.source;
    selection_.clear();
    if (changed_source) {
        items_ = column.source;
        set_source(items_ ? std::make_shared<Siblings>(items_) : nullptr);
    }
    if (column.selected) selection_.select(source_, *column.selected, SelectionGesture::replace);
    else if (old_focus && valid_item(source_, *old_focus)) selection_.set_focus(old_focus);
    else selection_.set_focus(source_ && source_->size() ? std::optional{source_->key(0)} : std::nullopt);
    reveal_selection_ = column.selected && (changed_source || !old_selected || old_selected->size() != 1 ||
        old_selected->front() != *column.selected);
    set_offset(offset());
    invalidate(Invalidation::paint);
}
void MillerColumnList::arrange(Rect value) {
    if (value.x != bounds().x || value.y != bounds().y || value.width != bounds().width || value.height != bounds().height)
        hover_pointer({});
    VirtualCollection::arrange(value);
    if (reveal_selection_ && value.width > 0 && value.height > 0) {
        reveal_selection_ = false;
        if (const auto focus = selection_.focused()) reveal(*focus);
    }
}
bool MillerColumnList::select(ItemKey key, SelectionGesture gesture) {
    return available(key) && owner_->select_item(index_, key, gesture);
}
bool MillerColumnList::remove_selection(ItemKey key) {
    if (!available(key)) return false;
    if (owner_->columns_[index_].selected != key) return true;
    owner_->columns_[index_].selected.reset();
    selection_.clear();
    changed();
    return true;
}
bool MillerColumnList::prepare_context_menu(std::optional<Point> point) {
    if (point && (!std::isfinite(point->x) || !std::isfinite(point->y)))
        throw std::invalid_argument("Context menu coordinates must be finite");
    std::optional<ItemKey> key;
    if (point) {
        const auto row = hit_test(*point);
        if (row) key = source_->key(*row);
    } else key = selection_.focused();
    if (!key || !available(*key)) return false;
    owner_->set_active_column(index_);
    owner_->columns_[index_].selected = key;
    selection_.select(source_, *key, SelectionGesture::replace);
    reveal(*key);
    invalidate(Invalidation::paint);
    return true;
}
void MillerColumnList::step(int delta, SelectionGesture gesture) {
    if (!source_ || !source_->size() || !delta) return;
    const auto current = selection_.focused() ? source_->find(*selection_.focused()) : std::nullopt;
    const auto count = static_cast<std::int64_t>(source_->size());
    const auto start = current ? static_cast<std::int64_t>(*current) : delta < 0 ? count : -1;
    auto row = std::clamp(start + delta, std::int64_t{0}, count - 1);
    for (; row >= 0 && row < count; row += delta < 0 ? -1 : 1)
        if (select(source_->key(static_cast<std::size_t>(row)), gesture)) return;
}
void MillerColumnList::edge(bool last, SelectionGesture gesture) {
    if (!source_) return;
    for (std::size_t i = 0; i < source_->size(); ++i)
        if (select(source_->key(last ? source_->size() - 1 - i : i), gesture)) return;
}
void MillerColumnList::horizontal(bool right, SelectionGesture) {
    if (!owner_ || !enabled() || !owner_->enabled() || !owner_->visible() || index_ >= owner_->columns().size()) return;
    owner_->set_active_column(index_);
    owner_->move_active(right);
}
void MillerColumnList::set_presentation(ItemsPresentation value) {
    if (value != ItemsPresentation::list) throw std::invalid_argument("Miller columns require list presentation");
}
std::vector<CollectionRow> MillerColumnList::visible_content() const {
    auto rows = VirtualCollection::visible_content();
    for (auto& row : rows) row.content.submenu = items_->hierarchy(row.index).expandable;
    return rows;
}
void MillerColumnList::hover_pointer(std::optional<Point> point) {
    if (point && (!std::isfinite(point->x) || !std::isfinite(point->y)))
        throw std::invalid_argument("Hover coordinates must be finite");
    const auto previous = hovered_row();
    hover_pointer_ = point;
    if (previous != hovered_row()) invalidate(Invalidation::paint);
}
std::optional<std::size_t> MillerColumnList::hovered_row() const {
    if (!hover_pointer_ || !owner_ || !owner_->enabled() || !owner_->visible() || !enabled() || !visible()) return {};
    const auto row = hit_test(*hover_pointer_);
    return row && available(source_->key(*row)) ? row : std::nullopt;
}
void MillerColumnList::cancel() {
    hover_pointer({});
    Control::cancel();
}

MillerColumns::MillerColumns(std::wstring name) :
    Control(ControlRole::content_view, std::move(name), {720, 480}),
    previous_(std::make_shared<Button>(L"Previous column")),
    next_(std::make_shared<Button>(L"Next column")) {
    previous_->set_icon(ButtonIcon::back); next_->set_icon(ButtonIcon::forward);
    previous_->on_click([this] { move_active(false); });
    next_->on_click([this] { move_active(true); });
    children_ = {previous_, next_};
    for (std::size_t i = 0; i < maximum_columns; ++i) {
        auto header = std::make_shared<Label>(L"");
        header->set_body_strong(true);
        auto list = std::shared_ptr<MillerColumnList>(new MillerColumnList(*this, i));
        header->set_visible(false); list->set_visible(false);
        headers_.push_back(header); lists_.push_back(list);
        children_.push_back(header); children_.push_back(list);
    }
    for (const auto& child : children_) adopt(child);
    layout();
}
MillerColumns::~MillerColumns() {
    previous_->on_click({}); next_->on_click({});
    for (const auto& list : lists_) { list->on_activate({}); list->owner_ = nullptr; }
}
void MillerColumns::set_columns(std::vector<MillerColumn> value) {
    if (value.size() > maximum_columns) throw std::length_error("Miller columns support at most 32 columns");
    for (const auto& column : value) {
        if (column.title.size() > 1024) throw std::invalid_argument("Column titles support at most 1024 characters");
        if (column.source && column.source->size() > INT_MAX) throw std::length_error("Column sources support at most INT_MAX items");
        if (column.selected && !valid_item(column.source, *column.selected))
            throw std::invalid_argument("Selected identity must identify an enabled column item");
    }
    columns_ = std::move(value);
    for (std::size_t i = 0; i < maximum_columns; ++i) {
        const MillerColumn empty;
        const auto& column = i < columns_.size() ? columns_[i] : empty;
        headers_[i]->set_text(column.title);
        lists_[i]->set_name(column.title.empty() ? L"Column items" : column.title);
        lists_[i]->replace(column);
    }
    active_ = columns_.empty() ? 0 : std::min(active_, columns_.size() - 1);
    reveal_active(); layout(); invalidate(Invalidation::layout);
}
std::shared_ptr<MillerColumnList> MillerColumns::column_list(std::size_t index) const {
    if (index >= maximum_columns) throw std::invalid_argument("Column index is out of range");
    return lists_[index];
}
float MillerColumns::effective_width() const { return std::min(width_, bounds().width); }
double MillerColumns::maximum_horizontal() const {
    return std::max(0.0, static_cast<double>(effective_width()) * columns_.size() - bounds().width);
}
void MillerColumns::reveal_active() {
    offset_ = std::clamp(offset_, 0.0, maximum_horizontal());
    if (columns_.empty() || bounds().width <= 0) return;
    const auto left = static_cast<double>(active_) * effective_width();
    const auto right = left + effective_width();
    if (left < offset_) offset_ = left;
    else if (right > offset_ + bounds().width) offset_ = right - bounds().width;
}
void MillerColumns::set_active_column(std::size_t index) {
    if (index >= columns_.size()) throw std::invalid_argument("Active column index is out of range");
    active_ = index; reveal_active(); layout(); invalidate(Invalidation::layout);
}
void MillerColumns::set_column_width(float value) {
    if (!std::isfinite(value) || value < 120 || value > 2000)
        throw std::invalid_argument("Column width must be finite and between 120 and 2000 DIPs");
    if (width_ == value) return;
    width_ = value; reveal_active(); layout(); invalidate(Invalidation::layout);
}
void MillerColumns::set_horizontal_offset(double value) {
    if (!std::isfinite(value) || value < 0 || value > maximum_horizontal())
        throw std::invalid_argument("Horizontal offset is outside the column viewport");
    if (offset_ == value) return;
    offset_ = value; layout(); invalidate(Invalidation::layout);
}
void MillerColumns::scroll_horizontal(double delta) {
    if (!std::isfinite(delta)) throw std::invalid_argument("Horizontal scroll delta must be finite");
    set_horizontal_offset(std::clamp(offset_ + delta, 0.0, maximum_horizontal()));
}
Rect MillerColumns::horizontal_track() const {
    const auto area = bounds();
    if (maximum_horizontal() <= 0 || area.width <= 0 || area.height <= 0) return {};
    const float height = std::min(scrollbar_height, area.height);
    return {0, area.height - height, area.width, height};
}
Rect MillerColumns::horizontal_thumb() const {
    const auto track = horizontal_track();
    if (track.width <= 0) return {};
    const double extent = maximum_horizontal() + track.width;
    const float width = std::min(track.width, std::max(24.0f, static_cast<float>(track.width * track.width / extent)));
    return {static_cast<float>(offset_ / maximum_horizontal() * (track.width - width)),
        track.y + std::min(2.0f, track.height / 2), width, std::max(0.0f, track.height - 4)};
}
Rect MillerColumns::separator_bounds(std::size_t column) const {
    if (column >= columns_.size() || column + 1 >= columns_.size()) return {};
    const auto area = bounds();
    const float available = std::max(0.0f, area.height - horizontal_track().height);
    const float toolbar = std::min(32.0f, available);
    const float right = static_cast<float>((column + 1) * static_cast<double>(effective_width()) - offset_);
    const float left = std::max(0.0f, right - separator_width);
    const float clipped_right = std::min(area.width, right);
    return {left, toolbar, std::max(0.0f, clipped_right - left), available - toolbar};
}
void MillerColumns::move_active(bool right) {
    if (!enabled() || !visible() || columns_.empty() || (right ? active_ + 1 >= columns_.size() : active_ == 0)) return;
    set_active_column(right ? active_ + 1 : active_ - 1);
    auto callback = focus_;
    const std::shared_ptr<VirtualCollection> target = lists_[active_];
    if (callback) callback(target);
}
bool MillerColumns::select_item(std::size_t index, ItemKey key, SelectionGesture gesture) {
    auto& list = *lists_[index];
    set_active_column(index);
    const bool changed = gesture != SelectionGesture::focus_only && columns_[index].selected != key;
    list.selection_.select(list.source_, key, gesture == SelectionGesture::focus_only ?
        SelectionGesture::focus_only : SelectionGesture::replace);
    if (gesture != SelectionGesture::focus_only) columns_[index].selected = key;
    list.reveal(key); list.invalidate(Invalidation::paint);
    auto callback = selection_;
    if (changed && callback) callback(index, key);
    return true;
}
void MillerColumns::activate_item(std::size_t index, ItemKey key) {
    if (index >= columns_.size() || !lists_[index]->available(key)) return;
    const auto list = lists_[index];
    const auto source = list->source();
    if (!list->select(key) || !list->owner_ || list->source() != source || !list->available(key)) return;
    auto callback = list->owner_->activate_;
    if (callback) callback(index, key);
}
Size MillerColumns::measure(Size available) {
    return visible() ? Control::measure(available) : Size{};
}
void MillerColumns::arrange(Rect value) {
    const bool resized = bounds().width != value.width;
    Control::arrange(value);
    if (resized) reveal_active();
    else offset_ = std::clamp(offset_, 0.0, maximum_horizontal());
    layout();
}
void MillerColumns::layout() {
    const auto area = bounds();
    const float width = effective_width();
    const float available = std::max(0.0f, area.height - horizontal_track().height);
    const float toolbar = std::min(32.0f, available);
    const float header = std::min(32.0f, std::max(0.0f, available - toolbar));
    const float height = std::max(0.0f, available - toolbar - header);
    const bool shown = visible() && area.width > 0 && area.height > 0;
    previous_->set_visible(shown); next_->set_visible(shown);
    previous_->set_enabled(active_ > 0 && !columns_.empty());
    next_->set_enabled(!columns_.empty() && active_ + 1 < columns_.size());
    const float button = std::min(160.0f, area.width / 2);
    previous_->arrange({area.x, area.y, button, toolbar});
    next_->arrange({area.x + button, area.y, button, toolbar});
    for (std::size_t i = 0; i < maximum_columns; ++i) {
        const float left = static_cast<float>(static_cast<double>(i) * width - offset_);
        const bool present = shown && i < columns_.size();
        const bool onscreen = present && left < area.width && left + width > 0;
        const float content_width = present ? std::max(0.0f, width - (i + 1 < columns_.size() ? separator_width : 0)) : 0;
        headers_[i]->set_visible(onscreen && header > 0);
        lists_[i]->set_visible(onscreen && height > 0);
        headers_[i]->arrange({area.x + left, area.y + toolbar, content_width, header});
        lists_[i]->arrange({area.x + left, area.y + toolbar + header, content_width, present ? height : 0});
    }
}

}
