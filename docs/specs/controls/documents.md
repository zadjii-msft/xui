# Documents and forms

[Control catalog](README.md) · [Document contract](../documents.md) · [Binding coverage](../bindings.md)

Examples use the [C++ fragment context](README.md#use-the-examples).
Add `xui\documents.hpp` at file scope.
Property setters are silent. Native edits and semantic actions call application callbacks.

## MultilineText

Use `MultilineText` for a plain-text document.
Use TextInput for one line and RichText for authored formatting.

```cpp
auto notes = std::make_shared<xui::MultilineText>(L"Notes");
notes->set_maximum_length(65536);
notes->set_text(L"First paragraph\rSecond paragraph");
notes->set_monospace(false);
auto status = std::make_shared<xui::Label>(L"Unchanged");
notes->on_change([status](const std::wstring&) {
    status->set_text(L"Notes changed");
});
root->add(notes, 1);
root->add(status);
```

`set_read_only` preserves selection without user edits.
`set_selection` uses `TextSelection` with UTF-16 endpoints.
`command(TextCommand::copy)` and other editing commands require an attached native peer.
Windows RichEdit owns selection, caret movement, clipboard, undo, and IME.

The default limit is 65,536 UTF-16 units. The maximum is 1,048,576.
Paragraphs use `\r`.
Setters normalize line endings and reject NUL or unpaired surrogates.

The style target is `multiline_text`.
Supported frame, native background, text foreground, and default font properties do not replace RichEdit.
There is no header part.
The native RichEdit automation ID does not use the custom `Control::set_automation_id` override.

## RichText

Use `RichText` for authored bold, italic, underline, and explicit links.
It is not an HTML or RTF importer.

```cpp
auto document = std::make_shared<xui::RichText>(L"Release notes");
document->set_read_only(true);
document->set_runs({
    {L"Release notes\r", true},
    {L"Read the documentation", false, false, true,
        L"https://example.com/docs"}
});
auto target = std::make_shared<xui::Label>(L"No link selected");
document->on_link([target](const std::wstring& uri) {
    target->set_text(uri);
});
root->add(document, 1);
root->add(target);
```

Link activation requests a callback. XUI does not open the URL automatically.
The application decides whether to open an accepted target.
The control accepts up to 4,096 runs with explicit HTTP or HTTPS links.
Plain-text paste does not import embedded objects.

`DocumentText` supplies the same text, selection, command, and change APIs as MultilineText.
Its constructor is protected.
The style target is `rich_text`.
Default text styles do not replace existing authored runs.
Native text accessibility remains RichEdit-owned.

## PasswordInput

Use `PasswordInput` for masked secret entry.
Do not put the password in a Label, accessible name, event payload, or log.

```cpp
auto password = std::make_shared<xui::PasswordInput>(L"Account password");
password->set_maximum_length(256);
password->set_reveal_policy(xui::PasswordRevealPolicy::never);
auto status = std::make_shared<xui::Label>(L"No password change");
password->on_change([status] {
    status->set_text(L"Password changed");
});
anchor->on_click([password, status] {
    password->with_password([status](std::wstring_view secret) {
        status->set_text(secret.empty() ? L"Password required" : L"Password supplied");
    });
});
root->add(password);
root->add(status);
```

`with_password` is the explicit read boundary.
The example reports presence without a copy of the secret.
C# supplies a scoped UTF-8 span. Rust supplies a borrowed byte slice.
Application code must not retain either borrowed view.

The default limit is 256 UTF-16 units. The maximum is 4,096.
The native editor stays masked even during an explicitly permitted reveal preview.
Copy, cut, and the native context menu are disabled.
UIA reports a password and does not return plaintext.
This control is not a credential vault.

The style target is `password_input`.
Supported native text and frame styles do not expose the secret.
There is no header part or synthetic reveal Button part.

## DateTimePicker

Use `DateTimePicker` for local Gregorian date or time fields.
It does not represent a UTC instant or time zone.

```cpp
auto date = std::make_shared<xui::DateTimePicker>(
    L"Start date", xui::DateTimePresentation::date);
date->set_value({2026, 9, 16, 9, 30, 0});
date->set_range({2026, 1, 1}, {2026, 12, 31, 23, 59, 59});
auto time = std::make_shared<xui::DateTimePicker>(
    L"Start time", xui::DateTimePresentation::time);
time->set_value({2026, 9, 16, 9, 30, 0});
auto calendar = std::make_shared<xui::DateTimePicker>(
    L"Calendar", xui::DateTimePresentation::calendar);
root->add(date);
root->add(time);
root->add(calendar);
```

`on_change` receives a `DateTimeValue`.
Date edits preserve time fields. Time edits preserve date fields.
The supported years are 1601 through 9999.
A new range must include the current value.
Windows supplies locale formats and native accessibility.

The style target is `date_time_picker` for all three presentations.
The owned frame and native font support styling.
Native content background and foreground reject.
Calendar internals remain Windows-owned.

## InlineStatus

Use `InlineStatus` for a message with severity, an optional action, and optional dismissal.
Use a Label for ordinary text without status semantics.

```cpp
auto status = std::make_shared<xui::InlineStatus>(L"Preferences saved");
status->set_message(L"Preferences saved", xui::StatusSeverity::success);
status->set_dismissible(true);
status->set_action(L"View details", [&window] {
    window.set_title(L"Saved preferences");
});
root->add(status);
```

`show` restores a dismissed status.
`on_dismiss` reports dismissal.
The action and dismiss buttons remain independent.
Severity has an accessible prefix and a visible symbol, not only color.
Error messages use assertive live-region semantics. Other severities use polite semantics.
Actual screen-reader speech still needs a manual check.

The style target is `inline_status`.
Its message, accent, and severity icon are parts.
`action_button()` and `dismiss_button()` are retained Button children.

## ColorPicker

Use `ColorPicker` for unpremultiplied sRGB byte values.
Four numeric channels and optional swatches provide keyboard access.

```cpp
auto color = std::make_shared<xui::ColorPicker>(L"Highlight color");
color->set_value({32, 96, 192, 255});
color->set_swatches({{32, 96, 192, 255}, {192, 64, 32, 128}});
auto status = std::make_shared<xui::Label>(L"Color ready");
color->on_change([status](xui::RgbaColor value) {
    status->set_text(L"Alpha: " + std::to_wstring(value.alpha));
});
root->add(color);
root->add(status);
```

Alpha changes the checkerboard preview.
Fractional numeric edits round to the nearest byte.
Up to 16 swatches expose distinct accessible RGBA names.

The style target is `color_picker`.
Frame, checkerboard, preview border, and channel labels support presentation changes.
RGBA values remain authored data.
`channels()` returns four NumericInput children.
`swatch_button(index)` returns a retained Button.
The unsupported `channel_field` part does not replace those children.

## ContentDialog

Use `ContentDialog` for modal content with validation and primary/cancel results.
Use Popup for nonmodal anchored options.
Use `Window::confirm` for the separate native confirmation service.

```cpp
auto notes = std::make_shared<xui::MultilineText>(L"Notes");
auto dialog = std::make_shared<xui::ContentDialog>(L"Edit notes", notes);
dialog->on_validate([notes] {
    return notes->text().empty() ? std::wstring(L"Enter a note.") : std::wstring{};
});
auto result = std::make_shared<xui::Label>(L"Dialog not opened");
dialog->on_result([result](xui::DialogResult value) {
    result->set_text(value == xui::DialogResult::primary ? L"Accepted" : L"Canceled");
});
anchor->on_click([&window, &anchor, dialog, notes] {
    window.show_dialog(dialog, *anchor, notes.get());
});
root->add(result);
```

Do not add `notes` to the root. The dialog already retains it.
Validation returns an error message or an empty string.
Invalid content stays open.
Owner controls are disabled while the dialog is open.
Tab stays inside the top popup.
Enter runs the primary action outside multiline documents.
Escape cancels after native composition ends.

The facade has no independent style target.
`popup()` accepts `StyleTarget::popup`, with only `open` and inherited `disabled`.
Root `invalid`, `loading`, and `error` rules reject.
Validation error styling belongs to `validation()`, a retained InlineStatus.
`title()`, `body()`, `footer()`, `primary()`, and `cancel_button()` retain their own targets.

Result callbacks run after dismissal and generation revocation.
The contract protects a newly reopened dialog from an obsolete validation result.
Application captures must still respect ownership and lifetime.

## Related contracts

- [Full document, secret, and dialog behavior](../documents.md)
- [Native styling boundaries](../control-styling-inventory.md#choices-fields-and-progress)
- [Bound secret access and dialog children](../bindings.md)
