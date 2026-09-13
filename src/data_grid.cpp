#include "xui/data_grid.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <climits>
#include <numeric>

namespace xui {
DataGrid::DataGrid(std::wstring name) : Control(ControlRole::data_grid, std::move(name), {640, 360}) {}
void DataGrid::set_columns(std::vector<GridColumn> value) {
    if (value.empty() || value.size() > 64) throw std::invalid_argument("Grid requires 1 to 64 columns");
    for (auto& c : value) if (!std::isfinite(c.width) || c.width < 48 || c.width > 2000)
        throw std::invalid_argument("Invalid grid column width");
    std::vector<std::size_t> order(value.size());
    std::iota(order.begin(), order.end(), 0);
    columns_ = std::move(value);
    column_order_ = std::move(order);
    cancel(); filters_.assign(columns_.size(), {});
    sort_ = std::min(sort_, columns_.size() - 1);
    focused_column_ = std::min(focused_column_, columns_.size() - 1);
    set_header_part(header_part_);
    set_offset(offset_, horizontal_);
    invalidate(Invalidation::paint);
}
std::optional<std::size_t> DataGrid::display_column(std::size_t source_column) const {
    const auto it = std::find(column_order_.begin(), column_order_.end(), source_column);
    return it == column_order_.end() ? std::nullopt : std::optional<std::size_t>(it - column_order_.begin());
}
void DataGrid::set_column_order(std::vector<std::size_t> order) {
    if (order.size() != columns_.size()) throw std::invalid_argument("Column order must be a complete permutation");
    std::array<bool, 64> seen{};
    for (auto column : order) {
        if (column >= columns_.size() || seen[column]) throw std::invalid_argument("Invalid column order");
        seen[column] = true;
    }
    if (order == column_order_) return;
    const auto focused = source_column(focused_column_);
    std::vector<GridColumn> columns;
    columns.reserve(order.size());
    for (auto column : order) columns.push_back(columns_[*display_column(column)]);
    columns_ = std::move(columns);
    column_order_ = std::move(order);
    focused_column_ = *display_column(focused);
    set_offset(offset_, horizontal_);
    invalidate(Invalidation::paint);
}
bool DataGrid::reorder_column(std::size_t from, std::size_t to) {
    if (from >= columns_.size() || to >= columns_.size()) return false;
    if (from == to) return true;
    auto order = column_order_;
    const auto column = order[from];
    order.erase(order.begin() + from);
    order.insert(order.begin() + to, column);
    set_column_order(std::move(order));
    return true;
}
void DataGrid::set_source(std::shared_ptr<const GridSource> value) {
    if (value && value->size() > INT_MAX) throw std::invalid_argument("Grid supports at most INT_MAX rows");
    if (source_ == value) return;
    if (filter_pending_) { filter_stop_.request_stop(); filter_pending_ = false; ++filter_generation_; }
    source_ = std::move(value);
    // Keep the old identity even when filtered out; actions require a visible match.
    set_offset(offset_, horizontal_);
    invalidate(Invalidation::paint);
}
bool DataGrid::select(RowKey key, bool reveal) {
    return select(key, SelectionGesture::replace, reveal);
}
bool DataGrid::select(RowKey key, SelectionGesture gesture, bool reveal) {
    if (!enabled() || !source_ || !source_->find(key)) return false;
    const auto before = selection_;
    const bool changed = selected_ != key;
    selected_ = key;
    selection_.select(source_, key, gesture);
    header_focus_ = false;
    if (reveal) reveal_selection();
    invalidate(Invalidation::paint);
    if ((changed || !(before == selection_)) && select_callback_) { auto callback = select_callback_; callback(); }
    return true;
}
void DataGrid::clear_selection() {
    if (!selected_ && selection_.empty()) return;
    selected_.reset();
    selection_.clear(); selection_.set_focus({});
    invalidate(Invalidation::paint);
    if (select_callback_) { auto callback = select_callback_; callback(); }
}
void DataGrid::step(int delta, SelectionGesture gesture) {
    if (!source_ || !source_->size()) return;
    auto index = selected_ ? source_->find(*selected_) : std::nullopt;
    const auto current = index ? static_cast<std::int64_t>(*index) : (delta < 0 ? static_cast<std::int64_t>(source_->size()) : -1);
    const auto next = std::clamp(current + delta, std::int64_t{0}, static_cast<std::int64_t>(source_->size() - 1));
    select(source_->key(static_cast<std::size_t>(next)), gesture);
}
void DataGrid::edge(bool last, SelectionGesture gesture) {
    if (source_ && source_->size()) select(source_->key(last ? source_->size() - 1 : 0), gesture);
}
void DataGrid::activate_selected() {
    if (enabled() && source_ && selected_ && source_->find(*selected_) && activate_callback_) { auto callback = activate_callback_; callback(); }
}
void DataGrid::set_sort(std::size_t column, bool descending) {
    if (column >= columns_.size()) return;
    sort_ = column; descending_ = descending; invalidate(Invalidation::paint);
}
void DataGrid::sort(std::size_t column) {
    const auto display = display_column(column);
    if (!enabled() || !display) return;
    set_sort(column, sort_ == column ? !descending_ : columns_[*display].numeric);
    if (sort_callback_) { auto callback = sort_callback_; callback(sort_, descending_); }
}
void DataGrid::set_selection(CollectionSelection value) {
    selection_ = std::move(value); selected_ = selection_.focused(); invalidate(Invalidation::paint);
}
void DataGrid::select_all() {
    if (!enabled() || !source_) return;
    selection_.select_all(source_, scope_, full_source_);
    invalidate(Invalidation::paint); auto callback = select_callback_; if (callback) callback();
}
void DataGrid::set_checked(RowKey key, bool value) {
    selection_.set(key, value); invalidate(Invalidation::paint);
}
void DataGrid::toggle_check(std::optional<RowKey> key) {
    if (!enabled() || !source_) return;
    if (key) {
        if (!source_->find(*key)) return;
        selection_.set(*key, !selection_.contains(*key));
        selection_.set_focus(*key); selected_ = key;
    } else {
        if (check_state() == SelectionState::all) selection_.clear();
        else { select_all(); return; }
    }
    invalidate(Invalidation::paint); auto callback = select_callback_; if (callback) callback();
}
void DataGrid::set_filter(std::size_t column, std::wstring text) {
    const auto display = display_column(column);
    if (!display || !columns_[*display].filterable || text.size() > 4096) throw std::invalid_argument("Invalid grid filter");
    if (filters_[column] == text) return;
    filter_stop_.request_stop(); filter_pending_ = false; ++filter_generation_;
    filters_[column] = std::move(text); invalidate(Invalidation::paint);
}
void DataGrid::filter(std::size_t column, std::wstring text) {
    if (!enabled()) return;
    set_filter(column, std::move(text));
    filter_stop_.request_stop(); filter_stop_ = std::stop_source{};
    const GridFilterRequest request{++filter_generation_, column, filters_, filter_stop_.get_token()};
    filter_pending_ = true; invalidate(Invalidation::paint);
    auto callback = filter_callback_;
    if (callback) {
        try { callback(request); } catch (...) { cancel(); throw; }
    } else filter_pending_ = false;
}
void DataGrid::open_filter(std::size_t column) {
    const auto display = display_column(column);
    if (!enabled() || !display || !columns_[*display].filterable) return;
    auto callback = filter_open_; if (callback) callback(column);
}
bool DataGrid::complete_filter(GridFilterRequest request, std::shared_ptr<const GridSource> source) {
    if (!filter_pending_ || request.generation != filter_generation_ || request.cancellation != filter_stop_.get_token() || request.cancellation.stop_requested()) return false;
    filter_pending_ = false; set_source(std::move(source)); invalidate(Invalidation::paint); return true;
}
void DataGrid::cancel() {
    Control::cancel(); filter_stop_.request_stop(); ++filter_generation_;
    if (filter_pending_) { filter_pending_ = false; invalidate(Invalidation::paint); }
}
Rect DataGrid::header_part_bounds(std::size_t column, GridHeaderPart part) const {
    const auto display = display_column(column); if (!display) return {};
    const auto& c = columns_[*display]; float x = -static_cast<float>(horizontal_);
    for (std::size_t i = 0; i < *display; ++i) x += columns_[i].width;
    if (part == GridHeaderPart::check) return c.checkable ? Rect{x + 4, 0, 28, header_height} : Rect{};
    if (part == GridHeaderPart::filter) return c.filterable ? Rect{x + c.width - 32, 0, 28, header_height} : Rect{};
    return {x + (c.checkable ? 32 : 0), 0, std::max(0.0f, c.width - (c.checkable ? 32 : 0) - (c.filterable ? 32 : 0)), header_height};
}
GridHeaderPart DataGrid::header_part_at(float x) const {
    const auto display = column_at(x); if (!display) return GridHeaderPart::sort;
    for (auto part : {GridHeaderPart::check, GridHeaderPart::filter}) {
        const auto b = header_part_bounds(source_column(*display), part);
        if (b.width && x >= b.x && x < b.x + b.width) return part;
    }
    return GridHeaderPart::sort;
}
void DataGrid::set_header_part(GridHeaderPart part) {
    if (columns_.empty() || !header_part_bounds(source_column(focused_column_), part).width) part = GridHeaderPart::sort;
    if (header_part_ == part) return;
    header_part_ = part; invalidate(Invalidation::paint);
}
float DataGrid::content_width() const { float width{}; for (const auto& c : columns_) width += c.width; return width; }
float DataGrid::viewport_width() const { return std::max(0.0f, bounds().width - bar_width); }
float DataGrid::viewport_height() const { return std::max(0.0f, bounds().height - header_height - bar_width); }
double DataGrid::maximum_offset() const { return std::max(0.0, (source_ ? static_cast<double>(source_->size()) * row_height : 0) - viewport_height()); }
double DataGrid::maximum_horizontal() const { return std::max(0.0f, content_width() - viewport_width()); }
void DataGrid::set_offset(double vertical, double horizontal) {
    const auto y = std::clamp(std::isfinite(vertical) ? vertical : 0, 0.0, maximum_offset());
    const auto x = std::clamp(std::isfinite(horizontal) ? horizontal : 0, 0.0, maximum_horizontal());
    if (y == offset_ && x == horizontal_) return;
    offset_ = y; horizontal_ = x; invalidate(Invalidation::paint);
}
void DataGrid::arrange(Rect rect) { Control::arrange(rect); set_offset(offset_, horizontal_); }
std::pair<std::size_t, std::size_t> DataGrid::visible_rows() const {
    if (!source_ || viewport_height() <= 0) return {};
    const auto first = std::min(source_->size(), static_cast<std::size_t>(offset_ / row_height));
    return {first, std::min(source_->size(), first + static_cast<std::size_t>(std::ceil(viewport_height() / row_height)) + 1)};
}
std::optional<std::size_t> DataGrid::row_at(float y) const {
    if (!source_ || y < header_height || y >= header_height + viewport_height()) return {};
    auto row = static_cast<std::size_t>((y - header_height + offset_) / row_height);
    return row < source_->size() ? std::optional{row} : std::nullopt;
}
std::optional<std::size_t> DataGrid::column_at(float x) const {
    if (x < 0 || x >= viewport_width()) return {};
    x += static_cast<float>(horizontal_);
    for (std::size_t i = 0; i < columns_.size(); ++i) { if (x < columns_[i].width) return i; x -= columns_[i].width; }
    return {};
}
std::optional<std::size_t> DataGrid::resize_boundary(float x) const {
    if (x < 0 || x >= viewport_width()) return {};
    float edge = -static_cast<float>(horizontal_);
    for (std::size_t c = 0; c < columns_.size(); ++c) {
        edge += columns_[c].width;
        if (edge >= 0 && edge < viewport_width() && std::abs(x - edge) <= 5) return c;
    }
    return {};
}
void DataGrid::reveal_selection() {
    if (selected_) reveal(*selected_);
}
bool DataGrid::reveal(RowKey key) {
    const auto row = source_ ? source_->find(key) : std::nullopt;
    if (!row) return false;
    const double top = static_cast<double>(*row) * row_height;
    set_offset(top < offset_ ? top : top + row_height > offset_ + viewport_height() ? top + row_height - viewport_height() : offset_, horizontal_);
    return true;
}
Rect DataGrid::vertical_thumb() const {
    const auto height = viewport_height();
    if (!maximum_offset() || height <= 0) return {};
    const float length = std::min(height, std::max(24.0f, static_cast<float>(height * height / (maximum_offset() + height))));
    return {viewport_width() + 3, header_height + static_cast<float>(offset_ / maximum_offset()) * (height - length), 6, length};
}
Rect DataGrid::horizontal_thumb() const {
    const auto width = viewport_width();
    if (!maximum_horizontal() || width <= 0) return {};
    const float length = std::min(width, std::max(24.0f, width * width / content_width()));
    return {static_cast<float>(horizontal_ / maximum_horizontal()) * (width - length), bounds().height - 9, length, 6};
}
void DataGrid::resize_column(std::size_t column, float width) {
    if (column >= columns_.size() || !std::isfinite(width)) return;
    set_column_width(column, std::clamp(width, 64.0f, 1000.0f));
}
void DataGrid::set_column_width(std::size_t column, float width) {
    if (column >= columns_.size() || !std::isfinite(width) || width < 48 || width > 2000)
        throw std::invalid_argument("Invalid grid column width");
    if (columns_[column].width == width) return;
    columns_[column].width = width;
    set_offset(offset_, horizontal_); invalidate(Invalidation::paint);
}
void DataGrid::focus_header(bool value) { header_focus_ = value; invalidate(Invalidation::paint); }
void DataGrid::step_header(int delta) {
    if (columns_.empty()) return;
    focused_column_ = static_cast<std::size_t>(std::clamp(static_cast<int>(focused_column_) + delta, 0, static_cast<int>(columns_.size() - 1)));
    set_header_part(header_part_);
    float left{}; for (std::size_t i = 0; i < focused_column_; ++i) left += columns_[i].width;
    set_offset(offset_, left < horizontal_ ? left : left + columns_[focused_column_].width > horizontal_ + viewport_width() ?
        left + columns_[focused_column_].width - viewport_width() : horizontal_);
    invalidate(Invalidation::paint);
}
HistoryChart::HistoryChart(std::wstring name) : Control(ControlRole::history_chart, std::move(name), {400, 160}) {}
void HistoryChart::set_scale(double value) {
    if (!std::isfinite(value) || value <= 0) throw std::invalid_argument("Chart scale must be positive and finite");
    maximum_ = value; invalidate(Invalidation::paint);
}
void HistoryChart::append(std::optional<double> value) {
    if (value && (!std::isfinite(*value) || *value < 0 || *value > maximum_)) value.reset();
    values_[next_] = value; next_ = (next_ + 1) % capacity; size_ = std::min(size_ + 1, capacity);
    invalidate(Invalidation::paint);
}
std::optional<double> HistoryChart::at(std::size_t index) const {
    return index < size_ ? values_[(next_ + capacity - size_ + index) % capacity] : std::nullopt;
}
}
