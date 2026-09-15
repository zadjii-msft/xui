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
| `SplitButton` | Independent primary and dropdown Buttons with separate names, callbacks, and keyboard targets |
| `NumericInput` | Native text entry, locale parsing, finite bounds, step actions, visible invalid text, UIA Value and RangeValue |
| `RangeInput` | Horizontal/vertical input, explicit reversal, pointer capture, small/page steps, preview/change/cancel callbacks, UIA RangeValue |
| `Expander` | Expand/collapse semantics, hidden-child inactivity, and focus repair to the header |
| `Progress` | Read-only determinate values, static indeterminate/unknown states, paused/error states, and a capacity-meter recipe |

Choice controls accept up to 4,096 items. They use one peer and virtual accessible children, not one peer per choice.
Use `ItemsView` for larger sources.
An editable ComboBox does not infer an ID from arbitrary text. Handle `on_edit` separately from `on_change`.
`NumericInput::set_locale` selects parsing and formatting rules. Invalid text stays visible and leaves the last valid value unchanged.
`NumericInput::step` restores valid formatted text. Native EDIT retains selection, undo, caret, and IME ownership.
`RangeInput::on_preview` reports drag values without committing them. `on_change` reports accepted values.
Input cancellation calls `on_cancel` after it clears the preview. Property-driven cancellation remains silent.

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
Tooltips use one pending one-shot timer and no extra HWND or target. Hidden tooltips have no timer.
Indeterminate progress is deliberately static. This batch has no progress ring or progress animation.
Capacity meters show used/total text without implying an active task.
