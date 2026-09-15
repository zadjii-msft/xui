#pragma once
#include "xui/commands.hpp"
#include "xui/adaptive_layout.hpp"

namespace xui {
enum class NavigationSection { header, main, footer };
struct NavigationItem {
    ItemKey key;
    std::optional<ItemKey> parent;
    std::wstring label;
    ButtonIcon icon{ButtonIcon::none};
    std::wstring keywords, badge;
    bool enabled{true}, selectable{true}, expanded{true};
    NavigationSection section{NavigationSection::main};
};

class NavigationView;
// Virtual rows use the existing tree input and accessibility adapters.
class NavigationList final : public VirtualCollection {
public:
    bool multiple_selection() const override { return false; }
    void select_all() override {}
    bool select(ItemKey key, SelectionGesture gesture = SelectionGesture::replace) override;
    void step(int delta, SelectionGesture gesture = SelectionGesture::replace) override;
    void edge(bool last, SelectionGesture gesture = SelectionGesture::replace) override;
    bool disclose(ItemKey key, bool expanded) override;
    void horizontal(bool right, SelectionGesture gesture) override;
    std::vector<CollectionRow> visible_content() const override;
    void hover_item(std::optional<ItemKey> key);
    bool disclosure_hit(Point point) const;
    bool remove_selection(ItemKey key);
    void arrange(Rect bounds) override;
private:
    friend class NavigationView;
    explicit NavigationList(std::wstring name, NavigationView& owner);
    NavigationView* owner_;
    std::optional<ItemKey> hovered_item_;
    bool reveal_focus_{};
    void replace(std::shared_ptr<const ItemsSource> source, std::optional<ItemKey> selected);
};

class NavigationView final : public Control {
public:
    explicit NavigationView(std::wstring name = L"Navigation");
    ~NavigationView() override;
    // Keys are unique across sections. Parents precede children in display order, not necessarily input order.
    void set_items(std::vector<NavigationItem> items);
    const std::vector<NavigationItem>& entries() const { return entries_; }
    const NavigationItem* find(ItemKey key) const;
    bool expanded() const { return expanded_; }
    void set_expanded(bool value);
    void set_pane_widths(float expanded, float collapsed);
    bool item_expanded(ItemKey key) const;
    bool set_item_expanded(ItemKey key, bool value);
    const std::wstring& filter() const { return filter_; }
    void set_filter(std::wstring query);
    std::size_t match_count() const { return matches_; }
    bool item_matches(ItemKey key) const { return matching_.contains(key); }
    std::optional<ItemKey> selected() const { return selected_; }
    // Select reveals ancestors and raises on_select only when the selected key changes.
    bool select(ItemKey key);
    void clear_selection();
    void on_select(std::function<void(ItemKey)> callback) { select_ = std::move(callback); }
    void on_activate(std::function<void(ItemKey)> callback) { activate_ = std::move(callback); }
    void on_filter(std::function<void(const std::wstring&)> callback) { filter_changed_ = std::move(callback); }
    void on_expanded(std::function<void(bool)> callback) { expanded_changed_ = std::move(callback); }
    const std::shared_ptr<TextInput>& search() const { return search_; }
    const std::shared_ptr<Button>& toggle_button() const { return toggle_; }
    const std::shared_ptr<NavigationList>& items() const { return main_; }
    const std::shared_ptr<NavigationList>& header_items() const { return header_; }
    const std::shared_ptr<NavigationList>& footer_items() const { return footer_; }
    void set_search_visible(bool value);
    bool search_visible() const { return search_visible_; }
    Size measure(Size available) override;
    void arrange(Rect bounds) override;
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    static constexpr std::size_t maximum_items = 4096, maximum_depth = 64;
private:
    friend class NavigationList;
    void presentation_changed() override;
    void rebuild();
    bool effective_enabled(ItemKey key) const;
    bool has_children(ItemKey key) const;
    bool expanded_state(const NavigationItem& item) const;
    bool navigate(ItemKey key);
    void activate_item(ItemKey key);
    std::vector<NavigationItem> entries_;
    std::map<ItemKey, std::size_t> index_;
    std::set<ItemKey> closed_, matching_, filter_matches_;
    std::map<ItemKey, bool> filter_expansion_;
    std::optional<ItemKey> selected_;
    std::wstring filter_;
    std::size_t matches_{};
    float expanded_width_{280}, collapsed_width_{64};
    bool expanded_{true}, search_visible_{true};
    std::shared_ptr<Button> toggle_;
    std::shared_ptr<Label> title_, empty_;
    std::shared_ptr<TextInput> search_;
    std::shared_ptr<NavigationList> header_, main_, footer_;
    std::vector<std::shared_ptr<Element>> children_;
    std::function<void(ItemKey)> select_, activate_;
    std::function<void(const std::wstring&)> filter_changed_;
    std::function<void(bool)> expanded_changed_;
};

struct PathSegment {
    ItemKey key;
    std::wstring label;
    bool operator==(const PathSegment&) const = default;
};
class Breadcrumb final : public Control {
public:
    explicit Breadcrumb(std::wstring name = L"Current location");
    ~Breadcrumb() override;
    void set_segments(std::vector<PathSegment> segments);
    const std::vector<PathSegment>& segments() const { return segments_; }
    std::optional<ItemKey> current() const;
    void on_navigate(std::function<void(ItemKey)> callback);
    void on_overflow(std::function<void()> callback);
    const std::shared_ptr<Button>& overflow_button() const { return overflow_; }
    std::shared_ptr<const CommandSet> overflow_commands() const;
    Control* adjacent(const Control& current, int direction) const;
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
private:
    struct State { std::function<void(ItemKey)> navigate; };
    std::shared_ptr<State> state_{std::make_shared<State>()};
    std::vector<PathSegment> segments_;
    std::vector<std::shared_ptr<Element>> children_;
    std::shared_ptr<Button> overflow_;
    std::size_t first_{};
};
struct NavigationQuery {
    std::wstring text;
    std::uint64_t generation{};
    std::stop_token cancellation;
};
// No filesystem operations. Sources provide cached, immutable item snapshots.
class NavigationPane final : public Control {
public:
    explicit NavigationPane(std::wstring name = L"Quick access");
    ~NavigationPane() override;
    const std::shared_ptr<ItemsView>& items() const { return items_; }
    const std::shared_ptr<Expander>& group() const { return group_; }
    const std::shared_ptr<Progress>& progress() const { return progress_; }
    void set_items(std::shared_ptr<const ItemsSource> source);
    void on_navigate(std::function<void(ItemKey)> callback);
    void on_query(std::function<void(NavigationQuery)> callback) { query_ = std::move(callback); }
    NavigationQuery request(std::wstring text);
    bool complete(const NavigationQuery& request, std::shared_ptr<const ItemsSource> source, std::wstring error = {});
    void cancel() override;
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
private:
    std::shared_ptr<ItemsView> items_;
    std::shared_ptr<Expander> group_;
    std::shared_ptr<Progress> progress_;
    std::shared_ptr<Label> status_;
    std::vector<std::shared_ptr<Element>> children_;
    std::stop_source stop_;
    std::uint64_t generation_{};
    std::function<void(NavigationQuery)> query_;
};
class LocationPicker final {
public:
    explicit LocationPicker(std::wstring name = L"Choose location");
    ~LocationPicker();
    const std::shared_ptr<TextInput>& editor() const { return editor_; }
    const std::shared_ptr<NavigationPane>& navigation() const { return navigation_; }
    const std::shared_ptr<CommandBar>& toolbar() const { return toolbar_; }
    const std::shared_ptr<Popup>& popup() const { return popup_; }
private:
    std::shared_ptr<TextInput> editor_;
    std::shared_ptr<NavigationPane> navigation_;
    std::shared_ptr<CommandBar> toolbar_;
    std::shared_ptr<Popup> popup_;
};
// Radio choices and a separate vertical size editor change a real ItemsView.
class ViewPicker final {
public:
    explicit ViewPicker(std::shared_ptr<ItemsView> target);
    ~ViewPicker();
    const std::shared_ptr<Popup>& popup() const { return popup_; }
    const std::shared_ptr<RadioGroup>& choices() const { return choices_; }
    const std::shared_ptr<RangeInput>& size() const { return size_; }
private:
    std::shared_ptr<Popup> popup_;
    std::shared_ptr<RadioGroup> choices_;
    std::shared_ptr<RangeInput> size_;
};
}
