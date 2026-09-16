# Basic controls

[Control catalog](README.md) · [Application contract](../application.md) · [Styling inventory](../control-styling-inventory.md#basic-controls-and-layouts)

These controls cover text, actions, boolean choices, and single-line input.
Examples use the [language fragment contexts](README.md#use-the-examples).
The C++ context includes `xui\application.hpp`.

## Common properties

`Element` supplies layout, identity, and explicit style attachments.
`Control` adds names, enabled state, visibility, focus notifications, help text, and context menus.
Application code normally creates a concrete control, not a base object.

{% tabs %}
{% tab title=".xui" %}

```text
namespace ControlExamples;
component ApplyProperties {
    view {
        VStack() {
            Button("Apply", ref: Apply, id: "apply-preferences",
                help: "Apply these preferences to the current document.", enabled: true);
        }
    }
}
```

C# sets the minimum size after component construction:

```csharp
var view = new ControlExamples.ApplyProperties(window);
view.Apply.MinimumSize(100, 36);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var action = window.Button("Apply")
    .SetAutomationId("apply-preferences")
    .Help("Apply these preferences to the current document.")
    .MinimumSize(100, 36)
    .SetEnabled(true);
root.Add(action);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let action = window.button("Apply")?;
window.update(&[
    Property { text: "apply-preferences", ..Property::new(&action, PropertyKind::AutomationId) },
    Property { a: 100.0, b: 36.0, ..Property::new(&action, PropertyKind::MinimumSize) },
    Property { integer: 1, ..Property::new(&action, PropertyKind::Enabled) },
])?;
action.help("Apply these preferences to the current document.")?;
root.add(&action, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto action = std::make_shared<xui::Button>(L"Apply");
action->set_automation_id(L"apply-preferences");
action->set_help_text(L"Apply these preferences to the current document.");
action->set_minimum_size({100, 36});
action->set_enabled(true);
root->add(action);
```

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

These styles supply heading typography and wrapping.
The C# binding has no semantic heading or tone setter.

```text
namespace ControlExamples;
component WorkspaceLabels {
    style Title for Label {
        part label { fontSize: 24; fontWeight: 600; }
    }
    style Description for Label {
        part label { wrapping: true; maximumLines: 3; }
    }
    view {
        VStack() {
            Text("Workspace preferences", style: Title);
            Text("Choose the preferences for this workspace.", style: Description);
        }
    }
}
```

{% endtab %}
{% tab title="C#" %}

The binding uses explicit typography instead of the C++ heading and tone setters.

```csharp
var title = window.Label("Workspace preferences");
title.SetControlStyleValues(StylePart.Label, new PartStyleValues {
    FontSize = 24, FontWeight = 600
});
var description = window.Label("Choose the preferences for this workspace.");
description.SetControlStyleValues(StylePart.Label, new PartStyleValues {
    Wrapping = true, MaximumLines = 3
});
root.Add(title).Add(description);
```

{% endtab %}
{% tab title="Rust" %}

The binding uses explicit typography instead of the C++ heading and tone setters.

```rust
let title = window.label("Workspace preferences")?;
title.set_control_style_values(StylePart::Label, PartStyleValues {
    font_size: Some(24.0), font_weight: Some(600), ..Default::default()
})?;
let description = window.label("Choose the preferences for this workspace.")?;
description.set_control_style_values(StylePart::Label, PartStyleValues {
    wrapping: Some(true), maximum_lines: Some(3), ..Default::default()
})?;
root.add(&title, 0.0)?;
root.add(&description, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

```text
namespace ControlExamples;
component ApplyAction {
    state string Message = "Ready";
    view {
        VStack() {
            Text(Message);
            Button("Apply preferences", click: Apply);
        }
    }
    code csharp {
        void Apply() => Message = "Preferences applied";
    }
}
```

{% endtab %}
{% tab title="C#" %}

```csharp
var message = window.Label("Ready");
var apply = window.Button("Apply preferences");
apply.Click += () => message.Text = "Preferences applied";
root.Add(message).Add(apply);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let message = window.label("Ready")?;
let apply = window.button("Apply preferences")?;
let output = message.downgrade();
apply.on_event(move |event| {
    let Some(output) = output.upgrade() else { return Ok(()); };
    if event.kind == 1 {
        output.set_text("Preferences applied").map_err(|error| {
            eprintln!("Apply callback: {error}");
            error
        })?;
    }
    Ok(())
})?;
root.add(&message, 0.0)?;
root.add(&apply, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto message = std::make_shared<xui::Label>(L"Ready");
auto apply = std::make_shared<xui::Button>(L"Apply preferences");
apply->on_click([message] { message->set_text(L"Preferences applied"); });
root->add(message);
root->add(apply);
```

{% endtab %}
{% endtabs %}

Button variants use the same constructor:

{% tabs %}
{% tab title=".xui" %}

C# configures behavior through the generated references.
The binding has no silent checked-state setter for Button, so the Pin button starts unchecked.

```text
namespace ControlExamples;
component ButtonVariants {
    view {
        VStack() {
            Button("Increase", ref: Repeat);
            Button("Pin panel", ref: Pin);
            Button("More actions", ref: More, icon: global::Xui.ButtonIcon.More);
        }
    }
}
```

```csharp
var view = new ControlExamples.ButtonVariants(window);
view.Repeat.Behavior(ButtonBehavior.Repeat).RepeatTiming(400, 80);
view.Pin.Behavior(ButtonBehavior.Toggle);
view.More.Behavior(ButtonBehavior.Dropdown);
```

{% endtab %}
{% tab title="C#" %}

The binding has no silent checked-state setter for Button, so the Pin button starts unchecked.

```csharp
var repeat = window.Button("Increase")
    .Behavior(ButtonBehavior.Repeat).RepeatTiming(400, 80);
var pin = window.Button("Pin panel").Behavior(ButtonBehavior.Toggle);
var more = window.Button("More actions")
    .Behavior(ButtonBehavior.Dropdown).SetIcon(ButtonIcon.More);
root.Add(repeat).Add(pin).Add(more);
```

{% endtab %}
{% tab title="Rust" %}

Rust exposes the behaviors but has no button-icon setter.
It has no silent checked-state setter for Button, so the Pin button starts unchecked.

```rust
let repeat = window.button("Increase")?;
repeat.behavior(ButtonBehavior::Repeat)?;
repeat.repeat_timing(400, 80)?;
let pin = window.button("Pin panel")?;
pin.behavior(ButtonBehavior::Toggle)?;
let more = window.button("More actions")?;
more.behavior(ButtonBehavior::Dropdown)?;
root.add(&repeat, 0.0)?;
root.add(&pin, 0.0)?;
root.add(&more, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

```text
namespace ControlExamples;
component PreviewPreference {
    state bool Preview = false;
    view {
        VStack() {
            Toggle("Show preview", checked: Preview, change: ChangePreview);
            Text(Preview ? "Preview enabled" : "Preview disabled");
        }
    }
    code csharp {
        void ChangePreview(bool value) => Preview = value;
    }
}
```

{% endtab %}
{% tab title="C#" %}

```csharp
var status = window.Label("Preview disabled");
var preview = window.Toggle("Show preview").SetChecked(false);
preview.Changed += value => status.Text = value ? "Preview enabled" : "Preview disabled";
root.Add(preview).Add(status);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let status = window.label("Preview disabled")?;
let preview = window.toggle("Show preview")?;
preview.checked(false)?;
let output = status.downgrade();
preview.on_event(move |event| {
    let Some(output) = output.upgrade() else { return Ok(()); };
    if event.kind == 2 {
        output.set_text(if event.value != 0 { "Preview enabled" } else { "Preview disabled" })
            .map_err(|error| {
                eprintln!("Preview callback: {error}");
                error
            })?;
    }
    Ok(())
})?;
root.add(&preview, 0.0)?;
root.add(&status, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

`checked()` reads the value.
`set_checked` is silent.
Space changes the focused checkbox on release. Enter does not change it.

The style target is `toggle`.
The label, indicator, and check mark are parts of one control, not independently focusable children.
The `checked` state follows the value.

## TextInput

Use `TextInput` for one line of ordinary text.
Use `PasswordInput` for secrets, `NumericInput` for numbers, and native document controls for multiple paragraphs.

{% tabs %}
{% tab title=".xui" %}

The binding has no search-style, maximum-length, or semantic tone setter for `TextInput`.
This example reports acceptance through text.

```text
namespace ControlExamples;
component DocumentSearch {
    state string Result = "No search text";
    view {
        VStack() {
            TextInput("Search documents", placeholder: "Type a document name",
                change: ChangeQuery, submit: SubmitQuery);
            Text(Result);
        }
    }
    code csharp {
        void ChangeQuery(string text) => Result = text.Length == 0 ? "No search text" : text;
        void SubmitQuery() => Result = "Accepted: " + Result;
    }
}
```

{% endtab %}
{% tab title="C#" %}

The binding has no search-style, maximum-length, or semantic tone setter for this example.

```csharp
var query = window.TextInput("Search documents").SetPlaceholder("Type a document name");
var result = window.Label("No search text");
query.Changed += text => result.Text = text.Length == 0 ? "No search text" : text;
query.Submitted += () => result.Text = "Accepted: " + result.Text;
root.Add(query).Add(result);
```

{% endtab %}
{% tab title="Rust" %}

Rust has no placeholder, search-style, maximum-length, or semantic tone setter for this example.
The native input still reports committed text and submission.

```rust
let query = window.text_input("Search documents")?;
let result = window.label("No search text")?;
let input = query.downgrade();
let output = result.downgrade();
query.on_event(move |event| {
    let (Some(input), Some(output)) = (input.upgrade(), output.upgrade()) else { return Ok(()); };
    let update = || -> xui::Result<()> {
        if event.kind == 2 {
            let text = input.text()?;
            output.set_text(if text.is_empty() { "No search text" } else { &text })?;
        } else if event.kind == 3 {
            output.set_text(&format!("Accepted: {}", output.text()?))?;
        }
        Ok(())
    };
    update().map_err(|error| {
        eprintln!("Search callback: {error}");
        error
    })
})?;
root.add(&query, 0.0)?;
root.add(&result, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

Unavailable. `NativeEditBridge` is not an `Element` and has no C# factory.
The `Content` node cannot embed this native integration helper.
Use the `TextInput` example for application markup.

{% endtab %}
{% tab title="C#" %}

Unavailable. The binding has no `NativeEditBridge` factory or native parent-message integration API.
Use `Window.TextInput` for an ordinary native text field.

{% endtab %}
{% tab title="Rust" %}

Unavailable. The binding has no `NativeEditBridge` factory or native parent-message integration API.
Use `Window::text_input` for an ordinary native text field.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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
