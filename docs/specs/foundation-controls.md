# Foundation controls

Build and test commands are in [CONTRIBUTING](../../CONTRIBUTING.md).
See the [reference index](README.md) for related APIs.
Examples in this reference use C++ unless stated otherwise.
For C# and Rust coverage, use the [binding reference](bindings.md).

## Foundation controls

Include `xui/foundation.hpp` for the foundation controls.
Property setters do not call application callbacks. Input actions do.
Invalid identities, nonfinite numbers, invalid bounds, and invalid timing values throw `std::invalid_argument`.

| API | Contract |
| --- | --- |
| `RadioGroup` | Stable `ChoiceItem` IDs, one enabled selection, arrow navigation, prefix selection, and virtual UIA SelectionItem children |
| `ComboBox` | Committed ID separate from popup preview; Enter commits, Escape cancels, and optional editing uses a native EDIT |
| `Popup` | Arbitrary retained content, anchored placement, nested dismissal, initial focus, focus return, and generation checks |
| `Control::set_help_text` | Delayed hover/focus help and UIA HelpText; the tooltip never receives focus |
| `Button::set_behavior` | Momentary, repeat, toggle-action, or dropdown behavior; toggle actions expose UIA Toggle |
| `ToggleButton` | A Button with toggle behavior by default, silent checked-state setters, and an `on_toggle` callback |
| `ToggleSwitch` | A Toggle with a switch pill and thumb, the same checked-state model, and inherited checkbox accessibility |
| `CheckBox` | Unchecked, checked, and indeterminate values with an optional three-state input cycle |
| `HyperlinkButton` | A link-shaped Button action with no implicit URI or browser navigation |
| `SelectorBar` | Horizontal exclusive selection with stable ChoiceItem IDs |
| `InfoBadge` | A noninteractive dot, count, or icon with an accessible name |
| `SplitButton` | Independent primary and dropdown Buttons with separate names, callbacks, and keyboard targets |
| `NumericInput` | Native text entry, locale parsing, finite bounds, step actions, visible invalid text, UIA Value and RangeValue |
| `RangeInput` | Horizontal/vertical input, explicit reversal, pointer capture, small/page steps, preview/change/cancel callbacks, UIA RangeValue |
| `Expander` | Expand/collapse semantics, hidden-child inactivity, and focus repair to the header |
| `Progress`, `ProgressRing` | Read-only values, animated indeterminate state, static unknown/paused/error states, and a capacity-meter recipe |

Choice controls accept up to 4,096 items. They use one peer and virtual accessible children, not one peer per choice.
Use `ItemsView` for larger sources.
An editable ComboBox does not infer an ID from arbitrary text. Handle `on_edit` separately from `on_change`.
`NumericInput::set_locale` selects parsing and formatting rules. Invalid text stays visible and leaves the last valid value unchanged.
`NumericInput::step` restores valid formatted text. Native EDIT retains selection, undo, caret, and IME ownership.
`RangeInput::on_preview` reports drag values without committing them. `on_change` reports accepted values.
Input cancellation calls `on_cancel` after it clears the preview. Property-driven cancellation remains silent.
In WinUI style, the full-length track and thumb travel have separate bounds.
`SliderVisual::pointer_fraction` accounts for that difference, including root padding, short controls, and authored thumb sizes.
The [WinUI presentation contract](winui-style.md) defines the default thumb layout and painted outset.

```cpp
auto size = std::make_shared<xui::RangeInput>(L"Item size");
size->set_range({16, 256, 8, 32});
size->set_value(64);
size->set_orientation(xui::Axis::vertical);
auto popup = std::make_shared<xui::Popup>(size, L"View options");
popup->set_preferred_size({120, 240});
// window and anchor must remain valid when this action runs.
anchor.on_click([&window, &anchor, popup] { window.show_popup(popup, anchor); });
```

`Window::show_popup(popup, anchor, initial_focus)` requires an active retained anchor in that window.
The default focus is the first eligible content control.
Nested popups must anchor in the top popup; the maximum depth is eight.
Tab traversal stays in the top popup. Escape dismisses it.
Outside input, focus departure, hide, disable, or window closure cancels affected popups.
`Window::dismiss_popup` accepts an explicit dismissal reason.
Before callbacks run, closed popups have stale generations. Use `Popup::current(saved_generation)` to reject late provider results.
Applications must also cancel their own queries and avoid strong callback ownership cycles.

Popups stay inside the intersection of the client area and monitor work area. They cannot extend outside the application window.
They share the root Direct2D target. Open content creates normal child input/UIA peers, including native EDIT/caption peers where needed.
Dismissed peers are released after input dispatch. Retaining the public Popup does not retain its closed native peers.
Native text is composed before `EndDraw`; popup clipping also masks underlying native fields.

Popup padding has an opaque background in both Classic and WinUI.
`Popup::set_window_background(true)` selects the window background instead of the default surface color.
WinUI popups retain rounded corners and shadows outside this background.
High-contrast popups use system colors without shadows.
Content controls retain their own background rules.
For example, Classic `ItemsView` uses the window background, while WinUI `ItemsView` uses the popup background unless an inner Stack selects a surface.

Tooltips use one pending one-shot timer and no extra HWND or target. Hidden tooltips have no timer.

## Toggle presentations

`Toggle` remains a checkbox.
`ToggleSwitch` derives from Toggle and replaces the checkbox mark with a switch pill and thumb.
It retains `checked()`, silent `set_checked`, and `on_change`.
It inherits the Toggle accessibility role, Space-key behavior, and `toggle` style target.
The switch presentation does not introduce a separate accessibility role or style target.
For custom renderers, `mark_bounds(bounds, enabled)` uses the effective enabled state to suppress WinUI thumb growth under a disabled ancestor.

`ToggleButton` derives from Button and selects `ButtonBehavior::toggle` by default.
It retains `checked()`, silent `set_checked`, and `on_toggle`.
Toggle actions call `on_toggle`, not `on_click`.
It uses the `button` style target and the normal Button activation and cancellation rules.
The [basic control guide](controls/basic.md#togglebutton) contains examples for both presentations.

## CheckBox, SelectorBar, and InfoBadge

`CheckBox` defaults to unchecked state with three-state input off.
`CheckState` contains `unchecked`, `checked`, and `indeterminate`.
`set_state` is silent and accepts all three states.
`set_three_state` changes the input cycle without restricting programmatic state.
Three-state activation cycles through unchecked, checked, indeterminate, then unchecked.
With three-state input off, activation from indeterminate selects unchecked.

`CheckBox::on_change` receives a CheckState.
Legacy Toggle and ToggleSwitch remain binary controls with boolean callbacks.
The [basic control guide](controls/basic.md#checkbox) separates these contracts.

`SelectorBar` supplies horizontal exclusive selection through the ChoiceItem model.
`set_items` replaces the items and selection together.
`set_selected` is silent. `select` reports a semantic selection through `on_change`.
The [selector guide](controls/choices.md#selectorbar) includes reactive snapshot examples.

`InfoBadge` defaults to a dot.
`set_count`, `set_icon`, and `set_dot` select the presentation.
The count stores an unsigned 32-bit value. The visible count uses `99+` for values greater than 99.

The accessible name describes the badge purpose.
For a count badge, the accessible name also includes the full count.
The badge has no input action or keyboard focus stop.

`HyperlinkButton` calls an application action through `on_click`.
Its appearance does not imply automatic URI handling or browser navigation.
The [hyperlink guide](controls/basic.md#hyperlinkbutton) uses a local status callback.

## Progress presentations and animation

`Progress` defaults to a determinate bar.
`ProgressRing` derives from Progress and defaults to an indeterminate ring.
Both share the range, value, state, and read-only accessibility contracts.
Both use the `progress` style target.
The shape does not change the meaning of the value.
ProgressRing has no visible caption. A separate Label can show task or capacity text.
ProgressRing defaults to a 48-by-48-DIP preferred size.
Indeterminate and unknown progress states omit the UIA RangeValue pattern.
Other progress states expose read-only values.

Indeterminate progress animates only while attached, visible, and effectively enabled in a visible, nonminimized window.
A window-owned timer drives eligible progress controls.
The timer stops when no eligible control remains.
Hide, detach, ancestor disable, window minimization, and window destruction stop affected animation.
The system client-area animation preference disables motion without changing the progress state.
The indicator remains visible without motion.
Unknown, paused, error, determinate, and capacity displays do not request animation.
Capacity meters show used/total text without an active-task claim.
The [progress guide](controls/choices.md#progress) contains examples.
