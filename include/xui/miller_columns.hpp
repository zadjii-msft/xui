#pragma once
#include "xui/collections.hpp"

namespace xui {

struct MillerColumn {
    std::wstring title;
    std::shared_ptr<const ItemsSource> source;
    std::optional<ItemKey> selected;
};

class MillerColumns;
class MillerColumnList final : public VirtualCollection {
public:
    bool multiple_selection() const override { return false; }
    void select_all() override {}
    bool select(ItemKey key, SelectionGesture gesture = SelectionGesture::replace) override;
    bool remove_selection(ItemKey key);
    // Local list coordinates, or no point for keyboard context. Never raises selection or activation callbacks.
    bool prepare_context_menu(std::optional<Point> point = {});
    void step(int delta, SelectionGesture gesture = SelectionGesture::replace) override;
    void edge(bool last, SelectionGesture gesture = SelectionGesture::replace) override;
    void horizontal(bool right, SelectionGesture gesture) override;
    void set_presentation(ItemsPresentation value) override;
    std::vector<CollectionRow> visible_content() const override;
    void hover_pointer(std::optional<Point> point);
    std::optional<std::size_t> hovered_row() const;
    void cancel() override;
    void arrange(Rect bounds) override;
    MillerColumns* owner() const { return owner_; }
    std::size_t column_index() const { return index_; }
private:
    friend class MillerColumns;
    MillerColumnList(MillerColumns& owner, std::size_t index);
    bool available(ItemKey key) const;
    void replace(const MillerColumn& column);
    MillerColumns* owner_;
    std::size_t index_;
    std::shared_ptr<const ItemsSource> items_;
    bool reveal_selection_{};
    std::optional<Point> hover_pointer_;
};

// Sources are immutable, thread-safe snapshots. Applications own loading and delivery.
class MillerColumns final : public Control {
public:
    explicit MillerColumns(std::wstring name = L"Columns");
    ~MillerColumns() override;
    static constexpr std::size_t maximum_columns = 32;
    void set_columns(std::vector<MillerColumn> columns);
    const std::vector<MillerColumn>& columns() const { return columns_; }
    // Fixed retained slots can be configured before population. Dormant lists have no source and reject input.
    std::shared_ptr<MillerColumnList> column_list(std::size_t index) const;
    // Empty views report zero; setting an active index requires an existing column.
    std::size_t active_column() const { return active_; }
    void set_active_column(std::size_t index);
    float column_width() const { return width_; }
    void set_column_width(float width);
    double horizontal_offset() const { return offset_; }
    double maximum_horizontal() const;
    void set_horizontal_offset(double offset);
    void scroll_horizontal(double delta);
    Rect horizontal_track() const;
    Rect horizontal_thumb() const;
    Rect separator_bounds(std::size_t column) const;
    static constexpr float separator_width = 1;
    static constexpr float scrollbar_height = 12;
    void on_selection(std::function<void(std::size_t, ItemKey)> callback) { selection_ = std::move(callback); }
    void on_activate(std::function<void(std::size_t, ItemKey)> callback) { activate_ = std::move(callback); }
    // Adapter focus request for keyboard and navigation-button actions, not property setters.
    void on_focus_column(std::function<void(const std::shared_ptr<VirtualCollection>&)> callback) { focus_ = std::move(callback); }
    void move_active(bool right);
    const std::shared_ptr<Button>& previous_button() const { return previous_; }
    const std::shared_ptr<Button>& next_button() const { return next_; }
    Size measure(Size available) override;
    void arrange(Rect bounds) override;
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
private:
    friend class MillerColumnList;
    bool select_item(std::size_t index, ItemKey key, SelectionGesture gesture);
    void activate_item(std::size_t index, ItemKey key);
    void reveal_active();
    void layout();
    float effective_width() const;
    std::vector<MillerColumn> columns_;
    std::vector<std::shared_ptr<MillerColumnList>> lists_;
    std::vector<std::shared_ptr<Label>> headers_;
    std::vector<std::shared_ptr<Element>> children_;
    std::shared_ptr<Button> previous_, next_;
    std::size_t active_{};
    float width_{240};
    double offset_{};
    std::function<void(std::size_t, ItemKey)> selection_, activate_;
    std::function<void(const std::shared_ptr<VirtualCollection>&)> focus_;
};

}
