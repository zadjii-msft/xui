#include "xui/foundation.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace xui;
using namespace std::chrono_literals;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid progress input must throw.");
}
void defaults_and_frames(VisualStyle style) {
    Progress progress;
    progress.set_visual_style(style);
    progress.arrange({10, 20, 320, 42});
    const auto measured = progress.measure({500, 500});
    require(progress.duration() == 0 && !progress.animating(), "Progress motion is opt-in");
    progress.set_value(20);
    require(progress.presented_value() == 20 && progress.presented_fraction() == 0.2,
        "Default presentation keeps the original value and fraction");
    unsigned paints{}, other{};
    progress.set_invalidator([&](Invalidation kind) { if (kind == Invalidation::paint) ++paints; else ++other; });
    progress.set_duration(10000);
    require(paints == 0 && other == 0, "Enabling motion does not start idle work");
    const auto before = Animation::Clock::now();
    progress.set_value(80);
    require(progress.value() == 80 && progress.presented_value() == 20 && progress.animating(),
        "Logical value changes immediately while presentation starts at the displayed value");
    progress.advance(before);
    require(progress.presented_value() == 20, "Old clock samples do not start a frame");
    const auto started = Animation::Clock::now();
    double previous = progress.presented_value();
    for (int milliseconds : {100, 1000, 2500, 5000, 9000}) {
        progress.advance(started + std::chrono::milliseconds(milliseconds));
        require(progress.value() == 80, "Animation never changes the logical or accessibility value");
        require(progress.presented_value() > previous && progress.presented_value() < 80,
            "Presentation interpolates monotonically without overshooting");
        previous = progress.presented_value();
        require(progress.bounds().x == 10 && progress.bounds().y == 20 && progress.bounds().width == 320 &&
            progress.bounds().height == 42 && progress.measure({500, 500}).height == measured.height,
            "Progress animation leaves retained geometry and measurement unchanged");
    }
    const auto sampled_paints = paints;
    progress.advance(before);
    require(progress.presented_value() == previous && paints == sampled_paints, "Stale samples do not reverse or invalidate a frame");
    progress.advance(started + 10001ms);
    require(progress.presented_value() == 80 && !progress.animating() && other == 0 && paints >= 7,
        "Completion is exact and every frame is paint-only");
    const auto idle = paints;
    progress.advance(started + 20s); progress.settle(); progress.set_value(80); progress.set_duration(10000);
    require(paints == idle && other == 0, "Completed or unchanged progress performs no idle work");
    progress.set_value(30);
    progress.advance(Animation::Clock::now() + 2500ms);
    const auto displayed = progress.presented_value();
    require(displayed > 30 && displayed < 80, "Decreasing values also interpolate");
    progress.set_value(90);
    require(progress.presented_value() == displayed && progress.value() == 90 && progress.animating(),
        "Retargeting starts at the last displayed value, not an unpresented clock sample");
    progress.advance(Animation::Clock::now() + 2500ms);
    require(progress.presented_value() > displayed && progress.presented_value() < 90, "Reversal uses the new target");
    progress.set_duration(180);
    require(progress.presented_value() == 90 && !progress.animating(), "Changing duration settles rather than stretching an active clock");
    progress.set_value(40);
    progress.set_duration(0);
    require(progress.presented_value() == 40 && !progress.animating(), "Disabling motion immediately applies the target");
}
void invalid_and_interruptions() {
    Progress progress;
    progress.set_duration(10000);
    progress.set_value(80);
    progress.advance(Animation::Clock::now() + 2500ms);
    const auto displayed = progress.presented_value();
    unsigned invalidations{};
    progress.set_invalidator([&](Invalidation) { ++invalidations; });
    for (double value : {-1., 101., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { progress.set_value(value); });
    rejects([&] { progress.set_duration(10001); });
    rejects([&] { progress.set_duration((std::numeric_limits<unsigned>::max)()); });
    rejects([&] { progress.set_range(50, 50); });
    rejects([&] { progress.set_range(1, 0); });
    rejects([&] { progress.set_range(0, std::numeric_limits<double>::infinity()); });
    rejects([&] { progress.set_state(static_cast<ProgressState>(99)); });
    rejects([&] { progress.set_capacity(101, 100); });
    rejects([&] { progress.set_capacity(0, 0); });
    require(progress.value() == 80 && progress.presented_value() == displayed && progress.duration() == 10000 &&
        progress.animating() && invalidations == 0, "Rejected input preserves logical value, presentation, clock, and invalidation state");
    progress.set_range(0, 100); progress.set_state(ProgressState::determinate); progress.set_value(80);
    require(progress.animating() && progress.presented_value() == displayed && invalidations == 0,
        "Unchanged range, mode, or target does not restart or settle motion");
    progress.set_range(0, 50);
    require(progress.value() == 50 && progress.presented_value() == 50 && !progress.animating(),
        "Range changes clamp the logical value and settle presentation");
    for (auto state : {ProgressState::indeterminate, ProgressState::unknown, ProgressState::paused, ProgressState::error}) {
        progress.set_state(ProgressState::determinate);
        progress.set_value(20);
        require(progress.animating(), "Determinate mode permits interpolation");
        progress.set_state(state);
        require(progress.presented_value() == 20 && !progress.animating(), "Mode changes settle immediately");
        progress.set_value(40);
        require(progress.presented_value() == 40 && !progress.animating(), "Non-determinate modes remain static");
    }
    progress.set_state(ProgressState::determinate);
    progress.set_value(30);
    progress.set_capacity(25, 100, L"bytes");
    require(!progress.animating() && progress.presented_value() == 25 && progress.value_text() == L"25 / 100 bytes",
        "Capacity retains its immediate read-only display contract");
    progress.set_value(75);
    progress.settle();
    require(!progress.animating() && progress.presented_value() == 75 && progress.value_text().empty(),
        "The shared visibility and reduced-motion policy can settle the exact logical value");
}
void precision_and_completion() {
    Progress progress;
    for (const auto range : {NumericRange{-100, 100}, NumericRange{1e100, 2e100}, NumericRange{1e-100, 2e-100}}) {
        progress.set_range(range.minimum, range.maximum);
        progress.set_value(range.minimum);
        progress.settle();
        progress.set_duration(10000);
        const auto target = std::nextafter(range.maximum, range.minimum);
        progress.set_value(target);
        progress.advance(Animation::Clock::now() + 2500ms);
        require(std::isfinite(progress.presented_value()) && progress.presented_value() > range.minimum &&
            progress.presented_value() < target, "Double endpoints retain finite precision outside the float value range");
        progress.settle();
        require(progress.value() == target && progress.presented_value() == target,
            "An exact double target survives scalar easing");
        require(static_cast<float>(progress.presented_fraction()) < 1 &&
            static_cast<int>(progress.presented_fraction() * 100) < 100,
            "A logically incomplete value cannot round to a complete float fill or percentage");
        progress.set_value(range.maximum);
        progress.settle();
        require(progress.presented_fraction() == 1, "Actual completion can present a full fill and 100 percent");
        progress.set_value(range.minimum);
        require(progress.value() == range.minimum && progress.presented_value() < range.maximum &&
            static_cast<float>(progress.presented_fraction()) < 1,
            "Leaving a completed target immediately removes false completion even before the first tick");
        progress.settle();
        require(progress.presented_value() == range.minimum && progress.presented_fraction() == 0,
            "A decreasing transition reaches the exact minimum");
    }
    progress.set_range(0, 100);
    progress.set_value(0);
    progress.settle();
    progress.set_duration(10000);
    for (double target : {100., 0.}) {
        const auto before = Animation::Clock::now();
        progress.set_value(target);
        const auto after = Animation::Clock::now();
        progress.advance(before + 9999ms);
        require(progress.animating() && progress.presented_value() == target, "Easing may round to the target before the clock completes");
        unsigned paints{};
        progress.set_invalidator([&](Invalidation kind) {
            require(kind == Invalidation::paint, "Rounded completion is paint-only");
            ++paints;
        });
        progress.advance(after + 10001ms);
        require(!progress.animating() && paints == 1, "An unchanged rounded endpoint still publishes the terminal frame");
        progress.settle(); progress.advance(after + 20s);
        require(paints == 1, "Rounded completion leaves no idle work");
        progress.set_invalidator({});
    }
}
void ring_frames() {
    ProgressRing ring;
    require(ring.ring_presentation() && ring.duration() == 0, "Ring presentation inherits opt-in value motion");
    ring.set_state(ProgressState::determinate);
    ring.set_duration(1000);
    ring.set_value(80);
    require(ring.value() == 80 && ring.presented_value() == 0 && ring.animating(),
        "The ring retains immediate logical progress and an independent displayed value");
    ring.advance(Animation::Clock::now() + 500ms);
    require(ring.presented_fraction() > 0 && ring.presented_fraction() < 0.8,
        "Determinate rings use intermediate presentation fractions");
    ring.set_state(ProgressState::indeterminate);
    require(!ring.animating() && ring.presented_value() == 80,
        "Indeterminate ring visuals do not keep the value-interpolation clock active");
}
}
int main() {
    static_assert(std::is_base_of_v<Animation, Progress>);
    try {
        defaults_and_frames(VisualStyle::classic);
        defaults_and_frames(VisualStyle::winui);
        invalid_and_interruptions();
        precision_and_completion();
        ring_frames();
        std::cout << "Progress animation model contracts passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
