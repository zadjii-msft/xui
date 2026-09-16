# 1. Your first window

This chapter creates a window and a generated component.
Keep `Window` creation and the message loop in C#.
Keep the retained layout in `.xui`.

## Define the project

Create `TaskCard.csproj`, `Program.cs`, and `TaskCard.xui` in one directory.
The [checked-in sample](sample/) already has these files.
Its relative import assumes that directory stays inside this repository.

For a project outside this repository, use this project file.
Replace the import path with your XUI checkout.
Choose `win-x64` instead of `win-arm64` for an x64 native DLL.

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

The import adds the managed bindings, source generator, and native-control manifest.
It includes `.xui` files as generator inputs.
Debug builds can also include the development host.
Do not edit the generated C# files.

## Create the entry point

Use this complete `Program.cs` for chapters 1 and 2:

```csharp
using Xui;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        using var window = new Window("Task card", 560, 440);
        _ = new Tutorial.TaskCard(window);
        window.Run();
    }
}
```

`using` disposes the window after `Run` returns.
The window owns the native controls created through its factories.
Do not dispose it from an event callback while its message loop runs.
Use `window.Close()` to request closure, then let `Run` return.

`STAThread` marks the entry thread's apartment.
It does not make controls safe to access from another thread.
All control calls belong on the creating UI thread.

## Describe the component

Use this complete `TaskCard.xui`:

```xui
namespace Tutorial;

component TaskCard {
    view {
        VStack(spacing: 12, padding: 20) {
            Text("My first task", id: "heading");
            Button("Apply", id: "apply");
        }
    }
}
```

The compiler creates `Tutorial.TaskCard`.
Its default constructor installs the Stack root in the supplied window.
The button has no callback yet.
Clicking it therefore does not change the application.

Sizes, spacing, and padding use device-independent pixels, or DIPs.
The Stack measures its children and arranges them vertically.
The literal IDs support automation and stable identity during development edits.
They are not CSS selectors.

## Check the result

Use the [sample build procedure](../../../CONTRIBUTING.md#gitbook-tutorial-sample) with your project path.
The window should show a heading and an Apply button.
Use Tab to reach the button and confirm that keyboard focus remains visible.
Close the window before rebuilding its native DLL.

If the application cannot load `xui.dll`, check its search path and architecture.
See [diagnostics](07-reload-and-errors.md) for other common failures.

Continue with [state and native input](02-state-and-input.md).
