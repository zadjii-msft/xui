#pragma once
#include "xui/commands.hpp"
#include "xui/adaptive_layout.hpp"

namespace xui {
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
