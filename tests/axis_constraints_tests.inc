#include "xui/adaptive_layout.hpp"
#include "xui/foundation.hpp"
#include <cmath>
#include <limits>
#include <source_location>

namespace axis_constraints_tests {
using namespace xui;
unsigned assertions{};
void check(bool condition, const std::source_location location = std::source_location::current()) {
    if (!condition) throw std::runtime_error("Axis constraint assertion at line " + std::to_string(location.line()));
    ++assertions;
}
void near(float actual, float expected, const std::source_location location = std::source_location::current()) {
    if (std::abs(actual - expected) > 0.01f || !std::isfinite(actual))
        throw std::runtime_error("Axis metric at line " + std::to_string(location.line()) +
            ": " + std::to_string(actual) + " != " + std::to_string(expected));
    ++assertions;
}
void size(Size actual, float width, float height) { near(actual.width, width); near(actual.height, height); }
void run() {
    const AxisConstraints automatic{};
    {
        Element element;
        element.set_fixed_size({90, 40});
        element.set_axis_constraints(AxisConstraints{30}, std::nullopt);
        size(element.measure({500, 500}), 30, 40);
        element.arrange({10, 20, 500, 500});
        near(element.bounds().width, 30); near(element.bounds().height, 40);
        element.set_fixed_size({110, 55});
        size(element.measure({500, 500}), 30, 55);
        element.set_axis_constraints(std::nullopt, AxisConstraints{80, 60, 90});
        size(element.measure({500, 500}), 110, 80);
        size(element.measure({10, 12}), 10, 12);
        element.arrange({0, 0, 5, 6});
        near(element.bounds().width, 5); near(element.bounds().height, 6);
        element.set_axis_constraints(std::nullopt, std::nullopt);
        size(element.measure({500, 500}), 110, 55);
    }
    {
        TextInput input(L"Per-axis caption");
        input.set_visual_style(VisualStyle::winui);
        const auto baseline = input.measure({1000, 1000});
        input.set_fixed_size({210, 110});
        input.set_axis_constraints(automatic, std::nullopt);
        size(input.measure({1000, 1000}), 320, 110);
        check(input.preferred_size_explicit() && !input.auto_size());
        input.set_fixed_size({250, 120});
        size(input.measure({1000, 1000}), 320, 120);
        input.set_axis_constraints(std::nullopt, automatic);
        near(input.measure({1000, 1000}).width, 250);
        check(input.measure({1000, 1000}).height >= baseline.height);
        PartStyleValues font; font.font_size = 32.0f;
        input.set_control_style_values(StylePart::text, font);
        input.set_control_style_values(StylePart::header, font);
        const auto large = input.measure({1000, 1000});
        check(large.height >= 48 + input.caption_extent());
        input.set_preferred_size({350, 122});
        near(input.measure({1000, 1000}).height, large.height);
        input.set_axis_constraints(std::nullopt, std::nullopt);
        // Preferred does not reset the latest legacy Fixed limits.
        size(input.measure({1000, 1000}), 250, 120);
        input.set_maximum_size({1000, 1000}); input.set_minimum_size({0, 0});
        size(input.measure({1000, 1000}), 350, 122);
        input.set_axis_constraints(AxisConstraints{280}, automatic);
        near(input.measure({1000, 1000}).height, large.height);
        size(input.measure({100, 20}), 100, 20);
        input.set_axis_constraints(AxisConstraints{{}, 400, 500}, std::nullopt);
        near(input.measure({260, 1000}).width, 260);
        input.arrange({0, 0, 900, 150}); near(input.bounds().width, 500);
        input.arrange({0, 0, 260, 150}); near(input.bounds().width, 260);
    }
    {
        Label label(L"Wrapping text");
        label.set_text_measurer([](auto, auto) { return Size{300, 20}; });
        float offered{};
        label.set_wrapping(true);
        label.set_wrapped_text_measurer([&](auto, auto, float width, auto) {
            offered = width; return Size{std::min(300.0f, width), std::ceil(300.0f / width) * 20};
        });
        label.set_fixed_size({300, 25});
        label.set_axis_constraints(AxisConstraints{75}, automatic);
        size(label.measure({500, 500}), 75, 80); near(offered, 75);
        label.set_axis_constraints(AxisConstraints{{}, 0, 100}, automatic);
        size(label.measure({500, 500}), 100, 60); near(offered, 100);
        label.set_axis_constraints(automatic, std::nullopt);
        size(label.measure({500, 500}), 300, 25);
        unsigned layouts{};
        label.set_invalidator([&](Invalidation kind) { layouts += kind == Invalidation::layout; });
        label.set_name(L"Updated natural text"); check(layouts == 1);
        label.set_visible(false); size(label.measure({500, 500}), 0, 0);
    }
    {
        Stack stack(Axis::vertical);
        auto first = std::make_shared<Element>(), second = std::make_shared<Element>();
        first->set_preferred_size({80, 30}); second->set_preferred_size({100, 40});
        stack.add(first); stack.add(second); stack.set_spacing(5);
        stack.set_fixed_size({250, 150});
        stack.set_axis_constraints(automatic, automatic);
        size(stack.measure({500, 500}), 100, 75);
        stack.set_axis_constraints(AxisConstraints{60}, automatic);
        size(stack.measure({500, 500}), 60, 75);
        stack.set_axis_constraints(std::nullopt, std::nullopt);
        size(stack.measure({500, 500}), 250, 150);
        stack.set_axis_constraints(automatic, std::nullopt);
        stack.set_auto_size(true);
        // Legacy minimum still governs the inherited height.
        size(stack.measure({500, 500}), 100, 150);
    }
    {
        auto content = std::make_shared<Stack>(Axis::vertical);
        auto child = std::make_shared<Element>(); child->set_preferred_size({80, 55}); content->add(child);
        ScrollView scroll(content);
        scroll.set_fixed_size({200, 150});
        scroll.set_axis_constraints(automatic, automatic);
        const auto measured = scroll.measure({500, 500});
        near(measured.width, 80 + scroll.effective_bar_width()); near(measured.height, 55);
        scroll.set_axis_constraints(std::nullopt, std::nullopt);
        size(scroll.measure({500, 500}), 200, 150);
    }
    {
        auto content = std::make_shared<Stack>(Axis::vertical);
        content->set_spacing(8);
        auto first = std::make_shared<Element>(), second = std::make_shared<Element>();
        first->set_fixed_size({60, 32}); second->set_fixed_size({60, 48});
        content->add(first, 1); content->add(second, 3);
        ScrollView scroll(content);
        scroll.arrange({0, 0, 240, 200});
        check(scroll.fill_viewport());
        near(scroll.extent(), 200); near(second->bounds().y, 56);
        scroll.set_fill_viewport(false); scroll.arrange({0, 0, 240, 200});
        near(scroll.extent(), 88); near(first->bounds().height, 32);
        near(second->bounds().height, 48); near(second->bounds().y, 40);
        content->set_axis_constraints(std::nullopt, AxisConstraints{{}, 0, 100.0f});
        scroll.arrange({0, 0, 240, 200});
        near(scroll.extent(), 88); near(second->bounds().y, 40);
        content->set_axis_constraints(std::nullopt, std::nullopt);
        scroll.arrange({0, 0, 240, 50}); near(scroll.maximum_offset(), 38);
        scroll.set_offset(38); scroll.arrange({0, 0, 240, 50});
        near(second->bounds().y, 2); near(second->bounds().height, 48);
        scroll.set_fill_viewport(true); scroll.arrange({0, 0, 240, 200});
        near(scroll.extent(), 200); near(second->bounds().y, 56);
    }
    {
        auto nested = std::make_shared<Stack>(Axis::vertical);
        nested->set_spacing(8);
        auto first = std::make_shared<Element>(), second = std::make_shared<Element>();
        first->set_preferred_size({60, 32}); second->set_preferred_size({60, 48});
        nested->add(first, 3); nested->add(second, 1);
        auto horizontal = std::make_shared<Stack>(Axis::horizontal);
        horizontal->add(nested, 1);
        ScrollView scroll(horizontal); scroll.set_fill_viewport(false);
        scroll.arrange({0, 0, 240, 200});
        near(scroll.extent(), 88); near(first->bounds().height, 32); near(second->bounds().y, 40);
        nested->set_axis_constraints(std::nullopt, AxisConstraints{200.0f});
        scroll.arrange({0, 0, 240, 300});
        near(first->bounds().height, 144); near(second->bounds().y, 152);
    }
    {
        auto content = std::make_shared<Stack>(Axis::horizontal);
        content->set_spacing(8);
        auto first = std::make_shared<Element>(), second = std::make_shared<Element>();
        first->set_preferred_size({32, 20}); second->set_preferred_size({48, 20});
        content->add(first, 1); content->add(second, 3);
        content->set_axis_constraints(AxisConstraints{{}, 0, 100.0f}, std::nullopt);
        size(content->measure({std::numeric_limits<float>::infinity(), 50}), 88, 20);
        content->arrange_with_context({0, 0, 100, 50}, {true, false});
        near(first->bounds().width, 32); near(second->bounds().x, 40); near(second->bounds().width, 48);
        content->arrange({0, 0, 100, 50});
        near(first->bounds().width, 23); near(second->bounds().x, 31); near(second->bounds().width, 69);
    }
    {
        std::vector<std::shared_ptr<Control>> controls{
            std::make_shared<Button>(L"Button"), std::make_shared<Toggle>(L"Toggle"),
            std::make_shared<CheckBox>(L"CheckBox"), std::make_shared<Progress>()};
        for (const auto& control : controls) {
            check(control->supports_axis_constraints());
            control->set_visual_style(VisualStyle::winui);
            control->set_text_measurer([](auto, auto) { return Size{100, 40}; });
            control->set_fixed_size({240, 70});
            control->set_axis_constraints(automatic, std::nullopt);
            near(control->measure({500, 500}).height, 70);
            check(control->measure({500, 500}).width != 240);
            control->set_axis_constraints(std::nullopt, automatic);
            near(control->measure({500, 500}).width, 240);
            check(control->measure({500, 500}).height != 70);
            control->set_axis_constraints(std::nullopt, std::nullopt);
            size(control->measure({500, 500}), 240, 70);
        }
    }
    {
        Element element;
        const AxisConstraints width{30, 10, 50}, height{40};
        element.set_axis_constraints(width, height);
        unsigned invalidations{};
        element.set_invalidator([&](Invalidation) { ++invalidations; });
        const auto nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        for (const auto invalid : {AxisConstraints{-1}, AxisConstraints{nan}, AxisConstraints{inf},
            AxisConstraints{{}, -1}, AxisConstraints{{}, nan}, AxisConstraints{{}, inf},
            AxisConstraints{{}, 10, 5}, AxisConstraints{{}, 0, nan}, AxisConstraints{{}, 0, inf},
            AxisConstraints{5, 10}, AxisConstraints{60, 0, 50}}) {
            bool rejected{};
            try { element.set_axis_constraints(AxisConstraints{20}, invalid); }
            catch (const std::invalid_argument&) { rejected = true; }
            check(rejected && element.width_constraints() == width && element.height_constraints() == height);
        }
        check(invalidations == 0);
        element.set_axis_constraints(width, height); check(invalidations == 0);
        element.set_axis_constraints(AxisConstraints{0}, automatic); check(invalidations == 1);
        size(element.measure({100, 100}), 0, 0);
        Wrap unsupported;
        check(!unsupported.supports_axis_constraints());
        bool rejected{};
        try { unsupported.set_axis_constraints(automatic, automatic); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && !unsupported.width_constraints() && !unsupported.height_constraints());
    }
    std::cout << assertions << " per-axis model assertions passed\n";
}
}
