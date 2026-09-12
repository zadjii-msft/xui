#include "xui/data_grid.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <climits>

namespace xui {
DataGrid::DataGrid(std::wstring name) : Control(ControlRole::data_grid, std::move(name), {640, 360}) {}
void DataGrid::set_columns(std::vector<GridColumn> value) {
    if (value.empty() || value.size() > 64) throw std::invalid_argument("Grid requires 1 to 64 columns");
    for (auto& c : value) if (!std::isfinite(c.width) || c.width < 48 || c.width > 2000)
        throw std::invalid_argument("Invalid grid column width");
    columns_ = std::move(value);
    sort_ = std::min(sort_, columns_.size() - 1);
    focused_column_ = std::min(focused_column_, columns_.size() - 1);
    set_offset(offset_, horizontal_);
    invalidate(Invalidation::paint);
}
void DataGrid::set_source(std::shared_ptr<const GridSource> value) {
    if (value && value->size() > INT_MAX) throw std::invalid_argument("Grid supports at most INT_MAX rows");
    if (source_ == value) return;
    source_ = std::move(value);
    // Keep the old identity even when filtered out; actions require a visible match.
    set_offset(offset_, horizontal_);
    invalidate(Invalidation::paint);
}
bool DataGrid::select(RowKey key, bool reveal) {
    if (!enabled() || !source_ || !source_->find(key)) return false;
    const bool changed = selected_ != key;
    selected_ = key;
    header_focus_ = false;
    if (reveal) reveal_selection();
    invalidate(Invalidation::paint);
    if (changed && select_callback_) select_callback_();
    return true;
}
void DataGrid::clear_selection() {
    if (!selected_) return;
    selected_.reset();
    invalidate(Invalidation::paint);
    if (select_callback_) select_callback_();
}
void DataGrid::step(int delta) {
    if (!source_ || !source_->size()) return;
    auto index = selected_ ? source_->find(*selected_) : std::nullopt;
    const auto current = index ? static_cast<std::int64_t>(*index) : (delta < 0 ? static_cast<std::int64_t>(source_->size()) : -1);
    const auto next = std::clamp(current + delta, std::int64_t{0}, static_cast<std::int64_t>(source_->size() - 1));
    select(source_->key(static_cast<std::size_t>(next)));
}
void DataGrid::edge(bool last) {
    if (source_ && source_->size()) select(source_->key(last ? source_->size() - 1 : 0));
}
void DataGrid::activate_selected() {
    if (enabled() && source_ && selected_ && source_->find(*selected_) && activate_callback_) activate_callback_();
}
void DataGrid::set_sort(std::size_t column, bool descending) {
    if (column >= columns_.size()) return;
    sort_ = column; descending_ = descending; invalidate(Invalidation::paint);
}
void DataGrid::sort(std::size_t column) {
    if (!enabled() || column >= columns_.size()) return;
    set_sort(column, sort_ == column ? !descending_ : columns_[column].numeric);
    if (sort_callback_) sort_callback_(sort_, descending_);
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
    columns_[column].width = std::clamp(width, 64.0f, 1000.0f);
    set_offset(offset_, horizontal_); invalidate(Invalidation::paint);
}
void DataGrid::focus_header(bool value) { header_focus_ = value; invalidate(Invalidation::paint); }
void DataGrid::step_header(int delta) {
    if (columns_.empty()) return;
    focused_column_ = static_cast<std::size_t>(std::clamp(static_cast<int>(focused_column_) + delta, 0, static_cast<int>(columns_.size() - 1)));
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
