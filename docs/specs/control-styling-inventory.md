# Control styling inventory

This inventory records implemented styling coverage for the public XUI control catalog.
It distinguishes owned presentation from native or application-authored content.
The [control-style contract](control-styling.md) describes shared behavior and the compatible Button foundation.
The [template design](styling-and-templates-design.md) describes separate, later structural customization.
Control templates and item templates are not shipped.

## Shared vocabulary

A **surface** has background, foreground, border color, per-edge border thickness, corner radius, and padding.
A **frame** has these surface properties except foreground.
Passive layout frames reject foreground instead of accepting an unused text color.
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
| `Stack` | Frame, separator, padding, spacing, content alignment | disabled | Explicit structural padding/spacing override style values, including explicit zero. |
| `Grid`, `Wrap` | Frame, separator, padding, spacing, row/column gaps, content alignment | disabled | Existing tracks and child-placement APIs remain authoritative. |
| `AdaptiveLayout` | Frame, separator, padding, spacing | compact, expanded | Adaptive navigation state and ownership remain unchanged. |
| `PageView` | Frame, separator, padding, content alignment | disabled | Styling does not select or replace a page. |
| `ContentView` | Frame, padding, content alignment | disabled | Content ownership and retained child identity do not change. |
| `ScrollView` | Viewport frame, scrollbar track and thumb, track width, corner radius | dragging, scrollable on thumb | Scroll geometry and accessible bounds use the same metrics as painting. Foreground is unsupported. |
| `SplitView` | Pane frames, divider, grip, divider width | dragging | Divider hit testing and keyboard resizing retain their contracts. Foreground is unsupported. |
| `Expander` | Root surface, header frame, text, disclosure indicator, content frame, header padding and height | expanded | Fonts and text alignment belong only to `text`, not root/header/content. Collapsed children retain their state. |
| `SplitButton` | Root frame, spacing/alignment, retained primary/dropdown Buttons, separator | checked on Button children | Root foreground rejects. The composition reuses Button behavior and style definitions. |

## Choices, fields, and progress

| Control | Styling properties and parts | Additional states | Boundary |
| --- | --- | --- | --- |
| `RadioGroup` | Root surface, item surface and text, radio indicator, selected dot, row spacing and height | selected, item disabled | Stable choice IDs remain the source for input and accessibility. |
| `ChoiceList` presentation | Root and item surfaces, item text, selected marker, row padding and height | selected, item disabled | There is no retained control or style allocation for every source item. |
| `ComboBox`, editable and noneditable | Root/field surfaces, selected text, arrow, opt-in header | open, empty | `editor()`, `popup()`, and `choices()` expose real children. Noneditable editor access returns absence. |
| `TextInput`, plain and search variants | Header text, field surface, native text, placeholder, search icon, clear action, shortcut badge | empty, focused native editor | Supported native colors and font settings must match the surrounding frame. |
| `NumericInput` | Root/field surfaces and opt-in header | invalid, minimum, maximum | Native text uses `editor()`. Spin styling uses `decrease_button()` and `increase_button()`, not synthetic parent parts. |
| `RangeInput` | Root surface, track, filled track, thumb surface, thumb size, track thickness | dragging, minimum, maximum | Horizontal, vertical, and reversed geometry use one shared layout calculation. |
| `Progress` | Root surface, caption, track, filled segment, thickness | determinate, indeterminate, paused, error, unknown | Styling does not add an animation timer to a static progress control. |
| `MultilineText`, `RichText` | Owned frame, supported native background, text foreground and default font | read-only, empty | No header part. Rich defaults do not restyle existing authored runs. Selection, IME, undo, and native scrollbars retain ownership. |
| `PasswordInput` | Owned frame, supported native background, text foreground and font | revealed, empty | No header or reveal Button part. Styling does not expose password content or create a reveal control. |
| `DateTimePicker`, date, time, and calendar | Owned frame and native font | focused, disabled | No header part. Native content background/foreground reject. Calendar internals remain platform-owned. |
| `InlineStatus` | Surface, accent stripe, severity icon, message text, action and dismiss Buttons | information, success, warning, error, dismissed | Severity remains available through semantics and text, not only color. |
| `ColorPicker` | Frame, checkerboard colors, preview border, swatch border, channel labels | selected swatch, invalid channel | `channels()` and `swatch_button(index)` expose retained controls. `channel_field` rejects. RGBA content colors remain authored data. |

## Virtual collections and navigation

| Control | Styling properties and parts | Additional states | Boundary |
| --- | --- | --- | --- |
| `FileList` | Root and row surfaces, primary and secondary text, generic icon tint, focus marker, scrollbar | selected, item focused, item hovered, item disabled where supplied | Shell icons and thumbnail pixels retain their content colors. |
| `ItemsView`, list, tiles, and grouped | Root, row or tile, primary and secondary text, icon, inline action, inline progress, group header, scrollbar | selected, item focused, item hovered, item disabled, checked, group expanded | `tile.width` is base/local-only. Root row metrics remain uniform and use actual owner state. |
| `MillerColumns` | Retained column lists use the `ItemsView` schema | Actual column-list and item states | The composite root has no style schema. Column navigation and activation remain model-owned. |
| `TreeView` | Collection parts, disclosure indicator, indentation, pending and error text | expanded, loading, error, selected | Collapse, asynchronous cancellation, and stable item keys remain unchanged. |
| `DataGrid` | Root, header, row, alternating row, cell text, grid line, sort/filter icons, check indicator, scrollbar, reorder marker | selected, hovered, focused, sorted, descending, filtered, checked, mixed, filter pending, dragging | Outer row/header metrics belong to the root and remain uniform. Fonts belong only to cell/header text. |
| `HistoryChart` | Surface, title, caption, grid lines, plot line, line thickness | empty | Fonts belong to title/caption, not root. Missing samples remain gaps. Styling does not rewrite series values. |
| `NavigationList` | Collection parts, selection marker, icon, badge, section header, disclosure indicator | selected, selected descendant, expanded, compact | Disabled ancestors and filtered items retain current navigation semantics. |
| `NavigationView` | Root frame and retained title, toggle Button, search field, header/main/footer collections | expanded, compact, empty | Child accessors preserve actual targets. NavigationList children use retained Element wrappers, not TreeView wrappers. |
| `Breadcrumb` | Root frame, segment Buttons and text, separators, overflow Button | Root: overflowed, disabled. Segment Buttons use their own interaction states. | Native and binding accessors use segment keys with versions. There is no automatic root/current selector. |
| `NavigationPane` | Root frame and retained group Expander, item collection, progress, status text | loading, error, empty | Child styles target their actual controls. Style application does not cancel or replace a query. |
| `LocationPicker`, `ViewPicker` | Actual Popup root and explicit editor, navigation, toolbar, choices, and size-control accessors | Root: open, disabled. Child: loading/error/empty on NavigationPane; selected on choice items. | Root styles target Popup. Child styles target their real controls; facade-specific targets reject. |
| `TabStrip` | Root frame, tab surface, label, close action, separators and selection marker | selected, tab hovered, close hovered | The new-tab Button respects root insets but has no dedicated style part. Tab icons and drag states remain unsupported. Action identities remain unchanged. |

## Commands, windows, and hosted content

| Control or composition | Styling properties and parts | Additional states | Boundary |
| --- | --- | --- | --- |
| `CommandMenu` | Root, item, section header, separator, icon, check mark, shortcut, submenu arrow, scrollbar | item disabled, selected, checked, submenu open | Command snapshots and menu input semantics remain intact. |
| `CommandBar` | Root frame, retained command/overflow Buttons, separators and overflow menu | Root overflowed/disabled. Checked on command children. | Separator background and base/local thickness are implemented. Unstyled pitch remains unchanged. Arbitrary slot metrics and section labels are not supported. |
| `CommandSurface` and XUI command palette | Actual Popup root; retained search field, title/status Labels, menu, content and results Stacks, close Button | Root: open, disabled. Menu items: selected, checked, submenu open and interaction states. | Query loading/error/empty are not automatic facade-root selectors. Status Labels use their own Label schema. |
| Custom Shell menu | Owner-drawn Shell menu customization remains unsupported by this expansion. | No exported style states. | Neither custom nor platform-owned Shell menu rendering gains a facade style target. |
| `Popup` | Frame, padding, corner radius | open, disabled | Foreground rejects. Placement, clipping, dismissal, and focus restoration remain window-owned. |
| `ContentDialog` | Actual Popup root; retained title, body, footer, validation, primary and cancel children | Root: open, disabled. Validation: error on the retained InlineStatus. | Root invalid/loading/error rules reject. Modal input, native editors, and validation callbacks remain intact. |
| `TitleBar` | Root frame, caption Button defaults, retained title Label and caption Buttons | active, inactive, maximized | Child accessors target real controls. Hit-test regions, system actions, and drag behavior retain the native contract. |
| Tooltips | Surface, text, border, radius, padding | open (actually shown) | Styling uses the Window bridge and does not change delays or expose an extra focus stop. |
| `Image` | Frame, background, placeholder text, error text, border | empty, loading, ready, error | Decoded pixels, fit mode, and decode cancellation remain image-owned. |
| `VectorCanvas` | Frame, background, selection highlight, empty text | selected, empty | No error part or error state. Shape fills and strokes remain authored scene data. |
| `MapView` | Canvas frame, background, selection highlight, coordinate and error text | loading, error, selected | Map geometry and offline data colors remain authored content. |
| `MediaPlayback` | Host frame, placeholder, caption, status and error text | idle, loading, ready, playing, paused, stopped, suspended, error | Video pixels and native media behavior are not control theme colors. |
| `WebContent` | Host frame, placeholder, caption, status and error text | idle, loading, ready, stopped, suspended, error | Playing/paused reject. Page CSS and browser-owned content remain outside the XUI style scope. |

### Facade state boundary

Popup-backed facades keep the real Popup target and its single attachment.
Their roots expose only `open` and inherited `disabled`.
Root `invalid`, `loading`, `error`, `selected`, and `overflowed` rules reject explicitly.
Those names do not become supported merely because a retained child has a related model state.
The [retained-child binding contract](bindings.md#retained-composition-children) identifies the exposed application paths.

ContentDialog validation uses its retained InlineStatus, not a root invalid selector.
LocationPicker query states belong to its NavigationPane.
ViewPicker selection belongs to its retained choices.
CommandSurface status Labels do not gain automatic loading or error selectors.
Child names in this inventory do not imply named parts on the parent schema.

Command palette query-state selectors and custom Shell menu presentation remain coverage gaps.
The implementation does not substitute a private facade target or synthetic state setter for those gaps.

## Value and geometry contracts

Native text/header font limits are 512 DIPs, 31 UTF-16 code units, and normal or italic style.
These limits cover the native text fields, including document, password, and date/time controls.
Only TextInput exposes an existing header part in that native-field group.
Native paragraph parts reject vertical stretch but accept start, center, and end.
Supported container alignment retains stretch.
The schema defines the exact limits for each part.

ComboBox and NumericInput headers are opt-in presentations of the existing control name.
An authored header uses 24 DIPs by default or its explicit `headerHeight`, constrained by available space.
It does not create a Label, HWND, or accessibility provider.

DataGrid root `rowHeight` and `headerHeight` define uniform outer geometry.
Actual owner-state rules can change those metrics for the whole grid.
Header/row padding affects internal content, not a separate outer height for each item.
ItemsView `tile.width` accepts base/local values only and resolves to at least one DIP.
`tile.width` state rules and collection `root.width` reject.
Drawing, hit testing, scrolling, and UIA use the same resulting geometry.

## Delivery status

Sonnet-5 began the shared-engine pilot.
Astra completed the Toggle pilot and integrated the control-family expansion with the family owners and coordinator.
The delivered native adapters use the shared sparse engine, named parts, state rules, and bounded effective values.
The Button foundation remains compatible.
Its foundation examples do not limit the current shared engine to Button or Toggle.

The ABI, C#, Rust, and compiler use the same exported property/state masks and value limits.
Bindings include retained facade and choices/picker children and the Tooltip Window bridge.
They also expose keyed Breadcrumb/CommandBar Buttons, Window titlebar children, and NavigationView children.
NavigationPane bindings expose its retained group Expander and query progress indicator.
The [binding contract](bindings.md) records the available paths.
The [maintainer evidence](../llm/control-styling.md) records integration results and unresolved smoke caveats.

Default controls do not allocate a style dictionary.
Virtual rows do not retain a style instance or visual tree for each source item.
Definitions remain shared, and effective values remain bounded.
Style metrics update measurement, hit testing, scrolling, and accessibility together.
High contrast, focus visibility, native input, ownership, and cancellation remain acceptance requirements.

The previously measured opt-in Button cost is accepted for this expansion.
That acceptance does not permit new unbounded memory use, idle rendering, or unrelated performance regressions.
