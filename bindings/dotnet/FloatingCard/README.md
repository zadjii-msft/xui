# Floating C# card

An XUI card floats over a transparent, borderless top-level window. The desktop shows around its rounded corners; the empty area passes pointer input through. Drag the blank strip above the heading, type in the native editor, or close with the button. The sample's `NativeHwndCreated` callback makes the window topmost with Win32 `SetWindowPos`; XUI retains ownership of the HWND.

Build the native DLL as described in [CONTRIBUTING](../../../CONTRIBUTING.md#build-the-native-code), then run:

```powershell
dotnet run --project bindings\dotnet\FloatingCard\FloatingCard.csproj -c Release
dotnet run --project bindings\dotnet\FloatingCard\FloatingCard.csproj -c Release -- --smoke
```

`--smoke` paints a frame and checks transparent-corner hit testing (or the high-contrast solid fallback), the visible card, caption hit testing, native EDIT delivery, and stable borrowed HWND identity before closing. The default card uses solid colors and per-pixel alpha.

`--acrylic` makes the whole borderless window the card. It calls `SetBackdrop(WindowBackdrop.Acrylic)` before Show, so Desktop Acrylic blurs the windows behind it and Windows 11 supplies rounded corners and a compositor shadow. `--acrylic --smoke` also checks that the window is not layered and that Desktop Acrylic is selected when Windows presents it. Without transparency effects, or in high contrast, the card uses the solid theme background.

```powershell
dotnet run --project bindings\dotnet\FloatingCard\FloatingCard.csproj -c Release -- --acrylic
```

The [application window contract](../../../docs/specs/application.md#borrowed-hwnd-and-transparent-windows) defines hosting, ownership, and size limits. The [materials contract](../../../docs/specs/materials.md#borderless-material-windows) describes the acrylic card.
