#include "xui/documents.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace xui;
namespace {
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
template<class F> void rejects(F action) { bool rejected{}; try { action(); } catch (const std::exception&) { rejected = true; } require(rejected, "Invalid argument rejected"); }
bool near(float left, float right) { return std::abs(left - right) < 0.001f; }
void size_is(Size actual, Size expected, const char* message) {
    if (!near(actual.width, expected.width) || !near(actual.height, expected.height))
        throw std::runtime_error(std::string(message) + ": expected " + std::to_string(expected.width) +
            "x" + std::to_string(expected.height) + ", got " + std::to_string(actual.width) + "x" + std::to_string(actual.height));
}
void rect_is(Rect actual, Rect expected, const char* message) {
    require(near(actual.x, expected.x) && near(actual.y, expected.y) &&
        near(actual.width, expected.width) && near(actual.height, expected.height), message);
}
void dialog_layout_tests() {
    auto content = std::make_shared<MultilineText>();
    content->set_preferred_size({180, 40}); content->set_text(L"Retained document");
    content->set_selection({2, 8});
    auto dialog = std::make_shared<ContentDialog>(L"Dialog title", content);
    const auto popup = dialog->popup();
    const auto composition = std::dynamic_pointer_cast<Stack>(popup->content());
    require(composition && composition->child_count() == 2, "Dialog retains body and action containers");
    const auto scroll = std::dynamic_pointer_cast<ScrollView>(composition->child_at(0));
    require(scroll && scroll->passthrough() && !scroll->tab_stop(), "Classic body wrapper is passthrough and not a Tab stop");
    const auto body = std::dynamic_pointer_cast<Stack>(scroll->content());
    require(body && body->child_count() == 3, "Scrollable body retains title, content and validation");
    const auto heading = std::dynamic_pointer_cast<Label>(body->child_at(0));
    const auto primary = dialog->primary(), cancel = dialog->cancel_button();
    const auto validation = dialog->validation();
    const auto row = std::dynamic_pointer_cast<Stack>(composition->child_at(1));
    heading->set_text_measurer([](auto, TextStyle style) { return Size{80, style == TextStyle::subtitle ? 28.0f : 32.0f}; });
    primary->set_text_measurer([](auto, auto) { return Size{20, 19}; });
    cancel->set_text_measurer([](auto, auto) { return Size{40, 19}; });
    size_is(dialog->measure({800, 600}), {460, 360}, "Classic dialog retains fixed popup size");
    popup->arrange({100, 200, 460, 360});
    const auto classic_title = heading->bounds(), classic_content = content->bounds();
    const auto classic_primary = primary->bounds(), classic_cancel = cancel->bounds();
    rect_is(classic_title, {116, 216, 428, 32}, "Classic title padding is unchanged");
    rect_is(classic_content, {116, 258, 428, 40}, "Classic content spacing is unchanged");
    rect_is(classic_primary, {116, 308, 210, 36}, "Hidden Classic validation contributes no trailing Stack gap");
    require(near(classic_primary.y - classic_content.y - classic_content.height, 10),
        "Classic dialog actions follow visible content with exactly one gap");
    rect_is(scroll->viewport(), scroll->bounds(), "Classic body wrapper reserves no scrollbar gutter");
    primary->set_appearance(ButtonAppearance::subtle);
    cancel->set_appearance(ButtonAppearance::accent);
    primary->set_behavior(ButtonBehavior::toggle); primary->set_checked(true);
    primary->set_automation_id(L"retained-primary"); primary->set_enabled(false);
    int edits{}, closes{}, results{}, validations{}, dismissals{};
    content->on_change([&](auto&) { ++edits; });
    dialog->bind_close([&](auto) { ++closes; });
    dialog->on_result([&](auto) { ++results; });
    dialog->on_validate([&] { ++validations; return L"Invalid retained value"; });
    popup->on_dismiss([&](auto) { ++dismissals; });
    popup->opened();
    const auto generation = popup->generation(), revision = content->revision();
    for (int i = 0; i < 5; ++i) {
        dialog->set_visual_style(VisualStyle::winui);
        const auto desired = dialog->measure({800, 600});
        size_is(desired, {320, 209}, "WinUI dialog measures natural content instead of dead popup height");
        popup->arrange({100, 200, desired.width, desired.height});
        rect_is(heading->bounds(), {124, 224, 272, 28}, "WinUI title uses 24px padding and subtitle metrics");
        require(heading->text_style() == TextStyle::subtitle, "WinUI title selects 20px semibold subtitle");
        require(heading->wrapping(), "WinUI title enables bounded wrapping");
        rect_is(content->bounds(), {124, 264, 272, 40}, "WinUI title-to-content gap is 12px");
        rect_is(dialog->footer_bounds(), {100, 329, 320, 80}, "WinUI footer excludes the preceding body separator");
        rect_is(primary->bounds(), {124, 353, 132, 32}, "WinUI buttons have equal columns, 24px padding and 32px height");
        rect_is(cancel->bounds(), {264, 353, 132, 32}, "WinUI action columns have an 8px gap");
        require(content->visual_style() == VisualStyle::winui, "Dialog propagates style before measuring content");
        require(!scroll->passthrough() && scroll->overlay_scrollbar() && !scroll->tab_stop(),
            "WinUI body scrolls without adding a Tab stop");
        rect_is(scroll->viewport(), scroll->bounds(), "WinUI overlay scrollbar reserves no content gutter");
        require(primary->appearance() == ButtonAppearance::subtle && cancel->appearance() == ButtonAppearance::accent &&
            primary->checked() && !primary->enabled() && primary->automation_id() == L"retained-primary",
            "Style changes preserve application button properties");
        dialog->set_visual_style(VisualStyle::classic);
        size_is(dialog->measure({800, 600}), {460, 360}, "Classic preferred size survives repeated switches");
        popup->arrange({100, 200, 460, 360});
        rect_is(heading->bounds(), classic_title, "Classic title geometry restored");
        rect_is(content->bounds(), classic_content, "Classic content geometry restored");
        rect_is(primary->bounds(), classic_primary, "Classic primary geometry restored");
        rect_is(cancel->bounds(), classic_cancel, "Classic cancel geometry restored");
        rect_is(dialog->footer_bounds(), {}, "Classic has no separate footer surface");
        require(!heading->wrapping(), "Classic title restores nonwrapping presentation");
    }
    require(popup == dialog->popup() && composition == popup->content() && scroll == composition->child_at(0) &&
        body == scroll->content() && heading == body->child_at(0) && content == body->child_at(1) &&
        validation == body->child_at(2) && row == composition->child_at(1) &&
        primary == row->child_at(0) && cancel == row->child_at(1),
        "All dialog composition identities survive repeated style changes");
    require(content->text() == L"Retained document" && content->selection() == TextSelection{2, 8} &&
        content->revision() == revision && popup->current(generation) &&
        edits == 0 && closes == 0 && results == 0 && validations == 0 && dismissals == 0,
        "Style changes preserve text, native revision, selection, open generation and callbacks");
    dialog->set_visual_style(VisualStyle::winui);
    primary->set_enabled(true); dialog->accept();
    require(validations == 1 && validation->visible() && closes == 0, "Validation callback survives a style change");
    size_is(dialog->measure({800, 600}), {448, 285}, "Visible validation contributes to natural size");
    dialog->set_visual_style(VisualStyle::classic); dialog->set_visual_style(VisualStyle::winui);
    require(validation->visible() && validation->name() == L"Invalid retained value", "Validation state survives style changes");
    validation->set_visible(false);
    primary->set_text_measurer([](auto, auto) { return Size{20, 20}; });
    size_is(dialog->measure({800, 600}), {320, 210}, "Larger text line height grows natural actions instead of enforcing 32px");
    primary->set_text_measurer([](auto, auto) { return Size{20, 19}; });
    primary->set_preferred_size({90, 58});
    const auto tall_actions = dialog->measure({800, 600});
    size_is(tall_actions, {320, 235}, "Natural action height contributes to dialog desired height");
    popup->arrange({0, 0, tall_actions.width, tall_actions.height});
    require(near(primary->bounds().height, 58) && near(cancel->bounds().height, 58) &&
        near(dialog->footer_bounds().height, 106), "Actions and footer grow beyond the default 32px button height");
    primary->set_auto_size(true);
    heading->set_wrapped_text_measurer([](auto, TextStyle style, float width, std::size_t lines) {
        require(style == TextStyle::subtitle && lines == 2 && width > 0, "Dialog title requests at most two subtitle lines");
        return Size{80, 56};
    });
    size_is(dialog->measure({800, 600}), {320, 237}, "A two-line title increases natural dialog height");
    dialog->set_visual_style(VisualStyle::classic);
    popup->arrange({100, 200, 460, 360});
    rect_is(heading->bounds(), classic_title, "Classic does not use the wrapped subtitle measurer");
    dialog->set_visual_style(VisualStyle::winui);
    heading->set_wrapped_text_measurer({});
    content->set_preferred_size({1000, 1000});
    size_is(dialog->measure({800, 600}), {548, 552}, "WinUI max width and viewport margins cap large content");
    size_is(dialog->measure({1600, 1200}), {548, 756}, "WinUI maximum height caps large content on a large viewport");
    const auto constrained = dialog->measure({300, 240});
    size_is(constrained, {252, 192}, "Small viewports override dialog minimum width");
    popup->arrange({24, 24, constrained.width, constrained.height});
    require(scroll->viewport().y + scroll->viewport().height <= dialog->footer_bounds().y &&
        scroll->maximum_offset() > 0 && content->bounds().height == 1000 &&
        primary->bounds().y >= dialog->footer_bounds().y && near(primary->bounds().height, 32),
        "Tall content retains its extent and is clipped above the retained footer actions");
    for (const Size viewport : {Size{48, 48}, Size{0, 0}, Size{100, 70}}) {
        const auto tiny = dialog->measure(viewport);
        require(std::isfinite(tiny.width) && std::isfinite(tiny.height) && tiny.width >= 0 && tiny.height >= 0,
            "Tiny viewport dimensions remain finite and nonnegative");
        popup->arrange({0, 0, tiny.width, tiny.height});
        require(primary->bounds().width >= 0 && primary->bounds().height >= 0 &&
            content->bounds().height >= 0, "Tiny dialog children have nonnegative dimensions");
    }
    popup->set_preferred_size({1000, 1000});
    size_is(dialog->measure({1600, 1200}), {548, 756}, "Explicit popup dimensions respect WinUI template maxima");
    popup->set_preferred_size({500, 300});
    size_is(dialog->measure({800, 600}), {500, 300}, "Explicit popup dimensions are honored in WinUI");
    size_is(dialog->measure({300, 240}), {252, 192}, "Explicit popup dimensions still respect viewport margins");
    popup->set_maximum_size({280, 250});
    size_is(dialog->measure({800, 600}), {280, 250}, "Application maximum dimensions remain authoritative");
    popup->set_maximum_size({1000, 1000});
    dialog->set_visual_style(VisualStyle::classic);
    size_is(dialog->measure({800, 600}), {500, 300}, "Classic restores the application's explicit popup size");
    rejects([&] { dialog->set_visual_style(static_cast<VisualStyle>(99)); });
    require(popup->visual_style() == VisualStyle::classic, "Rejected visual style does not partially mutate the dialog");
    content->commit_text(L"Edited retained document");
    dialog->cancel(); dialog->notify_result(DialogResult::cancel); popup->closed(PopupDismissReason::cancel);
    require(edits == 1 && closes == 1 && results == 1 && dismissals == 1, "All retained callbacks still work after style switches");
    auto untitled_content = std::make_shared<Element>();
    untitled_content->set_preferred_size({100, 40});
    ContentDialog untitled(L"", untitled_content);
    untitled.primary()->set_text_measurer([](auto, auto) { return Size{20, 19}; });
    untitled.cancel_button()->set_text_measurer([](auto, auto) { return Size{40, 19}; });
    untitled.set_visual_style(VisualStyle::winui);
    size_is(untitled.measure({800, 600}), {320, 184}, "An empty WinUI title respects template minimum height");
    const auto untitled_size = untitled.measure({800, 600});
    untitled.popup()->arrange({0, 0, untitled_size.width, untitled_size.height});
    require(near(untitled_content->bounds().y, 24), "An empty WinUI title has no title row or title gap");
    untitled.set_visual_style(VisualStyle::classic);
    size_is(untitled.measure({800, 600}), {460, 360}, "An empty title does not change Classic sizing");
}
void dialog_overflow_tests() {
    auto fields = std::make_shared<Stack>(Axis::vertical);
    fields->set_spacing(8);
    std::vector<std::shared_ptr<TextInput>> editors;
    for (int i = 0; i < 18; ++i) {
        auto editor = std::make_shared<TextInput>(L"Field " + std::to_wstring(i));
        editor->set_preferred_size({240, 40});
        editor->set_text(L"Retained field " + std::to_wstring(i));
        fields->add(editor); editors.push_back(std::move(editor));
    }
    ContentDialog dialog(L"Scrollable form", fields);
    auto root = std::dynamic_pointer_cast<Stack>(dialog.popup()->content());
    auto scroll = std::dynamic_pointer_cast<ScrollView>(root->child_at(0));
    auto body = std::dynamic_pointer_cast<Stack>(scroll->content());
    auto title = std::dynamic_pointer_cast<Label>(body->child_at(0));
    title->set_text_measurer([](auto, auto) { return Size{100, 28}; });
    dialog.primary()->set_text_measurer([](auto, auto) { return Size{20, 19}; });
    dialog.cancel_button()->set_text_measurer([](auto, auto) { return Size{40, 19}; });
    dialog.set_visual_style(VisualStyle::winui);
    const auto desired = dialog.measure({420, 330});
    dialog.popup()->arrange({24, 24, desired.width, desired.height});
    const auto footer = dialog.footer_bounds(), actions = dialog.primary()->bounds();
    require(scroll->maximum_offset() > 0 && scroll->thumb().height > 0 &&
        editors.back()->bounds().y >= scroll->viewport().y + scroll->viewport().height,
        "Tall forms expose a scrollbar and retain offscreen fields");
    const auto original_bottom = editors.back()->bounds();
    scroll->scroll_by(64);
    dialog.popup()->arrange({24, 24, desired.width, desired.height});
    require(near(editors.back()->bounds().y, original_bottom.y - 64), "Scrolling actually moves retained form fields");
    scroll->reveal(editors.back()->bounds());
    dialog.popup()->arrange({24, 24, desired.width, desired.height});
    const auto viewport = scroll->viewport(), last = editors.back()->bounds();
    require(last.y >= viewport.y && last.y + last.height <= viewport.y + viewport.height,
        "Focus reveal can bring the final field completely into view");
    rect_is(dialog.footer_bounds(), footer, "Scrolling does not move the footer surface");
    rect_is(dialog.primary()->bounds(), actions, "Scrolling does not move the action buttons");
    const auto offset = scroll->offset();
    for (int i = 0; i < 3; ++i) {
        dialog.set_visual_style(VisualStyle::classic);
        dialog.popup()->arrange({24, 24, 460, 360});
        require(scroll->passthrough() && near(scroll->offset(), 0) && near(scroll->maximum_offset(), 0) &&
            near(scroll->thumb().height, 0) && !scroll->tab_stop(), "Classic has no scrolling or extra Tab stop");
        require(near(fields->bounds().width, 428), "Classic form content keeps its full pre-wrapper width");
        scroll->scroll_by(100);
        require(near(scroll->offset(), 0), "Classic ignores wrapper scrolling");
        dialog.set_visual_style(VisualStyle::winui);
        dialog.popup()->arrange({24, 24, desired.width, desired.height});
        require(near(scroll->offset(), offset), "Returning to WinUI restores the retained overflow offset");
        require(scroll == root->child_at(0) && body == scroll->content() && fields == body->child_at(1) &&
            fields->child_at(17) == editors.back() && editors.back()->text() == L"Retained field 17",
            "Overflow style switches retain the same wrapper, content and field state");
    }
    scroll->reveal(editors.front()->bounds());
    dialog.popup()->arrange({24, 24, desired.width, desired.height});
    require(editors.front()->bounds().y >= scroll->viewport().y &&
        editors.front()->bounds().y + editors.front()->bounds().height <= scroll->viewport().y + scroll->viewport().height,
        "Focus reveal can return to the first field");
}
}
int main() {
    try {
        MultilineText text;
        int changes{}; text.on_change([&](const std::wstring&) { ++changes; text.commit_text(L"reentrant"); });
        text.set_text(L"A\r\U0001f642"); require(changes == 0, "Property setters are silent");
        text.set_selection({2, 4}); require(text.selection() == TextSelection{2, 4}, "UTF-16 selection");
        require(!text.monospace(), "Documents retain proportional text by default");
        const auto revision = text.revision();
        text.set_monospace(true); text.set_monospace(true);
        require(text.monospace() && text.revision() == revision && changes == 0 &&
            text.selection() == TextSelection{2, 4}, "Monospace changes preserve text, selection and callbacks");
        text.set_monospace(false); require(!text.monospace(), "Document font can return to proportional text");
        rejects([&] { text.set_selection({2, 3}); });
        rejects([&] { text.set_text(std::wstring(1, 0xd800)); });
        rejects([&] { text.set_text(std::wstring(L"a\0b", 3)); });
        rejects([&] { text.set_maximum_length(1); });
        rejects([&] { text.set_maximum_length(DocumentText::document_limit + 1); });
        text.commit_text(L"Committed"); require(changes == 1 && text.text() == L"Committed", "Exactly once and no nested commit");
        text.set_read_only(true); text.commit_text(L"Blocked"); require(text.text() == L"Committed", "Read-only commit blocked");
        require(!text.command(TextCommand::undo), "Detached native command rejected");
        RichText rich; rich.set_runs({{L"Bold", true}, {L" Link", false, false, true, L"https://example.com"}});
        require(rich.text() == L"Bold Link" && rich.runs().size() == 2, "Styled document retains runs");
        int links{}; rich.on_link([&](const auto&) { ++links; }); rich.activate_link(5); require(links == 1, "Explicit link action");
        rejects([&] { rich.set_runs({{L"Unsafe", false, false, false, L"file:///test"}}); });
        rejects([&] { rich.set_runs({{L"Unsafe", false, false, false, L"https://"}}); });
        rejects([&] { rich.set_runs({{L"Unsafe", false, false, false, L"https://example.com/\rtest"}}); });
        rejects([&] { rich.set_runs(std::vector<TextRun>(4097)); });
        require(rich.text() == L"Bold Link", "Rejected rich document is atomic");
        rich.commit_text(L"New Bold Link"); rich.activate_link(9); require(links == 2, "Native edits move surviving application link offsets");
        PasswordInput password; int secrets{};
        password.on_change([&] { ++secrets; password.commit_password(L"reentrant"); });
        password.set_password(L"Fixture secret"); require(secrets == 0, "Password setter is silent");
        rejects([&] { password.set_revealed(true); });
        password.set_reveal_policy(PasswordRevealPolicy::explicit_request); password.set_revealed(true);
        password.set_reveal_policy(PasswordRevealPolicy::never); require(!password.revealed(), "Policy removes reveal");
        password.commit_password(L"New fixture"); require(secrets == 1 && password.length() == 11, "Password notification has no value");
        password.with_password([&](auto value) { require(value == L"New fixture", "Explicit secret boundary"); });
        password.set_reveal_policy(PasswordRevealPolicy::explicit_request); password.set_revealed(true);
        password.clear_password();
        require(password.length() == 0 && !password.revealed() && secrets == 1, "Terminal cleanup clears contents and reveal silently");
        password.with_password([](auto value) { require(value.empty(), "Explicit secret read sees cleared contents"); });
        password.clear_password();
        require(secrets == 1, "Repeated terminal clear stays silent");
        password.set_password(L"Legacy retained value"); password.set_visible(false); password.set_enabled(false);
        require(password.length() == 21, "Legacy hide and disable do not implicitly clear password ownership");
        password.set_revealed(true);
        password.set_invalidator([](Invalidation) { throw std::runtime_error("Injected terminal invalidation failure"); });
        rejects([&] { password.clear_password(); });
        require(password.length() == 0 && !password.revealed() && secrets == 1,
            "Terminal invalidation failure cannot retain plaintext or emit an edit event");
        password.set_invalidator({});
        {
            for (const auto purpose : {TextInputPurpose::normal, TextInputPurpose::email, TextInputPurpose::url,
                TextInputPurpose::telephone, TextInputPurpose::number}) {
                TextInput advisory(L"Purpose", purpose);
                advisory.set_text(L" Mixed Case + @ - 12.3 ");
                require(advisory.purpose() == purpose && advisory.text() == L" Mixed Case + @ - 12.3 ",
                    "Input purposes retain exact content without coercion");
            }
            rejects([] { TextInput invalid(L"Invalid purpose", static_cast<TextInputPurpose>(5)); });
            MultilineText multiline;
            multiline.set_fixed_size({100, 50});
            multiline.set_axis_constraints(AxisConstraints{280.0f}, AxisConstraints{});
            size_is(multiline.measure({1000, 1000}), {280, 180}, "Multiline Auto is a native viewport, not name/content growth");
            multiline.set_text(std::wstring(4000, L'\n'));
            size_is(multiline.measure({1000, 1000}), {280, 180}, "Long documents use native scrollbars without implicit growth");
            PartStyleValues font; font.font_size = 200.0f;
            multiline.set_control_style_values(StylePart::text, font);
            require(multiline.measure({1000, 1000}).height >= 306, "Multiline Auto reserves a complete scaled line");
            multiline.set_axis_constraints(std::nullopt, std::nullopt);
            size_is(multiline.measure({1000, 1000}), {100, 50}, "Multiline legacy fixed size survives override reset");
            PasswordInput masked;
            masked.set_fixed_size({100, 20});
            masked.set_axis_constraints(AxisConstraints{}, AxisConstraints{});
            masked.set_control_style_values(StylePart::text, font);
            require(masked.measure({1000, 1000}).height >= 306, "Password Auto reserves a complete masked scaled line");
            masked.set_axis_constraints(std::nullopt, std::nullopt);
            size_is(masked.measure({1000, 1000}), {100, 20}, "Password legacy fixed size survives override reset");
        }
        DateTimePicker date; int dates{}; date.on_change([&](auto) { ++dates; });
        date.set_value({2024, 2, 29}); date.set_range({2024, 1, 1}, {2024, 12, 31});
        rejects([&] { date.set_value({2023, 2, 29}); });
        rejects([&] { date.set_value({2025, 1, 1}); });
        rejects([&] { date.set_range({2024, 3, 1}, {2024, 12, 31}); });
        require(dates == 0, "Date properties are silent"); date.change_value({2024, 3, 1}); require(dates == 1, "Date action fires once");
        InlineStatus status(L"Saved"); int dismissals{}; status.on_dismiss([&] { ++dismissals; });
        status.set_dismissible(true); status.dismiss(); status.dismiss(); require(dismissals == 1 && !status.visible(), "Dismiss once");
        status.show(); require(status.visible(), "Status can reopen");
        ColorPicker color; int colors{}; color.on_change([&](auto) { ++colors; });
        color.set_value({255, 10, 20, 40}); require(colors == 0 && color.channels()[3]->value() == 40, "RGBA property sync");
        color.change_value({30, 40, 50, 60}); require(colors == 1, "RGBA action sync");
        rejects([&] { color.set_swatches(std::vector<RgbaColor>(17)); });
        for (int i = 0; i < 100; ++i) color.set_swatches({{0, 0, 0}});
        require(color.retained_children().size() == 9, "Swatches retain bounded children");
        auto dialog = std::make_shared<ContentDialog>(L"Dialog", std::make_shared<MultilineText>());
        dialog->popup()->opened(); int closed{};
        dialog->on_validate([] { return L"Invalid value"; }); dialog->bind_close([&](auto) { ++closed; });
        dialog->accept(); require(closed == 0 && dialog->validation()->visible(), "Validation stays visible");
        dialog->on_validate({}); dialog->accept(); require(closed == 1, "Valid default action");
        dialog_layout_tests();
        dialog_overflow_tests();
        std::cout << "Seven document models: bounds, invalid UTF-16, property silence, reentrancy, selection, explicit links, password policy, validation passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
