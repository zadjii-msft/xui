# 5. Styles and themes

Styles change presentation without replacing controls, callbacks, or native input.
Add these declarations inside `TaskCard`, before `view`.
They also appear in the [complete final component](sample/TaskCard.xui).

```xui
resources {
    Accent: theme(light: 0x1456B8, dark: 0x78B7FF);
    TextColor: theme(light: 0x202020, dark: 0xEEEEEE);
}

style HeadingText for Label {
    foreground: resource(TextColor);
    fontSize: 24;
    fontWeight: 600;
    wrapping: true;
    maximumLines: 2;
}

style ApplyAction for Button {
    foreground: theme(light: 0xFFFFFF, dark: 0x102030);
    background: resource(Accent);
    padding: (16, 8, 16, 8);
    cornerRadius: 4;
    when hovered {
        background: theme(light: 0x104490, dark: 0xA8D2FF);
    }
}

style CompletionToggle for Toggle {
    foreground: resource(TextColor);
    part indicator {
        borderBrush: resource(Accent);
        borderThickness: 1;
        size: 20;
        when checked { background: resource(Accent); }
    }
    part mark {
        foreground: theme(light: 0xFFFFFF, dark: 0x102030);
    }
}

style CompletionProgress for Progress {
    part fill { background: resource(Accent); }
}
```

## Apply the declarations

Add `style: HeadingText` to the heading's `Text` node.
Add `style: ApplyAction` to Apply and `style: CompletionToggle` to the toggle.
Use this Progress node:

```xui
Content(Completion, column: 1, style: CompletionProgress);
```

`Text` maps to the native Label target.
`Content` uses the actual target of its supplied element, here Progress.
The compiler accepts declarations for more targets than it can directly construct.
That support does not add `Progress(...)` to the markup grammar.

The indicator and mark are named visual parts, not child controls.
They have no separate event handlers or native windows.
Part names, properties, and state-rule support come from the exported schema.
A property supported on one part can be invalid on another.

## Understand sparse values

An omitted property remains unset.
An explicit zero, black color, or empty border is a value.
Style replacement preserves local overrides.
Removing a style does not clear those overrides.

For example, this local padding overrides the declared padding:

```xui
Button("Apply", click: Apply, id: "apply",
    style: ApplyAction, padding: (24, 8, 24, 8));
```

Remove that local binding to return to the style value.
Adding or removing a binding is a structural development edit.
Changing the value of an existing binding can refresh in place.
The handwritten APIs also provide explicit local-value clearing.

Root state rules and part state rules use the model's actual states.
Do not infer a selector from a property name.
For example, a ContentDialog facade has a Popup root, not a dialog-specific invalid root state.
Use its retained validation child for status presentation.

## Select an appearance

Before creating the component, choose the window appearance in C#:

```csharp
window.SetVisualStyle(VisualStyle.WinUI);
window.SetTheme(Theme.Light);
```

Classic remains the default visual style.
Theme-aware colors retain both light and dark values.
The renderer chooses the active value; the compiler does not freeze it at startup.

High contrast can suppress authored face colors and outline geometry.
It keeps the system palette and visible outlines according to each control's contract.
Keyboard focus must remain visible independently of decorative styling.
Do not rely on color alone to communicate completion: this card also uses a toggle and status text.

There are no control templates, item templates, implicit selectors, or custom Shell-menu styling.
See the [style contract](../control-styling.md) and [inventory](../control-styling-inventory.md) for exact limits.

Continue with the separate [collections exercise](06-collections-and-work.md), or go directly to [reload and diagnostics](07-reload-and-errors.md).
