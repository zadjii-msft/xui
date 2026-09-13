#pragma once
#include "xui/core.hpp"

namespace xui {
enum class TrackSizing { fixed, automatic, star };
struct GridTrack {
    TrackSizing sizing{TrackSizing::star};
    float value{1}, minimum{}, maximum{(std::numeric_limits<float>::max)()};
    bool operator==(const GridTrack&) const = default;
};
// These panels retain the same children at every width. Stack supplies ownership and traversal.
class Grid : public Stack {
public:
    Grid();
    void set_tracks(std::vector<GridTrack> rows, std::vector<GridTrack> columns);
    void set_gap(float horizontal, float vertical);
    void set_padding(Insets padding);
    void add(std::shared_ptr<Element> child, std::size_t row, std::size_t column,
        std::size_t row_span = 1, std::size_t column_span = 1);
    Size measure(Size available) override;
    void arrange(Rect bounds) override;
private:
    struct Cell { std::size_t row, column, rows, columns; };
    std::pair<std::vector<float>, std::vector<float>> sizes(Size available);
    std::vector<GridTrack> rows_{{}}, columns_{{}};
    std::vector<Cell> cells_;
    Insets padding_{};
    float horizontal_{}, vertical_{};
};
class Wrap : public Stack {
public:
    Wrap();
    void set_spacing(float spacing);
    void set_padding(Insets padding);
    void set_item_width(float minimum);
    using Stack::add;
    Size measure(Size available) override;
    void arrange(Rect bounds) override;
    std::size_t columns() const { return columns_; }
private:
    Size layout(Size available, bool arrange, Point origin);
    Insets padding_{};
    float spacing_{8}, width_{180};
    std::size_t columns_{1};
};
enum class CompactNavigation { stacked, overlay };
// Wide panes share a row. Compact navigation can stack or overlay the same retained content.
class AdaptiveLayout : public Stack {
public:
    AdaptiveLayout(std::shared_ptr<Element> navigation, std::shared_ptr<Element> content);
    void set_breakpoint(float width);
    void set_navigation_extent(float extent);
    void set_compact_navigation(CompactNavigation mode);
    void set_navigation_open(bool open);
    const std::shared_ptr<Element>& navigation() const { return child_at(1); }
    const std::shared_ptr<Element>& content() const { return child_at(0); }
    bool overlay_active() const { return compact_ && mode_ == CompactNavigation::overlay && open_ && bounds().width > 0 && bounds().height > 0; }
    bool compact() const { return compact_; }
    Size measure(Size available) override;
    void arrange(Rect bounds) override;
private:
    float breakpoint_{640}, extent_{220};
    bool compact_{};
    bool open_{true};
    CompactNavigation mode_{};
};
}
