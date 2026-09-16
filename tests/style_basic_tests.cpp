#include "xui/controls.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Unsupported basic style combination was accepted");
}
void schema_cells() {
    struct Property { StyleProperty id; void (*set)(PartStyleValues&); };
    const Property properties[]{
        {StyleProperty::background, [](auto& v) { v.background = ThemeColor{0x123456}; }},
        {StyleProperty::foreground, [](auto& v) { v.foreground = ThemeColor{0x123456}; }},
        {StyleProperty::border_brush, [](auto& v) { v.border_brush = ThemeColor{0x123456}; }},
        {StyleProperty::border_thickness, [](auto& v) { v.border_thickness = Insets{0, 2, 4, 6}; }},
        {StyleProperty::padding, [](auto& v) { v.padding = Insets{0, 2, 4, 6}; }},
        {StyleProperty::corner_radius, [](auto& v) { v.corner_radius = 0.0f; }},
        {StyleProperty::size, [](auto& v) { v.size = 0.0f; }},
        {StyleProperty::font_family, [](auto& v) { v.font_family = make_style_font_family("Segoe UI"); }},
        {StyleProperty::font_size, [](auto& v) { v.font_size = 18.0f; }},
        {StyleProperty::font_weight, [](auto& v) { v.font_weight = 650; }},
        {StyleProperty::font_style, [](auto& v) { v.font_style = StyleFontStyle::italic; }},
        {StyleProperty::horizontal_alignment, [](auto& v) { v.horizontal_alignment = StyleAlignment::end; }},
        {StyleProperty::vertical_alignment, [](auto& v) { v.vertical_alignment = StyleAlignment::start; }},
        {StyleProperty::spacing, [](auto& v) { v.spacing = 2.0f; }},
        {StyleProperty::row_height, [](auto& v) { v.row_height = 20.0f; }},
        {StyleProperty::header_height, [](auto& v) { v.header_height = 20.0f; }},
        {StyleProperty::indentation, [](auto& v) { v.indentation = 2.0f; }},
        {StyleProperty::thickness, [](auto& v) { v.thickness = 2.0f; }},
        {StyleProperty::width, [](auto& v) { v.width = 2.0f; }},
        {StyleProperty::height, [](auto& v) { v.height = 2.0f; }},
        {StyleProperty::row_gap, [](auto& v) { v.row_gap = 2.0f; }},
        {StyleProperty::column_gap, [](auto& v) { v.column_gap = 2.0f; }},
        {StyleProperty::maximum_lines, [](auto& v) { v.maximum_lines = 2; }},
        {StyleProperty::wrapping, [](auto& v) { v.wrapping = true; }},
    };
    for (auto target : {StyleTarget::label, StyleTarget::button, StyleTarget::toggle}) {
        const auto* schema = basic_text_style_schema(target);
        require(schema && schema->parts.size() == 4, "Basic family schema has four real presentation parts");
        for (const auto& part : schema->parts) {
            if (!(part.allowed & style_property(StyleProperty::vertical_alignment))) continue;
            PartStyleValues alignment;
            alignment.vertical_alignment = StyleAlignment::stretch;
            rejects([&] { validate_part_values(target, part.part, alignment); });
            alignment.vertical_alignment.reset();
            alignment.horizontal_alignment = StyleAlignment::stretch;
            validate_part_values(target, part.part, alignment);
        }
        for (const auto& part : schema->parts) for (const auto& property : properties) {
            PartStyleValues value;
            property.set(value);
            const bool allowed = (part.allowed & style_property(property.id)) != 0;
            if (!allowed) { rejects([&] { validate_part_values(target, part.part, value); }); continue; }
            validate_part_values(target, part.part, value);
            for (unsigned bit = 0; bit < 43; ++bit) {
                const auto state = StyleStateMask{1} << bit;
                if (part.states & state) {
                    const auto style = ControlStyle::create(target, {}, {{part.part, state, value}});
                    require(style->resolve(part.part, state).has_value(), "Every implemented state/property cell resolves");
                } else rejects([&] { ControlStyle::create(target, {}, {{part.part, state, value}}); });
            }
        }
        rejects([&] { ControlStyle::create(target, {}, {{StylePart::root, 0, {}}}); });
        rejects([&] { ControlStyle::create(target, {}, {{StylePart::root, style_states::disabled | style_states::hovered, {}}}); });
    }
}
void label_values_and_metrics() {
    Label label(L"Caption");
    PartStyleValues root;
    root.font_family = make_style_font_family("Segoe UI");
    root.font_size = 20.0f;
    root.horizontal_alignment = StyleAlignment::end;
    root.vertical_alignment = StyleAlignment::start;
    root.foreground = ThemeColor{0x123456};
    root.padding = Insets{1, 2, 3, 4};
    root.border_thickness = Insets{0, 1, 2, 3};
    label.set_control_style(ControlStyle::create(StyleTarget::label, {{StylePart::root, root}}, {}));
    label.set_caption(true);
    const auto* inherited = label.effective_control_style_values(StylePart::caption);
    require(inherited && inherited->font_family == root.font_family && inherited->font_size == 20,
        "Caption inherits immutable root font values through label");
    require(inherited->horizontal_alignment == StyleAlignment::end && inherited->vertical_alignment == StyleAlignment::start,
        "Caption inherits root text alignment through the same model and rendering values");
    int measures{};
    label.set_text_measurer([&](auto, auto) {
        ++measures;
        const auto* values = label.effective_control_style_values(label.text_part());
        const float size = values && values->font_size ? *values->font_size : 10;
        return Size{size * 2, size};
    });
    require(label.measure({500, 500}).width == 46, "Label measurement includes unequal borders and padding");
    label.measure({500, 500});
    require(measures == 1, "Unchanged typography reuses native text metrics");
    PartStyleValues local;
    local.font_size = 30.0f; local.foreground = ThemeColor{0xabcdef};
    label.set_control_style_values(StylePart::caption, local);
    require(label.measure({500, 500}).width == 66 && measures == 2, "Part font update invalidates native measurement");
    label.set_control_style({});
    require(label.effective_control_style_values(StylePart::caption)->font_size == 30,
        "Part local typography survives source clearing");
    label.set_control_style_values(StylePart::caption, {});
    require(!label.has_control_styling(), "Clearing the final local releases styling storage");

    PartStyleValues wrapping;
    wrapping.wrapping = true; wrapping.maximum_lines = 2;
    label.set_control_style_values(StylePart::root, wrapping);
    require(label.wrapping() && label.maximum_lines() == 2, "Root wrapping reaches caption");
    label.set_wrapping(false, 0);
    require(!label.wrapping() && label.maximum_lines() == 0, "Explicit wrapping remains authoritative");
    label.set_preferred_size({90, 50});
    require(label.measure({500, 500}).width == 90, "Authored label geometry stays authoritative");
}
void buttons_and_toggle() {
    Button button(L"Action");
    button.set_text_measurer([](auto, auto) { return Size{40, 16}; });
    const auto baseline = button.measure({500, 500});
    PartStyleValues text; text.foreground = ThemeColor{0x123456};
    button.set_control_style(ControlStyle::create(StyleTarget::button, {{StylePart::label, text}}, {}));
    const auto paint_only = button.measure({500, 500});
    require(paint_only.width == baseline.width && paint_only.height == baseline.height,
        "Paint-only button styling preserves default metrics");
    ButtonStyleValues legacy;
    legacy.padding = Insets{1, 2, 3, 4}; legacy.foreground = ThemeColor{0xabcdef};
    button.set_style_values(legacy);
    button.set_style(ButtonStyle::create({}, {}));
    button.set_style({});
    require(button.style_values().padding.has_value(), "Legacy locals survive legacy source clearing");
    button.set_behavior(ButtonBehavior::dropdown);
    PartStyleValues arrow; arrow.size = 24.0f; arrow.padding = Insets{2, 1, 3, 1};
    button.set_control_style_values(StylePart::arrow, arrow);
    const auto area = button.dropdown_bounds({0, 0, 100, 40});
    require(area.width == 24, "Dropdown geometry uses the authored indicator extent");
    PartStyleValues icon; icon.size = 30.0f;
    button.set_icon(ButtonIcon::add);
    button.set_control_style_values(StylePart::icon, icon);
    require(button.measure({500, 500}).width == 65, "Icon and dropdown metrics include legacy padding and borders");
    button.set_behavior(ButtonBehavior::toggle);
    PartStyleValues checked; checked.font_size = 24.0f;
    button.set_control_style(ControlStyle::create(StyleTarget::button, {},
        {{StylePart::label, style_states::checked, checked}}));
    button.set_checked(true);
    require(button.effective_control_style_values(StylePart::label)->font_size == 24, "Checked model drives Button typography");
    button.set_enabled(false);
    require(!button.invoke() && button.checked(), "Style states preserve disabled activation and checked semantics");
    button.set_control_style({});
    require(button.control_style_values(StylePart::icon).size == 30 && button.style_values().padding.has_value(),
        "Generic and legacy locals survive independent source clearing");

    Toggle toggle(L"Toggle");
    PartStyleValues root; root.font_size = 16.0f;
    PartStyleValues indicator; indicator.size = 28.0f; indicator.border_thickness = Insets{0, 2, 4, 6};
    PartStyleValues pressed; pressed.font_size = 22.0f;
    PartStyleValues disabled; disabled.font_size = 12.0f;
    toggle.set_style(ControlStyle::create(StyleTarget::toggle,
        {{StylePart::root, root}, {StylePart::indicator, indicator}},
        {{StylePart::label, style_states::pressed, pressed}, {StylePart::label, style_states::disabled, disabled}}));
    toggle.pointer_move(true); require(toggle.pointer_down(), "Toggle accepts its existing pointer interaction");
    require(toggle.effective_style_values(StylePart::label)->font_size == 22, "Pressed typography resolves");
    toggle.set_enabled(false);
    require(toggle.effective_style_values(StylePart::label)->font_size == 12, "Disabled typography has precedence");
    const auto mark = toggle.mark_bounds({0, 0, 100, 60});
    require(mark.width == 24 && mark.height == 20, "Toggle mark respects unequal indicator borders");
}
void button_projection_precedence() {
    Button button(L"Projected child");
    PartStyleValues projected_root, projected_label;
    projected_root.background = ThemeColor{1}; projected_root.foreground = ThemeColor{2};
    projected_label.foreground = ThemeColor{3}; projected_label.font_size = 18.0f;
    button.set_control_style_projection(StylePart::root, projected_root);
    button.set_control_style_projection(StylePart::label, projected_label);
    require(button.content_style_values(StylePart::label).foreground == ThemeColor{3},
        "Projected label overrides projected root defaults");
    ButtonStyleValues legacy;
    legacy.background = ThemeColor{4}; legacy.foreground = ThemeColor{5};
    button.set_style(ButtonStyle::create(legacy));
    require(button.surface_style_values().background == ThemeColor{4} &&
        button.content_style_values(StylePart::label).foreground == ThemeColor{5},
        "Legacy child definitions override projected root and label defaults");
    PartStyleValues own_root;
    own_root.background = ThemeColor{6}; own_root.foreground = ThemeColor{7}; own_root.font_size = 30.0f;
    button.set_control_style(ControlStyle::create(StyleTarget::button, {{StylePart::root, own_root}}, {}));
    require(button.surface_style_values().background == ThemeColor{6} &&
        button.content_style_values(StylePart::label).font_size == 30,
        "Own generic root values override projected typography");
    ButtonStyleValues legacy_local;
    legacy_local.foreground = ThemeColor{8};
    button.set_style_values(legacy_local);
    require(button.content_style_values(StylePart::label).foreground == ThemeColor{8},
        "Explicit legacy child locals override inherited generic root style");
    PartStyleValues label_local;
    label_local.foreground = ThemeColor{9}; label_local.font_size = 40.0f;
    button.set_control_style_values(StylePart::label, label_local);
    require(button.content_style_values(StylePart::label).foreground == ThemeColor{9} &&
        button.content_style_values(StylePart::label).font_size == 40,
        "Explicit child label locals win over all defaults");
    button.set_control_style_projection(StylePart::root, {});
    button.set_control_style_projection(StylePart::label, {});
    require(button.style() && button.control_style() && button.style_values().foreground == ThemeColor{8} &&
        button.control_style_values(StylePart::label).font_size == 40,
        "Removing parent projection preserves both child style definitions and locals");
}
void restored_auto_size() {
    Label label(L"Label");
    Button button(L"Button");
    Toggle toggle(L"Toggle");
    for (auto* control : {static_cast<Control*>(&label), static_cast<Control*>(&button), static_cast<Control*>(&toggle)}) {
        control->set_text_measurer([](std::wstring_view text, TextStyle) {
            return Size{static_cast<float>(text.size()) * 7.0f, 18.0f};
        });
        control->set_preferred_size({90, 50});
        require(control->measure({1000, 1000}).width == 90, "Preferred size disables automatic measurement");
        control->set_auto_size(true);
        control->set_minimum_size({180, 40});
        control->set_maximum_size({200, 50});
        require(control->measure({1000, 1000}).width == 180, "Restored automatic measurement respects minimum size");
        control->set_name(std::wstring(200, L'x'));
        require(control->measure({1000, 1000}).width == 200, "Restored automatic measurement respects maximum size");
        PartStyleValues metrics;
        metrics.padding = Insets{};
        control->set_control_style_values(StylePart::root, metrics);
        require(control->measure({1000, 1000}).width == 200,
            "Styled automatic measurement remains authoritative after preferred size");
        control->set_auto_size(false);
        require(control->measure({1000, 1000}).width == 180, "Disabling automatic size restores constrained preferred size");
    }
}
}
int main() {
    try { schema_cells(); label_values_and_metrics(); buttons_and_toggle(); button_projection_precedence(); restored_auto_size(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Basic text style contracts passed\n";
}
