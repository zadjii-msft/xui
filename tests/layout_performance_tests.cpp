#include "xui/core.hpp"
#include "xui/control_styling.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
std::atomic<std::size_t> allocations{};
}
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void close(float actual, float expected, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 0.001f, message);
}

class Probe : public xui::Element {
public:
    std::size_t measurements{};
    bool fail{};
    std::function<void()> measuring;
    std::function<void()> arranging;
    xui::Size measure(xui::Size available) override {
        ++measurements;
        if (fail) throw std::runtime_error("Injected measurement failure");
        if (measuring) measuring();
        return Element::measure(available);
    }
    void arrange(xui::Rect bounds) override {
        if (arranging) arranging();
        Element::arrange(bounds);
    }
};

void repeated_layout(bool styled) {
    xui::Stack root(xui::Axis::vertical);
    root.set_spacing(2);
    root.set_padding({3, 4, 5, 6});
    xui::PartStyleValues style;
    style.horizontal_alignment = xui::StyleAlignment::center;
    style.vertical_alignment = xui::StyleAlignment::end;
    if (styled) root.set_control_style_values(xui::StylePart::root, style);
    std::vector<std::shared_ptr<Probe>> leaves;
    for (int row = 0; row < 8; ++row) {
        auto group = std::make_shared<xui::Stack>(xui::Axis::horizontal);
        group->set_spacing(1);
        if (styled) group->set_control_style_values(xui::StylePart::root, style);
        for (int column = 0; column < 8; ++column) {
            auto child = std::make_shared<Probe>();
            child->set_preferred_size({12, 16});
            group->add(child, column % 2 ? 1 : 0);
            leaves.push_back(std::move(child));
        }
        root.add(std::move(group), row % 2 ? 1 : 0);
    }
    const auto layout = [&](int frame) {
        const auto width = 640.0f + frame % 31;
        const auto height = 480.0f + frame % 17;
        const auto measured = root.measure({width, height});
        close(measured.width, width, "Nested flex must retain measured width");
        close(measured.height, height, "Nested flex must retain measured height");
        root.arrange({7, 9, width, height});
    };
    layout(0);
    const auto calls = leaves.front()->measurements;
    const auto before = allocations.load();
    const auto start = std::chrono::steady_clock::now();
    constexpr int frames = 10000;
    for (int frame = 0; frame < frames; ++frame) layout(frame);
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    const auto allocated = allocations.load() - before;
    require(allocated == 0, "Steady-state Stack layout must not allocate scratch buffers");
    require(leaves.front()->measurements == calls + frames * 3,
        "Scratch reuse must not skip child measurements");
    std::cout << "nested_stack styled=" << styled << " frames=" << frames << " allocations=" << allocated
        << " elapsed_ms=" << elapsed << '\n';
}

void scratch_lifetime() {
    xui::Stack root(xui::Axis::horizontal);
    auto first = std::make_shared<Probe>();
    auto second = std::make_shared<Probe>();
    first->set_preferred_size({20, 10});
    second->set_preferred_size({30, 10});
    root.add(first);
    root.add(second);
    root.arrange({0, 0, 100, 40});

    bool entered{};
    first->measuring = [&] {
        if (entered) return;
        entered = true;
        const auto nested = root.measure({5, 5});
        close(nested.width, 5, "Reentrant measurement uses its own constraints");
        entered = false;
    };
    root.arrange({0, 0, 100, 40});
    close(first->bounds().width, 20, "Reentrant measurement preserves outer first size");
    close(second->bounds().x, 20, "Reentrant measurement preserves outer second position");
    close(second->bounds().width, 30, "Reentrant measurement preserves outer second size");
    first->measuring = {};
    first->arranging = [&] { root.measure({5, 5}); };
    root.arrange({0, 0, 100, 40});
    close(second->bounds().x, 20, "Reentrant arrange cannot overwrite pending outer sizes");
    close(second->bounds().width, 30, "Reentrant arrange preserves outer second size");
    first->arranging = {};

    second->fail = true;
    bool threw{};
    try { root.measure({100, 40}); }
    catch (const std::runtime_error&) { threw = true; }
    require(threw, "Measurement exceptions must propagate");
    second->fail = false;
    const auto before = allocations.load();
    root.arrange({0, 0, 100, 40});
    const auto allocated = allocations.load() - before;
    require(allocated == 0, "Measurement failure must return scratch storage for reuse");
    std::cout << "after_measurement_failure allocations=" << allocated << '\n';
    close(second->bounds().width, 30, "Measurement failure cannot retain partial sizes");

    second->set_preferred_size({35, 10});
    auto third = std::make_shared<Probe>();
    third->set_preferred_size({10, 10});
    root.add(third);
    root.set_padding({2, 3, 4, 5});
    root.set_spacing(6);
    root.arrange({0, 0, 100, 40});
    close(third->bounds().x, 69, "Growth and sizing changes recompute child positions");
    close(third->bounds().width, 10, "New children receive their own scratch slot");

    xui::PartStyleValues style;
    style.horizontal_alignment = xui::StyleAlignment::center;
    style.vertical_alignment = xui::StyleAlignment::end;
    root.set_control_style_values(xui::StylePart::root, style);
    root.arrange({0, 0, 100, 40});
    close(first->bounds().x, 10.5f, "Styled main-axis alignment uses current sizes");
    close(first->bounds().y, 25, "Styled cross-axis alignment uses current sizes");
    const auto styled_before = allocations.load();
    root.arrange({1, 2, 100, 40});
    require(allocations.load() == styled_before, "Warmed styled Stack layout must not allocate");
    close(first->bounds().x, 11.5f, "Scratch reuse retains position changes");
    close(first->bounds().y, 27, "Scratch reuse retains cross-axis position changes");
}

void cross_alignment() {
    for (const auto axis : {xui::Axis::horizontal, xui::Axis::vertical}) {
        const bool horizontal = axis == xui::Axis::horizontal;
        for (const auto alignment : {xui::StyleAlignment::start, xui::StyleAlignment::center,
            xui::StyleAlignment::end, xui::StyleAlignment::stretch}) {
            xui::Stack root(axis);
            auto child = std::make_shared<xui::Element>();
            child->set_preferred_size({20, 20});
            root.add(child, 1);
            xui::PartStyleValues style;
            style.horizontal_alignment = horizontal ? xui::StyleAlignment::end : alignment;
            style.vertical_alignment = horizontal ? alignment : xui::StyleAlignment::end;
            root.set_control_style_values(xui::StylePart::root, style);
            root.arrange({3, 3, 100, 100});
            const auto bounds = child->bounds();
            close(horizontal ? bounds.x : bounds.y, 3, "Cross alignment must not shift the flex main axis");
            close(horizontal ? bounds.width : bounds.height, 100, "Cross alignment must preserve the flex allocation");
            const auto expected_origin = alignment == xui::StyleAlignment::center ? 43.0f :
                alignment == xui::StyleAlignment::end ? 83.0f : 3.0f;
            close(horizontal ? bounds.y : bounds.x, expected_origin, "Cross alignment must preserve the selected origin");
            close(horizontal ? bounds.height : bounds.width,
                alignment == xui::StyleAlignment::stretch ? 100.0f : 20.0f,
                "Cross alignment must preserve the selected extent");
        }
    }
}
}

int main() {
    try {
        repeated_layout(false);
        repeated_layout(true);
        scratch_lifetime();
        cross_alignment();
        std::cout << "Layout performance tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
