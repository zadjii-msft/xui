#pragma once
#include "xui/collections.hpp"
#include <array>

namespace xui {
using RowKey = ItemKey;
struct GridColumn {
    std::wstring name;
    float width{120};
    bool numeric{};
    bool filterable{}, checkable{};
    bool operator==(const GridColumn&) const = default;
};
// Implementations are immutable and thread-safe. A key must never identify a different item.
// Providers and the renderer retain a source, not a visual or string cache for each row.
class GridSource : public CollectionIndex {
public:
    virtual ~GridSource() = default;
    virtual std::size_t size() const = 0;
    virtual RowKey key(std::size_t row) const = 0;
    virtual std::optional<std::size_t> find(RowKey key) const = 0;
    virtual std::wstring text(std::size_t row, std::size_t column) const = 0;
    virtual ItemVisual visual(std::size_t, std::size_t) const { return {}; }
};
enum class GridHeaderPart { sort, filter, check };
struct GridFilterRequest {
    std::uint64_t generation{};
    std::size_t column{};
    std::vector<std::wstring> filters;
    std::stop_token cancellation;
};
class DataGrid final : public Control {
public:
    explicit DataGrid(std::wstring name = L"Data");
    ~DataGrid() override { filter_stop_.request_stop(); }
    void set_columns(std::vector<GridColumn> columns);
    const auto& columns() const { return columns_; }
    // Columns are in display order. Source identities are the initial set_columns indices.
    // set_columns resets this permutation. set_source preserves order and widths.
    const auto& column_order() const { return column_order_; }
    std::size_t source_column(std::size_t display_column) const { return column_order_.at(display_column); }
    std::optional<std::size_t> display_column(std::size_t source_column) const;
    void set_column_order(std::vector<std::size_t> order);
    bool reorder_column(std::size_t from, std::size_t to);
    void set_source(std::shared_ptr<const GridSource> source);
    const auto& source() const { return source_; }
    std::optional<RowKey> selected() const { return selected_; }
    bool select(RowKey key, bool reveal = true);
    bool select(RowKey key, SelectionGesture gesture, bool reveal = true);
    const CollectionSelection& selection() const { return selection_; }
    void set_selection(CollectionSelection selection);
    void set_full_source(std::shared_ptr<const GridSource> source) {
        if (full_source_ == source) return;
        full_source_ = std::move(source); invalidate(Invalidation::paint);
    }
    const std::shared_ptr<const GridSource>& full_source() const { return full_source_; }
    void set_select_all_scope(SelectAllScope scope) { scope_ = scope; }
    void select_all();
    void set_checked(RowKey key, bool value);
    void toggle_check(std::optional<RowKey> key = {});
    SelectionState check_state() const { return selection_.state(source_, full_source_); }
    bool reveal(RowKey key);
    void clear_selection();
    // Pointer coordinates are local DIPs. No position means the keyboard context key.
    void prepare_context_menu(std::optional<Point> position);
    void step(int delta, SelectionGesture gesture = SelectionGesture::replace);
    void edge(bool last, SelectionGesture gesture = SelectionGesture::replace);
    void activate_selected();
    // Sort arguments and callbacks use source identities, not display ordinals.
    void sort(std::size_t column);
    void set_sort(std::size_t column, bool descending);
    std::size_t sort_column() const { return sort_; }
    bool descending() const { return descending_; }
    void on_sort(std::function<void(std::size_t, bool)> fn) { sort_callback_ = std::move(fn); }
    void on_select(std::function<void()> fn) { select_callback_ = std::move(fn); }
    void on_activate(std::function<void()> fn) { activate_callback_ = std::move(fn); }
    // Filter identities are source columns. Setters are silent; filter() starts external work.
    const std::vector<std::wstring>& filters() const { return filters_; }
    void set_filter(std::size_t column, std::wstring text);
    void filter(std::size_t column, std::wstring text);
    void open_filter(std::size_t column);
    void on_filter_open(std::function<void(std::size_t)> callback) { filter_open_ = std::move(callback); }
    void on_filter(std::function<void(GridFilterRequest)> callback) { filter_callback_ = std::move(callback); }
    bool complete_filter(GridFilterRequest request, std::shared_ptr<const GridSource> source);
    bool filter_pending() const { return filter_pending_; }
    void cancel() override;
    Rect header_part_bounds(std::size_t source_column, GridHeaderPart part) const;
    GridHeaderPart header_part_at(float x) const;
    void set_header_part(GridHeaderPart part);
    GridHeaderPart header_part() const { return header_part_; }
    void arrange(Rect bounds) override;
    void set_offset(double vertical, double horizontal);
    double offset() const { return offset_; }
    double horizontal_offset() const { return horizontal_; }
    double maximum_offset() const;
    double maximum_horizontal() const;
    float content_width() const;
    float viewport_height() const;
    float viewport_width() const;
    std::pair<std::size_t, std::size_t> visible_rows() const;
    std::optional<std::size_t> row_at(float y) const;
    void hover_pointer(std::optional<Point> position);
    std::optional<std::size_t> hovered_row() const;
    std::optional<std::size_t> column_at(float x) const;
    std::optional<std::size_t> resize_boundary(float x) const;
    Rect vertical_thumb() const;
    Rect horizontal_thumb() const;
    // Exact configured width (48..2000 DIPs), without resetting column order.
    void set_column_width(std::size_t column, float width);
    void resize_column(std::size_t column, float width);
    bool header_focus() const { return header_focus_; }
    std::size_t focused_column() const { return focused_column_; }
    void focus_header(bool value);
    void step_header(int delta);
    static constexpr float row_height = 32, header_height = 38, bar_width = 12;
private:
    void reveal_selection();
    std::shared_ptr<const GridSource> source_;
    std::shared_ptr<const GridSource> full_source_;
    CollectionSelection selection_;
    SelectAllScope scope_{SelectAllScope::filtered};
    std::vector<GridColumn> columns_;
    std::vector<std::size_t> column_order_;
    std::optional<RowKey> selected_;
    std::optional<Point> hover_pointer_;
    double offset_{}, horizontal_{};
    std::size_t sort_{}, focused_column_{};
    bool descending_{}, header_focus_{};
    GridHeaderPart header_part_{};
    std::vector<std::wstring> filters_;
    std::stop_source filter_stop_;
    std::uint64_t filter_generation_{};
    bool filter_pending_{};
    std::function<void(std::size_t)> filter_open_;
    std::function<void(GridFilterRequest)> filter_callback_;
    std::function<void(std::size_t, bool)> sort_callback_;
    std::function<void()> select_callback_, activate_callback_;
};

// Fixed storage. nullopt creates a gap rather than an invented zero.
class HistoryChart final : public Control {
public:
    explicit HistoryChart(std::wstring name = L"History");
    void append(std::optional<double> value);
    void set_scale(double maximum);
    double maximum() const { return maximum_; }
    std::size_t size() const { return size_; }
    std::optional<double> at(std::size_t index) const;
    static constexpr std::size_t capacity = 60;
private:
    std::array<std::optional<double>, capacity> values_{};
    std::size_t next_{}, size_{};
    double maximum_{100};
};
}
