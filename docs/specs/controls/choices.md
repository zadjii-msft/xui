# Choices and values

[Control catalog](README.md) · [Foundation contract](../foundation-controls.md) · [Binding coverage](../bindings.md)

Examples use the [C++ fragment context](README.md#use-the-examples).
The context includes `xui\foundation.hpp` through `application.hpp`.
These controls reject invalid identities, nonfinite numbers, invalid bounds, and invalid timing values.

## RadioGroup

Use `RadioGroup` for one choice from a small, visible set.
Use `ComboBox` to save space. Use `ItemsView` for a large source.

```cpp
auto choices = std::make_shared<xui::RadioGroup>(L"Sort order");
choices->set_items({{1, L"Name"}, {2, L"Date"}, {3, L"Size"}}, 1);
auto status = std::make_shared<xui::Label>(L"Name order");
choices->on_change([status](std::uint64_t id) {
    status->set_text(id == 1 ? L"Name order" : L"Another order");
});
root->add(choices);
root->add(status);
```

Choice IDs must remain stable and unique.
`set_selected` sets the property. `select` performs a semantic selection.
`on_accept` handles acceptance separately from a change.
Arrow keys and prefix input select enabled choices.
The 4,096-item limit does not imply one retained control per item.

The style target is `radio_group`.
Item surfaces, labels, radio indicators, and selected dots share virtual row geometry.
Item selection and disabled states do not become independent child controls.

## ChoiceList

ChoiceList is the list presentation of `RadioGroup`.
It is not a separate public C++ class.
Use this presentation for a short popup choice list without radio circles.

```cpp
auto choices = std::make_shared<xui::RadioGroup>(L"View choices", true);
choices->set_items({{1, L"List"}, {2, L"Tiles"}}, 1);
root->add(choices);
```

The second constructor argument selects the presentation.
The API still uses `set_items`, `set_selected`, `on_change`, and `on_accept`.
Its style target is `choice_list`, not `radio_group`.

C# and Rust expose ComboBox choices as borrowed children.
They do not expose an independent ChoiceList constructor.
Use the [retained-child contract](../bindings.md#retained-composition-children) for that path.

## ComboBox

Use `ComboBox` for a committed choice in a compact field.
The optional editor accepts text without inventing a selected identity.

```cpp
auto format = std::make_shared<xui::ComboBox>(L"Export format", true);
format->set_items({{1, L"Text"}, {2, L"Markdown"}}, 1);
auto status = std::make_shared<xui::Label>(L"Text format");
format->on_change([status](std::uint64_t id) {
    status->set_text(id == 1 ? L"Text format" : L"Markdown format");
});
format->on_edit([status](const std::wstring& text) {
    status->set_text(L"Uncommitted text: " + text);
});
root->add(format);
root->add(status);
```

Enter commits a popup choice. Escape cancels its preview.
`selected()` and `selected_text()` describe the committed identity.
`editor()` is absent for a noneditable ComboBox.
The native editor retains IME, selection, and undo.

The root style target is `combo_box`.
`editor()`, `popup()`, and `choices()` have their own real targets.
Use child styles for native text and choice rows.
An authored header shows the existing name without a new Label.

## NumericInput

Use `NumericInput` for exact numeric text, locale parsing, and spin actions.
Use `RangeInput` for spatial adjustment.

```cpp
auto count = std::make_shared<xui::NumericInput>(L"Copies");
count->set_range({1, 100, 1, 10});
count->set_value(2);
count->set_spin_placement(xui::NumberSpinPlacement::inline_buttons);
auto status = std::make_shared<xui::Label>(L"Copies: 2");
count->on_change([status](double value) {
    status->set_text(L"Copies: " + std::to_wstring(value));
});
root->add(count);
root->add(status);
```

`set_locale` selects parsing and formatting rules.
Invalid text stays visible and preserves the last valid value.
`valid()` reports the current text state.
`step` restores valid formatted text.

The style target is `numeric_input`.
The root supports field and optional header presentation.
`editor()` supplies native text styling.
`decrease_button()` and `increase_button()` are independent Button children.
There are no synthetic parent spin-button parts.

## RangeInput

Use `RangeInput` for a bounded value with keyboard and pointer adjustment.
Preview and accepted values have separate callbacks.

```cpp
auto value = std::make_shared<xui::Label>(L"Volume: 25");
auto volume = std::make_shared<xui::RangeInput>(L"Volume");
volume->set_range({0, 100, 1, 10});
volume->set_value(25);
volume->on_preview([value](double preview) {
    value->set_text(L"Preview: " + std::to_wstring(preview));
});
volume->on_change([value](double committed) {
    value->set_text(L"Volume: " + std::to_wstring(committed));
});
volume->on_cancel([value](double committed) {
    value->set_text(L"Volume: " + std::to_wstring(committed));
});
root->add(volume);
root->add(value);
```

`set_orientation(Axis::vertical)` selects a vertical control.
`set_reversed(true)` reverses its direction.
The UIA RangeValue pattern shares the numeric bounds.
Input cancellation clears the preview before `on_cancel`.
Property-driven cancellation stays silent.

The style target is `range_input`.
Track, fill, and thumb metrics also define hit testing.
The `dragging`, `minimum`, and `maximum` states follow real input and values.

## Progress

Use `Progress` for a read-only measurement or task status.
Use a RangeInput for editable progress-like values.

```cpp
auto task = std::make_shared<xui::Progress>(L"Import progress");
task->set_range(0, 100);
task->set_value(40);
task->set_state(xui::ProgressState::determinate);
auto capacity = std::make_shared<xui::Progress>(L"Storage capacity");
capacity->set_capacity(40, 100, L"GB");
root->add(task);
root->add(capacity);
```

`paused`, `error`, `unknown`, and `indeterminate` describe other states.
Indeterminate progress is static. It has no progress animation or ring.
Capacity text describes used and total values without an active-task claim.

The style target is `progress`.
Caption, track, and fill styles do not alter the value.
There is no action callback or editable UIA value.

## Expander

Use `Expander` for optional details in the existing page.
Use Popup for temporary anchored content.

```cpp
auto details = std::make_shared<xui::Stack>(xui::Axis::vertical);
details->add(std::make_shared<xui::TextInput>(L"Advanced path"));
auto expander = std::make_shared<xui::Expander>(L"Advanced", details);
expander->set_expanded(false);
root->add(expander);
```

`on_change` reports user expansion changes.
Collapsed content retains its state but does not accept input.
Focus returns to the header after a collapse that hides the focused child.

The style target is `expander`.
Header geometry, text, disclosure, and content frames have separate parts.
Fonts belong to the text part, not the root, header frame, or content frame.
The retained content remains application-owned.

## SplitButton

Use `SplitButton` for a primary command and a separate secondary action.
The composition retains two independent keyboard and UIA Button targets.

```cpp
auto status = std::make_shared<xui::Label>(L"Ready");
auto split = std::make_shared<xui::SplitButton>(L"Save", L"Save options");
split->primary()->on_click([status] { status->set_text(L"Saved"); });
split->secondary()->on_click([status] {
    status->set_text(L"Choose a save destination");
});
root->add(split);
root->add(status);
```

The secondary callback can open a Popup or CommandSurface.
The root style target is `split_button`.
The root frame does not accept foreground.
`primary()` and `secondary()` accept Button styles and retain their separate actions.

## Popup

Use `Popup` for retained content anchored to an active control.
Use `ContentDialog` for modal validation and primary/cancel results.

```cpp
auto size = std::make_shared<xui::RangeInput>(L"Preview size");
size->set_range({32, 256, 8, 32});
size->set_value(96);
auto popup = std::make_shared<xui::Popup>(size, L"Preview options");
popup->set_placement(xui::PopupPlacement::below);
popup->set_preferred_size({280, 100});
anchor->on_click([&window, &anchor, popup, size] {
    window.show_popup(popup, *anchor, size.get());
});
```

Do not also add popup content to the page.
The popup retains that content as its single parent.
The anchor must belong to the active window.
Nested popups anchor in the top popup and have a maximum depth of eight.

`on_dismiss` reports cancel, commit, outside input, focus loss, hide, or owner closure.
Escape dismisses the top popup.
Window-managed closure revokes its generation before callbacks and restores eligible focus.
Applications must cancel their own outstanding work.
`current(saved_generation)` rejects obsolete deliveries.

The style target is `popup`.
The root supports frame parts and only `open` plus inherited `disabled` states.
It rejects foreground.
Style changes do not open a hidden popup.
The popup stays inside the client area and monitor work area.
Active native runtime hosts have an [explicit popup restriction](media.md#native-host-boundaries).

## Related contracts

- [Foundation behavior and limits](../foundation-controls.md)
- [Exact style parts](../control-styling-inventory.md#choices-fields-and-progress)
- [Modal document dialogs](documents.md#contentdialog)
- [C# and Rust constructor and child APIs](../bindings.md)
