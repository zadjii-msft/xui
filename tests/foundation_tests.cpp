#include "xui/foundation.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F&& operation) {
    bool rejected{};
    try { operation(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid input must throw invalid_argument");
}
struct Comma : std::numpunct<wchar_t> {
    wchar_t do_decimal_point() const override { return L','; }
    wchar_t do_thousands_sep() const override { return L'.'; }
    std::string do_grouping() const override { return "\3"; }
};
void ranges() {
    using namespace xui;
    RangeInput range;
    int changed{}, preview{};
    range.on_change([&](double) { ++changed; });
    range.on_preview([&](double) { ++preview; });
    range.set_range({-100, 100, 5, 20});
    range.set_value(10);
    require(!changed && !preview, "Properties do not fire application callbacks");
    range.move(RangeKey::increase);
    require(range.value() == 15 && changed == 1, "Arrow steps use small change");
    range.move(RangeKey::page_decrease);
    require(range.value() == -5, "Page uses large change");
    range.begin_drag(0.8);
    require(range.value() == -5 && range.preview_value() == 60 && preview == 1, "Drag preview does not commit");
    range.cancel();
    require(range.preview_value() == -5 && !range.dragging(), "Cancel restores committed value");
    range.begin_drag(1); range.commit_drag();
    require(range.value() == 100 && changed == 3, "Release commits once");
    range.set_reversed(true); range.begin_drag(1); range.commit_drag();
    require(range.value() == -100, "Explicit reverse direction");
    range.begin_drag(0.5); range.set_enabled(false);
    require(!range.dragging() && !range.move(RangeKey::increase), "Disable cancels capture state");
    const auto nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    for (const auto invalid : {nan, inf, -inf, 101.0}) rejects([&] { range.set_value(invalid); });
    rejects([&] { range.set_range({1, 1}); });
    rejects([&] { range.set_range({-std::numeric_limits<double>::max(), std::numeric_limits<double>::max()}); });
    rejects([&] { range.set_range({0, 1, 0, 1}); });
    rejects([&] { range.set_range({0, inf}); });
    range.set_enabled(true);
    range.on_preview([&](double) { range.cancel(); });
    range.begin_drag(0.5);
    require(!range.dragging(), "Reentrant preview can cancel safely");
    range.on_preview([](double) { throw std::runtime_error("preview"); });
    bool thrown{};
    try { range.begin_drag(0.4); } catch (...) { thrown = true; }
    require(thrown, "Core propagates callback errors to the host boundary");
    range.cancel();
    int cancelled{};
    range.on_preview({});
    range.on_cancel([&](double value) {
        ++cancelled;
        require(value == range.value() && !range.dragging(), "Cancellation sees the committed value and cleared preview");
        range.set_value(0);
    });
    range.begin_drag(0.2); range.cancel_drag(); range.cancel_drag();
    require(cancelled == 1 && range.value() == 0, "Input cancellation is idempotent and permits reentrant setters");
    range.begin_drag(0.3); range.cancel();
    require(cancelled == 1, "Property cancellation remains silent");
    const auto unchanged = changed;
    range.begin_drag(0.4); range.change_value(range.value());
    require(!range.dragging() && changed == unchanged, "An accepted unchanged value still clears a pending preview");
}
void choices() {
    using namespace xui;
    RadioGroup radio;
    int changes{};
    radio.on_change([&](auto) { ++changes; });
    radio.set_items({{10, L"Alpha"}, {20, L"Unavailable", false}, {30, L"Beta"}}, 10);
    radio.step(1);
    require(radio.selected() == 30 && changes == 1, "Exclusive arrows skip disabled choices");
    radio.step(1);
    require(radio.selected() == 10, "Arrows wrap");
    radio.set_selected(30);
    require(changes == 2, "Selected property does not emit");
    radio.type_ahead(L"al");
    require(radio.selected() == 10, "Case-insensitive prefix selection");
    rejects([&] { radio.set_items({{1, L"One"}, {1, L"Duplicate"}}); });
    rejects([&] { radio.set_selected(999); });
    rejects([&] { radio.select(20); });
    radio.arrange({0, 0, 100, 34}); radio.set_selected(30);
    require(radio.hit_test(10) == 2 && radio.item_bounds(2).height == 34, "Selected row remains visible");
    radio.set_enabled(false); require(!radio.step(1), "Disabled choices do not change");
    ComboBox combo(L"Format", true);
    combo.set_items({{10, L"Text"}, {20, L"Markdown"}}, 10);
    combo.prepare_popup(); combo.choices()->select(20);
    require(combo.selected() == 10, "Picker preview is separate");
    combo.prepare_popup(); require(combo.choices()->selected() == 10, "Cancel restores preview on next open");
    combo.select(20); require(combo.editor()->text() == L"Markdown", "Commit updates native edit model");
    combo.editor()->commit_text(L"custom");
    require(combo.selected() == 20, "Custom text does not invent a selected identity");
    std::shared_ptr<TextInput> retained;
    { auto owner = std::make_unique<ComboBox>(L"Retained", true); retained = owner->editor(); }
    retained->commit_text(L"After owner destruction");
}
void numeric() {
    using namespace xui;
    NumericInput number;
    number.set_range({-2000, 2000, 0.5, 10});
    number.set_locale(std::locale(std::locale::classic(), new Comma));
    int changed{}; number.on_change([&](double) { ++changed; });
    require(number.commit_text(L"1.234,5") && number.value() == 1234.5, "Locale thousands and decimal separators");
    require(!number.commit_text(L"1,2 garbage") && !number.valid() && number.editor()->text() == L"1,2 garbage",
        "Invalid native text remains visible");
    require(number.value() == 1234.5 && changed == 1, "Invalid text cannot overwrite numeric value");
    for (auto text : {L"", L"nan", L"inf", L"9e9999", L"2001", L"12.34,5"}) require(!number.commit_text(text), "Reject malformed and unbounded text");
    number.step(1); require(number.valid() && number.value() == 1235, "Step restores formatted valid input");
    const auto before = changed; number.set_value(1.5);
    require(changed == before && number.editor()->text() == L"1,5", "Locale formatting and silent setters");
    std::shared_ptr<TextInput> retained;
    { auto owner = std::make_unique<NumericInput>(); retained = owner->editor(); }
    retained->commit_text(L"10");
}
void popup_disclosure_progress_actions() {
    using namespace xui;
    auto child = std::make_shared<TextInput>(L"Child");
    Popup popup(child); popup.opened();
    const auto generation = popup.generation();
    int dismissed{};
    popup.on_dismiss([&](auto) { ++dismissed; require(!popup.current(generation), "Dismiss callbacks see a stale generation"); });
    popup.closed(PopupDismissReason::cancel); popup.closed(PopupDismissReason::cancel);
    require(dismissed == 1 && !popup.is_open(), "Idempotent close");
    const auto placed = place_popup({180, 180, 20, 20}, {80, 70}, {0, 0, 200, 200}, PopupPlacement::below);
    require(placed.x == 120 && placed.y == 110, "Work area flips and clips");
    const auto clipped = place_popup({0, 0, 10, 10}, {400, 400}, {0, 0, 100, 100}, PopupPlacement::right);
    require(clipped.width == 100 && clipped.height == 100 && clipped.x == 0 && clipped.y == 0, "Oversized content clips");
    popup.set_placement(PopupPlacement::center);
    require(popup.placement() == PopupPlacement::center, "Centered placement is retained");
    for (const auto anchor : {Rect{20, 30, 80, 40}, Rect{900, 300, 120, 40}}) {
        const auto centered = place_popup(anchor, {760, 420}, {10, 20, 1300, 800}, PopupPlacement::center);
        require(centered.x == 280 && centered.y == 210 && centered.width == 760 && centered.height == 420,
            "Popup centers in the client viewport regardless of its pane anchor");
        const auto small = place_popup(anchor, {760, 420}, {10, 20, 300, 200}, PopupPlacement::center);
        require(small.x == 10 && small.y == 20 && small.width == 300 && small.height == 200,
            "Centered popups fit narrow and offset viewports");
    }
    rejects([&] { popup.set_placement(static_cast<PopupPlacement>(5)); });
    auto content = std::make_shared<Stack>(Axis::vertical);
    auto button = std::make_shared<Button>(L"Details action"); content->add(button);
    Expander expander(L"Details", content);
    expander.set_expanded(false); expander.arrange({0, 0, 200, 100});
    require(button->bounds().height == 0, "Collapsed subtree has zero layout");
    int expanded{}; expander.on_change([&](bool) { ++expanded; }); expander.invoke();
    require(expander.expanded() && expanded == 1, "Expander activation is semantic");
    Progress progress; int invalidations{}; progress.set_invalidator([&](auto) { ++invalidations; });
    progress.set_capacity(48, 128, L"GB");
    const auto count = invalidations; progress.set_capacity(48, 128, L"GB");
    require(invalidations == count && !progress.focusable(), "Capacity updates are idempotent and read-only");
    rejects([&] { progress.set_capacity(1, 0); });
    rejects([&] { progress.set_value(std::numeric_limits<double>::infinity()); });
    SplitButton split(L"Run", L"Options");
    int primary{}, secondary{}; split.primary()->on_click([&] { ++primary; }); split.secondary()->on_click([&] { ++secondary; });
    split.secondary()->invoke(); require(primary == 0 && secondary == 1, "Split secondary cannot invoke primary");
    Button toggle(L"Action"); toggle.set_behavior(ButtonBehavior::toggle); toggle.invoke();
    require(toggle.checked(), "Toggle action retains state");
    rejects([&] { toggle.set_repeat_timing(0, 1); });
    rejects([&] { toggle.set_tooltip_delay(0); });
}
}
int main() {
    try { ranges(); choices(); numeric(); popup_disclosure_progress_actions(); std::cout << "Foundation core contracts passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
