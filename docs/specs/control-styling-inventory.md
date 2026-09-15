# Control styling inventory

This inventory defines the styling target for the public XUI control catalog.
It distinguishes owned presentation from native or application-authored content.
The [Button contract](control-styling.md) describes the implemented foundation.
The [template design](styling-and-templates-design.md) describes separate, later structural customization.
This expansion does not require control templates or item templates.

## Shared vocabulary

A **surface** has background, foreground, border color, per-edge border thickness, corner radius, and padding.
A **text part** has foreground, typography, and alignment.
Typography includes font family, size, weight, and style where the text backend supports them.
A **metric** changes both layout and hit testing, not only painting.
Examples include indicator size, row height, track thickness, and spacing.

Styles apply to a control and its named visual parts.
Parts are not new controls, native windows, or accessibility providers.
Part names describe stable public presentation roles, not private child indexes.
The implementation contract defines the exact property identifiers and supported combinations.
An unavailable property or state must produce an explicit diagnostic.

The common interactive states are normal, disabled, hovered, pressed, and focused.
Each control exposes only states that its model or native peer actually supplies.
Selected, checked, expanded, invalid, loading, and severity states remain distinct.
An application cannot change a model value by setting a visual state rule.
Hidden and closed content does not acquire a visible rendering merely through a style.

High contrast retains system-color and visible-focus requirements.
Theme-dependent resources supply light and dark colors.
State rules resolve deterministically, with disabled presentation taking priority over pointer interaction.
Local values remain distinct from style values.

## Basic controls and layouts

| Control or composition | Styling properties and parts | Additional states | Boundary |
| --- | --- | --- | --- |
| `Button`, icon, repeat, toggle, and dropdown variants | Surface, text, icon, dropdown indicator, content alignment | checked | The existing Button API and grammar remain compatible. |
| `Label` / `.xui Text` | Surface, text, caption, heading, wrapping and line limits | disabled | Text tone remains a semantic default, not a hard-coded override of authored color. |
| `Toggle` | Surface, label text, indicator surface and size, check mark | checked | One control retains activation and Toggle accessibility. |
| `Stack`, `Grid`, `Wrap`, `AdaptiveLayout`, `PageView` | Surface, separator, padding, spacing, layout gaps | compact, expanded navigation where applicable | Existing track, flex, and adaptive layout APIs remain authoritative. |
| `ContentView` | Surface, border, padding, content alignment | normal, disabled where applicable | Content ownership and retained child identity do not change. |
| `ScrollView` | Viewport surface, scrollbar track and thumb, scrollbar width, corner radius | dragging, scrollable | Scroll geometry and accessible bounds use the same metrics as painting. |
| `SplitView` | Pane surfaces, divider, grip, divider width | dragging | Divider hit testing and keyboard resizing retain their contracts. |
| `Expander` | Header surface and text, disclosure indicator, content surface, header padding and height | expanded | Collapsed children remain hidden without replacement or loss of state. |
| `SplitButton` | Primary and dropdown Button styles, separator, spacing | checked or open where supplied by its children | The composition reuses Button behavior and style definitions. |

## Choices, fields, and progress

| Control | Styling properties and parts | Additional states | Boundary |
| --- | --- | --- | --- |
| `RadioGroup` | Root surface, item surface and text, radio indicator, selected dot, row spacing and height | selected, item disabled | Stable choice IDs remain the source for input and accessibility. |
| `ChoiceList` presentation | Root and item surfaces, item text, selected marker, row padding and height | selected, item disabled | There is no retained control or style allocation for every source item. |
| `ComboBox`, editable and noneditable | Field surface, selected text, arrow, header, popup and choice styles | open, empty | Editable text remains native. Popup geometry retains selected-row alignment. |
| `TextInput`, plain and search variants | Header text, field surface, native text, placeholder, search icon, clear action, shortcut badge | empty, focused native editor | Supported native colors and font settings must match the surrounding frame. |
| `NumericInput` | Shared field surface, header, native text, decrement and increment Button parts | invalid, minimum, maximum | Validity and range behavior remain model-owned. |
| `RangeInput` | Root surface, track, filled track, thumb surface, thumb size, track thickness | dragging, minimum, maximum | Horizontal, vertical, and reversed geometry use one shared layout calculation. |
| `Progress` | Root surface, caption, track, filled segment, thickness | determinate, indeterminate, paused, error, unknown | Styling does not add an animation timer to a static progress control. |
| `MultilineText`, `RichText` | Owned frame and header, supported native background, text defaults, font | read-only, empty | Rich runs, selection, IME, undo, and native scrollbars retain their ownership. |
| `PasswordInput` | Owned frame, header, supported native text and background, reveal action | revealed, empty | Style diagnostics never expose password content. |
| `DateTimePicker`, date, time, and calendar | Owned frame, header, supported platform colors and typography | focused, disabled | Native calendar internals remain platform-owned unless a supported native API exposes the property. |
| `InlineStatus` | Surface, accent stripe, severity icon, message text, action and dismiss Buttons | information, success, warning, error, dismissed | Severity remains available through semantics and text, not only color. |
| `ColorPicker` | Frame, checkerboard colors, preview border, swatch border, channel labels and numeric fields | selected swatch, invalid channel | The selected color and authored swatch colors are data, not theme substitutions. |

## Virtual collections and navigation

| Control | Styling properties and parts | Additional states | Boundary |
| --- | --- | --- | --- |
| `FileList` | Root and row surfaces, primary and secondary text, generic icon tint, focus marker, scrollbar | selected, item focused, item hovered, item disabled where supplied | Shell icons and thumbnail pixels retain their content colors. |
| `ItemsView`, list, tiles, and grouped | Root, row or tile, primary and secondary text, icon, inline action, inline progress, group header, scrollbar | selected, item focused, item hovered, item disabled, checked, group expanded | Item dimensions remain consistent across drawing, hit testing, scrolling, and UIA. |
| `TreeView` | Collection parts, disclosure indicator, indentation, pending and error text | expanded, loading, error, selected | Collapse, asynchronous cancellation, and stable item keys remain unchanged. |
| `DataGrid` | Root, header, row, alternating row, cell text, grid line, sort and filter icons, check indicator, scrollbar, reorder marker | selected, row hovered, header focused, sorted, descending, filtered, checked, mixed, filter pending, dragging | Column identities and virtual cell providers must not depend on visual instances. |
| `HistoryChart` | Surface, title, caption, grid lines, plot line, line thickness | empty | Missing samples remain gaps. Styling does not rewrite series values. |
| `NavigationList` | Collection parts, selection marker, icon, badge, section header, disclosure indicator | selected, selected descendant, expanded, compact | Disabled ancestors and filtered items retain current navigation semantics. |
| `NavigationView` | Pane surface, title, toggle Button, search field, header/main/footer collection styles | expanded, compact, empty | One retained navigation subtree survives style and layout changes. |
| `Breadcrumb` | Surface, segment Buttons and text, separators, overflow Button | current, overflowed | Navigation and overflow use the existing stable segment identities. |
| `NavigationPane` | Surface, group Expander, item collection, progress, status text | loading, error, empty | Style application does not cancel or replace a query. |
| `LocationPicker`, `ViewPicker` | Popup, editor, navigation, toolbar, choices, and size control styles | open, loading or selected through owned children | Helpers forward styles to their documented components. |
| `TabStrip` | Surface, tab surface, label, icon, close action, add action, separators and selection marker | selected, tab hovered, close hovered, dragging | Authored tab data and action identities remain unchanged. |

## Commands, windows, and hosted content

| Control or composition | Styling properties and parts | Additional states | Boundary |
| --- | --- | --- | --- |
| `CommandMenu` | Root, item, section header, separator, icon, check mark, shortcut, submenu arrow, scrollbar | item disabled, selected, checked, submenu open | Command snapshots and menu input semantics remain intact. |
| `CommandBar`, `CommandSurface` | Surface, command Buttons, separators, overflow Button and menu | overflowed, checked through commands | Layout must retain existing command identity and overflow rules. |
| XUI command palette and custom Shell menu | Popup, search field, command items, shortcuts, empty and error text | open, loading, error, empty | Platform-owned Shell menus are not recolored through this API. |
| `Popup` | Surface, border, padding, corner radius | open | Placement, clipping, dismissal, and focus restoration remain window-owned. |
| `ContentDialog` | Popup surface, title, body, footer, separator, validation, primary and cancel Buttons | invalid, open | Modal input, native editors, and validation callbacks remain intact. |
| `TitleBar` | Surface, caption text, icon, caption Buttons and hover fills | active, inactive, maximized | Hit-test regions, system actions, and drag behavior use the existing native contract. |
| Tooltips | Surface, text, border, radius, padding | visible | Styling does not change delays or expose an extra focus stop. |
| `Image` | Frame, background, placeholder text, error text, border | empty, loading, ready, error | Decoded pixels, fit mode, and decode cancellation remain image-owned. |
| `VectorCanvas` | Frame, background, selection highlight, error or empty text | selected, empty | Shape fills and strokes remain authored scene data. |
| `MapView` | Canvas frame, background, selection highlight, coordinate and error text | loading, error, selected | Map geometry and offline data colors remain authored content. |
| `MediaPlayback` | Host frame, placeholder, caption, status and error text | idle, loading, ready, playing, paused, stopped, suspended, error | Video pixels and native media behavior are not control theme colors. |
| `WebContent` | Host frame, placeholder, caption, status and error text | idle, loading, ready, suspended, error | Page CSS and browser-owned content remain outside the XUI style scope. |

## Implementation gates

The first Sonnet-5 pilot implements a shared extension mechanism and one representative control family.
The pilot must demonstrate state rules, named parts, actual drawing, default-path preservation, bindings, and declarative authoring.
The coordinator reviews its API and evidence before parallel implementation starts.

Parallel families share the same property, part, resource, precedence, and error contracts.
Each family owns its model and rendering adapters.
Central files need a single integration owner or explicit nonoverlapping edit regions.
Bindings and compiler support must not fall behind native setters.
The coverage record must distinguish implemented properties from planned properties and platform limits.

Default controls must not allocate a style dictionary.
Virtual rows must not retain one style instance or visual tree per source item.
Definitions remain shared, and effective values remain bounded.
New metrics must update measurement, hit testing, scrolling, and accessibility together.
High contrast, focus visibility, native input, ownership, and cancellation remain acceptance requirements.

The previously measured opt-in Button cost is accepted for this expansion.
That acceptance does not permit new unbounded memory use, idle rendering, or unrelated performance regressions.
