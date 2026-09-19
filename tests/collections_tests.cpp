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
void gallery_contracts() {
    static_assert(static_cast<int>(ItemsPresentation::list) == 0 && static_cast<int>(ItemsPresentation::tiles) == 1 &&
        static_cast<int>(ItemsPresentation::grouped) == 2 && static_cast<int>(ItemsPresentation::gallery) == 3);
    auto source = std::make_shared<Items>();
    ItemsView view; view.set_items(source); view.set_presentation(ItemsPresentation::gallery);
    view.set_item_size({96, 128}); view.arrange({0, 0, 312, 270});
    require(view.columns() == 3 && view.wraps_items(), "Gallery wraps through shared tile geometry");
    const auto rows = view.visible_content();
    require(rows.size() <= 12 && source->reads <= 12, "Million-item gallery materializes only visible rows");
    for (const auto& row : rows) {
        if (row.bounds.y >= view.content_viewport().height) continue;
        require(view.hit_test({row.bounds.x + 1, row.bounds.y + 1}) == row.index,
            "Gallery hit testing matches painted tile bounds");
    }
    for (const auto size : {Size{96, 128}, Size{160, 192}, Size{256, 288}}) {
        view.set_item_size(size); view.arrange({0, 0, size.width * 3 + 12, size.height * 2});
        auto row = view.visible_content().front(); row.content.secondary.clear(); row.content.action.clear();
        const auto layout = view.gallery_layout(row);
        require(layout.image.width == layout.image.height && layout.image.width > size.width / 2 &&
            layout.image.width <= size.width - 16, "Gallery image extent follows the requested DIP size, not the viewport");
        require(layout.image.y + layout.image.height < layout.primary.y &&
            layout.image.x + layout.image.width / 2 == row.bounds.x + row.bounds.width / 2 &&
            layout.primary.x + layout.primary.width / 2 == row.bounds.x + row.bounds.width / 2,
            "Gallery centers the image above the filename");
    }
    view.select({2, 1}); view.step(static_cast<int>(view.columns()));
    require(view.selection().focused() == ItemKey{5, 1}, "Gallery vertical navigation advances one tile row");
    view.horizontal(true, SelectionGesture::replace);
    require(view.selection().focused() == ItemKey{6, 1}, "Gallery horizontal navigation advances one item");
    view.select_rectangle({2, 1}, {6, 1});
    require(view.selection().contains({2, 1}) && view.selection().contains({6, 1}) && !view.selection().contains({4, 1}),
        "Gallery rectangle selection uses wrapped columns");
    view.select({1000000, 1});
    require(view.visible_items().end == source->size(), "Gallery reveals the last item without enumeration");
    view.set_presentation(ItemsPresentation::list);
    require(view.columns() == 1 && view.selection().contains({1000000, 1}), "Gallery-to-list retains stable selection");
    bool rejected{};
    try { TreeView tree; tree.set_presentation(ItemsPresentation::gallery); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Gallery is opt-in for ItemsView only");
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
void tree_error_retry_contract() {
    TreeView tree; tree.set_tree(std::make_shared<Tree>()); tree.arrange({0, 0, 400, 240});
    std::vector<TreeRequest> requests; tree.on_request([&](TreeRequest request) { requests.push_back(request); });
    const auto empty = std::make_shared<Items>(0);
    tree.disclose({1, 1}, true);
    const auto failed = requests.back();
    require(tree.complete(failed, empty, L"Folder disappeared") && tree.visible_content().front().error,
        "Failed child delivery can include the nonnull empty source required by bindings");
    tree.select({1, 1}); tree.horizontal(true, SelectionGesture::replace);
    require(requests.size() == 2 && requests.back().generation != failed.generation &&
        tree.visible_content().front().pending && !tree.visible_content().front().error,
        "Right arrow retries an open failed branch instead of reusing its empty source");
    require(!tree.complete(failed, empty), "An old error completion cannot replace the retry");
    require(tree.complete(requests.back(), empty, L"Still unavailable"), "Retry can report another error");
    tree.disclose({1, 1}, false); tree.disclose({1, 1}, true);
    require(requests.size() == 3 && tree.visible_content().front().pending,
        "Reopening a failed branch also retries instead of using cached children");
    require(tree.complete(requests.back(), empty), "An empty successful child source is valid");
    tree.horizontal(true, SelectionGesture::replace);
    tree.disclose({1, 1}, false); tree.disclose({1, 1}, true);
    require(requests.size() == 3 && !tree.visible_content().front().error,
        "Successful empty child sources stay cached across expansion gestures");
}
void tree_details_contract() {
    auto source = std::make_shared<DetailTree>(); source->top = std::make_shared<DetailItems>(1000000);
    TreeView tree; tree.set_tree(source); compact_tree(tree); tree.arrange({0, 0, 912, 240});
    auto rows = tree.visible_content();
    require(rows.size() <= 11 && source->top->reads <= 11 && source->top->cell_reads <= 33,
        "Million-row details read only visible metadata cells");
    const auto folder = tree.details_layout(rows[0]), file = tree.details_layout(rows[1]);
    require(folder.columns[0].width == 515 && folder.columns[1].x == 515 &&
        folder.columns[2].x == 675 && folder.columns[3].x == 800,
        "Extra viewport width belongs to Name, with fixed metadata columns");
    require(folder.icon.width == 16 && folder.icon.height == 16 && folder.icon.y == 4 &&
        folder.disclosure.width == 16 && file.disclosure.width == 0 && file.icon.x == 4,
        "Compact tree icons are centered at 16 DIPs and files reserve no disclosure slot");
    require(tree.disclosure_hit(0, {8, 12}) && !tree.disclosure_hit(1, {8, 36}),
        "Shared compact disclosure geometry distinguishes folders and files");
    std::optional<TreeRequest> pending;
    tree.on_request([&](TreeRequest request) { pending = request; });
    tree.disclose({1, 1}, true);
    const auto children = std::make_shared<DetailItems>(1000000, 2000001);
    require(tree.complete(*pending, children), "Details accepts a virtual lazy child source");
    rows = tree.visible_content();
    require(rows[1].depth == 1 && rows[1].cells[0] == L"C1:2000001" &&
        rows[2].cells[2] == L"C3:2000002" && children->cell_reads <= 30,
        "Projection delegates metadata reads to the correct immutable child source");
    const auto child_file = tree.details_layout(rows[2]);
    require(child_file.icon.x == file.icon.x + 16 && child_file.columns[1].x == folder.columns[1].x,
        "Only the Name column receives hierarchy indentation");
    tree.select({2000002, 1});
    const auto columns = tree.detail_columns();
    bool rejected{};
    try { tree.set_columns({{L"Bad", 20}}); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && tree.detail_columns() == columns && tree.selection().contains({2000002, 1}),
        "Invalid detail columns preserve configuration and selection");
    tree.arrange({0, 0, 120, 240}); rows = tree.visible_content();
    for (const auto& row : rows) {
        const auto layout = tree.details_layout(row);
        for (const auto& cell : layout.columns)
            require(cell.width >= 0 && cell.x + cell.width <= tree.content_viewport().width,
                "Narrow metadata columns clip without negative widths or out-of-row bounds");
        require(layout.name.width >= 0 && layout.name.x + layout.name.width <= tree.content_viewport().width,
            "Deep names stay within the first column");
    }
    const auto projected = tree.source();
    const auto reads = children->cell_reads;
    tree.set_columns({}); rows = tree.visible_content();
    require(tree.source() == projected && tree.selection().contains({2000002, 1}) && rows[1].cells.empty() &&
        children->cell_reads == reads && rows[1].content.secondary == L"Ordinary subtitle",
        "Clearing columns restores ordinary rows without replacing the source or selection");
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
    try { allocation_contract(); selection_contracts(); items_contracts(); gallery_contracts(); trees(); tree_collapse_notifications(); tree_error_retry_contract(); tree_details_contract(); grids(); layouts(); std::cout << "Collection selection, million-row virtualization, lazy trees, filters, and adaptive layout passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
