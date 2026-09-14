# Declarative XUI

The `.xui` language describes a retained control tree with C# state and behavior.
The compiler generates C# that uses the existing XUI bindings.
It does not add a runtime parser, virtual tree, or reconciler.

The initial implementation supports fixed compositions.
It does not support arbitrary dynamic children, custom row templates, or a complete styling language.
The [engineering plan](xui-language-plan.md) defines the implementation and acceptance checks.

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

The initial control names are `VStack`, `HStack`, `Text`, `Button`, `Toggle`, and `TextInput`.
The `id` argument supplies a control's automation ID.
Stacks support `spacing` and `padding`.
Control arguments use C# expressions.

`Text`, `Button`, and `Toggle` use their positional string for both text and the accessible name.
They do not accept a separate `name` argument because those native properties share storage.
`TextInput` has a separate accessible name and text value.
Its `text`, `change`, and `submit` arguments configure that input.
`Toggle` supports `checked` and `change`.
Event arguments name C# methods.

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

The component constructor creates its controls and installs the root content.
All control access uses the creating UI thread.
The development host does not change that rule.

## Run the sample

The commands use Windows ARM64.
They require the .NET 10 SDK and Visual Studio C++ build tools.
The `cmake` command must be available in the shell.

1. Build the native library:

   ```powershell
   cmake -S . -B build\xui-language -A ARM64
   cmake --build build\xui-language --config Release --target xui
   ```

2. Add the library directory to this shell's DLL search path:

   ```powershell
   $env:PATH = (Resolve-Path build\xui-language\Release).Path + ";" + $env:PATH
   ```

3. Run the sample with the watcher:

   ```powershell
   dotnet watch --project bindings\dotnet\DeclarativeSample\DeclarativeSample.csproj --non-interactive
   ```

4. Edit `bindings\dotnet\DeclarativeSample\Counter.xui`.

The watcher observes `.xui` inputs.
Generated files do not require manual edits.
The native library does not rebuild for each `.xui` change.
The application uses its generated executable, which contains the native-control manifest.
Running its `.dll` through `dotnet` directly does not apply that executable manifest.

If you change the compiler itself, stop the watcher before you rebuild the compiler.
The watcher can hold the analyzer assembly open on Windows.

For restart-on-save without .NET Hot Reload, use this command:

```powershell
dotnet watch --project bindings\dotnet\DeclarativeSample\DeclarativeSample.csproj --no-hot-reload
```

## Understand reload behavior

Authored text and supported property expressions can update existing controls.
Stack spacing and padding can also update in place.
Supported C# handler-body edits affect later events.
The development host applies refreshes on the UI thread.

In-place refresh preserves component state.
It does not attach each event handler again.
Unchanged authored input values do not overwrite user edits.

A structural edit changes the control types or their parent-child relationships.
A source edit to the state schema, an initializer, or an explicit automation-ID expression also requires replacement.
Adding or deleting an event subscription requires replacement.
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

1. Publish the sample:

   ```powershell
   dotnet publish bindings\dotnet\DeclarativeSample\DeclarativeSample.csproj -c Release -p:PublishAot=true
   ```

2. Copy the native XUI library beside the executable:

   ```powershell
   Copy-Item build\xui-language\Release\xui.dll bindings\dotnet\DeclarativeSample\bin\Release\net10.0\win-arm64\publish\
   ```

The published executable does not use the development reload host.
The executable and `xui.dll` must use the same architecture.

## Install VS Code syntax support

The extension is in `integrations\vscode-xui`.
Its [README](../integrations/vscode-xui/README.md) contains the package and installation commands.

The extension supplies `.xui` highlighting, embedded C# highlighting, brackets, comments, and snippets.
It is a syntax package.
It does not provide a language server, semantic completion, or a visual designer.

## Run the integration checks

The native probe reads controls through UI Automation.
It targets the fixture's process ID.
It does not move the pointer or activate another application.

1. Build the native library and probe:

   ```powershell
   cmake --build build\xui-language --config Release --target xui xui_language_probe
   ```

2. Run the edit-loop checks:

   ```powershell
   .\tests\xui-language.ps1
   ```

The script creates an isolated fixture under `build\xui-language-check`.
It starts `dotnet watch` and changes the fixture, not the tracked sample.
It checks text, layout, input preservation, event behavior, invalid-edit recovery, and structural fallback.
It also checks input addition, renaming, deletion, no-op builds, and release references.
Logs and edit durations remain in the fixture directory.
The script stops the processes that it starts.

The compiler and MSBuild checks do not open a native window:

```powershell
dotnet run --project bindings\dotnet\GeneratorTests\GeneratorTests.csproj -c Release
.\bindings\dotnet\GeneratorTests\BuildTests.ps1
```

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
