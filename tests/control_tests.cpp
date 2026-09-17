#include "xui/controls.hpp"
#include "xui/file_list.hpp"
#include <iostream>
#include <stdexcept>

using namespace xui;
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void behavior() {
    Button button(L"Save");
    button.set_preferred_size({240, 40});
    Toggle toggle(L"Preference");
    int clicks{}, changes{}, paints{}, layouts{};
    button.on_click([&] { ++clicks; });
    button.set_invalidator([&](Invalidation kind) {
        if (kind == Invalidation::paint) ++paints;
        else ++layouts;
    });
    require(!button.pointer_down(), "Pointer outside cannot capture");
    button.pointer_move(true);
    require(button.pointer_down() && button.captured() && button.pressed(), "Pointer press captures");
    button.pointer_move(false);
    require(!button.pressed() && button.captured(), "Drag outside disarms but retains capture");
    require(!button.pointer_up(false) && clicks == 0 && !button.captured(), "Release outside cancels");
    button.pointer_move(true);
    button.pointer_down();
    button.cancel();
    require(!button.pointer_up(true) && clicks == 0, "Capture loss cancels");
    button.pointer_down();
    button.pointer_move(false);
    button.pointer_move(true);
    require(button.pointer_up(true) && clicks == 1, "Drag back and release invokes once");
    button.set_focused(true);
    button.key_down(ActivationKey::space);
    require(button.pressed() && clicks == 1, "Space waits for release");
    require(!button.key_down(ActivationKey::space, true), "Repeat ignored");
    require(button.key_up(ActivationKey::space) && clicks == 2, "Space release invokes once");
    require(!button.key_up(ActivationKey::space), "Extra release does not invoke");
    button.key_down(ActivationKey::space);
    button.set_focused(false);
    require(!button.key_up(ActivationKey::space) && !button.pressed(), "Focus loss cancels keyboard press");
    button.set_focused(true);
    require(button.key_down(ActivationKey::enter) && clicks == 3, "Enter invokes button");
    require(!button.key_down(ActivationKey::enter, true), "Enter repeat ignored");
    button.pointer_down();
    button.set_enabled(false);
    require(!button.captured() && !button.pressed() && !button.focused() && !button.hovered(), "Disable resets interaction");
    require(!button.invoke() && !button.pointer_up(true) && !button.key_down(ActivationKey::enter),
        "Disabled input and automation rejected");
    button.set_enabled(true);
    require(button.invoke() && clicks == 4, "Re-enabled command invokes");
    toggle.on_change([&](bool value) { require(value == toggle.checked(), "Callback sees updated property"); ++changes; });
    toggle.set_checked(true);
    require(changes == 0, "Property updates do not fire callbacks");
    toggle.set_focused(true);
    require(!toggle.key_down(ActivationKey::enter) && toggle.checked(), "Enter does not toggle checkbox");
    toggle.key_down(ActivationKey::space);
    require(toggle.key_up(ActivationKey::space) && !toggle.checked() && changes == 1, "Space toggles checkbox");
    toggle.set_enabled(false);
    require(!toggle.invoke() && changes == 1, "Disabled checkbox rejects automation");
    const auto before = paints;
    button.set_name(L"Save");
    require(paints == before, "No-op property causes no invalidation");
    button.set_name(L"Saved");
    require(paints == before + 1 && layouts == 0, "Fixed-size caption update only repaints");
    button.set_preferred_size({120, 50});
    require(layouts == 1, "Size update requests layout");
}
void sizing_and_scroll() {
    static_assert(static_cast<int>(ButtonIcon::drive) == 21 &&
        static_cast<int>(ButtonIcon::open) == 22 &&
        static_cast<int>(ButtonIcon::save) == 23 && static_cast<int>(ButtonIcon::save_as) == 24 &&
        static_cast<int>(ButtonIcon::undo) == 25 && static_cast<int>(ButtonIcon::redo) == 26 &&
        static_cast<int>(ButtonIcon::chevron_up) == 27 && static_cast<int>(ButtonIcon::chevron_down) == 28);
    for (const auto value : {ButtonIcon::save, ButtonIcon::save_as, ButtonIcon::undo, ButtonIcon::redo,
        ButtonIcon::chevron_up, ButtonIcon::chevron_down}) {
        Button command(L"Document command");
        const auto id = command.id();
        command.set_icon(value);
        int clicks{};
        command.on_click([&] { ++clicks; });
        require(command.icon() == value && command.name() == L"Document command" &&
            command.id() == id && command.measure({300, 80}).width == 36,
            "Document icons preserve button names, identity, and icon sizing");
        command.set_focused(true);
        require(command.key_down(ActivationKey::enter) && clicks == 1,
            "Document icon buttons retain keyboard activation");
        command.set_enabled(false);
        require(!command.invoke() && clicks == 1, "Disabled document icons cannot invoke commands");
        for (const auto invalid : {static_cast<ButtonIcon>(-1), static_cast<ButtonIcon>(29)}) {
            bool rejected{};
            try { command.set_icon(invalid); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && command.icon() == value, "Invalid button icons fail without mutation");
        }
    }
    Button icon(L"Back");
    icon.set_text_measurer([](std::wstring_view, TextStyle) { return Size{180, 20}; });
    icon.set_icon(ButtonIcon::back);
    require(icon.measure({300, 80}).width == 36 && icon.name() == L"Back", "Icon size is independent of accessible name");
    icon.set_preferred_size({32, 40});
    require(icon.measure({300, 80}).width == 32, "Icon respects explicit size");
    icon.set_auto_size(true);
    icon.set_icon(ButtonIcon::none);
    require(icon.measure({300, 80}).width == 208, "Text button sizing is restored");
    TextInput compact(L"Folder address");
    require(compact.caption_visible(), "Input caption remains visible by default");
    int caption_layouts{};
    compact.set_invalidator([&](Invalidation kind) { if (kind == Invalidation::layout) ++caption_layouts; });
    compact.set_caption_visible(false);
    compact.set_caption_visible(false);
    compact.set_shortcut_hint(L"Ctrl+L");
    require(!compact.caption_visible() && !compact.search_style() && caption_layouts == 1,
        "Compact input hides caption without search presentation and avoids duplicate layout");
    require(!compact.shortcut_visible(150) && compact.shortcut_visible(300), "Compact hint leaves room for narrow text");
    compact.set_search_style(true);
    compact.set_caption_visible(true);
    require(!compact.caption_visible() && compact.shortcut_visible(150), "Search presentation retains its previous caption and hint policy");
    int calls{}, layouts{};
    const TextMeasurer measure = [&](std::wstring_view text, TextStyle style) {
        ++calls;
        return Size{static_cast<float>(text.size()) * (style == TextStyle::heading ? 12.0f : 7.0f),
            style == TextStyle::heading ? 30.0f : 18.0f};
    };
    auto label = std::make_shared<Label>(L"Short");
    label->set_text_measurer(measure);
    label->set_invalidator([&](Invalidation kind) { if (kind == Invalidation::layout) ++layouts; });
    require(label->measure({1000, 1000}).width == 35 && calls == 1, "Automatic text width uses injected metrics");
    label->measure({20, 30});
    label->pointer_move(true);
    require(label->measure({1000, 1000}).width == 35 && calls == 1, "Constraints and hover reuse intrinsic metrics");
    label->set_text(L"A much longer label");
    require(layouts == 1 && label->measure({1000, 1000}).width == 133 && calls == 2, "Text updates measure and layout");
    label->set_heading(true);
    require(label->measure({1000, 1000}).height == 30 && calls == 3, "Typography invalidates metrics");
    label->set_preferred_size({90, 50});
    label->set_text(L"Fixed label");
    require(label->measure({1000, 1000}).width == 90 && calls == 3, "Explicit preferred sizes remain fixed");
    label->set_auto_size(true);
    label->set_minimum_size({180, 40});
    label->set_maximum_size({200, 50});
    require(label->measure({1000, 1000}).width == 180, "Automatic size respects minimum");
    require(label->measure({25, 10}).width == 25 && label->measure({25, 10}).height == 10,
        "Parent constraints take precedence in narrow windows");
    label->set_text(std::wstring(200, L'x'));
    require(label->measure({1000, 1000}).width == 200, "Automatic size respects maximum");
    label->arrange({0, 0, 500, 100});
    require(label->bounds().width == 200 && label->bounds().height == 50, "Maximum limits arranged dimensions");
    label->set_fixed_size({80, 32});
    label->arrange({0, 0, 200, 100});
    require(label->bounds().width == 80 && label->bounds().height == 32, "Fixed size constrains both axes");
    label->arrange({0, 0, 10, 5});
    require(label->bounds().width == 10 && label->bounds().height == 5, "Fixed size cannot escape parent allocation");
    auto content = std::make_shared<Stack>(Axis::vertical);
    content->set_spacing(5);
    for (int i = 0; i < 10; ++i) {
        auto button = std::make_shared<Button>(L"Action");
        button->set_preferred_size({100, 40});
        content->add(button, i == 9 ? 1.0f : 0.0f);
    }
    auto scroll = std::make_shared<ScrollView>(content);
    Stack root(Axis::vertical);
    root.add(scroll, 1);
    root.arrange({0, 0, 220, 120});
    require(scroll->extent() == 445 && scroll->maximum_offset() == 325,
        "Scroll measures content unbounded vertically; flex takes natural size");
    require(content->bounds().width == 208, "Scrollbar reserves viewport width");
    scroll->set_offset(9999);
    root.arrange({0, 0, 220, 120});
    require(scroll->offset() == 325 && content->bounds().y == -325, "Scroll clamps and translates content");
    scroll->reveal(content->child_at(0)->bounds());
    root.arrange({0, 0, 220, 120});
    require(scroll->offset() == 0, "Reveal above viewport");
    scroll->reveal(content->child_at(9)->bounds());
    root.arrange({0, 0, 220, 120});
    require(scroll->offset() == 325, "Reveal below viewport");
    root.arrange({0, 0, 220, 1000});
    require(scroll->offset() == 0 && scroll->maximum_offset() == 0 && scroll->thumb().height == 0,
        "Growing viewport removes scrolling and clamps offset");
    root.arrange({0, 0, 0, 0});
    require(scroll->viewport().width == 0, "Zero-sized viewport stays nonnegative");
    bool rejected{};
    try { ScrollView duplicate(content); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Scroll content has one retained owner");
    int invalidations{};
    std::shared_ptr<Label> retained;
    {
        auto inner = std::make_shared<Stack>(Axis::vertical);
        retained = std::make_shared<Label>(L"Before");
        inner->add(retained);
        ScrollView temporary(inner);
        temporary.set_invalidator([&](Invalidation) { ++invalidations; });
        retained->set_text(L"During");
    }
    retained->set_text(L"After");
    require(invalidations == 1, "Scroll content invalidation detaches safely");
}
void focus_and_lifetime() {
    auto label = std::make_shared<Label>(L"Title");
    auto button = std::make_shared<Button>(L"Save");
    auto toggle = std::make_shared<Toggle>(L"Preference");
    auto input = std::make_shared<TextInput>(L"Name");
    Control* targets[]{label.get(), button.get(), toggle.get(), input.get()};
    require(next_focus(targets, nullptr, false) == button.get(), "Initial focus skips labels");
    require(next_focus(targets, nullptr, true) == input.get(), "Initial reverse focus");
    button->set_enabled(false);
    require(next_focus(targets, input.get(), false) == toggle.get(), "Forward wrap skips disabled");
    require(next_focus(targets, toggle.get(), true) == input.get(), "Reverse wrap skips disabled");
    input->set_enabled(false);
    toggle->set_enabled(false);
    require(!next_focus(targets, nullptr, false) && !next_focus({}, nullptr, true), "Empty focus ring");
    require(label->id() != button->id() && button->id() != toggle->id(), "Stable unique IDs");
    const auto id = button->id();
    int invalidations{};
    {
        Stack root(Axis::vertical);
        auto nested = std::make_shared<Stack>(Axis::horizontal);
        nested->add(button);
        root.add(nested);
        root.set_invalidator([&](Invalidation) { ++invalidations; });
        button->set_name(L"Changed");
        require(invalidations == 1, "Control updates reach root through nested layout");
    }
    button->set_name(L"Retained after host");
    require(invalidations == 1 && button->id() == id, "Retained control outlives root safely");
    int changes{};
    input->on_change([&](const std::wstring& value) {
        require(value == L"\u65e5\u672c", "Committed Unicode text");
        ++changes;
        input->set_text(L"Reentrant replacement");
    });
    input->set_text(L"Initial");
    require(changes == 0, "Text property is silent");
    input->commit_text(L"\u65e5\u672c");
    require(changes == 1 && input->text() == L"Reentrant replacement", "Text callback can update properties");
    input->commit_text(L"Reentrant replacement");
    require(changes == 1, "Unchanged text is silent");
    input->set_text(std::wstring(1100, L'x'));
    require(input->text().size() == 1024, "Native and model limits match");
    input->set_text(std::wstring(L"a\0b", 3));
    require(input->text() == L"a", "Text model respects native NUL termination");
    if constexpr (sizeof(wchar_t) == 2) {
        input->set_text(std::wstring(1023, L'x') + L"\U0001f642");
        require(input->text().size() == 1023, "Length limit does not split a surrogate pair");
    }
}
void virtual_list_control() {
    auto list = std::make_shared<FileList>(L"Results");
    auto input = std::make_shared<TextInput>(L"Filter");
    auto button = std::make_shared<Button>(L"Clear");
    Stack root(Axis::vertical);
    root.add(input);
    root.add(list, 1);
    root.add(button);
    require(list->role() == ControlRole::file_list && list->focusable(), "FileList is a public focusable Control");
    Control* controls[]{input.get(), list.get(), button.get()};
    require(next_focus(controls, input.get(), false) == list.get(), "The list participates in standard traversal");
    int selections{}, views{};
    list->on_selection_change([&] {
        ++selections;
        require(list->model().selected_index() == 0, "Selection callback sees the committed model");
    });
    list->on_view_change([&] { ++views; });
    list->set_items(std::make_shared<const std::vector<FileItem>>(
        std::vector<FileItem>{{71, L"Alpha", L"A", false}, {92, L"Beta", L"B", true}}));
    list->select(0);
    list->focus_item(1);
    require(!list->thumbnails(), "FileList thumbnails require explicit opt-in");
    const auto thumbnail_revision = list->thumbnail_revision();
    list->set_thumbnails(true);
    list->reload_thumbnails();
    require(list->thumbnails() && list->thumbnail_revision() == thumbnail_revision + 2 &&
        list->model().selected_id() == 71 && list->focused_id() == 92 && selections == 1,
        "Thumbnail configuration preserves selection, focus, and selection events");
    list->set_thumbnails(false);
    require(selections == 1 && list->model().selected_id() == 71 && list->focused_id() == 92,
        "Item focus is independent of selection and its event");
    const auto source = list->model().view()->source();
    list->set_view(FilteredView::build(source, L"beta"));
    require(views == 2 && list->model().view()->query() == L"beta" && !list->model().selected_index() &&
        list->focused_index() == 0, "View callback and properties preserve hidden selection and visible focus");
    list->set_filter(L"");
    require(views == 3 && list->model().selected_index() == 0 && list->focused_index() == 1,
        "Small synchronous views use the same control events");
    list->set_enabled(false);
    require(next_focus(controls, input.get(), false) == button.get(), "Disabled list is skipped");
    list->on_selection_change({});
    list->on_view_change({});
}
}
int main() {
    try { behavior(); focus_and_lifetime(); virtual_list_control(); sizing_and_scroll(); std::cout << "Control tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
