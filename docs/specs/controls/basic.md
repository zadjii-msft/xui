# Basic controls

[Control catalog](README.md) · [Application contract](../application.md) · [Styling inventory](../control-styling-inventory.md#basic-controls-and-layouts)

These controls cover text, actions, boolean choices, and single-line input.
Examples use the [C++ fragment context](README.md#use-the-examples).
The context includes `xui\application.hpp`.

## Common properties

`Element` supplies layout, identity, and explicit style attachments.
`Control` adds names, enabled state, visibility, focus notifications, help text, and context menus.
Application code normally creates a concrete control, not a base object.

```cpp
auto action = std::make_shared<xui::Button>(L"Apply");
action->set_automation_id(L"apply-preferences");
action->set_help_text(L"Apply these preferences to the current document.");
action->set_minimum_size({100, 36});
action->set_enabled(true);
root->add(action);
```

`set_fixed_size` also sets both size limits.
`set_preferred_size` disables automatic sizing.
Parent constraints still limit the arranged size.
The [size contract](../application.md#content-sizes-and-constraints) explains flex allocation and automatic text measurements.

Hidden controls and disabled ancestors restrict descendant interaction.
`on_focus` reports focus but does not request it.
Use `window.focus(*action)` from an active-window callback.
The framework rejects foreign, closed, or disabled focus targets.

## Label

Use `Label` for noneditable text.
Use `TextInput` or `MultilineText` for user edits.
A label can use a semantic tone, heading typography, or explicit wrapping.

```cpp
auto title = std::make_shared<xui::Label>(L"Workspace preferences");
title->set_heading(true);
auto description = std::make_shared<xui::Label>(
    L"Choose the preferences for this workspace.");
description->set_tone(xui::TextTone::secondary);
description->set_wrapping(true, 3);
root->add(title);
root->add(description);
```

`set_text` updates the visible text and accessible name.
`set_caption`, `set_subtitle`, and `set_body_strong` select other text presentations.
Labels do not expose an action callback or an extra keyboard focus stop.

The style target is `label`.
The active text part is `label`, `caption`, or `heading`, according to `text_style()`.
Semantic tones supply defaults below authored colors.
Wrapping limits do not shorten the accessible name.

## Button

Use a `Button` for an action.
Use `Toggle` for a labeled checkbox.
Use `SplitButton` for independent primary and secondary actions.

```cpp
auto message = std::make_shared<xui::Label>(L"Ready");
auto apply = std::make_shared<xui::Button>(L"Apply preferences");
apply->on_click([message] { message->set_text(L"Preferences applied"); });
root->add(message);
root->add(apply);
```

Button variants use the same constructor:

```cpp
auto repeat = std::make_shared<xui::Button>(L"Increase");
repeat->set_behavior(xui::ButtonBehavior::repeat);
repeat->set_repeat_timing(400, 80);
auto pin = std::make_shared<xui::Button>(L"Pin panel");
pin->set_behavior(xui::ButtonBehavior::toggle);
pin->set_checked(true);
auto more = std::make_shared<xui::Button>(L"More actions");
more->set_behavior(xui::ButtonBehavior::dropdown);
more->set_icon(xui::ButtonIcon::more);
root->add(repeat);
root->add(pin);
root->add(more);
```

`on_click` handles the command.
`on_toggle` reports accepted toggle-action changes.
`set_checked` changes the property without the action callback.
A dropdown appearance does not create a menu. Connect it to [Popup](choices.md#popup) or [CommandSurface](commands.md#commandsurface).

Icon-only buttons retain their name for accessibility.
Pointer capture loss, drag-out release, and window deactivation cancel pending presses.
Enter activates a focused button. Space activates it on release.

The style target is `button`, with text, icon, and dropdown presentation parts.
The older `ButtonStyle` API remains supported.
Use the [shared style contract](../control-styling.md) for precedence and compatibility.

## Toggle

Use `Toggle` for a boolean preference with a checkbox.
Its accessible role and action describe checked state.

```cpp
auto status = std::make_shared<xui::Label>(L"Preview disabled");
auto preview = std::make_shared<xui::Toggle>(L"Show preview");
preview->set_checked(false);
preview->on_change([status](bool value) {
    status->set_text(value ? L"Preview enabled" : L"Preview disabled");
});
root->add(preview);
root->add(status);
```

`checked()` reads the value.
`set_checked` is silent.
Space changes the focused checkbox on release. Enter does not change it.

The style target is `toggle`.
The label, indicator, and check mark are parts of one control, not independently focusable children.
The `checked` state follows the value.

## TextInput

Use `TextInput` for one line of ordinary text.
Use `PasswordInput` for secrets, `NumericInput` for numbers, and native document controls for multiple paragraphs.

```cpp
auto query = std::make_shared<xui::TextInput>(L"Search documents");
query->set_search_style(true);
query->set_placeholder(L"Type a document name");
query->set_maximum_length(256);
auto result = std::make_shared<xui::Label>(L"No search text");
query->on_change([result](const std::wstring& text) {
    result->set_text(text.empty() ? L"No search text" : text);
});
query->on_submit([result] { result->set_tone(xui::TextTone::accent); });
root->add(query);
root->add(result);
```

`name()` is the accessible field name. `text()` is the value.
`set_selection` uses UTF-16 endpoints and does not split surrogate pairs.
`set_text` does not synthesize a user edit.
`on_change` receives committed native text, not IME preedit text.
`on_submit` handles acceptance.

`set_shortcut_hint` shows a hint but does not register a keyboard shortcut.
`set_suggestions` connects a `SuggestionSource`.
The [input contract](../menus-and-input.md) describes asynchronous suggestions and native key ownership.

The style target is `text_input`.
It supports owned frame parts and supported native text colors and fonts.
The placeholder, search icon, and shortcut badge do not replace the native editor.
The `empty` and `focused` states follow the real field.

## NativeEditBridge

`NativeEditBridge` is the public Windows EDIT integration boundary in `xui\native_edit.hpp`.
Use TextInput for ordinary XUI applications.
The bridge serves code that already owns native parent-window integration.

This complete C++ helper belongs at file scope.
It requires a live parent HWND on the calling UI thread:

```cpp
std::unique_ptr<xui::NativeEditBridge> attach_native_editor(HWND parent) {
    auto editor = std::make_unique<xui::NativeEditBridge>();
    editor->attach(parent, 1001);
    editor->set_placeholder(L"Filter");
    editor->set_model_text(L"");
    editor->arrange({0, 0, 320, 40});
    return editor;
}
```

Retain the returned bridge for the native editor lifetime.
Its destructor releases the child window and owned font.
`attach` rejects a second attachment.
Native host code remains responsible for parent message routing, DPI, and lifetime.
`selection` and `set_selection` require the live UI thread.
Selection changes reject active IME composition.

Windows retains text, IME, undo, clipboard, and native accessibility.
This bridge has no generic style target and no C# or Rust factory.
Font, color, inset, and suggestion methods belong to backend integration.
They do not replace the normal TextInput property API.
The [application contract](../application.md#framework-boundaries) describes this native text boundary.

## Next steps

- [Arrange the controls](layout.md).
- [Add numeric and choice fields](choices.md).
- [Add help text and context menus](commands.md).
- [Use C# or Rust](../bindings.md).
