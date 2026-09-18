#include "xui/reveal.hpp"
#include "xui/adaptive_layout.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(float actual, float expected, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 0.01f, message);
}
void contracts() {
    auto content = std::make_shared<Stack>(Axis::horizontal);
    content->set_padding({6, 6, 6, 6});
    auto edit = std::make_shared<TextInput>(L"Find");
    edit->set_caption_visible(false);
    edit->set_fixed_size({320, 44});
    content->add(edit);
    Reveal reveal(content);
    require(!reveal.open() && !reveal.animating() && reveal.duration() == 0, "Reveal is closed and motion is opt-in");
    near(reveal.measure({500, 500}).height, 0, "Closed reveal reserves no height");
    require(reveal.retained_children().size() == 1 && reveal.content() == content, "Reveal retains one child");
    reveal.set_open(true);
    near(reveal.progress(), 1, "Default opening is immediate");
    near(reveal.measure({500, 500}).height, 56, "Open reveal uses full child height");
    reveal.arrange({10, 20, 500, 56});
    near(edit->bounds().height, 44, "Reveal preserves the native editor extent");
    reveal.set_open(false);
    reveal.arrange({10, 20, 500, 56});
    near(reveal.bounds().height, 0, "Closed reveal collapses even in an oversized slot");
    reveal.set_duration(200);
    unsigned layout{}, placement{}, paint{};
    reveal.set_invalidator([&](Invalidation kind) {
        if (kind == Invalidation::layout) ++layout;
        else if (kind == Invalidation::placement) ++placement;
        else ++paint;
    });
    reveal.set_open(true);
    require(reveal.animating(), "Explicit duration starts an opening transition");
    near(reveal.measure({500, 500}).height, 56, "Opening reserves full height before motion");
    reveal.arrange({10, 20, 500, 56});
    near(content->bounds().y, 76, "Closed presentation starts below its clip");
    const auto before = Reveal::Clock::now();
    reveal.advance(before + std::chrono::milliseconds(50));
    require(reveal.progress() > 0 && reveal.progress() < 1, "Clock advances to an intermediate position");
    require(placement == 1 && layout == 1 && paint == 0, "Intermediate motion requests placement, not root layout");
    reveal.arrange(reveal.bounds());
    near(content->bounds().y, 20 + 56 * (1 - reveal.progress()), "Full-size content slides inside fixed bounds");
    near(edit->bounds().height, 44, "Intermediate frames do not shrink the editor");
    const auto midpoint = reveal.progress();
    reveal.set_open(false);
    near(reveal.progress(), midpoint, "Reversal starts at the current presentation");
    require(!reveal.open() && reveal.animating(), "Logical close precedes exit completion");
    near(reveal.measure({500, 500}).height, 56, "Exit retains the slot");
    reveal.advance(Reveal::Clock::now() + std::chrono::milliseconds(50));
    require(reveal.progress() < midpoint && reveal.progress() > 0, "Reversed motion moves toward closed");
    const auto closing = reveal.progress();
    reveal.set_open(true);
    near(reveal.progress(), closing, "A second reversal is continuous");
    reveal.advance(Reveal::Clock::now() + std::chrono::seconds(2));
    require(!reveal.animating() && reveal.progress() == 1, "Delayed clock delivery settles exactly at the endpoint");
    const auto notifications = layout + placement;
    reveal.set_open(true);
    reveal.advance(Reveal::Clock::now() + std::chrono::seconds(3));
    require(layout + placement == notifications, "Settled state has no periodic work");
    reveal.set_open(false);
    reveal.advance(Reveal::Clock::now() + std::chrono::seconds(2));
    require(!reveal.animating() && reveal.progress() == 0, "Exit finishes exactly closed");
    near(reveal.measure({500, 500}).height, 0, "Completed exit releases its slot");
    reveal.set_open(true);
    reveal.set_duration(0);
    require(!reveal.animating() && reveal.progress() == 1, "Disabling motion settles an active transition");
    reveal.set_duration(200);
    reveal.set_open(false);
    reveal.settle();
    require(!reveal.animating() && reveal.progress() == 0, "Host cancellation settles to the logical state");
    bool invalid{};
    try { reveal.set_duration(10001); } catch (const std::invalid_argument&) { invalid = true; }
    require(invalid && reveal.duration() == 200, "Invalid duration rejects without changing state");
    invalid = false;
    try { Reveal missing(nullptr); } catch (const std::invalid_argument&) { invalid = true; }
    require(invalid, "Missing content rejects");
    invalid = false;
    try { Reveal duplicate(content); } catch (const std::invalid_argument&) { invalid = true; }
    require(invalid, "Shared child ownership rejects");
}
void rounded_completion_contracts() {
    for (const auto layout : {RevealLayout::fixed, RevealLayout::expand}) {
        for (const bool opening : {false, true}) {
            auto child = std::make_shared<Element>();
            child->set_preferred_size({120, 56});
            Reveal reveal(child);
            reveal.set_layout(layout);
            reveal.set_open(!opening);
            reveal.set_duration(10000);
            unsigned notifications{};
            Invalidation last{};
            reveal.set_invalidator([&](Invalidation kind) { ++notifications; last = kind; });
            reveal.set_open(opening);
            const auto now = Reveal::Clock::now();
            reveal.advance(now + std::chrono::milliseconds(9999));
            require(reveal.animating() && reveal.progress() == (opening ? 1 : 0),
                "Cubic easing can round to its endpoint before the clock completes");
            const auto before = notifications;
            reveal.advance(now + std::chrono::milliseconds(10001));
            require(!reveal.animating() && notifications == before + 1,
                "Rounded completion still invalidates terminal presentation");
            require(last == (layout == RevealLayout::expand || !opening ? Invalidation::layout : Invalidation::placement),
                "Rounded fixed exit releases layout; fixed entry remains placement-only");
            reveal.advance(now + std::chrono::milliseconds(20000));
            require(notifications == before + 1, "Completed rounded motion has no idle work");
        }
    }
}
void expanding_contracts() {
    for (const auto direction : {RevealDirection::bottom, RevealDirection::top, RevealDirection::left, RevealDirection::right}) {
        auto child = std::make_shared<Element>();
        child->set_preferred_size({120, 56});
        Reveal reveal(child);
        require(reveal.layout() == RevealLayout::fixed && reveal.direction() == RevealDirection::bottom,
            "Existing defaults remain fixed and bottom");
        reveal.set_layout(RevealLayout::expand);
        reveal.set_direction(direction);
        reveal.set_duration(1000);
        unsigned layouts{}, placements{};
        reveal.set_invalidator([&](Invalidation kind) {
            if (kind == Invalidation::layout) ++layouts;
            if (kind == Invalidation::placement) ++placements;
        });
        reveal.set_open(true);
        const bool vertical = reveal.vertical();
        auto desired = reveal.measure({500, 400});
        near(vertical ? desired.height : desired.width, 0, "Opening reserves zero animated extent");
        reveal.arrange({10, 20, desired.width, desired.height});
        near(vertical ? child->bounds().height : child->bounds().width, vertical ? 56 : 120,
            "The child has full extent even at progress zero");
        reveal.advance(Reveal::Clock::now() + std::chrono::milliseconds(200));
        desired = reveal.measure({500, 400});
        near(vertical ? desired.height : desired.width, (vertical ? 56 : 120) * reveal.progress(),
            "The reserved extent uses the same progress as the presentation");
        const auto repeated = reveal.measure(desired);
        near(vertical ? repeated.height : repeated.width, vertical ? desired.height : desired.width,
            "Measurement with the allocated slot does not apply progress twice");
        reveal.arrange({10, 20, desired.width, desired.height});
        near(child->bounds().x, direction == RevealDirection::left ? 10 + desired.width - 120 : 10,
            "Horizontal edge determines the clipped child position");
        near(child->bounds().y, direction == RevealDirection::top ? 20 + desired.height - 56 : 20,
            "Vertical edge determines the clipped child position");
        require(layouts == 2 && placements == 0, "Expansion requests one layout per sampled frame");
        const auto progress = reveal.progress();
        reveal.set_open(false);
        near(reveal.progress(), progress, "Expanding exit starts at the current extent");
        reveal.advance(Reveal::Clock::now() + std::chrono::milliseconds(200));
        require(reveal.progress() > 0 && reveal.progress() < progress, "Exit shrinks the layout continuously");
        reveal.set_open(true);
        reveal.set_direction(direction == RevealDirection::bottom ? RevealDirection::top : RevealDirection::bottom);
        require(!reveal.animating() && reveal.progress() == 1, "A direction change settles the active target");
        bool rejected{};
        try { reveal.set_layout(static_cast<RevealLayout>(2)); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && reveal.layout() == RevealLayout::expand, "Invalid layout preserves the previous mode");
        rejected = false;
        try { reveal.set_direction(static_cast<RevealDirection>(4)); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Invalid direction rejects");
        reveal.set_open(false);
        reveal.set_layout(RevealLayout::fixed);
        require(!reveal.animating() && reveal.progress() == 0, "A layout change settles the active target");
    }
}
void coordinated_grid_contract() {
    auto files = std::make_shared<Element>();
    auto bar = std::make_shared<Stack>(Axis::horizontal);
    auto input = std::make_shared<TextInput>(L"Find");
    input->set_caption_visible(false);
    input->set_preferred_size({320, 44});
    bar->set_padding({6, 6, 6, 6});
    bar->add(input, 1);
    auto reveal = std::make_shared<Reveal>(bar);
    reveal->set_layout(RevealLayout::expand);
    reveal->set_duration(1000);
    Grid grid;
    grid.set_tracks({{TrackSizing::star, 1}, {TrackSizing::automatic}}, {{TrackSizing::star, 1}});
    grid.add(files, 0, 0);
    grid.add(reveal, 1, 0);
    reveal->set_open(true);
    grid.measure({500, 400});
    grid.arrange({0, 0, 500, 400});
    near(files->bounds().height, 400, "The details area does not snap before the first frame");
    const auto start = Reveal::Clock::now();
    for (int time : {50, 100, 300, 700, 1000}) {
        reveal->advance(start + std::chrono::milliseconds(time));
        for (const float width : {500.0f, 300.0f, 800.0f}) {
            grid.measure({width, 400});
            grid.arrange({0, 0, width, 400});
            near(reveal->bounds().height, 56 * reveal->progress(), "Grid uses the interpolated height after remeasurement and resize");
            near(files->bounds().height + reveal->bounds().height, 400, "Details and Find use one conserved layout extent");
            near(files->bounds().y + files->bounds().height, reveal->bounds().y, "Details and Find share a moving edge");
            near(bar->bounds().y, reveal->bounds().y, "Find content follows that edge without a second translation");
            near(input->bounds().height, 44, "Resizing the viewport never shrinks the native editor");
        }
    }
    reveal->set_open(false);
    reveal->advance(Reveal::Clock::now() + std::chrono::seconds(2));
    grid.arrange({0, 0, 500, 400});
    near(files->bounds().height, 400, "Exit releases the final extent");
    near(reveal->bounds().height, 0, "Exit reaches exactly zero");
    reveal->set_open(true);
    reveal->advance(Reveal::Clock::now() + std::chrono::milliseconds(300));
    reveal->set_maximum_size({500, 20});
    near(reveal->measure({500, 400}).height, 20, "A host maximum clips the animated reserved extent");
    reveal->arrange({0, 0, 500, 20});
    near(input->bounds().height, 44, "A host maximum never shrinks the native editor");
    reveal->arrange({0, 0, 200, 5});
    near(input->bounds().height, 44, "A smaller parent allocation clips instead of shrinking native input");

    auto unbounded = std::make_shared<Grid>();
    Reveal invalid(unbounded);
    invalid.set_layout(RevealLayout::expand);
    invalid.set_open(true);
    bool rejected{};
    try { invalid.measure({500, 400}); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Unbounded content requires an explicit natural extent");
}
void nested_scroll_contract() {
    auto input = std::make_shared<TextInput>(L"Nested Find");
    input->set_caption_visible(false);
    input->set_preferred_size({320, 44});
    auto inner = std::make_shared<Reveal>(input);
    inner->set_layout(RevealLayout::expand);
    inner->set_duration(1000);
    auto outer = std::make_shared<Reveal>(inner);
    outer->set_layout(RevealLayout::expand);
    outer->set_duration(1000);
    auto content = std::make_shared<Stack>(Axis::vertical);
    auto spacer = std::make_shared<Element>();
    spacer->set_fixed_size({320, 600});
    content->add(spacer);
    content->add(outer);
    ScrollView scroll(content);
    inner->set_open(true);
    outer->set_open(true);
    const auto now = Reveal::Clock::now() + std::chrono::milliseconds(300);
    inner->advance(now);
    outer->advance(now);
    scroll.arrange({0, 0, 400, 300});
    near(outer->bounds().height, 44 * inner->progress() * outer->progress(),
        "Nested expansion uses each independent progress once");
    scroll.set_offset(100);
    scroll.arrange({0, 0, 400, 300});
    near(scroll.offset(), 100, "Expansion preserves a valid scroll offset");
    near(input->bounds().height, 44, "Nested clips preserve the full native content extent");
    outer->set_open(false);
    outer->advance(Reveal::Clock::now() + std::chrono::seconds(2));
    scroll.arrange({0, 0, 400, 300});
    near(scroll.offset(), 100, "Collapsing content preserves an offset within the new range");
    near(scroll.extent(), 600, "Scroll extent releases the collapsed content");
}
}
int main() {
    try { contracts(); rounded_completion_contracts(); expanding_contracts(); coordinated_grid_contract(); nested_scroll_contract(); std::cout << "Reveal model contracts passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
