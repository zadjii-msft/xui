# Asynchronous Shell menu discovery

These notes describe the gallery and C# explorer Shell-menu integration.
Public menu contracts are in [the menu reference](../specs/menus-and-input.md).

The gallery menu shows application commands and a loading message before Shell discovery finishes.
After the first menu paint, a dedicated STA thread creates the Shell handlers and their native window.
The UI thread receives command metadata, not COM objects.
When discovery finishes, the menu replaces the loading message with the discovered commands.
The replacement retains the gallery appearance and recomputes the native menu dimensions.
Discovery errors appear in the menu, with the Windows menu fallback still available.

Application commands and the Windows fallback keep their positions when Shell commands appear.
The menu postpones replacement while a command is highlighted or a mouse button is pressed.

Each opening uses its exact selection and fresh Shell handlers.
The worker does not reuse commands from a previous file, folder, or opening.
It retains at most one active request and one pending request, then exits after ten idle seconds.
Closing the menu cancels its request without waiting for a Shell extension.
A blocked extension can delay later Shell results, but application commands and cancellation remain available.
The worker releases its handlers on their STA after the extension returns, and retains the module until that cleanup finishes.

## Search snapshots

`c_api_shell_actions.inc` adapts the same worker to a cancellable metadata snapshot.
The adapter owns a hidden UI-thread validation window.
Each action checks the managed selection callback through that window.
The worker retains its original COM provider and command identities.
The search path requests canonical verbs. The ordinary gallery path still omits that extra extension work.

`ShellActions.cs` copies the metadata into managed records.
`ContextActionsController.cs` owns the Explorer popup, search, favorites, hidden app actions, and completion polling.
`ContextActionCatalog.cs` supplies explicit app identities, canonical-verb normalization, and pure search rules.
Duplicate canonical verbs cannot identify favorites.
The controller never saves `ItemKey` values.

`ContextActionsSmoke.cs` covers the live popup and preference changes through `--context-actions-smoke`.
`ContextActionTests.cs` covers search, favorite identities, disabled entries, and duplicate verbs.
`shell_menu_tests.cpp` covers native metadata, original COM invocation, stale selection rejection, cancellation, and handle retirement.
