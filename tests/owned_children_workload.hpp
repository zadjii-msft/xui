#pragma once
#include "xui/foundation.hpp"
#include "xui/reveal.hpp"

namespace owned_workload {
inline std::shared_ptr<xui::Stack> make_tree() {
    using namespace xui;
    auto root = std::make_shared<Stack>(Axis::vertical);
    for (int i = 0; i < 32; ++i) {
        auto pages = std::make_shared<PageView>();
        for (int page = 0; page < 4; ++page) {
            auto body = std::make_shared<Stack>(Axis::vertical);
            for (int row = 0; row < 16; ++row) body->add(std::make_shared<NumericInput>());
            pages->add(std::make_shared<Reveal>(std::make_shared<ContentView>(std::make_shared<ScrollView>(body))));
        }
        root->add(pages);
    }
    return root;
}
inline std::uint64_t walk(const xui::Element& element, std::size_t& visits) {
    ++visits;
    auto sum = element.id();
#ifdef XUI_BASELINE_OWNED_CHILDREN
    // Measurement adapter for pristine/stage-1 libraries, never runtime compatibility.
    if (const auto* stack = dynamic_cast<const xui::Stack*>(&element)) {
        for (std::size_t i = 0; i < stack->child_count(); ++i) sum += walk(*stack->child_at(i), visits);
    } else if (const auto* control = dynamic_cast<const xui::Control*>(&element)) {
        if (const auto* scroll = dynamic_cast<const xui::ScrollView*>(control)) sum += walk(*scroll->content(), visits);
        if (const auto* content = dynamic_cast<const xui::ContentView*>(control)) sum += walk(*content->content(), visits);
        if (const auto* split = dynamic_cast<const xui::SplitView*>(control)) {
            sum += walk(*split->first(), visits); sum += walk(*split->second(), visits);
        }
        for (const auto& child : control->retained_children()) sum += walk(*child, visits);
    }
#else
    for (const auto& child : element.owned_children()) sum += walk(*child, visits);
#endif
    return sum;
}
inline constexpr std::size_t nodes = 8737;
inline constexpr int iterations = 2000;
}
