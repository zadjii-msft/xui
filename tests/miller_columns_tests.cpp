#include "xui/miller_columns.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace xui;
int checks{};
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F&& action) {
    bool threw{};
    try { action(); } catch (const std::exception&) { threw = true; }
    require(threw, "Invalid Miller columns parameters must fail explicitly");
}
class Rows final : public ItemsSource {
public:
    explicit Rows(std::size_t count = 100, std::uint64_t version = 1) : count_(count), version_(version) {}
    std::size_t size() const override { return count_; }
    ItemKey key(std::size_t index) const override { ++keys; return {index + 1, version_}; }
    std::optional<std::size_t> find(ItemKey value) const override {
        return value.version == version_ && value.id > 0 && value.id <= count_ ?
            std::optional<std::size_t>{static_cast<std::size_t>(value.id - 1)} : std::nullopt;
    }
    ItemContent item(std::size_t index) const override {
        ++reads;
        ItemContent result;
        result.primary = L"Item " + std::to_wstring(index + 1);
        result.enabled = index != 2;
        return result;
    }
    ItemHierarchy hierarchy(std::size_t index) const override {
        ItemHierarchy result;
        result.expandable = index % 2 == 0;
        return result;
    }
    mutable std::atomic<std::size_t> reads{}, keys{};
private:
    std::size_t count_;
    std::uint64_t version_;
};
std::vector<MillerColumn> fixture(const std::shared_ptr<const ItemsSource>& source) {
    return {{L"Root", source, ItemKey{1, 1}}, {L"Folder", source, ItemKey{2, 1}},
        {L"Child", source, std::nullopt}};
}
void layout_and_viewport() {
    MillerColumns view;
    const auto retained = view.retained_children().size();
    auto source = std::make_shared<Rows>();
    view.set_columns(fixture(source));
    view.arrange({20, 30, 500, 400});
    require(view.role() == ControlRole::content_view && retained == 2 + 2 * MillerColumns::maximum_columns,
        "A fixed retained composite hosts one virtual list and header per column");
    require(view.column_list(0)->bounds().width == 239 && view.column_width() == 240,
        "Column slots reserve a separator outside the list and scrollbar");
    const auto separator = view.separator_bounds(0);
    require(separator.x == 239 && separator.width == 1 && separator.y == 32 &&
        separator.y + separator.height == view.horizontal_track().y &&
        view.column_list(0)->bounds().x + view.column_list(0)->bounds().width == view.bounds().x + separator.x,
        "The separator joins header and list without covering rows, scrollbars or the toolbar");
    require(view.separator_bounds(2).width == 0 && view.separator_bounds(100).width == 0,
        "Only adjacent columns have a separator");
    const auto first = view.column_list(0), child = view.column_list(2);
    require(view.maximum_horizontal() == 220, "Horizontal range uses the combined column width");
    int focus_requests{};
    view.on_focus_column([&](const std::shared_ptr<VirtualCollection>&) { ++focus_requests; });
    view.set_active_column(2);
    require(view.horizontal_offset() == 220 && focus_requests == 0, "Silent active setter reveals without callbacks");
    require(child->bounds().x + child->bounds().width == 520, "Active column aligns with the viewport edge");
    view.set_horizontal_offset(0);
    require(view.horizontal_offset() == 0, "Explicit viewport movement does not require selection");
    view.arrange({20, 30, 60, 400});
    require(view.column_width() == 240 && child->bounds().width == 60 && view.horizontal_offset() == 120,
        "A narrow viewport fits and reveals one column without changing the preferred width");
    require(child->bounds().x == 20 && !first->visible(), "Hidden columns do not receive clipped pointer input");
    view.previous_button()->invoke();
    require(view.active_column() == 1 && focus_requests == 1, "Accessible Previous action moves focus to the preceding column");
    view.next_button()->invoke();
    require(view.active_column() == 2 && focus_requests == 2, "Accessible Next action moves focus to the following column");
    require(!view.next_button()->enabled(), "Next is disabled at the final column");
    view.set_column_width(180);
    view.arrange({0, 0, 720, 400});
    require(view.maximum_horizontal() == 0 && view.horizontal_offset() == 0, "Widening the viewport removes obsolete scroll offsets");
    for (float width : {0.0f, 1.0f, 32.0f, 100.0f, 600.0f})
        for (float height : {0.0f, 1.0f, 32.0f, 50.0f, 100.0f}) {
            view.arrange({0, 0, width, height});
            for (const auto& element : view.retained_children()) {
                const auto bounds = element->bounds();
                require(bounds.width >= 0 && bounds.height >= 0 && std::isfinite(bounds.x),
                    "Tiny layouts have finite, nonnegative sizes");
            }
        }
    view.set_visible(false); view.arrange({0, 0, 500, 400});
    require(view.measure({500, 400}).width == 0, "Hidden columns consume no measured width");
    for (const auto& element : view.retained_children())
        require(!std::static_pointer_cast<Control>(element)->visible(), "Hidden composite hides every native peer");
    view.set_visible(true); view.arrange({0, 0, 500, 400});
    require(view.column_list(view.active_column())->visible(), "Showing the composite restores the active viewport");
}
void horizontal_scrolling() {
    MillerColumns view;
    auto source = std::make_shared<Rows>();
    view.set_columns(fixture(source)); view.arrange({10, 20, 300, 400});
    auto first = view.column_list(0), last = view.column_list(2);
    first->set_offset(800); last->set_offset(1600);
    int callbacks{};
    view.on_selection([&](std::size_t, ItemKey) { ++callbacks; });
    view.on_activate([&](std::size_t, ItemKey) { ++callbacks; });
    view.on_focus_column([&](const std::shared_ptr<VirtualCollection>&) { ++callbacks; });
    view.set_active_column(2);
    view.set_horizontal_offset(123.5);
    view.arrange({30, 40, 300, 350});
    require(view.horizontal_offset() == 123.5 && view.active_column() == 2,
        "Layout, height and position changes preserve manual scrolling away from the active column");
    require(view.separator_bounds(0).x == 115.5f && view.separator_bounds(1).width == 0,
        "Separator bounds follow fractional scrolling and clip outside the viewport");
    require(first->offset() == 800 && last->offset() == 1600 && callbacks == 0,
        "Horizontal movement preserves vertical positions and emits no selection, focus or activation");
    auto track = view.horizontal_track(), thumb = view.horizontal_thumb();
    require(track.x == 0 && track.y == 338 && track.width == 300 && track.height == 12 &&
        first->bounds().y + first->bounds().height == view.bounds().y + track.y,
        "The local horizontal track has its own strip below the column lists");
    require(thumb.width == 125 && thumb.x > 0 && thumb.x + thumb.width < track.width,
        "The proportional thumb represents content extent and scroll position");
    view.scroll_horizontal(10000); thumb = view.horizontal_thumb();
    require(view.horizontal_offset() == view.maximum_horizontal() && thumb.x + thumb.width == track.width,
        "Positive scrolling clamps at the final column and aligns the thumb");
    view.scroll_horizontal(-10000);
    require(view.horizontal_offset() == 0 && view.horizontal_thumb().x == 0, "Negative scrolling clamps at the start");
    view.scroll_horizontal(0.25); view.arrange(view.bounds());
    require(view.horizontal_offset() == 0.25, "Fractional scrolling survives layout without snapping to the active column");
    rejects([&] { view.scroll_horizontal(std::numeric_limits<double>::quiet_NaN()); });
    rejects([&] { view.scroll_horizontal(std::numeric_limits<double>::infinity()); });
    view.arrange({0, 0, 1000, 350});
    require(view.maximum_horizontal() == 0 && view.horizontal_offset() == 0 && view.horizontal_track().width == 0 &&
        first->bounds().height == 286, "A fitting path hides the scrollbar and returns its space to the lists");
}
void selection_and_keyboard() {
    MillerColumns view;
    auto source = std::make_shared<Rows>();
    view.set_columns(fixture(source)); view.arrange({0, 0, 720, 400});
    int selections{}, activations{}, focus_requests{};
    std::size_t changed_column{};
    ItemKey changed_key{};
    view.on_selection([&](std::size_t column, ItemKey key) {
        ++selections; changed_column = column; changed_key = key;
    });
    view.on_activate([&](std::size_t column, ItemKey key) {
        ++activations; changed_column = column; changed_key = key;
    });
    view.on_focus_column([&](const std::shared_ptr<VirtualCollection>& column) {
        ++focus_requests; require(column == view.column_list(view.active_column()), "Focus requests identify the active column");
    });
    auto root = view.column_list(0), middle = view.column_list(1), leaf = view.column_list(2);
    require(!root->multiple_selection(), "Each column is single-select");
    view.set_active_column(2);
    root->select({5, 1});
    require(view.active_column() == 0 && view.columns().size() == 3 && changed_column == 0 && changed_key == ItemKey{5, 1},
        "Ancestor selection reports its own identity and preserves descendants until application replacement");
    leaf->select({8, 1});
    require(view.columns().size() == 3 && changed_column == 2 && changed_key == ItemKey{8, 1},
        "Leaf selection reports its column and delegates descendant replacement to the application");
    root->select({5, 1}, SelectionGesture::toggle);
    root->select({5, 1}, SelectionGesture::extend);
    root->select_all();
    require(selections == 2 && root->selection().storage_size() == 1, "Toggle, Shift and select-all cannot create a range or clear selection");
    root->select({6, 1}, SelectionGesture::focus_only);
    require(view.columns()[0].selected == ItemKey{5, 1} && selections == 2, "Focus-only movement preserves selection");
    root->horizontal(true, SelectionGesture::replace);
    require(view.active_column() == 1 && middle->selection().contains({2, 1}) && selections == 2 && focus_requests == 1,
        "Right enters an existing child without changing either selection");
    middle->horizontal(false, SelectionGesture::replace);
    require(view.active_column() == 0 && root->selection().contains({5, 1}) && focus_requests == 2, "Left returns without losing ancestor selection");
    root->horizontal(false, SelectionGesture::replace);
    leaf->horizontal(true, SelectionGesture::replace);
    require(focus_requests == 2 && view.columns().size() == 3, "Horizontal boundaries never load or manufacture columns");
    root->edge(false);
    root->step(1); root->step(1);
    require(view.columns()[0].selected == ItemKey{4, 1}, "Vertical arrows skip disabled rows");
    root->edge(true);
    require(view.columns()[0].selected == ItemKey{100, 1}, "End selects the final sibling");
    root->activate_item({100, 1});
    require(activations == 1 && changed_column == 0 && changed_key == ItemKey{100, 1},
        "Enter and double-click activation preserve the selected row identity");
    const int before = selections;
    require(!root->select({3, 1}) && !root->select({1, 2}) && !root->select({900, 1}),
        "Disabled, missing and stale identities cannot select");
    root->activate_item({3, 1}); root->activate_item({1, 2});
    require(activations == 1 && selections == before, "Invalid activation produces no events");
    require(root->remove_selection({100, 1}) && !view.columns()[0].selected && root->selection().empty(),
        "Accessibility removal updates both descriptor and row membership");
    view.set_enabled(false);
    root->horizontal(true, SelectionGesture::replace);
    root->activate_item({2, 1});
    require(!root->select({2, 1}) && activations == 1, "Disabled composites reject child input");
}
void replacement_and_virtualization() {
    MillerColumns view;
    auto source = std::make_shared<Rows>(1'000'000);
    auto columns = fixture(source);
    view.set_columns(columns); view.arrange({0, 0, 720, 464});
    auto first = view.column_list(0), second = view.column_list(1), removed = view.column_list(2);
    const auto first_snapshot = first->source();
    first->set_offset(800); second->set_offset(1600);
    columns[2].title = L"Renamed child";
    view.set_columns(columns);
    require(first == view.column_list(0) && first->source() == first_snapshot && first->offset() == 800 && second->offset() == 1600,
        "Unchanged snapshots preserve column peers, accessibility identities and independent vertical scroll");
    const auto rows = first->visible_content();
    require(rows.size() <= 12 && source->reads < 100 && source->keys < 150,
        "Million-row columns request only visible content and bounded identity lookups");
    require(rows.front().content.submenu && !first->source()->hierarchy(rows.front().index).expandable,
        "Branches use trailing indicators without advertising in-place tree expansion");
    require(first->source()->item(rows.front().index).primary == rows.front().content.primary,
        "Immutable accessibility snapshots use the same row content");
    auto replacement = std::make_shared<Rows>(100, 2);
    columns[0] = {L"Replaced", replacement, ItemKey{2, 2}};
    view.set_columns(columns);
    require(first->source() != first_snapshot && !first->select({2, 1}) && first->selection().contains({2, 2}),
        "Replacement invalidates stale versions without changing the retained list peer");
    view.set_active_column(2);
    columns.resize(1); view.set_columns(columns);
    require(view.active_column() == 0 && !removed->source() && removed->selection().empty() && !removed->select({1, 1}),
        "Removing descendants clears their snapshots and repairs the active column");
    require(first_snapshot->find({2, 1}).has_value() && !first_snapshot->find({2, 2}),
        "Retained old snapshots remain immutable");
    view.set_columns({});
    require(view.columns().empty() && view.active_column() == 0 && view.horizontal_offset() == 0,
        "Empty replacement resets viewport state");
    view.set_columns({{L"Loading", {}, {}}});
    require(!view.column_list(0)->source(), "An empty/loading column does not run an internal loader");
}
void context_menu_selection() {
    MillerColumns view;
    auto source = std::make_shared<Rows>();
    view.set_columns(fixture(source)); view.arrange({0, 0, 720, 400});
    int callbacks{};
    view.on_selection([&](std::size_t, ItemKey) { ++callbacks; });
    view.on_activate([&](std::size_t, ItemKey) { ++callbacks; });
    view.on_focus_column([&](const std::shared_ptr<VirtualCollection>&) { ++callbacks; });
    auto root = view.column_list(0);
    const auto snapshot = root->source();
    root->set_offset(80);
    view.set_active_column(2);
    require(root->prepare_context_menu(Point{20, 90}), "Pointer context resolves the scrolled row");
    require(view.active_column() == 0 && view.columns()[0].selected == ItemKey{5, 1} &&
        root->selection().contains({5, 1}) && callbacks == 0,
        "Pointer context selects and activates its column without invoking or drilling");
    require(view.columns().size() == 3 && root->source() == snapshot,
        "Context preparation preserves descendant columns and source identity");
    root->select({6, 1}, SelectionGesture::focus_only);
    require(root->prepare_context_menu() && view.columns()[0].selected == ItemKey{6, 1} && callbacks == 0,
        "Keyboard context selects the focused row silently");
    require(!root->prepare_context_menu(Point{-1, 10}) && !root->prepare_context_menu(Point{20, 0}) &&
        view.columns()[0].selected == ItemKey{6, 1},
        "Outside hits and disabled rows leave the current selection intact");
    rejects([&] { root->prepare_context_menu(Point{std::numeric_limits<float>::quiet_NaN(), 0}); });
    view.set_enabled(false);
    require(!root->prepare_context_menu(), "Disabled owners reject context preparation");
}
void programmatic_selection_keeps_external_focus() {
    MillerColumns view;
    auto source = std::make_shared<Rows>(1);
    view.set_columns({{L"Filtered folder", source, {}}});
    view.arrange({0, 0, 240, 400});
    int focus_requests{}, selections{};
    view.on_focus_column([&](const std::shared_ptr<VirtualCollection>&) { ++focus_requests; });
    view.on_selection([&](std::size_t column, ItemKey key) {
        ++selections;
        require(column == 0 && key == ItemKey{1, 1}, "Programmatic Down identifies the only filtered folder");
        auto columns = view.columns();
        columns.push_back({L"Loaded descendants", source, {}});
        view.set_columns(std::move(columns));
        view.set_active_column(1);
    });
    view.column_list(0)->step(1);
    require(selections == 1 && focus_requests == 0 && view.active_column() == 1,
        "Programmatic selection and reentrant child loading reveal columns without requesting native focus");
}
void pointer_hover() {
    MillerColumns view;
    auto source = std::make_shared<Rows>();
    view.set_columns(fixture(source)); view.arrange({0, 0, 720, 400});
    auto list = view.column_list(0);
    int callbacks{};
    view.on_selection([&](std::size_t, ItemKey) { ++callbacks; });
    view.on_activate([&](std::size_t, ItemKey) { ++callbacks; });
    view.on_focus_column([&](const std::shared_ptr<VirtualCollection>&) { ++callbacks; });
    list->hover_pointer(Point{20, 60});
    require(list->hovered_row() == 1 && list->selection().focused() == ItemKey{1, 1} &&
        list->selection().contains({1, 1}) && !list->focused() && view.active_column() == 0 && callbacks == 0,
        "Hover identifies a row without selection, focus, activation or a column change");
    list->hover_pointer(Point{20, 100});
    require(!list->hovered_row(), "Disabled rows reject hover");
    list->hover_pointer(Point{list->bounds().width - 2, 60});
    require(!list->hovered_row(), "Scrollbar space is outside the hover body");
    list->hover_pointer(Point{20, 60});
    list->set_offset(80);
    require(list->hovered_row() == 3, "Hover follows visible rows after vertical scrolling");
    list->cancel();
    require(!list->hovered_row() && callbacks == 0, "Input cancellation clears hover without an action");
    list->hover_pointer(Point{20, 60});
    view.set_enabled(false);
    require(!list->hovered_row(), "Disabled column owners suppress hover");
    view.set_enabled(true);
    view.set_columns(fixture(source));
    require(!list->hovered_row(), "Snapshot replacement clears pointer state");
    rejects([&] { list->hover_pointer(Point{std::numeric_limits<float>::quiet_NaN(), 10}); });
}
void validation_and_lifetime() {
    MillerColumns view;
    const auto dormant = view.column_list(MillerColumns::maximum_columns - 1);
    require(!dormant->source() && !dormant->visible() && !dormant->select({1, 1}),
        "Fixed column peers can be configured before population without accepting input");
    auto source = std::make_shared<Rows>();
    auto columns = fixture(source); view.set_columns(columns);
    int callbacks{};
    view.on_selection([&](std::size_t, ItemKey) { ++callbacks; });
    view.on_activate([&](std::size_t, ItemKey) { ++callbacks; });
    view.on_focus_column([&](const std::shared_ptr<VirtualCollection>&) { ++callbacks; });
    view.set_columns(columns); view.set_active_column(1); view.set_column_width(200);
    require(callbacks == 0, "Property setters are silent");
    rejects([&] { view.set_columns(std::vector<MillerColumn>(MillerColumns::maximum_columns + 1)); });
    rejects([&] { view.set_columns({{std::wstring(1025, L'x'), source, {}}}); });
    rejects([&] { view.set_columns({{L"Missing", source, ItemKey{999, 1}}}); });
    rejects([&] { view.set_columns({{L"Stale", source, ItemKey{1, 2}}}); });
    rejects([&] { view.set_columns({{L"Disabled", source, ItemKey{3, 1}}}); });
    rejects([&] { view.set_columns({{L"Empty", {}, ItemKey{1, 1}}}); });
    require(view.columns().size() == 3 && view.active_column() == 1 && callbacks == 0, "Invalid replacements preserve the prior model");
    rejects([&] { view.set_active_column(3); });
    rejects([&] { view.column_list(MillerColumns::maximum_columns); });
    for (float width : {0.0f, 119.0f, 2001.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        rejects([&] { view.set_column_width(width); });
    view.set_column_width(120);
    require(view.column_width() == 120, "Minimum column width matches the binding contract");
    view.set_column_width(2000);
    require(view.column_width() == 2000 && callbacks == 0, "Maximum column width is accepted silently");
    for (double offset : {-1.0, 1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { view.set_horizontal_offset(offset); });
    rejects([&] { view.column_list(0)->set_presentation(ItemsPresentation::tiles); });
    view.set_columns({});
    require(view.column_list(MillerColumns::maximum_columns - 1) == dormant,
        "Clearing the path retains dormant child identities for prewired application callbacks");
    rejects([&] { view.set_active_column(0); });
    auto owner = std::make_unique<MillerColumns>(); owner->set_columns(columns);
    auto list = owner->column_list(0);
    owner->on_selection([&](std::size_t, ItemKey) { owner.reset(); });
    require(list->select({2, 1}) && !owner && !list->owner(), "Selection callbacks can release the owning composite");
    require(!list->select({4, 1}), "Externally retained lists detach from a destroyed owner");
    list->horizontal(true, SelectionGesture::replace); list->activate_item({4, 1});
    owner = std::make_unique<MillerColumns>(); owner->set_columns(columns);
    list = owner->column_list(0);
    int activations{};
    owner->on_activate([&](std::size_t, ItemKey) { ++activations; });
    owner->on_selection([&](std::size_t, ItemKey) {
        owner->set_columns({{L"New namespace", std::make_shared<Rows>(100, 2), {}}});
    });
    list->activate_item({2, 1});
    require(activations == 0, "Reentrant source replacement revokes activation of an obsolete row");
    owner->set_columns(columns);
    owner->on_selection([&](std::size_t, ItemKey) { owner.reset(); });
    list->activate_item({2, 1});
    require(!owner && activations == 0, "Owner destruction during selection also revokes pending activation");
}
}
int main() {
    try {
        layout_and_viewport(); horizontal_scrolling(); selection_and_keyboard(); replacement_and_virtualization();
        context_menu_selection(); programmatic_selection_keeps_external_focus(); pointer_hover(); validation_and_lifetime();
        std::cout << checks << " Miller columns checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
