# Experimental portable XUI

The `Portable` generator profile compiles a small `.xui` subset into managed retained elements.
It shares the Windows parser, state dependency analysis, and C# emitter.
It does not port `xui_core`, replace `Xui.Managed`, or promise equivalent platform appearance.
Windows remains the default profile.

The runtime targets `net10.0` without native DLLs, platform SDKs, reflection, or runtime code generation.
The namespace is `Xui.Experimental.Portable`.
The runtime project is [Xui.Portable](../../bindings/dotnet/Experimental/Xui.Portable/Xui.Portable.csproj).
This experiment has no hot reload, dynamic child replacement, styles, or general control parity.

## Project integration

For a repository-local portable application, import `bindings/dotnet/Experimental/Xui.Portable.targets`.
This import sets `XuiGeneratorProfile=Portable` and references the generator and runtime.
It includes local `.xui` files and exposes the profile to Roslyn.
It rejects `XuiHotReload=true`.
An absent profile, or `Windows`, preserves existing Windows output.
An unknown profile produces `XUI001`.

The following project fragment links the shared demo from a sibling experimental project:

```xml
<Import Project="..\Xui.Portable.targets" />
<ItemGroup>
  <AdditionalFiles Include="..\SharedDemo\Greeting.xui" Link="Greeting.xui" />
</ItemGroup>
```

The [shared demo](../../bindings/dotnet/Experimental/SharedDemo/Greeting.xui) contains all UI and application behavior.
The portable tests and [Windows demo](../../bindings/dotnet/Experimental/WindowsDemo/WindowsDemo.csproj) compile that exact file.
The Windows demo uses the existing Windows generator profile and native bindings.
Platform hosts supply only startup, dispatch, and widget adapters.
Build procedures are in [CONTRIBUTING](../../CONTRIBUTING.md#experimental-portable-foundation).

Authored `code csharp` stays real compiled C#.
The profile does not rewrite authored types, expressions, or strings.
Portable applications must use APIs available on their target.
Browser applications run this C# locally through .NET WebAssembly, not a server event connection.
Android applications run this C# through .NET for Android.

## Supported language

The root must be `VStack` or `HStack`.
Children form one fixed tree.
All elements accept `ref`, `size`, `preferredSize`, and `flex`.
`flex` requires a direct stack parent and cannot depend on state.
References expose the corresponding runtime type.

Stacks accept `spacing` and uniform `padding`.
They do not accept `id`, `enabled`, `visible`, or `help`.
`VStack` produces `Stack` with `Axis.Vertical`.
`HStack` produces `Stack` with `Axis.Horizontal`.

`Text` produces `Label`.
Its positional string supplies both text and accessible name.
`Button` uses the same name rule and accepts `click`.
These controls do not accept an independent accessible-name argument.

`TextInput` accepts `name`, `text`, `change`, `submit`, `captionVisible`, and `placeholder`.
Its positional string supplies the accessible name unless `name` overrides it.
The default text and placeholder are empty.
The default `captionVisible` value is `true`.
Hiding the caption does not remove the accessible name.
The input is single-line.

`ScrollView` requires one child and a positional accessible name.
It provides vertical scrolling, a bounded content width, and an unbounded content height.
It does not provide horizontal scrolling.

`Text`, `Button`, `TextInput`, and `ScrollView` also accept `id`, `enabled`, `visible`, and `help`.
They default to enabled and visible, with empty identity and help strings.
`id` supplies the automation identity, not a process-global object key.
The backend must scope external identifiers to its host.
Duplicate IDs do not replace or merge elements.

Events name C# methods.
`click` and `submit` handlers take no arguments.
`change` handlers take one `string` argument.
Parameters, state declarations, and state-driven expressions use the existing language rules.
View expressions cannot call component methods or contain assignments, increments, lambdas, or `await`.

Unsupported nodes, properties, styles, and resources produce source-located `XUI001` errors.
C# type and method errors retain source mapping.
There are no ignored portable properties.

## Values and layout

Lengths use logical units: Windows DIPs, Android dp, and CSS pixels.
Spacing, padding, flex weights, and size dimensions must be finite and nonnegative.
Invalid values throw before the corresponding element property changes.
Strings cannot be null or contain NUL.
Empty strings and Unicode text are supported.

The backend owns native measurement and arrangement.
A stack places children in authored order along its axis.
Padding applies to all four edges.
Spacing occurs between visible children, not outside the first and last child.
An invisible control occupies no layout space and accepts no input.

Without explicit sizing, controls use their native content size.
`preferredSize` supplies a desired size, including the stack padding.
`size` constrains both dimensions to the requested size, within the parent allocation.
`size` takes precedence over `preferredSize` when both exist.
No element forces overflow from a smaller parent allocation.

Non-flex children use their desired main-axis size first.
Positive flex weights divide the remaining main-axis space proportionally, after padding and spacing.
Cross-axis children stretch within their allocation unless `size` constrains that dimension.
In an unbounded main axis, flex children use their desired size.
Native fonts, control chrome, and text measurement can differ between backends.
This contract does not promise pixel parity with the Windows engine.

## Host ownership and thread access

An application creates `Host` with an `IUiDispatcher` on the target UI thread.
The generated constructor accepts that host, followed by declared component parameters.
The host owns exactly one component tree.
The generated constructor uses `BeginBuild`, `SetContent`, and `BuildScope.Complete`.
A failed constructor releases partial elements and leaves the host available for another construction attempt.

```csharp
using var host = new Xui.Experimental.Portable.Host(dispatcher);
var component = new PortableDemo.Greeting(host);
host.Attach(backend);
```

The application owns the host lifetime.
The generated component does not own a native window and does not implement `IDisposable`.
`Host.Dispose` releases the attachment and all managed event handlers.
It is terminal and idempotent on the UI thread.
State and element access after disposal throws `ObjectDisposedException`.

The tree rejects cross-host children, duplicate parents, cycles, orphan elements, and structural changes after construction.
The element hierarchy exposes `Kind`, `Parent`, `Children`, `Flex`, `FixedSize`, and `PreferredSize`.
Concrete types expose their current control and layout properties.
The backend needs no internal fields.

`IUiDispatcher.CheckAccess` reports UI-thread access.
`Post` accepts an action exactly once, or throws if dispatch is unavailable.
The platform must execute accepted actions while the dispatcher remains alive.
`Host.DispatchAsync` can be called from any thread.
Its task reports dispatcher errors, callback errors, wrong-thread delivery, or host disposal.
Queued work cannot mutate a disposed host.
Disposal does not require the backend to drain a platform queue.

`VerifyAccess` guards reads.
`VerifyMutation` also rejects mutation during backend callbacks.
Generated state setters use `VerifyMutation`.
State refresh is synchronous and not transactional across several bindings.
A binding error can leave the authored state changed and earlier bindings applied.
The error propagates to the caller.

## Backend attachment contract

The public interfaces are in [Contracts.cs](../../bindings/dotnet/Experimental/Xui.Portable/Contracts.cs).
The lifecycle implementation is in [Host.cs](../../bindings/dotnet/Experimental/Xui.Portable/Host.cs).
`IBackend` creates peers, mounts the root, and releases its host surface.
`IElementPeer` adds native children, updates one property, and releases one peer.

Before `Attach` accepts a backend, the caller owns it.
Argument or host-state rejection does not dispose it.
Once creation starts, the host owns the backend and every returned peer.
Each element requires a distinct, non-null peer.
The backend must release partial native resources if its own `Create` call throws before it returns a peer.

Creation proceeds in parent-first tree order.
`Create` reads all current properties and creates an unmounted native element.
It can inspect the typed element throughout the attachment lifetime.
`AddChild` receives completed child peers in authored order.
`Mount` attaches the completed root to the platform surface.
Events remain inactive until `Mount` returns.

`Update(ElementProperty)` reads the new property from the same element.
It must update the existing peer, not replace the tree or input widget.
Label and button text changes use `ElementProperty.Name`.
Text input value changes use `ElementProperty.Text`.
An update must preserve focus, selection, and IME composition unless the changed property itself requires a native edit.
Programmatic input edits must not produce user change events.

Backend callbacks can read the model but cannot mutate it or reenter attachment operations.
The runtime rejects synchronous event echoes during `Create`, `AddChild`, `Mount`, and `Update`.
Backends must also suppress delayed native echoes themselves.

`Detach` invalidates all event sinks before native cleanup.
It first calls `IBackend.Dispose` to unmount the surface.
It then disposes peers in reverse creation order.
Backend disposal must not dispose the peers.
Peer disposal must remove native listeners and release interop handles.
Cleanup continues after errors and reports all failures through `AggregateException`.

Detach retains the model and authored handlers.
A later attachment creates new peers from current state.
Old event sinks never become active again.
An attachment failure releases the partial attachment and preserves the model.
An update failure retains the new model value, detaches the failed backend, and propagates the error.
An application can attach a new backend after it handles the error.
Disposal remains terminal even if native cleanup throws.

## Native event delivery

Each peer receives an `IControlEvents` sink.
Native callbacks call `Click`, `Change`, or `Submit` on the UI thread.
A button accepts only `Click`.
A text input accepts only `Change` and `Submit`.
Wrong event types throw while the sink is active.

`Change(text)` records the native input value before it calls authored handlers.
It does not write that value back to the peer.
An identical text value does not call the change handler again.
An authored handler can set another value explicitly.
Setters remain silent, including setters inside handlers.

Event methods return `false` for stale, detached, disposed, invisible, or disabled targets.
An invisible or disabled ancestor also rejects descendant events.
They return `true` for accepted events, including unchanged text.
Wrong-thread delivery throws.
Handler errors propagate to the native callback boundary.
The backend must report those errors explicitly, not discard them.

## Platform implementation boundary

The DOM backend maps stacks to real layout containers and controls to native DOM text, button, input, and scroll elements.
The Android backend maps stacks to native linear layouts and controls to native text, button, edit, and scroll widgets.
A text-input peer can own a wrapper for its caption and edit widget.
That wrapper remains one runtime element.
The platform adapter must preserve semantic labels, enabled state, keyboard input, and accessible identity.

Platform projects link the shared `.xui` file directly.
They must not copy its UI into Razor, JavaScript, Android XML, or hand-written C#.
Platform-specific startup, dispatch, lifecycle callbacks, and widget mapping remain outside the shared sample.
Actual browser and Android execution require separate platform implementation and acceptance checks.

The [Android adapter](experimental-android.md) implements this subset with native widgets and a shared-source Activity sample.
Its reference-only compilation and arithmetic tests do not establish Android device execution.

The [experimental DOM backend](experimental-dom-web.md) implements this subset with local .NET WebAssembly and native DOM controls.
Its public contract describes browser layout, event ordering, input identity, and page lifetime.
