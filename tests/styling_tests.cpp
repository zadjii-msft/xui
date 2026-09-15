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
    ButtonStyleValues base;
    base.background = ThemeColor{1};
    base.foreground = ThemeColor{2};
    base.corner_radius = 8.0f;
    std::vector<ButtonStyleRule> states;
    for (unsigned state = 0; state < 5; ++state) {
        ButtonStyleValues value;
        value.background = ThemeColor{10 + state};
        states.push_back({static_cast<ButtonStyleState>(state), value});
    }
    auto first = ButtonStyle::create(base, states);
    ButtonStyleValues derived;
    derived.foreground = ThemeColor{3};
    derived.corner_radius = 0.0f;
    auto second = ButtonStyle::create(derived, {}, first);
    for (unsigned mask = 0; mask < 32; ++mask) {
        auto expected = 1u;
        for (unsigned state = 0; state < 5; ++state) if (mask & (1 << state)) expected = 10 + state;
        const auto& value = second->values(mask);
        require(value.background == ThemeColor{expected}, "All state conflicts follow focused/checked/hovered/pressed/disabled order");
        require(value.foreground == ThemeColor{3} && value.corner_radius == 0, "Derived values preserve explicit zero");
    }
    derived.foreground = ThemeColor{99};
    require(second->values(0).foreground == ThemeColor{3}, "Definition copies mutable input");
    require(first->values(0).corner_radius == 8, "Derivation does not change base");
    ButtonStyleValues overridden;
    overridden.background = ThemeColor{42};
    auto third = ButtonStyle::create({}, {{ButtonStyleState::hovered, overridden}}, second);
    require(third->values(4).background == ThemeColor{42}, "Derived state wins the same inherited state");
    require(third->values(20).background == ThemeColor{14}, "Disabled wins over derived hover");
    for (unsigned i = 1; i < 16; ++i) first = ButtonStyle::create({}, {}, first);
    rejects([&] { ButtonStyle::create({}, {}, first); }, "Style depth is bounded");
    rejects([] { ButtonStyle::create({}, {{static_cast<ButtonStyleState>(5), {}}}); }, "Invalid state fails");
    rejects([] { ButtonStyle::create({}, std::vector<ButtonStyleRule>(257)); }, "Rule count is bounded");
}
void button_model() {
    Button button(L"Delete");
    button.set_text_measurer([](auto, auto) { return Size{40, 14}; });
    const auto original = button.measure({1000, 1000});
    require(!button.effective_style_values() && !button.style(), "Unstyled Button has no style state");
    ButtonStyleValues base, hover, disabled, local;
    base.background = ThemeColor{0xcccccc, 0x111111};
    base.padding = Insets{4, 5, 6, 7};
    base.border_thickness = Insets{3, 0, 0, 0};
    hover.background = ThemeColor{0x222222};
    disabled.padding = Insets{0, 0, 0, 0};
    auto style = ButtonStyle::create(base, {{ButtonStyleState::hovered, hover}, {ButtonStyleState::disabled, disabled}});
    Invalidation invalidation{};
    unsigned notifications{};
    button.set_invalidator([&](Invalidation value) { invalidation = value; ++notifications; });
    button.set_style(style);
    require(invalidation == Invalidation::layout, "Padding and borders require layout");
    const auto* cached_values = button.effective_style_values();
    const auto cached_notifications = notifications;
    for (unsigned i = 0; i < 8192; ++i) button.set_style(style);
    require(button.style().get() == style.get() && button.effective_style_values() == cached_values,
        "Same definition preserves native identity and cached effective values");
    require(notifications == cached_notifications, "Same definition does not invalidate");
    auto size = button.measure({1000, 1000});
    require(size.width == 53 && size.height == 26, "Measurement includes four padding and border edges");
    button.set_style_enabled(false);
    require(button.enabled() && button.effective_style_values()->padding->left == 0,
        "Disabled presentation context selects disabled style without changing local enabled");
    button.set_style_enabled(true);
    button.pointer_move(true);
    require(invalidation == Invalidation::paint, "Color-only hover requires paint, not layout");
    require(button.effective_style_values()->background == ThemeColor{0x222222}, "Hover resolves on state transition");
    button.set_enabled(false);
    require(invalidation == Invalidation::layout, "Disabled padding change requires layout");
    size = button.measure({1000, 1000});
    require(size.width == 43 && size.height == 14, "Explicit zero padding remains explicit");
    local.background = ThemeColor{0};
    local.corner_radius = 0.0f;
    button.set_style_values(local);
    require(button.effective_style_values()->background == ThemeColor{0}, "Local value wins active state and supports black");
    button.set_style(nullptr);
    require(button.style_values().background == ThemeColor{0} && !button.style(), "Clear style preserves stored local values");
    button.set_style(style);
    require(button.effective_style_values()->background == ThemeColor{0}, "Apply style preserves local value");
    auto invalid = local;
    invalid.padding = Insets{-1, 0, 0, 0};
    rejects([&] { button.set_style_values(invalid); }, "Negative dimension fails");
    require(button.style_values().background == ThemeColor{0} && !button.style_values().padding,
        "Failed local update is atomic");
    invalid.padding = Insets{0, 0, 0, std::numeric_limits<float>::infinity()};
    rejects([&] { ButtonStyle::create(invalid); }, "Infinite dimension fails");
    invalid.padding.reset(); invalid.corner_radius = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { ButtonStyle::create(invalid); }, "NaN radius fails");
    invalid.corner_radius = 32769.0f;
    rejects([&] { ButtonStyle::create(invalid); }, "Huge radius fails");
    button.set_style(nullptr);
    button.set_style_values({});
    button.set_enabled(true);
    size = button.measure({1000, 1000});
    require(!button.effective_style_values() && size.width == original.width && size.height == original.height,
        "Clear local and style releases sidecar and restores original measurement");
    const auto before = notifications;
    button.set_style(nullptr);
    button.set_style_values({});
    require(notifications == before, "Repeated empty clears do not invalidate");
    for (unsigned i = 0; i < 10000; ++i) {
        button.set_style(style);
        require(button.style().get() == style.get(), "Buttons share immutable definitions");
        button.pointer_move(i % 2 == 0);
        button.set_style(nullptr);
        require(!button.effective_style_values(), "Clear drops effective state");
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
