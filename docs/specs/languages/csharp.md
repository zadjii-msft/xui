# Handwritten C# applications

The `Xui` namespace supplies window factories, typed controls, properties, and events.
The managed assembly is `Xui.Managed.dll`.
It calls the native `xui.dll` through the C ABI.
It does not contain another renderer.

Handwritten C# does not require `.xui` files or the declarative compiler.
This path suits applications that construct their fixed control tree directly.

## A minimal application

This complete `Program.cs` uses an STA entry point.
The text field reports committed text through `Changed`.
The Apply button and Enter key update the label.

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
            using var window = new Window("C# application", 480, 280);
            var root = window.Stack().Padding(20).Spacing(12);
            var status = window.Label("Ready").SetAutomationId("status");
            var input = window.TextInput("Workspace name")
                .SetAutomationId("workspace");
            var apply = window.Button("Apply").SetAutomationId("apply");

            input.Changed += text => status.Text = $"Editing: {text}";
            input.Submitted += () => status.Text = $"Submitted: {input.Text}";
            apply.Click += () => status.Text = $"Applied: {input.Text}";

            root.Add(status).Add(input).Add(apply);
            window.SetContent(root);
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

The managed window remains alive throughout `Run`.
The `using` declaration disposes it after the run returns, including error paths.

## Project integration

The current repository targets .NET 10.
An application references [`Xui.csproj`](../../../bindings/dotnet/Xui/Xui.csproj).
It needs the native-control manifest and a matching native DLL.
[`Sample.csproj`](../../../bindings/dotnet/Sample/Sample.csproj) shows the handwritten sample's configuration.

The [tutorials](../tutorials/README.md) describe application project integration.
The [managed build procedure](../../../CONTRIBUTING.md#c-and-declarative-samples) contains build and run commands.
The apphost executable applies the native-control manifest.
Direct `dotnet Application.dll` execution does not apply that apphost manifest.

## Controls, layout, and events

Window factories create controls in the correct native owner.
Control constructors are not public application entry points.
For example, `window.Button("Apply")` creates a Button, not `new Button("Apply")`.

| Task | C# API |
| --- | --- |
| Create a horizontal row | `window.Stack(Axis.Horizontal)` |
| Share remaining space | `root.Add(child, flex: 1)` |
| Retain a scrolling subtree | `window.ScrollView(content, "Preferences")` |
| Set dimensions | `FixedSize`, `PreferredSize`, `MinimumSize`, `MaximumSize` |
| Handle a button | `Button.Click` |
| Handle a checkbox | `Toggle.Changed`, with a `bool` value |
| Handle tri-state input | `CheckBox.Changed`, with a `CheckState` value |
| Handle a link-shaped action | `HyperlinkButton.Click`, without automatic URI navigation |
| Select a horizontal choice | `SelectorBar.SetItems`, `SetSelected`, and `Changed` |
| Show a badge | `InfoBadge.SetDot`, `SetCount`, or `SetIcon` |
| Show submenu headings | `MenuBar.SetCommands`, `Invoked`, and `Pinned` |
| Handle a switch preference | `ToggleSwitch.Changed`, with a `bool` value |
| Handle a toggle action | `ToggleButton.Toggled`, with a `bool` value |
| Show a progress ring | `window.ProgressRing`, with `SetRange`, `SetValue`, and `SetState` |
| Handle text | `TextInput.Changed` and `TextInput.Submitted` |
| Handle other control events | `Control.Event`, with a `UiEvent` |
| Apply several ordinary properties | `Window.Update` |
| Request closure | `Window.Close` |

Fluent size and control methods preserve the concrete control type.
Writable properties retain normal assignment syntax.
For example, `range.Value = 25` and `range.SetValue(25)` call the same setter.
Setters retain native validation and thread rules.

The feature binding supplies typed collections, documents, dialogs, scenes, and native hosts.
The [coverage table](../bindings.md#current-coverage) lists the available families.
The [feature sample](../../../bindings/dotnet/Sample/FeatureDemo.cs) shows ranges, a map, a dialog, and an immutable source.

Coverage does not imply complete C++ method parity.
For example, DataGrid filter and sort state do not perform application data work automatically.
The application supplies the corresponding source snapshot.
The [advanced gaps](../bindings.md#advanced-api-gaps) also identify absent provider and completion APIs.

## Styles

The Window constructor accepts `visualStyle: VisualStyle.WinUI` for the optional WinUI-style appearance.
`ControlStyle`, `PartStyleValues`, `PartStyle`, and `ControlStyleRule` provide shared named-part definitions.
The separate `ButtonStyle` API remains available.

The [Toggle style examples](../control-styling.md#toggle-pilot) include C# declarations and attachment.
The [binding style examples](../bindings.md#binding-examples) describe Button definitions and local values.
The [retained-child contract](../bindings.md#retained-composition-children) identifies real child controls inside compositions.
For example, a ContentDialog root uses the Popup target, while its Primary button uses Button.

The wrappers share immutable definitions without retaining windows through style caches.
Applied controls retain their native definitions.
Styles do not replace native editing or expose arbitrary templates.

## Lifetime, threads, and errors

The window owns the native arena.
Managed controls do not have independent destruction.
Access after window disposal throws `ObjectDisposedException`.
The window has no finalizer that can perform UI-thread destruction.
Explicit disposal is required.

All UI calls and source callbacks require the creating thread.
The binding does not capture a synchronization context.
An `async` continuation does not automatically return to the XUI thread.
Worker results require an application-owned UI queue and UI-thread delivery.

A callback can call `Close`, but cannot dispose an active window.
Disposal during a callback or `Run` reports a busy error.
The callback trampoline catches managed exceptions before they cross the native boundary.
Native failures become `XuiException`, with a numeric `Status`.
Callback failures retain the managed cause through the exception's inner error.

Source snapshots, request objects, and script results have their own disposal contracts.
Password access uses the scoped `WithPassword` callback, not an ordinary plaintext property.
The [binding lifetime reference](../bindings.md#ownership-and-data-limits) describes cancellation and borrowed data.

## Deployment and next steps

Framework-dependent applications need their managed output, the matching .NET runtime, and `xui.dll`.
NativeAOT applications still need `xui.dll`.
The process and native DLL must both use x64 or both use ARM64.
The managed assembly name avoids a loader collision with the native DLL.

The [NativeAOT and deployment procedure](../../../CONTRIBUTING.md#nativeaot-and-deployment) contains publish commands.

- [Tutorials](../tutorials/README.md)
- [Control catalog](../controls/README.md)
- [Declarative XUI and C#](declarative.md)
- [Managed ownership contract](../bindings.md#managed-ownership-and-use)
- [Language comparison](README.md)
