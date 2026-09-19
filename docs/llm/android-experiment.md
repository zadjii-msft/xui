# Android experiment handoff

The [public Android contract](../specs/experimental-android.md) describes native mapping and lifetime.
The [contributor procedures](../../CONTRIBUTING.md#experimental-android-backend) contain build and device commands.
The implementation uses the shared foundation without runtime or generator changes.

## Source map

- `bindings/dotnet/Experimental/Xui.Android/AndroidBackend.cs`: Attachment, native peers, event suppression, accessible properties, and cleanup.
- `bindings/dotnet/Experimental/Xui.Android/AndroidDispatcher.cs`: Android main-looper dispatch.
- `bindings/dotnet/Experimental/Xui.Android/NativeLayout.cs`: Native stack measurement, element constraints, and disabled scroll input.
- `bindings/dotnet/Experimental/Xui.Android/LayoutMath.cs`: Production density conversion and bounded flex allocation.
- `bindings/dotnet/Experimental/AndroidDemo/MainActivity.cs`: Shared component startup, attachment lifetime, and saved state.
- `bindings/dotnet/Experimental/AndroidDeviceTests/TestActivity.cs`: Real-widget checks for authored events, text spans, scrolling, layout, dispatch, and teardown.
- `bindings/dotnet/Experimental/Xui.Android.LayoutTests`: SDK-only tests of the production arithmetic.
- `bindings/dotnet/Experimental/Xui.Android.ReferenceCheck`: Compile-only checks of the exact adapter, demo, and native test sources.

The native test app links the same `SharedDemo/Greeting.xui` as the demo.
Its additional layout fixture constructs runtime elements to exercise constraints.
It does not replace Android classes with mocks.
Its native text assignments exercise Android listeners, not physical keyboard or IME input.
The manual device procedure remains necessary.

## Evidence, September 19, 2026

The host is Windows ARM64 with .NET SDK 10.0.401.
The Android workload is absent.
The actual `AndroidDemo` build stops at `NETSDK1147`.
No APK, Java build, deployment, emulator, or device execution is established.
No workload installation, elevation, system configuration change, or license acceptance occurred in this session.

The configured NuGet mirror supplied `Microsoft.Android.Ref.36` version `36.1.69`.
The separate reference project compiles the production sources against `Mono.Android.dll` and `Java.Interop.dll`.
It does not import Android packaging targets or run Android code.
This check caught binding errors before delivery.
The final reference compilation completed with no warnings or errors.
The regular production projects still target `net10.0-android`.

The layout arithmetic tests passed 4,170 assertions.
The unchanged shared runtime and generated demo passed 77 assertions.
The generator regression suite passed 42,062 assertions.
The documentation check passed with 55 pages and 717 local links.
These results do not establish layout, focus, accessibility, rotation, or IME behavior on Android.
The native test application and manual smoke procedure remain the acceptance gates.

Existing repository release automation builds Windows artifacts.
No workflow or PR was published to claim Android CI execution.
The parent integration must keep the native execution gap explicit.

## Design notes

The backend uses native controls inside measurement wrappers.
The wrappers apply preferred and fixed sizes even for a root or scroll child.
The stack allocates finite main-axis space without Android's negative-weight shrink behavior.
The scroll viewport does not apply finite-height flex rules to its unbounded content.

Backend disposal removes only the mounted root.
Peer disposal unhooks listeners, removes native parent links, and releases only that peer's resources.
Partial construction failures also release already-created resources.
Cleanup failures remain visible as aggregate exceptions.

The Activity saves authored state rather than an Activity-bound host.
Ordinary state updates retain the existing EditText.
Explicit replacement clamps selection and can end composition.
Activity recreation restores text and selection, but not composition spans or scroll position.
