#include "xui/controls.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(float actual, float expected, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 0.01f, message);
}
void contracts() {
    auto first = std::make_shared<TextInput>(L"First");
    auto second = std::make_shared<TextInput>(L"Second");
    SplitView split(first, second);
    split.arrange({10, 20, 1000, 400});
    require(split.transition_duration() == 0 && !split.animating() && split.expanded(), "Split motion is opt-in");
    near(split.progress(), 1, "Default split starts fully expanded");
    split.set_secondary_visible(false);
    split.arrange(split.bounds());
    near(first->bounds().width, 1000, "Default closure remains immediate");
    near(second->bounds().width, 0, "Default closure hides the second pane");
    split.set_transition_duration(1000);
    unsigned layouts{};
    split.set_invalidator([&](Invalidation kind) {
        require(kind == Invalidation::layout, "Pane motion coordinates root layout");
        ++layouts;
    });
    split.set_secondary_visible(true);
    split.arrange(split.bounds());
    require(split.expanded() && split.animating(), "Logical expansion precedes presentation");
    near(first->bounds().width, 1000, "The first pane does not snap before entry");
    near(second->bounds().width, 495, "The incoming pane has full width at progress zero");
    near(second->bounds().x, 1010, "The incoming pane begins beyond the right clip");
    const auto now = Animation::Clock::now();
    for (int time : {50, 100, 250, 500, 900, 1000}) {
        split.advance(now + std::chrono::milliseconds(time));
        split.arrange(split.bounds());
        const auto progress = split.progress();
        near(first->bounds().width, 1000 - 505 * progress, "The first pane follows the shared progress");
        near(split.divider().width, 10 * progress, "The divider enters with the secondary pane");
        near(second->bounds().x, 1010 - 495 * progress, "The second pane slides in from the right");
        near(second->bounds().width, 495, "The secondary content does not reflow on each animation frame");
        near(first->bounds().x + first->bounds().width + split.divider().width, second->bounds().x,
            "The panes and divider share moving edges");
    }
    require(!split.animating() && split.progress() == 1 && layouts == 7, "Sampled entry reaches its exact endpoint");
    split.set_secondary_visible(false);
    require(!split.expanded() && split.animating(), "Logical closure precedes visual exit");
    split.advance(Animation::Clock::now() + std::chrono::milliseconds(250));
    split.arrange(split.bounds());
    const auto closing = split.progress();
    require(closing > 0 && closing < 1 && second->bounds().width == 495, "Exit retains full-width content");
    split.set_secondary_visible(true);
    near(split.progress(), closing, "Reopening retargets the current presentation");
    split.set_ratio(0.7f);
    require(!split.animating() && split.progress() == 1, "A ratio change settles an interrupted transition");
    split.arrange(split.bounds());
    split.set_secondary_visible(false);
    split.set_transition_duration(0);
    require(!split.animating() && split.progress() == 0, "Disabling motion settles immediately");
    split.set_transition_duration(200);
    split.set_secondary_visible(true);
    split.arrange({0, 0, 500, 400});
    require(!split.animating() && !split.expanded(), "A narrow breakpoint settles active motion");
    near(second->bounds().width, 0, "A narrow window hides the second pane");
    split.arrange({0, 0, 1000, 400});
    require(split.expanded() && !split.animating(), "Widening restores the logical pane without a stale transition");
    bool rejected{};
    try { split.set_transition_duration(10001); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && split.transition_duration() == 200, "Invalid duration preserves the previous configuration");
    split.set_transition_duration(10000);
    split.set_secondary_visible(false);
    const auto rounded_start = Animation::Clock::now();
    split.advance(rounded_start + std::chrono::milliseconds(9999));
    require(split.animating() && split.progress() == 0, "Pane easing can round closed before the clock completes");
    const auto before_completion = layouts;
    split.advance(rounded_start + std::chrono::milliseconds(10001));
    require(!split.animating() && layouts == before_completion + 1,
        "Rounded pane completion still requests terminal layout");
    split.arrange(split.bounds());
    require(second->bounds().width == 0, "Rounded completion removes the outgoing pane bounds");
    const auto completed_layouts = layouts;
    split.advance(rounded_start + std::chrono::milliseconds(20000));
    require(layouts == completed_layouts, "Completed rounded pane motion has no idle work");
}
void ratio_geometry(SplitView& split) {
    const auto area = split.pane_area();
    const auto first = split.first()->bounds(), second = split.second()->bounds(), divider = split.divider();
    near(first.x, area.x, "Ratio motion retains the first pane origin");
    near(first.x + first.width, divider.x, "The presented first pane touches the divider");
    near(divider.x + divider.width, second.x, "The presented divider touches the second pane");
    near(second.x + second.width, area.x + area.width, "Ratio motion retains the trailing pane edge");
    near(divider.width, split.effective_divider_width(), "Ratio motion does not shrink the divider");
    require(first.width >= SplitView::minimum_pane_width && second.width >= SplitView::minimum_pane_width,
        "Ratio motion preserves both minimum pane widths");
    require(split.progress() == 1 && split.expanded(), "Ratio presets do not change visibility progress");
}
void ratio_contracts() {
    auto first = std::make_shared<TextInput>(L"First");
    auto second = std::make_shared<TextInput>(L"Second");
    SplitView split(first, second);
    split.set_transition_duration(1000);
    split.set_ratio(0.6f);
    require(!split.animating(), "Presets before initial arrangement remain immediate");
    split.arrange({10, 20, 1000, 400});
    near(split.first()->bounds().width, 594, "Initial ratio is applied when first arranged");
    split.set_transition_duration(0);
    split.set_ratio(0.5f);
    split.arrange(split.bounds());
    require(!split.animating(), "Zero-duration presets remain immediate");
    near(split.first()->bounds().width, 495, "Zero duration applies the new geometry directly");
    split.set_transition_duration(1000);
    unsigned layouts{}, expansions{};
    split.on_expanded([&](bool) { ++expansions; });
    split.set_invalidator([&](Invalidation kind) {
        require(kind == Invalidation::layout, "Ratio motion uses coordinated layout invalidation");
        ++layouts;
    });
    split.set_ratio(0.65f);
    require(split.animating() && split.ratio() == 0.65f, "Logical ratio changes before the presented geometry");
    split.arrange(split.bounds());
    near(split.first()->bounds().width, 495, "A preset does not snap the pane before its first frame");
    ratio_geometry(split);
    const auto start = Animation::Clock::now();
    float previous = split.first()->bounds().width;
    for (int time : {50, 100, 250, 500}) {
        split.advance(start + std::chrono::milliseconds(time));
        split.arrange(split.bounds());
        const auto width = split.first()->bounds().width;
        require(width > previous && width < 643.5f && split.animating(), "Ratio frames approach the target monotonically");
        previous = width;
        ratio_geometry(split);
    }
    split.advance(start);
    split.arrange(split.bounds());
    near(split.first()->bounds().width, previous, "An older clock sample cannot reverse ratio motion");
    const auto unchanged_layouts = layouts;
    split.set_ratio(0.65f);
    require(layouts == unchanged_layouts && split.animating(), "Assigning the same preset does not restart or invalidate");
    split.set_ratio(0.4f);
    require(split.ratio() == 0.4f && split.animating(), "A second preset retargets immediately");
    split.arrange(split.bounds());
    near(split.first()->bounds().width, previous, "Retargeting starts from the last presented geometry");
    const auto retarget = Animation::Clock::now();
    split.advance(retarget + std::chrono::milliseconds(250));
    split.arrange(split.bounds());
    require(split.first()->bounds().width < previous && split.first()->bounds().width > 396,
        "A reversed preset moves back toward its new target");
    ratio_geometry(split);
    split.advance(retarget + std::chrono::milliseconds(999));
    require(split.animating(), "Each retarget keeps the full configured duration");
    split.advance(retarget + std::chrono::milliseconds(1001));
    split.arrange(split.bounds());
    near(split.first()->bounds().width, 396, "Retargeted motion reaches the exact new width");
    require(!split.animating() && split.first()->content() == first && split.second()->content() == second,
        "Completion preserves retained pane content");
    ratio_geometry(split);
    const auto completed_layouts = layouts;
    split.advance(retarget + std::chrono::seconds(2));
    split.settle();
    split.set_ratio(0.4f);
    require(layouts == completed_layouts, "Completed ratio motion requests no idle work");
    split.set_ratio(0.8f);
    split.advance(Animation::Clock::now() + std::chrono::milliseconds(500));
    split.arrange(split.bounds());
    require(split.first()->bounds().width > 396 && split.first()->bounds().width < 690,
        "Ratio motion interpolates to the constrained pane endpoint without an early plateau");
    split.settle();
    split.arrange(split.bounds());
    near(split.first()->bounds().width, 690, "Preset endpoints respect the second pane minimum");
    split.set_ratio(0.9f);
    require(!split.animating() && split.ratio() == 0.9f, "Equivalent constrained presets need no animation");
    split.set_ratio(0.1f);
    split.settle();
    split.arrange(split.bounds());
    near(split.first()->bounds().width, 300, "Preset endpoints respect the first pane minimum");
    require(expansions == 0, "Ratio presets do not emit logical expansion changes");
}
void ratio_interruptions() {
    SplitView split(std::make_shared<Label>(L"First"), std::make_shared<Label>(L"Second"));
    split.arrange({0, 0, 1000, 400});
    split.set_transition_duration(1000);
    split.set_ratio(0.65f);
    split.advance(Animation::Clock::now() + std::chrono::milliseconds(250));
    split.arrange(split.bounds());
    const auto grabbed = split.divider().x;
    split.set_style_dragging(true);
    split.arrange(split.bounds());
    require(!split.animating(), "Pointer capture cancels preset motion");
    near(split.divider().x, grabbed, "Grabbing an animated divider must not jump to the preset target");
    near(split.ratio(), grabbed / 990, "Pointer capture adopts the currently presented ratio");
    split.set_ratio(0.4f);
    split.arrange(split.bounds());
    require(!split.animating() && split.ratio() == 0.4f, "Pointer ratio updates remain direct with duration enabled");
    near(split.divider().x, 396, "Pointer movement places the divider directly");
    split.set_style_dragging(false);
    require(!split.animating(), "Pointer release does not resume the cancelled preset");
    split.set_ratio(0.65f);
    require(split.animating(), "A later programmatic preset can animate again");
    split.arrange({20, 30, 1000, 400});
    require(split.animating(), "Translation with an unchanged viewport preserves ratio motion");
    split.arrange({20, 30, 1200, 400});
    require(!split.animating() && split.ratio() == 0.65f, "Resizing settles to the logical ratio in the new viewport");
    ratio_geometry(split);
    near(split.first()->bounds().width, 773.5f, "Resized panes use the logical preset rather than stale pixel targets");
    split.set_ratio(0.4f);
    split.set_transition_duration(200);
    split.arrange(split.bounds());
    require(!split.animating() && split.ratio() == 0.4f, "Changing duration settles the active preset");
    split.set_ratio(0.6f);
    const auto before_invalid = split.divider().x;
    bool rejected{};
    try { split.set_transition_duration(10001); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && split.transition_duration() == 200 && split.animating(), "Invalid duration preserves ratio motion");
    split.set_ratio(std::numeric_limits<float>::quiet_NaN());
    split.set_ratio(std::numeric_limits<float>::infinity());
    require(split.ratio() == 0.6f && split.animating(), "Nonfinite ratios preserve the existing setter behavior");
    near(split.divider().x, before_invalid, "Invalid inputs must not alter presented ratio geometry");
    PartStyleValues divider;
    divider.width = 20.0f;
    split.set_control_style_values(StylePart::divider, divider);
    split.arrange(split.bounds());
    require(!split.animating(), "Changing divider metrics settles incompatible ratio motion");
    ratio_geometry(split);
    split.set_ratio(0.5f);
    split.arrange({0, 0, 500, 400});
    require(!split.animating() && !split.expanded(), "Crossing the narrow breakpoint settles preset motion");
    split.set_ratio(0.6f);
    require(!split.animating(), "A narrow split cannot animate a ratio preset");
    split.arrange({0, 0, 1000, 400});
    require(!split.animating() && split.expanded(), "Widening uses the stored ratio without a stale transition");
    ratio_geometry(split);
    split.arrange({0, 0, 1000, 0});
    split.set_ratio(0.5f);
    require(!split.animating(), "A zero-height secondary pane cannot animate a ratio preset");
}
void ratio_visibility() {
    SplitView split(std::make_shared<Label>(L"First"), std::make_shared<Label>(L"Second"));
    split.arrange({0, 0, 1000, 400});
    split.set_transition_duration(1000);
    split.set_ratio(0.6f);
    split.advance(Animation::Clock::now() + std::chrono::milliseconds(250));
    split.set_secondary_visible(false);
    split.arrange(split.bounds());
    require(!split.expanded() && split.animating() && split.ratio() == 0.6f,
        "Hiding during a preset settles the ratio before starting visibility motion");
    near(split.second()->bounds().width, 396, "Exit retains the full logical preset pane width");
    split.advance(Animation::Clock::now() + std::chrono::milliseconds(250));
    split.arrange(split.bounds());
    near(split.second()->bounds().width, 396, "The outgoing pane does not shrink during exit");
    split.set_ratio(0.4f);
    split.arrange(split.bounds());
    require(!split.animating() && split.progress() == 0 && split.second()->bounds().width == 0,
        "Changing ratio during exit settles visibility and applies the preset immediately");
    split.set_ratio(0.6f);
    require(!split.animating(), "A logically hidden secondary pane cannot animate a ratio preset");
    split.set_secondary_visible(true);
    split.arrange(split.bounds());
    require(split.animating() && split.progress() == 0, "Entry still starts at zero visibility progress");
    near(split.first()->bounds().width, 1000, "Ratio configuration does not snap the first pane before entry");
    near(split.second()->bounds().width, 396, "Entry retains full target width from its first frame");
    split.advance(Animation::Clock::now() + std::chrono::milliseconds(250));
    split.arrange(split.bounds());
    near(split.second()->bounds().width, 396, "Entry does not resize secondary content");
    split.set_ratio(0.5f);
    split.arrange(split.bounds());
    require(!split.animating() && split.progress() == 1, "Changing ratio during entry settles visibility rather than mixing transitions");
    ratio_geometry(split);
    split.set_ratio(0.6f);
    require(split.animating(), "A preset after completed entry animates normally");
    split.set_secondary_visible(true);
    require(split.animating(), "Repeated visibility assignments do not interrupt presets");
    split.set_transition_duration(0);
    split.arrange(split.bounds());
    require(!split.animating(), "Disabling motion settles a preset");
    near(split.first()->bounds().width, 594, "Disabling motion uses the logical ratio endpoint");
}
void ratio_rounded_completion() {
    SplitView split(std::make_shared<Label>(L"First"), std::make_shared<Label>(L"Second"));
    split.arrange({0, 0, 1000, 400});
    split.set_transition_duration(10000);
    unsigned layouts{};
    split.set_invalidator([&](Invalidation kind) {
        require(kind == Invalidation::layout, "Ratio completion invalidates layout");
        ++layouts;
    });
    for (const auto target : {0.6f, 0.4f}) {
        split.set_ratio(target);
        const auto start = Animation::Clock::now();
        split.advance(start + std::chrono::milliseconds(9999));
        split.arrange(split.bounds());
        const auto width = split.first()->bounds().width;
        require(split.animating(), "Rounded ratio geometry must not complete the clock early");
        near(width, 990 * target, "Cubic ratio easing can round to its endpoint before completion");
        const auto before_completion = layouts;
        split.advance(start + std::chrono::milliseconds(10001));
        split.arrange(split.bounds());
        require(!split.animating() && layouts == before_completion + 1,
            "Rounded ratio completion still publishes terminal layout");
        require(split.first()->bounds().width == width, "Terminal layout runs even when ratio geometry already equals its endpoint");
        ratio_geometry(split);
        const auto completed = layouts;
        split.advance(start + std::chrono::milliseconds(20000));
        require(layouts == completed, "Rounded ratio completion leaves no idle work");
    }
}
void primary_visibility() {
    SplitView split(std::make_shared<Label>(L"First"), std::make_shared<Label>(L"Second"));
    split.arrange({10, 20, 1000, 400});
    split.set_transition_duration(1000);
    split.set_ratio(0.6f);
    require(split.animating(), "The two-pane ratio starts animating");
    split.set_primary_visible(false);
    split.arrange(split.bounds());
    require(!split.animating() && split.expanded(), "Tear-out topology settles ratio motion");
    near(split.first()->bounds().width, 0, "A hidden primary pane has no width");
    near(split.second()->bounds().width, 1000, "The remaining secondary pane occupies the full width");
    near(split.second()->bounds().x, 10, "The remaining pane starts at the container origin");
    near(split.divider().width, 0, "One remaining pane has no divider");
    split.arrange({10, 20, 400, 300});
    require(split.expanded(), "Secondary-only content remains visible below the two-pane breakpoint");
    near(split.second()->bounds().width, 400, "Narrow secondary-only content keeps the full width");
    split.set_ratio(0.4f);
    require(!split.animating(), "A single pane cannot animate a ratio preset");
    split.set_secondary_visible(false);
    split.arrange(split.bounds());
    require(!split.animating() && !split.expanded() && split.second()->bounds().width == 0,
        "Hiding both panes is immediate");
    split.set_secondary_visible(true);
    split.arrange(split.bounds());
    require(!split.animating() && split.second()->bounds().width == 400, "Restoring the sole pane is immediate");
    split.set_primary_visible(true);
    split.arrange({10, 20, 1000, 400});
    ratio_geometry(split);
    split.set_secondary_visible(false);
    require(split.animating(), "Normal two-pane exit still animates");
    split.set_primary_visible(false);
    split.arrange(split.bounds());
    require(!split.animating() && split.progress() == 0 && split.first()->bounds().width == 0 &&
        split.second()->bounds().width == 0, "Primary retirement settles an active secondary exit");
}
void axis_animation_contracts() {
    for (const auto axis : {Axis::horizontal, Axis::vertical}) {
        SplitView split(std::make_shared<Label>(L"First"), std::make_shared<Label>(L"Second"));
        split.set_layout(axis, 48);
        const bool horizontal = axis == Axis::horizontal;
        const auto extent = [=](Rect r) { return horizontal ? r.width : r.height; };
        const auto origin = [=](Rect r) { return horizontal ? r.x : r.y; };
        split.arrange(horizontal ? Rect{10, 20, 400, 200} : Rect{10, 20, 200, 400});
        split.set_secondary_visible(false);
        split.arrange(split.bounds());
        split.set_transition_duration(1000);
        split.set_secondary_visible(true);
        split.arrange(split.bounds());
        near(extent(split.first_pane_area()), 400, "Both axes start entry with a full first pane");
        near(extent(split.second_pane_area()), 195, "Both axes retain full incoming content extent");
        near(origin(split.second_pane_area()), origin(split.pane_area()) + 400, "Entry starts beyond the trailing clip");
        const auto started = Animation::Clock::now();
        for (const int time : {100, 500, 1001}) {
            split.advance(started + std::chrono::milliseconds(time));
            split.arrange(split.bounds());
            const auto first = split.first_pane_area(), second = split.second_pane_area(), divider = split.divider();
            near(extent(first), 400 - 205 * split.progress(), "First pane follows visibility progress on either axis");
            near(extent(second), 195, "Secondary extent remains fixed through entry");
            near(origin(first) + extent(first), origin(divider), "Styled first pane touches the moving divider");
            near(origin(divider) + extent(divider), origin(second), "Styled second pane touches the moving divider");
            near(extent(split.first()->bounds()), extent(first), "First content and surface share geometry");
            near(extent(split.second()->bounds()), extent(second), "Second content and surface share geometry");
        }
        unsigned changes{};
        split.on_ratio_changed([&](float ratio) {
            require(ratio == split.ratio(), "Callbacks observe the committed logical ratio");
            ++changes;
        });
        split.set_ratio(0.9f);
        split.advance(Animation::Clock::now() + std::chrono::milliseconds(500));
        split.arrange(split.bounds());
        require(split.animating() && extent(split.first_pane_area()) > 195 &&
            extent(split.first_pane_area()) < 342, "Both axes animate toward the configured minimum");
        const auto grabbed = extent(split.first_pane_area());
        split.set_style_dragging(true);
        split.arrange(split.bounds());
        near(extent(split.first_pane_area()), grabbed, "Drag takeover preserves the presented position on both axes");
        near(split.ratio(), grabbed / 390, "Drag takeover uses the selected axis extent");
        require(changes == 2, "Drag takeover reports its adopted ratio even without pointer movement");
        split.set_ratio(0.1f);
        split.arrange(split.bounds());
        near(extent(split.first_pane_area()), 48, "Direct drag honors the custom minimum");
        require(changes == 3 && !split.animating(), "Logical ratio notifications do not fire on animation frames");
        split.set_style_dragging(false);
        split.set_ratio(0.6f);
        require(split.animating(), "A later ratio preset animates");
        split.set_layout(horizontal ? Axis::vertical : Axis::horizontal, 40);
        split.arrange(split.bounds());
        require(!split.animating() && !split.divider_dragging(), "Changing axes settles stale pixel targets and capture");
        split.set_secondary_visible(false);
        split.advance(Animation::Clock::now() + std::chrono::milliseconds(250));
        split.arrange(split.bounds());
        require(split.animating() && split.second_pane_area().width > 0 && split.second_pane_area().height > 0,
            "Closing retains the outgoing styled surface before logical retirement");
        split.set_primary_visible(false);
        split.arrange(split.bounds());
        require(!split.animating() && split.divider().width == 0 && split.second_pane_area().width == 0,
            "Retiring both panes removes the divider and outgoing content");
    }
}
}
int main() {
    try {
        contracts(); ratio_contracts(); ratio_interruptions(); ratio_visibility(); ratio_rounded_completion(); primary_visibility();
        axis_animation_contracts();
        std::cout << "Split visibility and ratio animation contracts passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
