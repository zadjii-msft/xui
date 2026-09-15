# Button styles and color resources

XUI supports reusable, sparse Button styles in C++, the C ABI, C#, Rust, and `.xui`.
This feature does not provide WPF API or XAML compatibility.
Control templates and item templates are not available.
The [design proposal](styling-and-templates-design.md) describes those later stages.

An unstyled Button keeps its existing Classic or WinUI presentation.
An authored face uses a flat fill, a border, and one corner radius.
Authored faces do not retain the WinUI elevation gradient.
A style that changes only text color or padding keeps the existing face.

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
The `.xui` compiler rejects duplicate declarations and duplicate state blocks.

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

Only Button has an optional style-state pointer.
Other controls and virtualized collection rows have no new style storage.
An unstyled Button allocates no style definition, state cache, or resource scope.
A styled Button retains one immutable definition and one local/effective-value cache.
Painting reads the cached values without dictionary, string, or ancestor searches.

C ABI structs use explicit property masks to distinguish unset values from zero.
Existing ABI structs and existing functions retain their layouts and signatures.
The [binding contract](bindings.md) describes handles, window ownership, sharing, release, and error results.
The wrappers share definitions per window without retaining unused native handles.

This stage does not support selectors, implicit styles, animated transitions, custom visual trees, or resource dictionaries on arbitrary elements.
Styles currently target only Button.
Resource values currently contain only theme-aware colors.
Final counterbalanced measurements stayed within the working default-path guardrail for the sampled workloads.
Authored styles still have a separate workload cost that has not met performance acceptance.
The [implementation evidence](../llm/control-styling.md) records measurements and unresolved test variability.
