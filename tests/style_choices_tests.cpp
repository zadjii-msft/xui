#include "xui/documents.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <source_location>

namespace { std::atomic<std::size_t> allocations{}; }
void* operator new(std::size_t n) {
    ++allocations;
    if (auto* result = std::malloc(n ? n : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F operation, const std::source_location source = std::source_location::current()) {
    bool rejected{};
    try { operation(); } catch (const std::invalid_argument&) { rejected = true; }
    if (!rejected) throw std::runtime_error(std::string("Expected invalid_argument at ") +
        source.file_name() + ":" + std::to_string(source.line()));
}
PartStyleValues background(uint32_t color) {
    PartStyleValues value; value.background = ThemeColor{color}; return value;
}
PartStyleValues foreground(uint32_t color) {
    PartStyleValues value; value.foreground = ThemeColor{color}; return value;
}
uint32_t fill(const Element& element, StylePart part) {
    const auto* value = element.effective_control_style_values(part);
    require(value && value->background.has_value(), "Expected effective fill");
    return value->background->light;
}
void catalog() {
    for (auto target : {StyleTarget::radio_group, StyleTarget::choice_list, StyleTarget::combo_box,
        StyleTarget::numeric_input, StyleTarget::range_input, StyleTarget::progress, StyleTarget::inline_status,
        StyleTarget::color_picker}) {
        const auto* schema = choices_status_style_schema(target);
        require(schema != nullptr && !schema->parts.empty(), "Every choice/status target has a schema");
        for (const auto& part : schema->parts) if (part.allowed & style_property(StyleProperty::vertical_alignment)) {
            PartStyleValues alignment; alignment.vertical_alignment = StyleAlignment::stretch;
            rejects([&] { ControlStyle::create(target, {{part.part, alignment}}, {}); });
            alignment.vertical_alignment = StyleAlignment::end;
            alignment.horizontal_alignment = StyleAlignment::stretch;
            validate_part_values(target, part.part, alignment);
        }
        ControlStyle::create(target, {{StylePart::root, background(0x123456)}}, {});
        rejects([&] { ControlStyle::create(target, {{StylePart::plot, background(0)}}, {}); });
    }
    PartStyleValues metric; metric.row_height = 0;
    rejects([&] { validate_part_values(StyleTarget::radio_group, StylePart::root, metric); });
    metric = {}; metric.size = -1;
    rejects([&] { validate_part_values(StyleTarget::range_input, StylePart::thumb, metric); });
    metric.size = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { validate_part_values(StyleTarget::range_input, StylePart::thumb, metric); });
    PartStyleValues row_metric; row_metric.row_height = 48;
    rejects([&] { ControlStyle::create(StyleTarget::radio_group, {},
        {{StylePart::item, style_states::selected, row_metric}}); });
    rejects([&] { ControlStyle::create(StyleTarget::choice_list, {},
        {{StylePart::root, style_states::selected, row_metric}}); });
    rejects([&] { ControlStyle::create(StyleTarget::progress, {}, {{StylePart::fill, style_states::selected, background(1)}}); });
    rejects([&] { ControlStyle::create(StyleTarget::color_picker, {{StylePart::swatch, background(1)}}, {}); });
}
void choices() {
    RadioGroup radio;
    radio.set_items({{71, L"First"}, {92, L"Second"}, {105, L"Disabled", false}}, 71);
    PartStyleValues root; root.row_height = 40; root.spacing = 6; root.padding = Insets{9, 7, 13, 11};
    auto style = ControlStyle::create(StyleTarget::radio_group,
        {{StylePart::root, root}, {StylePart::label, foreground(0x102030)}},
        {{StylePart::label, style_states::selected, foreground(0x112233)},
         {StylePart::label, style_states::hovered, foreground(0x334455)},
         {StylePart::label, style_states::disabled, foreground(0x556677)}});
    radio.set_control_style(style); radio.arrange({0, 0, 240, 160});
    require(radio.item_bounds(0).x == 9 && radio.item_bounds(0).y == 10 &&
        radio.item_bounds(0).width == 218 && radio.item_bounds(1).y == 56, "Authored rows and unequal padding define geometry");
    require(radio.hit_test(12) == 0 && !radio.hit_test(52) && radio.hit_test(59) == 1,
        "Choice hit testing excludes row spacing");
    radio.pointer_move(true);
    require(radio.item_style_values(StylePart::label, 1).foreground->light == 0x102030,
        "Container hover is not a hover on every row");
    require(radio.item_style_values(StylePart::label, 1, true).foreground->light == 0x334455,
        "Only the actual hovered row receives hover");
    require(radio.item_style_values(StylePart::label, 2, true, true).foreground->light == 0x556677,
        "Item disabled wins over pointer state");
    int changed{};
    radio.on_change([&](auto id) { require(id == 92, "Callbacks retain stable IDs"); ++changed; });
    radio.select(92);
    require(changed == 1 && radio.selected() == 92 &&
        radio.item_style_values(StylePart::label, 1).foreground->light == 0x112233, "Selection remains model-owned");
    radio.set_control_style_values(StylePart::label, foreground(0xaabbcc));
    radio.set_control_style(style);
    require(radio.item_style_values(StylePart::label, 1).foreground->light == 0xaabbcc, "Locals override immutable styles");
    radio.set_control_style_values(StylePart::label, {});
    const auto before = allocations.load();
    for (unsigned n = 0; n < 256; ++n) {
        radio.pointer_move(n % 2 != 0);
        for (unsigned i = 0; i < 3; ++i) {
            const auto row = radio.item_style_values(StylePart::label, i, i == n % 3);
            require(row.foreground.has_value(), "Visible rows resolve an effective foreground");
            radio.item_bounds(i);
        }
    }
    require(allocations.load() == before, "Warmed choice states and visible rows allocate no style objects");
    radio.set_control_style(nullptr);
    require(!radio.has_control_styling() && radio.selected() == 92, "Clearing restores the default path without model changes");
    RadioGroup list(L"List", true); list.set_items(radio.items());
    list.set_control_style(ControlStyle::create(StyleTarget::choice_list, {{StylePart::selected_marker, background(7)}}, {}));
    require(list.item_style_values(StylePart::selected_marker, 0).background->light == 7, "ChoiceList has its own marker schema");
    list.set_control_style(ControlStyle::create(StyleTarget::choice_list, {{StylePart::root, foreground(0x102030)}},
        {{StylePart::root, style_states::hovered, foreground(0x334455)},
         {StylePart::label, style_states::hovered, foreground(0x778899)},
         {StylePart::root, style_states::disabled, foreground(0x556677)}}));
    list.pointer_move(true);
    require(list.item_style_values(StylePart::label, 0).foreground->light == 0x334455 &&
        list.item_style_values(StylePart::label, 1).foreground->light == 0x334455,
        "Authored root hover deliberately changes the inherited foreground for all unoverridden rows");
    require(list.item_style_values(StylePart::label, 1, true).foreground->light == 0x778899 &&
        list.item_style_values(StylePart::label, 0).foreground->light == 0x334455,
        "Part hover overrides global inheritance only for the actual hovered row");
    list.set_enabled(false);
    require(list.item_style_values(StylePart::label, 0).foreground->light == 0x556677,
        "Owner disabled state changes inherited root presentation");
    RadioGroup metrics;
    metrics.set_items({{1, L"First"}, {2, L"Second"}});
    PartStyleValues row; row.row_height = 40;
    PartStyleValues hovered_row; hovered_row.row_height = 50;
    PartStyleValues indicator; indicator.size = 12;
    PartStyleValues hovered_indicator; hovered_indicator.size = 24;
    PartStyleValues hovered_item; hovered_item.padding = Insets{20, 2, 11, 2};
    metrics.set_control_style(ControlStyle::create(StyleTarget::radio_group,
        {{StylePart::root, row}, {StylePart::indicator, indicator}},
        {{StylePart::root, style_states::hovered, hovered_row},
         {StylePart::indicator, style_states::hovered, hovered_indicator},
         {StylePart::item, style_states::hovered, hovered_item}}));
    metrics.arrange({0, 0, 240, 140});
    require(metrics.item_bounds(0).height == 40 && metrics.indicator_bounds(0).width == 12,
        "Base uniform row and indicator metrics define actual geometry");
    require(metrics.indicator_bounds(0, true).width == 24 && metrics.label_bounds(0, true).x == 52,
        "Per-item indicator size and internal padding alter shared paint geometry");
    metrics.pointer_move(true);
    require(metrics.item_bounds(0).height == 50 && metrics.item_bounds(1).y == 50 && metrics.hit_test(55) == 1,
        "Control-wide state row metrics update uniform row geometry and hit testing together");
    PartStyleValues local_row; local_row.row_height = 60;
    metrics.set_control_style_values(StylePart::root, local_row);
    require(metrics.item_bounds(1).y == 60, "Local uniform row metric overrides control-wide state");
    RadioGroup defaults;
    defaults.set_visual_style(VisualStyle::winui);
    defaults.set_items({{1, L"First"}, {2, L"Second"}});
    defaults.arrange({0, 0, 160, 64});
    for (const bool styled : {false, true}) {
        if (styled) {
            PartStyleValues shape;
            shape.corner_radius = 16.0f;
            defaults.set_control_style_values(StylePart::item, shape);
        }
        const auto circle = defaults.indicator_bounds(0), label = defaults.label_bounds(0);
        require(circle.x == 0 && circle.y == 6.5f && circle.width == 19 &&
            label.x == 28 && label.y == 0 && label.width == 132 && label.height == 32,
            "WinUI radio default geometry does not change when an item corner style is attached");
    }
}
void ranges() {
    RangeInput range;
    range.arrange({0, 0, 200, 100});
    PartStyleValues track; track.thickness = 10;
    PartStyleValues thumb; thumb.size = 40;
    range.set_control_style(ControlStyle::create(StyleTarget::range_input,
        {{StylePart::track, track}, {StylePart::thumb, thumb}, {StylePart::fill, background(1)}},
        {{StylePart::fill, style_states::minimum, background(2)},
         {StylePart::fill, style_states::maximum, background(3)},
         {StylePart::fill, style_states::dragging, background(4)}}));
    require(fill(range, StylePart::fill) == 2, "Minimum comes from the numeric range");
    range.set_value(25);
    auto geometry = range.slider_geometry();
    require(geometry.track.x == 20 && geometry.track.width == 160 && geometry.track.height == 10 &&
        geometry.thumb.width == 40 && geometry.thumb.x == 40, "Track thickness and thumb size change shared geometry");
    require(std::abs(geometry.pointer_fraction({60, 50}, Axis::horizontal) - .25) < 1e-6, "Pointer and thumb agree");
    range.set_orientation(Axis::vertical); range.set_reversed(true);
    geometry = range.slider_geometry();
    const Point center{geometry.thumb.x + 20, geometry.thumb.y + 20};
    require(std::abs(geometry.pointer_fraction(center, Axis::vertical) - .75) < 1e-6,
        "Vertical reversal is applied exactly once");
    range.begin_drag(geometry.pointer_fraction(center, Axis::vertical));
    require(range.preview_value() == 25 && fill(range, StylePart::fill) == 4, "Dragging state is real, even without value movement");
    range.cancel();
    range.set_value(100);
    require(fill(range, StylePart::fill) == 3, "Maximum state follows committed value");
    const auto before = allocations.load();
    for (unsigned n = 0; n < 100; ++n) { range.set_value(n); range.slider_geometry(); }
    require(allocations.load() == before, "Warmed slider state and geometry allocate nothing");
    range.set_control_style(nullptr);
    require(range.slider_geometry().thumb.width == 16 && range.value() == 99, "Clear restores default thumb without changing value");
    range.set_visual_style(VisualStyle::winui);
    range.set_orientation(Axis::horizontal); range.set_reversed(false); range.set_value(25);
    PartStyleValues root; root.padding = Insets{10, 20, 30, 40};
    range.set_control_style_values(StylePart::root, root);
    geometry = range.slider_geometry();
    require(geometry.track.x == 10 && geometry.track.y == 34 && geometry.track.width == 160,
        "WinUI rail fills padded content and retains its leading cross-axis slot");
    const Point winui_center{geometry.thumb.x + geometry.thumb.width / 2, geometry.thumb.y + geometry.thumb.height / 2};
    require(std::abs(geometry.pointer_fraction(winui_center, Axis::horizontal) - .25) < 1e-6 &&
        geometry.pointer_fraction({0, 36}, Axis::horizontal) == 0 &&
        geometry.pointer_fraction({200, 36}, Axis::horizontal) == 1,
        "Padded WinUI geometry maps thumb centers and clamps endpoint clicks");
    int previews{}, changes{}, cancellations{};
    range.on_preview([&](double) { ++previews; });
    range.on_change([&](double) { ++changes; });
    range.on_cancel([&](double value) { require(value == 25, "User cancellation reports the committed value"); ++cancellations; });
    require(range.begin_drag(geometry.pointer_fraction({0, 36}, Axis::horizontal)) &&
        range.drag(geometry.pointer_fraction({200, 36}, Axis::horizontal)) &&
        range.preview_value() == 100 && range.value() == 25 && previews == 2 && changes == 0,
        "WinUI endpoint travel previews values without an early commit");
    range.cancel_drag();
    require(!range.dragging() && range.value() == 25 && cancellations == 1,
        "User cancellation restores the value after endpoint movement");
    range.begin_drag(geometry.pointer_fraction({200, 36}, Axis::horizontal));
    require(range.commit_drag() && range.value() == 100 && changes == 1,
        "WinUI endpoint release commits exactly once");
    range.begin_drag(geometry.pointer_fraction({0, 36}, Axis::horizontal));
    range.set_value(25);
    require(!range.dragging() && range.value() == 25 && changes == 1 && cancellations == 1,
        "Property-driven endpoint cancellation remains silent");
    rejects([] { slider_visual({100, 30}, Axis::horizontal, false, .5, VisualStyle::classic, -1); });
}
void composites() {
    ComboBox combo(L"Choice", true);
    combo.set_items({{19, L"A"}, {38, L"B"}});
    const auto editor = combo.editor(); const auto popup = combo.popup(); const auto choices = combo.choices();
    PartStyleValues header; header.header_height = 28; header.foreground = ThemeColor{0x112233};
    PartStyleValues arrow; arrow.size = 60;
    combo.set_control_style(ControlStyle::create(StyleTarget::combo_box,
        {{StylePart::header, header}, {StylePart::arrow, arrow}, {StylePart::field, background(1)}},
        {{StylePart::field, style_states::open, background(2)}, {StylePart::field, style_states::empty, background(3)}}));
    combo.arrange({0, 0, 200, 80});
    require(combo.editor_bounds().y == 28 && combo.editor_bounds().width == 140, "Header and arrow metrics arrange native editor");
    popup->opened(); require(fill(combo, StylePart::field) == 2, "Popup supplies the open state");
    popup->closed(PopupDismissReason::cancel);
    editor->set_text(L""); require(fill(combo, StylePart::field) == 3, "Native editable text supplies empty state");
    combo.set_control_style(nullptr);
    require(combo.editor() == editor && combo.popup() == popup && combo.choices() == choices &&
        combo.selected() == 19, "Combo styling preserves retained children and committed identity");
    NumericInput numeric;
    const auto input = numeric.editor(); const auto minus = numeric.decrease_button(); const auto plus = numeric.increase_button();
    numeric.set_control_style(ControlStyle::create(StyleTarget::numeric_input, {{StylePart::field, background(1)}},
        {{StylePart::field, style_states::minimum, background(2)}, {StylePart::field, style_states::maximum, background(3)},
         {StylePart::field, style_states::invalid, background(4)}}));
    require(fill(numeric, StylePart::field) == 2, "Numeric minimum is model-owned");
    numeric.commit_text(L"invalid"); require(fill(numeric, StylePart::field) == 4, "Invalid native text drives invalid style");
    numeric.set_value(100); require(fill(numeric, StylePart::field) == 3, "Numeric maximum is model-owned");
    numeric.set_control_style_values(StylePart::header, header); numeric.arrange({0, 0, 250, 80});
    require(numeric.editor_bounds().y == 28 && numeric.decrease_bounds().y == 28, "Numeric header affects editor and button geometry");
    numeric.set_control_style(nullptr); numeric.set_control_style_values(StylePart::header, {});
    require(numeric.editor() == input && numeric.decrease_button() == minus && numeric.increase_button() == plus,
        "Numeric styling retains native input and Button identities");
    minus->invoke();
    require(numeric.value() == 99, "Retained numeric Button callbacks remain connected");
}
void status() {
    Progress progress;
    const ProgressState states[]{ProgressState::determinate, ProgressState::indeterminate, ProgressState::paused,
        ProgressState::error, ProgressState::unknown};
    const StyleStateMask masks[]{style_states::determinate, style_states::indeterminate, style_states::paused,
        style_states::error, style_states::unknown};
    std::vector<StyleRule> rules;
    for (unsigned i = 0; i < 5; ++i) rules.push_back({StylePart::fill, masks[i], background(i + 1)});
    progress.set_control_style(ControlStyle::create(StyleTarget::progress, {}, rules));
    for (unsigned i = 0; i < 5; ++i) {
        progress.set_state(states[i]); require(fill(progress, StylePart::fill) == i + 1, "Every progress state comes from the model");
    }
    progress.set_capacity(2, 10);
    require(fill(progress, StylePart::fill) == 1 && progress.value() == 2, "Capacity restores static determinate state");
    InlineStatus status;
    const auto action = status.action_button(); const auto dismiss = status.dismiss_button();
    int activated{}; status.set_action(L"Retry", [&] { ++activated; });
    status.set_control_style(ControlStyle::create(StyleTarget::inline_status, {{StylePart::stripe, background(1)}},
        {{StylePart::stripe, style_states::error, background(2)}, {StylePart::stripe, style_states::dismissed, background(3)}}));
    status.set_message(L"Failure", StatusSeverity::error);
    require(fill(status, StylePart::stripe) == 2, "Severity remains semantic and drives styling");
    status.set_dismissible(true); status.dismiss();
    require(!status.visible() && fill(status, StylePart::stripe) == 3, "Dismissed styles cannot expose hidden content");
    status.show();
    require(status.action_button() == action && status.dismiss_button() == dismiss, "Status Buttons retain identity");
    action->invoke(); require(activated == 1, "Status action callback survives style state transitions");
    for (auto severity : {StatusSeverity::information, StatusSeverity::success, StatusSeverity::warning, StatusSeverity::error}) {
        status.set_message(L"Message", severity);
        require(fill(status, StylePart::stripe) == (severity == StatusSeverity::error ? 2u : 1u),
            "Every severity resolves without unsupported or fabricated state");
    }
    ColorPicker picker;
    const auto channels = picker.channels(); const auto swatches = picker.swatches();
    const auto swatch = picker.swatch_button(0);
    const RgbaColor color{2, 4, 8, 16}; picker.set_value(color);
    auto picker_root = background(1); picker_root.foreground = ThemeColor{0x112233};
    picker.set_control_style(ControlStyle::create(StyleTarget::color_picker, {{StylePart::root, picker_root}},
        {{StylePart::root, style_states::invalid, background(2)},
         {StylePart::channel_label, style_states::invalid, foreground(0xcc2233)}}));
    channels[0]->commit_text(L"invalid");
    require(fill(picker, StylePart::root) == 2, "Invalid channels drive picker state");
    for (std::size_t i = 0; i < channels.size(); ++i)
        require(picker.channel_label_style_values(i).foreground->light == (i == 0 ? 0xcc2233u : 0x112233u),
            "A direct invalid label rule applies only to the corresponding invalid channel");
    const auto before_labels = allocations.load();
    for (unsigned refresh = 0; refresh < 128; ++refresh)
        for (std::size_t i = 0; i < channels.size(); ++i) picker.channel_label_style_values(i);
    require(allocations.load() == before_labels, "Channel label state refresh allocates no styles or attachments");
    channels[0]->set_value(color.red);
    require(fill(picker, StylePart::root) == 1, "Picker aggregate invalid state clears after channel recovery");
    for (std::size_t i = 0; i < channels.size(); ++i)
        require(picker.channel_label_style_values(i).foreground->light == 0x112233,
            "Recovered and unaffected channel labels retain their ordinary inherited foreground");
    picker.set_control_style(nullptr);
    require(picker.value() == color && picker.swatches() == swatches && picker.channels() == channels,
        "Styling preserves authored RGBA, swatches, and retained channels");
    require(picker.swatch_button(0) == swatch, "Swatch Button identity survives style replacement");
    swatch->invoke(); require(picker.value() == swatches[0], "Retained swatch callbacks still select authored data");
}
}
int main() {
    try { catalog(); choices(); ranges(); composites(); status(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Choice, range, composite, progress, status, and color styling contracts passed\n";
}
