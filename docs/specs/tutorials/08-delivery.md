# 8. Ship an accessible application

The final sample is a small task editor with shared styles and an embedded native Progress control.
Its [project](sample/TaskCard.csproj), [entry point](sample/Program.cs), and [component](sample/TaskCard.xui) are the complete source.

Before distribution, decide how a real application will persist data and deliver background results.
The sample intentionally provides neither service.
Apply only changes in-memory state, and closing or replacing the window discards that state.

## Check input and accessibility

Perform these checks in your application on an interactive desktop:

- Reach the input, toggle, and buttons with the keyboard; confirm visible focus.
- Check the input's accessible name, selection, undo, clipboard behavior, and the input methods your users need.
- Confirm that validation and completion are understandable without color.
- Check light, dark, and system high-contrast behavior.
- Resize the window and check text clipping, scrolling, and your required display scales.
- Use your target screen reader and verify names, roles, state changes, and the reading order.

These are acceptance tasks, not claims that this tutorial verified physical input, speech output, or every monitor transition.
Do not infer screen-reader announcements from a visible status-label update alone.
Application testing must confirm the experience your users need.

Styles preserve native behavior; they do not remove the need for these checks.
Use native text controls rather than implementing your own selection or IME handling.
Keep cancellation and disposal part of the same acceptance work.

## Choose the deployment model

| Application | Distribution boundary |
| --- | --- |
| Static C++ | Executable and required Windows system components |
| C ABI or Rust | Executable plus matching `xui.dll` |
| Framework-dependent C# | Application files, matching .NET runtime, and `xui.dll` |
| NativeAOT C# | Published native executable, companion output as required, and matching `xui.dll` |

Use the [NativeAOT and deployment procedure](../../../CONTRIBUTING.md#nativeaot-and-deployment).
The [tutorial procedure](../../../CONTRIBUTING.md#gitbook-tutorial-sample) gives this sample's project path.
Run the generated apphost executable, not `dotnet TaskCard.dll`, so the native-control manifest applies.

Release output does not need the `.xui` source generator or development reload host.
Changing markup in a deployed directory does not update a compiled release application.
Rebuild and redeploy it.

Media and web hosts are optional controls.
The core tutorial does not need a browser runtime.
An application that explicitly uses WebContent must follow its [runtime and allowed-origin contract](../scenes-and-hosts.md).

## Continue by feature

Use the [control catalog](../controls/README.md) for dialog, navigation, document, collection, and visualization features.
Use the [language guides](../languages/README.md) to implement the same interaction patterns outside `.xui`.
The [gallery](../gallery.md), [file explorers](../file-explorers.md), and [Minesweeper](../../../bindings/dotnet/Minesweeper/README.md) show larger applications.
