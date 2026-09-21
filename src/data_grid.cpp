#include "xui/data_grid.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <climits>
#include <numeric>

namespace xui {
float GridGeometry::viewport_width() const { return std::max(0.0f, width - scrollbar_width); }
float GridGeometry::viewport_height() const { return std::max(0.0f, height - header_height - scrollbar_width); }
Rect GridGeometry::viewport() const { return {left, header_bottom(), viewport_width(), viewport_height()}; }
Rect GridGeometry::header() const { return {left, top, viewport_width(), std::min(height, header_height)}; }
Rect GridGeometry::row(std::size_t index) const {
    return {left, header_bottom() + static_cast<float>(index * double(row_height) - vertical), viewport_width(), row_height};
}
Rect GridGeometry::column(std::span<const GridColumn> columns, std::size_t display) const {
    if (display >= columns.size()) return {};
    float x = left - static_cast<float>(horizontal);
    for (std::size_t i = 0; i < display; ++i) x += columns[i].width;
    return {x, top, columns[display].width, header_height};
}
Rect GridGeometry::header_part(std::span<const GridColumn> columns, std::size_t display, GridHeaderPart part) const {
    auto box = column(columns, display);
    if (display >= columns.size()) return {};
    const auto& c = columns[display];
    if (part == GridHeaderPart::check) return c.checkable ? Rect{box.x + 4, top, 28, header_height} : Rect{};
    if (part == GridHeaderPart::filter) return c.filterable ? Rect{box.x + box.width - 32, top, 28, header_height} : Rect{};
    return {box.x + (c.checkable ? 32 : 0), top,
        std::max(0.0f, box.width - (c.checkable ? 32 : 0) - (c.filterable ? 32 : 0)), header_height};
}
Rect GridGeometry::vertical_track() const { return {left + viewport_width(), header_bottom(), std::min(width, scrollbar_width), viewport_height()}; }
Rect GridGeometry::horizontal_track() const { return {left, top + std::max(0.0f, height - scrollbar_width), viewport_width(), std::min(height, scrollbar_width)}; }

std::optional<StyleTarget> DataGrid::control_style_target() const { return StyleTarget::data_grid; }
StyleStateMask DataGrid::control_style_state_bits() const {
    return Control::control_style_state_bits() & (style_states::focused | style_states::hovered | style_states::disabled);
}
PartStyleValues DataGrid::part_style(StylePart part, StyleStateMask states) const {
    return resolve_control_style_part(part, states);
}
float DataGrid::effective_row_height() const { return part_style(StylePart::root).row_height.value_or(row_height); }
float DataGrid::effective_header_height() const { return part_style(StylePart::root).header_height.value_or(header_height); }
float DataGrid::effective_scrollbar_width() const { return part_style(StylePart::scrollbar).width.value_or(bar_width); }
int DataGrid::page_rows() const {
    const auto g = geometry();
    return std::max(1, static_cast<int>(std::min(double(INT_MAX), double(g.viewport_height()) / g.row_height)));
}
GridGeometry DataGrid::geometry() const {
    const auto style = part_style(StylePart::root);
    const auto p = style.padding.value_or(Insets{}), b = style.border_thickness.value_or(Insets{});
    const auto left = std::min(bounds().width, p.left + b.left), top = std::min(bounds().height, p.top + b.top);
    return {left, top, std::max(0.0f, bounds().width - left - p.right - b.right),
        std::max(0.0f, bounds().height - top - p.bottom - b.bottom),
        style.row_height.value_or(row_height), style.header_height.value_or(header_height), effective_scrollbar_width(),
        offset_, horizontal_, bounds().width, bounds().height};
}
Rect DataGrid::cell_bounds(std::size_t row, std::size_t source) const {
    const auto display = display_column(source);
    if (!display || !source_ || row >= source_->size()) return {};
    const auto g = geometry();
    auto box = g.row(row);
    const auto column = g.column(columns_, *display);
    box.x = column.x; box.width = column.width;
    return box;
}
StyleStateMask DataGrid::row_style_states(std::size_t row, bool context_enabled, bool dragging) const {
    StyleStateMask state = !enabled() || !context_enabled ? style_states::disabled : 0;
    if (!source_ || row >= source_->size()) return state;
    const auto key = source_->key(row);
    if (selection_.contains(key)) state |= style_states::selected | style_states::checked;
    if (!(state & style_states::disabled)) {
        if (!dragging && hovered_row() == row) state |= style_states::hovered;
        if (focused() && !header_focus_ && selected_ == key) state |= style_states::focused;
    }
    return state;
}
StyleStateMask DataGrid::header_style_states(std::size_t column, bool context_enabled, bool dragging) const {
    StyleStateMask state = !enabled() || !context_enabled ? style_states::disabled : 0;
    const auto display = display_column(column);
    if (!display) return state;
    if (column == sort_) state |= style_states::sorted | (descending_ ? style_states::descending : 0);
    if (!filters_[column].empty()) state |= style_states::filtered;
    if (filter_pending_ && column == filter_column_) state |= style_states::filter_pending;
    if (columns_[*display].checkable) {
        const auto check = check_state();
        if (check == SelectionState::all) state |= style_states::checked;
        else if (check == SelectionState::mixed) state |= style_states::mixed;
    }
    if (!(state & style_states::disabled)) {
        if (focused() && header_focus_ && focused_column_ == *display) state |= style_states::focused;
        if (dragging) state |= style_states::dragging;
    }
    return state;
}
PartStyleValues DataGrid::row_style(StylePart part, std::size_t row, bool context_enabled, bool dragging) const {
    const auto state = row_style_states(row, context_enabled, dragging);
    auto values = part_style(part, state);
    if (part == StylePart::row && row % 2) values = merge_part_values(std::move(values), part_style(StylePart::alternating_row, state));
    if (part == StylePart::cell && !values.foreground) values.foreground = row_style(StylePart::row, row, context_enabled, dragging).foreground;
    return values;
}
PartStyleValues DataGrid::header_style(StylePart part, std::size_t source, bool context_enabled, bool dragging) const {
    return part_style(part, header_style_states(source, context_enabled, dragging));
}

void DataGrid::prepare_context_menu(std::optional<Point> position) {
    if (position) {
        const auto view = geometry().viewport();
        const auto row = position->x >= view.x && position->x < view.x + view.width ? row_at(position->y) : std::nullopt;
        if (row && source_) {
            const auto key = source_->key(*row);
            select(key, selection_.contains(key) ? SelectionGesture::focus_only : SelectionGesture::replace, false);
        }
        else clear_selection();
    } else if (selected_ && (!source_ || !source_->find(*selected_))) clear_selection();
}
DataGrid::DataGrid(std::wstring name) : Control(ControlRole::data_grid, std::move(name), {640, 360}) {}
bool DataGrid::file_drop_hit(Point point, std::optional<RowKey>& key) const {
    key.reset();
    const auto view = geometry().viewport();
    if (!enabled() || !visible() || !std::isfinite(point.x) || !std::isfinite(point.y) ||
        point.x < view.x || point.x >= view.x + view.width || point.y < view.y ||
        point.y >= view.y + view.height) return false;
    if (auto row = row_at(point.y)) {
        if (!source_->selectable(*row)) return false;
        key = source_->key(*row);
    }
    return true;
}
FileTransferEffect DataGrid::query_file_drop(Point point, FileTransferEffect effect) const {
    std::optional<RowKey> key;
    auto callback = drop_query_;
    if (!callback || !file_drop_ || !file_drop_hit(point, key)) return FileTransferEffect::none;
    const auto source = source_;
    const auto result = callback(key, effect);
    std::optional<RowKey> current;
    return source_ == source && file_drop_hit(point, current) && current == key ? result : FileTransferEffect::none;
}
FileTransferEffect DataGrid::drop_files(Point point, const std::vector<std::wstring>& paths, FileTransferEffect effect) {
    std::optional<RowKey> key;
    auto callback = file_drop_;
    return callback && file_drop_hit(point, key) ? callback(key, paths, effect) : FileTransferEffect::none;
}
bool DataGrid::begin_file_press(Point point, SelectionGesture gesture) {
    end_file_press(false);
    std::optional<RowKey> key;
    if (!file_drag_enabled() || !file_drop_hit(point, key) || !key) return false;
    const bool preserve = selection_.contains(*key) && gesture == SelectionGesture::replace;
    const auto source = source_;
    select(*key, preserve ? SelectionGesture::focus_only : gesture, false);
    if (!enabled() || !visible() || source != source_ || !selection_.contains(*key)) return false;
    file_press_ = point; file_press_key_ = key; file_press_replace_ = preserve;
    return true;
}
bool DataGrid::file_drag_threshold(Point point, Size threshold) const {
    return file_press_ && (std::abs(point.x - file_press_->x) >= threshold.width ||
        std::abs(point.y - file_press_->y) >= threshold.height);
}
void DataGrid::end_file_press(bool click) {
    const auto key = file_press_key_;
    const bool replace = file_press_replace_;
    file_press_.reset(); file_press_key_.reset(); file_press_replace_ = false;
    if (click && replace && key) select(*key, false);
}
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
    end_file_press(false);
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
    if (file_press_key_ && file_press_key_ != key) end_file_press(false);
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
    end_file_press(false);
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
    if (file_press_key_ && file_press_key_ != value.focused()) end_file_press(false);
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
    filter_pending_ = true; filter_column_ = column; invalidate(Invalidation::paint);
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
    end_file_press(false);
    hover_pointer({});
    Control::cancel(); filter_stop_.request_stop(); ++filter_generation_;
    if (filter_pending_) { filter_pending_ = false; invalidate(Invalidation::paint); }
}
Rect DataGrid::header_part_bounds(std::size_t column, GridHeaderPart part) const {
    const auto display = display_column(column); if (!display) return {};
    return geometry().header_part(columns_, *display, part);
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
float DataGrid::viewport_width() const { return geometry().viewport_width(); }
float DataGrid::viewport_height() const { return geometry().viewport_height(); }
double DataGrid::maximum_offset() const { return std::max(0.0, (source_ ? static_cast<double>(source_->size()) * effective_row_height() : 0) - viewport_height()); }
double DataGrid::maximum_horizontal() const { return std::max(0.0f, content_width() - viewport_width()); }
void DataGrid::set_offset(double vertical, double horizontal) {
    const auto y = std::clamp(std::isfinite(vertical) ? vertical : 0, 0.0, maximum_offset());
    const auto x = std::clamp(std::isfinite(horizontal) ? horizontal : 0, 0.0, maximum_horizontal());
    if (y == offset_ && x == horizontal_) return;
    offset_ = y; horizontal_ = x; invalidate(Invalidation::paint);
}
void DataGrid::arrange(Rect rect) { Control::arrange(rect); set_offset(offset_, horizontal_); }
std::pair<std::size_t, std::size_t> DataGrid::visible_rows() const {
    if (!source_) return {};
    const auto g = geometry();
    if (g.viewport_height() <= 0) return {};
    const auto first = static_cast<std::size_t>(std::min(double(source_->size()), offset_ / g.row_height));
    const auto count = static_cast<std::size_t>(std::min(double(source_->size() - first), std::ceil(double(g.viewport_height()) / g.row_height) + 1));
    return {first, first + count};
}
std::optional<std::size_t> DataGrid::row_at(float y) const {
    const auto g = geometry();
    if (!source_ || !std::isfinite(y) || y < g.header_bottom() || y >= g.header_bottom() + g.viewport_height()) return {};
    const auto row = (y - g.header_bottom() + offset_) / g.row_height;
    return row >= 0 && row < double(source_->size()) ? std::optional{static_cast<std::size_t>(row)} : std::nullopt;
}
void DataGrid::hover_pointer(std::optional<Point> position) {
    if (position && (!std::isfinite(position->x) || !std::isfinite(position->y)))
        throw std::invalid_argument("Grid hover coordinates must be finite");
    const auto previous = hovered_row();
    hover_pointer_ = position;
    if (previous != hovered_row()) invalidate(Invalidation::paint);
}
std::optional<std::size_t> DataGrid::hovered_row() const {
    const auto view = geometry().viewport();
    if (!enabled() || !visible() || !hover_pointer_ || hover_pointer_->x < view.x || hover_pointer_->x >= view.x + view.width) return {};
    const auto row = row_at(hover_pointer_->y);
    return row && source_->selectable(*row) ? row : std::nullopt;
}
std::optional<std::size_t> DataGrid::column_at(float x) const {
    const auto view = geometry().viewport();
    if (!std::isfinite(x) || x < view.x || x >= view.x + view.width) return {};
    x += static_cast<float>(horizontal_) - view.x;
    for (std::size_t i = 0; i < columns_.size(); ++i) { if (x < columns_[i].width) return i; x -= columns_[i].width; }
    return {};
}
std::optional<std::size_t> DataGrid::resize_boundary(float x) const {
    const auto view = geometry().viewport();
    if (x < view.x || x >= view.x + view.width) return {};
    float edge = view.x - static_cast<float>(horizontal_);
    for (std::size_t c = 0; c < columns_.size(); ++c) {
        edge += columns_[c].width;
        if (edge >= view.x && edge < view.x + view.width && std::abs(x - edge) <= 5) return c;
    }
    return {};
}
void DataGrid::reveal_selection() {
    if (selected_) reveal(*selected_);
}
bool DataGrid::reveal(RowKey key) {
    const auto row = source_ ? source_->find(key) : std::nullopt;
    if (!row) return false;
    const auto height = effective_row_height();
    const double top = static_cast<double>(*row) * height;
    set_offset(top < offset_ ? top : top + height > offset_ + viewport_height() ? top + height - viewport_height() : offset_, horizontal_);
    return true;
}
Rect DataGrid::vertical_thumb() const {
    const auto height = viewport_height();
    if (!maximum_offset() || height <= 0) return {};
    const float length = std::min(height, std::max(24.0f, static_cast<float>(height * height / (maximum_offset() + height))));
    const auto track = geometry().vertical_track();
    return {track.x + track.width / 4, track.y + static_cast<float>(offset_ / maximum_offset()) * (height - length), track.width / 2, length};
}
Rect DataGrid::horizontal_thumb() const {
    const auto width = viewport_width();
    if (!maximum_horizontal() || width <= 0) return {};
    const float length = std::min(width, std::max(24.0f, width * width / content_width()));
    const auto track = geometry().horizontal_track();
    return {track.x + static_cast<float>(horizontal_ / maximum_horizontal()) * (width - length), track.y + track.height / 4, length, track.height / 2};
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
std::optional<StyleTarget> HistoryChart::control_style_target() const { return StyleTarget::history_chart; }
StyleStateMask HistoryChart::control_style_state_bits() const {
    return (Control::control_style_state_bits() & style_states::disabled) | (empty() ? style_states::empty : 0);
}
PartStyleValues HistoryChart::part_style(StylePart part) const {
    return resolve_control_style_part(part, empty() ? style_states::empty : 0);
}
bool HistoryChart::empty() const {
    for (std::size_t i = 0; i < size_; ++i) if (at(i)) return false;
    return true;
}
Rect HistoryChart::title_bounds() const {
    const auto style = part_style(StylePart::root);
    const auto p = style.padding.value_or(Insets{12, 2, 12, 0}), b = style.border_thickness.value_or(Insets{});
    return {p.left + b.left, p.top + b.top, std::max(0.0f, bounds().width - p.left - p.right - b.left - b.right),
        std::min(32.0f, std::max(0.0f, bounds().height - p.top - p.bottom - b.top - b.bottom))};
}
Rect HistoryChart::caption_bounds() const {
    const auto style = part_style(StylePart::root);
    const auto p = style.padding.value_or(Insets{12, 2, 12, 0}), b = style.border_thickness.value_or(Insets{});
    const auto title = title_bounds();
    const float bottom = std::max(title.y, bounds().height - p.bottom - b.bottom);
    return {title.x, std::max(title.y, bottom - 22), title.width, std::min(20.0f, std::max(0.0f, bottom - title.y))};
}
Rect HistoryChart::plot_bounds() const {
    const auto title = title_bounds(), caption = caption_bounds();
    const float top = title.y + title.height + 4;
    return {title.x, top, title.width, std::max(0.0f, caption.y + 2 - top)};
}
void HistoryChart::set_scale(double value) {
    if (!std::isfinite(value) || value <= 0) throw std::invalid_argument("Chart scale must be positive and finite");
    maximum_ = value; invalidate(Invalidation::paint);
}
void HistoryChart::append(std::optional<double> value) {
    if (value && (!std::isfinite(*value) || *value < 0 || *value > maximum_)) value.reset();
    values_[next_] = value; next_ = (next_ + 1) % capacity; size_ = std::min(size_ + 1, capacity);
    invalidate_control_style_state();
    invalidate(Invalidation::paint);
}
std::optional<double> HistoryChart::at(std::size_t index) const {
    return index < size_ ? values_[(next_ + capacity - size_ + index) % capacity] : std::nullopt;
}
}
