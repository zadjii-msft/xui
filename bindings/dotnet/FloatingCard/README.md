# Floating C# card

An XUI card floats over a transparent, borderless top-level window. The desktop shows around its rounded corners; the empty area passes pointer input through. Drag the blank strip above the heading, type in the native editor, or close with the button. The sample's `NativeHwndCreated` callback makes the window topmost with Win32 `SetWindowPos`; XUI retains ownership of the HWND.

Build the native DLL as described in [CONTRIBUTING](../../../CONTRIBUTING.md#build-the-native-code), then run:

```powershell
dotnet run --project bindings\dotnet\FloatingCard\FloatingCard.csproj -c Release
dotnet run --project bindings\dotnet\FloatingCard\FloatingCard.csproj -c Release -- --smoke
```

`--smoke` paints a frame and checks transparent-corner hit testing (or the high-contrast solid fallback), the visible card, caption hit testing, native EDIT delivery, and stable borrowed HWND identity before closing. The card uses solid colors, not CmdPal's backdrop blur or compositor shadow. The [application window contract](../../../docs/specs/application.md#borrowed-hwnd-and-transparent-windows) defines hosting, ownership, and size limits.
