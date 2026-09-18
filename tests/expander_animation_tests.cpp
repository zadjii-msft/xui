#include "xui/foundation.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(float actual, float expected, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 0.01f, message);
}
void style(Expander& expander, unsigned variant) {
    if (variant == 1) expander.set_visual_style(VisualStyle::winui);
    if (variant == 2) {
        PartStyleValues root; root.padding = Insets{3, 4, 5, 6};
        PartStyleValues header; header.height = 50.0f;
        PartStyleValues body; body.padding = Insets{7, 5, 9, 7};
        expander.set_control_style(ControlStyle::create(StyleTarget::expander,
            {{StylePart::root, root}, {StylePart::header, header}, {StylePart::content, body}}, {}));
    }
}
void scalar_contract() {
    ScalarTransition transition(0);
    const auto start = Animation::Clock::time_point{};
    require(!transition.animating() && !transition.advance(start), "Idle scalar sampling does no work");
    require(transition.retarget(1, 10000, start), "Scalar targets change immediately");
    transition.advance(start + std::chrono::milliseconds(2500));
    const auto middle = transition.value();
    require(middle > 0 && middle < 1, "Scalar easing has intermediate values");
    require(!transition.advance(start) && transition.value() == middle, "Old clock samples cannot reverse motion");
    require(!transition.retarget(1, 10000, start), "Repeated scalar targets preserve the transition");
    transition.advance(start + std::chrono::milliseconds(9999));
    require(transition.animating() && transition.value() == 1, "Float easing can reach one before clock completion");
    require(transition.advance(start + std::chrono::milliseconds(10001)) && !transition.animating(),
        "Rounded scalar completion still reports a terminal change");
    require(!transition.settle() && !transition.advance(start + std::chrono::seconds(20)), "Settled scalar state has no idle work");
    transition.retarget(0, 10000, start);
    transition.advance(start + std::chrono::milliseconds(9999));
    require(transition.animating() && transition.value() == 0, "Closing can round to zero early");
    require(transition.advance(start + std::chrono::milliseconds(10001)), "Rounded closing publishes completion");
}
void animated_layout(unsigned variant, bool explicit_size) {
    auto child = std::make_shared<Element>();
    child->set_preferred_size({160, 80});
    auto expander = std::make_shared<Expander>(L"Details", child);
    style(*expander, variant);
    if (explicit_size) expander->set_preferred_size({300, 220});
    const auto open = expander->measure({500, 500});
    expander->arrange({10, 20, open.width, open.height});
    const auto full = child->bounds();
    require(expander->duration() == 0 && expander->expanded() && !expander->animating(),
        "Expander motion defaults to disabled");
    expander->set_expanded(false);
    const auto closed = expander->measure({500, 500});
    expander->arrange({10, 20, closed.width, closed.height});
    require(child->bounds().width == 0 && child->bounds().height == 0, "Zero-duration collapse retains legacy empty content geometry");
    expander->set_duration(1000);
    unsigned callbacks{}, invalidations{};
    Invalidation last_invalidation{};
    expander->on_change([&](bool) { ++callbacks; });
    expander->set_invalidator([&](Invalidation kind) {
        last_invalidation = kind;
        ++invalidations;
    });
    expander->set_expanded(true);
    require(expander->expanded() && expander->animating() && expander->progress() == 0 && callbacks == 0,
        "Logical expansion is immediate without a synthetic callback");
    require(expander->allows_empty_clip(), "Opening a zero-height body permits native focus");
    const auto start = Animation::Clock::now();
    for (int elapsed : {0, 100, 250, 500, 900, 1001}) {
        if (elapsed) expander->advance(start + std::chrono::milliseconds(elapsed));
        require(last_invalidation == Invalidation::layout, "Body animation frames request coordinated layout");
        const auto desired = expander->measure({500, 500});
        near(desired.height, closed.height + (open.height - closed.height) * expander->progress(),
            "Layout interpolates the legacy open and closed extents");
        for (int repeat = 0; repeat < 4; ++repeat) {
            const auto constrained = expander->measure({desired.width, desired.height});
            near(constrained.height, desired.height, "Repeated measurement does not scale body height twice");
            expander->arrange({10, 20, desired.width, desired.height});
            near(child->bounds().height, full.height, "Body content retains its full height throughout entry");
            near(child->bounds().width, full.width, "Body content retains its full width throughout entry");
        }
        const auto clip = expander->content_bounds(), surface = expander->content_surface_bounds();
        if (clip.height > 0)
            require(clip.y + clip.height <= surface.y + surface.height + 0.01f,
                "The content viewport cannot extend beyond the animated body");
        require(expander->content() == child && expander->retained_children().size() == 1,
            "Animation preserves the single owned child and hierarchy");
    }
    require(!expander->animating() && expander->progress() == 1, "Entry completes exactly");
    const auto idle = invalidations;
    expander->advance(start + std::chrono::seconds(2));
    expander->set_expanded(true);
    expander->settle();
    require(invalidations == idle, "A completed expander has no idle invalidation");
    expander->set_expanded(false);
    expander->advance(Animation::Clock::now() + std::chrono::milliseconds(250));
    const auto exiting = expander->progress();
    require(!expander->expanded() && expander->body_presented() && exiting > 0 && exiting < 1,
        "Closing remains presented after logical collapse");
    auto desired = expander->measure({500, 500});
    expander->arrange({10, 20, desired.width, desired.height});
    near(child->bounds().height, full.height, "Outgoing content does not shrink");
    expander->set_expanded(true);
    near(expander->progress(), exiting, "Reversal starts from the displayed progress");
    require(expander->animating(), "Reversal remains scheduled");
    bool rejected{};
    try { expander->set_duration(10001); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && expander->duration() == 1000 && expander->animating(), "Invalid duration preserves active presentation");
    expander->set_duration(0);
    require(!expander->animating() && expander->progress() == 1, "Disabling motion settles to logical expansion");
    near(expander->measure({500, 500}).height, open.height, "Disabling motion restores legacy measurement");
    expander->set_expanded(false);
    expander->invoke();
    require(callbacks == 1 && expander->expanded(), "The existing Expander header owns the semantic action");
}
void reserved_space_and_siblings() {
    auto child = std::make_shared<Element>();
    child->set_preferred_size({120, 80});
    auto expander = std::make_shared<Expander>(L"Body", child);
    expander->set_visual_style(VisualStyle::winui);
    expander->set_duration(1000);
    expander->set_expanded(false);
    expander->settle();
    auto sibling = std::make_shared<Element>();
    Stack root(Axis::vertical);
    root.add(expander);
    root.add(sibling, 1);
    root.arrange({0, 0, 400, 500});
    const auto before = sibling->bounds().height;
    expander->set_expanded(true);
    root.arrange(root.bounds());
    near(sibling->bounds().height, before, "Neighbors do not snap when opening begins");
    expander->advance(Animation::Clock::now() + std::chrono::milliseconds(250));
    root.arrange(root.bounds());
    near(expander->bounds().height + sibling->bounds().height, 500, "The sibling and body preserve their combined extent");
    near(sibling->bounds().y, expander->bounds().height, "The sibling follows the animated shared edge");
    near(child->bounds().height, 80, "Sibling reflow does not shrink body content");
    expander->set_fixed_size({400, 240});
    expander->arrange({0, 0, 400, 240});
    require(expander->content_surface_bounds().height < 240 - expander->effective_header_height(),
        "Explicit host sizing reserves space without bypassing the body clip");
    expander->set_expanded(false);
    expander->settle();
    near(expander->measure({500, 500}).height, 240, "A fixed host retains its reserved extent when closed");
}
void rounded_completion() {
    auto child = std::make_shared<Element>();
    child->set_preferred_size({100, 80});
    Expander expander(L"Rounded", child);
    expander.set_duration(10000);
    unsigned layouts{};
    expander.set_invalidator([&](Invalidation kind) {
        require(kind == Invalidation::layout, "Terminal expander changes invalidate layout");
        ++layouts;
    });
    for (bool expanded : {false, true}) {
        expander.set_expanded(expanded);
        const auto start = Animation::Clock::now();
        expander.advance(start + std::chrono::milliseconds(9999));
        require(expander.animating() && expander.progress() == (expanded ? 1 : 0), "Body easing can round to its endpoint early");
        const auto before = layouts;
        expander.advance(start + std::chrono::milliseconds(10001));
        require(!expander.animating() && layouts == before + 1, "Rounded body completion still invalidates final layout");
        const auto size = expander.measure({500, 500});
        expander.arrange({0, 0, size.width, size.height});
        require(expanded || (child->bounds().height == 0 && !expander.body_presented()),
            "Completed collapse releases body geometry and presentation");
    }
}
void nested_and_resized() {
    auto child = std::make_shared<Element>();
    child->set_preferred_size({120, 80});
    auto inner = std::make_shared<Expander>(L"Inner", child);
    inner->set_visual_style(VisualStyle::winui);
    inner->set_expanded(false);
    inner->set_duration(1000);
    Expander outer(L"Outer", inner);
    outer.set_visual_style(VisualStyle::winui);
    outer.set_expanded(false);
    outer.set_duration(1000);
    outer.set_expanded(true);
    inner->set_expanded(true);
    const auto now = Animation::Clock::now() + std::chrono::milliseconds(250);
    outer.advance(now); inner->advance(now);
    const auto size = outer.measure({500, 500});
    for (int i = 0; i < 4; ++i) {
        near(outer.measure({size.width, size.height}).height, size.height, "Nested measurement applies each transition once");
        outer.arrange({0, 0, size.width, size.height});
        near(child->bounds().height, 80, "Nested clips retain full leaf content height");
    }
    const auto progress = outer.progress();
    outer.arrange({0, 0, 420, size.height});
    near(outer.progress(), progress, "A viewport resize retains current progress");
    require(outer.animating(), "A viewport resize does not restart or cancel the body transition");
    near(child->bounds().height, 80, "Resizing the body width keeps its natural height");

    auto unbounded = std::make_shared<Element>();
    unbounded->set_preferred_size({100, (std::numeric_limits<float>::max)()});
    Expander invalid(L"Unbounded", unbounded);
    invalid.set_visual_style(VisualStyle::winui);
    invalid.set_duration(180);
    bool rejected{};
    try { invalid.measure({500, 500}); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Animated natural-height content must be bounded instead of producing invalid native geometry");
}
}
int main() {
    try {
        static_assert(std::is_base_of_v<Animation, Expander>);
        scalar_contract();
        for (unsigned variant : {0u, 1u, 2u})
            for (bool explicit_size : {false, true}) animated_layout(variant, explicit_size);
        reserved_space_and_siblings();
        rounded_completion();
        nested_and_resized();
        std::cout << "Expander animation and scalar transition contracts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
