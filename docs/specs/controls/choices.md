# Choices and values

[Control catalog](README.md) · [Foundation contract](../foundation-controls.md) · [Binding coverage](../bindings.md)

Examples use the [language fragment contexts](README.md#use-the-examples).
The C++ context includes `xui\foundation.hpp` through `application.hpp`.
These controls reject invalid identities, nonfinite numbers, invalid bounds, and invalid timing values.

## RadioGroup

Use `RadioGroup` for one choice from a small, visible set.
Use `ComboBox` to save space. Use `ItemsView` for a large source.

{% tabs %}
{% tab title=".xui" %}

`RadioGroup` has no markup constructor.
This component requires C# controls from the same window, without existing parents.

```text
namespace ControlExamples;
component SortOrder {
    param global::Xui.RadioGroup Choices;
    param global::Xui.Label Status;
    view {
        VStack() {
            Content(Choices);
            Content(Status);
        }
    }
}
```

C# constructs the controls and connects selection:

```csharp
var choices = window.RadioGroup("Sort order");
choices.SetItems([new(1, "Name"), new(2, "Date"), new(3, "Size")], 1);
var status = window.Label("Name order");
choices.Event += e => {
    if (e.Kind == EventKind.Selection)
        status.Text = e.Value == 1 ? "Name order" : "Another order";
};
var view = new ControlExamples.SortOrder(window, choices, status);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var choices = window.RadioGroup("Sort order");
choices.SetItems([new(1, "Name"), new(2, "Date"), new(3, "Size")], 1);
var status = window.Label("Name order");
choices.Event += e => {
    if (e.Kind == EventKind.Selection)
        status.Text = e.Value == 1 ? "Name order" : "Another order";
};
root.Add(choices).Add(status);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let choices = window.radio_group("Sort order")?;
choices.set_items(&[
    Choice { id: 1, text: "Name".into(), enabled: true, version: 0 },
    Choice { id: 2, text: "Date".into(), enabled: true, version: 0 },
    Choice { id: 3, text: "Size".into(), enabled: true, version: 0 },
], Some(1))?;
let status = window.label("Name order")?;
let output = status.downgrade();
choices.on_event(move |event| {
    let Some(output) = output.upgrade() else { return Ok(()); };
    if event.kind == 5 {
        output.set_text(if event.value == 1 { "Name order" } else { "Another order" })
            .map_err(|error| {
                eprintln!("Sort callback: {error}");
                error
            })?;
    }
    Ok(())
})?;
root.add(&choices, 0.0)?;
root.add(&status, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

Choice IDs must remain stable and unique.
`set_selected` sets the property. `select` performs a semantic selection.
`on_accept` handles acceptance separately from a change.
Arrow keys and prefix input select enabled choices.
The 4,096-item limit does not imply one retained control per item.

The style target is `radio_group`.
Item surfaces, labels, radio indicators, and selected dots share virtual row geometry.
Item selection and disabled states do not become independent child controls.

## SelectorBar

Use `SelectorBar` for one selection in a horizontal row.
It derives from RadioGroup and uses the same stable ChoiceItem IDs.
The items remain virtual entries, not separate retained Buttons.

{% tabs %}
{% tab title=".xui" %}

```text
namespace ControlExamples;
component SortSelector {
    state (global::Xui.Choice[] Items, ulong? Selected) Options =
        (new global::Xui.Choice[] { new(1, "Name"), new(2, "Date") }, 1UL);
    view {
        VStack() {
            SelectorBar("Sort order", items: Options.Items,
                selected: Options.Selected, change: ChangeOrder);
        }
    }
    code csharp {
        void ChangeOrder(ulong value) => Options = (Options.Items, value);
    }
}
```

{% endtab %}
{% tab title="C#" %}

```csharp
var status = window.Label("Name order");
var choices = window.SelectorBar("Sort order")
    .SetItems([new(1, "Name"), new(2, "Date")], 1);
choices.Changed += id => status.Text = id == 1 ? "Name order" : "Date order";
root.Add(choices).Add(status);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let status = window.label("Name order")?;
let choices = window.selector_bar("Sort order")?;
choices.set_items(&[
    Choice { id: 1, text: "Name".into(), enabled: true, version: 0 },
    Choice { id: 2, text: "Date".into(), enabled: true, version: 0 },
], Some(1))?;
let output = status.downgrade();
choices.on_change(move |id| {
    if let Some(output) = output.upgrade() {
        output.set_text(if id == 1 { "Name order" } else { "Date order" })?;
    }
    Ok(())
})?;
root.add(&choices, 0.0)?;
root.add(&status, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto status = std::make_shared<xui::Label>(L"Name order");
auto choices = std::make_shared<xui::SelectorBar>(L"Sort order");
choices->set_items({{1, L"Name"}, {2, L"Date"}}, 1);
choices->on_change([status](std::uint64_t id) {
    status->set_text(id == 1 ? L"Name order" : L"Date order");
});
root->add(choices);
root->add(status);
```

{% endtab %}
{% endtabs %}

`set_items` replaces the items and selected ID as one snapshot.
`set_selected` changes the property silently. `select` performs a semantic selection and reports a change.
The selected ID must identify an enabled item.
IDs must be nonzero, unique, and at most `INTPTR_MAX - 100`. The limit is 4,096 items.

Without an explicit selected ID, a new snapshot preserves a surviving enabled selection.
Otherwise, it selects the first enabled item.
An empty or all-disabled snapshot has no selection.
The selection getter can return absence, but `set_selected` requires an ID.
There is no clear-selection setter. An empty snapshot removes the selection.

Selecting the current ID emits no change event.
The bindings return an error for semantic selection on a disabled control.

The style target is `choice_list`, not a new SelectorBar target.
It supplies item surfaces, text, and a selection marker in horizontal geometry.

UIA exposes a List with SelectionItem children.
The items do not expose a Toggle pattern.
The bar has one Tab stop.
Narrow widths reveal the selected overflow choice.
Horizontal arrows, Home, End, and prefix input select enabled choices.

The `.xui` compiler applies `items` and `selected` together.
A tuple state keeps joint reactive replacements in one update.
Two separate state assignments refresh immediately and can expose an invalid intermediate selection.
The [language contract](../xui-language.md#checkbox-links-selectors-badges-and-menu-bars) describes these arguments.

## ChoiceList

ChoiceList is the list presentation of `RadioGroup`.
It is not a separate public C++ class.
Use this presentation for a short popup choice list without radio circles.

{% tabs %}
{% tab title=".xui" %}

A standalone `ChoiceList` is unavailable in markup and C#.
This component accepts a C# `ComboBox` whose popup owns the choice list.
The ComboBox must belong to the same window and have no parent.

```text
namespace ControlExamples;
component ViewChoiceField {
    param global::Xui.ComboBox Field;
    view {
        VStack() {
            Content(Field);
        }
    }
}
```

C# constructs the owner and accesses its retained choices without a new parent:

```csharp
var field = window.ComboBox("View choices");
field.SetItems([new(1, "List"), new(2, "Tiles")], 1);
var choices = field.Choices;
choices.Help("Choose the list or tile presentation.");
var view = new ControlExamples.ViewChoiceField(window, field);
```

{% endtab %}
{% tab title="C#" %}

A standalone constructor is unavailable.
This example accesses the choice list inside a ComboBox instead.

```csharp
var field = window.ComboBox("View choices");
field.SetItems([new(1, "List"), new(2, "Tiles")], 1);
var choices = field.Choices;
choices.Help("Choose the list or tile presentation.");
root.Add(field);
```

{% endtab %}
{% tab title="Rust" %}

A standalone constructor is unavailable.
This example accesses the choice list inside a ComboBox instead.

```rust
let field = window.combo_box("View choices", false)?;
field.set_items(&[
    Choice { id: 1, text: "List".into(), enabled: true, version: 0 },
    Choice { id: 2, text: "Tiles".into(), enabled: true, version: 0 },
], Some(1))?;
let choices = field.choices()?;
choices.help("Choose the list or tile presentation.")?;
root.add(&field, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto choices = std::make_shared<xui::RadioGroup>(L"View choices", true);
choices->set_items({{1, L"List"}, {2, L"Tiles"}}, 1);
root->add(choices);
```

{% endtab %}
{% endtabs %}

The second constructor argument selects the presentation.
The API still uses `set_items`, `set_selected`, `on_change`, and `on_accept`.
Its style target is `choice_list`, not `radio_group`.

C# and Rust expose ComboBox choices as borrowed children.
They do not expose an independent ChoiceList constructor.
Use the [retained-child contract](../bindings.md#retained-composition-children) for that path.

## ComboBox

Use `ComboBox` for a committed choice in a compact field.
The optional editor accepts text without inventing a selected identity.

{% tabs %}
{% tab title=".xui" %}

`ComboBox` has no markup constructor.
This component requires C# controls from the same window, without existing parents.
The retained editor reports edits because the binding has no ComboBox edit callback.

```text
namespace ControlExamples;
component ExportFormat {
    param global::Xui.ComboBox Format;
    param global::Xui.Label Status;
    view {
        VStack() {
            Content(Format);
            Content(Status);
        }
    }
}
```

C# constructs the editable field and connects its callbacks:

```csharp
var format = window.ComboBox("Export format", true);
format.SetItems([new(1, "Text"), new(2, "Markdown")], 1);
var status = window.Label("Text format");
format.Event += e => {
    if (e.Kind == EventKind.Selection)
        status.Text = e.Value == 1 ? "Text format" : "Markdown format";
};
var editor = format.Editor ?? throw new InvalidOperationException("An editable ComboBox needs an editor.");
editor.Changed += text => status.Text = "Uncommitted text: " + text;
var view = new ControlExamples.ExportFormat(window, format, status);
```

{% endtab %}
{% tab title="C#" %}

The retained editor reports edits because the binding has no ComboBox edit callback.

```csharp
var format = window.ComboBox("Export format", true);
format.SetItems([new(1, "Text"), new(2, "Markdown")], 1);
var status = window.Label("Text format");
format.Event += e => {
    if (e.Kind == EventKind.Selection)
        status.Text = e.Value == 1 ? "Text format" : "Markdown format";
};
var editor = format.Editor ?? throw new InvalidOperationException("An editable ComboBox needs an editor.");
editor.Changed += text => status.Text = "Uncommitted text: " + text;
root.Add(format).Add(status);
```

{% endtab %}
{% tab title="Rust" %}

The retained editor reports edits because the binding has no ComboBox edit callback.

```rust
let format = window.combo_box("Export format", true)?;
format.set_items(&[
    Choice { id: 1, text: "Text".into(), enabled: true, version: 0 },
    Choice { id: 2, text: "Markdown".into(), enabled: true, version: 0 },
], Some(1))?;
let status = window.label("Text format")?;
let output = status.downgrade();
format.on_event(move |event| {
    let Some(output) = output.upgrade() else { return Ok(()); };
    if event.kind == 5 {
        output.set_text(if event.value == 1 { "Text format" } else { "Markdown format" })
            .map_err(|error| {
                eprintln!("Format callback: {error}");
                error
            })?;
    }
    Ok(())
})?;
let editor = format.editor()?.ok_or_else(|| xui::Error {
    status: 1,
    message: "An editable ComboBox needs an editor.".into(),
})?;
let input = editor.downgrade();
let output = status.downgrade();
editor.on_event(move |event| {
    let (Some(input), Some(output)) = (input.upgrade(), output.upgrade()) else { return Ok(()); };
    let update = || -> xui::Result<()> {
        if event.kind == 2 {
            output.set_text(&format!("Uncommitted text: {}", input.text()?))?;
        }
        Ok(())
    };
    update().map_err(|error| {
        eprintln!("Format edit callback: {error}");
        error
    })
})?;
root.add(&format, 0.0)?;
root.add(&status, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

`NumericInput` has no markup constructor.
This component requires C# controls from the same window, without existing parents.
The binding has no spin-placement setter, so this example uses the native default.

```text
namespace ControlExamples;
component CopyCount {
    param global::Xui.NumericInput Count;
    param global::Xui.Label Status;
    view {
        VStack() {
            Content(Count);
            Content(Status);
        }
    }
}
```

C# constructs the field and connects its callback:

```csharp
var count = window.NumericInput("Copies").SetRange(new(1, 100, 1, 10)).SetValue(2);
var status = window.Label("Copies: 2");
count.OnChange(value => status.Text = $"Copies: {value}");
var view = new ControlExamples.CopyCount(window, count, status);
```

{% endtab %}
{% tab title="C#" %}

The binding has no spin-placement setter, so this example uses the native default.

```csharp
var count = window.NumericInput("Copies").SetRange(new(1, 100, 1, 10)).SetValue(2);
var status = window.Label("Copies: 2");
count.OnChange(value => status.Text = $"Copies: {value}");
root.Add(count).Add(status);
```

{% endtab %}
{% tab title="Rust" %}

The binding has no spin-placement setter, so this example uses the native default.

```rust
let count = window.numeric_input("Copies")?;
count.set_range(NumericRange { minimum: 1.0, maximum: 100.0, small_step: 1.0, large_step: 10.0 })?;
count.set_value(2.0)?;
let status = window.label("Copies: 2")?;
let output = status.downgrade();
count.on_change(move |value| {
    let Some(output) = output.upgrade() else { return Ok(()); };
    output.set_text(&format!("Copies: {value}")).map_err(|error| {
        eprintln!("Copies callback: {error}");
        error
    })
})?;
root.add(&count, 0.0)?;
root.add(&status, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

`RangeInput` has no markup constructor.
This component requires C# controls from the same window, without existing parents.

```text
namespace ControlExamples;
component VolumeControl {
    param global::Xui.RangeInput Volume;
    param global::Xui.Label Value;
    view {
        VStack() {
            Content(Volume);
            Content(Value);
        }
    }
}
```

C# connects preview, change, and cancel events.
Each numeric event stores the double value as IEEE 754 bits.

```csharp
var value = window.Label("Volume: 25");
var volume = window.RangeInput("Volume").SetRange(new(0, 100, 1, 10)).SetValue(25);
volume.Event += e => {
    if (e.Kind is EventKind.Preview or EventKind.Change or EventKind.Cancel) {
        var number = BitConverter.UInt64BitsToDouble(e.Value);
        value.Text = $"{(e.Kind == EventKind.Preview ? "Preview" : "Volume")}: {number}";
    }
};
var view = new ControlExamples.VolumeControl(window, volume, value);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var value = window.Label("Volume: 25");
var volume = window.RangeInput("Volume").SetRange(new(0, 100, 1, 10)).SetValue(25);
volume.Event += e => {
    if (e.Kind is EventKind.Preview or EventKind.Change or EventKind.Cancel) {
        var number = BitConverter.UInt64BitsToDouble(e.Value);
        value.Text = $"{(e.Kind == EventKind.Preview ? "Preview" : "Volume")}: {number}";
    }
};
root.Add(volume).Add(value);
```

{% endtab %}
{% tab title="Rust" %}

Event kinds 7, 2, and 8 report preview, change, and cancel.
Their payload stores the double value as IEEE 754 bits.

```rust
let value = window.label("Volume: 25")?;
let volume = window.range_input("Volume")?;
volume.set_range(NumericRange { minimum: 0.0, maximum: 100.0, small_step: 1.0, large_step: 10.0 })?;
volume.set_value(25.0)?;
let output = value.downgrade();
volume.on_event(move |event| {
    let Some(output) = output.upgrade() else { return Ok(()); };
    if matches!(event.kind, 7 | 2 | 8) {
        let label = if event.kind == 7 { "Preview" } else { "Volume" };
        output.set_text(&format!("{label}: {}", f64::from_bits(event.value)))
            .map_err(|error| {
                eprintln!("Volume callback: {error}");
                error
            })?;
    }
    Ok(())
})?;
root.add(&volume, 0.0)?;
root.add(&value, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

The capacity recipe uses a generated reference and a C# setter.

```text
namespace ControlExamples;
component ImportProgress {
    view {
        VStack() {
            Progress("Import progress", currentValue: 40,
                progressState: global::Xui.ProgressState.Determinate);
            Progress("Storage capacity", ref: Capacity);
        }
    }
}
```

```csharp
var view = new ControlExamples.ImportProgress(window);
view.Capacity.SetCapacity(40, 100, "GB");
```

{% endtab %}
{% tab title="C#" %}

```csharp
var task = window.Progress("Import progress").SetRange(new(0, 100))
    .SetValue(40).SetState(ProgressState.Determinate);
var capacity = window.Progress("Storage capacity").SetCapacity(40, 100, "GB");
root.Add(task).Add(capacity);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let task = window.progress("Import progress")?;
task.set_range(NumericRange { minimum: 0.0, maximum: 100.0, small_step: 1.0, large_step: 10.0 })?;
task.set_value(40.0)?;
task.set_state(ProgressState::Determinate)?;
let capacity = window.progress("Storage capacity")?;
capacity.set_capacity(40.0, 100.0, "GB")?;
root.add(&task, 0.0)?;
root.add(&capacity, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

`paused`, `error`, `unknown`, and `indeterminate` describe other states.
Indeterminate progress animates while attached, visible, and effectively enabled.
The window-owned timer stops without eligible controls, including hidden or minimized windows.
The system client-area animation preference suppresses motion without changing the state.
Unknown, paused, error, and determinate states remain static.
Capacity text describes used and total values without an active-task claim.
`set_capacity` selects determinate state and requires finite values with `0 <= used <= total` and a positive total.

The style target is `progress`.
Caption, track, and fill styles do not alter the value.
There is no action callback or editable UIA value.
The [foundation contract](../foundation-controls.md#progress-presentations-and-animation) defines animation ownership and lifecycle.

## ProgressRing

Use `ProgressRing` for task status in a circular presentation.
The native class derives from Progress and defaults to indeterminate state.
An ordinary Progress still defaults to a determinate bar.
The native preferred size is 48 by 48 DIPs.

{% tabs %}
{% tab title=".xui" %}

```text
namespace ControlExamples;
component ImportRing {
    view {
        VStack() {
            ProgressRing("Import activity");
            ProgressRing("Import completion", currentValue: 40,
                progressState: global::Xui.ProgressState.Determinate);
        }
    }
}
```

{% endtab %}
{% tab title="C#" %}

```csharp
var activity = window.ProgressRing("Import activity");
var completion = window.ProgressRing("Import completion")
    .SetRange(new(0, 100)).SetValue(40).SetState(ProgressState.Determinate);
root.Add(activity).Add(completion);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let activity = window.progress_ring("Import activity")?;
let completion = window.progress_ring("Import completion")?;
completion.set_range(NumericRange {
    minimum: 0.0, maximum: 100.0, small_step: 1.0, large_step: 10.0
})?;
completion.set_value(40.0)?;
completion.set_state(ProgressState::Determinate)?;
root.add(&activity, 0.0)?;
root.add(&completion, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto activity = std::make_shared<xui::ProgressRing>(L"Import activity");
auto completion = std::make_shared<xui::ProgressRing>(L"Import completion");
completion->set_range(0, 100);
completion->set_value(40);
completion->set_state(xui::ProgressState::determinate);
root->add(activity);
root->add(completion);
```

{% endtab %}
{% endtabs %}

Range, value, paused/error/unknown states, and read-only accessibility follow Progress.
Indeterminate and unknown states omit the UIA RangeValue pattern.
The ring does not add a keyboard focus stop.
The ring uses the same lifecycle-bound indeterminate animation and reduced-animation rules as the bar.
The style target remains `progress`.
Track and fill parts describe a circular track and arc rather than a linear track and segment.
The ring retains its accessible name and value but has no visible caption.
A separate Label can show task or capacity text.
Styles do not change the progress state or request animation.

## Expander

Use `Expander` for optional details in the existing page.
Use Popup for temporary anchored content.

{% tabs %}
{% tab title=".xui" %}

`Expander` has no markup constructor.
This component requires a C# expander from the same window, without an existing parent.

```text
namespace ControlExamples;
component AdvancedDetails {
    param global::Xui.Expander Details;
    view {
        VStack() {
            Content(Details);
        }
    }
}
```

C# constructs the retained content and expander:

```csharp
var details = window.Stack().Add(window.TextInput("Advanced path"));
var expander = window.Expander("Advanced", details).SetExpanded(false);
var view = new ControlExamples.AdvancedDetails(window, expander);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var details = window.Stack().Add(window.TextInput("Advanced path"));
var expander = window.Expander("Advanced", details).SetExpanded(false);
root.Add(expander);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let details = window.stack(Axis::Vertical)?;
details.add(&*window.text_input("Advanced path")?, 0.0)?;
let expander = window.expander("Advanced", &details)?;
expander.set_expanded(false)?;
root.add(&expander, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto details = std::make_shared<xui::Stack>(xui::Axis::vertical);
details->add(std::make_shared<xui::TextInput>(L"Advanced path"));
auto expander = std::make_shared<xui::Expander>(L"Advanced", details);
expander->set_expanded(false);
root->add(expander);
```

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

`SplitButton` has no markup constructor.
This component requires C# elements from the same window, without existing parents.

```text
namespace ControlExamples;
component SaveActions {
    param global::Xui.SplitButton Split;
    param global::Xui.Label Status;
    view {
        VStack() {
            Content(Split);
            Content(Status);
        }
    }
}
```

C# constructs the composition and connects both retained buttons:

```csharp
var status = window.Label("Ready");
var split = window.SplitButton("Save");
split.Secondary.SetText("Save options");
split.Primary.Click += () => status.Text = "Saved";
split.Secondary.Click += () => status.Text = "Choose a save destination";
var view = new ControlExamples.SaveActions(window, split, status);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var status = window.Label("Ready");
var split = window.SplitButton("Save");
split.Secondary.SetText("Save options");
split.Primary.Click += () => status.Text = "Saved";
split.Secondary.Click += () => status.Text = "Choose a save destination";
root.Add(split).Add(status);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let status = window.label("Ready")?;
let split = window.split_button("Save")?;
let primary = split.primary()?;
let secondary = split.secondary()?;
secondary.set_text("Save options")?;
let output = status.downgrade();
primary.on_event(move |event| {
    let Some(output) = output.upgrade() else { return Ok(()); };
    if event.kind == 1 {
        output.set_text("Saved").map_err(|error| {
            eprintln!("Save callback: {error}");
            error
        })?;
    }
    Ok(())
})?;
let output = status.downgrade();
secondary.on_event(move |event| {
    let Some(output) = output.upgrade() else { return Ok(()); };
    if event.kind == 1 {
        output.set_text("Choose a save destination").map_err(|error| {
            eprintln!("Save options callback: {error}");
            error
        })?;
    }
    Ok(())
})?;
root.add(&split, 0.0)?;
root.add(&status, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

The secondary callback can open a Popup or CommandSurface.
The root style target is `split_button`.
The root frame does not accept foreground.
`primary()` and `secondary()` accept Button styles and retain their separate actions.

## Popup

Use `Popup` for retained content anchored to an active control.
Use `ContentDialog` for modal validation and primary/cancel results.

{% tabs %}
{% tab title=".xui" %}

`Popup` has a markup constructor, but `RangeInput` does not.
This component requires a C# range input from the same window, without an existing parent.
Its root is a popup, not page content.

```text
namespace ControlExamples;
component PreviewOptions {
    param global::Xui.RangeInput Size;
    view {
        Popup("Preview options", placement: global::Xui.PopupPlacement.Below,
            preferredSize: (280, 100)) {
            Content(Size);
        }
    }
}
```

C# constructs the range input and a separate page with an anchor button.
Unlike Stack roots, this popup root does not attach as page content.
The binding has no explicit initial-focus argument.

```csharp
var size = window.RangeInput("Preview size").SetRange(new(32, 256, 8, 32)).SetValue(96);
var view = new ControlExamples.PreviewOptions(window, size, attach: false);
var page = window.Stack();
var popupAnchor = window.Button("Preview options");
popupAnchor.Click += () => view.Root.Show(popupAnchor);
page.Add(popupAnchor);
window.SetContent(page);
```

{% endtab %}
{% tab title="C#" %}

The binding has no explicit initial-focus argument.

```csharp
var size = window.RangeInput("Preview size").SetRange(new(32, 256, 8, 32)).SetValue(96);
var popup = window.Popup("Preview options", size)
    .SetPlacement(PopupPlacement.Below).PreferredSize(280, 100);
anchor.Click += () => popup.Show(anchor);
```

{% endtab %}
{% tab title="Rust" %}

Rust has no popup-placement setter or explicit initial-focus argument.
This example uses the native placement default.

```rust
let size = window.range_input("Preview size")?;
size.set_range(NumericRange { minimum: 32.0, maximum: 256.0, small_step: 8.0, large_step: 32.0 })?;
size.set_value(96.0)?;
let popup = window.popup("Preview options", &size)?;
window.update(&[Property {
    a: 280.0, b: 100.0, ..Property::new(&popup, PropertyKind::PreferredSize)
}])?;
let popup_anchor = anchor.downgrade();
let popup = popup.weak();
anchor.on_event(move |event| {
    let (Some(popup), Some(popup_anchor)) = (popup.upgrade(), popup_anchor.upgrade()) else { return Ok(()); };
    if event.kind == 1 {
        popup.show(&popup_anchor).map_err(|error| {
            eprintln!("Popup callback: {error}");
            error
        })?;
    }
    Ok(())
})?;
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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
