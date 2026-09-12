#pragma once
#include "xui/controls.hpp"
#include <array>

namespace xui {
struct RowKey {
    std::uint64_t id{}, version{};
    auto operator<=>(const RowKey&) const = default;
};
struct GridColumn {
    std::wstring name;
    float width{120};
    bool numeric{};
    bool operator==(const GridColumn&) const = default;
};
// Implementations are immutable and thread-safe. A key must never identify a different item.
// Providers and the renderer retain a source, not a visual or string cache for each row.
class GridSource {
public:
    virtual ~GridSource() = default;
    virtual std::size_t size() const = 0;
    virtual RowKey key(std::size_t row) const = 0;
    virtual std::optional<std::size_t> find(RowKey key) const = 0;
    virtual std::wstring text(std::size_t row, std::size_t column) const = 0;
};
class DataGrid final : public Control {
public:
    explicit DataGrid(std::wstring name = L"Data");
    void set_columns(std::vector<GridColumn> columns);
    const auto& columns() const { return columns_; }
    void set_source(std::shared_ptr<const GridSource> source);
    const auto& source() const { return source_; }
    std::optional<RowKey> selected() const { return selected_; }
    bool select(RowKey key, bool reveal = true);
    bool reveal(RowKey key);
    void clear_selection();
    void step(int delta);
    void edge(bool last);
    void activate_selected();
    void sort(std::size_t column);
    void set_sort(std::size_t column, bool descending);
    std::size_t sort_column() const { return sort_; }
    bool descending() const { return descending_; }
    void on_sort(std::function<void(std::size_t, bool)> fn) { sort_callback_ = std::move(fn); }
    void on_select(std::function<void()> fn) { select_callback_ = std::move(fn); }
    void on_activate(std::function<void()> fn) { activate_callback_ = std::move(fn); }
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
    std::optional<std::size_t> column_at(float x) const;
    Rect vertical_thumb() const;
    Rect horizontal_thumb() const;
    void resize_column(std::size_t column, float width);
    bool header_focus() const { return header_focus_; }
    std::size_t focused_column() const { return focused_column_; }
    void focus_header(bool value);
    void step_header(int delta);
    static constexpr float row_height = 32, header_height = 38, bar_width = 12;
private:
    void reveal_selection();
    std::shared_ptr<const GridSource> source_;
    std::vector<GridColumn> columns_;
    std::optional<RowKey> selected_;
    double offset_{}, horizontal_{};
    std::size_t sort_{}, focused_column_{};
    bool descending_{}, header_focus_{};
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
