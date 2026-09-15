# Declarative XUI

The `.xui` language describes a retained control tree with C# state and behavior.
The compiler generates C# that uses the existing XUI bindings.
It does not add a runtime parser, virtual tree, or reconciler.

The initial implementation supports fixed compositions.
It does not support arbitrary dynamic children, custom row templates, or a complete styling language.
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
Arguments use C# expressions.

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
