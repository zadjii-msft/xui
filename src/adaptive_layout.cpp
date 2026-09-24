#include "xui/adaptive_layout.hpp"
#include "layout_styling.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace xui {
namespace {
void positive(float value) { if (!std::isfinite(value) || value < 0) throw std::invalid_argument("Layout size must be finite and nonnegative"); }
void padding_valid(Insets p) { for (auto v : {p.left, p.top, p.right, p.bottom}) positive(v); }
constexpr float unbounded_extent = (std::numeric_limits<float>::max)();
bool unbounded(float available) { return available >= unbounded_extent; }
float extent(double value, bool reject_overflow = false) {
    if (std::isnan(value) || value < 0) return 0;
    if (reject_overflow && value > unbounded_extent)
        throw std::overflow_error("Unbounded Grid content exceeds the supported layout extent");
    return static_cast<float>(std::min(value, static_cast<double>(unbounded_extent)));
}
double sum(const std::vector<float>& values, std::size_t first, std::size_t count, float gap) {
    return std::accumulate(values.begin() + first, values.begin() + first + count, 0.0) +
        static_cast<double>(gap) * (count ? count - 1 : 0);
}
struct TrackDemand { std::size_t first, count; float extent; };
std::vector<double> intrinsic_tracks(const std::vector<GridTrack>& definitions,
    std::vector<TrackDemand>& demands, float gap) {
    std::vector<double> result;
    result.reserve(definitions.size());
    for (const auto& track : definitions)
        result.push_back(track.sizing == TrackSizing::fixed ?
            std::clamp(track.value, track.minimum, track.maximum) : track.minimum);
    std::stable_sort(demands.begin(), demands.end(), [](const auto& a, const auto& b) { return a.count < b.count; });
    for (const auto& demand : demands) {
        positive(demand.extent);
        double covered = static_cast<double>(gap) * (demand.count - 1);
        for (auto i = demand.first; i < demand.first + demand.count; ++i) covered += result[i];
        double remaining = std::max(0.0, demand.extent - covered);
        for (std::size_t pass = 0; pass < demand.count && remaining > 0; ++pass) {
            std::size_t eligible{};
            for (auto i = demand.first; i < demand.first + demand.count; ++i)
                eligible += definitions[i].sizing != TrackSizing::fixed && result[i] < definitions[i].maximum;
            if (!eligible) break;
            const auto share = remaining / eligible;
            double consumed{};
            for (auto i = demand.first; i < demand.first + demand.count; ++i) {
                if (definitions[i].sizing == TrackSizing::fixed) continue;
                const auto add = std::min(share, definitions[i].maximum - result[i]);
                result[i] += add; consumed += add;
            }
            if (consumed <= 0) break;
            remaining = std::max(0.0, remaining - consumed);
        }
    }
    return result;
}
std::vector<float> tracks(const std::vector<GridTrack>& definitions, const std::vector<double>& desired,
    float available, float gap, bool unconstrained) {
    std::vector<double> sizes; double used = static_cast<double>(gap) * (definitions.size() - 1), weight{};
    for (std::size_t i = 0; i < definitions.size(); ++i) {
        const auto& t = definitions[i];
        sizes.push_back(t.sizing == TrackSizing::star && !unconstrained ? t.minimum : desired[i]);
        used += sizes.back();
        if (t.sizing == TrackSizing::star && sizes.back() < t.maximum) weight += t.value;
    }
    if (unconstrained) extent(used, true);
    double remaining = unconstrained ? 0 : std::max(0.0, available - used);
    // Saturated tracks return their remaining share to the other star tracks.
    for (std::size_t pass = 0; pass < definitions.size() && weight > 0 && remaining > 0.01f; ++pass) {
        double consumed{}, next_weight{};
        for (std::size_t i = 0; i < definitions.size(); ++i) {
            const auto& t = definitions[i]; if (t.sizing != TrackSizing::star || sizes[i] >= t.maximum) continue;
            const auto add = std::min(t.maximum - sizes[i], remaining * (t.value / weight));
            sizes[i] += add; consumed += add; if (sizes[i] < t.maximum) next_weight += t.value;
        }
        remaining -= consumed; weight = next_weight;
    }
    std::vector<float> result; result.reserve(sizes.size());
    for (auto value : sizes) result.push_back(extent(value));
    return result;
}
std::pair<float, float> cell_span(const std::vector<float>& tracks, std::size_t first,
    std::size_t count, float gap, float available) {
    const auto offset = first ? sum(tracks, 0, first, gap) + gap : 0;
    const auto start = std::min(static_cast<double>(available), offset);
    return {extent(start), extent(std::min(sum(tracks, first, count, gap), available - start))};
}
bool intrinsic_span(const std::vector<GridTrack>& tracks, std::size_t first, std::size_t count) {
    return std::any_of(tracks.begin() + first, tracks.begin() + first + count,
        [](const auto& track) { return track.sizing != TrackSizing::fixed; });
}
float fixed_span_offer(const std::vector<GridTrack>& tracks, std::size_t first,
    std::size_t count, float gap, float available) {
    double total = static_cast<double>(gap) * (count - 1);
    for (auto i = first; i < first + count; ++i)
        total += std::clamp(tracks[i].value, tracks[i].minimum, tracks[i].maximum);
    return extent(std::min(static_cast<double>(available), total));
}
}
Grid::Grid() : Stack(Axis::vertical) { set_auto_size(true); }
std::optional<StyleTarget> Grid::control_style_target() const { return StyleTarget::grid; }
Insets Grid::layout_insets() const {
    auto result = layout_style::insets(effective_control_style_values(StylePart::root), padding_, padding_explicit_);
    result.bottom += effective_separator_inset();
    return result;
}
float Grid::horizontal_gap() const {
    const auto* values = effective_control_style_values(StylePart::root);
    return !gap_explicit_ && values ? values->column_gap.value_or(values->spacing.value_or(horizontal_)) : horizontal_;
}
float Grid::vertical_gap() const {
    const auto* values = effective_control_style_values(StylePart::root);
    return !gap_explicit_ && values ? values->row_gap.value_or(values->spacing.value_or(vertical_)) : vertical_;
}
void Grid::set_tracks(std::vector<GridTrack> rows, std::vector<GridTrack> columns) {
    for (const auto* list : {&rows, &columns}) {
        if (list->empty() || list->size() > 256) throw std::invalid_argument("Grid requires 1 to 256 tracks");
        for (const auto& t : *list) {
            if (t.sizing < TrackSizing::fixed || t.sizing > TrackSizing::star)
                throw std::invalid_argument("Invalid grid track sizing");
            positive(t.value); positive(t.minimum); positive(t.maximum);
            if (t.minimum > t.maximum || (t.sizing == TrackSizing::star && t.value == 0)) throw std::invalid_argument("Invalid grid track");
        }
    }
    for (const auto& cell : cells_) if (cell.row + cell.rows > rows.size() || cell.column + cell.columns > columns.size())
        throw std::invalid_argument("Existing cell exceeds new tracks");
    if (rows_ == rows && columns_ == columns) return;
    rows_ = std::move(rows); columns_ = std::move(columns); invalidate(Invalidation::layout);
}
void Grid::set_gap(float horizontal, float vertical) { positive(horizontal); positive(vertical); if (gap_explicit_ && horizontal_ == horizontal && vertical_ == vertical) return; gap_explicit_ = true; horizontal_ = horizontal; vertical_ = vertical; invalidate(Invalidation::layout); }
void Grid::set_padding(Insets value) {
    padding_valid(value);
    if (padding_explicit_ && padding_.left == value.left && padding_.top == value.top && padding_.right == value.right && padding_.bottom == value.bottom) return;
    padding_explicit_ = true;
    padding_ = value; invalidate(Invalidation::layout);
}
void Grid::add(std::shared_ptr<Element> child, std::size_t row, std::size_t column, std::size_t row_span, std::size_t column_span) {
    if (!row_span || !column_span || row >= rows_.size() || column >= columns_.size() ||
        row_span > rows_.size() - row || column_span > columns_.size() - column) throw std::invalid_argument("Grid cell exceeds tracks");
    cells_.reserve(cells_.size() + 1);
    Stack::add(std::move(child)); cells_.push_back({row, column, row_span, column_span});
}
std::pair<std::vector<float>, std::vector<float>> Grid::sizes(Size available, LayoutContext context) {
    const auto padding = layout_insets();
    const auto column_gap = horizontal_gap(), row_gap = vertical_gap();
    const bool unbounded_width = context.unbounded_width, unbounded_height = context.unbounded_height;
    available.width = extent(static_cast<double>(available.width) - padding.left - padding.right);
    available.height = extent(static_cast<double>(available.height) - padding.top - padding.bottom);
    std::vector<TrackDemand> demands; demands.reserve(cells_.size());
    const auto measure_child = [&](std::size_t i, Size offered) {
        const auto& cell = cells_[i];
        const LayoutContext child_context{
            context.unbounded_width && intrinsic_span(columns_, cell.column, cell.columns),
            context.unbounded_height && intrinsic_span(rows_, cell.row, cell.rows)};
        if (context.unbounded_width && !child_context.unbounded_width)
            offered.width = fixed_span_offer(columns_, cell.column, cell.columns, column_gap, offered.width);
        if (context.unbounded_height && !child_context.unbounded_height)
            offered.height = fixed_span_offer(rows_, cell.row, cell.rows, row_gap, offered.height);
        return child_at(i)->measure_with_context(offered, child_context);
    };
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const auto& c = cells_[i];
        const auto m = measure_child(i, available);
        demands.push_back({c.column, c.columns, m.width});
    }
    auto columns = tracks(columns_, intrinsic_tracks(columns_, demands, column_gap), available.width, column_gap, unbounded_width);
    demands.clear();
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const auto& c = cells_[i];
        const auto [offset, width] = cell_span(columns, c.column, c.columns, column_gap, available.width);
        const auto m = measure_child(i, {width, available.height});
        demands.push_back({c.row, c.rows, m.height});
    }
    return {tracks(rows_, intrinsic_tracks(rows_, demands, row_gap), available.height, row_gap, unbounded_height), std::move(columns)};
}
Size Grid::measure(Size available) {
    return measure_with_context(available, {unbounded(available.width), unbounded(available.height)});
}
Size Grid::measure_with_context(Size available, LayoutContext context) {
    context = constrain_layout_context(context);
    return measure_axes(available, [&](Size offered, bool natural) {
        if (!natural && !auto_size()) return base_measure(offered, false);
        const auto padding = layout_insets();
        auto [rows, columns] = sizes(offered, context);
        const Size desired{
            extent(sum(columns, 0, columns.size(), horizontal_gap()) + padding.left + padding.right, unbounded(offered.width)),
            extent(sum(rows, 0, rows.size(), vertical_gap()) + padding.top + padding.bottom, unbounded(offered.height))};
        return constrain_measure(desired, offered, natural);
    });
}
void Grid::arrange(Rect value) {
    arrange_cells(value, {});
}
void Grid::arrange_with_context(Rect value, LayoutContext context) {
    arrange_cells(value, constrain_layout_context(context));
}
void Grid::arrange_cells(Rect value, LayoutContext context) {
    const auto padding = layout_insets();
    const auto column_gap = horizontal_gap(), row_gap = vertical_gap();
    Element::arrange(value); value = bounds(); auto [rows, columns] = sizes({value.width, value.height}, context);
    const float left = std::min(padding.left, value.width), top = std::min(padding.top, value.height);
    const auto width = extent(static_cast<double>(value.width) - padding.left - padding.right);
    const auto height = extent(static_cast<double>(value.height) - padding.top - padding.bottom);
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const auto& c = cells_[i];
        const auto [x, cell_width] = cell_span(columns, c.column, c.columns, column_gap, width);
        const auto [y, cell_height] = cell_span(rows, c.row, c.rows, row_gap, height);
        Rect area{value.x + left + x, value.y + top + y, cell_width, cell_height};
        const auto* style = effective_control_style_values(StylePart::root);
        if (style) area = layout_style::aligned(area, child_at(i)->measure({area.width, area.height}), style);
        child_at(i)->arrange_with_context(area,
            {context.unbounded_width && intrinsic_span(columns_, c.column, c.columns),
                context.unbounded_height && intrinsic_span(rows_, c.row, c.rows)});
    }
}
Wrap::Wrap() : Stack(Axis::horizontal) { set_auto_size(true); }
std::optional<StyleTarget> Wrap::control_style_target() const { return StyleTarget::wrap; }
void Wrap::set_spacing(float value) { positive(value); if (spacing_explicit_ && spacing_ == value) return; spacing_explicit_ = true; spacing_ = value; invalidate(Invalidation::layout); }
void Wrap::set_padding(Insets value) {
    padding_valid(value);
    if (padding_explicit_ && padding_.left == value.left && padding_.top == value.top && padding_.right == value.right && padding_.bottom == value.bottom) return;
    padding_explicit_ = true;
    padding_ = value; invalidate(Invalidation::layout);
}
void Wrap::set_item_width(float value) { positive(value); if (value < 1) throw std::invalid_argument("Wrap width must be positive"); if (width_ == value) return; width_ = value; invalidate(Invalidation::layout); }
Size Wrap::layout(Size available, bool arrange, Point origin) {
    const auto* values = effective_control_style_values(StylePart::root);
    auto padding = layout_style::insets(values, padding_, padding_explicit_);
    padding.bottom += effective_separator_inset();
    const auto spacing = !spacing_explicit_ && values ? values->spacing.value_or(spacing_) : spacing_;
    const auto column_gap = !spacing_explicit_ && values ? values->column_gap.value_or(spacing) : spacing;
    const auto row_gap = !spacing_explicit_ && values ? values->row_gap.value_or(spacing) : spacing;
    const auto width = std::max(0.0f, available.width - padding.left - padding.right);
    columns_ = static_cast<std::size_t>(std::max(1.0f, std::min(float(std::max(std::size_t{1}, child_count())), std::floor((width + column_gap) / (width_ + column_gap)))));
    const float cell = std::max(0.0f, (width - column_gap * (columns_ - 1)) / columns_);
    float y = padding.top;
    for (std::size_t start = 0; start < child_count(); start += columns_) {
        float height{};
        for (auto i = start; i < std::min(child_count(), start + columns_); ++i)
            height = std::max(height, child_at(i)->measure({cell, available.height}).height);
        if (arrange) for (auto i = start; i < std::min(child_count(), start + columns_); ++i) {
            const Rect area{origin.x + padding.left + (i - start) * (cell + column_gap), origin.y + y, cell,
                std::max(0.0f, std::min(height, available.height - padding.bottom - y))};
            child_at(i)->arrange(values ? layout_style::aligned(area, child_at(i)->measure({area.width, area.height}), values) : area);
        }
        y += height + row_gap;
    }
    return {available.width, y + padding.bottom - (child_count() ? row_gap : 0)};
}
Size Wrap::measure(Size available) { return auto_size() ? constrain(layout(available, false, {}), available) : Element::measure(available); }
void Wrap::arrange(Rect value) { Element::arrange(value); value = bounds(); layout({value.width, value.height}, true, {value.x, value.y}); }
AdaptiveLayout::AdaptiveLayout(std::shared_ptr<Element> navigation, std::shared_ptr<Element> content) : Stack(Axis::horizontal) {
    add(std::move(content)); add(std::move(navigation)); set_preferred_size({800, 360});
}
std::optional<StyleTarget> AdaptiveLayout::control_style_target() const { return StyleTarget::adaptive_layout; }
StyleStateMask AdaptiveLayout::control_style_state_bits() const {
    return Element::control_style_state_bits() | (compact_ ? style_states::compact : 0) |
        (!compact_ || mode_ != CompactNavigation::overlay || open_ ? style_states::expanded : 0);
}
void AdaptiveLayout::set_breakpoint(float value) { positive(value); if (breakpoint_ == value) return; breakpoint_ = value; invalidate(Invalidation::layout); }
void AdaptiveLayout::set_navigation_extent(float value) { positive(value); if (extent_ == value) return; extent_ = value; invalidate(Invalidation::layout); }
void AdaptiveLayout::set_content_sized(bool value) { if (content_sized_ == value) return; content_sized_ = value; invalidate(Invalidation::layout); }
void AdaptiveLayout::set_compact_navigation(CompactNavigation value) { if (mode_ == value) return; mode_ = value; invalidate_control_style_state(); invalidate(Invalidation::layout); }
void AdaptiveLayout::set_navigation_open(bool value) { if (open_ == value) return; open_ = value; invalidate_control_style_state(); invalidate(Invalidation::layout); }
Size AdaptiveLayout::measure(Size available) {
    if (!auto_size()) return Element::measure(available);
    if (content_sized_) {
        const auto p = effective_layout_insets();
        const auto inner = layout_style::inner(available, p);
        const Size natural{(std::numeric_limits<float>::max)(), inner.height};
        const auto nav = navigation()->measure(natural), body = content()->measure(natural);
        const auto gap = effective_spacing();
        const bool compact = inner.width < nav.width + gap + body.width;
        const auto desired = compact && mode_ == CompactNavigation::overlay ? body :
            compact ? Size{std::max(nav.width, body.width), nav.height + gap + body.height} :
            Size{nav.width + gap + body.width, std::max(nav.height, body.height)};
        return constrain(layout_style::outer(desired, p), available);
    }
    if (compact_ != (available.width < breakpoint_)) {
        compact_ = available.width < breakpoint_;
        invalidate_control_style_state();
    }
    const auto p = effective_layout_insets();
    auto inner = layout_style::inner(available, p);
    if (compact_ && mode_ == CompactNavigation::overlay)
        return constrain(layout_style::outer(content()->measure(inner), p), available);
    const auto gap = std::min(effective_spacing(), compact_ ? inner.height : inner.width);
    const auto extent = std::min(extent_, ((compact_ ? inner.height : inner.width) - gap) * 0.5f);
    const auto navigation_size = navigation()->measure({compact_ ? inner.width : extent, compact_ ? extent : inner.height});
    const auto content_size = content()->measure({inner.width - (compact_ ? 0 : extent + gap),
        inner.height - (compact_ ? extent + gap : 0)});
    return constrain(layout_style::outer(compact_ ? Size{std::max(navigation_size.width, content_size.width), extent + gap + content_size.height} :
        Size{extent + gap + content_size.width, std::max(navigation_size.height, content_size.height)}, p), available);
}
void AdaptiveLayout::arrange(Rect value) {
    Element::arrange(value); value = bounds();
    if (content_sized_) {
        value = layout_style::inset(value, effective_layout_insets());
        const Size natural{(std::numeric_limits<float>::max)(), value.height};
        const auto nav = navigation()->measure(natural), body = content()->measure(natural);
        const bool compact = value.width < nav.width + effective_spacing() + body.width;
        if (compact_ != compact) { compact_ = compact; invalidate_control_style_state(); }
        if (compact_ && mode_ == CompactNavigation::overlay) {
            content()->arrange(value);
            navigation()->arrange(open_ ? Rect{value.x, value.y, std::min(nav.width, value.width * 0.85f), value.height} : Rect{});
            return;
        }
        const auto gap = std::min(effective_spacing(), compact_ ? value.height : value.width);
        const auto extent = std::min(compact_ ? nav.height : nav.width, (compact_ ? value.height : value.width) - gap);
        navigation()->arrange({value.x, value.y, compact_ ? value.width : extent, compact_ ? extent : value.height});
        content()->arrange({value.x + (compact_ ? 0 : extent + gap), value.y + (compact_ ? extent + gap : 0),
            value.width - (compact_ ? 0 : extent + gap), value.height - (compact_ ? extent + gap : 0)});
        return;
    }
    if (compact_ != (value.width < breakpoint_)) {
        compact_ = value.width < breakpoint_;
        invalidate_control_style_state();
    }
    value = layout_style::inset(value, effective_layout_insets());
    if (compact_ && mode_ == CompactNavigation::overlay) {
        content()->arrange(value);
        navigation()->arrange(open_ ? Rect{value.x, value.y, std::min(extent_, value.width * 0.85f), value.height} : Rect{});
        return;
    }
    const auto gap = std::min(effective_spacing(), compact_ ? value.height : value.width);
    const auto extent = std::min(extent_, ((compact_ ? value.height : value.width) - gap) * 0.5f);
    navigation()->arrange({value.x, value.y, compact_ ? value.width : extent, compact_ ? extent : value.height});
    content()->arrange({value.x + (compact_ ? 0 : extent + gap), value.y + (compact_ ? extent + gap : 0),
        value.width - (compact_ ? 0 : extent + gap), value.height - (compact_ ? extent + gap : 0)});
}
}
