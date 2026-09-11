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
    try { behavior(); focus_and_lifetime(); virtual_list_control(); std::cout << "Control tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
