#include "xui/foundation.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    bool rejected{};
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid model input must fail explicitly");
}
void checkbox() {
    CheckBox check(L"Include"); Toggle binary(L"Binary");
    require(!check.three_state() && check.state() == CheckState::unchecked, "Checkbox defaults to binary unchecked input");
    int changes{}; CheckState delivered{};
    check.on_change([&](CheckState value) { ++changes; delivered = value; });
    check.set_state(CheckState::indeterminate);
    require(!changes && check.indeterminate() && !check.checked(), "Programmatic mixed state is silent and uses shared Toggle storage");
    check.set_checked(true);
    require(check.state() == CheckState::checked, "Inherited binary setter cannot diverge from checkbox state");
    check.set_three_state(true);
    check.invoke();
    require(delivered == CheckState::indeterminate && changes == 1, "Three-state input cycles checked to mixed");
    check.invoke(); require(check.state() == CheckState::unchecked, "Mixed cycles to unchecked");
    check.set_focused(true); check.key_down(ActivationKey::space); check.cancel(); check.key_up(ActivationKey::space);
    require(changes == 2, "Cancelled checkbox key input is silent");
    check.pointer_down(); check.set_enabled(false); check.pointer_up(true);
    require(changes == 2 && !check.invoke(), "Disabled checkbox cannot activate");
    rejects([&] { check.set_state(static_cast<CheckState>(4)); });
    binary.invoke(); binary.invoke();
    require(!binary.checked() && !binary.indeterminate(), "Existing Toggle stays binary");
    PartStyleValues fill; fill.background = ThemeColor{0x123456};
    check.set_style(ControlStyle::create(StyleTarget::toggle, {},
        {{StylePart::indicator, style_states::checked, fill}}));
    check.set_state(CheckState::indeterminate);
    require(check.effective_style_values(StylePart::indicator)->background.has_value(), "Mixed state participates in checked styles");
}
void hyperlink() {
    HyperlinkButton link(L"Documentation"); int clicks{};
    link.on_click([&] { ++clicks; });
    link.set_focused(true); link.key_down(ActivationKey::enter);
    require(clicks == 1, "Hyperlink executes an explicit callback");
    link.key_down(ActivationKey::space); link.set_focused(false); link.key_up(ActivationKey::space);
    require(clicks == 1, "Focus departure cancels hyperlink Space input");
    link.set_enabled(false); require(!link.invoke(), "Disabled hyperlink rejects invocation");
}
void selector() {
    SelectorBar bar(L"Views"); int changes{};
    bar.on_change([&](std::uint64_t) { ++changes; });
    bar.set_items({{1,L"All"}, {2,L"Disabled",false}, {3,L"Recent"}, {4,L"Pinned"}}, 1);
    require(bar.role() == ControlRole::choice_list && bar.tab_stop() && bar.horizontal_presentation(),
        "Selector is one exclusive choice peer, not a checkbox group");
    bar.arrange({0,0,400,42});
    const auto first = bar.item_bounds(0), second = bar.item_bounds(1);
    require(first.y == second.y && second.x > first.x + first.width, "Selector lays choices across a horizontal row");
    require(bar.hit_test(Point{second.x + 1, second.y + 1}) == 1, "Selector pointer hit uses both coordinates");
    require(!bar.hit_test(Point{first.x + first.width + 1, first.y + 1}), "Selector inter-item gap is not actionable");
    bar.step(1); require(bar.selected() == 3 && changes == 1, "Selector arrows skip disabled IDs");
    bar.set_selected(4); require(changes == 1, "Selector property updates are silent");
    rejects([&] { bar.set_selected(2); });
    rejects([&] { bar.set_items({{1,L"A"},{1,L"B"}}); });
    bar.arrange({0,0,50,42}); bar.set_selected(1);
    require(bar.item_bounds(0).width > 0 && bar.item_bounds(3).width == 0, "Narrow selector keeps a visible item");
    bar.set_selected(4);
    require(bar.item_bounds(3).width > 0 && bar.item_bounds(0).width == 0, "Keyboard selection reveals overflow choices");
    bar.set_items({});
    require(!bar.selected() && !bar.step(1), "Empty selector has no fabricated selection");
}
void badge() {
    InfoBadge badge(L"Unread");
    require(badge.kind() == InfoBadgeKind::dot && !badge.focusable() && !badge.invoke(), "Badge is a noninteractive status indicator");
    const auto dot = badge.measure({100,100});
    badge.set_count(100);
    require(badge.count() == 100 && badge.display_text() == L"99+" && badge.measure({100,100}).width > dot.width,
        "Count display caps visually without losing its real value");
    badge.set_count(std::numeric_limits<std::uint32_t>::max());
    require(badge.count() == std::numeric_limits<std::uint32_t>::max(), "Badge retains full unsigned count");
    badge.set_icon(ButtonIcon::bookmark);
    require(badge.kind() == InfoBadgeKind::icon && badge.icon() == ButtonIcon::bookmark, "Icon selection changes presentation");
    rejects([&] { badge.set_icon(static_cast<ButtonIcon>(999)); });
    badge.set_dot(); require(badge.kind() == InfoBadgeKind::dot, "Dot selection restores compact presentation");
    PartStyleValues text; text.font_size = 24.0f;
    badge.set_control_style(ControlStyle::create(StyleTarget::inline_status, {{StylePart::message,text}}, {}));
    badge.set_count(42);
    require(badge.measure({100,100}).height == 40, "Authored badge typography participates in measurement");
}
}
int main() {
    try { checkbox(); hyperlink(); selector(); badge(); std::cout << "Next control model contracts passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
