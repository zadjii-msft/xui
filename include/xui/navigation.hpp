#pragma once
#include "xui/commands.hpp"
#include "xui/adaptive_layout.hpp"
#include "xui/animation.hpp"

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
    std::wstring image_path;
};

class NavigationView;
// Virtual rows use the existing tree input and accessibility adapters.
class NavigationList final : public VirtualCollection, public Animation {
public:
    ~NavigationList() override;
    bool animating() const override;
    void advance(Clock::time_point now) override;
    void settle() override;
    bool multiple_selection() const override { return false; }
    void select_all() override {}
    bool select(ItemKey key, SelectionGesture gesture = SelectionGesture::replace) override;
    void step(int delta, SelectionGesture gesture = SelectionGesture::replace) override;
    void edge(bool last, SelectionGesture gesture = SelectionGesture::replace) override;
    bool disclose(ItemKey key, bool expanded) override;
    void horizontal(bool right, SelectionGesture gesture) override;
    std::vector<CollectionRow> visible_content() const override;
    void hover_item(std::optional<ItemKey> key);
    std::optional<ItemKey> hovered_item() const { return hovered_item_; }
    std::uint64_t hover_revision() const { return hover_revision_; }
    std::optional<Rect> hover_anchor() const;
    void request_hover_help();
    bool disclosure_hit(Point point) const;
    bool remove_selection(ItemKey key);
    bool prepare_context_menu(std::optional<Point> position);
    void arrange(Rect bounds) override;
private:
    friend class NavigationView;
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::navigation_list; }
    explicit NavigationList(std::wstring name, NavigationView& owner);
    NavigationView* owner_;
    std::optional<ItemKey> hovered_item_;
    std::uint64_t hover_revision_{};
    bool hover_requested_{};
    bool reveal_focus_{};
    void replace(std::shared_ptr<const ItemsSource> source, std::optional<ItemKey> selected,
        std::optional<ItemKey> transition = {});
    void collection_presentation_retired() override;
    struct Motion;
    std::unique_ptr<Motion> motion_;
    std::uint64_t presentation_version_{};
    std::shared_ptr<const detail::CollectionPresentation> motion_frame();
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
    void set_duration(unsigned milliseconds);
    unsigned duration() const { return duration_; }
    bool animating() const;
    bool item_expanded(ItemKey key) const;
    bool set_item_expanded(ItemKey key, bool value);
    const std::wstring& filter() const { return filter_; }
    void set_filter(std::wstring query);
    std::size_t match_count() const { return matches_; }
    bool item_matches(ItemKey key) const { return matching_.contains(key); }
    std::optional<ItemKey> selected() const { return selected_; }
    // Select reveals ancestors and raises on_select only when the selected key changes.
    bool select(ItemKey key);
    // Silent model projection, including while the whole navigation is disabled.
    void set_selection(std::optional<ItemKey> key);
    void clear_selection();
    void on_select(std::function<void(ItemKey)> callback) { select_ = std::move(callback); }
    void on_activate(std::function<void(ItemKey)> callback) { activate_ = std::move(callback); }
    void on_filter(std::function<void(const std::wstring&)> callback) { filter_changed_ = std::move(callback); }
    void on_expanded(std::function<void(bool)> callback) { expanded_changed_ = std::move(callback); }
    // Hover changes cancel earlier work. Requests occur only after the native tooltip delay.
    void on_hover_changed(std::function<void(std::optional<ItemKey>)> callback) { hover_changed_ = std::move(callback); }
    void on_hover_requested(std::function<void(ItemKey)> callback) { hover_requested_ = std::move(callback); }
    bool set_hover_help(ItemKey key, std::wstring text);
    void set_hover_delay(unsigned milliseconds);
    const std::shared_ptr<TextInput>& search() const { return search_; }
    const std::shared_ptr<Button>& toggle_button() const { return toggle_; }
    const std::shared_ptr<NavigationList>& items() const { return main_; }
    const std::shared_ptr<NavigationList>& header_items() const { return header_; }
    const std::shared_ptr<NavigationList>& footer_items() const { return footer_; }
    const std::shared_ptr<Label>& title() const { return title_; }
    const std::shared_ptr<Label>& empty_message() const { return empty_; }
    void set_search_visible(bool value);
    bool search_visible() const { return search_visible_; }
    void set_header_visible(bool value);
    bool header_visible() const { return header_visible_; }
    Size measure(Size available) override;
    void arrange(Rect bounds) override;
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    static constexpr std::size_t maximum_items = 4096, maximum_depth = 64;
private:
    friend class NavigationList;
    bool select_item(ItemKey key, bool interactive);
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::navigation_view; }
    StyleStateMask control_style_state_bits() const override;
    void presentation_changed() override;
    void rebuild(std::optional<ItemKey> transition = {});
    void settle_motion();
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
    unsigned duration_{};
    float expanded_width_{280}, collapsed_width_{64};
    bool expanded_{true}, search_visible_{true}, header_visible_{true};
    std::shared_ptr<Button> toggle_;
    std::shared_ptr<Label> title_, empty_;
    std::shared_ptr<TextInput> search_;
    std::shared_ptr<NavigationList> header_, main_, footer_;
    std::vector<std::shared_ptr<Element>> children_;
    std::function<void(ItemKey)> select_, activate_;
    std::function<void(const std::wstring&)> filter_changed_;
    std::function<void(bool)> expanded_changed_;
    std::function<void(std::optional<ItemKey>)> hover_changed_;
    std::function<void(ItemKey)> hover_requested_;
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
    void set_button_invoked_handler(std::function<void(const Button&)> handler);
    void on_overflow(std::function<void()> callback);
    const std::shared_ptr<Button>& overflow_button() const { return overflow_; }
    std::shared_ptr<Button> segment_button(ItemKey key) const;
    bool overflowed() const { return first_ != 0; }
    std::shared_ptr<const CommandSet> overflow_commands() const;
    Control* adjacent(const Control& current, int direction) const;
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
private:
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::breadcrumb; }
    StyleStateMask control_style_state_bits() const override;
    struct State {
        Breadcrumb* owner{};
        std::function<void(ItemKey)> navigate;
        std::function<void(const Button&)> button_invoked;
    };
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
    const std::shared_ptr<Label>& status() const { return status_; }
    const std::shared_ptr<Stack>& content() const { return content_; }
    void set_items(std::shared_ptr<const ItemsSource> source);
    void on_navigate(std::function<void(ItemKey)> callback);
    void on_query(std::function<void(NavigationQuery)> callback) { query_ = std::move(callback); }
    NavigationQuery request(std::wstring text);
    bool complete(const NavigationQuery& request, std::shared_ptr<const ItemsSource> source, std::wstring error = {});
    void cancel() override;
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
private:
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::navigation_pane; }
    StyleStateMask control_style_state_bits() const override;
    std::shared_ptr<ItemsView> items_;
    std::shared_ptr<Expander> group_;
    std::shared_ptr<Progress> progress_;
    std::shared_ptr<Label> status_;
    std::shared_ptr<Stack> content_;
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
    const std::shared_ptr<Stack>& content() const { return content_; }
    const std::shared_ptr<Label>& footer() const { return footer_; }
private:
    std::shared_ptr<Stack> content_;
    std::shared_ptr<Label> footer_;
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
    const std::shared_ptr<Stack>& content() const { return content_; }
private:
    std::shared_ptr<Stack> content_;
    std::shared_ptr<Popup> popup_;
    std::shared_ptr<RadioGroup> choices_;
    std::shared_ptr<RangeInput> size_;
};
}
