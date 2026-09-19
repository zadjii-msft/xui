# Portable foundation handoff

The [public contract](../specs/experimental-portable-xui.md) defines the experimental API and its limits.
The [contributor procedures](../../CONTRIBUTING.md#experimental-portable-foundation) contain build commands.
The foundation does not include Android or DOM implementations.

## Source map

- `bindings/dotnet/Xui.Generator/XuiGenerator.cs`: Profile selection, portable subset rejection, and shared emission.
- `bindings/dotnet/Xui.Generator/Parser.cs`: The unchanged parser and AST.
- `bindings/dotnet/Experimental/Xui.Portable.targets`: Repository-local portable project integration.
- `bindings/dotnet/Experimental/Xui.Portable/Contracts.cs`: Backend, peer, event, and dispatcher interfaces.
- `bindings/dotnet/Experimental/Xui.Portable/Elements.cs`: Typed retained element state and ownership.
- `bindings/dotnet/Experimental/Xui.Portable/Host.cs`: Build scopes, attachment, dispatch, event delivery, and cleanup.
- `bindings/dotnet/Experimental/SharedDemo/Greeting.xui`: The single shared application source.
- `bindings/dotnet/Experimental/WindowsDemo`: The existing Windows profile with that shared source.
- `bindings/dotnet/Experimental/Xui.Portable.Tests`: Actual generated demo execution against a recording backend.
- `bindings/dotnet/GeneratorTests/Program.Portable.cs`: Profile switching, source mapping, and diagnostic rejection.

## Backend work

Use separate platform project directories under `bindings/dotnet/Experimental`.
Import `Xui.Portable.targets`.
Link `SharedDemo/Greeting.xui` as an `AdditionalFiles` item.
Construct `PortableDemo.Greeting` with a platform-thread `Host`.
Keep the generated class in the platform assembly.

Implement `IBackend`, `IElementPeer`, and `IUiDispatcher` against the public runtime types.
Keep one native input widget for each `TextInput` throughout an attachment.
Remove native listeners during peer disposal.
Report callback and dispatch errors at the platform boundary.
Do not change the shared runtime to access internal state.

The host owns one component tree and one attachment at a time.
Platform recreation can detach and attach while the host remains on the same UI thread.
A replacement UI thread requires a new host.
The platform must dispose the host before it abandons the final UI surface.

DOM execution uses local .NET WebAssembly and JavaScript interop.
An application bootstrap can use Blazor WebAssembly without authored Razor UI.
Android execution uses .NET for Android and native widgets.
Neither backend translates authored C# into another language.

## Foundation evidence, September 19, 2026

The SDK-independent runtime and generated demo passed headless event, lifetime, ownership, and error checks on Windows ARM64.
The Windows sample compiled against the real managed Windows bindings without native DLL deployment.
The tests exercise native text edits without writeback, silent setters, stable peers, stale events, and failed cleanup.
These results do not establish Android or browser execution.
The commit handoff records final assertion counts and command results.
