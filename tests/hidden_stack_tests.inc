#include <array>
#include <cmath>
#include <source_location>

namespace hidden_stack_tests {
using namespace xui;
unsigned assertions{};
void check(bool value, const std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("Hidden Stack assertion at line " + std::to_string(where.line()));
    ++assertions;
}
void metric(float actual, float expected, const std::source_location where = std::source_location::current()) {
    if (!std::isfinite(actual) || std::abs(actual - expected) >= 0.01f)
        throw std::runtime_error("Hidden Stack metric " + std::to_string(actual) + " != " +
            std::to_string(expected) + " at line " + std::to_string(where.line()));
    ++assertions;
}
void run() {
    for (const auto axis : {Axis::vertical, Axis::horizontal}) {
        Stack stack(axis); stack.set_spacing(10); stack.set_padding({2, 3, 5, 7});
        std::array<std::shared_ptr<Button>, 3> children;
        for (std::size_t i = 0; i < children.size(); ++i) {
            children[i] = std::make_shared<Button>(L"Retained");
            children[i]->set_preferred_size({30.0f + 10 * i, 20.0f + 10 * i});
            stack.add(children[i]);
        }
        for (const auto mask : {5u, 6u, 3u, 0u, 7u, 1u, 2u, 4u, 7u}) {
            unsigned visible{}; float main{}, cross{};
            for (std::size_t i = 0; i < children.size(); ++i) {
                children[i]->set_visible((mask & (1u << i)) != 0);
                check(stack.child_at(i) == children[i]);
                if (!children[i]->visible()) continue;
                ++visible;
                main += (axis == Axis::vertical ? 20.0f : 30.0f) + 10 * i;
                cross = std::max(cross, (axis == Axis::vertical ? 30.0f : 20.0f) + 10 * i);
            }
            check(stack.child_count() == 3);
            const auto desired = stack.measure({300, 200});
            const auto gaps = visible ? (visible - 1) * 10.0f : 0;
            metric(desired.width, (axis == Axis::vertical ? cross : main + gaps) + 7);
            metric(desired.height, (axis == Axis::vertical ? main + gaps : cross) + 10);
            stack.arrange({11, 17, 300, 200});
            float position = axis == Axis::vertical ? 20.0f : 13.0f;
            bool previous{};
            for (std::size_t i = 0; i < children.size(); ++i) {
                const auto bounds = children[i]->bounds();
                if (!children[i]->visible()) {
                    metric(bounds.width, 0); metric(bounds.height, 0); continue;
                }
                if (previous) position += 10;
                metric(axis == Axis::vertical ? bounds.y : bounds.x, position);
                const auto length = (axis == Axis::vertical ? 20.0f : 30.0f) + 10 * i;
                metric(axis == Axis::vertical ? bounds.height : bounds.width, length);
                position += length; previous = true;
            }
        }
    }
    {
        Stack stack(Axis::vertical); stack.set_spacing(10);
        auto first = std::make_shared<Label>(L"First"), last = std::make_shared<Label>(L"Last");
        auto hidden = std::make_shared<Button>(L"Hidden");
        auto empty = std::make_shared<Stack>(Axis::horizontal);
        first->set_preferred_size({30, 20}); last->set_preferred_size({30, 40});
        hidden->set_visible(false);
        stack.add(first); stack.add(empty); stack.add(hidden); stack.add(last);
        metric(stack.measure({300, 200}).height, 80);
        stack.arrange({0, 0, 300, 200});
        metric(empty->bounds().height, 0); metric(empty->bounds().y, 30);
        metric(last->bounds().y, 40);
        first->set_preferred_size({0, 0});
        metric(stack.measure({300, 200}).height, 60);
        stack.arrange({0, 0, 300, 200});
        metric(last->bounds().y, 20);
    }
    {
        Stack stack(Axis::vertical); stack.set_spacing(10);
        std::array<std::shared_ptr<Button>, 3> children;
        for (std::size_t i = 0; i < children.size(); ++i) {
            children[i] = std::make_shared<Button>(L"Flex"); children[i]->set_preferred_size({30, 20});
            stack.add(children[i], static_cast<float>(i + 1));
        }
        for (const auto mask : {5u, 6u, 3u, 0u, 7u}) {
            float weight{}; unsigned visible{};
            for (std::size_t i = 0; i < children.size(); ++i) {
                children[i]->set_visible((mask & (1u << i)) != 0);
                if (children[i]->visible()) { weight += i + 1; ++visible; }
            }
            stack.arrange({0, 0, 300, 110});
            float position{};
            bool previous{};
            for (std::size_t i = 0; i < children.size(); ++i) {
                const auto bounds = children[i]->bounds();
                if (!children[i]->visible()) { metric(bounds.width, 0); metric(bounds.height, 0); continue; }
                if (previous) position += 10;
                const float height = (110 - (visible - 1) * 10.0f) * (i + 1) / weight;
                metric(bounds.y, position); metric(bounds.height, height);
                position += height; previous = true;
            }
        }
        children[1]->set_visible(false);
        stack.arrange_with_context({0, 0, 300, 110}, {false, true});
        metric(children[0]->bounds().height, 20); metric(children[2]->bounds().y, 30);
        PartStyleValues alignment; alignment.vertical_alignment = StyleAlignment::end;
        stack.set_control_style_values(StylePart::root, alignment);
        stack.arrange_with_context({0, 0, 300, 110}, {false, true});
        metric(children[0]->bounds().y, 60); metric(children[2]->bounds().y, 90);
    }
    std::cout << assertions << " hidden Stack assertions passed\n";
}
}
