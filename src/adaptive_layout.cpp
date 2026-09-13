#include "xui/adaptive_layout.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace xui {
namespace {
void positive(float value) { if (!std::isfinite(value) || value < 0) throw std::invalid_argument("Layout size must be finite and nonnegative"); }
void padding_valid(Insets p) { for (auto v : {p.left, p.top, p.right, p.bottom}) positive(v); }
float sum(const std::vector<float>& values, std::size_t first, std::size_t count, float gap) {
    return std::accumulate(values.begin() + first, values.begin() + first + count, 0.0f) + gap * (count - 1);
}
std::vector<float> tracks(const std::vector<GridTrack>& definitions, const std::vector<float>& desired, float available, float gap) {
    std::vector<float> result; float used = gap * (definitions.size() - 1), weight{};
    for (std::size_t i = 0; i < definitions.size(); ++i) {
        const auto& t = definitions[i];
        result.push_back(std::clamp(t.sizing == TrackSizing::fixed ? t.value :
            t.sizing == TrackSizing::automatic ? desired[i] : t.minimum, t.minimum, t.maximum));
        used += result.back(); if (t.sizing == TrackSizing::star) weight += t.value;
    }
    float remaining = std::max(0.0f, available - used);
    // Saturated tracks return their remaining share to the other star tracks.
    for (std::size_t pass = 0; pass < definitions.size() && weight > 0 && remaining > 0.01f; ++pass) {
        float consumed{}, next_weight{};
        for (std::size_t i = 0; i < definitions.size(); ++i) {
            const auto& t = definitions[i]; if (t.sizing != TrackSizing::star || result[i] >= t.maximum) continue;
            const auto add = std::min(t.maximum - result[i], remaining * t.value / weight);
            result[i] += add; consumed += add; if (result[i] < t.maximum) next_weight += t.value;
        }
        remaining -= consumed; weight = next_weight;
    }
    return result;
}
}
Grid::Grid() : Stack(Axis::vertical) { set_auto_size(true); }
void Grid::set_tracks(std::vector<GridTrack> rows, std::vector<GridTrack> columns) {
    for (const auto* list : {&rows, &columns}) {
        if (list->empty() || list->size() > 256) throw std::invalid_argument("Grid requires 1 to 256 tracks");
        for (const auto& t : *list) {
            positive(t.value); positive(t.minimum); positive(t.maximum);
            if (t.minimum > t.maximum || (t.sizing == TrackSizing::star && t.value == 0)) throw std::invalid_argument("Invalid grid track");
        }
    }
    for (const auto& cell : cells_) if (cell.row + cell.rows > rows.size() || cell.column + cell.columns > columns.size())
        throw std::invalid_argument("Existing cell exceeds new tracks");
    if (rows_ == rows && columns_ == columns) return;
    rows_ = std::move(rows); columns_ = std::move(columns); invalidate(Invalidation::layout);
}
void Grid::set_gap(float horizontal, float vertical) { positive(horizontal); positive(vertical); if (horizontal_ == horizontal && vertical_ == vertical) return; horizontal_ = horizontal; vertical_ = vertical; invalidate(Invalidation::layout); }
void Grid::set_padding(Insets value) {
    padding_valid(value);
    if (padding_.left == value.left && padding_.top == value.top && padding_.right == value.right && padding_.bottom == value.bottom) return;
    padding_ = value; invalidate(Invalidation::layout);
}
void Grid::add(std::shared_ptr<Element> child, std::size_t row, std::size_t column, std::size_t row_span, std::size_t column_span) {
    if (!row_span || !column_span || row >= rows_.size() || column >= columns_.size() ||
        row_span > rows_.size() - row || column_span > columns_.size() - column) throw std::invalid_argument("Grid cell exceeds tracks");
    cells_.reserve(cells_.size() + 1);
    Stack::add(std::move(child)); cells_.push_back({row, column, row_span, column_span});
}
std::pair<std::vector<float>, std::vector<float>> Grid::sizes(Size available) {
    available.width = std::max(0.0f, available.width - padding_.left - padding_.right);
    available.height = std::max(0.0f, available.height - padding_.top - padding_.bottom);
    std::vector<float> rw(rows_.size()), cw(columns_.size());
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const auto m = child_at(i)->measure(available); const auto& c = cells_[i];
        for (auto col = c.column; col < c.column + c.columns; ++col) cw[col] = std::max(cw[col], (m.width - horizontal_ * (c.columns - 1)) / c.columns);
    }
    auto columns = tracks(columns_, cw, available.width, horizontal_);
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const auto& c = cells_[i];
        const auto m = child_at(i)->measure({sum(columns, c.column, c.columns, horizontal_), available.height});
        for (auto row = c.row; row < c.row + c.rows; ++row) rw[row] = std::max(rw[row], (m.height - vertical_ * (c.rows - 1)) / c.rows);
    }
    return {tracks(rows_, rw, available.height, vertical_), std::move(columns)};
}
Size Grid::measure(Size available) {
    if (!auto_size()) return Element::measure(available);
    auto [rows, columns] = sizes(available);
    return constrain({sum(columns, 0, columns.size(), horizontal_) + padding_.left + padding_.right,
        sum(rows, 0, rows.size(), vertical_) + padding_.top + padding_.bottom}, available);
}
void Grid::arrange(Rect value) {
    Element::arrange(value); value = bounds(); auto [rows, columns] = sizes({value.width, value.height});
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const auto& c = cells_[i];
        const float x = value.x + padding_.left + (c.column ? sum(columns, 0, c.column, horizontal_) + horizontal_ : 0);
        const float y = value.y + padding_.top + (c.row ? sum(rows, 0, c.row, vertical_) + vertical_ : 0);
        child_at(i)->arrange({x, y, std::max(0.0f, std::min(sum(columns, c.column, c.columns, horizontal_), value.x + value.width - padding_.right - x)),
            std::max(0.0f, std::min(sum(rows, c.row, c.rows, vertical_), value.y + value.height - padding_.bottom - y))});
    }
}
Wrap::Wrap() : Stack(Axis::horizontal) { set_auto_size(true); }
void Wrap::set_spacing(float value) { positive(value); if (spacing_ == value) return; spacing_ = value; invalidate(Invalidation::layout); }
void Wrap::set_padding(Insets value) {
    padding_valid(value);
    if (padding_.left == value.left && padding_.top == value.top && padding_.right == value.right && padding_.bottom == value.bottom) return;
    padding_ = value; invalidate(Invalidation::layout);
}
void Wrap::set_item_width(float value) { positive(value); if (value < 1) throw std::invalid_argument("Wrap width must be positive"); if (width_ == value) return; width_ = value; invalidate(Invalidation::layout); }
Size Wrap::layout(Size available, bool arrange, Point origin) {
    const auto width = std::max(0.0f, available.width - padding_.left - padding_.right);
    columns_ = static_cast<std::size_t>(std::max(1.0f, std::min(float(std::max(std::size_t{1}, child_count())), std::floor((width + spacing_) / (width_ + spacing_)))));
    const float cell = std::max(0.0f, (width - spacing_ * (columns_ - 1)) / columns_);
    float y = padding_.top;
    for (std::size_t start = 0; start < child_count(); start += columns_) {
        float height{};
        for (auto i = start; i < std::min(child_count(), start + columns_); ++i)
            height = std::max(height, child_at(i)->measure({cell, available.height}).height);
        if (arrange) for (auto i = start; i < std::min(child_count(), start + columns_); ++i)
            child_at(i)->arrange({origin.x + padding_.left + (i - start) * (cell + spacing_), origin.y + y, cell,
                std::max(0.0f, std::min(height, available.height - padding_.bottom - y))});
        y += height + spacing_;
    }
    return {available.width, y + padding_.bottom - (child_count() ? spacing_ : 0)};
}
Size Wrap::measure(Size available) { return auto_size() ? constrain(layout(available, false, {}), available) : Element::measure(available); }
void Wrap::arrange(Rect value) { Element::arrange(value); value = bounds(); layout({value.width, value.height}, true, {value.x, value.y}); }
AdaptiveLayout::AdaptiveLayout(std::shared_ptr<Element> navigation, std::shared_ptr<Element> content) : Stack(Axis::horizontal) {
    add(std::move(content)); add(std::move(navigation)); set_preferred_size({800, 360});
}
void AdaptiveLayout::set_breakpoint(float value) { positive(value); if (breakpoint_ == value) return; breakpoint_ = value; invalidate(Invalidation::layout); }
void AdaptiveLayout::set_navigation_extent(float value) { positive(value); if (extent_ == value) return; extent_ = value; invalidate(Invalidation::layout); }
void AdaptiveLayout::set_compact_navigation(CompactNavigation value) { if (mode_ == value) return; mode_ = value; invalidate(Invalidation::layout); }
void AdaptiveLayout::set_navigation_open(bool value) { if (open_ == value) return; open_ = value; invalidate(Invalidation::layout); }
Size AdaptiveLayout::measure(Size available) { return Element::measure(available); }
void AdaptiveLayout::arrange(Rect value) {
    Element::arrange(value); value = bounds(); compact_ = value.width < breakpoint_;
    if (compact_ && mode_ == CompactNavigation::overlay) {
        content()->arrange(value);
        navigation()->arrange(open_ ? Rect{value.x, value.y, std::min(extent_, value.width * 0.85f), value.height} : Rect{});
        return;
    }
    const auto extent = std::min(extent_, (compact_ ? value.height : value.width) * 0.5f);
    navigation()->arrange({value.x, value.y, compact_ ? value.width : extent, compact_ ? extent : value.height});
    content()->arrange({value.x + (compact_ ? 0 : extent), value.y + (compact_ ? extent : 0),
        value.width - (compact_ ? 0 : extent), value.height - (compact_ ? extent : 0)});
}
}
