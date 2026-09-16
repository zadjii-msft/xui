# Control styles and color resources

XUI supports reusable, sparse Button styles in C++, the C ABI, C#, Rust, and `.xui`.
The Toggle pilot uses a shared control-style engine with named visual parts.
This feature does not provide WPF API or XAML compatibility.
Control templates and item templates are not available.
The [design proposal](styling-and-templates-design.md) describes those later stages.

An unstyled Button keeps its existing Classic or WinUI presentation.
An authored face uses a flat fill, a border, and one corner radius.
Authored faces do not retain the WinUI elevation gradient.
A style that changes only text color or padding keeps the existing face.

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

The sparse properties are `background`, `foreground`, `borderBrush`, `borderThickness`, `cornerRadius`, and `padding`.
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

Styles do not replace HWNDs, UI Automation providers, event handlers, or native editors.
Theme and style changes preserve native text, selection, and ownership.
No native IME, undo, or input implementation changes are part of this stage.

## Storage and binding boundaries

Control has an optional generic attachment pointer.
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
Styles currently target Button and Toggle.
Resource values currently contain only theme-aware colors.
Final counterbalanced measurements stayed within the working default-path guardrail for the sampled workloads.
Authored styles still have a separate workload cost.
The [implementation evidence](../llm/control-styling.md) records measurements and unresolved test variability.

## Toggle pilot

Toggle uses `ControlStyle`, not `ButtonStyle`.
The generic engine stores sparse per-part state rules instead of every possible state combination.
The state mask has 64 bits.
The pilot accepts `focused`, `checked`, `hovered`, `pressed`, and `disabled`, in that precedence order.
Each rule selects one state.
Inherited state values remain separate from ordinary values.

The Toggle schema has four parts:

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
Part declarations belong directly in a Toggle style.
A part can contain properties and `when` rules.
Nested parts and parts inside `when` rules are invalid.
Button grammar does not accept parts or `size`.

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

C++ exposes the common attachment through `Control::set_control_style` and `Control::set_control_style_values`.
Toggle convenience methods forward to that attachment.
C# exposes `Control.SetControlStyle` and `Control.SetControlStyleValues`.
Rust exposes `Element::set_control_style` and `Element::set_control_style_values`.
Unsupported control types reject generic styles.
The [binding contract](bindings.md#generic-control-styles-toggle-pilot) defines record and handle behavior.
