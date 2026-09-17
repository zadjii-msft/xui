#include "collections_fixture.hpp"
#include "xui/adaptive_layout.hpp"
#include "xui/foundation.hpp"
#include <iostream>
#include <cstdlib>
#include <new>

namespace allocation_probe {
thread_local bool active{};
thread_local std::size_t bytes{}, calls{};
}
void* operator new(std::size_t size) {
    if (auto* value = std::malloc(size ? size : 1)) {
        if (allocation_probe::active) { allocation_probe::bytes += size; ++allocation_probe::calls; }
        return value;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace xui;
using namespace collections_test;
void allocation_contract() {
    const auto measure = [](std::size_t count) {
        struct Scope {
            Scope() { allocation_probe::bytes = allocation_probe::calls = 0; allocation_probe::active = true; }
            ~Scope() { allocation_probe::active = false; }
        } scope;
        auto source = std::make_shared<Items>(count); ItemsView view;
        view.set_items(source, source); view.arrange({0, 0, 600, 280});
        auto rows = view.visible_content(); view.select_all();
        return std::pair{allocation_probe::bytes, allocation_probe::calls};
    };
    const auto small = measure(100000), large = measure(1000000);
    require(large.first < 65536 && large.first <= small.first + 1024 && large.second <= small.second + 4,
        "Actual C++ allocation cost is independent of source count");
    std::cout << "100k/million visible+select-all allocations: " << small.first << "/" << large.first <<
        " bytes, " << small.second << "/" << large.second << " calls\n";
}
void selection_contracts() {
    auto all = std::make_shared<Items>(), even = std::make_shared<Items>(500000, 2, 2);
    CollectionSelection selection;
    selection.select(all, {10, 1}, SelectionGesture::replace);
    selection.select(all, {1000000, 1}, SelectionGesture::extend);
    require(selection.storage_size() == 1 && selection.contains({600000, 1}) && !selection.contains({9, 1}), "Million-row range uses one term");
    selection.select_all(even, SelectAllScope::filtered, all);
    require(selection.contains({1000000, 1}) && !selection.contains({999999, 1}), "Filtered all uses the immutable filtered domain");
    require(selection.state(even) == SelectionState::all && !selection.selected_keys(even), "Huge selection has bounded automation enumeration");
    selection.select_all(even, SelectAllScope::full_source, all);
    require(selection.contains({999999, 1}) && selection.storage_size() == 1, "Full all includes hidden IDs without allocation");
    selection.set({20, 1}, false);
    require(selection.state(all) == SelectionState::mixed && !selection.contains({20, 1}) && selection.storage_size() == 2, "All-minus-exception is bounded and mixed");
    for (int i = 0; i < 100; ++i) selection.set({20, 1}, i % 2);
    require(selection.storage_size() == 2, "Repeated checkbox input replaces trailing exception");
    selection.rectangle(all, 13, 37, 10);
    require(selection.storage_size() == 1 && selection.contains({14, 1}) && selection.contains({38, 1}) &&
        !selection.contains({20, 1}) && !selection.contains({23, 1}), "Rectangular selection stores row and column intervals");
    require(!selection.contains({14, 2}), "Versions do not alias recycled identities");
    selection.clear(); selection.select(all, {10, 1}, SelectionGesture::replace);
    selection.select(all, {20, 1}, SelectionGesture::focus_only);
    require(selection.focused() == ItemKey{20, 1} && selection.contains({10, 1}) && !selection.contains({20, 1}), "Focus is separate from selection");
    const auto keys = selection.selected_keys(all); require(keys && keys->size() == 1 && keys->front().id == 10, "Small selection enumerates without scanning million rows");
    require(all->reads == 0 && all->keys < 100 && even->keys == 0, "Selection never requests visual content or scans source");
    selection.clear();
    for (std::size_t i = 0; i < CollectionSelection::maximum_terms; ++i) selection.set({i + 1, 1}, true);
    bool limited{}; try { selection.set({999999, 1}, true); } catch (const std::length_error&) { limited = true; }
    require(limited && selection.storage_size() == CollectionSelection::maximum_terms, "Selection has an explicit atomic limit");
    selection.select_all(all, SelectAllScope::filtered); require(selection.storage_size() == 1, "Replacement gesture releases prior terms");
}
void items_contracts() {
    auto source = std::make_shared<Items>(); ItemsView items; int changes{}, actions{};
    items.on_selection([&] { ++changes; }); items.on_action([&](ItemKey) { ++actions; });
    items.set_items(source, source); items.arrange({0, 0, 620, 280});
    require(!changes && source->reads == 0, "Source and layout setters do not fetch rows or fire selection callbacks");
    auto rows = items.visible_content();
    require(rows.size() <= 6 && source->reads <= 6 && rows[0].content.action == L"Act", "Only visible rich content materializes");
    items.select({1000000, 1}); require(items.visible_items().end == 1000000, "Last item is revealed using bounded geometry");
    items.set_presentation(ItemsPresentation::tiles);
    require(items.columns() == 3 && items.selection().contains({1000000, 1}), "List-to-tiles preserves selection");
    items.arrange({0, 0, 390, 280}); require(items.columns() == 2 && items.selection().focused() == ItemKey{1000000, 1}, "Wrap transition preserves focused identity");
    items.set_items(std::make_shared<Items>(500000, 2, 2), source);
    require(items.selection().contains({1000000, 1}), "Source replacement preserves selected keys");
    items.select({2, 1}); items.set_items(source, source); items.set_presentation(ItemsPresentation::grouped);
    require(items.source()->size() == 1000002, "Group headings add only span records");
    require(items.disclose({9000000001, 1}, false) && items.source()->size() == 500002, "Group collapse removes a span, not per-row state");
    require(items.selection().contains({2, 1}) && items.selection().focused() == ItemKey{2, 1}, "Collapsed group retains selected and focused IDs");
    items.select_all();
    require(items.selection().contains({2, 1}) && !items.selection().contains({9000000001, 1}), "Grouped select-all includes collapsed data, not headers");
    items.disclose({9000000001, 1}, true); items.reveal({2, 1}); items.activate_item({2, 1}, true);
    require(actions == 1 && source->reads < 40, "Inline action is independent and bounded");
    items.set_enabled(false); require(!items.select({3, 1}), "Disabled collection rejects input");
    items.activate_item({2, 1}, true); require(actions == 1, "Disabled inline action cannot invoke");
}
void trees() {
    TreeView tree; tree.set_tree(std::make_shared<Tree>()); tree.arrange({0, 0, 600, 240});
    std::vector<TreeRequest> requests; tree.on_request([&](TreeRequest request) { requests.push_back(request); });
    tree.disclose({1, 1}, true); require(requests.size() == 1 && tree.visible_content()[0].pending, "Lazy expansion exposes pending state");
    auto pending = requests.back(); tree.disclose({1, 1}, false);
    require(pending.cancellation.stop_requested() && !tree.complete(pending, std::make_shared<Items>()), "Collapse cancels and rejects a stale result");
    tree.disclose({1, 1}, true); auto children = std::make_shared<Items>(1000000, 1000001);
    require(tree.complete(requests.back(), children) && tree.source()->size() == 1000004, "Million-child tree uses virtual sibling spans");
    tree.select({1, 1}); tree.horizontal(true, SelectionGesture::replace);
    require(tree.selection().focused() == ItemKey{1000001, 1}, "Right arrow enters first child");
    tree.disclose({1000001, 1}, true);
    auto grandchildren = std::make_shared<Items>(100000, 3000001);
    tree.complete(requests.back(), grandchildren); tree.select({3000001, 1});
    tree.disclose({1, 1}, false);
    require(tree.selection().contains({3000001, 1}) && tree.selection().focused() == ItemKey{1, 1}, "Collapse retains hidden selection and repairs focus to ancestor");
    tree.disclose({1, 1}, true);
    require(tree.source()->find({3000001, 1}).has_value() && tree.retained_branches() == 2, "Expanded subtree identity survives parent collapse");
    const auto before = children->keys.load(); tree.set_offset(tree.maximum_offset()); tree.visible_content();
    require(children->keys - before < 30 && children->reads < 30, "Tree scrolling skips unmaterialized siblings");
    tree.disclose({2, 1}, true); pending = requests.back();
    tree.complete(pending, {}, L"Offline");
    tree.disclose({2, 1}, true); require(requests.back().generation != pending.generation, "Error permits a new child request");
    pending = requests.back(); tree.cancel();
    require(pending.cancellation.stop_requested() && !tree.complete(pending, children), "Owner cancellation rejects delivery");
    tree.disclose({3, 1}, true); pending = requests.back(); tree.set_tree(std::make_shared<Tree>());
    require(pending.cancellation.stop_requested() && !tree.complete(pending, children), "Source replacement invalidates child requests");
    std::optional<TreeRequest> late;
    { TreeView owner; owner.set_tree(std::make_shared<Tree>()); owner.on_request([&](TreeRequest r) { late = r; }); owner.disclose({1, 1}, true); }
    require(late->cancellation.stop_requested(), "Tree destruction cancels external work");
}
void tree_collapse_notifications() {
    auto tree = std::make_unique<TreeView>();
    tree->set_tree(std::make_shared<Tree>());
    tree->on_request([&](TreeRequest request) {
        tree->complete(request, std::make_shared<Items>(2, 1000001));
    });
    tree->disclose({1, 1}, true);
    tree->select({1000001, 1});
    unsigned changes{};
    tree->on_selection([&] {
        ++changes;
        require(tree->selection().focused() == ItemKey{1, 1} && !tree->expanded({1, 1}) &&
            !tree->source()->find({1000001, 1}), "Collapse callback sees repaired focus and completed projection");
    });
    tree->disclose({1, 1}, false);
    require(changes == 1 && tree->selection().contains({1000001, 1}),
        "Collapse reports one focus repair without changing hidden selection");
    tree->disclose({1, 1}, false);
    tree->disclose({1, 1}, true);
    tree->disclose({1, 1}, false);
    require(changes == 1, "Collapse and expansion without a focus change emit no selection event");
    tree->on_selection({});
    tree->disclose({1, 1}, true);
    tree->select({2, 1});
    tree->on_selection([&] { ++changes; });
    tree->disclose({1, 1}, false);
    require(changes == 1 && tree->selection().focused() == ItemKey{2, 1}, "Unrelated focus stays silent");
    tree->on_selection({});
    tree->disclose({1, 1}, true);
    tree->select({1000001, 1});
    tree->on_selection([&] { ++changes; tree->disclose({1, 1}, true); });
    tree->disclose({1, 1}, false);
    require(changes == 2 && tree->expanded({1, 1}), "Callback can reexpand without obsolete collapse work afterward");
    tree->on_selection({});
    tree->select({1000001, 1});
    tree->on_selection([&] { ++changes; tree.reset(); });
    tree->disclose({1, 1}, false);
    require(changes == 3 && !tree, "Collapse callback can delete its owner");
}
void grids() {
    DataGrid grid; grid.set_columns({{L"Name", 240, false, true, true}, {L"Value", 120, true, true}});
    auto all = std::make_shared<Rows>(); grid.set_source(all); grid.set_full_source(all); grid.arrange({0, 0, 500, 220});
    int changes{}, sorts{}; grid.on_select([&] { ++changes; }); grid.on_sort([&](auto, auto) { ++sorts; });
    grid.select({10, 1}); grid.select({20, 1}, SelectionGesture::extend);
    require(grid.selection().contains({15, 1}) && grid.selection().storage_size() == 1, "Grid shares compact range selection");
    grid.select_all(); require(grid.check_state() == SelectionState::all && grid.selection().storage_size() == 1, "Grid header check selects virtual source");
    grid.toggle_check(RowKey{10, 1}); require(grid.check_state() == SelectionState::mixed && !grid.selection().contains({10, 1}), "Grid mixed header reflects exception");
    const auto count = changes; grid.set_checked({10, 1}, true); require(changes == count, "Checked property is silent");
    grid.set_source(std::make_shared<Rows>(50000, false, 2));
    require(grid.check_state() == SelectionState::all, "Explicit full-source selection remains all in a filtered view");
    grid.set_source(all);
    grid.set_filter(0, L"even"); grid.reorder_column(0, 1); grid.set_column_width(1, 300);
    require(grid.source_column(1) == 0 && grid.filters()[0] == L"even" && grid.columns()[1].checkable, "Reorder retains filter and check logical identities");
    grid.sort(0); require(sorts == 1 && grid.sort_column() == 0, "Sort remains separate from check/filter");
    std::vector<GridFilterRequest> requests; grid.on_filter([&](GridFilterRequest request) { requests.push_back(request); });
    grid.filter(0, L"first"); const auto stale = requests.back(); grid.filter(0, L"second");
    auto even = std::make_shared<Rows>(50000, false, 2);
    require(stale.cancellation.stop_requested() && !grid.complete_filter(stale, even), "New filter rejects stale query");
    require(grid.complete_filter(requests.back(), even) && grid.source()->size() == 50000, "Current filter completes on the retained control");
    grid.filter(0, L"third"); const auto replacing = requests.back(); grid.set_source(all);
    require(!grid.complete_filter(replacing, even), "Direct source replacement invalidates pending query");
    grid.filter(0, L"fourth"); const auto closing = requests.back(); grid.cancel();
    require(!grid.complete_filter(closing, even) && closing.cancellation.stop_requested(), "Grid cancellation ends external work");
    grid.set_enabled(false); const auto before = grid.selection(); grid.toggle_check();
    require(before == grid.selection(), "Disabled header cannot change selection");
}
void layouts() {
    auto label = std::make_shared<Label>(L"Auto label"); label->set_fixed_size({80, 32});
    auto input = std::make_shared<TextInput>(L"Input"); input->set_minimum_size({20, 20}); input->set_maximum_size({300, 60});
    Grid grid; grid.set_tracks({{TrackSizing::automatic}, {TrackSizing::star}},
        {{TrackSizing::fixed, 100}, {TrackSizing::star, 1, 20, 300}});
    grid.set_gap(8, 6); grid.set_padding({10, 10, 10, 10}); grid.add(label, 0, 0); grid.add(input, 1, 0, 1, 2);
    grid.arrange({0, 0, 500, 220});
    require(label->bounds().x == 10 && label->bounds().width == 80 && input->bounds().y >= 48 && input->bounds().width <= 300, "Grid respects track sizing, padding, auto and child maximum");
    Wrap wrap; wrap.set_item_width(100);
    std::vector<std::shared_ptr<Button>> buttons;
    for (int i = 0; i < 6; ++i) { auto b = std::make_shared<Button>(L"Action"); buttons.push_back(b); wrap.add(b); }
    wrap.arrange({0, 0, 360, 240}); require(wrap.columns() == 3 && buttons[3]->bounds().y > buttons[0]->bounds().y, "Wrap forms measured rows");
    buttons[2]->set_focused(true); const auto id = buttons[2]->id();
    wrap.arrange({0, 0, 220, 360}); require(wrap.columns() == 2 && buttons[2]->focused() && buttons[2]->id() == id, "Wrap keeps focus and instances");
    auto nav = std::make_shared<ItemsView>(); nav->set_items(std::make_shared<Items>(10)); nav->select({3, 1});
    auto detail = std::make_shared<Button>(L"Details"); detail->set_focused(true);
    AdaptiveLayout adaptive(nav, detail); adaptive.arrange({0, 0, 900, 300});
    require(!adaptive.compact() && detail->bounds().x > nav->bounds().x, "Wide navigation is inline");
    adaptive.arrange({0, 0, 400, 300});
    require(adaptive.compact() && detail->bounds().y > nav->bounds().y && detail->focused() && nav->selection().contains({3, 1}), "Compact recipe preserves selection and focus");
    adaptive.set_compact_navigation(CompactNavigation::overlay); adaptive.arrange({0, 0, 400, 300});
    require(adaptive.overlay_active() && nav->bounds().x == detail->bounds().x && detail->focused() && nav->selection().contains({3, 1}), "Overlay reuses retained navigation and selection");
    adaptive.set_navigation_open(false); adaptive.arrange({0, 0, 400, 300});
    require(!adaptive.overlay_active() && nav->bounds().width == 0 && detail->bounds().width == 400, "Compact navigation closes without removing its model");
}
}
int main() {
    try { allocation_contract(); selection_contracts(); items_contracts(); trees(); tree_collapse_notifications(); grids(); layouts(); std::cout << "Collection selection, million-row virtualization, lazy trees, filters, and adaptive layout passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
