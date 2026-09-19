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

## Shell command icons

`NativeShellProvider::read` copies ordinary `MIIM_BITMAP` images into immutable `MenuIcon` pixels on the Shell STA.
Each bitmap is at most 64 by 64 pixels. The menu has a 4 MiB pixel budget.
Legacy bitmaps without alpha become opaque. Alpha bitmaps retain premultiplied BGRA pixels.
Callback and system pseudo-handles never reach GDI bitmap APIs.
Owner-drawn entries retain their native fallback and do not request extension drawing.
Unreadable or oversized bitmaps leave the icon space empty and emit a debugger diagnostic.

`CustomShellMenu` retains copied icons by command ID. Shell entries without copied bitmap data have no icon.
`Menu::draw` composites pixels against the current row color with GDI.
For supplied icons, disabled rows and high-contrast menus use the theme-colored generic glyph.
A separate column preserves checkmarks. Separators and discovery notices have no icon.
The Windows-menu fallback has no icon before or after discovery.
Icon discovery does not request canonical verb strings or traverse dynamic submenus.

`tests/shell_menu_tests.cpp` covers copied alpha pixels, bitmap ownership, pseudo-handles, size limits, and model propagation.
Its drawing checks inspect bitmap pixels, glyph pixels, and empty icon spaces at multiple DPI values in dark, light, and high-contrast palettes.
The existing latency fixture retains its first-paint, cancellation, and original-handler checks.
