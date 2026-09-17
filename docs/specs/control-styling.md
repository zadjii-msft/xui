# Control styles and color resources

XUI supports reusable, sparse styles in C++, the C ABI, C#, Rust, and `.xui`.
The shared control-style engine supports named parts, state rules, and typography across control families.
The [inventory](control-styling-inventory.md) and [binding contract](bindings.md) identify available styling surfaces and remaining gaps.
This feature does not provide WPF API or XAML compatibility.
Control templates and item templates are not available.
The [design proposal](styling-and-templates-design.md) describes those later stages.

An unstyled Button keeps its existing Classic or WinUI presentation.
An authored face uses a flat fill, a border, and one corner radius.
Authored faces do not retain the WinUI elevation gradient.
A style that changes only text color or padding keeps the existing face.

Labels have no background fill unless the application specifies `background`.
Font, foreground, padding, and border properties do not add a fill.
The parent surface remains visible through the label in Classic and WinUI.
`TextInput` captions also have transparent backgrounds.
The editor field retains its own background.

The [Minesweeper demo](../../bindings/dotnet/Minesweeper/README.md) uses shared styles for borderless cleared cells, numbered cells, flags, and game outcomes.
Its presentation layer changes style references without changing game rules or cell identity.

## Named declarations

Declarations belong inside a component.
The following example uses the same grammar as the compiling `DeclarativeSample\Counter.xui`:

```xui
component StyledActions {
    resources {
        DangerFill: theme(light: 0xB91C1C, dark: 0x991B1B);
        OnDangerFill: 0xFFFFFF;
        DangerEdge: 0x7F1D1D;
        DangerHover: theme(light: 0xDC2626, dark: 0xB91C1C);
        DisabledFill: theme(light: 0xAAAAAA, dark: 0x555555);
    }
    style DangerButton for Button {
        background: resource(DangerFill);
        foreground: resource(OnDangerFill);
        cornerRadius: 0;
        borderBrush: resource(DangerEdge);
        borderThickness: (3, 0, 0, 0);
        when hovered { background: resource(DangerHover); }
        when disabled { background: resource(DisabledFill); }
    }
    view {
        VStack {
            Button("Delete", style: DangerButton);
        }
    }
}
```

`resources` distinguishes named definitions from C# component state.
This stage uses explicit component scope instead of implicit ancestor lookup.
The generator resolves names before native painting.
The [language guide](xui-language.md) defines the complete grammar and diagnostics.

## Properties and precedence

The sparse Button foundation properties are `background`, `foreground`, `borderBrush`, `borderThickness`, `cornerRadius`, and `padding`.
Colors contain opaque RGB24 light and dark values.
Dimensions use device-independent pixels and must be finite, from zero through 32768.
Insets use left, top, right, bottom order.
A single inset value applies to all four edges.

An absent property is unset.
Black, zero padding, zero border width, and a zero radius are explicit values.
Zero radius produces square corners.
A `(3, 0, 0, 0)` border occupies only the left edge.
Unequal borders use four edge regions clipped to the outer rounded silhouette.
Their inner corners are rectangular rather than independently rounded.

The effective value follows this order, from lowest to highest priority:

1. The current Classic or WinUI default.
2. Base-style properties, then derived-style properties.
3. Active state properties, in the order `focused`, `checked`, `hovered`, `pressed`, `disabled`.
4. Local Button properties.
5. The high-contrast accessibility policy for face colors and outline geometry.

State properties inherit separately from ordinary properties.
A derived hover rule replaces only the inherited hover properties that it specifies.
An inherited disabled rule still wins over a derived hover rule.
For repeated native rules with the same state, the last specified property wins.
The `.xui` compiler rejects duplicate declarations.
Toggle declarations also reject duplicate state blocks.
Button declarations retain repeated state blocks for compatibility.

A Button definition that adds typography or named parts can inherit a legacy-compatible Button definition.
The compiler creates shared generic copies of legacy ancestors for that derived chain.
References to the original legacy definitions remain `ButtonStyle` references.
Promotion preserves theme colors, sparse values, inheritance depth, and field-wise precedence for repeated legacy state blocks.
Style reload replaces both graphs without replacing controls.

Local values survive style replacement and removal.
Clearing a local property exposes the effective style value again.
Clearing both the style and all local properties restores the unstyled path.
The native state cache also accepts disabled-ancestor and modal context from the window backend.
This context does not change the stored local `enabled` value.

## C++ definitions

```cpp
#include "xui/styling.hpp"
#include "xui/controls.hpp"

auto resources = xui::ResourceScope::create({
    {"DangerFill", xui::ThemeColor{0xb91c1c, 0x991b1b}},
    {"DangerHover", xui::ThemeColor{0xdc2626, 0xb91c1b}},
    {"DangerAlias", std::string{"DangerFill"}}
});
xui::ButtonStyleValues values;
values.background = resources->color("DangerAlias");
values.foreground = xui::ThemeColor{0xffffff};
values.border_brush = xui::ThemeColor{0x7f1d1d};
values.border_thickness = xui::Insets{3, 0, 0, 0};
values.corner_radius = 0.0f;
xui::ButtonStyleValues hover;
hover.background = resources->color("DangerHover");
auto danger = xui::ButtonStyle::create(values, {
    {xui::ButtonStyleState::hovered, hover}
});
auto button = std::make_shared<xui::Button>(L"Delete");
button->set_style(danger);

xui::ButtonStyleValues local;
local.padding = xui::Insets{16, 8, 16, 8};
button->set_style_values(local);
button->set_style(nullptr);       // Local padding remains.
button->set_style_values({});     // Default presentation returns.
```

`ButtonStyle::create(values, rules, based_on)` creates a derived definition.
The factory copies inputs and returns an immutable shared definition.
Derivation has a maximum depth of 16.
Each definition accepts at most 256 rules.
Construction resolves all 32 combinations of the five state flags.
It does not retain the base definition after that copy.
Native construction cannot create an inheritance cycle because parents are already immutable.
The `.xui` compiler reports cycles in named declarations.

`ResourceScope::create(entries, parent)` accepts at most 256 named colors or aliases.
Each name requires 1 through 256 bytes.
The maximum scope depth is 16.
Local names shadow parent names.
An alias uses the scope where its declaration belongs.
An inherited alias does not change when a child shadows its target name.
Duplicate names, missing names, and alias cycles produce explicit errors.

Resource lookup occurs during definition construction or an explicit `color(name)` call.
It never occurs during native painting.
Resources preserve both theme colors in each resolved value.
Changing `Window::theme` changes the selected color without a name lookup.
Definitions are immutable: applications replace a style to publish different resource values.
There is no mutable resource dictionary or automatic resource-change subscription.

## Layout, paint, and accessibility

Padding and border widths contribute to automatic Button measurement.
Explicit padding replaces the default padding and default minimum Button height.
Explicit control size constraints still apply.
Fixed-size Buttons keep their size and clip content within the authored content area.
Text and icons use the remaining area after padding and borders.
Button text is centered in this area by default.

A color or radius change requests paint.
A padding or border-width change requests layout and paint.
State transitions compare the cached geometry before they request layout.
Repeated native application of the same style pointer does not invalidate.
The wrappers also reuse native definitions and skip repeated same-style assignments.

High contrast ignores authored foreground, background, border color, radius, and border widths for the face.
The renderer uses the existing system-color face and visible outline instead.
Authored padding and border widths still reserve content space.
The keyboard focus ring remains independent of authored state rules.
Classic and WinUI retain their existing focus-visibility policies.

The retained New tab Button supports ordinary Button styles through `TabStrip::new_tab_button()` in C++, `TabStrip.NewTabButton` in C#, and `TabStrip::new_tab_button()` in Rust.
The C ABI exposes it through `xui_feature_child(tab_strip, 0, result)`, including while hidden.
The accessor does not change visibility or the existing New tab action.

Styles do not replace HWNDs, UI Automation providers, event handlers, or native editors.
Theme and style changes preserve native text, selection, and ownership.
No native IME, undo, or input implementation changes are part of this stage.

NavigationView lists accept a `NavigationList` style through their retained child accessors.
Root `rowHeight` sets row geometry. Root `fontSize` supplies inherited text size.
The `icon.size` property sets a square slot in DIPs for vector icons and Shell images.
It accepts finite values from zero through 32768, including state rules.
An unset size preserves the existing icon dimensions.

## Storage and binding boundaries

Element has an optional generic attachment pointer.
Button retains its separate, compatible style-state pointer.
An unstyled control does not allocate an attachment.
Virtualized collection rows do not retain style instances.
An unstyled Button allocates no style definition, state cache, or resource scope.
A styled Button retains one immutable definition and one local/effective-value cache.
Painting reads the cached values without dictionary, string, or ancestor searches.

Button C ABI structs use explicit property masks to distinguish unset values from zero.
Generic property records distinguish unset values by absence.
Existing ABI structs and existing functions retain their layouts and signatures.
The [binding contract](bindings.md) describes handles, window ownership, sharing, release, and error results.
The wrappers share definitions per window without retaining unused native handles.

This stage does not support selectors, implicit styles, animated transitions, custom visual trees, or resource dictionaries on arbitrary elements.
The exported catalog defines supported targets across the control families.
Each target accepts only the properties and states that its presentation supports.
Resource values currently contain only theme-aware colors.
Final counterbalanced measurements stayed within the working default-path guardrail for the sampled workloads.
Authored styles still have a separate workload cost.
The [implementation evidence](../llm/control-styling.md) records measurements and unresolved test variability.

## Toggle pilot

This example and its original property subset describe the committed Toggle foundation.
The current generic schema also supports root/label typography and alignment.
The inventory and exported catalog describe the complete current coverage.

Toggle uses `ControlStyle`, not `ButtonStyle`.
The generic engine stores sparse per-part state rules instead of every possible state combination.
The state mask has 64 bits.
The pilot accepts `focused`, `checked`, `hovered`, `pressed`, and `disabled`, in that precedence order.
Each rule selects one state.
Inherited state values remain separate from ordinary values.

The foundation schema has four parts:

- The implicit root accepts background, foreground, border brush, border thickness, corner radius, and padding.
- `label` accepts foreground.
- `indicator` accepts background, border brush, border thickness, corner radius, and size.
- `mark` accepts foreground.

Unknown targets, parts, properties, and states produce errors.
The indicator size specifies its outer square in DIPs.
Its border occupies space inside that square.
Root padding and borders surround the indicator and label.
Automatic measurement reserves the larger of the indicator height and text height.
Explicit control size constraints still apply.
The complete Toggle remains one native input and accessibility target.
Parts do not create native windows or accessibility providers.

A label without its own foreground inherits the effective root foreground.
This includes local-only root values and disabled-state values.
An explicit label foreground overrides that inheritance.
Locals survive style replacement and removal.
Clearing the style and all locals releases the attachment.

High contrast suppresses authored colors and face outlines.
The system palette supplies colors and visible outlines.
Authored metrics still reserve content space.
Keyboard focus remains separate from authored rules.

```xui
style CompactToggle for Toggle {
    foreground: theme(light: 0x202020, dark: 0xEEEEEE);
    part indicator {
        background: theme(light: 0xEEEEEE, dark: 0x202020);
        borderBrush: 0x777777;
        borderThickness: 1;
        cornerRadius: 3;
        size: 18;
        when checked { background: 0x2468AD; }
    }
    part mark { foreground: 0xFFFFFF; }
    when disabled { foreground: 0x888888; }
}
```

The declaration belongs inside a component.
`Toggle("Active", style: CompactToggle)` applies it.
Part declarations belong directly in a style.
A part can contain properties and `when` rules.
Nested parts and parts inside `when` rules are invalid.
Button declarations also accept their supported named parts and typography.
The compiler retains the compatible `ButtonStyle` path for declarations that use only the original Button properties.

```csharp
var style = new Xui.ControlStyle(Xui.StyleTarget.Toggle,
    [new(Xui.StylePart.Indicator, new() { Size = 18, CornerRadius = 3 })],
    [new(Xui.StylePart.Indicator, Xui.StyleState.Checked,
        new() { Background = new Xui.ThemeColor(0x2468AD) })]);
var toggle = window.Toggle("Active").SetStyle(style);
toggle.SetStyleValues(Xui.StylePart.Label, new() { Foreground = new Xui.ThemeColor(0) });
toggle.SetStyle(null); // Local label foreground remains.
toggle.SetStyleValues(Xui.StylePart.Label, new()); // Default presentation returns.
```

C++ exposes the common attachment through `Element::set_control_style` and `Element::set_control_style_values`.
Toggle convenience methods forward to that attachment.
C# exposes `Element.SetControlStyle` and `Element.SetControlStyleValues`.
Rust exposes `Element::set_control_style` and `Element::set_control_style_values`.
Unsupported control types reject generic styles.
The [binding contract](bindings.md#generic-control-styles) defines current record and handle behavior.

## Native field boundaries

`TextInput`, `MultilineText`, `RichText`, `PasswordInput`, and `DateTimePicker` have distinct style targets.
Each target exposes only properties that its owned frame or native editor supports.
The native editor retains text input, selection, IME composition, undo, and ownership.

Date/time styles can change the owned frame surface and native text typography.
The adapter uses `WM_SETFONT` for native date/time typography.
The themed native date/time control does not reliably support authored text foreground or background.
Those text properties are rejected rather than accepted without an effect.
Calendar internals remain platform-owned.

Multiline and rich-document typography changes use `EM_SETCHARFORMAT` with `SCF_DEFAULT`.
They change native character defaults, not application-authored rich runs.
Styles must not rewrite existing rich runs or clear the undo history.
The separate `RichText` target preserves this distinction.

`TextInput` exposes its existing header and clear-action parts.
The retained renderer paints the header text without a background fill.
The native caption window still supplies the accessible name for the editor.
The `clear_action` part accepts only `background`, `foreground`, `borderBrush`, and `cornerRadius`.
Padding, border thickness, size, and typography are unsupported on this part.
The native action bounds and editor reservation remain unchanged.
Styles do not create a clear action where the native presentation does not show one.

The renderer resolves parent defaults from the actual clear Button interaction state and the owner disabled and empty states.
It passes these defaults directly to the shared Button renderer, without a child attachment or another cache.
Explicit child legacy styles, generic styles, and local values override these defaults.
The WinUI renderer preserves `Symbol::clear`, the existing Button identity, and the native undoable clear action.

Document, password, and date/time controls do not expose an owned header part.
Applications can place a separately styled Label beside those controls.
Password reveal uses the existing reveal API and state, not a synthetic reveal-button part.
Unsupported header and reveal-button parts produce explicit schema errors.

### Numeric and editable ComboBox insets

`NumericInput` and editable `ComboBox` use nested surfaces with additive insets.
Root padding and borders inset the composition.
Field padding and borders then inset the retained editor and the spin-button or arrow regions within the field frame.
ComboBox and NumericInput show an owned header only when the application explicitly authors the `header` part.
The header paints the existing control name without creating a Label, native window, or accessibility provider.
Its default height is 24 DIPs, or the authored `headerHeight`, limited by available space.
Unstyled controls do not have an always-visible header.
The retained native `TextInput` keeps its own default or authored text-content insets.
Parent field insets do not replace these child insets.

To change the native text inset, set `root.padding` explicitly on the retained child editor.

### ColorPicker parts and retained children

The `checkerboard_light` and `checkerboard_dark` parts each accept `background`.
The `channel_label` part accepts text properties and padding.
Its `invalid` rule applies only to the corresponding invalid channel.
Root `invalid` remains global when any channel is invalid.
The `channel_field` part is unsupported and rejects explicitly.
Styles do not replace the selected color or the color represented by a swatch.

In C++, `ColorPicker::channels()` exposes the retained NumericInput children.
Applications can style each channel and its existing editor and Buttons through their own targets.
`ColorPicker::swatch_button(index)` exposes a retained swatch Button.
C# exposes `Channel(index)` and `SwatchButton(index)`.
Rust exposes `channel(index)` and `swatch_button(index)`.
The [binding contract](bindings.md) defines binding coverage.

## Owned hosts and scenes

Host styles affect owned frames, overlays, placeholder text, status text, and selection indicators.
They do not recolor decoded images, authored scene data, native media content, or web page content.
Media and web captions appear only while the native content surface is inactive.
Frame geometry and clipping preserve the existing native child identity.

WebContent supports its actual `idle`, `loading`, `ready`, `stopped`, `suspended`, and `error` states.
It does not advertise media-only `playing` or `paused` states.
Image states are `empty`, `loading`, `ready`, and `error`.
Style application does not restart requests, change request tokens, or transfer cancellation ownership.

## Retained child projections

A native composition adapter can project parent part values into an existing child with `Element::set_control_style_projection`.
This parent layer has lower precedence than the child's own generic style and local values.
The projection uses the child's existing attachment, not a second cache or a replacement control.
An empty projection clears only the parent layer.
The adapter must clear its projections when the parent style or composition no longer supplies them.

The child's ordinary local-value getter does not include the projection.
The effective-value getter includes it.
`control_style_projection_values` and `own_control_style_values` keep the layers separate for adapters that also support legacy Button styles.
Legacy child styles and local values must remain above the projected parent defaults.
Repeated projection updates on existing parts require no allocation.

## Shared typography and content geometry

`StylePartSchema::typography_from` declares the source of missing font fields.
The source must precede the destination in the schema.
Inheritance covers font fields, alignment, wrapping, and line limits, but only properties that the destination accepts.
An explicit destination value overrides its inherited value.

Cached and transient resolution use the same inheritance rules.
Transient non-root rules use the requested item state, plus disabled context from the owner.
Root values use the actual owner state, including when other parts inherit them.
An authored root hover rule can therefore change the inherited foreground of every unoverridden item.
Owner hover and focus do not activate individual item hover or focus rules.
An immutable `StyleFontFamily` owns both UTF-8 and native wide-string encodings.
Font inheritance shares that owner without a separate font allocation for each part.
Each part can specify font limits through `StylePartSchema::limits`.
These limits include the maximum font size, UTF-16 family length, and supported font styles.
Separate horizontal and vertical masks restrict alignment to values that each part supports.
Text parts reject vertical stretch when the renderer cannot stretch the text.
Layout parts can retain stretch for their arranged children.
An inheritance source cannot permit fonts that its destination rejects.
The schema export supplies the same limits to managed bindings and the `.xui` compiler.

`style_content_insets` combines padding and border thickness.
`style_content_bounds` applies those insets and clamps the remaining width and height to zero.
Both helpers accept default padding and border values.
An explicit zero overrides the corresponding default.

Stack padding and spacing use this precedence, from lowest to highest:

1. Built-in or composition defaults.
2. Style definitions, overridden by local style values.
3. Explicit structural values from `set_padding` and `set_spacing`, including zero.

Composition constructors use `set_default_padding` and `set_default_spacing` without marking those values explicit.
Later default calls cannot overwrite explicit structural values.
Clearing a style restores composition defaults unless an explicit structural value takes precedence.
Root borders and separator insets remain additive to the effective padding.
Default setters do not create a style attachment.

The `.xui` compiler emits Stack padding and spacing setters only for authored arguments.
Omitted arguments leave style values and composition defaults available.
Explicit `padding: 0` and `spacing: 0` remain structural overrides on both stack axes, including after style reload.

`rowHeight` requires a positive value of at most 32768 DIPs.
Zero, negative values, and nonfinite values produce an error before attachment.
This restriction prevents zero-sized rows in virtualized geometry.

### Uniform metrics and state rules

Each part has separate property masks for base/local values and state rules.
`StylePartSchema::allowed` defines base/local properties.
`StylePartSchema::state_allowed` further restricts properties in state rules.
The default state mask permits every property that `allowed` permits.
Unsupported rule properties produce an error before style publication.

ItemsView uses `tile.width` for uniform tile width, not `root.width`.
Base definitions and local values can change tile width and the resulting column count.
Every state rule on `tile.width` is rejected.
Tile padding, borders, colors, and supported text typography remain state-capable where the renderer consumes them.
Text stays within the explicit uniform row or tile extent, without a source scan for automatic sizing.
Collection scrollbar reservation uses `scrollbar.width`, not `scrollbar.size`.

Grid row height and header height belong to the root, not individual rows, cells, or headers.
Grid scrollbar width belongs to the scrollbar part.
The grid root supports actual owner `focused`, `hovered`, and `disabled` states.
These states can change root metrics for the whole grid.
The scrollbar width supports the owner `disabled` state.
Per-row padding remains an internal content inset and does not change the uniform row extent.
Unsupported grid indicator size and per-item width, spacing, or row-height properties reject.

RadioGroup and ChoiceList keep uniform row height and spacing on the root.
Their supported owner-state metrics change actual row geometry.
Supported per-item padding and indicator size remain state-capable because they change actual item content geometry.
No variable-row layout or per-source-row style cache is part of this contract.

The C ABI exports the effective rule mask as `state_properties`, intersected with the part's allowed properties.
Generated C#, Rust, and compiler catalogs carry this separate mask.
Definition validation and `.xui` diagnostics use it, rather than treating independent property and state support as sufficient.
The catalog parity checks compare base properties, states, and rule properties separately.

## Window tooltips

Tooltip styles use the Window API, not an Element handle.
The Window retains one optional attachment for the `Tooltip` target.
Style application and removal do not show a hidden tooltip.
The existing tooltip timing, focus, and placement rules remain in effect.
The `open` state means that the existing tooltip is visible.
It does not request visibility.

The C++ API provides `set_tooltip_style`, `tooltip_style`, `set_tooltip_style_values`, `tooltip_style_values`, and `effective_tooltip_style_values`.
An empty local value replaces that part's local layer.
A null definition clears the style but preserves locals.

The C ABI provides `xui_window_set_tooltip_style` and `xui_window_try_set_tooltip_style`.
They use existing generic style handles and weak identities.
`xui_window_set_tooltip_style_values` and `xui_window_get_tooltip_style_values` use the existing typed property records.
These functions require a Window handle and obey its lifetime, thread, and mutation guards.

```csharp
window.SetTooltipStyle(new ControlStyle(StyleTarget.Tooltip, [
    new(StylePart.Root, new() { Padding = new Insets(8), CornerRadius = 4 }),
    new(StylePart.Text, new() { FontSize = 16 })
]));
window.SetTooltipStyleValues(StylePart.Root, new() { Foreground = new ThemeColor(0) });
var effective = window.GetTooltipStyleValues(StylePart.Text, effective: true);
window.SetTooltipStyle(null);
window.SetTooltipStyleValues(StylePart.Root, new());
```

Rust provides `Window::set_tooltip_style`, `set_tooltip_style_values`, and `tooltip_style_values`.
`None` clears the definition, and default `PartStyleValues` clear a local layer.

The `.xui` compiler does not provide Window-style application syntax.
It rejects Tooltip declarations rather than creating a style that a component cannot apply.
Applications can construct and apply Tooltip styles through the native, C#, or Rust Window API.

## Portable schema catalogs

The `.xui` compiler and managed definition constructors use compiled, checked-in schema catalogs.
They do not load the native DLL for validation.
The native schema and limit queries support an explicit export and parity-check step.
They are not prerequisites for ordinary markup compilation.

The exporter records the exact supported target, part, property, state, and value-limit metadata.
The same snapshot generates the C#, Rust, and compiler catalogs.
Portable tests compare those catalogs and prevent native library loads during managed definition construction.
The native parity check detects differences between the snapshot and the current DLL.
The [contributor guide](../../CONTRIBUTING.md#binding-generation-and-compatibility) describes the export and check commands.
