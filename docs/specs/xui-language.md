# Declarative XUI

The `.xui` language describes a retained control tree with C# state and behavior.
The compiler generates C# that uses the existing XUI bindings.
It does not add a runtime parser, virtual tree, or reconciler.

The implementation supports fixed compositions.
It supports named control styles, named visual parts, state rules, typography, and color resources.
The [control inventory](control-styling-inventory.md) describes the available presentation surfaces.
It does not support arbitrary dynamic children, control templates, or custom row templates.
The [engineering plan](../llm/xui-language-plan.md) defines the implementation and acceptance checks.

## Author a component

```text
namespace Demo;

component Counter {
    state int Count = 0;

    view {
        VStack(spacing: 8, padding: 16) {
            Text($"Count: {Count}", id: "count");
            Button("Increment", click: Increment, id: "increment");
        }
    }

    code csharp {
        void Increment() => Count++;
    }
}
```

The namespace is optional.
Each file declares one component.
A component produces a C# class.
The `view` block describes the native tree.
The `code csharp` block supplies C# methods.
Fields belong in `state` declarations.

The native node names are `VStack`, `HStack`, `Text`, `Button`, `Toggle`, `TextInput`, `Grid`, `DataGrid`, `NavigationView`, `ItemsView`, `ScrollView`, `Popup`, and `SplitView`.
`Content` embeds an existing element.
Stacks have no positional argument.
Each other native node requires a string argument.
Most arguments use C# expressions.
Style arguments use the bounded style syntax described below.

All nodes support `ref: Identifier`, `size: (width, height)`, and `preferredSize: (width, height)`.
Size values use DIPs.
`ref` produces a typed public property for the native element.
Reference names cannot conflict with parameters, state, methods, the component name, or `Root`.
Names that start with `__xui` are reserved.

Stacks support `spacing` and uniform `padding`.
Controls support `id`, `enabled`, `visible`, and `help`.
The `id` argument supplies the automation ID.
`help` supplies native help text.
`Stack`, `Grid`, and `Content` are elements, not controls, so they do not support those four arguments.
For example, `Button("?", size: (36, 36), help: "Row 1, column 1: covered.");` declares a square cell.

`Text`, `Button`, and `Toggle` use their positional string for both text and the accessible name.
They do not accept a separate `name` argument because those native properties share storage.
`TextInput` has a separate accessible name and text value.
Its `text`, `change`, and `submit` arguments configure that input.
It also supports `captionVisible` and `placeholder`.
`Toggle` supports `checked` and `change`.
Event arguments name C# methods.
`Button` supports `icon: global::Xui.ButtonIcon.Refresh` through the native `SetIcon` method.
`NavigationView` supports `headerVisible`.
Its `searchId` and `searchHelp` arguments configure the native search input.
`SplitView` supports `secondVisible`.
`Popup` supports `placement: global::Xui.PopupPlacement.Right` and `windowBackground`.
`DataGrid` accepts a `global::Xui.GridColumn[]` expression in `columns`.
The compiler calls `SetColumns` when the authored column values change.

<a id="declare-button-styles-and-resources"></a>

## Declare control styles and resources

Resources and named styles belong directly inside a component:

```text
component DangerActions {
    resources {
        DangerFill: theme(light: 0xB42318, dark: 0x8F1D16);
        OnDangerFill: 0xFFFFFF;
        DangerEdge: theme(light: 0x68110C, dark: 0xFFA198);
        DangerHover: theme(light: 0xD92D20, dark: 0xB42318);
        DisabledFill: theme(light: 0xD0D5DD, dark: 0x475467);
        DestructiveFill: resource(DangerFill);
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
        VStack() {
            Button("Delete", style: DangerButton);
        }
    }
}
```

The sample `DeclarativeSample\Counter.xui` includes this Delete button beside the existing counter controls.
The button demonstrates appearance and does not delete data.

### Grammar

The following grammar describes the style structure.
The exported catalog restricts each target, part, property, state, and value combination.
`Identifier` uses the same identifier syntax as other XUI names.
Brackets mark optional syntax, and braces after `=` mark repetition.
Quoted braces are literal delimiters.

```text
Resources   = "resources" "{" { Identifier ":" Color ";" } "}"
Style       = "style" Identifier "for" Target [ "basedOn" Identifier ]
              "{" { Property | Rule | Part } "}"
Part        = "part" PartName "{" { Property | Rule } "}"
Rule        = "when" State "{" { Property } "}"
Property    = PropertyName ":" Value ";"
Color       = Rgb24
            | "theme" "(" "light" ":" Rgb24 "," "dark" ":" Rgb24 ")"
            | "resource" "(" Identifier ")"
Insets      = Dimension | "(" Dimension "," Dimension "," Dimension "," Dimension ")"
```

`Target`, `PartName`, `State`, and `PropertyName` must match the supported catalog names.
`Value` must match the property's type and the part's limits.
Targets use case-sensitive PascalCase names, such as `TextInput`, `ItemsView`, and `NavigationView`.
Part, property, and state names use lower camel case.
The target aliases are `Text` for `Label` and `VStack` or `HStack` for `Stack`.
The [style contract](control-styling.md) describes shared behavior and target-specific boundaries.
Root and part rules accept only states that the corresponding native model supplies.
Support for a property and a state does not imply support for that property inside the state rule.
For example, `ItemsView` accepts base `tile.width`, but rejects `tile.width` in every state rule.
Parts cannot contain other parts.
State blocks cannot contain parts.
Generic styles reject duplicate parts and duplicate state blocks within one part.
The root is implicit, so `part root` is invalid.
Base and derived styles must target the same control type.
Per-part local values use the C++, C ABI, C#, or Rust setter.
Style-value and part-rule edits update the method-body revision for hot reload.
They preserve the existing control tree.

The compiler accepts the Element-applicable targets in the [exported catalog](../../bindings/control_style_catalog.json).
Tooltip declarations reject because tooltips require the Window API.
`ContentDialog`, `CommandSurface`, `LocationPicker`, and `ViewPicker` are facades, not style targets.
Their root styles use `Popup`.
Retained children use their actual control targets.

### Property values

The catalog determines which properties each target and part accept.
This vocabulary does not imply that every property applies to every part.

| Properties | Value syntax |
| --- | --- |
| `background`, `foreground`, `borderBrush` | RGB24 integer, `resource(Name)`, or `theme(light: RGB24, dark: RGB24)` |
| `padding`, `borderThickness` | One dimension or four dimensions in left, top, right, bottom order |
| `fontFamily` | Nonempty C# string literal with valid Unicode and no NUL |
| `fontSize` | Positive numeric literal, limited by the part |
| `fontWeight` | Integer literal from 1 through 999 |
| `fontStyle` | Bare `normal`, `italic`, or `oblique`, limited by the part |
| `horizontalAlignment`, `verticalAlignment` | Bare `start`, `center`, `end`, or `stretch`, limited by the part |
| `wrapping` | `true` or `false` |
| `maximumLines` | Integer literal from 0 through 32768 |
| `cornerRadius`, `size`, `spacing`, `headerHeight`, `indentation`, `thickness`, `width`, `height`, `rowGap`, `columnGap` | Dimension |
| `rowHeight` | Positive dimension |

Font families have a maximum length of 1024 UTF-8 bytes and must also fit the part's UTF-16 limit.
Native text parts restrict font size to 512 DIPs and family length to 31 UTF-16 code units.
Those native parts accept normal or italic fonts, not oblique.
Paragraph parts reject vertical stretch.
Supported layout parts can accept stretch.
Enum values are unquoted names, not strings or C# enum expressions.

`Rgb24` is an integer literal from `0x000000` through `0xFFFFFF`.
Hexadecimal values use `0xRRGGBB`, not alpha or COLORREF byte order.
Decimal and other C# integer literal forms are also valid within that range.
`Dimension` is a finite numeric literal from 0 through 32768 DIPs.
Negative values, arithmetic expressions, state references, and method calls are not style values.
Insets use left, top, right, bottom order.
A single dimension applies to all four edges.
Parenthesized expressions, named tuple elements, and two-value inset shorthand are not supported.

The `theme(light: ..., dark: ...)` order is fixed.
This explicit pair keeps light and dark values together without separate theme blocks or ambiguous override order.
It also permits each resource alias to identify one complete color.
The generator preserves both colors in `ThemeColor`.
The native renderer selects the active theme at paint time.
The generator does not read the current theme or freeze the color during construction.

All `resources` blocks in one component form one color scope.
Resource names are case-sensitive and must be unique within that scope.
Aliases and style properties can refer to resources declared later in the component.
The compiler checks every resource, including unused resources.
Cross-component resources, mutable resource dictionaries, and other resource types are not supported.

### Sparse values, derivation, and local properties

A style sets only its declared properties.
An omitted property remains absent, not zero.
Explicit zero therefore differs from an omitted corner radius, inset, or black color.
State rules also contain sparse values.
Repeated properties within the same style body or rule are errors.
Legacy Button declarations permit repeated state blocks and retain their declaration order.
Generic declarations reject duplicate state blocks.
Rules cannot contain other rules.

A derived style names its base after `for Button`:

```text
style CompactDanger for Button basedOn DangerButton {
    padding: (8, 2, 8, 2);
    when pressed { background: 0x68110C; }
}
```

Base styles can appear later in the component.
Style names must be unique and cannot conflict with generated members, state, parameters, references, or methods.
Resources and styles have separate name scopes.
The compiler rejects missing base styles and inheritance cycles.

Native nodes accept a declared style name in `style: Identifier`.
The supported nodes are `VStack`, `HStack`, `Text`, `Button`, `Toggle`, `TextInput`, `Grid`, `DataGrid`, `NavigationView`, `ItemsView`, `ScrollView`, `Popup`, `SplitView`, and `Content`.
The style target must match the node.
Other supported targets use `Content(existingElement, style: NamedStyle)`, without a new constructor syntax.
Native attachment checks the actual target of that existing element.
`Content` requires a named style before it accepts local style properties.
Legacy Button styles require a `Button` node, not `Content`.

Style names are XUI references, not fields available inside `code csharp`.
A Button also accepts the six foundation properties directly as named arguments:

```text
Button("Delete", style: DangerButton, padding: (8, 2, 8, 2), cornerRadius: 0);
Button("Local only", background: resource(DangerFill), foreground: 0xFFFFFF);
```

Local properties use the same constant syntax as style properties.
Legacy Button properties produce sparse `Button.StyleValues`, separate from `Button.Style`.
Parts or extended properties select generic `ControlStyle` definitions.
Generic local properties use the root part's supported property set.
The native engine resolves local overrides, state rules, base styles, and defaults.

The node argument `size: (width, height)` remains structural size, not the scalar style property `size`.
Button scalar `size` belongs only to its supported icon and arrow parts.
Stack node arguments `padding` and `spacing` remain structural float setters and override style values.
Omitted Stack arguments leave style values available.
Explicit zero overrides them.
Padding inside a style declaration still accepts four-edge insets.

```xui
style Heading for Label {
    foreground: theme(light: 0x202020, dark: 0xEEEEEE);
    fontFamily: "Segoe UI";
    fontSize: 20;
    fontWeight: 600;
    wrapping: true;
    maximumLines: 2;
}
style Panel for Stack { padding: 12; spacing: 8; }
```

`Text("Title", style: Heading)` and `VStack(style: Panel)` apply these declarations inside the component's view.
These declarations do not replace the controls or their input behavior.

The [style contract](control-styling.md) describes that precedence and the native rendering boundaries.
This stage does not change control ownership, events, keyboard behavior, accessibility, or the control tree.
It does not implement `ItemTemplate`, control templates, implicit styles, arbitrary selectors, or state expressions.

### Bounds and diagnostics

A component supports at most 256 color resources.
A style supports at most 256 rules.
Style inheritance supports at most 16 layers, including the applied style.
Resource aliases can traverse the bounded component scope but cannot form a cycle.
The compiler checks unused styles and resources as well as referenced declarations.

Invalid declarations produce `XUI001` at the `.xui` source location and a mapped C# error that blocks the build.
These errors include duplicate names, missing references, cycles, unsupported targets, properties, states, invalid literal types, and values outside the limits.
Invalid inset lengths, duplicate properties, and excess resources, rules, or inheritance layers also block the build.
General control expressions outside the style subset retain their existing C# diagnostics.

### Shared definitions and reload

The generated class shares immutable style definitions across its component instances.
The managed binding creates native handles for each owning window.
Components without named styles do not allocate a style cache.

Edits to existing colors, aliases, properties, state rules, base-style references, and applied style references can update existing controls.
The generated cache checks a revision from a method body during refresh.
A changed revision creates new shared definitions and applies them to the existing elements.
Unchanged definitions do not trigger another native style assignment.
Local value edits update the existing local override binding.
These refreshes preserve component state, input state, event subscriptions, and control ownership.

Resource and style declaration names and order belong to the structural signature.
Adding, deleting, renaming, or reordering those declarations requires replacement.
Adding or removing a style binding or local property also requires replacement.
This prevents a removed property from remaining on a retained element.
The general runtime restrictions in [reload behavior](#understand-reload-behavior) still apply.

## Reuse a component

```text
component BrowserPane {
    param global::Xui.Element Files;
    param string Title;

    view {
        Grid(Title, ref: Layout,
            rows: [new(global::Xui.TrackSizing.Star, 1)],
            columns: [new(global::Xui.TrackSizing.Fixed, 200), new(global::Xui.TrackSizing.Star, 1)]) {
            NavigationView("Locations", ref: Navigation, headerVisible: false);
            ScrollView("Files", column: 1) {
                Content(Files);
            }
        }
    }
}
```

`param Type Name;` declares an immutable constructor input and a public read-only property.
Parameters have no initializer.
The constructor accepts `Window window`, the parameters in declaration order, and `bool attach = true`.
The parameter names `window` and `attach` are reserved.
Every component exposes its typed native root as `Root`.

```csharp
var pane = new BrowserPane(window, files, "Browser", attach: false);
var root = window.Stack().Add(pane.Root, flex: 1);
window.SetContent(root);
```

With `attach: false`, the constructor creates the tree without calling `SetContent`.
With `attach: true`, the root must be `VStack` or `HStack`.
A different root causes an `ArgumentException` before the constructor creates controls.
Existing calls such as `new Counter(window)` still attach their Stack root.

`Content(expression);` uses the supplied `Element` without creating another native element.
The expression runs once during construction and cannot depend on state.
The native bindings reject content from another window or content that already has a parent.
The caller must keep constructor-supplied element identities fixed for the component lifetime.

`Grid` accepts `global::Xui.GridTrack[]` expressions in `rows` and `columns`.
Each omitted argument defaults to one star track.
The compiler installs tracks before it adds children.
Track expressions can depend on state.
The positional Grid name is a constructor input and cannot depend on state.
Unlike controls, the managed Grid has no name setter.

A direct Grid child supports integer `row`, `column`, `rowSpan`, and `columnSpan` arguments.
Rows and columns start at zero.
Spans default to one and must be positive.
The native Grid rejects cells outside its tracks.
A direct Stack child supports `flex: float`.
Placement and flex cannot depend on state because the bindings expose no placement update method.
`Content` also accepts these parent-placement arguments.

`Grid`, `VStack`, and `HStack` use braces for their children.
`ScrollView` and `Popup` require exactly one child inside braces.
`SplitView` requires exactly two children.
The compiler creates children before it calls factories that require those children.

## Bind state

The compiler generates a property for each `state` declaration.
A state change updates properties that directly depend on that state.
It does not reevaluate the entire view.
Initializers do not run again during an in-place reload.
Equal assignments do not update the controls.
Mutations inside a reference-type state value do not trigger updates.
Assigning a replacement state value can trigger an update.

Direct state references make dependencies explicit.
An arbitrary method can hide a state dependency from the compiler.
The first version rejects unsupported view expressions instead of adding runtime dependency discovery.
External helpers must be pure and receive their state dependencies as explicit arguments.
Pure methods on a state value, such as `Game.CellText(0)`, also expose that state dependency.
The compiler does not prove the purity of arbitrary external C# code.

## Configure a project

The current integration uses projects in this repository.
It is not a published compiler package.

1. Create a .NET 10 executable project.
2. Import `bindings\dotnet\Xui.Declarative.targets` after the project's property definitions.
3. Add the `.xui` files inside the project directory.
4. Create the component before `Window.Run`.

The following project uses a placeholder path to the repository:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <RuntimeIdentifier>win-arm64</RuntimeIdentifier>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
  </PropertyGroup>
  <Import Project="D:\src\xui\bindings\dotnet\Xui.Declarative.targets" />
</Project>
```

The targets include the compiler and managed binding references.
Debug builds also include the development host.
Release builds exclude that host.
The targets supply the native-control application manifest unless the project specifies another manifest.

The entry point can select the development host at compile time:

```csharp
using Xui;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
#if XUI_HOT_RELOAD
        Xui.Development.ReloadHost.Run(
            () => new Window("Counter", 480, 360),
            window => _ = new Demo.Counter(window));
#else
        using var window = new Window("Counter", 480, 360);
        _ = new Demo.Counter(window);
        window.Run();
#endif
    }
}
```

By default, the component constructor creates its controls and installs the Stack root content.
All control access uses the creating UI thread.
The development host does not change that rule.

## Run the sample

Use the [C# build and watch commands](../../CONTRIBUTING.md#c-and-declarative-samples).
Edit `bindings\dotnet\DeclarativeSample\Counter.xui` while the watcher runs.
The native library does not rebuild for each markup change.

## Understand reload behavior

Authored text and supported property expressions can update existing controls.
Stack spacing and padding can also update in place.
Existing `size` and `help` bindings can update in place.
Other supported property bindings, including tracks and columns, can also update in place.
Supported C# handler-body edits affect later events.
The development host applies refreshes on the UI thread.

In-place refresh preserves component state.
It does not attach each event handler again.
Unchanged authored input values do not overwrite user edits.
Unchanged authored arrays do not reset tracks or user-adjusted column widths.
The compiler compares array contents, not array identities.
Omitted control properties do not generate refresh setters.

A structural edit changes the control types or their parent-child relationships.
A source edit to the state schema, an initializer, or an explicit automation-ID expression also requires replacement.
Parameter declarations, reference names, placement, flex, Grid names, and Content expressions belong to the structural signature.
An edit to any of them requires replacement.
Adding or deleting an event subscription requires replacement.
Adding or removing an optional binding also requires replacement.
Changing the target method of an existing event subscription can update in place.
The development host reports window replacement and resets component state.
It closes the old window, waits for `Run` to return, and disposes the old owner.
Then it constructs a new window.

The .NET runtime can reject other code changes as unsupported live edits.
With `--non-interactive`, the watcher restarts the process for those edits.
Process restart also resets transient state.

Controls without explicit IDs use positional identity.
Reordering same-kind controls without IDs can retain native input state at the old positions.
Stable literal IDs distinguish stateful controls during source edits.

Recoverable property errors produce diagnostics without a partial update.
The old window remains usable for those errors.
An error that prevents generation of a component's declarations can cause the SDK to request a process restart.
A later valid edit recovers without deletion of generated files, but a restarted process loses transient state.

## Publish a native executable

Use the [NativeAOT instructions](../../CONTRIBUTING.md#nativeaot-and-deployment).
The published executable excludes the development reload host.
The executable and `xui.dll` must use the same architecture.

## Play Minesweeper

The [Minesweeper sample](../../bindings/dotnet/Minesweeper/README.md) uses `.xui` for a complete game interface.
It includes first-click safety, flood reveal, flags, win/loss states, and restart.
Its immutable C# model supplies values for a fixed native board.

With the native library on this shell's DLL search path, run:

```powershell
dotnet watch --project bindings\dotnet\Minesweeper\Minesweeper.csproj --non-interactive
```

## Install VS Code syntax support

The extension is in `integrations\vscode-xui`.
Its [README](../../integrations/vscode-xui/README.md) contains the package and installation commands.

The extension supplies `.xui` highlighting, embedded C# highlighting, brackets, comments, and snippets.
It is a syntax package.
It does not provide a language server, semantic completion, or a visual designer.

## Run the integration checks

Use the [compiler and integration commands](../../CONTRIBUTING.md#tests).
The [fixture reference](../llm/testing.md#declarative-integration-fixtures) describes the native probes and isolated edit loop.

## Performance boundary

The baseline is equivalent handwritten C# that uses the same managed bindings.
The bindings still allocate and marshal data for native calls.
The language does not remove those existing costs.

The ordinary state-update path uses generated, targeted property calls.
Development refresh tracking does not belong in the release application.
The release application does not load the compiler or a UI interpreter.

The managed Debug build supplies the development loop.
NativeAOT publishing is a separate release operation.
An incremental generator does not guarantee a fixed save-to-visible-change duration for every application.
