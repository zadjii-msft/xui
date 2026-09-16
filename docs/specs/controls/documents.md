# Documents and forms

[Control catalog](README.md) · [Document contract](../documents.md) · [Binding coverage](../bindings.md)

Examples use the [shared fragment context](README.md#use-the-examples).
For C++, add `xui\documents.hpp` at file scope.
Property setters are silent. Native edits and semantic actions call application callbacks.

The `.xui` compiler has no document-control node names.
Each `.xui` example requires C# to create and configure the existing element before `Content` embeds it.
Each component attaches its own Stack root to the window.
Do not add its mounted elements to another parent.
Use each example independently on the window's UI thread.
The C# fragments require `using System;` and `using Xui;` at file scope.
The Rust fragments require `use xui::*;` and the fallible function context from the shared setup.
Rust callbacks retain weak handles and upgrade them only for the callback.

## MultilineText

Use `MultilineText` for a plain-text document.
Use TextInput for one line and RichText for authored formatting.

{% tabs %}
{% tab title=".xui" %}

```xui
namespace ControlExamples;
component NotesDocument {
    param global::Xui.Element Notes;
    param global::Xui.Element Status;
    view {
        VStack() {
            Content(Notes, flex: 1);
            Content(Status);
        }
    }
}
```

Create the native editor in C# before you construct the component:

```csharp
var notes = window.MultilineText("Notes").SetMaximumLength(65536)
    .SetDocument("First paragraph\rSecond paragraph");
var status = window.Label("Unchanged");
notes.Event += e =>
{
    if (e.Kind == EventKind.Change) status.Text = "Notes changed";
};
var view = new ControlExamples.NotesDocument(window, notes, status);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var notes = window.MultilineText("Notes").SetMaximumLength(65536)
    .SetDocument("First paragraph\rSecond paragraph");
var status = window.Label("Unchanged");
notes.Event += e =>
{
    if (e.Kind == EventKind.Change) status.Text = "Notes changed";
};
root.Add(notes, 1);
root.Add(status);
```

The binding has no `SetMonospace` method.

{% endtab %}
{% tab title="Rust" %}

```rust
let notes = window.multiline_text("Notes")?;
notes.set_maximum_length(65536)?;
notes.set_document("First paragraph\rSecond paragraph")?;
let status = window.label("Unchanged")?;
let changed_status = status.downgrade();
notes.on_event(move |event| {
    if event.kind == 2 {
        let Some(changed_status) = changed_status.upgrade() else { return Ok(()); };
        if let Err(error) = changed_status.set_text("Notes changed") {
            eprintln!("Notes status failed: {error}");
            return Err(error);
        }
    }
    Ok(())
})?;
root.add(&notes, 1.)?;
root.add(&status, 0.)?;
```

Event kind `2` is a change. The binding has no `set_monospace` method.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

```xui
namespace ControlExamples;
component ReleaseDocument {
    param global::Xui.Element Document;
    view {
        VStack() {
            Content(Document, flex: 1);
        }
    }
}
```

The C# prerequisite supplies the authored runs.
The C# binding does not expose the native link callback.

```csharp
var document = window.RichText("Release notes").SetReadOnly(true);
document.SetRuns([
    new TextRun("Release notes\r", Bold: true),
    new TextRun("Read the documentation", Underline: true,
        Link: "https://example.com/docs")
]);
var view = new ControlExamples.ReleaseDocument(window, document);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var document = window.RichText("Release notes").SetReadOnly(true);
document.SetRuns([
    new TextRun("Release notes\r", Bold: true),
    new TextRun("Read the documentation", Underline: true,
        Link: "https://example.com/docs")
]);
root.Add(document, 1);
```

Authored links are supported. The native link callback is unavailable in C#.

{% endtab %}
{% tab title="Rust" %}

```rust
let document = window.rich_text("Release notes")?;
document.set_read_only(true)?;
document.set_runs(&[
    TextRun {
        text: "Release notes\r".into(), bold: true, italic: false,
        underline: false, link: String::new(),
    },
    TextRun {
        text: "Read the documentation".into(), bold: false, italic: false,
        underline: true, link: "https://example.com/docs".into(),
    },
])?;
root.add(&document, 1.)?;
```

Authored links are supported. The native link callback is unavailable in Rust.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

```xui
namespace ControlExamples;
component AccountSecret {
    param global::Xui.Element Password;
    param global::Xui.Element Status;
    param global::Xui.Element Check;
    view {
        VStack() {
            Content(Password);
            Content(Check);
            Content(Status);
        }
    }
}
```

The C# prerequisite owns the secret-access callback.
The component never receives the secret.

```csharp
var password = window.PasswordInput("Account password")
    .SetMaximumLength(256).SetRevealAllowed(false);
var status = window.Label("No password change");
var check = window.Button("Check password");
password.OnChange(() => status.Text = "Password changed");
check.Click += () => password.WithPassword(secret =>
    status.Text = secret.IsEmpty ? "Password required" : "Password supplied");
var view = new ControlExamples.AccountSecret(window, password, status, check);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var password = window.PasswordInput("Account password")
    .SetMaximumLength(256).SetRevealAllowed(false);
var status = window.Label("No password change");
password.OnChange(() => status.Text = "Password changed");
anchor.Click += () => password.WithPassword(secret =>
    status.Text = secret.IsEmpty ? "Password required" : "Password supplied");
root.Add(password);
root.Add(status);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let password = window.password_input("Account password")?;
password.set_maximum_length(256)?;
password.set_reveal_allowed(false)?;
let status = window.label("No password change")?;
let changed_status = status.downgrade();
password.on_change(move || {
    let Some(changed_status) = changed_status.upgrade() else { return Ok(()); };
    changed_status.set_text("Password changed").map_err(|error| {
        eprintln!("Password status failed: {error}");
        error
    })
})?;
let read_password = password.weak();
let read_status = status.downgrade();
anchor.on_event(move |event| {
    if event.kind == 1 {
        let (Some(read_password), Some(read_status)) =
            (read_password.upgrade(), read_status.upgrade()) else { return Ok(()); };
        read_password.with_password(|secret| {
            read_status.set_text(if secret.is_empty() {
                "Password required"
            } else {
                "Password supplied"
            })
        }).map_err(|error| {
            eprintln!("Password presence check failed: {error}");
            error
        })?;
    }
    Ok(())
})?;
root.add(password.as_element(), 0.)?;
root.add(&status, 0.)?;
```

Event kind `1` is a click. Error messages do not include the secret.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

```xui
namespace ControlExamples;
component LocalSchedule {
    param global::Xui.Element Date;
    param global::Xui.Element Time;
    param global::Xui.Element Calendar;
    view {
        VStack() {
            Content(Date);
            Content(Time);
            Content(Calendar);
        }
    }
}
```

Create the three native presentations in C#.
The binding has no date-range setter.

```csharp
var value = new DateTime(2026, 9, 16, 9, 30, 0, DateTimeKind.Unspecified);
var date = window.DateTimePicker("Start date", DateTimePresentation.Date).SetValue(value);
var time = window.DateTimePicker("Start time", DateTimePresentation.Time).SetValue(value);
var calendar = window.DateTimePicker("Calendar", DateTimePresentation.Calendar);
var view = new ControlExamples.LocalSchedule(window, date, time, calendar);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var value = new DateTime(2026, 9, 16, 9, 30, 0, DateTimeKind.Unspecified);
var date = window.DateTimePicker("Start date", DateTimePresentation.Date).SetValue(value);
var time = window.DateTimePicker("Start time", DateTimePresentation.Time).SetValue(value);
var calendar = window.DateTimePicker("Calendar", DateTimePresentation.Calendar);
root.Add(date);
root.Add(time);
root.Add(calendar);
```

The binding requires `DateTimeKind.Unspecified` and has no date-range setter.

{% endtab %}
{% tab title="Rust" %}

```rust
let value = LocalDateTime {
    year: 2026, month: 9, day: 16, hour: 9, minute: 30, second: 0,
};
let date = window.date_time_picker("Start date", DateTimePresentation::Date)?;
date.set_value(value)?;
let time = window.date_time_picker("Start time", DateTimePresentation::Time)?;
time.set_value(value)?;
let calendar = window.date_time_picker("Calendar", DateTimePresentation::Calendar)?;
root.add(&date, 0.)?;
root.add(&time, 0.)?;
root.add(&calendar, 0.)?;
```

The binding has no date-range setter.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

```xui
namespace ControlExamples;
component SavedPreferences {
    param global::Xui.Element Status;
    view {
        VStack() {
            Content(Status);
        }
    }
}
```

Create and configure the status in C# before composition:

```csharp
var status = window.InlineStatus("Preferences saved")
    .SetMessage("Preferences saved", StatusSeverity.Success).SetDismissible(true);
var view = new ControlExamples.SavedPreferences(window, status);
```

The C# binding does not expose action configuration.

{% endtab %}
{% tab title="C#" %}

```csharp
var status = window.InlineStatus("Preferences saved")
    .SetMessage("Preferences saved", StatusSeverity.Success).SetDismissible(true);
root.Add(status);
```

Dismissal is supported. The native `set_action` API is unavailable in C#.

{% endtab %}
{% tab title="Rust" %}

```rust
let status = window.inline_status("Preferences saved")?;
status.set_message("Preferences saved", StatusSeverity::Success)?;
status.set_dismissible(true)?;
root.add(&status, 0.)?;
```

Dismissal is supported. The native `set_action` API is unavailable in Rust.

{% endtab %}
{% tab title="C++" %}

```cpp
auto status = std::make_shared<xui::InlineStatus>(L"Preferences saved");
status->set_message(L"Preferences saved", xui::StatusSeverity::success);
status->set_dismissible(true);
status->set_action(L"View details", [&window] {
    window.set_title(L"Saved preferences");
});
root->add(status);
```

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

```xui
namespace ControlExamples;
component HighlightColor {
    param global::Xui.Element Color;
    param global::Xui.Element Status;
    view {
        VStack() {
            Content(Color);
            Content(Status);
        }
    }
}
```

The C# prerequisite supplies the color and change callback.
The binding has no swatch-configuration setter.

```csharp
var color = window.ColorPicker("Highlight color").SetValue(new(32, 96, 192, 255));
var status = window.Label("Color ready");
color.Event += e =>
{
    if (e.Kind == EventKind.Change) status.Text = $"Alpha: {(byte)(e.Value >> 24)}";
};
var view = new ControlExamples.HighlightColor(window, color, status);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var color = window.ColorPicker("Highlight color").SetValue(new(32, 96, 192, 255));
var status = window.Label("Color ready");
color.Event += e =>
{
    if (e.Kind == EventKind.Change) status.Text = $"Alpha: {(byte)(e.Value >> 24)}";
};
root.Add(color);
root.Add(status);
```

The binding exposes channel and swatch children, but not swatch configuration.

{% endtab %}
{% tab title="Rust" %}

```rust
let color = window.color_picker("Highlight color")?;
color.set_value(RgbaColor { red: 32, green: 96, blue: 192, alpha: 255 })?;
let status = window.label("Color ready")?;
let changed_status = status.downgrade();
color.on_event(move |event| {
    if event.kind == 2 {
        let Some(changed_status) = changed_status.upgrade() else { return Ok(()); };
        changed_status.set_text(&format!("Alpha: {}", (event.value >> 24) as u8))
            .map_err(|error| {
                eprintln!("Color status failed: {error}");
                error
            })?;
    }
    Ok(())
})?;
root.add(&color, 0.)?;
root.add(&status, 0.)?;
```

Event kind `2` is a change. The binding has no swatch-configuration setter.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

```xui
namespace ControlExamples;
component NotesDialogLauncher {
    param global::Xui.Element Open;
    param global::Xui.Element Result;
    view {
        VStack() {
            Content(Open);
            Content(Result);
        }
    }
}
```

Create the editor and dialog in C#.
Only the dialog retains the editor. The component mounts the launcher and result label.

```csharp
var notes = window.MultilineText("Notes");
var dialog = window.ContentDialog("Edit notes", notes)
    .SetValidationMessage("Enter a note.");
notes.Event += e =>
{
    if (e.Kind == EventKind.Change)
        dialog.SetValidationMessage(notes.Text.Length == 0 ? "Enter a note." : "");
};
var result = window.Label("Dialog not opened");
var open = window.Button("Edit notes");
dialog.OnResult(accepted => result.Text = accepted ? "Accepted" : "Canceled");
open.Click += () => dialog.Show(open);
var view = new ControlExamples.NotesDialogLauncher(window, open, result);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var notes = window.MultilineText("Notes");
var dialog = window.ContentDialog("Edit notes", notes)
    .SetValidationMessage("Enter a note.");
notes.Event += e =>
{
    if (e.Kind == EventKind.Change)
        dialog.SetValidationMessage(notes.Text.Length == 0 ? "Enter a note." : "");
};
var result = window.Label("Dialog not opened");
dialog.OnResult(accepted => result.Text = accepted ? "Accepted" : "Canceled");
anchor.Click += () => dialog.Show(anchor);
root.Add(result);
```

The binding supplies a stored validation message, not a validation callback.
After a programmatic document change, update that message explicitly.

{% endtab %}
{% tab title="Rust" %}

```rust
let notes = window.multiline_text("Notes")?;
let dialog = window.content_dialog("Edit notes", &notes)?;
dialog.set_validation_message("Enter a note.")?;
let edited_notes = notes.downgrade();
let edited_dialog = dialog.weak();
notes.on_event(move |event| {
    if event.kind == 2 {
        let (Some(edited_notes), Some(edited_dialog)) =
            (edited_notes.upgrade(), edited_dialog.upgrade()) else { return Ok(()); };
        let update = || -> xui::Result<()> {
            edited_dialog.set_validation_message(if edited_notes.text()?.is_empty() {
                "Enter a note."
            } else {
                ""
            })
        };
        update().map_err(|error| {
            eprintln!("Dialog validation update failed: {error}");
            error
        })?;
    }
    Ok(())
})?;
let result = window.label("Dialog not opened")?;
let result_label = result.downgrade();
dialog.on_event(move |event| {
    if event.kind == 10 {
        let Some(result_label) = result_label.upgrade() else { return Ok(()); };
        result_label.set_text(if event.value == 0 { "Accepted" } else { "Canceled" })
            .map_err(|error| {
                eprintln!("Dialog result failed: {error}");
                error
            })?;
    }
    Ok(())
})?;
let open_dialog = dialog.weak();
let open_anchor = anchor.downgrade();
anchor.on_event(move |event| {
    if event.kind == 1 {
        let (Some(open_dialog), Some(open_anchor)) =
            (open_dialog.upgrade(), open_anchor.upgrade()) else { return Ok(()); };
        open_dialog.show(&open_anchor).map_err(|error| {
            eprintln!("Dialog open failed: {error}");
            error
        })?;
    }
    Ok(())
})?;
root.add(&result, 0.)?;
```

Event kinds `1`, `2`, and `10` mean click, change, and dismissal.
The binding supplies a stored validation message, not a validation callback.
After a programmatic document change, update that message explicitly.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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
