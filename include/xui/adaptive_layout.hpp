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
    Size measure_with_context(Size available, LayoutContext context) override;
    bool supports_axis_constraints() const override { return typeid(*this) == typeid(Grid); }
    void arrange(Rect bounds) override;
    void arrange_with_context(Rect bounds, LayoutContext context) override;
protected:
    std::optional<StyleTarget> control_style_target() const override;
private:
    struct Cell { std::size_t row, column, rows, columns; };
    std::pair<std::vector<float>, std::vector<float>> sizes(Size available, LayoutContext context);
    void arrange_cells(Rect bounds, LayoutContext context);
    std::vector<GridTrack> rows_{{}}, columns_{{}};
    std::vector<Cell> cells_;
    Insets padding_{};
    float horizontal_{}, vertical_{};
    bool padding_explicit_{}, gap_explicit_{};
    Insets layout_insets() const;
    float horizontal_gap() const;
    float vertical_gap() const;
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
protected:
    std::optional<StyleTarget> control_style_target() const override;
private:
    Size layout(Size available, bool arrange, Point origin);
    Insets padding_{};
    float spacing_{8}, width_{180};
    bool spacing_explicit_{}, padding_explicit_{};
    std::size_t columns_{1};
};
enum class CompactNavigation { stacked, overlay };
// Wide panes share a row. Compact navigation can stack or overlay the same retained content.
class AdaptiveLayout : public Stack {
public:
    AdaptiveLayout(std::shared_ptr<Element> navigation, std::shared_ptr<Element> content);
    void set_breakpoint(float width);
    void set_navigation_extent(float extent);
    void set_content_sized(bool value);
    bool content_sized() const { return content_sized_; }
    void set_compact_navigation(CompactNavigation mode);
    void set_navigation_open(bool open);
    const std::shared_ptr<Element>& navigation() const { return child_at(1); }
    const std::shared_ptr<Element>& content() const { return child_at(0); }
    bool overlay_active() const { return compact_ && mode_ == CompactNavigation::overlay && open_ && bounds().width > 0 && bounds().height > 0; }
    bool compact() const { return compact_; }
    Size measure(Size available) override;
    void arrange(Rect bounds) override;
protected:
    std::optional<StyleTarget> control_style_target() const override;
    StyleStateMask control_style_state_bits() const override;
private:
    float breakpoint_{640}, extent_{220};
    bool compact_{};
    bool content_sized_{};
    bool open_{true};
    CompactNavigation mode_{};
};
}
