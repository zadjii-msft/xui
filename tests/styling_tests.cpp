#include "xui/controls.hpp"
#include "xui/collections.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace xui;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(message);
}
void resources() {
    auto root = ResourceScope::create({{"Fill", ThemeColor{0xffffff, 0}}, {"Alias", std::string("Fill")}});
    auto scope = ResourceScope::create({{"Fill", ThemeColor{1, 2}}, {"LocalAlias", std::string("Fill")}}, root);
    require(scope->color("Fill") == ThemeColor{1, 2}, "Local resource shadows parent");
    require(scope->color("LocalAlias") == ThemeColor{1, 2}, "Local alias resolves local resource");
    require(scope->color("Alias") == ThemeColor{0xffffff, 0}, "Parent aliases keep declaration scope");
    require(root->color("Fill").resolve(ThemeMode::light) == 0xffffff &&
        root->color("Fill").resolve(ThemeMode::dark) == 0, "Theme colors resolve without name lookup");
    rejects([&] { scope->color("Missing"); }, "Missing resource must fail");
    rejects([] { ResourceScope::create({{"A", std::string("B")}, {"B", std::string("A")}}); }, "Resource cycle must fail");
    rejects([] { ResourceScope::create({{"A", ThemeColor{0}}, {"A", ThemeColor{1}}}); }, "Duplicate resource must fail");
    rejects([] { ResourceScope::create({{"A", ThemeColor{0x1000000}}}); }, "ARGB is not RGB24");
    rejects([] { ResourceScope::create(std::vector<ColorResource>(257)); }, "Resource count is bounded");
    for (unsigned depth = 1; depth < 16; ++depth) root = ResourceScope::create({}, root);
    rejects([&] { ResourceScope::create({}, root); }, "Scope depth is bounded");
}
void state_resolution() {
    PartStyleValues base;
    base.background = ThemeColor{1};
    base.foreground = ThemeColor{2};
    base.corner_radius = 8.0f;
    std::vector<StyleRule> states;
    for (unsigned state = 0; state < 5; ++state) {
        PartStyleValues value;
        value.background = ThemeColor{10 + state};
        states.push_back({StylePart::root, StyleStateMask{1} << state, value});
    }
    auto first = ControlStyle::create(StyleTarget::button, {{StylePart::root, base}}, states);
    PartStyleValues derived;
    derived.foreground = ThemeColor{3};
    derived.corner_radius = 0.0f;
    auto second = ControlStyle::create(StyleTarget::button, {{StylePart::root, derived}}, {}, first);
    for (unsigned mask = 0; mask < 32; ++mask) {
        auto expected = 1u;
        for (unsigned state = 0; state < 5; ++state) if (mask & (1 << state)) expected = 10 + state;
        const auto value = second->resolve(StylePart::root, mask).value();
        require(value.background == ThemeColor{expected}, "All state conflicts follow focused/checked/hovered/pressed/disabled order");
        require(value.foreground == ThemeColor{3} && value.corner_radius == 0, "Derived values preserve explicit zero");
    }
    derived.foreground = ThemeColor{99};
    require(second->resolve(StylePart::root, 0).value().foreground == ThemeColor{3}, "Definition copies mutable input");
    require(first->resolve(StylePart::root, 0).value().corner_radius == 8, "Derivation does not change base");
    PartStyleValues overridden;
    overridden.background = ThemeColor{42};
    auto third = ControlStyle::create(StyleTarget::button, {}, {{StylePart::root, style_states::hovered, overridden}}, second);
    require(third->resolve(StylePart::root, 4).value().background == ThemeColor{42}, "Derived state wins the same inherited state");
    require(third->resolve(StylePart::root, 20).value().background == ThemeColor{14}, "Disabled wins over derived hover");
    for (unsigned i = 1; i < 16; ++i) first = ControlStyle::create(StyleTarget::button, {}, {}, first);
    rejects([&] { ControlStyle::create(StyleTarget::button, {}, {}, first); }, "Style depth is bounded");
    rejects([] { ControlStyle::create(StyleTarget::button, {}, {{StylePart::root, StyleStateMask{1} << 5, {}}}); }, "Invalid state fails");
    rejects([] { ControlStyle::create(StyleTarget::button, {}, std::vector<StyleRule>(257)); }, "Rule count is bounded");
}
void button_model() {
    Button button(L"Delete");
    button.set_text_measurer([](auto, auto) { return Size{40, 14}; });
    const auto original = button.measure({1000, 1000});
    require(!button.effective_control_style_values(StylePart::root) && !button.control_style(), "Unstyled Button has no style state");
    PartStyleValues base, hover, disabled, local;
    base.background = ThemeColor{0xcccccc, 0x111111};
    base.padding = Insets{4, 5, 6, 7};
    base.border_thickness = Insets{3, 0, 0, 0};
    hover.background = ThemeColor{0x222222};
    disabled.padding = Insets{0, 0, 0, 0};
    auto style = ControlStyle::create(StyleTarget::button, {{StylePart::root, base}}, {{StylePart::root, style_states::hovered, hover}, {StylePart::root, style_states::disabled, disabled}});
    Invalidation invalidation{};
    unsigned notifications{};
    button.set_invalidator([&](Invalidation value) { invalidation = value; ++notifications; });
    button.set_control_style(style);
    require(invalidation == Invalidation::layout, "Padding and borders require layout");
    const auto* cached_values = button.effective_control_style_values(StylePart::root);
    const auto cached_notifications = notifications;
    for (unsigned i = 0; i < 8192; ++i) button.set_control_style(style);
    require(button.control_style().get() == style.get() && button.effective_control_style_values(StylePart::root) == cached_values,
        "Same definition preserves native identity and cached effective values");
    require(notifications == cached_notifications, "Same definition does not invalidate");
    auto size = button.measure({1000, 1000});
    require(size.width == 53 && size.height == 26, "Measurement includes four padding and border edges");
    button.pointer_move(true);
    require(invalidation == Invalidation::paint, "Color-only hover requires paint, not layout");
    require(button.effective_control_style_values(StylePart::root)->background == ThemeColor{0x222222}, "Hover resolves on state transition");
    button.set_enabled(false);
    require(invalidation == Invalidation::layout, "Disabled padding change requires layout");
    size = button.measure({1000, 1000});
    require(size.width == 43 && size.height == 14, "Explicit zero padding remains explicit");
    local.background = ThemeColor{0};
    local.corner_radius = 0.0f;
    button.set_control_style_values(StylePart::root, local);
    require(button.effective_control_style_values(StylePart::root)->background == ThemeColor{0}, "Local value wins active state and supports black");
    button.set_control_style(nullptr);
    require(button.control_style_values(StylePart::root).background == ThemeColor{0} && !button.control_style(), "Clear style preserves stored local values");
    button.set_control_style(style);
    require(button.effective_control_style_values(StylePart::root)->background == ThemeColor{0}, "Apply style preserves local value");
    auto invalid = local;
    invalid.padding = Insets{-1, 0, 0, 0};
    rejects([&] { button.set_control_style_values(StylePart::root, invalid); }, "Negative dimension fails");
    require(button.control_style_values(StylePart::root).background == ThemeColor{0} && !button.control_style_values(StylePart::root).padding,
        "Failed local update is atomic");
    invalid.padding = Insets{0, 0, 0, std::numeric_limits<float>::infinity()};
    rejects([&] { ControlStyle::create(StyleTarget::button, {{StylePart::root, invalid}}, {}); }, "Infinite dimension fails");
    invalid.padding.reset(); invalid.corner_radius = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { ControlStyle::create(StyleTarget::button, {{StylePart::root, invalid}}, {}); }, "NaN radius fails");
    invalid.corner_radius = 32769.0f;
    rejects([&] { ControlStyle::create(StyleTarget::button, {{StylePart::root, invalid}}, {}); }, "Huge radius fails");
    button.set_control_style(nullptr);
    button.set_control_style_values(StylePart::root, {});
    button.set_enabled(true);
    size = button.measure({1000, 1000});
    require(!button.effective_control_style_values(StylePart::root) && size.width == original.width && size.height == original.height,
        "Clear local and style releases the shared attachment and restores original measurement");
    const auto before = notifications;
    button.set_control_style(nullptr);
    button.set_control_style_values(StylePart::root, {});
    require(notifications == before, "Repeated empty clears do not invalidate");
    for (unsigned i = 0; i < 10000; ++i) {
        button.set_control_style(style);
        require(button.control_style().get() == style.get(), "Buttons share immutable definitions");
        button.pointer_move(i % 2 == 0);
        button.set_control_style(nullptr);
        require(!button.effective_control_style_values(StylePart::root), "Clear drops effective state");
    }
    require(style.use_count() == 1, "Apply/clear does not retain style definitions");
}
}
int main() {
    try {
        resources(); state_resolution(); button_model();
        std::cout << "Style resources, 32 state combinations, model, invalidation, validation, and retention passed.\n";
        std::cout << "sizeof_control=" << sizeof(xui::Control) << " sizeof_button=" << sizeof(xui::Button)
            << " sizeof_items_view=" << sizeof(xui::ItemsView) << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
