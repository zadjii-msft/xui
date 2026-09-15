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
