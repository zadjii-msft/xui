#include "xui/documents.hpp"
#include <iostream>
#include <source_location>
#include <stdexcept>

namespace {
using namespace xui;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const std::source_location location = std::source_location::current()) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Unsupported native field style was accepted at test line " + std::to_string(location.line()));
}
PartStyleValues ink(unsigned value) { PartStyleValues result; result.foreground = ThemeColor{value}; return result; }
void schemas() {
    for (const auto target : {StyleTarget::text_input, StyleTarget::multiline_text, StyleTarget::rich_text,
        StyleTarget::password_input, StyleTarget::date_time_picker}) {
        const auto& schema = control_style_schema(target);
        const StyleValueLimits* limits{};
        for (const auto& part : schema.parts) if (part.part == StylePart::text) limits = &part.limits;
        require(limits != nullptr, "Native text schema exists");
        if (limits->maximum_font_size != 512 || limits->maximum_font_family_utf16 != 31 || limits->font_styles != 3)
            throw std::runtime_error("Live native text schema target=" + std::to_string(static_cast<unsigned>(target)) +
                " font_size=" + std::to_string(limits->maximum_font_size) +
                " family_units=" + std::to_string(limits->maximum_font_family_utf16) +
                " font_styles=" + std::to_string(limits->font_styles) + "; expected 512/31/3");
        PartStyleValues surface;
        surface.background = ThemeColor{0x112233, 0x223344};
        surface.border_brush = ThemeColor{0x445566};
        surface.padding = Insets{8, 6, 9, 7};
        surface.border_thickness = Insets{1, 2, 3, 4};
        surface.corner_radius = 5;
        validate_part_values(target, StylePart::root, surface);
        PartStyleValues font;
        font.font_family = make_style_font_family("Consolas");
        font.font_size = 22; font.font_weight = 600; font.font_style = StyleFontStyle::italic;
        validate_part_values(target, StylePart::text, font);
        font.font_style = StyleFontStyle::oblique;
        rejects([&] { validate_part_values(target, StylePart::text, font); });
        font.font_style = StyleFontStyle::normal; font.font_size = 513;
        rejects([&] { validate_part_values(target, StylePart::text, font); });
        font.font_size = 20; font.font_family = make_style_font_family(std::string(32, 'a'));
        rejects([&] { validate_part_values(target, StylePart::text, font); });
        font.font_size = 512; font.font_family = make_style_font_family(std::string(31, 'a'));
        validate_part_values(target, StylePart::text, font);
        PartStyleValues alignment; alignment.horizontal_alignment = StyleAlignment::center;
        rejects([&] { validate_part_values(target, StylePart::text, alignment); });
    }
    PartStyleValues header_font;
    header_font.font_size = 512;
    header_font.font_family = make_style_font_family(std::string(31, 'a'));
    header_font.font_style = StyleFontStyle::italic;
    validate_part_values(StyleTarget::text_input, StylePart::header, header_font);
    header_font.font_style = StyleFontStyle::oblique;
    rejects([&] { validate_part_values(StyleTarget::text_input, StylePart::header, header_font); });
    header_font.font_style = StyleFontStyle::normal;
    header_font.font_size = 513;
    rejects([&] { validate_part_values(StyleTarget::text_input, StylePart::header, header_font); });
    header_font.font_size = 20;
    header_font.font_family = make_style_font_family(std::string(32, 'a'));
    rejects([&] { validate_part_values(StyleTarget::text_input, StylePart::header, header_font); });
    header_font.font_size = 513;
    header_font.font_style = StyleFontStyle::oblique;
    validate_part_values(StyleTarget::text_input, StylePart::shortcut, header_font);
    rejects([] { validate_part_values(StyleTarget::date_time_picker, StylePart::text, ink(1)); });
    rejects([] {
        PartStyleValues value; value.background = ThemeColor{1};
        validate_part_values(StyleTarget::date_time_picker, StylePart::text, value);
    });
    rejects([] { validate_part_values(StyleTarget::date_time_picker, StylePart::root, ink(1)); });
    for (const auto presentation : {DateTimePresentation::date, DateTimePresentation::time, DateTimePresentation::calendar}) {
        DateTimePicker picker(L"Native date", presentation);
        const auto before = picker.value();
        rejects([&] { picker.set_control_style_values(StylePart::text, ink(1)); });
        PartStyleValues frame; frame.background = ThemeColor{0x123456}; frame.padding = Insets{4, 4, 4, 4};
        rejects([&] { picker.set_control_style_values(StylePart::text, frame); });
        picker.set_control_style_values(StylePart::root, frame);
        require(picker.effective_control_style_values(StylePart::root)->background == frame.background && picker.value() == before,
            "All date presentations support owned frames without changing native values");
    }
    rejects([] { ControlStyle::create(StyleTarget::text_input, {}, {{StylePart::text, style_states::read_only, ink(1)}}); });
    rejects([] { ControlStyle::create(StyleTarget::multiline_text, {}, {{StylePart::text, style_states::revealed, ink(1)}}); });
    rejects([] { ControlStyle::create(StyleTarget::date_time_picker, {}, {{StylePart::root, style_states::empty, {}}}); });
    for (const auto metric : {StyleProperty::padding, StyleProperty::border_thickness, StyleProperty::size, StyleProperty::font_size}) {
        PartStyleValues value;
        if (metric == StyleProperty::padding) value.padding = Insets{1, 1, 1, 1};
        if (metric == StyleProperty::border_thickness) value.border_thickness = Insets{1, 1, 1, 1};
        if (metric == StyleProperty::size) value.size = 12;
        if (metric == StyleProperty::font_size) value.font_size = 12;
        rejects([&] { validate_part_values(StyleTarget::text_input, StylePart::clear_action, value); });
    }
}
void clear_projection() {
    TextInput input(L"Clearable"); input.set_text(L"value");
    const auto before = input.measure({1000, 1000});
    input.set_control_style(ControlStyle::create(StyleTarget::text_input, {{StylePart::clear_action, ink(1)}},
        {{StylePart::clear_action, style_states::hovered, ink(2)}, {StylePart::clear_action, style_states::pressed, ink(3)},
            {StylePart::clear_action, style_states::disabled, ink(4)}}));
    const auto after = input.measure({1000, 1000});
    require(before.width == after.width && before.height == after.height, "Clear color styles do not alter field metrics");
    require(input.resolve_control_style_part(StylePart::clear_action, style_states::hovered).foreground == ThemeColor{2},
        "Clear action consumes child hover state, not owner hover");
    require(input.resolve_control_style_part(StylePart::clear_action, style_states::hovered | style_states::pressed).foreground == ThemeColor{3},
        "Clear action consumes child pressed state");
    input.set_enabled(false);
    require(input.resolve_control_style_part(StylePart::clear_action, style_states::pressed).foreground == ThemeColor{4},
        "Owner disabled outranks clear child interaction");
    require(input.text() == L"value", "Visual clear state never edits text");
}
void state_and_identity() {
    TextInput input(L"Query");
    require(!input.has_control_styling(), "Default TextInput has no style attachment");
    auto style = ControlStyle::create(StyleTarget::text_input, {{StylePart::root, ink(1)}},
        {{StylePart::text, style_states::empty, ink(2)}, {StylePart::text, style_states::focused, ink(3)},
            {StylePart::text, style_states::disabled, ink(4)}});
    input.set_control_style(style);
    require(input.effective_control_style_values(StylePart::text)->foreground == ThemeColor{2}, "Empty uses actual text");
    input.set_text(L"unchanged"); input.set_selection({1, 5});
    require(input.effective_control_style_values(StylePart::text)->foreground == ThemeColor{1}, "Nonempty inherits root text");
    input.set_focused(true);
    require(input.effective_control_style_values(StylePart::text)->foreground == ThemeColor{3}, "Native focus rule applies");
    input.set_enabled(false);
    require(input.effective_control_style_values(StylePart::text)->foreground == ThemeColor{4}, "Disabled outranks focused");
    input.set_control_style(nullptr);
    require(input.text() == L"unchanged" && input.selection() == TextInput::Selection{1, 5}, "Style clear preserves text and selection");
    require(!input.has_control_styling(), "Style clear releases optional attachment");

    MultilineText document;
    document.set_text(L"retained"); document.set_selection({2, 5});
    const auto revision = document.revision();
    document.set_control_style(ControlStyle::create(StyleTarget::multiline_text, {},
        {{StylePart::text, style_states::read_only, ink(6)}}));
    document.set_read_only(true);
    require(document.effective_control_style_values(StylePart::text)->foreground == ThemeColor{6}, "Read-only is model state");
    document.set_control_style(nullptr);
    require(document.read_only() && document.revision() == revision && document.selection() == TextSelection{2, 5},
        "Styling never writes document state or revision");

    RichText rich; rich.set_runs({{L"bold", true}, {L"italic", false, true}});
    const auto runs = rich.runs();
    rich.set_control_style(ControlStyle::create(StyleTarget::rich_text, {{StylePart::text, ink(3)}}, {}));
    rich.set_control_style(nullptr);
    require(rich.runs() == runs, "Styles leave authored rich runs intact");

    PasswordInput password;
    password.set_password(L"not-a-diagnostic");
    const auto password_revision = password.revision();
    password.set_control_style(ControlStyle::create(StyleTarget::password_input, {},
        {{StylePart::text, style_states::revealed, ink(7)}, {StylePart::text, style_states::empty, ink(8)}}));
    require(!password.revealed() && password.revision() == password_revision, "Visual revealed state cannot reveal a password");
    password.set_reveal_policy(PasswordRevealPolicy::explicit_request); password.set_revealed(true);
    require(password.effective_control_style_values(StylePart::text)->foreground == ThemeColor{7}, "Explicit reveal supplies state");
    password.set_control_style(nullptr);
    require(password.length() == 16 && password.revealed(), "Style clear preserves password ownership and reveal policy");
}
void geometry() {
    TextInput compact(L"Compact");
    int compact_layouts{};
    compact.set_invalidator([&](Invalidation kind) { compact_layouts += kind == Invalidation::layout; });
    compact.set_caption_visible(false); compact.set_caption_visible(false);
    compact.set_shortcut_hint(L"Ctrl+L");
    require(compact_layouts == 1 && !compact.caption_visible() && !compact.search_style(),
        "Unstyled compact hint retains paint-only invalidation and one caption layout");
    PartStyleValues shortcut_font; shortcut_font.font_size = 22;
    compact.set_control_style_values(StylePart::shortcut, shortcut_font);
    compact_layouts = 0;
    compact.set_shortcut_hint(L"Ctrl+Shift+L"); compact.set_shortcut_hint(L"Ctrl+Shift+L");
    require(compact_layouts == 1, "Styled shortcut text resizes native reservation once, not for duplicate setters");
    TextInput input(L"Header");
    input.set_visual_style(VisualStyle::winui);
    PartStyleValues root; root.padding = Insets{20, 12, 18, 13}; root.border_thickness = Insets{2, 3, 4, 5};
    input.set_control_style_values(StylePart::root, root);
    const auto insets = input.field_insets({1, 1, 1, 1});
    require(insets.left == 22 && insets.top == 15 && insets.right == 22 && insets.bottom == 18,
        "Native placement uses the same padding and border metrics");
    PartStyleValues font; font.font_size = 32;
    input.set_control_style_values(StylePart::header, font);
    require(input.caption_height() >= 48, "Header typography updates layout");
    input.set_control_style_values(StylePart::text, font);
    require(input.measure({1000, 1000}).height >= 48 + 33 + input.caption_extent(), "Native text metrics update measurement");
    input.set_shortcut_hint(L"Ctrl+K"); input.set_control_style_values(StylePart::shortcut, font);
    require(input.shortcut_size().width >= 200 && input.shortcut_size().height >= 48, "Shortcut font updates reserved space");
    PasswordInput password;
    password.set_reveal_policy(PasswordRevealPolicy::explicit_request); password.set_revealed(true);
    password.set_control_style_values(StylePart::text, font);
    require(password.reveal_extent() >= 52, "Reveal preview and native field share font metrics");
}
}
int main() {
    try { schemas(); state_and_identity(); clear_projection(); geometry(); std::cout << "Native field style model contracts passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
