#include "xui/foundation.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace xui;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool same(Rect a, Rect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
void numeric_layout() {
    NumericInput number;
    number.set_locale(std::locale::classic());
    number.set_value(12);
    const auto children = number.retained_children();
    const auto editor = children[0], decrease = children[1], increase = children[2];
    number.arrange({10, 20, 320, 44});
    require(same(editor->bounds(), {10, 20, 240, 44}) &&
        same(decrease->bounds(), {250, 20, 40, 44}) &&
        same(increase->bounds(), {290, 20, 40, 44}), "Classic NumberBox geometry is unchanged");
    int changes{};
    number.on_change([&](double) { ++changes; });
    number.set_visual_style(VisualStyle::winui);
    require(number.measure({1000, 1000}).height == 32, "WinUI NumberBox defaults to 32 DIPs");
    require(same(editor->bounds(), number.editor_bounds()) &&
        same(decrease->bounds(), number.decrease_bounds()) &&
        same(increase->bounds(), number.increase_bounds()), "NumberBox peers use the public geometry");
    require(same(editor->bounds(), {10, 20, 248, 44}) &&
        same(increase->bounds(), {258, 24, 32, 36}) &&
        same(decrease->bounds(), {294, 24, 32, 36}), "WinUI puts Up before Down with template margins and vertical stretch");
    require(std::static_pointer_cast<Button>(decrease)->appearance() == ButtonAppearance::subtle,
        "Inline spin buttons are subtle actions");
    require(number.retained_children()[0] == editor && number.retained_children()[1] == decrease &&
        number.retained_children()[2] == increase && number.value() == 12 && !changes,
        "Style switches retain NumberBox peers and value without callbacks");
    number.commit_text(L"invalid");
    number.set_visual_style(VisualStyle::classic);
    require(!number.valid() && number.editor()->text() == L"invalid" && number.value() == 12 && !changes,
        "Style switches retain invalid edit buffers");
    require(same(decrease->bounds(), {250, 20, 40, 44}), "Classic bounds return after a style switch");
    number.set_visual_style(VisualStyle::winui);
    std::static_pointer_cast<Button>(increase)->invoke();
    require(number.value() == 13 && changes == 1 && number.valid(), "Retained spin button still invokes its action");
    number.set_preferred_size({210, 60});
    require(number.measure({1000, 1000}).height == 60, "Explicit NumberBox height is retained");
    number.set_fixed_size({210, 60});
    number.arrange({10, 20, 400, 100});
    require(same(number.bounds(), {10, 20, 210, 60}) &&
        same(increase->bounds(), number.increase_bounds()), "NumberBox layout uses constrained parent bounds");
    number.arrange({10, 20, 20, 16});
    require(same(number.editor_bounds(), {10, 20, 0, 16}) &&
        number.increase_bounds().x < number.decrease_bounds().x &&
        number.increase_bounds().x >= 10 && number.decrease_bounds().x + number.decrease_bounds().width <= 30 &&
        number.increase_bounds().y == 24 && number.increase_bounds().height == 8,
        "Narrow NumberBox children remain inside the field");
    require(number.spin_placement() == NumberSpinPlacement::inline_buttons, "Existing XUI NumberBox defaults remain Inline");
    number.editor()->set_text(L"uncommitted");
    auto up = std::static_pointer_cast<Button>(increase);
    up->set_focused(true); up->key_down(ActivationKey::space);
    number.set_spin_placement(NumberSpinPlacement::hidden);
    require(!up->pressed() && !up->visible() && !std::static_pointer_cast<Button>(decrease)->visible() &&
        same(number.editor_bounds(), number.bounds()) && number.increase_bounds().width == 0 &&
        number.decrease_bounds().height == 0 && number.editor()->text() == L"uncommitted" &&
        number.value() == 13 && changes == 1, "Hidden placement cancels gestures and preserves editor state and value");
    number.set_visual_style(VisualStyle::classic);
    require(number.spin_placement() == NumberSpinPlacement::hidden && same(editor->bounds(), number.bounds()),
        "Placement remains independent of theme");
    number.set_spin_placement(NumberSpinPlacement::inline_buttons);
    require(up->visible() && number.decrease_bounds().x < number.increase_bounds().x &&
        number.retained_children()[1] == decrease && number.retained_children()[2] == increase,
        "Restoring Classic Inline retains peers and decrement/increment order");
    bool rejected{};
    try { number.set_spin_placement(static_cast<NumberSpinPlacement>(99)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && number.spin_placement() == NumberSpinPlacement::inline_buttons,
        "Invalid spin placement is rejected without mutation");
}
void combo_layout() {
    ComboBox combo(L"Format", true);
    combo.set_items({{1, L"Text"}, {2, L"Markdown"}, {3, L"Other"}}, 2);
    combo.arrange({10, 20, 320, 42});
    combo.prepare_popup();
    require(same(combo.editor()->bounds(), {10, 20, 280, 42}) &&
        same(combo.drop_down_bounds(), {290, 20, 40, 42}) &&
        combo.popup()->measure({1000, 1000}).height == 102, "Classic ComboBox and popup bounds are unchanged");
    const auto editor_id = combo.editor()->id(), choices_id = combo.choices()->id();
    int changes{}, edits{};
    combo.on_change([&](auto) { ++changes; });
    combo.on_edit([&](const auto&) { ++edits; });
    combo.editor()->commit_text(L"custom");
    combo.choices()->set_selected(3);
    combo.set_visual_style(VisualStyle::winui);
    require(combo.measure({1000, 1000}).height == 32 &&
        same(combo.editor()->bounds(), {10, 20, 282, 42}) &&
        same(combo.drop_down_bounds(), {292, 20, 38, 42}), "WinUI ComboBox reserves the 38 DIP arrow column");
    require(combo.editor()->id() == editor_id && combo.choices()->id() == choices_id &&
        combo.editor()->text() == L"custom" && combo.selected() == 2 &&
        combo.choices()->selected() == 3 && changes == 0 && edits == 1,
        "Style changes preserve custom editor text, selection, and independent popup preview");
    require(combo.choices()->effective_row_pitch() == 35 &&
        combo.popup()->measure({1000, 1000}).height == 115,
        "WinUI popup includes item bodies, four DIP gaps, presenter padding and border");
    combo.prepare_popup();
    require(combo.choices()->selected() == 2, "Opening still restores committed selection");
    combo.popup()->arrange({10, 62, 320, 115});
    require(same(combo.choices()->item_bounds(0), {6, 7, 308, 31}) &&
        same(combo.choices()->item_bounds(1), {6, 42, 308, 31}) &&
        !combo.choices()->hit_test(6) && combo.choices()->hit_test(7) == 0 &&
        !combo.choices()->hit_test(38) && !combo.choices()->hit_test(41) &&
        combo.choices()->hit_test(42) == 1 &&
        combo.choices()->hit_test(107) == 2 && !combo.choices()->hit_test(108),
        "Popup hit testing excludes padding and the gaps between item bodies");
    require(combo.choices()->selected_item_bounds() &&
        same(*combo.choices()->selected_item_bounds(), {6, 42, 308, 31}),
        "Popup alignment uses the selected body center rather than the row pitch");
    combo.choices()->set_text_measurer([](std::wstring_view, TextStyle) { return Size{80, 19}; });
    combo.prepare_popup();
    combo.popup()->arrange({10, 62, 320, combo.popup()->measure({1000, 1000}).height});
    require(combo.choices()->effective_row_height() == 31 && combo.choices()->effective_row_pitch() == 35 &&
        same(combo.choices()->item_bounds(1), {6, 42, 308, 31}) &&
        combo.popup()->measure({1000, 1000}).height == 115 &&
        !combo.choices()->hit_test(38) && combo.choices()->hit_test(42) == 1,
        "Measured 19 DIP presenter plus 12 DIP padding produces 31 DIP bodies and 35 DIP pitch");
    combo.choices()->set_text_measurer([](std::wstring_view, TextStyle) { return Size{80, 25}; });
    combo.prepare_popup();
    require(combo.choices()->effective_row_height() == 37 && combo.choices()->effective_row_pitch() == 41 &&
        combo.popup()->measure({1000, 1000}).height == 133,
        "Choice geometry grows with measured text rather than fixing the body at 31 DIPs");
    combo.choices()->set_text_measurer({});
    combo.prepare_popup();
    int accepted{};
    combo.choices()->on_accept([&](auto id) { ++accepted; combo.select(id); });
    combo.choices()->step(1);
    combo.choices()->accept();
    require(combo.selected() == 3 && changes == 1 && accepted == 1,
        "WinUI popup keyboard navigation and acceptance retain their behavior");
    combo.set_visual_style(VisualStyle::classic);
    require(combo.popup()->measure({1000, 1000}).height == 102 &&
        same(combo.editor()->bounds(), {10, 20, 280, 42}) &&
        combo.selected() == 3, "Classic ComboBox geometry returns without losing selection");
    combo.set_preferred_size({200, 55});
    combo.set_visual_style(VisualStyle::winui);
    require(combo.measure({1000, 1000}).height == 55, "Explicit ComboBox dimensions are retained");
    combo.arrange({5, 6, 20, 16});
    require(same(combo.editor()->bounds(), {5, 6, 0, 16}) &&
        same(combo.drop_down_bounds(), {5, 6, 20, 16}), "Narrow ComboBox hit region remains inside its field");
    std::vector<ChoiceItem> many;
    for (std::uint64_t id = 1; id <= 20; ++id) many.push_back({id, L"Choice"});
    combo.set_items(many, 20);
    combo.prepare_popup();
    require(combo.popup()->measure({1000, 1000}).height == 504, "WinUI popup honors the template maximum dropdown height");
    combo.popup()->arrange({0, 0, 160, 80});
    const auto selected_row = combo.choices()->selected_item_bounds();
    require(selected_row && same(*selected_row, {6, 42, 148, 31}) &&
        combo.choices()->hit_test(selected_row->y + selected_row->height / 2) == 19,
        "Arranging a clipped popup automatically scrolls the selected row into view for placement");
    combo.popup()->arrange({40, 50, 160, 80});
    require(same(*combo.choices()->selected_item_bounds(), *selected_row),
        "Moving the popup preserves local selected-row alignment");
    combo.choices()->set_selected(1);
    require(same(*combo.choices()->selected_item_bounds(), {6, 7, 148, 31}) &&
        combo.choices()->hit_test(7) == 0 && combo.selected() == 20,
        "Popup selection reveals either end without changing committed ComboBox identity");
    combo.popup()->arrange({0, 0, 160, 5});
    require(!combo.choices()->selected_item_bounds(),
        "A viewport too short for any row reports no visible alignment target");
    combo.set_visual_style(VisualStyle::classic);
    require(combo.popup()->measure({1000, 1000}).height == 272, "Classic popup cap remains 272 DIPs");
    combo.set_items({});
    combo.set_visual_style(VisualStyle::winui);
    combo.prepare_popup();
    require(combo.popup()->measure({1000, 1000}).height == 45 &&
        !combo.choices()->selected_item_bounds(), "Empty WinUI popup reserves one row pitch and padding");
}
void radio_layout() {
    auto natural = std::make_shared<RadioGroup>();
    natural->set_items({{1, L"One"}, {2, L"Two"}, {3, L"Three"}});
    natural->set_visual_style(VisualStyle::winui);
    require(natural->measure({1000, 1000}).height == 96 && !natural->preferred_size_explicit(),
        "Three default WinUI radio rows measure 96 DIPs without becoming an explicit size");
    Stack form(Axis::vertical);
    form.set_spacing(0);
    auto following = std::make_shared<Button>(L"Following control");
    following->set_fixed_size({320, 32});
    form.add(natural); form.add(following);
    const auto size = form.measure({320, 1000});
    form.arrange({0, 0, size.width, size.height});
    require(natural->bounds().height == 96 && following->bounds().y == 96,
        "Following controls sit immediately after the final radio row");
    natural->set_visual_style(VisualStyle::classic);
    require(natural->measure({1000, 1000}).height == 136, "Classic keeps its constructor-default radio height");
    natural->set_visual_style(VisualStyle::winui);
    int layouts{};
    natural->set_invalidator([&](Invalidation kind) { if (kind == Invalidation::layout) ++layouts; });
    natural->set_items({{1, L"One"}, {2, L"Two"}});
    require(natural->measure({1000, 1000}).height == 64 && layouts == 1,
        "Item-count changes invalidate and recompute natural WinUI height");
    natural->set_minimum_size({0, 80});
    require(natural->measure({1000, 1000}).height == 80, "Natural radio height respects application minimums");
    natural->set_maximum_size({320, 90});
    natural->set_items({{1, L"One"}, {2, L"Two"}, {3, L"Three"}});
    require(natural->measure({1000, 1000}).height == 90 && natural->measure({1000, 70}).height == 70,
        "Natural radio height respects application maximums and available height");
    natural->set_preferred_size({200, 85});
    require(natural->measure({1000, 1000}).height == 85, "Explicit preferred radio height overrides natural sizing");
    natural->set_fixed_size({200, 110});
    require(natural->measure({1000, 1000}).height == 110, "Explicit fixed radio height overrides natural sizing");
    RadioGroup radio;
    radio.set_items({{1, L"One"}, {2, L"Disabled", false}, {3, L"Three"}, {4, L"Four"}}, 1);
    radio.arrange({10, 20, 200, 68});
    require(same(radio.item_bounds(1), {0, 34, 200, 34}) && radio.hit_test(33) == 0 &&
        radio.hit_test(34) == 1, "Classic RadioGroup row geometry is unchanged");
    radio.set_visual_style(VisualStyle::winui);
    require(radio.effective_row_height() == 32 && radio.effective_vertical_padding() == 0 &&
        same(radio.item_bounds(1), {0, 32, 200, 32}) &&
        radio.hit_test(31) == 0 && radio.hit_test(32) == 1, "WinUI radio bounds and hit testing share row geometry");
    radio.set_selected(4);
    require(radio.hit_test(32) == 3 && radio.item_bounds(3).height == 32,
        "WinUI selection reveals a complete row");
    radio.set_visual_style(VisualStyle::classic);
    require(radio.selected() == 4 && radio.hit_test(34) == 3 &&
        radio.item_bounds(3).height == 34, "Classic selected row is revealed after a style switch");
    radio.arrange({0, 0, 200, 32});
    radio.set_visual_style(VisualStyle::winui);
    require(radio.hit_test(0) == 3 && radio.item_bounds(3).y == 0,
        "Single-row viewport retains and reveals the selected item");
    require(!radio.hit_test(-1) && !radio.hit_test(32) &&
        !radio.hit_test(std::numeric_limits<float>::quiet_NaN()), "Radio hit testing rejects out-of-bounds input");
    radio.set_preferred_size({210, 75});
    require(radio.measure({1000, 1000}).height == 75, "Explicit RadioGroup size is retained");
    radio.step(1);
    require(radio.selected() == 1, "Radio keyboard selection still wraps");
    radio.step(1);
    require(radio.selected() == 3, "Radio keyboard selection still skips disabled rows");
}
void expander_layout() {
    auto natural_child = std::make_shared<Element>();
    natural_child->set_preferred_size({100, 19});
    Expander natural(L"Measured header", natural_child);
    natural.set_visual_style(VisualStyle::winui);
    natural.set_text_measurer([](std::wstring_view, TextStyle) { return Size{151, 19}; });
    const auto desired = natural.measure({1000, 1000});
    require(desired.width == 229 && desired.height == 100 && !natural.preferred_size_explicit(),
        "Natural WinUI Expander measures header width and 48 + 19 + 32 padding + 1 border height");
    natural.arrange({10, 20, desired.width, desired.height});
    require(same(natural.content_bounds(), {27, 84, 195, 19}) &&
        same(natural_child->bounds(), natural.content_bounds()),
        "WinUI body content uses 16 DIP padding inside the side and bottom borders");
    natural.set_expanded(false);
    require(natural.measure({1000, 1000}).width == 229 && natural.measure({1000, 1000}).height == 48,
        "Collapsed natural Expander measures only header content and chrome");
    natural.set_expanded(true);
    natural.set_minimum_size({260, 130});
    require(natural.measure({1000, 1000}).width == 260 && natural.measure({1000, 1000}).height == 130,
        "Natural Expander respects application minimum dimensions");
    natural.set_maximum_size({200, 90});
    require(natural.measure({1000, 1000}).width == 200 && natural.measure({1000, 1000}).height == 90,
        "Natural Expander respects application maximum dimensions");
    natural.set_fixed_size({280, 120});
    require(natural.measure({1000, 1000}).width == 280 && natural.measure({1000, 1000}).height == 120,
        "Explicit fixed Expander outer dimensions override natural measurement");
    auto child = std::make_shared<TextInput>(L"Content");
    Expander expander(L"Details", child);
    expander.arrange({10, 20, 300, 180});
    require(same(expander.header_bounds(), {10, 20, 300, 38}) &&
        same(child->bounds(), {10, 58, 300, 142}), "Classic Expander header and content bounds are unchanged");
    int changed{};
    expander.on_change([&](bool) { ++changed; });
    expander.set_visual_style(VisualStyle::winui);
    require(same(expander.header_bounds(), {10, 20, 300, 48}) &&
        same(child->bounds(), {27, 84, 266, 99}) && expander.expanded() && !changed,
        "WinUI Expander pads content below the header without changing expansion");
    expander.set_expanded(false);
    require(expander.measure({1000, 1000}).height == 48, "Collapsed WinUI Expander measures its header");
    expander.arrange({10, 20, 300, 48});
    require(child->bounds().height == 0 && child->bounds().width == 0,
        "Collapsed content is excluded from layout");
    expander.set_visual_style(VisualStyle::classic);
    require(expander.measure({1000, 1000}).height == 38 && !expander.expanded() && !changed,
        "Classic collapsed height returns without expansion callbacks");
    expander.set_visual_style(VisualStyle::winui);
    expander.set_preferred_size({250, 65});
    require(expander.measure({1000, 1000}).height == 65, "Explicit WinUI Expander dimensions are retained");
    expander.invoke();
    require(expander.expanded() && changed == 1, "Expander retains its semantic action");
    expander.arrange({10, 20, 100, 20});
    require(same(expander.header_bounds(), {10, 20, 100, 20}) &&
        same(child->bounds(), {27, 40, 66, 0}), "Short Expander clips header and content to available height");
    expander.set_text_measurer([](std::wstring_view, TextStyle) { return Size{80, 72}; });
    expander.measure({1000, 1000});
    expander.arrange({10, 20, 200, 150});
    require(expander.effective_header_height() == 72 &&
        same(expander.header_bounds(), {10, 20, 200, 72}) &&
        same(child->bounds(), {27, 108, 166, 45}), "WinUI header height is a minimum and grows with measured content");
    expander.set_visual_style(VisualStyle::classic);
    require(expander.effective_header_height() == 38, "Classic header stays unchanged with a tall text measurer");
}
}
int main() {
    try {
        numeric_layout(); combo_layout(); radio_layout(); expander_layout();
        std::cout << "WinUI form layout contracts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
