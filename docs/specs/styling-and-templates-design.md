# Styles and templates

This design defines the target for application-authored control presentation.
It does not describe a completed implementation of that target.
The [Button styling contract](control-styling.md) describes the first implementation stage.
Later delivery stages need their own public contracts and acceptance evidence.
The [language guide](xui-language.md) describes the current `.xui` syntax.
The [appearance reference](winui-style.md) describes the current built-in styles.

## Goal

An application can change a control's appearance without replacing its behavior.
Simple changes use properties and reusable styles.
Structural changes use control templates and item templates.
The target is WPF-level customization, not WPF API, XAML, or binary compatibility.

An application author must not implement painting or accessibility to make a red button.
A different border must not require a different control implementation.
A custom list item must retain selection, keyboard navigation, and virtualization.
Existing applications must retain their default appearance and behavior.

Performance and memory are acceptance conditions.
The implementation must preserve the inexpensive default rendering path.
Custom presentation pays for its own retained content, not for every source item.

## Authoring target

This example shows the intended authoring model.
It is not a claim that the current compiler accepts this syntax.
The implementation contract will define resource declarations, scope, and supported properties.

```text
style DangerButton for Button {
    background: resource(DangerFill);
    foreground: resource(OnDangerFill);
    cornerRadius: 0;
    borderBrush: resource(DangerEdge);
    borderThickness: (3, 0, 0, 0); // left, top, right, bottom
    when hovered { background: resource(DangerHover); }
    when disabled { background: resource(DisabledFill); }
}

Button("Delete", style: DangerButton);
ItemsView("Results", itemTemplate: ResultCard);
```

The button and item-template examples belong to different delivery stages.
A working button style does not establish item-template support.
Compiler diagnostics must reject unavailable features instead of ignoring them.

## Property and resource model

Properties have types, defaults, validation rules, and invalidation metadata.
The property system distinguishes an unset value from an explicit value.
Clearing an override restores resolution from the remaining sources.
Style application must not overwrite the stored local value.

The implementation contract must define precedence for all supported value sources.
These sources include local values, bindings, state rules, style inheritance, theme resources, and defaults.
The contract must also define conflict resolution for simultaneous states.
Declaration order must not become an accidental rule.
Later animation support needs its own precedence and restoration rules.

Resource keys resolve within an explicit scope.
Theme resources can supply different values for dark, light, and high contrast.
Missing resources, incorrect types, and cyclic references produce explicit errors.
Resolution must have bounded work and storage.
A theme change updates dependent values without changing control identity.

Styles share immutable definitions.
Control instances retain only the overrides and resolved state they need.
Default controls must not allocate a property dictionary.
Painting must not repeatedly search resource dictionaries or ancestor chains.
Changes invalidate paint or layout according to property metadata.

The initial appearance properties include colors, padding, corner radii, and per-edge border thickness.
Later stages add typography, content alignment, richer brushes, clipping, transforms, and transitions.
Every exposed property needs a renderer or layout consumer.
An unused setter does not establish support.

## Control templates

A semantic control owns commands, input behavior, model state, and accessible identity.
A template defines that control's replaceable visual structure.
Template bindings read properties from the owning control.
They remain connected after style, state, and resource changes.

Visual primitives include borders, panels, text, paths, images, and content presenters.
Decorative visual nodes do not require native windows or independent accessibility peers.
Interactive children retain their own input and accessibility contracts.
Shared drawing resources remain owned by the window.

Content and control templates have different roles.
A control template defines the surrounding structure and state presentation.
A content template defines the presentation of the supplied content.
Applications can replace either without duplicating control behavior.

Complex controls publish named-part contracts.
Each contract defines required parts, accepted types, and optional behavior.
Missing required parts produce an error before the replacement becomes active.
Templates must not depend on private native handles or undocumented child indexes.

Template application has explicit attachment and detachment phases.
A failed replacement leaves the previous valid presentation active.
Detachment releases bindings, callbacks, cached visuals, and owned resources.
Replacement preserves model state and accessible identity.
The contract defines focus transfer when the focused visual part disappears.

Classic and WinUI presentations will use the same public extension points as applications.
An optimized built-in renderer can remain where its observable behavior matches the contract.
Optimization must not make application styles ineffective.

## Item templates and virtualization

Collections expose separate customization points for item content, item containers, groups, and layout.
The item template describes data presentation.
The container template describes selection, focus, hover, and surrounding decoration.
The layout panel defines item arrangement and scrolling.
Template selection supports different presentations for different item kinds.

Collections retain stable item keys independently of their visual instances.
Only visible items and a bounded buffer need realized visual trees.
Template definitions are shared across instances.
Recycling must clear old bindings, callbacks, transient state, and asynchronous requests.
A recycled visual must never apply an action to a previous item.

Selection remains part of the collection model.
An embedded button does not silently replace row selection or row activation.
Keyboard traversal, pointer routing, and accessible parentage need explicit contracts.
Virtual accessibility providers resolve stable keys rather than recycled visual addresses.

The first virtualized template layout uses fixed item dimensions.
Variable-height layouts need indexed estimates, incremental measurement, and scroll anchoring.
They must not measure every source item during initial display or ordinary scrolling.
Custom panels must declare their virtualization contract.
Unsupported panel combinations must not silently realize the entire collection.

The existing metadata-based row renderer remains an optimized default presentation.
Its source contract must not gain per-row visual allocations.
Existing large-source applications must retain bounded visible-content queries.

## Native text and host boundaries

Native editors retain ownership of text, IME, selection, caret behavior, undo, and native accessibility.
Templates can change their surrounding borders, headers, adornments, and supported editor properties.
Restyling must not replace a live editor merely to change its frame.
An active composition or edit needs an explicit preservation or deferral policy.

Native editor internals and third-party hosts do not promise arbitrary visual-tree effects.
Clipping, transforms, transparency, and overlays need documented platform limits.
Unsupported combinations produce an explicit error or a documented restriction.
Replacing the native editing architecture is outside this design.

High contrast retains a documented system-color policy.
Custom presentation must retain visible keyboard focus and accessible names.
Decorative template content must not create duplicate announcements.

## Authoring, bindings, and diagnostics

The runtime defines the common property and template semantics.
C++, the C ABI, C#, Rust, and `.xui` expose those semantics within their documented coverage.
New ABI functions preserve existing structure layouts and error behavior.
The compiler emits typed bindings and reusable factories.
A runtime XAML parser is not required.

Default templates need application-author examples.
Diagnostics identify missing resources, invalid values, binding failures, and missing template parts.
Property inspection reports the effective value and its source.
Template inspection distinguishes semantic controls from decorative visuals.

Hot reload follows the existing identity and ownership contracts.
Property changes and structural replacements need separate rules.
A structural edit must not pretend to preserve state that the runtime cannot preserve.

## Performance acceptance

Performance evidence compares a pristine baseline with the changed implementation.
Both runs use the same architecture, build configuration, workloads, and measurement method.
Repeated samples separate reproducible changes from run noise.
Builds and desktop tests must not overlap measurements.
Results record environmental interference and unresolved uncertainty.

The initial engineering guardrail rejects reproducible regressions greater than 5% in default-path latency, CPU work, or private memory.
This guardrail is not evidence of negligible cost by itself.
Results also report absolute deltas, resource counts, and retained storage.
Inconclusive measurements do not satisfy acceptance.

Default-path acceptance requires:

- No heap allocation for style state on controls that do not use styles.
- No additional native windows, render targets, or idle paints.
- No per-item visual allocation for existing virtual sources.
- Measured control-size overhead, including any optional style reference.
- Bounded cache storage after repeated style, theme, and template changes.

Custom-presentation acceptance requires:

- Shared style and template definitions rather than a copy for every instance.
- Cached property resolution outside the ordinary paint path.
- Paint-only invalidation for paint-only changes.
- Realized collection storage bounded by the viewport and the declared buffer.
- Bounded resource retention after scrolling, replacement, cancellation, and closure.

Measurements cover unstyled controls and styled controls separately.
Template measurements also record visual count, interactive child count, and content complexity.
Arbitrary application-authored content cannot have a universal zero-cost guarantee.
New complexity must remain explicit and measurable.

Commands belong in [CONTRIBUTING](../../CONTRIBUTING.md).
Dated results belong in the [maintainer notes](../llm/README.md), not the root README.

## Delivery and acceptance

1. The style foundation supplies typed properties, resources, state rules, button presentation, bindings, and a compiling declarative example.
2. Control templates supply lightweight visuals, content presenters, named parts, and replacement lifecycle.
3. Collection templates supply item and container presentation with bounded realization and recycling.
4. Extended presentation supplies the remaining control coverage, template selection, brushes, transforms, transitions, and inspection.

Each stage includes its own performance evidence and public coverage statement.
Later capabilities remain planned until their implementation and evidence exist.
The first stage is not a claim of full customization parity.

Acceptance covers actual output and interaction, not only successful property calls.
The button example needs visible color changes, square corners, and a left-only border.
State changes, DPI changes, themes, and high contrast need consistent output.
Template replacement needs focus, accessibility, resource-lifetime, and native-input checks.
Collection templates need sorting, filtering, cancellation, keyboard, and retained-memory checks.
