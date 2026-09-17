# Declarative XUI and C#

A `.xui` component describes a fixed native control tree.
The compiler generates C# that uses the existing managed wrappers.
C# supplies state and event handlers.
There is no runtime markup parser or second renderer.

The compiler does not generate C++ or Rust.
The `.xui` constructor vocabulary is smaller than the handwritten binding API.

## A complete component

This `Counter.xui` component contains state, a native input, a checkbox, and a button.
The event methods assign state through the generated properties.
The named style changes appearance without replacing the Button.

```xui
namespace Demo;

component Counter {
    state int Count = 0;
    state string Entry = "";
    state bool Active = true;

    resources {
        ActionFill: theme(light: 0x005FB8, dark: 0x004578);
    }

    style ActionButton for Button {
        background: resource(ActionFill);
        foreground: 0xFFFFFF;
        padding: (12, 6, 12, 6);
        when disabled { background: 0x555555; }
    }

    view {
        VStack(spacing: 12, padding: 20) {
            Text($"Count: {Count}", id: "count");
            TextInput("Workspace name", text: Entry,
                change: SetEntry, id: "workspace");
            Text($"Workspace: {Entry}", id: "echo");
            Toggle("Enable increment", checked: Active,
                change: SetActive, id: "active");
            Button("Increment", click: Increment, enabled: Active,
                style: ActionButton, id: "increment");
        }
    }

    code csharp {
        void Increment() => Count++;
        void SetEntry(string value) => Entry = value;
        void SetActive(bool value) => Active = value;
    }
}
```

This `Program.cs` creates the component and runs its window:

```csharp
using System;
using Xui;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        try
        {
            using var window = new Window("Declarative counter", 480, 360);
            _ = new Demo.Counter(window);
            window.Run();
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
```

The generated constructor attaches the Stack root by default.
It creates the controls before `Run`.
The generated bindings update properties that directly depend on the changed state.

## Project integration

The current integration uses repository projects, not a published compiler package.
[`Xui.Declarative.targets`](../../../bindings/dotnet/Xui.Declarative.targets) supplies the generator, managed binding references, and default application manifest.
Debug configuration also includes the development host unless hot reload is disabled.
Release configuration excludes that host.

The [tutorials](../tutorials/README.md) cover application project integration.
The [managed build and watch procedure](../../../CONTRIBUTING.md#c-and-declarative-samples) supplies the commands.
The example entry point uses an ordinary Window, not the development reload host.
The [reload-host entry point](../xui-language.md#configure-a-project) shows the development alternative.

## Supported node forms

There are twenty-four node forms, including the existing-element bridge:

| Form | Purpose |
| --- | --- |
| `VStack`, `HStack` | Vertical or horizontal Stack |
| `Text` | Label |
| `Button`, `Toggle`, `TextInput` | Actions, checked state, and native text |
| `ToggleSwitch`, `ToggleButton` | Switch preferences and button-shaped toggle actions |
| `CheckBox`, `HyperlinkButton`, `InfoBadge` | Tri-state input, callback-only links, and noninteractive badges |
| `SelectorBar`, `MenuBar` | Horizontal exclusive selection and command submenu headings |
| `RangeInput`, `Progress`, `ProgressRing` | Numeric input and read-only bar/ring progress |
| `Grid` | Tracks and cell placement |
| `DataGrid`, `NavigationView`, `ItemsView` | Bound collection and navigation controls |
| `ScrollView` | One retained subtree |
| `Popup` | One popup subtree |
| `SplitView` | Two retained subtrees |
| `Content` | An existing managed Element |

Stacks have no positional argument.
Other native constructors require a string argument.
`Content` instead accepts an existing Element expression.
ToggleSwitch and ToggleButton accept `checked` and a boolean `change` handler.
ProgressRing accepts `range`, `currentValue`, and `progressState`, like Progress.
ProgressRing defaults to indeterminate state, while Progress defaults to determinate state.
The [control guides](../controls/README.md) include examples for each presentation.
CheckBox uses `checkState`, `threeState`, and a CheckState `change` handler.
SelectorBar applies `items` and `selected` as one snapshot.
InfoBadge accepts `count` or `icon`, not both. Neither argument means a dot.
MenuBar uses `commands`, `invoke`, and `pin`.
The [language contract](../xui-language.md#checkbox-links-selectors-badges-and-menu-bars) defines these types and snapshot rules.

All nodes support `ref`, `size`, and `preferredSize`.
A reference creates a typed public property.
Control nodes also support `id`, `enabled`, `visible`, and `help`.
Element-only forms such as Stack, Grid, and Content do not accept those four control arguments.

Direct Stack children accept `flex`.
Direct Grid children accept `row`, `column`, `rowSpan`, and `columnSpan`.
These placement values cannot depend on state.
Grid tracks can depend on state.
The [composition contract](../xui-language.md#reuse-a-component) specifies child counts, placement, and ownership.

## Use a handwritten control

`Content` bridges the broader C# API without inventing new markup constructors.
This complete component accepts an existing RangeInput as an Element:

```xui
namespace Demo;

component VolumePanel {
    param global::Xui.Element Volume;

    style VolumeStyle for RangeInput {
        part track { background: theme(light: 0xCCCCCC, dark: 0x444444); }
    }

    view {
        VStack(spacing: 12, padding: 20) {
            Text("Volume");
            Content(Volume, style: VolumeStyle);
        }
    }
}
```

This application fragment replaces component creation in the earlier entry point:

```csharp
var volume = window.RangeInput("Volume")
    .SetRange(new(0, 100, 1, 10))
    .SetValue(25);
_ = new Demo.VolumePanel(window, volume);
```

`Content` evaluates its Element expression once during construction.
It cannot depend on state.
The element must belong to the same window and must not already have a parent.
An incompatible actual style target produces a native attachment error.

Components also accept immutable `param` values and expose their native `Root`.
`attach: false` permits composition inside another tree without a `SetContent` call.
A component with automatic attachment requires a Stack root.

## State and styles

Equal state assignments do not update controls.
Mutation inside a reference-type state value does not trigger an update.
Replacement of that state value can trigger an update.
External helpers need explicit state arguments because the compiler does not discover hidden dependencies.

The shared catalog contains 46 style targets and 223 target/part schemas.
That catalog does not add markup constructors.
Broader Element-applicable targets use `Content(existingElement, style: Name)`.
Tooltip styles use the Window API instead of declarative Tooltip declarations.
Facade roots such as ContentDialog use Popup styles.
Their retained children use their actual targets.

Style values use a bounded constant syntax.
Unsupported targets, parts, state rules, and values produce `XUI001`.
Style failures block compilation rather than silently discard properties.
There are no implicit styles, arbitrary selectors, control templates, or custom row templates.
Shell `HMENU` styling is not part of this model.

The [style grammar](../xui-language.md#declare-control-styles-and-resources) and [style inventory](../control-styling-inventory.md) define exact support.

## Reload, lifetime, and errors

Supported property edits and existing handler-body edits can refresh controls in place.
This refresh preserves state, native input state, and subscriptions.
Structural edits require window replacement or process restart.
Replacement resets transient component state.
Stable literal IDs help distinguish stateful controls across source edits.

All control access still belongs to the creating UI thread.
The development host does not add automatic worker dispatch.
Component-supplied elements retain fixed identities for the component lifetime.
C# callbacks retain the managed binding's exception and disposal rules.

The [reload contract](../xui-language.md#understand-reload-behavior) distinguishes supported refreshes from replacement.
The [C# ownership guide](csharp.md#lifetime-threads-and-errors) describes callback errors and window disposal.

## Deployment and next steps

The application uses the same native DLL and architecture rules as handwritten C#.
NativeAOT excludes the development host but still requires `xui.dll`.
The [publish procedure](../../../CONTRIBUTING.md#nativeaot-and-deployment) contains the deployment commands.

- [Tutorials](../tutorials/README.md)
- [Control catalog](../controls/README.md)
- [Full declarative language contract](../xui-language.md)
- [Handwritten C#](csharp.md)
- [Language comparison](README.md)
