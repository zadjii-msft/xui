#include "xui/controls.hpp"
#include "xui/titlebar.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <type_traits>

namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(float actual, float expected) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 0.01f, "Tab presentation geometry mismatch");
}
void same(Rect a, Rect b) { near(a.x, b.x); near(a.y, b.y); near(a.width, b.width); near(a.height, b.height); }
void geometry(TabStrip& strip, bool gaps = false) {
    float edge = strip.content_bounds().x;
    for (std::size_t i = 0; i < strip.tabs().size(); ++i) {
        const auto tab = strip.tab_bounds(i);
        if (gaps) require(tab.x >= edge - 0.01f, "Removal rectangles must not overlap");
        else near(tab.x, edge);
        edge = tab.x + tab.width;
        if (tab.width > 0) {
            require(strip.hit_test(Point{tab.x + tab.width / 2, tab.y + tab.height / 2}) == i,
                "Hit testing must use each ID's presented rectangle");
            const auto close = strip.close_bounds(i);
            if (close.width > 0) {
                require(close.x >= tab.x && close.x + close.width <= tab.x + tab.width,
                    "Close geometry must remain within the presented tab");
                require(strip.hit_test(close.x + close.width / 2) == i, "Close action must hit its presented ID");
            }
        }
    }
    if (gaps) require(strip.new_tab_button_bounds().x >= edge - 0.01f, "The New button stays outside surviving tabs");
    else near(strip.new_tab_button_bounds().x, edge);
    near(strip.new_tab_button()->bounds().x, strip.bounds().x + strip.new_tab_button_bounds().x);
}
void contracts() {
    static_assert(std::is_base_of_v<Animation, TabStrip>);
    TabStrip tabs;
    tabs.set_new_tab_button_visible(true);
    tabs.on_close([](std::uint64_t) {});
    const auto button = tabs.new_tab_button();
    tabs.arrange({10, 20, 400, 38});
    tabs.set_duration(200);
    tabs.set_tabs({{11, L"First"}, {22, L"Last"}}, 11);
    require(!tabs.animating(), "Initial population must remain immediate");
    const auto first = tabs.tab_bounds(0), last = tabs.tab_bounds(1);
    unsigned layout{}, placement{}, paint{}, selections{};
    tabs.set_invalidator([&](Invalidation kind) {
        if (kind == Invalidation::layout) ++layout;
        else if (kind == Invalidation::placement) ++placement;
        else ++paint;
    });
    tabs.on_select([&](std::uint64_t) { ++selections; });
    tabs.set_tabs({{11, L"First"}, {33, L"Inserted"}, {22, L"Last"}}, 33);
    require(tabs.animating() && tabs.selected() == 33 && selections == 0, "Insertion selects logically without a synthetic event");
    same(tabs.tab_bounds(0), first);
    same(tabs.tab_bounds(2), last);
    near(tabs.tab_bounds(1).width, 0);
    require(tabs.close_bounds(1).width == 0, "A zero-width incoming tab has no close target");
    require(tabs.new_tab_button() == button && tabs.retained_children().size() == 1,
        "All tabs share one strip and the original retained New tab button");
    geometry(tabs);
    const auto tick = Animation::Clock::now();
    tabs.advance(tick + std::chrono::milliseconds(50));
    require(tabs.animating() && tabs.tab_bounds(1).width > 0 && tabs.tab_bounds(1).width < 368.0f / 3,
        "Insertion has a visible intermediate width");
    require(tabs.tab_bounds(0).width < first.width && tabs.tab_bounds(2).x > last.x,
        "Existing stable IDs reflow rather than jump");
    geometry(tabs);
    const auto middle = tabs.tab_bounds(1);
    tabs.advance(tick);
    same(tabs.tab_bounds(1), middle);
    tabs.arrange(tabs.bounds());
    require(tabs.animating(), "Unchanged placement must not cancel motion");
    same(tabs.tab_bounds(1), middle);
    tabs.set_tabs({{11, L"Renamed"}, {33, L"Inserted"}, {22, L"Last"}}, 11);
    require(tabs.animating() && tabs.selected() == 11, "Metadata and visible selection preserve motion");
    same(tabs.tab_bounds(1), middle);
    tabs.advance(tick + std::chrono::seconds(1));
    require(!tabs.animating(), "Delayed clock delivery settles insertion");
    near(tabs.tab_bounds(0).width, 368.0f / 3);
    geometry(tabs);
    require(layout == 0 && paint == 0 && placement >= 3,
        "Insertion frames and compatible metadata refresh use placement without root layout");
    const auto invalidations = layout + placement + paint;
    tabs.advance(tick + std::chrono::seconds(2));
    tabs.settle();
    tabs.set_tabs(tabs.tabs(), tabs.selected());
    require(layout + placement + paint == invalidations, "Settled tabs do not request idle work");
}
void interruptions() {
    TabStrip tabs;
    tabs.arrange({0, 0, 1000, 38});
    tabs.set_new_tab_button_visible(true);
    tabs.set_duration(200);
    tabs.set_tabs({{1, L"One"}}, 1);
    tabs.set_tabs({{1, L"One"}, {2, L"Two"}}, 2);
    tabs.advance(Animation::Clock::now() + std::chrono::milliseconds(50));
    const auto before = tabs.tab_bounds(1);
    tabs.set_tabs({{1, L"One"}, {3, L"Three"}, {2, L"Two"}}, 3);
    require(tabs.animating(), "Another compatible insertion retargets by stable ID");
    same(tabs.tab_bounds(2), before);
    near(tabs.tab_bounds(1).width, 0);
    geometry(tabs);
    bool invalid{};
    try { tabs.set_tabs({{1, L"One"}, {1, L"Duplicate"}}, 1); }
    catch (const std::invalid_argument&) { invalid = true; }
    require(invalid && tabs.animating() && tabs.tabs().size() == 3, "Invalid topology preserves active motion");
    invalid = false;
    try { tabs.set_duration(10001); } catch (const std::invalid_argument&) { invalid = true; }
    require(invalid && tabs.duration() == 200 && tabs.animating(), "Invalid duration must not settle motion");
    tabs.set_tabs({{3, L"Three"}, {1, L"One"}, {2, L"Two"}}, 3);
    require(tabs.animating(), "A stable-ID permutation retargets an active insertion");
    tabs.set_tabs({{3, L"Three"}, {1, L"One"}, {2, L"Two"}, {4, L"Four"}}, 4);
    require(tabs.animating(), "Insertion can interrupt an active reorder");
    tabs.set_tabs({{3, L"Three"}, {2, L"Two"}, {4, L"Four"}}, 3);
    require(tabs.animating() && tabs.tabs().size() == 3, "Removal reflows survivors without a retained exit item");
    tabs.set_tabs({{3, L"Three"}, {2, L"Two"}, {4, L"Four"}, {5, L"Five"}}, 5);
    tabs.set_duration(0);
    require(!tabs.animating(), "Disabling motion settles");
    tabs.set_tabs({{3, L"Three"}, {2, L"Two"}, {4, L"Four"}, {5, L"Five"}, {6, L"Six"}}, 6);
    require(!tabs.animating(), "Zero duration preserves immediate insertion");
    tabs.set_tabs({{1, L"One"}}, 1);
    tabs.set_duration(200);
    tabs.set_tabs({{1, L"One"}, {2, L"Two"}}, 2);
    tabs.arrange({0, 0, 400, 38});
    require(!tabs.animating(), "Resize settles before exposing new geometry");
    tabs.set_tabs({{1, L"One"}, {2, L"Two"}, {3, L"Three"}}, 3);
    require(tabs.animating(), "A fitting strip still animates");
    tabs.set_tabs({{1, L"One"}, {2, L"Two"}, {3, L"Three"}, {4, L"Four"}}, 4);
    require(!tabs.animating(), "Overflow settles and reveals logical selection immediately");
    require(tabs.tab_bounds(3).width >= 120 && tabs.tab_bounds(0).width == 0, "Overflow keeps the selected tab visible");
    require(tabs.new_tab_button_bounds().width == 32, "Overflow must not move the native New tab button outside the strip");
    tabs.set_tabs({{4, L"Four"}, {2, L"Two"}, {3, L"Three"}, {1, L"One"}}, 1);
    require(!tabs.animating() && tabs.selected() == 1 && tabs.tab_bounds(3).width >= 120,
        "Overflow reorder remains immediate and exposes logical selection");
    tabs.set_tabs({{1, L"One"}}, 1);
    tabs.set_tabs({{1, L"One"}, {2, L"Two"}}, 2);
    tabs.set_new_tab_button_visible(false);
    require(!tabs.animating(), "Changing the action viewport settles motion");
    tabs.set_tabs({{1, L"One"}, {2, L"Two"}, {3, L"Three"}}, 3);
    PartStyleValues style; style.width = 100.0f;
    tabs.set_control_style_values(StylePart::tab, style);
    require(!tabs.animating(), "Metric style changes settle instead of keeping stale rectangles");
    near(tabs.tab_bounds(0).width, 100);
    tabs.set_tabs({}, {});
    require(!tabs.animating() && !tabs.selected(), "Clearing tabs releases every motion item");
    TabStrip initial;
    require(initial.duration() == 0 && !initial.animating(), "Motion defaults to disabled");
    initial.set_duration(10000);
    initial.set_tabs({{1, L"First"}}, 1);
    initial.set_tabs({{1, L"First"}, {2, L"Second"}}, 2);
    require(!initial.animating(), "Population before an arranged viewport remains immediate");
}
void removal() {
    TabStrip tabs;
    tabs.arrange({10, 20, 800, 38});
    tabs.set_new_tab_button_visible(true);
    tabs.set_duration(1000);
    tabs.set_tabs({{1, L"One"}, {2, L"Two"}, {3, L"Three"}}, 2);
    const auto first = tabs.tab_bounds(0), last = tabs.tab_bounds(2);
    const auto button_x = tabs.new_tab_button_bounds().x;
    unsigned layouts{}, placements{}, closes{};
    tabs.set_invalidator([&](Invalidation kind) {
        if (kind == Invalidation::layout) ++layouts;
        if (kind == Invalidation::placement) ++placements;
    });
    tabs.on_close([&](auto) { ++closes; });
    tabs.set_tabs({{1, L"One"}, {3, L"Three"}}, 3);
    require(tabs.animating() && tabs.selected() == 3 && tabs.tabs().size() == 2,
        "Removal updates logical tabs and selection immediately");
    same(tabs.tab_bounds(0), first);
    same(tabs.tab_bounds(1), last);
    near(tabs.new_tab_button_bounds().x, button_x);
    require(!tabs.hit_test(first.x + first.width + 10), "The removed tab leaves no stale pointer target");
    tabs.request_close(2);
    require(closes == 0 && !tabs.select(2), "Removed IDs cannot receive close or selection commands");
    const auto start = Animation::Clock::now();
    tabs.advance(start + std::chrono::milliseconds(200));
    const auto middle = tabs.tab_bounds(1);
    require(middle.x < last.x && middle.x > first.x + first.width,
        "Surviving tabs present an intermediate gap-closing position");
    geometry(tabs, true);
    const auto middle_button = tabs.new_tab_button_bounds().x;
    tabs.set_tabs({{1, L"One"}, {4, L"Four"}, {3, L"Three"}}, 4);
    same(tabs.tab_bounds(2), middle);
    near(tabs.new_tab_button_bounds().x, middle_button);
    geometry(tabs, true);
    tabs.advance(Animation::Clock::now() + std::chrono::milliseconds(100));
    geometry(tabs, true);
    tabs.settle();
    geometry(tabs);
    const auto trailing_button = tabs.new_tab_button_bounds().x;
    tabs.set_tabs({{1, L"One"}, {4, L"Four"}}, 4);
    near(tabs.new_tab_button_bounds().x, trailing_button);
    tabs.advance(Animation::Clock::now() + std::chrono::milliseconds(200));
    require(tabs.new_tab_button_bounds().x < trailing_button &&
        tabs.new_tab_button_bounds().x > tabs.tab_bounds(1).x + tabs.tab_bounds(1).width,
        "Trailing removal moves the retained New button across the closing gap");
    geometry(tabs, true);
    tabs.set_tabs({{4, L"Four"}}, 4);
    geometry(tabs, true);
    tabs.advance(Animation::Clock::now() + std::chrono::seconds(2));
    require(!tabs.animating() && layouts == 0 && placements > 0,
        "Removal and retargeting use placement-only work and terminate");
    geometry(tabs);
    const auto count = placements;
    tabs.advance(Animation::Clock::now() + std::chrono::seconds(3));
    require(placements == count, "Settled removal has no idle work");
    tabs.set_tabs({}, {});
    require(!tabs.animating(), "Clearing the final tab remains immediate");
}
std::vector<std::pair<std::uint64_t, Rect>> snapshot(const TabStrip& tabs) {
    std::vector<std::pair<std::uint64_t, Rect>> result;
    for (std::size_t i = 0; i < tabs.tabs().size(); ++i) result.emplace_back(tabs.tabs()[i].id, tabs.tab_bounds(i));
    return result;
}
void preserves_survivors(const TabStrip& tabs, const std::vector<std::pair<std::uint64_t, Rect>>& before) {
    for (std::size_t i = 0; i < tabs.tabs().size(); ++i)
        for (const auto& [id, rect] : before) if (tabs.tabs()[i].id == id) same(tabs.tab_bounds(i), rect);
}
void crossing_geometry(TabStrip& tabs) {
    const auto content = tabs.content_bounds();
    const auto button = tabs.new_tab_button_bounds();
    for (std::size_t i = 0; i < tabs.tabs().size(); ++i) {
        const auto rect = tabs.tab_bounds(i);
        require(std::isfinite(rect.x) && std::isfinite(rect.width) && rect.width >= 0 &&
            rect.x >= content.x && rect.x + rect.width <= button.x + 0.001f,
            "Crossing tabs remain finite and outside the retained New button");
        for (std::size_t j = i + 1; j < tabs.tabs().size(); ++j) {
            const auto other = tabs.tab_bounds(j);
            require(rect.width == 0 || other.width == 0 ||
                rect.x + rect.width <= other.x || other.x + other.width <= rect.x,
                "Presented rectangles do not overlap regardless of logical or drawing order");
        }
        if (rect.width > 0.001f) {
            const Point center{rect.x + rect.width / 2, rect.y + rect.height / 2};
            require(tabs.hit_test(center) == i && tabs.prepare_context_menu(center) && tabs.context_tab() == tabs.tabs()[i].id,
                "Pointer and context actions address the displayed stable ID");
            const auto close = tabs.close_bounds(i);
            if (close.width > 0)
                require(tabs.hit_test(Point{close.x + close.width / 2, close.y + close.height / 2}) == i,
                    "Visible close targets cannot select another crossing tab");
        }
    }
    same(tabs.new_tab_button()->bounds(), {tabs.bounds().x + button.x, tabs.bounds().y + button.y, button.width, button.height});
}
void reorder() {
    TabStrip tabs;
    tabs.arrange({10, 20, 800, 38});
    tabs.set_new_tab_button_visible(true);
    tabs.set_duration(10000);
    tabs.set_tabs({{1, L"One"}, {2, L"Two"}, {3, L"Three"}}, 2);
    unsigned layouts{}, placements{}, selections{}, closes{};
    tabs.set_invalidator([&](Invalidation kind) {
        if (kind == Invalidation::layout) ++layouts;
        if (kind == Invalidation::placement) ++placements;
    });
    tabs.on_select([&](auto) { ++selections; });
    tabs.on_close([&](auto) { ++closes; });
    const auto before = snapshot(tabs);
    const auto button = tabs.new_tab_button();
    const auto revision = tabs.tabs_revision();
    tabs.set_tabs({{3, L"Three"}, {2, L"Two"}, {1, L"One"}}, 3);
    const auto start = Animation::Clock::now();
    require(tabs.animating() && tabs.tabs_revision() > revision && tabs.tabs()[0].id == 3 && tabs.selected() == 3 &&
        selections == 0, "Reorder publishes logical order and selection before the first frame without a callback");
    preserves_survivors(tabs, before);
    for (int milliseconds : {100, 1000, 1800, 2063, 2500, 5000, 9000}) {
        tabs.advance(start + std::chrono::milliseconds(milliseconds));
        crossing_geometry(tabs);
        const auto left = tabs.tab_bounds(0), right = tabs.tab_bounds(2);
        require(left.x + left.width / 2 < before[2].second.x + before[2].second.width / 2 &&
            right.x + right.width / 2 > before[0].second.x + before[0].second.width / 2,
            "Stable IDs travel toward their new positions");
        if (milliseconds == 2063)
            require(left.width < 5 && right.width < 5 && tabs.close_bounds(0).width == 0 && tabs.close_bounds(2).width == 0,
                "Crossing tabs narrow around their centers rather than overlap or retain hidden close targets");
        const auto sample = snapshot(tabs);
        tabs.advance(start);
        preserves_survivors(tabs, sample);
    }
    tabs.advance(start + std::chrono::seconds(11));
    require(!tabs.animating() && tabs.new_tab_button() == button && layouts == 0 && placements > 0,
        "Reorder retains native ownership and uses only placement frames");
    geometry(tabs);
    const auto idle = placements;
    tabs.settle(); tabs.advance(start + std::chrono::seconds(12)); tabs.set_tabs(tabs.tabs(), tabs.selected());
    require(placements == idle, "Completed reorder has no idle work");

    tabs.set_tabs({{1, L"One"}, {2, L"Two"}, {3, L"Three"}}, 1);
    tabs.advance(Animation::Clock::now() + std::chrono::milliseconds(1000));
    auto sample = snapshot(tabs);
    tabs.set_tabs({{2, L"Two"}, {1, L"One"}, {3, L"Three"}}, 2);
    preserves_survivors(tabs, sample);
    require(tabs.animating(), "Interrupted reorder uses each current displayed rectangle");
    tabs.advance(Animation::Clock::now() + std::chrono::milliseconds(1000));
    crossing_geometry(tabs);
    sample = snapshot(tabs);
    tabs.set_tabs({{2, L"Renamed"}, {1, L"One"}, {3, L"Three"}}, 3);
    preserves_survivors(tabs, sample);
    const auto previous_button = tabs.new_tab_button_bounds();
    tabs.set_tabs({{2, L"Renamed"}, {4, L"Four"}, {1, L"One"}, {3, L"Three"}}, 4);
    preserves_survivors(tabs, sample);
    same(tabs.new_tab_button_bounds(), previous_button);
    require(tabs.tab_bounds(1).width == 0 && tabs.animating(), "Insertion can interrupt reorder without a jump");
    tabs.advance(Animation::Clock::now() + std::chrono::milliseconds(1000));
    crossing_geometry(tabs);
    sample = snapshot(tabs);
    tabs.set_tabs({{4, L"Four"}, {3, L"Three"}}, 3);
    preserves_survivors(tabs, sample);
    require(tabs.animating() && !tabs.select(2), "Removal during reorder immediately retires removed IDs");
    tabs.request_close(2);
    require(closes == 0, "A removed ID cannot receive a close command");
    tabs.advance(Animation::Clock::now() + std::chrono::milliseconds(1000));
    crossing_geometry(tabs);
    sample = snapshot(tabs);
    tabs.set_tabs({{3, L"Three"}, {4, L"Four"}}, 3);
    preserves_survivors(tabs, sample);
    tabs.settle();
    geometry(tabs);
    tabs.set_tabs({{4, L"Four"}, {3, L"Three"}}, 4);
    tabs.set_duration(0);
    require(!tabs.animating(), "Disabling motion settles reordered geometry");
    tabs.set_tabs({{3, L"Three"}, {4, L"Four"}}, 3);
    require(!tabs.animating(), "Zero-duration reorder remains immediate");
    geometry(tabs);
    tabs.set_duration(10000);
    tabs.set_tabs({{4, L"Four"}, {3, L"Three"}}, 4);
    tabs.set_tabs({{4, L"Four"}, {9, L"Replacement"}}, 9);
    require(!tabs.animating(), "A same-size replacement is not a stable-ID permutation");
    tabs.set_tabs({{9, L"Replacement"}, {4, L"Four"}}, 9);
    tabs.arrange({10, 20, 700, 38});
    require(!tabs.animating(), "Resize settles active reorder");
    tabs.set_tabs({{4, L"Four"}, {9, L"Replacement"}}, 4);
    PartStyleValues style; style.width = 100.0f;
    tabs.set_control_style_values(StylePart::tab, style);
    require(!tabs.animating(), "Metric changes settle active reorder");
    tabs.set_tabs({{9, L"Replacement"}, {4, L"Four"}}, 9);
    tabs.set_new_tab_button_visible(false);
    require(!tabs.animating(), "Changing the New button viewport settles active reorder");
}
void overflow() {
    TabStrip tabs;
    PartStyleValues root; root.padding = Insets{3.25f, 1.5f, 8.75f, 2.25f};
    tabs.set_control_style_values(StylePart::root, root);
    tabs.arrange({10.125f, 20.25f, 460.3f, 38});
    tabs.set_new_tab_button_visible(true);
    tabs.on_close([](auto) {});
    std::vector<TabItem> items;
    for (std::uint64_t id = 1; id <= 24; ++id) items.push_back({id, L"Document"});
    tabs.set_tabs(items, 1);
    tabs.set_duration(10000);
    const auto button = tabs.new_tab_button_bounds();
    const auto first = tabs.tab_bounds(0);
    unsigned layouts{}, placements{}, selections{};
    tabs.set_invalidator([&](Invalidation kind) {
        if (kind == Invalidation::layout) ++layouts;
        if (kind == Invalidation::placement) ++placements;
    });
    tabs.on_select([&](auto) { ++selections; });
    auto geometry = [&] {
        float edge = tabs.content_bounds().x;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto rect = tabs.tab_bounds(i);
            if (rect.width <= 0) continue;
            require(rect.x >= edge && rect.x + rect.width <= button.x,
                "Overflow rectangles remain disjoint and outside the New button");
            edge = rect.x + rect.width;
            const Point center{rect.x + rect.width / 2, rect.y + rect.height / 2};
            require(tabs.hit_test(center) == i && tabs.prepare_context_menu(center) && tabs.context_tab() == items[i].id,
                "Overflow pointer and context targets follow the displayed ID, not the target viewport");
            const auto close = tabs.close_bounds(i);
            if (close.width > 0)
                require(tabs.hit_test(close.x + close.width / 2) == i, "Overflow close targets follow presentation");
        }
        same(tabs.new_tab_button_bounds(), button);
        require(!tabs.hit_test(button.x + button.width / 2), "Overflow never covers the New button");
    };
    require(tabs.select(24) && tabs.selected() == 24 && tabs.animating() && selections == 1,
        "Overflow selection changes immediately while viewport presentation starts at its prior position");
    same(tabs.tab_bounds(0), first);
    geometry();
    auto start = Animation::Clock::now();
    for (int frame = 1; frame <= 128; ++frame) {
        tabs.advance(start + std::chrono::microseconds(frame * 5000000LL / 128));
        geometry();
    }
    const auto before = snapshot(tabs);
    tabs.select(1);
    require(tabs.animating() && tabs.selected() == 1 && selections == 2, "Overflow reversal retains logical selection semantics");
    preserves_survivors(tabs, before);
    start = Animation::Clock::now();
    for (int frame = 1; frame < 256; ++frame) {
        tabs.advance(start + std::chrono::microseconds(frame * 10000000LL / 256));
        geometry();
    }
    tabs.advance(start + std::chrono::seconds(11));
    require(!tabs.animating() && layouts == 0 && placements > 0, "Overflow uses placement frames and stops");
    same(tabs.tab_bounds(0), first);
    geometry();
    const auto idle = placements;
    tabs.advance(start + std::chrono::seconds(12));
    tabs.settle();
    require(placements == idle, "Settled overflow has no idle invalidation");

    tabs.set_tabs(items, 24);
    require(tabs.animating() && selections == 2, "Same-ID source refresh can reveal selection without synthesizing selection events");
    tabs.advance(Animation::Clock::now() + std::chrono::milliseconds(1000));
    const auto metadata = snapshot(tabs);
    items[0].title = L"Renamed";
    tabs.set_tabs(items, 24);
    preserves_survivors(tabs, metadata);
    require(tabs.animating(), "Metadata refresh preserves viewport motion");
    tabs.arrange({30, 40, 460.3f, 38});
    preserves_survivors(tabs, metadata);
    tabs.arrange({30, 40, 520, 38});
    require(!tabs.animating() && tabs.tab_bounds(23).width >= 120, "Resize settles and reveals selected overflow content");
    tabs.select(1);
    tabs.set_new_tab_button_visible(false);
    require(!tabs.animating(), "Action viewport changes settle overflow");
    tabs.select(24);
    tabs.set_duration(0);
    require(!tabs.animating() && tabs.tab_bounds(23).width >= 120, "Immediate mode settles the logical overflow target");
    tabs.select(1);
    require(!tabs.animating() && tabs.tab_bounds(0).width > 0, "Zero-duration overflow remains immediate");
    tabs.set_duration(10000);
    tabs.select(24);
    items.erase(items.begin());
    tabs.set_tabs(items, 24);
    require(!tabs.animating() && !tabs.select(1), "Overflow topology changes settle and retire removed identities immediately");

    TabStrip narrow;
    narrow.arrange({0, 0, 250, 38});
    narrow.set_new_tab_button_visible(true);
    narrow.set_tabs(items, items.front().id);
    const auto narrow_button = narrow.new_tab_button_bounds();
    narrow.select(24);
    require(!narrow.animating() && narrow.tab_bounds(items.size() - 2).width > 0,
        "Narrow immediate viewports do not discard visible neighbors to seek an impossible minimum width");
    same(narrow.new_tab_button_bounds(), narrow_button);
    narrow.set_duration(1000);
    narrow.select(items.front().id);
    same(narrow.new_tab_button_bounds(), narrow_button);
    narrow.settle();
    same(narrow.new_tab_button_bounds(), narrow_button);
    require(narrow.tab_bounds(0).width > 0, "Narrow overflow reaches its selected endpoint without moving New tab");
    narrow.select(24);
    narrow.arrange({0, 0, 5000, 38});
    require(!narrow.animating() && narrow.tab_bounds(0).width > 0 && narrow.tab_bounds(items.size() - 1).width > 0,
        "A viewport that fits all tabs restores the leading tabs after overflow");

    TabStrip partial;
    partial.arrange({0, 0, 1100, 38});
    partial.set_new_tab_button_visible(true);
    partial.set_tabs(items, items.front().id);
    const auto partial_button = partial.new_tab_button_bounds();
    partial.select(24);
    same(partial.new_tab_button_bounds(), partial_button);
    require(partial.tab_bounds(items.size() - 1).x + partial.tab_bounds(items.size() - 1).width <= partial_button.x,
        "Overflow reserves the action slot even when the final tab leaves unused viewport space");
}
void aligned_titlebar() {
    TitleBar bar{L"Pane-aligned tabs"};
    auto first = std::make_shared<Label>(L"First pane");
    auto second = std::make_shared<Label>(L"Second pane");
    first->arrange({280, 44, 400, 500});
    second->arrange({686, 44, 376, 500});
    bar.set_title_visible(false);
    bar.leading()->set_visible(true);
    bar.secondary_tabs()->set_visible(true);
    bar.set_tab_panes(first, second);
    bar.arrange({0, 0, 1200, 44});
    for (const auto& tabs : {bar.tabs(), bar.secondary_tabs()}) {
        tabs->set_new_tab_button_visible(true);
        tabs->set_duration(1000);
        tabs->set_tabs({{1, L"One"}, {2, L"Two"}}, 1);
        tabs->set_tabs({{1, L"One"}, {3, L"Inserted"}, {2, L"Two"}}, 3);
        const auto inserted = snapshot(*tabs);
        bar.arrange(bar.bounds());
        require(tabs->animating(), "Unchanged pane-aligned titlebar layout preserves insertion");
        preserves_survivors(*tabs, inserted);
        tabs->settle();
        tabs->set_tabs({{2, L"Two"}, {3, L"Inserted"}, {1, L"One"}}, 3);
        bar.arrange(bar.bounds());
        require(tabs->animating(), "Unchanged pane-aligned titlebar layout preserves reorder");
        tabs->settle();
        tabs->set_tabs({{2, L"Two"}, {1, L"One"}}, 1);
        bar.arrange(bar.bounds());
        require(tabs->animating(), "Unchanged pane-aligned titlebar layout preserves removal");
        tabs->settle();
        std::vector<TabItem> items;
        for (std::uint64_t id = 1; id <= 12; ++id) items.push_back({id, L"Document"});
        tabs->set_tabs(items, 12);
        tabs->select(1);
        tabs->advance(Animation::Clock::now() + std::chrono::milliseconds(100));
        const auto scrolling = snapshot(*tabs);
        bar.arrange(bar.bounds());
        require(tabs->animating(), "Unchanged pane-aligned titlebar layout preserves overflow");
        preserves_survivors(*tabs, scrolling);
        tabs->select(12);
    }
    first->arrange({280, 44, 410, 500});
    bar.arrange(bar.bounds());
    require(!bar.tabs()->animating() && bar.secondary_tabs()->animating(),
        "Actual pane resize settles only the strip whose viewport changes");
}
void fractional_crossings() {
    for (float width : {460.3f, 800.25f}) {
        TabStrip tabs;
        PartStyleValues root; root.padding = Insets{3.25f, 1.5f, 8.75f, 2.25f};
        tabs.set_control_style_values(StylePart::root, root);
        tabs.arrange({10.125f, 20.25f, width, 38});
        tabs.set_new_tab_button_visible(true);
        tabs.on_close([](auto) {});
        tabs.set_duration(10000);
        tabs.set_tabs({{1, L"One"}, {2, L"Two"}, {3, L"Three"}}, 1);
        std::array<std::uint64_t, 3> order{1, 2, 3};
        while (std::next_permutation(order.begin(), order.end())) {
            const auto before = snapshot(tabs);
            const auto start = Animation::Clock::now();
            tabs.set_tabs({{order[0], L"Tab"}, {order[1], L"Tab"}, {order[2], L"Tab"}}, 1);
            preserves_survivors(tabs, before);
            require(tabs.animating(), "Every fitting stable-ID permutation supports motion");
            for (int frame = 0; frame < 512; ++frame) {
                tabs.advance(start + std::chrono::microseconds(frame * 10000000LL / 512));
                crossing_geometry(tabs);
            }
            tabs.settle();
            geometry(tabs);
        }
    }
}
}
int main() {
    try { contracts(); interruptions(); removal(); reorder(); fractional_crossings(); overflow(); aligned_titlebar(); std::cout << "Tab insertion/removal/reorder/overflow model contracts passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
