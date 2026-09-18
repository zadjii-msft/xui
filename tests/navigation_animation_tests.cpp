#include "xui/navigation.hpp"
#include "../src/collection_presentation.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace xui;
using detail::CollectionPresentationAccess;
using namespace std::chrono_literals;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& f) {
    bool rejected{};
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Reject invalid navigation input");
}
std::vector<NavigationItem> entries() {
    std::vector<NavigationItem> result;
    NavigationItem group; group.key = {10, 3}; group.label = L"Group"; group.selectable = false;
    result.push_back(group);
    for (std::uint64_t id = 11; id <= 14; ++id) {
        NavigationItem child; child.key = {id, 3}; child.parent = group.key;
        child.label = L"Child " + std::to_wstring(id); result.push_back(child);
    }
    NavigationItem tail; tail.key = {20, 3}; tail.label = L"Tail"; result.push_back(tail);
    return result;
}
double tail_y(const NavigationView& view) {
    return view.items()->item_bounds(*view.items()->source()->find({20, 3})).y;
}
void sampled_contract() {
    NavigationView view;
    view.set_items(entries()); view.arrange({0, 0, 280, 440});
    require(view.duration() == 0 && !view.animating(), "Initial population and default duration remain immediate");
    view.set_item_expanded({10, 3}, false);
    require(!view.animating() && view.items()->source()->size() == 2, "Default collapse immediately changes logical rows");
    view.set_duration(1000);
    unsigned selected{}, filtered{}, pane{};
    view.on_select([&](auto) { ++selected; });
    view.on_filter([&](const auto&) { ++filtered; });
    view.on_expanded([&](auto) { ++pane; });
    const auto list = view.items();
    view.set_item_expanded({10, 3}, true);
    const auto first = CollectionPresentationAccess::get(*list);
    const auto opening_source = list->source();
    require(view.animating() && view.item_expanded({10, 3}) && opening_source->find({11, 3}) &&
        first && first->clip(1).height == 0 && first->outgoing().empty(), "Opening publishes logical children before their clip grows");
    const auto start = Animation::Clock::now();
    list->advance(start + 250ms);
    const auto middle = CollectionPresentationAccess::get(*list);
    require(list->source() == opening_source && middle->version() > first->version() &&
        tail_y(view) > 40 && tail_y(view) < 200 && first->bounds(5).y == 40,
        "Frames reuse the immutable logical source and preserve earlier presentation snapshots");
    const auto y = tail_y(view);
    view.set_item_expanded({10, 3}, false);
    const auto closing_source = list->source();
    require(view.animating() && !view.item_expanded({10, 3}) && !closing_source->find({11, 3}) &&
        std::abs(tail_y(view) - y) < 0.001, "Closing reverses from the displayed rectangles with immediate logical removal");
    require(!list->hit_test({20, 50}) && !CollectionPresentationAccess::get(*list)->outgoing().empty(),
        "Outgoing children remain draw-only");
    const auto closing_frame = CollectionPresentationAccess::get(*list);
    view.set_item_expanded({10, 3}, false); view.set_duration(1000);
    require(CollectionPresentationAccess::get(*list) == closing_frame && list->source() == closing_source,
        "Equivalent targets and durations preserve active motion");
    rejects([&] { view.set_duration(10001); });
    auto invalid = entries(); invalid[1].parent = invalid[1].key;
    rejects([&] { view.set_items(invalid); });
    require(view.duration() == 1000 && CollectionPresentationAccess::get(*list) == closing_frame,
        "Invalid mutation preserves active state");
    list->advance(Animation::Clock::now() + 100ms);
    const auto reversing = tail_y(view);
    view.set_item_expanded({10, 3}, true);
    require(std::abs(tail_y(view) - reversing) < 0.001 && list->source()->find({14, 3}),
        "Reopening restores logical membership without a presentation jump");
    list->advance(Animation::Clock::now() + 1100ms);
    require(!view.animating() && !CollectionPresentationAccess::get(*list) && tail_y(view) == 200,
        "Completion releases frame storage and restores natural geometry");
    require(selected == 0 && filtered == 0 && pane == 0, "Animation emits no selection, filter, or pane callbacks");
    view.select({12, 3});
    view.set_item_expanded({10, 3}, false);
    require(view.selected() == ItemKey{12, 3} && list->selection().focused() == ItemKey{10, 3},
        "Collapse keeps logical selection and repairs row focus to its surviving ancestor");
    view.set_duration(0);
    require(!view.animating() && tail_y(view) == 40, "Duration changes settle immediately");
}
void settlement_contract() {
    NavigationView view; view.set_items(entries()); view.arrange({0, 0, 280, 440}); view.set_duration(10000);
    const auto list = view.items();
    const auto collapse = [&] {
        view.set_duration(0); view.set_filter(L""); view.set_items(entries());
        view.set_item_expanded({10, 3}, true); view.arrange({0, 0, 280, 440}); view.set_duration(10000);
        const auto start = Animation::Clock::now();
        view.set_item_expanded({10, 3}, false);
        require(view.animating(), "Start an arranged main-section transition");
        return start;
    };
    collapse(); list->set_offset(list->offset()); require(!view.animating(), "Direct scrolling settles motion");
    collapse(); list->reveal({20, 3}); require(!view.animating(), "Explicit focus reveal settles motion");
    collapse(); view.set_filter(L"Child"); require(!view.animating(), "Search source changes settle motion");
    collapse(); view.set_items(entries()); require(!view.animating(), "Data replacement settles motion");
    collapse(); view.arrange({0, 0, 300, 440}); require(!view.animating(), "Resize settles motion");
    collapse(); list->cancel(); require(!view.animating(), "Retirement and cancellation stop the producer");
    collapse(); list->set_item_size({240, 48}); require(!view.animating(), "Row metric changes settle motion");
    list->set_item_size({240, 40});
    collapse(); view.set_header_visible(false); require(!view.animating(), "Header geometry changes settle motion");
    view.set_header_visible(true);
    collapse(); view.set_search_visible(false); require(!view.animating(), "Search geometry changes settle motion");
    view.set_search_visible(true);
    collapse(); view.set_expanded(false); require(!view.animating(), "Compact pane changes settle motion");
    view.set_expanded(true);
    collapse(); list->settle(); require(!view.animating() && tail_y(view) == 40, "Shared reduced-motion settlement reaches the logical target");
    const auto start = collapse();
    list->advance(start + 9999ms);
    require(view.animating(), "Rounded easing endpoints do not retire the clock early");
    list->advance(Animation::Clock::now() + 10001ms);
    require(!view.animating() && !CollectionPresentationAccess::get(*list), "Terminal clock delivery retires an unchanged rounded endpoint");
    auto data = entries();
    for (auto& item : data) item.section = NavigationSection::footer;
    view.set_items(data); view.arrange({0, 0, 280, 440});
    view.set_item_expanded({10, 3}, true); view.set_item_expanded({10, 3}, false);
    require(!view.animating(), "Header/footer disclosure uses immediate section layout, not an unsupported transition");
}
void interruption_contract() {
    NavigationView view;
    view.set_duration(1000);
    auto data = entries();
    NavigationItem group; group.key = {30, 3}; group.label = L"Second group"; group.selectable = false;
    data.push_back(group);
    NavigationItem child; child.key = {31, 3}; child.parent = group.key; child.label = L"Second child";
    data.push_back(child);
    view.set_items(data); view.arrange({0, 0, 280, 600});
    require(!view.animating(), "Opt-in duration does not animate initial population");
    const auto list = view.items();
    view.set_item_expanded({10, 3}, false);
    list->advance(Animation::Clock::now() + 100ms);
    require(tail_y(view) > 40, "The first branch has an intermediate exit");
    view.set_item_expanded(group.key, false);
    const auto frame = CollectionPresentationAccess::get(*list);
    require(view.animating() && tail_y(view) == 40 && frame && frame->outgoing().size() == 1 &&
        frame->outgoing().front().frozen->row.key == child.key,
        "A different branch settles the old transition before starting its own exit");
    list->settle();
    view.arrange({0, 0, 280, 140});
    list->set_offset(list->maximum_offset());
    view.set_item_expanded({10, 3}, true);
    require(!view.animating(), "Offscreen branch expansion uses immediate geometry");
}
void scroll_anchor() {
    auto data = entries();
    for (std::uint64_t id = 30; id < 100; ++id) {
        NavigationItem row; row.key = {id, 3}; row.label = L"Row"; data.push_back(row);
    }
    NavigationView view; view.set_items(data); view.arrange({0, 0, 280, 300}); view.set_duration(1000);
    const auto list = view.items();
    list->select({20, 3}, SelectionGesture::focus_only);
    list->set_offset(60);
    const auto before = tail_y(view);
    view.set_item_expanded({10, 3}, false);
    require(view.animating(), "A visible subtree can close with its header above the viewport");
    list->advance(Animation::Clock::now() + 50ms);
    require(std::abs(tail_y(view) - before) < 0.001, "A surviving visible key retains its viewport position during motion");
    list->advance(Animation::Clock::now() + 1100ms);
    require(list->offset() == 0 && tail_y(view) == 40, "Scroll intent clamps at the final content boundary");
}
void archived_scroll_rows() {
    auto data = entries();
    data.erase(data.begin() + 1, data.begin() + 5);
    for (std::uint64_t id = 100; id < 200; ++id) {
        NavigationItem child; child.key = {id, 3}; child.parent = ItemKey{10, 3};
        child.label = L"Archived " + std::to_wstring(id); child.image_path = child.label + L".png";
        data.push_back(std::move(child));
    }
    for (std::uint64_t id = 300; id < 380; ++id) {
        NavigationItem row; row.key = {id, 3}; row.label = L"Tail row"; data.push_back(row);
    }
    NavigationView view; view.set_items(data); view.arrange({0, 0, 280, 300}); view.set_duration(1000);
    const auto list = view.items();
    list->select({20, 3}, SelectionGesture::focus_only); list->set_offset(3960);
    const auto first = list->visible_content().front().key.id;
    const auto y = tail_y(view);
    view.set_item_expanded({10, 3}, false);
    const auto logical = list->source();
    list->advance(Animation::Clock::now() + 100ms);
    const auto frame = CollectionPresentationAccess::get(*list);
    require(frame && !frame->outgoing().empty() && frame->outgoing().front().frozen->row.key.id < first,
        "Anchored scrolling samples newly exposed exit rows from the old immutable snapshot");
    require(list->source() == logical && std::abs(tail_y(view) - y) < 0.001 && frame->outgoing().size() < 10,
        "Exit sampling remains viewport-bounded without replacing the logical source");
    for (const auto& row : frame->outgoing()) require(!logical->find(row.frozen->row.key) &&
        row.frozen->visual.image_path == row.frozen->row.content.primary + L".png",
        "Outgoing text and images retain old identities and never use new logical indexes");
}
}
int main() {
    try {
        sampled_contract(); settlement_contract(); interruption_contract(); scroll_anchor(); archived_scroll_rows();
        std::cout << "Navigation disclosure sources, reversal, focus, scroll anchors, validation, and settlement passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
