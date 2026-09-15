# XUI

XUI is a small C++20 UI framework prototype for Windows.
The versioned C ABI also supports native ARM64 C# and Rust applications.
The file browser, Task Manager, control gallery, and thumbnail sample use the public control API.
The explorer supports folder navigation, file associations, context menus, tabs, and two resizable panes.
Win32 owns the windows and message loop. Direct2D draws the custom UI. DirectWrite draws text.
The executable uses the static MSVC runtime and Windows system libraries. It has no third-party runtime dependencies.

**Technical report:** [A useful minimum for a Windows GUI](docs/windows-gui-memory.md) explains the memory experiments, performance tradeoffs, and reproduction steps.

**Design proposal:** [A Windows 11 appearance for XUI](docs/winui-design-plan.md) describes an opt-in WinUI-style skin, implementation stages, and performance gates.

**Maintainer handoff:** [WinUI experiment status](docs/winui-maintainer-handoff.md) records completed work, known gaps, source ownership, and build and regression commands.

## WinUI-style gallery experiment

`xui_winui_gallery` is an opt-in, solid-surface experiment.
It uses the existing Win32, Direct2D, and native text-input backend.
The gallery has live Classic/WinUI and theme controls for comparison.
Standard, accent, and subtle buttons retain the same interaction behavior.
The skin also covers radio groups, choice lists, combo boxes, numeric inputs, expanders, sliders, and progress indicators.
Grids, collections, scrollbars, split views, charts, status messages, dialogs, and tooltips use the same style.
Native documents and password inputs have themed frames.
Image, vector, map, and runtime-host frames retain their authored content colors.
Existing applications still use the classic appearance by default.

Build and run the experiment from a Visual Studio developer PowerShell:

```powershell
cmake -S . -B build\winui -G "Visual Studio 17 2022" -A ARM64 -DBUILD_TESTING=ON
cmake --build build\winui --config Release --target xui_winui_gallery
.\build\winui\Release\xui_winui_gallery.exe
```

For an x64 build, replace `ARM64` with `x64`.
The ordinary `xui_gallery.exe` also accepts `--winui` to open the experiment.
Both executables accept `--light`, `--high-contrast`, and `--system-titlebar`.

For the complete catalog, build `xui_gallery` and use `--winui-catalog`:

```powershell
cmake --build build\winui --config Release --target xui_gallery
.\build\winui\Release\xui_gallery.exe --winui-catalog
```

The complete catalog also has a live `Style: WinUI` / `Style: Classic` button.
The style switch preserves the current page, control values, and native text.
The compact experiment remains the default for `xui_winui_gallery`.
Its **Control specimens** page shows default-size controls without gallery-specific height overrides.
This page includes enabled and disabled fields, buttons, choices, an expander, and a content dialog.

The WinUI presentation now changes measurement and part layout, not only paint.
Style changes preserve control identity and values, but can change their bounds.
Explicit preferred and fixed sizes remain application constraints.
WinUI buttons and single-line fields default to 32 DIPs.
Input headers use 14-DIP text, a 20-DIP line box, and an eight-DIP gap.
Numeric and editable combo inputs share one field frame instead of separate nested frames.
Numeric fields select the value on first focus, but later clicks place the caret normally.
Numeric inputs retain inline spin buttons by default.
`set_spin_placement(NumberSpinPlacement::hidden)` selects the WinUI-default hidden variant.
Expanders measure their content instead of retaining an empty Classic-sized body.
WinUI content dialogs use centered, content-sized layouts with separate action footers.
Long dialog bodies scroll without moving the footer.
Titles wrap to at most two lines.
Noneditable combo popups align the selected row over the closed field, within viewport limits.
Plain text fields show a clear action when focused and nonempty.
This action preserves native undo and adds no Tab stop.
Field fills use the reference state's alpha color over the actual parent surface.
Native EDIT and Direct2D use the same resolved fill.

The C++ API separates the visual style from the color theme:

```cpp
xui::WindowOptions options;
options.visual_style = xui::VisualStyle::winui;
xui::Window window(options);
auto save = std::make_shared<xui::Button>(L"Save");
save->set_appearance(xui::ButtonAppearance::accent);
// A live comparison preserves the controls, native inputs, and render target.
window.set_visual_style(xui::VisualStyle::classic);
```

Button appearances apply in the WinUI style.
High contrast uses system colors instead of the experimental RGB tokens.
Latin UI locales use Segoe UI Variable with automatic optical sizing when the installed DirectWrite fonts support it.
Native EDIT uses the corresponding Segoe UI Variable Text face.
Classic and unsupported font configurations retain Segoe UI.
WinUI command icons use the installed Segoe Fluent Icons font, including navigation, breadcrumbs, captions, disclosure arrows, search, and field actions.
Checkbox marks, menu checks, grid indicators, tab close buttons, status symbols, and generic file placeholders use the same font.
Classic retains its vector icons and existing caption font. Shell icons and thumbnails retain their original content.
If Segoe Fluent Icons is absent, WinUI uses Segoe MDL2 Assets and reports this fallback in the debugger.
Missing symbol fonts or required glyphs produce an explicit error, not missing-character boxes. XUI does not redistribute fonts.
Buttons and selection indicators use translucent template brushes, including separate disabled and pressed states.
Dark accent text and selected marks use black. Radio dots remain geometry, not font characters.
Navigation uses a 14-DIP semibold pane title and an unframed menu button.
Catalog-wide paint coverage does not establish pixel or behavior parity with WinUI.
It does not implement Mica, acrylic, animation, automatic system accent selection, or WinUI API compatibility.
Indeterminate progress uses a static segment, not an animation timer.
Date/time controls, native suggestion lists, native editor scrollbars, disabled RichEdit backgrounds, and third-party Shell menus retain platform-owned visuals.
The new style API is C++ only. The C ABI and its binding defaults remain unchanged.
The [design plan](docs/winui-design-plan.md) describes the remaining stages and measurement requirements.

**Declarative C#:** [The `.xui` language](docs/xui-language.md) compiles UI blocks into retained XUI controls.
It includes a `dotnet watch` development host and a VS Code syntax package.
The [engineering plan](docs/xui-language-plan.md) defines the language scope, reload contracts, and acceptance checks.
The [Minesweeper sample](bindings/dotnet/Minesweeper/README.md) uses `.xui` for a playable game with hot reload.

## C# file explorer

`bindings\dotnet\FileExplorer` contains the C# explorer demo.
It uses the XUI controls through the public .NET bindings.
The existing C++ explorer remains available as `xui_demo.exe`.

### Build and run

The demo requires Windows, the .NET 10 SDK, and the Visual Studio C++ build tools.
Run these commands from a Visual Studio developer shell at the repository root:

```powershell
cmake -S . -B build\explorer -A ARM64
cmake --build build\explorer --config Release --target xui
dotnet build bindings\dotnet\FileExplorer -c Release "-p:XuiNativeDir=$PWD\build\explorer\Release"
.\bindings\dotnet\FileExplorer\bin\Release\net10.0\win-arm64\FileExplorer.exe "D:\Documents"
```

Without a folder argument, the demo opens the current directory.
For an x64 build, use `-A x64` and add `-r win-x64` to the .NET command.
The native DLL and the .NET application must use the same architecture.

### Explorer controls

The title bar contains a navigation button, independent tab strips for each pane, and Windows caption controls.
Each tab row follows its pane, including splitter and window-size changes.
The selected tab has rounded top corners and an open bottom edge that joins its pane.
Inactive tabs share a continuous strip instead of separate button outlines.
The close button highlights under the pointer. The keyboard focus rectangle stays inside the selected tab.
Clicking a tab selects it and moves focus into its file pane, including clicks on the current tab.
Keyboard users can focus the tab strip and select tabs with the arrow keys.
Enter or Space moves focus into the selected pane.
The tab focus rectangle appears only during keyboard navigation.
The tab row inherits its parent background, including unused space after the last tab.
The gallery and explorer do not need a separate background rectangle for their tab rows.
The title bar does not repeat the window title.
The navigation pane contains Recents, Bookmarks, Storage drives, Places, and the path tree for the active folder.
It has no title header.
Its filter searches item names and paths.
The folder tree shows the ancestors and immediate child folders of the active location.
Selecting a folder updates the tree.
The collapsed navigation pane is completely hidden.
The navigation button stays at the left edge of the title bar.
When navigation is hidden, the first tab starts after that button.

Each pane has its own tabs, navigation history, details view, and Find bar.
Find uses a single-line field with placeholder text and an X button, without labels or internal scrollbars.
Each tab retains its folder, filter, sort order, selection, and scroll position.
Column headers support sorting and width adjustment.
File and folder rows highlight under the pointer without changing the selection.
File rows, navigation folders, and navigation-palette results show asynchronous Windows thumbnails or Shell icons.
Navigation sections also have icons.
Right-click selects the target row and opens its context menu.
The XUI menu combines supported Windows Shell commands with folder navigation, bookmarks, and Refresh.
It uses the same styled native context menu as the gallery's Menus and confirmation page, not a `CommandSurface` popup.
Shell commands still run through their original Windows handlers.
The Show Windows menu... item opens the full native menu for extension-specific content that requires native handling.
Labeled commands with native bitmaps remain available as text in XUI.
Native submenus and owner-drawn entries use the Windows menu fallback.
Open in this pane keeps folder navigation in the demo. Shell Open uses Windows behavior.
Files omit folder-only commands and duplicate Open actions.
Selection or source changes cancel pending menu actions instead of changing their target.

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

The size column sorts by byte count, not by the formatted text.
Folder scans and palette suggestions run outside the UI thread.
Canceled or obsolete requests cannot replace the current view.

| Input | Action |
| --- | --- |
| Navigation button | Expand or collapse the navigation pane |
| Alt+F | Focus the navigation filter |
| Ctrl+L / address button | Open the navigation palette at the active folder |
| Up / Down in the palette | Select the previous or next result |
| Tab in the navigation palette | Insert the selected full path without navigation |
| Ctrl+Backspace in the navigation palette | Delete the selection or previous word or path component |
| Enter in the navigation palette | Open the selected result in the active pane |
| Ctrl+Enter in the navigation palette | Open the selected result in the other pane |
| Alt+Left / Alt+Right in the palette | Previous or next completed query |
| Alt+Up in the palette | Show the parent folder |
| Escape in the palette | Close the palette without navigation |
| Ctrl+Shift+P | Open the searchable command palette |
| Ctrl+T / Ctrl+W | Add a tab / close the active tab |
| Ctrl+Tab / Ctrl+Shift+Tab | Next / previous tab |
| Ctrl+\\ | Show or hide the second pane |
| F6 | Focus the other pane |
| Alt+Left / Alt+Right | Previous / next folder in the active tab |
| Alt+Up / F5 | Parent folder / refresh |
| Ctrl+F | Show the Find bar at the bottom of the active pane |
| Escape with Find open | Clear the filter and close the Find bar |
| Ctrl+D | Add or remove the current folder bookmark |
| Ctrl+F6 | Switch between dark and light themes |

Both palettes appear at the center of the window, independent of the active pane.
They contain a query field and results, without duplicate headings, navigation buttons, or shortcut footers.
The palette frame and results share one background color.
Command shortcuts use separate keycaps on the right, beside each command title.
Status messages appear only for pending requests, empty results, and errors.
Keyboard history, completion, acceptance, and dismissal remain available.
The navigation palette initially shows the children of the current folder.
Typed paths support relative paths, quoted paths, environment variables, and UNC paths.
A partial final component filters the parent folder by name.
Prefix matches appear before other substring matches.
Tab completion leaves the caret at the end of the completed path.
Within a loaded folder, the palette filters its snapshot immediately without a delay or an empty intermediate view.
For a different folder, the existing rows remain visible but cannot activate until the new scan finishes.
With no selected result, Enter attempts to open the typed folder.
Explicit file activation uses the Windows file association, which can run executable files.

### State and implementation

Bookmarks and recents use `%LOCALAPPDATA%\Xui\FileExplorer\state.json`.
State writes replace the file atomically.
An unreadable or corrupt state file produces a visible error and disables state writes for that session.
The demo does not overwrite that file with empty state.
Navigation errors preserve the committed folder and its rows.

`Models` contains the filesystem services, tab state, history, and persistent state.
`FilePaneView` connects each pane to its model.
`PaletteController` handles the two palettes.
`NavigationSidebar` builds the navigation entries.
`FileContextMenu` supplies commands for the selected file or folder.
These classes use explicit model updates rather than a separate MVVM package.

Tab selection and content activation are separate operations.
C++ hosts use `TabStrip::on_activate` to focus their selected content after a click, Enter, or Space.
The C ABI reports this activation as `XUI_CLICK`. The .NET explorer handles `EventKind.Click` by focusing the file grid.
Arrow-key selection does not activate content or move focus out of the tab strip.

This first version does not provide file copy, move, rename, delete, drag-and-drop, or recursive search.
It does not claim full File Pilot parity.
The navigation pane limits very large lists to the native control capacity and shows a notice for omitted entries.
The details view still exposes all entries from the folder scan.

### Tab colors

`TabColors` provides optional colors for the row, selected tab, inactive tabs, hover state, and borders.
Selected and inactive tabs each have separate background and text colors.
"Selected" identifies the active page, not keyboard focus.
Each color uses `0xRRGGBB`. Alpha values are not supported.
Unset colors follow the current theme, and high contrast uses system colors instead of overrides.

```cpp
xui::TabColors colors;
colors.selected_background = 0x26465e;
colors.selected_text = 0xffffff;
tabs->set_colors(colors); // The row still inherits its parent background.
tabs->set_colors({});     // Restore theme colors.
```

The .NET binding provides `TabStrip.Colors` and `SetColors(new TabColors(...))`.
Nullable fields restore individual theme colors, and `SetColors(default)` clears all overrides.
Rust provides `TabStrip::set_colors` and `colors`, with `Option<u32>` fields.
The C ABI provides `xui_tab_set_colors` and `xui_tab_get_colors` in `xui_layout.h`.
Its versioned record uses a mask to distinguish an unset color from black.

Applications must pair custom backgrounds with readable text colors.
Explicit colors stay unchanged across theme switches until the application replaces or clears them.
The gallery's Tabs page includes a **Custom tab colors** toggle.

### Automated checks

Run the model checks without a native DLL:

```powershell
dotnet run --project bindings\dotnet\FileExplorer.Tests -c Release
```

After the native and .NET builds, run the window workflow checks:

```powershell
.\bindings\dotnet\FileExplorer\bin\Release\net10.0\win-arm64\FileExplorer.exe --smoke
```

The window checks use temporary folders and do not write the normal state file.
The executable supplies the Windows control manifest. The `dotnet FileExplorer.dll` command does not supply this manifest.

## Visual milestone

The explorer starts with a dark theme. Its Theme button and Ctrl+F6 switch between dark and light themes.
The C++ explorer accepts `--style=classic` (the default) or `--style=winui` after an optional folder path.
Both styles use attached tabs, with the navigation controls directly below the tab strip.
F6 switches between the two panes. The theme lasts for the current window.
System high contrast overrides the theme colors.
`ThemeMode::high_contrast` also uses system high-contrast colors without a change to Windows settings.

The window includes tabs, navigation commands, an address field, a search field, thumbnail icons, and a status area.
Rows have separate hover, selection, and keyboard-focus states.
The scrollbar supports thumb dragging and track paging. The list exposes scrolling through UIA `ScrollPattern`.
The scrollbar does not create a separate UIA element.

`include\xui\theme.hpp` contains shared colors, typography sizes, spacing values, and scrollbar geometry.
`WindowOptions::theme` selects the initial theme. The browser sample also accepts `BrowserOptions::theme`.
Both explorer panes use WIC image previews and Windows Shell thumbnails or file icons.
Pending or failed requests use vector icons. EXE files use their Shell application icons when no thumbnail is available.
Supported Windows versions use matching title-bar colors. Older versions retain system title-bar colors.

## Explorer workflows

Run the explorer with an optional initial folder:

```powershell
.\build\header\Release\xui_demo.exe "D:\Documents"
```

Without an argument, the explorer uses the current directory.
The window title is `XUI/Files - {folder}`, where `{folder}` is the committed location in the active pane and tab.
Successful navigation, history movement, tab selection, and pane focus update this title.
Typed prefixes and failed navigation do not change it.

Both panes retain independent tab strips in one horizontal band.
The first tab strip also contains the Split panes and Theme icon buttons. These buttons remain available when the second pane collapses.
The existing split ratio and minimum pane width still apply.
Automatic collapse activates the first pane and updates the title, including when a global button has focus.
Back, Forward, Up, and Refresh use compact vector buttons beside the address field.
Their accessible names and automation IDs remain unchanged.
The address field has no visible caption or search icon. Its native EDIT retains its accessible name and text patterns.
At narrow widths, the address field hides its shortcut hint to preserve text space.
The separate brand, pane headings, and shortcut footer are absent. This section and the context menus describe the keyboard commands.

The address field accepts absolute paths, UNC paths, extended paths, and paths relative to the current folder.
It accepts enclosing quotation marks and environment references such as `%USERPROFILE%` and `%SystemRoot%\System32`.
Successful navigation stores the expanded path in the address field, tab history, and window caption.
The address field does not interpret shell commands.

| Input | Action |
| --- | --- |
| Back / Alt+Left | Previous location in the active tab |
| Forward / Alt+Right | Next location in the active tab |
| Mouse Back / Forward | Previous / next location in the pane under the pointer, without a focus change |
| Up / Alt+Up | Parent folder, without movement beyond a drive or share root |
| Ctrl+L, then Enter | Address focus, then folder navigation |
| Up / Down in an address dropdown | Previous / next folder suggestion |
| Enter with a selected suggestion | Accept the folder and navigate once |
| Tab with a selected suggestion | Accept the folder without navigation or focus movement |
| Escape in an address dropdown | Close the dropdown and keep the typed text |
| Escape in the address field without a dropdown | Restore the current folder path |
| Ctrl+F | Search focus |
| Enter in search | List focus |
| Enter or double-click on a row | Folder navigation or the Windows file association |
| F5 | Refresh the active location or retry the pending location |
| Escape during a scan | Cancel the pending navigation |
| Ctrl+T / + | New tab at the current location |
| Ctrl+W / tab close mark | Close the active tab |
| Ctrl+Tab / Ctrl+Shift+Tab | Next / previous tab |
| Left / Right with tab-strip focus | Previous / next tab, including tabs outside the visible strip |
| Delete with tab-strip focus | Close the selected tab |
| Split panes / Ctrl+Shift+P | Show or hide the right pane |
| F6 | Focus the other pane |
| Left / Right with divider focus | Change the split ratio |
| Home with divider focus | Equal pane ratio |
| Right-click / Shift+F10 / context-menu key | Native context menu |
| Ctrl+C with list focus | Copy the visible selected path |
| Theme / Ctrl+F6 | Dark or light theme |

The context menu supports Open, Open folder in new tab, Open folder in other pane, Copy full path, and Refresh.
It also supports tab commands, Copy folder path, and Copy status details.
An empty-area context click clears the previous selection. A hidden selection cannot open or copy an item.
The native EDIT retains its own clipboard menu and text-editing keys.

### Compact header validation

The current validated executable is `build\header\Release\xui_demo.exe` (650,240 bytes).
This separate build leaves existing executables available to open applications.
The full ARM64 Release build and all 23 native CTest tests passed.
The explorer smoke passed 1,863 assertions, including title transitions, native address editing, and 18 width/DPI/theme combinations.
The matrix uses 924, 600, and 460 DIP outer widths, injected 96/144/192 DPI, long Unicode paths, and 16 tabs.
Both themes retain the global commands and independent pane tabs without overlap.
The frame regression retained zero missing glyph regions across 1,323 presentations, including a visible-to-hidden address caption transition.
All five existing binding clients passed normal and callback-failure UIA checks against the new binaries.
Both C# wrapper variants also passed their 17 assertions. The bindings used isolated copies under `build\header\bindings`.

Three fresh processes used the existing 60-file measurement procedure without a resize or memory trim.
The warm median private commit was **28.52 MiB**. The private working set was **14.12 MiB**.
The warm total working set was **44.85 MiB**. Input-and-paint latency was **17.29 ms**, with a 16.76–18.36 ms range.
The preceding complete-frame build recorded 30.29 MiB private commit, 16.00 MiB private working set, and 17.83 ms latency.
Those earlier results are historical measurements, not a simultaneous comparison.
The target count remained one. These results do not change the documented driver allocation threshold at larger window sizes.
Separate geometry probes measured a list height increase from **269 to 443 DIPs** at the unchanged default client size.
The **174-DIP gain** comes from the removed header rows and footer, not a smaller file row.

The full suite log is `build\header\ctest-final.log`. The final targeted frame rerun is `build\header\ctest-rerun.log`.
Memory and geometry samples are in `build\header\browser-memory.json` and `build\header\viewport-comparison.json`.
Owned-window captures are in `build\header\captures`, including single-pane, split-pane, narrow, dark, and light views.
Native EDIT exposes an editable `ValuePattern` on this machine. Its system `TextPattern` is unavailable for both address and search.
The compact field preserves that platform behavior rather than supplying a replacement text provider.
Physical mixed-monitor transitions and interactive IME candidate windows still need manual coverage.
IME composition suppresses application shortcuts until committed text reaches the control.

File activation calls `ShellExecuteExW` with the exact selected path and no command-line parameters.
Only an explicit row activation or Open command calls this dispatcher.
Windows associations can start executable files.
The sample does not copy, move, rename, or delete files.
The optional Shell menu can expose extension verbs, including Properties.
It uses an explicit native fallback. XUI does not reinterpret third-party owner-drawn menu items.
The sample has no drag-and-drop or tab reorder.

### Navigation and resource ownership

Each pane owns an independent tab set. Each tab retains its path, history, query, selected path, focused path, and scroll offset.
Each tab has at most 128 history entries. Each pane has at most 16 tabs.
The last tab stays open. The pane header shows the active tab position and total tab count.

Navigation commits only after a successful scan.
A failed navigation preserves the current location and view. The status shows the attempted path and the Windows error.
The address field returns to the current location.
An initial failure or an unavailable inactive tab shows an empty view with an error.
Inactive tabs retain state, not snapshots or native control trees. Tab selection starts a new scan and restores state by path.

One worker serves each active pane. A new navigation cancels obsolete source work.
Query changes cancel obsolete filters without repeated directory scans.
Generation checks reject old results after navigation, tab selection, or tab closure.
Snapshot and directory-map disposal stays outside the UI thread.
Window closure revokes callbacks and requests cancellation without a UI-thread join.

The divider retains two clipped content hosts and uses one shared render target.
Each pane has a 300-DIP minimum width. A width below 610 DIPs collapses the right pane.
A wider window restores that pane and its state.
The Single pane command also cancels the right worker and releases its active snapshot.
The fixed pane tree remains available for reuse. No timer polls inactive tabs.

## Reusable controls

`include\xui\application.hpp` provides `Window` and `Application::run`.
Applications describe a control tree and callbacks. They do not supply a window procedure or drawing callbacks.
`demo\gallery.cpp` and `demo\browser.cpp` contain the application compositions.

| API | Purpose |
| --- | --- |
| `Stack` | Layout, padding, spacing, flex space, an optional surface, and a separator |
| `Label` | Text, heading or caption appearance, semantic color, and an accessible name |
| `Button` | An enabled command with an `on_click` callback and optional vector icon |
| `Toggle` | A checkbox with `checked`, `set_checked`, and an `on_change` callback |
| `TextInput` | Native EDIT, committed-text and submit callbacks, optional asynchronous suggestions, search appearance, placeholder, and shortcut hint |
| `ScrollView` | Retained content, a vertical viewport, a scrollbar, focus reveal, and UIA scroll actions |
| `ContentView` | A clipped retained subtree with a native parent for child controls |
| `TabStrip` | Dynamic tab data, stable IDs, selection, close callbacks, overflow reveal, and UIA tab patterns |
| `SplitView` | Two content hosts, a draggable divider, keyboard resizing, minimum widths, and narrow-window collapse |
| `FileList` | Immutable views, stable selection and item focus, navigation, viewport, empty text, and change callbacks |
| `DataGrid` | Immutable row sources, stable keys, shared multi-selection, header filters, selection check columns, sorting, resize, reorder, and two-axis scrolling |
| `HistoryChart` | A fixed 60-sample history, explicit gaps, a numeric scale, and an accessible metric name |
| `PageView` | Retained pages with one visible content host and no page-selection I/O |
| `NavigationView` | Nested items, native search, pinned header/footer shortcuts, shared selection, icons, badges, and expanded or collapsed panes. C# exposes nested entries, search, and expansion. |
| `ViewTask` | Cancellable source and filter work, latest-generation delivery, and progress counters |
| `SampleTask` | One background worker, a bounded result slot, periodic or manual requests, pause, and UI-thread delivery |
| `Image` | Asynchronous WIC file decoding, bounded pixels, shared bitmaps, explicit unload, and accessible image names |
| `ImageResources` | Process-wide image limits, ownership counters, latency counters, and unused-cache eviction |
| `ContentDialog` | Client-bound modal content, validation, default/cancel actions, and focus return |
| `InlineStatus` | Severity, an independent action, dismissal, and UIA live announcements |
| `MultilineText` | Bounded native RichEdit plain text, selection, clipboard commands, undo, and read-only mode |
| `PasswordInput` | Masked native EDIT, explicit secret access, and an optional reveal preview |
| `RichText` | Native RichEdit styled runs, explicit links, selection, and bounded editing |
| `DateTimePicker` | Native date, time, and calendar presentations with validated Gregorian bounds |
| `ColorPicker` | Retained sRGB byte channels, alpha preview, swatches, and numeric keyboard access |
| `VectorCanvas` | Immutable paths, shapes, transforms, clipping, hit testing, and a virtual accessible element list |
| `MapView` | Offline Mercator coordinates, graticule, authored overlays, stable markers, pan, zoom, and cancelable provider requests |
| `MediaPlayback` | Explicit local audio/video through lazy Windows Media Foundation, with playback, seek, volume, and unload |
| `WebContent` | Optional WebView2, owned HTML, explicit HTTPS origins, script results, and native browser accessibility |
| `Window` | Content ownership, title, size, focus, themes, key callbacks, clipboard text, tasks, closure, and error text |

`Control::set_automation_id` supplies an optional application identity for custom controls.
Without an override, custom controls use their stable element IDs.
`Control::on_context_menu` returns themed native menu items with actions, enabled state, checked state, and separators.
Native EDIT retains its system menu unless the control supplies a context-menu callback. Its native text provider does not change.
Nested native EDIT controls expose the public automation ID and name through their geometry override.
`Control::on_focus` reports a focus transition without a replacement window procedure.

### Shared context menus

Every `Control::on_context_menu` callback uses the same Windows menu backend.
This includes buttons, native text inputs, file lists, and data grids. Applications do not supply colors, drawing code, or HWNDs.
`WindowOptions::theme` and `Window::set_theme` select the shared palette.
System high contrast overrides dark and light colors.

The backend keeps `HMENU` and the native `#32768` popup.
Owner drawing supplies Segoe UI text, DPI-scaled padding, a checkmark column, right-aligned shortcut labels, separators, and selection colors.
The popup uses the shared surface color and a one-pixel border.
Its outer frame is square. Selected rows have rounded corners outside high contrast.
Windows retains menu placement, capture, dismissal, and accessibility.

```cpp
control->on_context_menu([&] {
    return std::vector<xui::MenuItem>{
        {L"&Refresh\tF5", refresh},
        {L"Copy path\tCtrl+C", copy_path, has_selection()},
        {L"", {}, true, false, true},
        {L"Dark theme", select_dark_theme, true, dark_theme()}
    };
});
```

The first tab separates the command name from its shortcut label.
A shortcut label does not register a keyboard shortcut. `Window::on_key` can call the same command function.
An ampersand marks a mnemonic. Two ampersands display one literal ampersand.
Other letters select commands by their first letter. Repeated letters cycle through matching commands.
Up, Down, Home, End, Enter, Escape, the context-menu key, and Shift+F10 use the same menu.
Home and End select the first and last enabled commands through the native menu-selection protocol.
Native arrow navigation can focus a disabled command. Enter dismisses that menu without a command call.
Separators do not run commands. UIA invocation rejects disabled commands.

The menu factory and its returned items are snapshots.
The backend closes the native menu and releases its resources before it calls the selected action.
An action can change the theme, open another context menu, close the window, or delete its public `Window`.
The backend remains alive until the active dispatch returns.
Callback exceptions and reported native API failures reach `Window::error` and a nonzero `Application::run` result.

A context menu cancels visible and pending suggestions before it opens.
Late suggestion results cannot reopen the dropdown over the menu.
A custom text-input menu does not open during IME composition.
Menu dismissal preserves native EDIT selection. Focus returns to the previous control only while the same host remains in the foreground.
Outside clicks, host deactivation, host closure, and theme or DPI changes cancel the menu.
Windows places the popup within the destination monitor's work area.

Menu fonts, brushes, thread hooks, and owner subclasses exist only for the active popup.
Closed menus retain no menu resources and schedule no timers or paints.
The backend creates no Direct2D targets.
Native MSAA menu metadata preserves command names and checked state. UIA retains native `Menu`, `MenuItem`, focus, and Invoke behavior.
The current model is flat. Submenus, menu bars, dropdown buttons, and Windows Shell extension menus are not part of this change.

#### Menu checks and captures

The ARM64 menu test covers three palettes and 96, 144, and 192 DPI.
Pixel assertions read the visible popup border, margin, and selected row.
The test also covers UIA names, focus, disabled invocation, checked state, native EDIT focus, IME messages, and pending suggestion cancellation.
Lifecycle cases cover callback exceptions, public-window deletion, host closure, and a reentrant menu command.
Fifty open/close cycles retained no extra GDI or USER objects. Closed menus produced zero idle paints and zero Direct2D targets.
Work-area checks covered all four monitors on the test desktop.
The DPI tests inject drawing scales. They do not change monitor configuration.
The high-contrast test uses current system colors. Physical high-contrast and screen-reader sessions remain manual checks.

The final ARM64 build passed all 25 CTest tests, including 1,014 menu assertions and 347 C ABI assertions.
The explorer smoke passed 1,930 assertions. The Task Manager desktop smoke passed 328 assertions.
All five binding clients passed both regular and callback-failure runs.
Those clients use copies under `build\menus\bindings`, with the new DLL. Older application deployments remain unchanged.
The first sampled themed popup appeared after 16.5 ms.
Across 50 warm cycles, sampled visibility averaged 23.3 ms, with a 35.2 ms 95th percentile.
The samples use a 10 ms observation interval and include Windows menu work.
`build\menus\menu-timing.json`, `ctest-final.log`, and the client logs contain the measurements and results.

Run these commands from the repository root:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Preview\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
& $cmake -S . -B build\menus -G 'Visual Studio 17 2022' -A ARM64 -DXUI_DESKTOP_TESTS=ON
& $cmake --build build\menus --config Release --parallel 4
& $ctest --test-dir build\menus -C Release --output-on-failure
& build\menus\Release\xui_menu_tests.exe build\menus\menu-captures
```

The menu test writes owned-window BMP captures to `build\menus\menu-captures`.
Files include `before-native.bmp`, `dark-96.bmp`, `light-144.bmp`, and `contrast-192.bmp`.
The explorer and Task Manager smoke tests write menu captures beside their build's `Release` directory, under `captures`.
Current application captures are `build\menus\captures\explorer-menu.bmp` and `build\menus\captures\task-manager-menu.bmp`.
Before-change application captures are under `build\menus\before\captures`. Those runs use copies of the previous `build\columns` executables.
PNG copies permit image review without changes to the BMP capture tests.
The menu test reports sampled popup visibility latency, not an isolated rendering benchmark.
If Windows still maps an executable from a previous fixture run, Shell thumbnail tests need a fresh fixture directory.

The C++ API provides `Window::set_title` and `Window::title`.
The setter runs on the window owner thread before or during `Application::run`. Repeated values do not update the native caption.
A closed window rejects changes. The getter retains the last title after closure.
Invalid strings, wrong-thread calls, and native title failures throw exceptions.
`Button::set_icon` selects an icon-only presentation without changing the accessible name.
`ButtonIcon::none` restores text. Icon buttons retain standard focus, hover, pressed, disabled, and high-contrast states.
`TextInput::set_caption_visible(false)` hides the native caption without a search icon.
The native label remains available for EDIT naming. The default caption and search presentations remain unchanged.
These additions are C++ APIs only. They do not change the C ABI or the C# and Rust bindings.

### Tabs, split panes, and activation

`TabStrip::set_tabs(items, selected_id)` replaces tab data without a native tree replacement.
It rejects zero IDs, duplicate IDs, and an unknown selected ID before a state change.
IDs represent stable tab identities, not display positions. An application must not reuse an ID for a different tab.
`select`, `step`, and `request_close` share the public action callbacks.
Property assignment does not call `on_select`. `on_close` requests closure without an automatic data change.

The tab strip exposes UIA `Tab`, `TabItem`, `SelectionPattern`, and `SelectionItemPattern`.
It publishes structure, selection, and focus changes. A removed tab provider rejects later actions.
`SplitView` exposes a divider through `RangeValuePattern`, with a ratio from 10 to 90 percent.
The layout also enforces pane minima. A requested ratio can therefore differ from the physical split near the minimum width.
Native children and custom pixels stay inside their content host.
Capture loss, cancellation, deactivation, and DPI changes cancel a divider drag.

`FileList::on_activate` receives a copied `FileItem` and a `FileActivation` reason.
The reasons distinguish Enter, double-click, and an application command.
`activate_selected` rejects disabled lists and hidden selections.
`restore_state` restores selection, independent item focus, and scroll offset against the current snapshot.
It preserves hidden identities that still exist in the source.

These additions are native C++ APIs. The versioned C ABI and C#/Rust wrappers do not expose the new workspace controls.
Existing ABI entry points and layouts remain unchanged.

### Task Manager

Run the sample:

```powershell
.\build\arm64\Release\xui_task_manager.exe
```

The sample has Processes, Performance, and Details pages.
Every page uses the same live Windows snapshot.
There are no simulated GPU, network, service, or startup-app pages.
The sample uses the public controls. It has no application-specific window procedure, renderer, or UIA provider.
The Theme button cycles through dark, light, and high-contrast colors without a change to Windows settings.

| Input | Action |
| --- | --- |
| Ctrl+F | Process search by name or PID |
| Ctrl+1 / Ctrl+2 / Ctrl+3 | Processes / Performance / Details |
| F5 / Refresh | One new snapshot, including during pause |
| Ctrl+P / Pause / Resume | Suspend or resume periodic snapshots |
| Rate | Cycle through 1-, 2-, and 5-second intervals |
| Up / Down / Home / End / Page Up / Page Down | Row selection and reveal |
| F6 with grid focus | Switch between rows and column headers |
| Left / Right with header focus | Previous or next column header |
| Ctrl+Left / Ctrl+Right with header focus | Decrease or increase column width by 16 DIPs |
| Ctrl+Shift+Left / Ctrl+Shift+Right with header focus | Move the column one position left or right |
| Enter / Space with header focus | Sort the column |
| Enter or double-click on a row | Selected-process details |
| Drag a header boundary | Resize the column |
| Drag a header | Move the column to the insertion marker |
| Escape during a header drag | Cancel the move or restore the original width |
| Shift+wheel, horizontal wheel, or bottom scrollbar | Horizontal scroll |
| Right-click / Shift+F10 / context-menu key | Details, Copy PID, End task, pause, and refresh commands |

The Processes page shows name, PID, CPU, working set, thread count, total I/O rate, and counter availability.
An all-digit search matches an exact PID. Other searches match part of the process name, without case sensitivity.
Missing permissions and process exits are normal sampling conditions.
The row remains visible when possible. Unavailable values show an em dash, not zero.
The status area shows collection errors, snapshot count, and collection duration.
Details includes the selected image path, architecture, working set, and private commit.
The sampler requests image metadata only for the selected identity.
Open file location passes the exact parent directory to the Windows shell, without command-line concatenation.

**CAUTION:** End task can lose unsaved work.
The confirmation dialog names the process and PID. No is the default answer.
The worker opens the process again and compares its creation time with the selected identity.
It keeps that same handle through the critical-process check and termination.
PID 0, PID 4, the sample itself, and critical processes cannot be termination targets.
An unavailable identity or failed safety check also blocks termination.
The sample does not request debug privilege or elevation.

#### Metric definitions and sampling

Process CPU uses the kernel-plus-user delta, elapsed monotonic time, and the active logical-processor count.
A new process, resume, or invalid counter delta has no CPU rate until the next valid interval.
The code rejects negative, overflowing, nonfinite, and implausible deltas.
It clamps only drift within 0.01 percentage points of 100 percent.
System CPU subtracts idle from the kernel-plus-user delta because `GetSystemTimes` includes idle in kernel time.
On systems with multiple processor groups, system CPU covers the calling group. The Performance page states this limitation.

Working set means all resident process pages, including shared pages.
Private commit means committed private virtual memory, not private resident RAM.
Physical RAM reports total, used, and available bytes from `GlobalMemoryStatusEx`.
System commit and its limit come from `GetPerformanceInfo`.
`GetProcessIoCounters` includes file, network, and other transfers. The sample does not label this counter as disk throughput.
MiB and GiB use powers of 1024.

One worker collects all process and system counters. It does not create a thread for each process.
The interval starts after each collection, so slow collections cannot accumulate scheduled work.
The worker retains one pending request and one latest result.
An undelivered collection error remains pending until UI-thread delivery.
Closing the window revokes callbacks, requests cancellation, and transfers worker disposal outside the UI thread.
Loaders must bound their work or obey the stop token. Arbitrary uninterruptible loaders cannot guarantee immediate process exit.

Pause stops periodic sampling. There is no animation timer or scheduled repaint during pause.
A manual refresh remains available. Resume resets rate baselines.
Minimized or hidden windows suspend the worker and reset baselines on return.
Charts retain 60 values each. Missing samples and cadence changes produce gaps.
The horizontal axis represents sample positions, not a uniform wall-clock timeline across rate changes.
The interval label describes the current cadence.

#### Grid and chart contracts

`include\xui\data_grid.hpp` supplies `GridSource`, `RowKey`, `DataGrid`, and `HistoryChart`.
A source supplies row count, key lookup, and cell text. It must remain immutable and thread-safe.
Keys must be unique within the source and must never identify a different item.
The process source uses PID plus creation time.
An inaccessible process gets a snapshot-local key, so later PID reuse cannot select it silently.
The grid supports at most `INT_MAX` rows and 64 columns.

The renderer requests text only for visible rows plus one boundary row.
It retains no visual, native window, provider cache, or string cache for each row.
Selection uses `CollectionSelection` across refresh, sort, and filter changes.
The compatibility `selected()` accessor returns the focused row key. `selection().contains(key)` reports actual membership.
A missing or filtered key cannot activate a process. A provider for a missing identity rejects later actions.
`ScrollIntoView` changes the viewport without selection.
Column headers expose Invoke for sorting. F6 and arrow keys provide the keyboard equivalent.

`set_columns` assigns source identities from zero in the supplied order. It resets the display order and widths.
Column names do not serve as identities. Duplicate names are valid.
`columns()` returns columns in display order. `column_order()` maps each display ordinal to its source identity.
`source_column(ordinal)` returns that identity. `display_column(identity)` returns its current ordinal, or no value for an invalid identity.
`GridSource::text`, `sort`, `set_sort`, `sort_column`, and sort callbacks use source identities.
`resize_column`, `set_column_width`, `focused_column`, and `column_at` use display ordinals.
`set_column_width` sets an exact width from 48 to 2,000 DIPs without a change to column order.
An invalid ordinal or width throws `std::invalid_argument` without changes.
`reorder_column(from, to)` moves a column between display ordinals. The destination is its final position.
An invalid move returns `false` without changes. A same-position move returns `true` without changes.
`set_column_order` accepts a complete permutation of source identities.
An incorrect size, duplicate identity, or out-of-range identity throws `std::invalid_argument` without changes.
An unconfigured grid accepts an empty permutation. `set_columns` requires 1 to 64 columns.
Each move preserves the column width, numeric format, sort identity, focused identity, and selected row key.
Snapshot replacement through `set_source` preserves column order and widths.
Task Manager uses this path for refresh, sort, filter, pause, resume, and page changes.

The existing mouse resize uses a five-DIP boundary and a horizontal resize cursor.
Mouse and keyboard resizing use widths from 64 to 1,000 DIPs.
A header move starts after six DIPs of pointer movement. A click without a drag sorts on release.
The accent-colored marker shows the insertion position. Pointer movement near a viewport edge scrolls horizontally.
Keyboard header navigation also reveals offscreen columns. There is no drag timer or idle render loop.
Escape, focus loss, capture loss, and window closure cancel an uncommitted column move.
UIA headers retain Invoke for sorting. Their HelpText describes the keyboard resize and move commands.
The grid does not advertise UIA Drag or DropTarget patterns.
Retained cell and header providers use source identities. Their bounds and GridItem column ordinals follow the current display order.
Table headers and cell-header associations use the same mapping.
Column changes publish a locked snapshot and raise layout and structure invalidation events, without a provider for every cell.

The grid exposes UIA Grid, Table, Selection, Scroll, GridItem, TableItem, SelectionItem, Invoke, and ScrollItem patterns where applicable.
Virtual providers resolve stable keys against immutable snapshots.
The host processes value-only action tokens on the UI thread.
UIA events use root-level updates and selected-item changes, not thousands of per-cell events.
Charts expose their current metric and scale as accessible text. They do not advertise an editable value pattern.
The controls use the existing window render target and theme palette.

`DataGrid`, `HistoryChart`, `PageView`, `SampleTask`, and `Window::confirm` are currently C++-only APIs.
The existing C ABI layouts and versions remain unchanged.
The C#, NativeAOT, and Rust wrappers retain their existing surfaces.

#### Task Manager verification and measurements

The column update passed all 24 native tests on September 12, 2026, in 262.71 seconds.
The existing mouse resize remains in place, with a shared boundary cursor and keyboard commands.
The column tests passed 335 model assertions, 328 Task Manager desktop assertions, and 14 grid and sample lifecycle assertions.
They cover permutations through 64 columns, retained UIA identities, drag cancellation, keyboard input, narrow viewports, and snapshot persistence.
Native drag tests also exercise simulated 96-, 144-, and 192-DPI layouts. The actual Task Manager desktop run used 96 DPI.
The tests preserve the million-row virtualization, native EDIT, rendering, thumbnail, and ABI checks.
The isolated executable is `build\columns\Release\xui_task_manager.exe` (655,872 bytes).
The full test log is `build\columns\full-tests.log`.
The desktop smoke saves `build\columns\task-manager-columns.bmp` and `build\columns\task-manager-columns-narrow.bmp` under CTest.
These screenshots contain only the test-owned Task Manager window.

The final ARM64 Release verification completed on September 11, 2026.
The full compatibility command passed all 20 native tests in 184.94 seconds, including the previous 17 tests.
C# framework-dependent and NativeAOT tests each passed 17 assertions.
Rust passed six unit/layout tests, two compile-fail examples, Clippy, and formatting.
All five normal UIA clients and all five callback-failure UIA clients passed.

The new tests cover counter math, unavailable values, process identity, filtering, sorting, overflow, and bounded chart history.
A controlled busy fixture verifies CPU scaling, memory allocation, process exit, identity rejection, and confirmed termination.
The desktop smoke covers search, sort, focus, native EDIT Tab, header resizing, pages, pause, resume, and context commands.
Only a fixture started by the test can be terminated during verification.
The window test renders synthetic sources with 100,000 and 1,000,000 rows and checks bounded visible text requests.
It also checks delivery affinity, error retention, cancellation, and pause.
A delayed 1.5-second loader closed its window in approximately 48–60 ms without a UI-thread join.

Run these commands from the repository root:

```powershell
$tools = "C:\Program Files\Microsoft Visual Studio\2022\Preview\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
& "$tools\cmake.exe" --build build\arm64 --config Release --parallel 4
& "$tools\ctest.exe" --test-dir build\arm64 -C Release -R "xui_(task_manager|grid_window)" --output-on-failure
.\tests\phase4.ps1 -SkipMeasurements
.\tests\measure-task-manager.ps1 -Runs 3 -Screenshots -Output build\task-manager\final-measurements.json
.\build\arm64\Release\xui_task_manager_tests.exe --sample-bench
```

Run desktop tests and measurements separately. Concurrent windows can change focus and measurement results.
Final compatibility logs are copied to `build\task-manager\validation`.
Raw measurements, sampler timings, and screenshots are in `build\task-manager`.

The final measurement used three fresh processes, a 1080×780-DIP client area, 96 DPI, and 12 logical processors.
The outer window measured 1096×819 physical pixels. The earlier description incorrectly called the client dimensions the outer dimensions.
Each run observed 544 process rows. The renderer requested 10 visible or boundary rows.
The live interval was one second. Each live observation lasted five seconds.
Paused and minimized observations lasted three and two seconds, respectively.
Values below are medians. CPU is a percentage of total machine capacity, not one processor.

| Counter | Live, one-second interval | Paused |
| --- | ---: | ---: |
| Process private commit | 93.50 MiB | 93.66 MiB |
| Process private working set | 78.98 MiB | 79.27 MiB |
| Process total working set | 109.62 MiB | 109.93 MiB |
| Measured machine-capacity CPU | 0.233% | 0% |
| Additional paints per observation | 3–5 | 0 |

Live CPU ranged from 0.208% to 0.260%.
Each live run had eight threads, 249 handles, and one render target.
Every paused and minimized observation had zero measured CPU and zero additional paints.
These short observations do not establish a long-duration CPU or resource bound.
The first snapshot took 327.03 ms, with a 304.98–345.60 ms range.
Selection, scroll, and a forced full paint took 18.49 ms, with a 17.60–20.01 ms range.
Ten separate sampler-only collections took 28.50–63.80 ms for 544 processes.
That sampler-only process used 946,176–1,024,000 bytes of private commit and 56 handles.

The final `xui_task_manager.exe` is 588,800 bytes. `xui.dll` is 503,808 bytes.
The explorer executable is also 588,800 bytes, an increase of 64,512 bytes from the preceding explorer milestone.
Three fresh explorer runs used the same 60-file protocol and original window extent.
Explorer first-paint private commit was 28.79 MiB, compared with 28.78 MiB before these controls.
Its private working set was 14.34 MiB, compared with 14.40 MiB before.
First paint took 203.64 ms, compared with 199.85 ms before.
Warm selection, scroll, and a forced full paint took 17.91 ms. Idle caused no additional paints.
The larger Task Manager window is not a same-size memory comparison.
Earlier explorer experiments at larger extents also reached approximately 94 MiB of private commit.
The size investigation below locates the main increase in the graphics path.
It does not establish that this footprint is unavoidable or identify every allocator.

Screenshots cover standard and narrow layouts at 96, 144, and 192 injected window DPI.
They include Processes, Performance, selected Details, light colors, and high-contrast colors.
The physical monitor used 96 DPI. Injection tests layout; it does not replace mixed-monitor hardware testing.
The 192-DPI captures use a shorter window to fit the physical display.
Physical IME, mixed-monitor transitions, systems with more than 64 logical processors, and long-duration operation remain manual coverage.
External shell and clipboard gestures also remain manual coverage.
The sample does not claim full Windows Task Manager feature parity.

#### Retained-resource reduction: September 13, 2026

`PageView` creates native peers on first selection, not for every inactive page.
Visited peers remain alive, including native EDIT selection and undo.
The renderer releases hidden text layouts and unused scene geometry.
Its native composition buffer now matches the current visible regions instead of retaining its largest historical dimensions.

Three fresh processes per variant used the preserved baseline and the final ARM64 Release build.
Both variants used dark colors, 96 DPI, and identical physical client sizes.
No build or test ran during these measurements. No working-set trim occurred.

| Workload and client size | Private commit before → after, MiB | Private working set before → after, MiB |
|---|---:|---:|
| Gallery initial, 1040×731 | 95.30 → 94.61 | 80.96 → 80.38 |
| Gallery after two 46-page cycles and popup disposal, 1040×731 | 110.40 → 102.31 | 92.30 → 88.02 |
| Live Task Manager, 1080×780 | 94.13 → 94.08 | 79.87 → 79.80 |
| Explorer with 60 owned files, 924×641 | 32.08 → 32.02 | 15.95 → 15.83 |

These values are medians. The gallery reduction is 8.09 MiB of private commit.
The small Task Manager and Explorer differences do not establish a reduction.
Initial gallery peers decreased from 357 to 35. After both cycles, cached text layouts decreased from 594 to 28.
Visited native editors remain allocated. The change does not discard their undo history.

Warm navigation averaged 46.92 → 48.22 ms, with overlapping three-run ranges.
Warm navigation CPU time was 1265.63 → 1250.00 ms per cycle.
First visits averaged 38.73 → 47.24 ms because native peer creation moved from startup to first selection.
Initial process CPU time decreased from 312.50 to 218.75 ms.
Settled gallery and Explorer windows produced zero idle paints.

The gallery workload did not load media or browser engines. Separate native tests cover those engines and their shutdown.
The approximately 64 MiB allocation jump remained in that renderer configuration.
These measurements did not isolate ownership between Direct2D and the driver.
The full native suite passed all 36 tests, including C ABI, native composition, retained editor undo, and real host cleanup.
Raw samples, ranges, hashes, and commands are under `build\controls\memory-evidence`.
The complete report is `build\controls\memory-delivery.json`.

#### No-slowdown memory investigation: September 13, 2026

This investigation kept the retained-resource changes above and preserved new baseline binaries under `build\memory-tight\baseline`.
It did not accept another default renderer change.

At startup, a GDI-compatible hardware target saved 2.48–3.31 MiB of median private commit across the three native samples.
Six matched runs did not establish equivalent startup, first-use, and warm performance.
A separate six-run gallery comparison used normal input updates instead of extra forced updates.
Its uncertainty interval also did not establish performance equivalence.
The final build therefore uses the original hardware target configuration.

Other target properties and a raw flip-model probe retained substantial memory in fixed-size experiments.
The flip probe did not exercise application resize cycles or the later `explicit-swap-chain` implementation.
The earlier conclusion that flip-model rendering cannot solve resize retention exceeded the evidence.
The performance rejection concerned the GDI-compatible target, not the explicit-swap-chain branch.
The [dedicated report](docs/windows-gui-memory.md#audit-of-the-earlier-flip-model-claim) records this correction.
The measurements separate private commit, private working set, process CPU time, and process cycles.
They include per-interaction percentiles and paired-run uncertainty intervals.
The complete evidence and rejection reasons are in `build\memory-tight\delivery.json`.
The measurement script supports `-Performance` with `-Sizes @()` to retain each initial client size.

#### Explicit swap-chain branch comparison

The review compared Leonard Hecker's `explicit-swap-chain` commit `4c1f9ab` with its baseline `74bc1f8`.
Both exact source snapshots built as ARM64 Release.
Three matched resize runs per variant showed a transient private-working-set improvement at the first enlargement: **70.22 → 53.13 MiB**.
However, final private commit after shrinking and 30 seconds of idle was **99.80 → 102.32 MiB**.
The branch did not remove retained commit in this workload on the measured Qualcomm driver.
This result does not rule out other configurations or hardware.

Median synchronous fixed-resize time was **25.51 → 30.22 ms**, not an input-to-photon measurement.
No performance threshold stopped the comparison.
The branch passed 32/37 native tests, with unresolved capture and interaction failures.
Owned-window Graphics Capture showed the demo at five sizes.
The review also identified a source-level device-loss recovery gap and a change in default text antialiasing.
The [full comparison](docs/windows-gui-memory.md#matched-branch-results) separates these findings and their limits.
Commands, hashes, and raw results remain in `build\swapchain-review\benchmark-delivery.json`.
The review did not merge the branch or change production code.

#### Earlier window-size memory investigation

That investigation did not change the production renderer or sample binaries.
It did not identify a correction within the approaches it exercised.
That result did not rule out a different swap-chain implementation.
The extra 64 MiB reproduced in the graphics path without process-row storage or hidden controls.

The measured driver is `qcdx11arm64xum.dll`, version `31.0.133.1`.
At 96 DPI and a 780-pixel client height, one additional client pixel crossed the allocation threshold.
Each table entry is the median of three fresh processes after ten forced full paints.
All runs within each comparison used the same executable, controls, DPI, and dark theme.

| Fixture and physical client size | Private commit | Private working set |
| --- | ---: | ---: |
| Task Manager, 864×780 | 28.33 MiB | 13.71 MiB |
| Task Manager, 865×780 | 92.93 MiB | 78.54 MiB |
| Direct2D, one filled rectangle, 864×780 | 24.90 MiB | 10.69 MiB |
| Direct2D, one filled rectangle, 865×780 | 89.43 MiB | 75.34 MiB |

The rectangle fixture has no XUI controls, text, images, clipping layers, or worker.
Adding an axis-aligned clip did not remove the increase.
A blank XUI window and a Direct2D target with only `Clear` did not show the increase.
Fresh blank XUI windows at 1080×780 used approximately 25 MiB of private commit after full paints.
A native window without Direct2D used approximately 2.3 MiB.
The threshold also changed with height. A 1080×600 rectangle fixture stayed small, while 1080×640 crossed the threshold.

`VirtualQueryEx` identified the growth in `MEM_PRIVATE` regions.
Mapped-file and image commit stayed unchanged across the rectangle boundary comparison.
`HeapWalk` busy memory stayed near 3.1 MiB at first paint on both sides.
The report retains process commit and virtual-region totals separately. These counters are not interchangeable.

A process-local debugger traced only owned rectangle fixtures.
The larger filled target caused eight `NtGdiDdDDICreateAllocation` calls with approximately 8 MiB of additional commit each.
Their combined increase was 64.06 MiB.
The stacks pass through Direct2D, DXGI, Direct3D 11, the Qualcomm driver, and the Windows graphics allocation interface.
The smaller filled target and the larger clear-only target had no such increases.
Target creation alone stayed small. The increase occurred during the first filled frame.

The trace used local module information, not downloaded private symbols.
The debugger source is a local investigation artifact in the evidence directory.
Its breakpoints reject unknown ARM64 syscall instruction sequences.
Module names and offsets identify the allocation path. Nearby exported symbol names do not identify internal driver routines.
The trace does not identify the purpose of each driver-private allocation.
It does not establish the same threshold on other drivers, adapters, or DPI configurations.
The evidence does not support a framework text-cache, row-cache, or clipping-layer correction.

The additional commit persisted after shrink and after target, brush, and factory release.
Both traced sizes ended with zero targets, 215 handles, nine GDI objects, and three USER objects after window closure.
The large fixture still retained approximately 64 MiB more commit before process exit.
All owned fixtures exited. These observations do not establish long-duration resource bounds.
The investigation used no working-set trimming, software-rendering substitution, feature removal, or default-window reduction.

Two follow-up batches each used three fresh production processes at 1080×780 client pixels and 1096×819 outer pixels.
They observed 532–536 processes, ten visible rows, eight threads, 249 handles, and one target.
The executable hash stayed unchanged across both batches.
These medians compare measurements before and after validation, not different renderer versions.

| Counter | Before validation | After validation |
| --- | ---: | ---: |
| Live private commit | 93.50 MiB | 93.51 MiB |
| Live private working set | 79.12 MiB | 79.05 MiB |
| Live total working set | 109.70 MiB | 109.64 MiB |
| Machine-capacity CPU | 0.312% | 0.364% |
| First snapshot | 219.57 ms | 612.05 ms |
| Selection, scroll, and full paint | 17.30 ms | 17.93 ms |

Live CPU ranged from 0.260% to 0.416% across these six runs.
First-snapshot time ranged from 211.58 ms to 4665.84 ms, including one unexplained 4.67-second startup observation.
These short measurements do not establish a startup or CPU bound.
The paused CPU median was zero in both batches. The first batch included one 0.087% observation.
All paused and minimized observations caused no additional paints. All final-batch paused and minimized CPU observations were zero.

The sample remains 588,800 bytes. The DLL remains 503,808 bytes.
The sample SHA-256 stayed unchanged throughout the investigation.
There is no before/after memory-reduction claim because the production code did not change.
The historical measurements remain in their original files.
New measurements and allocation traces are in `build\task-manager\memory-investigation`.
The main evidence files are:

- `final-boundary-task-864.json` and `final-boundary-task-865.json`: fresh Task Manager comparison.
- `final-boundary-fill-864.json` and `final-boundary-fill-865.json`: fresh rectangle comparison with the same diagnostic executable.
- `lifetime-fill.json`, `lifetime-clear.json`, and `lifetime-xui.json`: resize history and resource lifetime.
- `allocation-fill-864-final.log`, `allocation-fill-865-final.log`, and `allocation-clear-865-final.log`: allocation-stack comparison.
- `production-repeat.json` and `production-final.json`: both production measurement batches.
- `summary.json`: medians, ranges, hashes, and allocation totals.
- `validation\compatibility.log` and `validation\phase4`: current build and compatibility results.

The ARM64 Release build succeeded. All 20 native tests passed in 190.23 seconds.
`tests\phase4.ps1 -SkipMeasurements` also passed both C# assertion suites, Rust tests, Clippy, and formatting.
All five normal UIA clients and all five callback-failure clients passed.
The original visual coverage remains applicable because the production renderer and executable did not change.

Run the size probe from the repository root:

```powershell
.\tests\measure-window-memory.ps1 -TaskManager -Runs 3 -Regions -Output build\task-manager\memory-investigation\repeat-production-sweep.json
& "$tools\cmake.exe" --build build\arm64 --config Release --target xui_window_memory_probe --parallel 4
foreach ($width in @(864,865)) {
    .\tests\measure-window-memory.ps1 -Executable build\arm64\Release\xui_window_memory_probe.exe -Arguments "task $width 780" -TaskManager -Runs 3 -Regions -Sizes "${width}x780" -Output "build\task-manager\memory-investigation\repeat-task-$width.json"
}
```

The first command measures resize history in the original production executable.
The optional fixture changes the initial window extent through a thread-local creation hook, before first paint.
It includes the unchanged Task Manager source.
The fixture is excluded from normal builds and is not a pass/fail benchmark.
Its other modes are `native`, `clear`, `fill`, `clip`, `rounded`, and `xui` (blank).
The JSON records actual client and outer dimensions, DPI, executable hash, allocation regions, mapped names, working sets, and paint latency.
`-CaptureOutput` also records fixture heap and resource counts.
`-ReleaseProbeTarget` releases graphics resources only in the raw Direct2D fixture modes.

### Application example

For example, this application updates a label through a button callback:

```cpp
#include "xui\application.hpp"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    xui::Window window({L"My application", {480, 240}});
    auto content = std::make_shared<xui::Stack>(xui::Axis::vertical);
    content->set_padding({24, 24, 24, 24});
    content->set_spacing(12);
    auto label = std::make_shared<xui::Label>(L"Ready");
    auto button = std::make_shared<xui::Button>(L"Update label");
    button->on_click([&] { label->set_text(L"Updated"); });
    content->add(label);
    content->add(button);
    window.set_content(content);
    return xui::Application::run(window);
}
```

Link the executable to `xui_windows`.
Include the manifest resource from `demo\xui.rc`, or supply an equivalent common-controls v6 and per-monitor-DPI manifest.
For MSVC builds that use this resource, set `/MANIFEST:NO`, as the gallery target does.

### Ownership and property updates

The window retains its content through `std::shared_ptr`. A control has one layout parent and a stable `Element::id`.
Build the tree before `Application::run`. Set properties and use callbacks on that same UI thread.
The window supports one active run on the thread. Each `Window` runs once, but separate windows can run in sequence.
The caller must not initialize COM as MTA.

Callbacks must not outlive the objects that they reference.
Reference captures in the example remain valid during the blocking `Application::run` call.
Strong captures that refer back to their own control can create an ownership cycle.
Window teardown clears the host invalidator and disconnects accessible providers.
Retained controls remain valid after window destruction.

Enabled state and checked state request paint updates.
Text and typography changes request layout for automatic sizes, or paint for preferred sizes.
Size limits, spacing, and padding request layout and paint updates.
The host combines pending updates. It has no animation timer or continuous render loop.
Text stays on one line unless the text contains an explicit line break.
An ellipsis marks text that exceeds the available width. The accessible name retains the full text.

Standard-control property setters do not call application action callbacks.
List selection operations call `on_selection_change`. View assignments call `on_view_change`.
Both callbacks observe the updated model. Item focus does not invoke the selection callback.
Pointer, keyboard, and UIA actions use the same activation behavior and enabled-state check.
Native committed text calls `TextInput::on_change`. IME preedit text does not call this callback.
Text input defaults to 1,024 UTF-16 units on Windows.
`TextInput::set_maximum_length` selects a limit from 1 to 32,767 units. Explorer address fields use 32,767 units.
The property setter preserves surrogate pairs at the limit and stops at the first NUL.
Callbacks can update other controls, change the theme, or request window closure.
`Window::focus(control, select_all)` requests native focus. The optional selection flag applies to text inputs.
Focus requests reject disabled controls, foreign controls, and closed windows.
Rejected select-all requests leave native selection and focus unchanged.
`Window::copy_text` requires an open window. Calls before startup or after closure throw before they open the clipboard.
The input-state methods on `Control` are backend boundaries, not application focus commands.
Startup errors and callback exceptions return a nonzero result. `Window::error()` supplies the error text.

### Address suggestions

Both explorer address fields provide folder suggestions. The search fields remain separate filters.
Typing does not navigate. A mouse click or Enter accepts the selected folder and submits the address once.
Tab accepts a selected folder without submission. Without a selected folder, Tab keeps its normal focus behavior.
Shift+Tab keeps reverse focus traversal. Escape first closes suggestions without a text change.
Another Escape restores the current explorer path or cancels a pending navigation.
Down can open suggestions without a text change. Ctrl+L alone does not enumerate folders or network drives.

The public C++ API is optional:

```cpp
#include "xui\application.hpp"
#include "xui\suggestions.hpp"

auto address = std::make_shared<xui::TextInput>(L"Folder address");
address->set_maximum_length(32767);
address->set_suggestions(xui::folder_suggestions());
address->set_suggestion_context(L"D:\\Documents");
```

The context is the absolute base folder for relative text. Each explorer pane updates this context from its active tab.
`set_text`, context changes, and source changes cancel old suggestions. Even a same-value `set_text` cancels an open dropdown.
These property updates do not open a dropdown. `set_suggestions(nullptr)` disables the feature.
The C ABI and language bindings do not expose this optional API yet.

`SuggestionSource::suggest` receives text, context, an explicit-request flag, and a cancellation function.
The worker calls the source outside the UI thread. Source implementations must not access controls or retain window callbacks.
The source can outlive the control during cancellation. Its destructor must not require the UI thread.
A source must bound its own work and allocations. Results contain at most 64 strings, each with at most 32,767 UTF-16 units.
The delivery boundary also enforces these limits. It limits status text to 256 units and excludes values that exceed the input limit.
Custom COM sources require their own apartment and correct marshaling. XUI does not transfer COM interfaces between threads for this feature.

#### Filesystem source and cancellation

The filesystem source calls `FindFirstFileExW` and `FindNextFileW` on the worker.
The search pattern contains the typed prefix. A second ordinal, case-insensitive comparison excludes DOS wildcard aliases.
Results contain directories, not executable files, URL history, or shell commands.
Supported input includes Unicode, spaces, folder names with dots, drive roots, absolute paths, relative paths, and both separator forms.
Dot and dot-dot expand against the current base folder. Quoted paths match the explorer navigation rules.
Environment references use `%name%`. Navigation and suggestions share `xui::expand_path_input` from `xui\path_input.hpp`.
The helper reads Unicode environment values through `GetEnvironmentVariableW`. It expands each reference once, without shell execution or process-directory changes.
Relative expanded paths use the current tab folder. Suggestions display complete expanded paths.
Unknown paired references report an error without a folder scan. A component such as `%USERPRO` reports an incomplete reference.
Unpaired percent signs inside names, a trailing `%`, and `%%` remain literal.
Other paired percent signs denote environment references, not literal filename characters.
Input and expanded output each have a 32,767-unit limit, including the terminator. Windows getter errors remain visible.
The suggestion worker reads environment values outside the UI thread. Navigation reads them when the address is submitted.
Drive-relative paths such as `C:folder` remain unsupported.
UNC share paths are valid. Server-only paths do not enumerate network shares.
An explicit request with empty text and empty context returns drive names without a scan of their contents.

An 80-ms timer combines successive edits. Only the focused input keeps an active timer.
An open dropdown retains its rows and size during the next request, then replaces the results in place.
Typing clears the old selection. Pending rows cannot accept keyboard or mouse actions for an outdated query.
The loading message appears only when the dropdown first opens, not between successive results.
One shared worker callback runs at a time. It retains one pending request, which the latest request replaces.
Generation checks reject old results after typing, tab changes, focus loss, or closure.
Each scan retains at most 64 paths and examines at most 4,096 matching entries.
A 100-ms elapsed-time budget stops the scan between filesystem calls. A status explains partial results or errors.
The retained subset is sorted, not the complete directory. More specific text narrows the next scan.
There is no directory cache, polling loop, thread per keystroke, or new Direct2D target.

One Windows filesystem call can block beyond the elapsed-time budget, especially on a network path.
Cancellation cannot interrupt that call. The UI remains responsive, but the latest request waits for the single worker.
Window closure revokes delivery without a UI-thread join. The worker owns its pending resources until the call returns.
The implementation does not start replacement workers to bypass a blocked network call.

#### Native input and popup

The real Windows EDIT retains text, caret, selection, clipboard, undo, IME, and native text accessibility.
The popup contains a native LISTBOX and a separate native status label. The status is not a selectable folder.
UIA exposes the native List and SelectionItem patterns, full path names, and selection state.
The native LegacyIAccessible default action accepts a folder and submits the address.
The popup does not take EDIT focus. It uses the complete field bounds and clamps its position to the monitor work area.
Focus loss, pane hiding, window movement, resize, minimize, and destruction close the popup.
IME composition closes suggestions and suppresses application shortcuts. Committed input can start a new request.
Disabled and read-only inputs do not open suggestions.

#### Browser mouse navigation

`Window::on_navigation` receives a `NavigationEvent` with a direction, a target control, and an optional mouse position in client DIPs.
The handler returns `true` to consume the event. This public C++ callback does not change focus or text selection.
The C ABI and language bindings do not expose this callback.
`WM_XBUTTONUP` dispatches Back for `XBUTTON1` and Forward for `XBUTTON2`.
The host consumes button-down and double-click messages without a second navigation. Parent notifications do not dispatch navigation.
Native EDIT, FileList, tab, and custom control peers use the same host callback.
`WM_APPCOMMAND` supports browser Back and Forward. Non-mouse commands identify the source control or the focused control.
The explorer activates the target pane and uses its active tab history. Unavailable history causes no folder request.
Alt+Left and Alt+Right retain their existing keyboard behavior.

The ARM64 Release build in `build\navigation` passed all 24 native tests, including the existing flicker, image, and Shell thumbnail tests.
Environment coverage includes UIA submission of `%SystemRoot%\System32`, quoted Unicode paths, unknown references, bounds, and expanded suggestion names.
Mouse coverage uses posted or sent Win32 messages on owned windows, not physical mouse hardware.
It covers both panes, native EDIT, search, FileList, tabs, root coordinates, application commands, unavailable history, and query restoration.
The checks also cover one request per click and unchanged EDIT focus and selection.
The build and regression logs are `build\navigation\build.log` and `build\navigation\regression.log`.

The list and status use the application text and background colors, including system high-contrast colors.
Windows retains control of the selection highlight, scrollbar, and border appearance.
The popup is not a pixel-identical copy of a particular Explorer version.

The Shell APIs were considered:
[IACList::Expand](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-iaclist-expand)
requests candidates,
[IACList2::SetOptions](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-iaclist2-setoptions)
controls the source, and
[IAutoComplete2](https://learn.microsoft.com/en-us/windows/win32/api/shldisp/nn-shldisp-iautocomplete2)
controls Shell autocomplete behavior.
These contracts do not establish bounded filesystem latency or cancellation for a slow UNC request.
XUI therefore uses the asynchronous source and native popup described here, not `IACList2` or `IAutoComplete2`.
This choice does not establish which implementation any particular Explorer version uses.

### Content sizes and constraints

Labels, buttons, and toggles use automatic content sizes by default.
DirectWrite supplies glyph widths and line heights through the `Control::set_text_measurer` backend boundary.
The core has no Windows dependency and does not estimate glyph widths.
Headless callers can supply a `TextMeasurer`. Without a measurer, controls use their original preferred sizes.

Each native label, button, and toggle retains one text layout.
Text or typography changes replace that layout. Hover, focus, scrolling, and paint reuse it.
DPI changes reuse DIP measurements and update the native input font.
Window teardown removes measurer callbacks before it releases the renderer.

| Method | Sizing rule |
| --- | --- |
| `set_auto_size(true)` | Use content size for labels, buttons, and toggles |
| `set_preferred_size({width, height})` | Disable automatic size and retain the original Stack allocation rules |
| `set_fixed_size({width, height})` | Set the preferred size and both limits to the supplied size |
| `set_minimum_size({width, height})` | Set the minimum desired size |
| `set_maximum_size({width, height})` | Limit both the desired size and the arranged size |

Parent constraints take precedence over minimum and fixed sizes. No child can force an overflow from a smaller allocation.
A new minimum raises a smaller maximum. A new maximum lowers a larger minimum.
`set_auto_size` does not clear limits. Zero minimum and infinite maximum values remove the limits.
`TextInput`, `FileList`, and `ScrollView` retain their preferred viewport sizes.

A Stack measures non-flex children first, in order, against the remaining main-axis space.
Flex children share the remaining main-axis space. On the cross axis, children stretch up to their maximum size.
Maximum limits do not redistribute unused flex space.
In an unbounded main axis, flex children use their natural desired size instead of an infinite share.
A ScrollView gives its content a bounded width and an unbounded height.
This model has no CSS cascade, automatic wrapping layout, or reactive property graph.

This composition gives the button its content width and the form the remaining height:

```cpp
auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
auto form = std::make_shared<xui::Stack>(xui::Axis::vertical);
form->set_spacing(12);
form->add(std::make_shared<xui::Label>(L"Workspace"));
form->add(std::make_shared<xui::TextInput>(L"Workspace name"));
auto actions = std::make_shared<xui::Stack>(xui::Axis::horizontal);
actions->add(std::make_shared<xui::Button>(L"Apply preferences"));
form->add(actions);
auto viewport = std::make_shared<xui::ScrollView>(form, L"Workspace preferences");
root->add(viewport, 1);
window.set_content(root);
```

### Retained scrolling

`ScrollView` accepts one retained `Element`, including a nested Stack.
Its content has one layout parent. Null content, duplicate ownership, and cycles are invalid.
`offset`, `extent`, and `maximum_offset` use DIPs.
`set_offset`, `scroll_by`, and `reveal` clamp the offset to the content range.
The viewport reserves 12 DIPs for its vertical scrollbar. A larger viewport clamps obsolete offsets automatically.

The mouse wheel follows Windows wheel settings. The scrollbar supports thumb drag and track paging.
With viewport focus, arrow keys, Home, End, Page Up, and Page Down move the viewport.
Page keys also work from ordinary descendants. Native input retains its text-editing keys and IME path.
Tab, Shift+Tab, `Window::focus`, and UIA focus reveal the target through ancestor viewports.
A disabled viewport rejects input and disables descendant actions.

The shared renderer clips content against every ancestor viewport.
Each viewport also owns a native parent HWND. Windows clips child EDIT and caption pixels against that parent.
Nested native fields and captions paint after the shared frame. This order prevents the transparent viewport from erasing native text.
A geometry override supplies clipped native EDIT bounds and offscreen state to UIA.
The override preserves the native accessible name, value pattern, and available native text patterns.
It does not replace editable text, selection, undo, or IME.

The viewport exposes UIA `ScrollPattern`, scroll percentages, viewport fraction, enabled state, and scroll-property events.
Custom descendants expose `ScrollItemPattern` for reveal without selection or focus.
Custom controls and embedded FileList rows expose clipped bounds.
The scrollbar has no separate UIA element.

ScrollView does not virtualize arbitrary content. All its controls and native peers remain in memory.
`FileList` remains the virtualized choice for large collections. ScrollView does not create a control for each FileList row.
The gallery has 17 searchable examples, including measured labels, native fields, disabled controls, and a scrollable preferences form.
F6 cycles dark, light, and explicit high-contrast colors outside grids. Within a grid, F6 selects the header.

### Input and accessibility

Buttons and checkboxes support hover, pressed state, pointer capture, drag-out cancellation, and capture loss.
Window deactivation and cancellation also cancel pending presses.
Tab and Shift+Tab move focus through enabled controls. Space activates a focused button or checkbox on release.
Enter activates a focused button. Enter does not change a checkbox.
The native EDIT retains Windows text selection, undo, clipboard, IME, and native UIA text behavior.
The bridge keeps activation on the top-level window when the native UIA proxy activates the EDIT HWND.
External UIA focus restores a minimized host and leaves keyboard focus on EDIT.
This path does not replace the native Value or Text providers.
Windows foreground-activation restrictions still apply.

The bridge keeps the composition guard active through native `WM_IME_ENDCOMPOSITION` processing.
It then publishes the committed value. Repeated end notifications do not repeat an unchanged text callback.
Same-DPI theme changes reuse the native font. DPI changes replace the font without replacing the EDIT HWND.

Custom controls expose UIA text, button, or checkbox roles.
Their accessible names track the public names. Their automation IDs use the stable element IDs.
Custom control fragments expose explicit focus, Invoke, and Toggle actions where applicable.
The HWND adapters supply native tree placement. Native EDIT keeps its own provider identity and a preceding native label.
The backend publishes name, enabled, focus, and toggle changes, plus button invocation events.
Retained providers reject actions after window teardown.
Lists also publish scroll percentage, viewport fraction, scroll availability, selection, and item-focus property changes.
Unchanged properties do not produce framework property events.
Enabled high-contrast checkmarks use the selection-text color. A pressed focus outline also uses that color.

### Virtual lists and asynchronous delivery

`FileList` derives from `Control`. A `Window` accepts it beside labels, buttons, toggles, and text inputs.
The native backend creates one list window, not one control per row.
The application supplies data and actions. It does not supply paint callbacks or accessibility providers.

This example uses the same public APIs as the real browser:

```cpp
#include "xui/application.hpp"

int run_results() {
    xui::Window window({L"Results", {600, 480}});
    auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto search = std::make_shared<xui::TextInput>(L"Filter");
    auto results = std::make_shared<xui::FileList>(L"Results");
    root->add(std::make_shared<xui::Label>(L"Available files"));
    root->add(search);
    root->add(results, 1);
    window.set_content(root);
    auto task = window.create_view_task(
        [](const xui::CancelCheck& cancel) {
            auto items = std::make_shared<const std::vector<xui::FileItem>>(
                std::vector<xui::FileItem>{
                    {1, L"Alpha.txt", L"C:\\Data\\Alpha.txt", false},
                    {2, L"Beta.txt", L"C:\\Data\\Beta.txt", false}});
            return xui::SourceResult{xui::FileSnapshot::build(items, cancel), {}};
        },
        [results](xui::ViewResult result) {
            if (result.view) results->set_view(std::move(result.view));
        });
    search->on_change([task](const std::wstring& query) { task->request(query); });
    task->request(L"");
    return xui::Application::run(window);
}
```

The task owns one worker and a bounded result mailbox. The window waits for a shared task event without polling.
The loader runs off the UI thread. Result callbacks run on the window thread.
Query changes cancel obsolete filters. `request(query, true)` also cancels and reloads the source.
Only the latest generation reaches the callback. `generation`, `applied_generation`, and `busy` expose task progress.
The loader must own its data and honor `CancelCheck`. Shared captures keep source services alive until cancellation finishes.

Retain the task handle while the task is necessary. Handle destruction, `cancel`, and window closure revoke delivery.
Cancellation is permanent. Later requests return zero.
`Window::close` revokes tasks immediately, including during a result callback.
The backend joins cancelled workers on a cleanup thread. Window closure does not join the worker on the UI thread.
Result delivery and list replacement also arrange off-thread snapshot disposal.
Retained controls keep their model after closure. Their final snapshot disposal uses the same cleanup facility.
Process exit waits for cleanup. A loader that ignores cancellation can still delay process exit.

### Images and resource limits

`include\xui\image.hpp` provides the Windows image API. Link the application to `xui_windows`.
The gallery contains a native path field, Load image and Unload image buttons, and a reusable image preview.
The thumbnail sample uses the same `Image` control. It has no application drawing calls or private backend includes.

```cpp
#include "xui\image.hpp"

auto image = std::make_shared<xui::Image>(L"Workspace photograph");
image->set_preferred_size({320, 180}); // Layout size in DIPs.
image->set_source(L"C:\\Pictures\\workspace.jpg", {640, 360}); // Decode box in physical pixels.
content->add(image);

// Later, on the UI thread:
image->reload(); // Read the file version again.
image->unload(); // Cancel pending work and clear the source.
auto usage = xui::ImageResources::statistics();
xui::ImageResources::clear_unused();
```

`ImageSize` contains unsigned 32-bit `width` and `height` fields.
The decoder fits the first frame inside this box and preserves its aspect ratio.
The decoder does not enlarge small source images. The renderer centers and scales the result inside the arranged control.
The default decode box is 192 by 144 pixels. The decode box is independent of the layout size and monitor DPI.
For a different physical resolution, call `set_source` with a different box.

`source`, `display_pixels`, `revision`, `status`, and `error` expose the current request state.
Resource statistics are thread-safe. Image control properties still belong to the UI thread.
`ImageStatus` has `empty`, `loading`, `ready`, and `error` values.
`ready` means that decoded pixels are available. The next frame uploads those pixels.
An upload error changes the state to `error`. The error text also appears in the accessible image name.
The control exposes UIA Image semantics and ancestor `ScrollItemPattern`. It does not advertise a keyboard action.

The backend starts a request only for a visible, attached image.
A fully clipped image releases its request, pixels, and bitmap on the next host update.
Its source remains available for a later reveal. Its state becomes `empty` until that reveal.
`unload` also clears the source. Property changes take effect through the normal coalesced host update.
A source revision and a cancelled mailbox prevent an obsolete completion from replacing a recycled tile.
Only the UI thread changes control state or uses Direct2D.

One process-wide WIC worker initializes COM as MTA and owns its WIC factory.
The worker starts with the first WIC image request. Text-only browser folders instead use the separate Shell worker.
File access, path resolution, metadata queries, and WIC operations stay on this worker.
The worker checks cancellation before decoding, after metadata, before allocation, before pixel conversion, after pixel transfer, and before delivery.
WIC codecs can still decode the full source internally. A codec call or filesystem driver can ignore cancellation until that call returns.

The shared service retains at most two active jobs and 64 queued jobs.
Only one WIC job and one Shell job can run at a time.
New requests remove cancelled queued jobs before the queue-limit check.
A full queue returns an error. It does not start another worker or silently use a different image.
Window closure cancels delivery and clears its image references without a worker join.
The workers and their shared service object last until process exit. Windows reclaims them without a C++ shutdown join.
Repeated windows do not create retired decoder threads or an unbounded shutdown queue.

| Limit | Policy |
| --- | --- |
| Owned decoded pixels | 8 MiB across the process, including in-flight reservations |
| Controlled bitmap estimate | 8 MiB across the process, including upload reservations |
| Decoded cache | At most 128 entries, least-recently-used eviction of unpinned entries |
| Bitmap cache | At most 128 entries per target, subject to the shared byte limit |
| Requests | One WIC worker and one Shell worker, two active jobs, and at most 64 queued jobs |
| Output box | Each dimension must be between 1 and 1,024 pixels |
| Source dimensions | At most 16,384 per axis and 16,777,216 pixels in total |
| Encoded file | Nonempty, at most 32 MiB |
| Request path | At most 32,767 UTF-16 units, with no embedded NUL |

Invalid output dimensions and invalid path strings throw `std::invalid_argument` from `set_source`.
File, codec, queue, pixel-budget, and bitmap errors produce an error state.
The backend checks dimensions with 64-bit arithmetic before multiplication or allocation.
If pinned resources fill a budget, unload other images.
Then call `reload` on the image that failed.
The backend never substitutes a success result at a lower requested resolution.

The cache key includes the normalized final path, volume and file identity, last-write timestamp, file length, source kind, and requested decode box.
Every new request opens the file and checks that key on the worker.
The open handle denies concurrent writes during metadata access and decoding.
Different sizes have separate cache entries. Identical keys share immutable premultiplied BGRA pixels and one bitmap per target.
`reload` checks the key again. It is not a file watcher or a content-hash check.
Changes that preserve file identity, length, and timestamp can reuse cached pixels.

Pixel reservations cover the output allocation before the worker allocates it.
The pixel counter includes decoded cache entries, pending results, and live controls without double counting shared resources.
Bitmap reservations cover `CreateBitmap` before the upload completes.
The bitmap counter uses width times height times four bytes. It is a controlled estimate, not measured GPU residency.
The counters exclude allocator overhead, file paths, WIC buffers, encoded data, Direct2D internal storage, render targets, and driver allocations.
These limits are not a process-memory ceiling.

Visible controls pin their decoded resources. Cache eviction cannot release those resources.
`clear_unused` removes unpinned decoded entries. The renderer removes bitmaps that no visible peer retains.
Window closure clears the decoded cache and releases its target-owned bitmaps.
Device loss releases all bitmaps for that target. The next frame recreates them from retained pixels without another file decode.
Settled images have no timer, animation loop, or periodic repaint.

The current image path does not apply EXIF orientation, ICC color management, or animation.
The sample enumerates PNG, JPEG, BMP, GIF, and TIFF extensions.
Installed WIC codecs determine actual format support. Automated fixtures cover PNG and malformed BMP data.
Real codec cancellation, physical GPU loss, mixed-monitor image quality, and screen-reader speech still need manual coverage.

### Foundation controls

Include `xui/foundation.hpp` for the foundation controls.
Property setters do not call application callbacks. Input actions do.
Invalid identities, nonfinite numbers, invalid bounds, and invalid timing values throw `std::invalid_argument`.

| API | Contract |
| --- | --- |
| `RadioGroup` | Stable `ChoiceItem` IDs, one enabled selection, arrow navigation, prefix selection, and virtual UIA SelectionItem children |
| `ComboBox` | Committed ID separate from popup preview; Enter commits, Escape cancels, and optional editing uses a native EDIT |
| `Popup` | Arbitrary retained content, anchored placement, nested dismissal, initial focus, focus return, and generation checks |
| `Control::set_help_text` | Delayed hover/focus help and UIA HelpText; the tooltip never receives focus |
| `Button::set_behavior` | Momentary, repeat, toggle-action, or dropdown behavior; toggle actions expose UIA Toggle |
| `SplitButton` | Independent primary and dropdown Buttons with separate names, callbacks, and keyboard targets |
| `NumericInput` | Native text entry, locale parsing, finite bounds, step actions, visible invalid text, UIA Value and RangeValue |
| `RangeInput` | Horizontal/vertical input, explicit reversal, pointer capture, small/page steps, preview/change/cancel callbacks, UIA RangeValue |
| `Expander` | Expand/collapse semantics, hidden-child inactivity, and focus repair to the header |
| `Progress` | Read-only determinate values, static indeterminate/unknown states, paused/error states, and a capacity-meter recipe |

Choice controls accept up to 4,096 items. They use one peer and virtual accessible children, not one peer per choice.
Use `ItemsView` for larger sources.
An editable ComboBox does not infer an ID from arbitrary text. Handle `on_edit` separately from `on_change`.
`NumericInput::set_locale` selects parsing and formatting rules. Invalid text stays visible and leaves the last valid value unchanged.
`NumericInput::step` restores valid formatted text. Native EDIT retains selection, undo, caret, and IME ownership.
`RangeInput::on_preview` reports drag values without committing them. `on_change` reports accepted values.
Input cancellation calls `on_cancel` after it clears the preview. Property-driven cancellation remains silent.

```cpp
auto size = std::make_shared<xui::RangeInput>(L"Item size");
size->set_range({16, 256, 8, 32});
size->set_value(64);
size->set_orientation(xui::Axis::vertical);
auto popup = std::make_shared<xui::Popup>(size, L"View options");
popup->set_preferred_size({120, 240});
// window and anchor must remain valid when this action runs.
anchor.on_click([&window, &anchor, popup] { window.show_popup(popup, anchor); });
```

`Window::show_popup(popup, anchor, initial_focus)` requires an active retained anchor in that window.
The default focus is the first eligible content control.
Nested popups must anchor in the top popup; the maximum depth is eight.
Tab traversal stays in the top popup. Escape dismisses it.
Outside input, focus departure, hide, disable, or window closure cancels affected popups.
`Window::dismiss_popup` accepts an explicit dismissal reason.
Before callbacks run, closed popups have stale generations. Use `Popup::current(saved_generation)` to reject late provider results.
Applications must also cancel their own queries and avoid strong callback ownership cycles.

Popups stay inside the intersection of the client area and monitor work area. They cannot extend outside the application window.
They share the root Direct2D target. Open content creates normal child input/UIA peers, including native EDIT/caption peers where needed.
Dismissed peers are released after input dispatch. Retaining the public Popup does not retain its closed native peers.
Native text is composed before `EndDraw`; popup clipping also masks underlying native fields.
Tooltips use one pending one-shot timer and no extra HWND or target. Hidden tooltips have no timer.
Indeterminate progress is deliberately static. This batch has no progress ring or progress animation.
Capacity meters show used/total text without implying an active task.

### Virtual collections and adaptive layout

Include `xui/collections.hpp` for `CollectionSelection`, `ItemsSource`, `ItemsView`, `TreeSource`, and `TreeView`.
Include `xui/adaptive_layout.hpp` for `Grid`, `Wrap`, and `AdaptiveLayout`.
These APIs are C++-only. They do not add a native window or render target for each item.

`CollectionIndex` supplies a count, a stable `ItemKey`, and identity lookup.
`ItemKey` contains an ID and a version. `RowKey` is a compatible alias.
Sources are immutable and thread-safe. Identity lookup must not scan all rows.
`ItemsSource::item(index)` supplies primary text, secondary text, an icon, optional progress, and an optional inline action.
The shared row renderer requests only visible content. It omits secondary text and inline buttons when the available space is too small.

`ItemsView` supports list, tile, and grouped presentations through `set_presentation`.
Groups describe ordered, nonoverlapping source ranges. Group IDs must not collide with item IDs.
Group headers receive focus but do not join item selection. Filtered select-all includes data in collapsed groups.
Collapse changes a small range projection, not an array of item controls.
Hidden selected and focused IDs remain in the selection model.
Arrow input repairs a hidden focus location without activating its item.
Source replacement must preserve the identity namespace or supply new versions for reused IDs.

```cpp
auto items = std::make_shared<xui::ItemsView>(L"Results");
items->set_items(filtered_source, full_source);
items->set_presentation(xui::ItemsPresentation::tiles);
items->set_item_size({180, 56});
items->set_select_all_scope(xui::SelectAllScope::filtered);
items->on_action(open_inline_details);
```

`CollectionSelection` separates focus, anchor, and membership.
Ctrl+click and Space toggle membership. Shift extends a range. Ctrl+arrows move focus without replacing membership.
A tile drag selects a rectangle. Escape cancels the drag.
F2 invokes the focused inline action without activating the row.
Ctrl+A uses the configured `SelectAllScope`.
Full-source scope requires an explicit index that contains every displayed identity.

Ranges and rectangles retain an immutable index plus compact bounds.
Select-all uses one term. Exceptions add point terms.
The model rejects more than 4,096 terms without changing the selection.
Replacement selection releases earlier terms.
Large header summaries never scan the source.
`CollectionIndex::contains_all` can prove domain containment across reordered or filtered snapshots without enumeration.

An explicit full-source index also supplies that containment contract.
Without a containment proof, a large selection from another snapshot has a conservative mixed summary.
Individual membership remains exact.
UIA Selection returns at most 256 identities. Larger or uncounted selections return `UIA_E_INVALIDOPERATION`, not a truncated selected array.
UIA clients can use ItemContainer, SelectionItem, and VirtualizedItem to inspect or reveal individual items.
The fragment tree contains visible rows and required tree ancestors, not every source row.

`TreeView` has a separate hierarchy contract.
`TreeSource::roots` supplies a virtual root index. `has_children` uses cached, nonblocking data.
Right expands a branch or enters its first child. Left collapses a branch or selects its parent.
Collapse retains hidden selection and repairs focus to the collapsed ancestor.
The tree retains at most 4,096 branch records and permits at most 128 expansion levels.
Closed branches retain cached children until source replacement or tree destruction.

```cpp
tree->on_request([weak_tree, start_query](xui::TreeRequest request) {
    // start_query owns worker execution and UI-thread delivery.
    start_query(request, [weak_tree, request](auto children, auto error) {
        if (auto owner = weak_tree.lock())
            owner->complete(request, std::move(children), std::move(error));
    });
});
```

Applications start their own asynchronous work. They must deliver `complete` on the UI thread.
Requests carry an owner-specific cancellation token and generation.
Collapse, cancellation, focus departure during a pending request, disable, hide, source replacement, and owner closure cancel affected work.
Late delivery returns `false`. Error text stays visible and accessible. Right retries a failed branch.
Source methods must not perform filesystem or network work on the UI thread.

`Grid` supports fixed, automatic, and weighted tracks, cell spans, gaps, padding, and child size constraints.
`Wrap` derives its column count from available width and measures each retained child.
`AdaptiveLayout` retains one navigation subtree and one content subtree.
Wide layouts place them side by side. Compact layouts stack them or display navigation over the content.
`set_compact_navigation(CompactNavigation::overlay)` selects the nonmodal overlay recipe.
`set_navigation_open` controls that compact overlay. Escape from navigation closes it.

Inline and overlay transitions keep the same controls, selected IDs, and focused navigation peer.
The overlay uses the root target and masks native fields under its rectangle.
It is client-bound and is not an anchored popup or a modal dialog.
Nested retained popups still use the separate `Window::show_popup` contract.

DataGrid adds `filterable` and `checkable` flags at the end of `GridColumn`.
Existing `GridSource` implementations require no new methods.
Check columns represent the shared row selection, not independently editable Boolean data.
Header checks select or clear all rows under the configured scope. Indeterminate state represents mixed or uncounted membership.
Column filters, sort state, and check flags follow source column identity through resize and reorder.

`set_filter` and `set_checked` are silent setters.
`filter` starts an external query through `on_filter`. Its `GridFilterRequest` contains all filters, a generation, and a cancellation token.
`complete_filter` accepts only the current request. Direct source replacement, cancellation, and owner closure invalidate pending requests.
`on_filter_open` lets an application compose a native text editor in a retained popup.
The gallery filter accepts `even` or an empty string and uses synthetic data only.

F6 switches between grid rows and headers.
F4 selects the sort, filter, or check part of a header. Enter or Space invokes that part.
Header sort exposes Invoke. Header filter exposes Value and Invoke. Header and cell checks expose Toggle.
SelectionItem remains separate from Toggle. UIA focus never replaces row selection.
All UIA mutation uses bounded action mailboxes on the UI thread.

Collection tests cover million-row sources, bounded selection terms, visible content queries, cancellation, stale results, and layout identity.
Native tests cover real Win32 input, external-process UIA, repeated popup cycles, native overlay clipping, target recovery, and zero idle paints.
The theme matrix uses dark, light, high contrast, and injected 96/144/192 DPI.
Physical monitor changes and screen-reader speech remain manual checks.

### Command and navigation controls

`xui/commands.hpp` supplies `CommandSet`, `CommandMenu`, `CommandSurface`, `CommandBar`, and `CommandBindings`.
The older `Commands` class and flat native `MenuItem` interface remain available.
An immutable command snapshot contains stable IDs, parent IDs, labels, actions, enabled states, optional checks, icons, and shortcut hints.
A pin has its own label and callback. It never calls the primary action.

`Window::show_commands` opens a retained menu or searchable palette.
The palette uses native EDIT for committed text and IME.

The palette opens with focus in its search field and shows an inline search prompt.
The title, close button, rounded frame, soft shadow, and keyboard footer distinguish the popup from the page.
The search field fills the available width. Its icon and padding also accept clicks to focus the editor.
Pointer hover highlights enabled command rows without changing search focus or keyboard selection.
A click runs a command, opens a submenu, or invokes its separate pin action.
Clicks on section headers, disabled commands, and separators leave the palette open. Outside clicks dismiss it.
The close button and Escape dismiss the palette and restore the previous focus.
Switching to another application leaves the palette and its query open.
Searchable palettes center horizontally in the available window area, with a stable search position near the top.
Their height follows the results, up to the configured popup height. Longer results scroll; empty results retain one message row.
Menus without search remain anchored to their invoking control.

Up and Down move the selection. Right opens a submenu. Left and Escape close the current submenu.
Enter runs the selected command. F2 runs only its pin action.
The primary action runs after dismissal. A pin leaves the menu open.
Missing filtered commands cause deterministic focus repair. Disabled commands and separators cannot receive menu selection.
Separators occupy 12 DIPs, rather than a full command row.
Disabled submenu parents also block their descendants.
Submenu UIA state follows popup expansion and collapse. Repeated expansion does not open a duplicate popup.

Shortcut hints are display text.
Only an explicit `CommandBindings::bind` registration creates a binding.
Applications pass keyboard events to `CommandBindings::invoke` from `Window::on_key`.
The command gallery registers Ctrl+O independently of its hint text.

```cpp
auto surface = std::make_shared<xui::CommandSurface>(L"Actions");
surface->set_commands(commands);
window.show_commands(surface, *anchor);

surface->on_query(start_application_query);
// UI-thread completion rejects canceled and foreign requests.
surface->complete(request, next_commands, error);
```

Command snapshots have at most 4,096 records and eight hierarchy levels.
Each label has a 1,024-code-unit limit. Each command has at most eight shortcut hints.
Queries have a 256-code-unit limit and one current cancellation token.
Pending queries keep the previous rows visible. Closure and direct source replacement invalidate the current token.
Each completion consumes its token once.
Applications own worker scheduling and UI-thread completion.
The toolbar has at most 64 records. Its overflow uses the same command IDs and actions.
Command snapshots own checked state. A toolbar click does not change that state independently.
Commands reuse virtual collection rendering and the bounded UIA action mailbox.
UIA exposes Menu, MenuItem, Invoke, SelectionItem, ExpandCollapse, and optional Toggle patterns.
Pins have separate Button providers. No native peer exists for each command row.

Use `CommandKind::section` for a labeled, non-interactive section header.
The header applies to subsequent records with the same parent until the next section header.
Each section needs a stable command ID and a label, but cannot have an action, check state, icon, or shortcut hints.
Section records cannot be parents. Use `CommandKind::submenu` for nested commands.
Filtering retains headers for sections with matching results and removes empty sections.
Section headers occupy 32 DIPs. Keyboard selection skips them, and UIA exposes them as headers without Invoke or SelectionItem patterns.
The command gallery includes two sections.

`xui/navigation.hpp` supplies `NavigationView`, `Breadcrumb`, `NavigationPane`, `LocationPicker`, and `ViewPicker`.
Breadcrumbs contain at most 64 stable path segments.
Overflow retains earlier segments. Arrow keys move between visible segment buttons.
The current segment exposes a current-location description.
Activation sends a navigation request. It does not silently change the committed path.

`NavigationPane` composes `ItemsView`, `Expander`, and static progress.
`LocationPicker` adds a native editor, command toolbar, and keyboard footer inside a retained popup.
`Window::show_location_picker` retains that composition and routes editor arrows to its virtual rows.
Navigation queries have a 1,024-code-unit limit and owner-specific cancellation tokens.
Hidden, collapsed, or closed navigation cancels its current query.
Sources supply cached rows and stable IDs. The framework performs no directory enumeration.

The explorer consumes command search, breadcrumbs, and a path-location picker.
The picker filters cached current-path locations. The existing address suggestions retain their separate asynchronous directory provider.
The path bar keeps its actions outside the native address band, including at narrow widths.
Paths with more than 64 components retain the first 63 and the current location, with an explicit gap marker.
Cancellation does not navigate or change the committed explorer location.
The gallery also composes inline and anchored quick access with `AdaptiveLayout`.
Its view picker changes the actual generic `ItemsView` presentation and row size.
It does not change the legacy explorer `FileList` presentation.

### Optional Shell and caption integration

`xui/shell_commands.hpp` separates discovery from invocation.
`ShellCommandSession` accepts a provider and rejects disabled, stale, canceled, and native-only actions.
Discovery does not invoke verbs. Explicit invocation consumes the session before the provider call.
Synthetic providers support tests without file operations.

The Windows provider owns `IContextMenu`, optional `IContextMenu2` and `IContextMenu3`, and its native menu.
Creation, discovery, invocation, and destruction belong to the same STA thread.
A selection has at most 256 paths with one Shell parent.
Discovery has a 4,096-item bound and an eight-level depth limit.
Native fallback forwards submenu, owner-draw, measurement, and mnemonic messages to the extension.
It preserves extension-owned icons, labels, checks, and shortcut text without custom interpretation.
Snapshots carry optional vector icons, native-icon presence, and up to eight shortcut hints.
Owner closure revokes the Windows provider. Calls from another apartment thread fail before COM dispatch.

`Window::show_shell_commands` opens the native fallback for an explicit user request.
The explorer exposes this command as **Shell commands (native fallback)**.
The gallery requires a user-supplied path for its real Shell menu.
Its default demonstration uses synthetic commands only.

CAUTION: Shell verbs can change or delete files. Only an explicit user choice invokes a verb.

Third-party COM calls can block, allocate outside XUI limits, or fail inside native code.
Cancellation revokes XUI results and actions. It cannot interrupt an extension call already in progress.
This service handles HRESULT failures. It does not isolate extension crashes in another process.
The explorer and gallery report service errors without closing their windows.
No periodic worker, query timer, or Shell verb discovery loop runs at idle.

`WindowOptions::custom_titlebar` defaults to false.
With this flag, `Window::titlebar()` supplies a retained `TitleBar` with the existing `TabStrip` and independent caption Buttons.
The Windows backend removes the standard caption area but retains resize styles and system commands.
Hit tests separate tabs, caption buttons, drag space, and resize borders.
The maximize region reports `HTMAXBUTTON` for Windows Snap integration.
The title remains the actual native window title.

The gallery enables this optional caption. `--system-titlebar` retains the standard caption.
The explorer retains its standard caption and committed-location title.
Caption buttons use Windows-style graphics instead of the rounded XUI button style.
Their 46-by-32-DIP bounds contain centered Windows caption glyphs, with no border or contrasting background at rest.
Hover and press states use rectangular fills. The close button uses red, and inactive windows use dimmed glyphs.
High-contrast mode uses system colors. Caption buttons retain DPI scaling, accessibility actions, and the root render target.
Tab navigation skips caption buttons. Alt+Space opens the native system menu for keyboard window commands.
Native tests open and cancel the system menu, check resize corners, and maximize and restore through UIA.
Physical monitor transitions, Snap flyout appearance, and screen-reader speech still require manual checks.

The command/navigation tests cover callback exceptions, host closure, public-window deletion, stale queries, and separate pin identity.
Nine native cases cover dark, light, and high contrast at injected 96, 144, and 192 DPI.
Each case checks stable USER resources across 24 popup cycles and one root render target.
Direct native print tests verify the popup region mask for EDIT and STATIC controls.
Full-window `PrintWindow` fixture images can still show underlying native child pixels.
They are not proof of live compositor occlusion. The gallery has separate popup captures.
The document batch resolves this check with an independent owned-window compositor capture.
This batch does not claim a committed-memory reduction.

The September 12 command/navigation matrix passed 12 of 13 tests.
One foundation target-recreation assertion failed. The subsequent six-test recheck passed, including foundation, collections, navigation, gallery, and explorer tests.
The assertion was not weakened.
See `build\controls\navigation-final-targeted.log`, `navigation-final-recheck.log`, and `navigation-delivery.json` for the earlier results.
The document delivery record contains the later live-frame evidence.

### Documents, dialogs, and color

Include `xui/documents.hpp` for these seven families.
Property setters are silent. Native user edits and semantic actions invoke callbacks.
`Control::set_visible` hides a control and its retained descendants.
Hidden native documents keep their last nonzero geometry. This avoids repeated calendar font allocation during page changes.

```cpp
auto notes = std::make_shared<xui::MultilineText>(L"Notes");
notes->set_maximum_length(65536);
notes->set_text(L"First paragraph\rSecond paragraph");
notes->on_change(save_notes);

auto dialog = std::make_shared<xui::ContentDialog>(L"Edit notes", notes);
dialog->on_validate(validate_notes); // Return an error message, or an empty string.
dialog->on_result(handle_result);
window.show_dialog(dialog, *anchor, notes.get());
```

`ContentDialog` uses the existing client-bound `Popup` and the root Direct2D target.
Owner controls remain disabled while the dialog is open. Tab stays within the top popup.
Enter invokes the default action outside multiline documents. Escape cancels after native composition ends.
Invalid content stays visible. Dismissal restores focus and revokes the old popup generation before result callbacks.
Validation callbacks can close or reopen the dialog. An obsolete validation result cannot close the new generation.
The UIA Window pattern reports modal state and supports Close.
Minimize, maximize, and process-idle waits are not operations of a client-bound dialog.
`Window::confirm` remains the separate native confirmation service.

`InlineStatus` has information, success, warning, and error severities.
Each severity has a visible symbol and an accessible prefix.
The action and dismiss button have separate callbacks. Errors use assertive live-region semantics. Other severities use polite semantics.
External UIA tests receive one live event per message change or re-show. Duplicate property writes remain silent.
Messages have a 4,096-code-unit limit. The badge recipe omits both buttons.
Actual screen-reader speech still requires a manual check.

`MultilineText` and `RichText` use the Windows `Msftedit.dll` RichEdit engine.
Windows owns composition, selection, caret movement, scrolling, clipboard operations, and undo.
`set_monospace(true)` selects Consolas for code. The default font remains Segoe UI.
The default document limit is 65,536 UTF-16 code units. The maximum is 1,048,576.
Paragraphs use `\r`. Setters normalize `\n` and `\r\n`, and reject null characters or unpaired surrogates.
`TextSelection` uses UTF-16 offsets. A property selection cannot split a surrogate pair.
Native surrogate-pair input publishes one complete value, not an intermediate half-character.
`TextCommand` supports undo, redo, copy, cut, paste, and select-all while a native peer is attached.
RichEdit retains at most 16 undo actions. Windows determines their byte cost.
Property changes replace text once per revision, not once per paint.

`RichText::set_runs` accepts at most 4,096 runs with bold, italic, underline, and explicit HTTP/HTTPS link targets.
A click or Ctrl+Enter requests a link callback. XUI never opens the target automatically.
Native edits retain formatting. The retained runs track surviving application-authored styles and link positions.
They are not a general RTF serialization format for arbitrary native formatting commands.
Unicode streaming explicitly selects plain text. RTF-looking strings remain literal text.
Paste accepts plain Unicode only. The OLE callback rejects embedded objects and drag/drop effects.
There is no RTF, HTML, image, or file importer in this API.

RichEdit supplies its own server-side UIA Text provider, including text ranges and font attributes.
Replacing that provider hides its Text pattern, so XUI leaves it intact.
Its accessible name follows `Control::name`. Its automation ID remains the native numeric control ID.
The custom `automation_id` override applies to the other document controls, not RichEdit.
Native focus HRESULTs are not proof of focus. Tests inspect actual focus and the disabled owner state.
Native EDIT and RichEdit remain Windows composition boundaries, not a new XUI TSF implementation.

`PasswordInput` defaults to 256 code units and permits at most 4,096.
`with_password` is the explicit application read boundary. Change callbacks carry no password value.
The native editor always keeps `ES_PASSWORD`. Copy, cut, and its context menu are disabled.
UIA reports `IsPassword`; value reads are empty or unavailable, including during reveal.
The default reveal policy is `never`.
`explicit_request` permits a separate noninteractive preview, while the native editor remains masked and keeps its real caret and selection behavior.
Focus loss or hiding removes that preview. It does not create an accessible plaintext value.
XUI erases the retained old value on replacement and destruction.
This is not a credential vault. Windows and application code can retain plaintext in process memory.

`DateTimePicker` has date, time, and calendar presentations.
The native date editor includes its calendar dropdown. Windows supplies locale formats and platform UIA behavior.
Values use local Gregorian fields, not UTC instants or time zones.
The supported year range is 1601–9999. Invalid dates and ranges that exclude the current value throw.
Date edits preserve the time fields. Time edits preserve the date fields.
Native calendar styling follows Windows, not the custom dark palette.
The native calendar background is initialized before bitmap composition, including unused margins.

`ColorPicker` stores unpremultiplied sRGB bytes in `RgbaColor`.
Four labeled numeric editors expose RangeValue and keyboard steps.
Fractional channel edits round to the nearest byte. Alpha changes the checkerboard preview.
Up to 16 swatches have distinct RGBA names and Invoke actions.
Swatch replacement reuses retained buttons. Color controls create no idle timer or image assets.

Native document pixels join the existing root frame before `EndDraw`.
The compositor test uses a uniquely colored underlying EDIT and STATIC, then opens a covering popup.
Four immediate root presentations contain no underlying ink. The native field still edits correctly after dismissal.
When foreground activation is blocked, the test uses Windows Graphics Capture with the owned HWND, not a desktop capture.
The corresponding full-window `PrintWindow` image still shows underlying pixels. It is a capture artifact, not the live compositor result.
The evidence is in `build\controls\documents-captures` and `build\controls\documents-delivery.json`.
Tests also cover native UIA, theme/DPI cases, target loss, callback closure, and repeated resources.
Physical monitor changes, interactive IME candidates, and screen-reader speech remain manual checks.
This batch adds no language bindings and makes no process-memory reduction claim.

### Vector scenes and offline maps

`include\xui\vector_canvas.hpp` defines the public retained scene API.
`VectorScene` owns immutable `VectorShape` records with nonzero, unique `ShapeId` values.
Paths contain straight segments. The rectangle helper creates four vertices, and the ellipse helper creates 64 vertices.
Closed paths use an even-odd fill. Open paths have no fill.
Colors use unpremultiplied sRGB components between zero and one.

Each shape has an affine transform and an optional axis-aligned clip.
The scene applies transforms once, at construction. Clips use canvas coordinates after the transform.
Stroke widths remain in DIPs after the transform. Rounded stroke ends and joins match the hit-test distance calculation.
Scene coordinates must be finite, with an absolute value of at most one billion.
Hit testing examines the topmost interactive shape first and applies the same clip.

```cpp
#include "xui/vector_canvas.hpp"
auto shape = xui::VectorShape::rectangle(10, {20, 20, 120, 80});
shape.fill = {0.1f, 0.4f, 0.9f, 1};
shape.name = L"Blue region";
shape.interactive = true;
shape.transform = {1, 0, 0.2, 1, 10, 0};
shape.clip = xui::Rect{0, 0, 320, 200};
auto canvas = std::make_shared<xui::VectorCanvas>();
canvas->set_scene(std::make_shared<const xui::VectorScene>(
    std::vector<xui::VectorShape>{shape}));
canvas->on_select([](xui::ShapeId id) { /* application selection */ });
```

A scene supports 4,096 shapes, 65,536 total vertices, and 256 interactive entries.
Names have at most 256 UTF-16 units. Strokes have a maximum width of 256 DIPs.
The root renderer caches geometry for at most eight scene snapshots. It creates no additional Direct2D render target.
Snapshot replacement normally causes paint invalidation. The first or last interactive entry also changes the list layout.
Selection retains a stable ID across scene replacement.

The named list below the scene is its semantic alternative.
It exposes virtual UIA SelectionItem children and ordinary keyboard navigation without one HWND per shape.
Its rows represent semantic elements, not the spatial bounds of the drawing.
Selection through the list changes the same scene selection and outline as a pointer hit.
Decorative shapes have no automation actions.

`include\xui\map_view.hpp` defines `MapView`, `GeoPoint`, `WorldPoint`, `MapMarker`, `MapPolyline`, and `MapOverlay`.
The map shows a Mercator graticule and application-authored coordinates.
It has no street basemap, commercial tiles, copied world assets, geocoding, routing, or implicit network access.
It is an offline coordinate map, not a mapping-service replacement.
The renderer uses `VectorCanvas` and the same root target.

Longitude wraps across the dateline. Latitude clamps to approximately ±85.051129 degrees.
Projection uses double-precision normalized world coordinates. Zoom clamps to the range zero through 20.
Drag or arrow keys pan the viewport. Plus and minus zoom, and Home resets the viewport.
`zoom_at` preserves the geographic point under its DIP anchor, except at the latitude clamp.
The viewport displays the nearest wrapped world copy of each marker.

`set_overlay` accepts at most 256 markers, 256 polylines, and 2,048 total polyline points.
Marker IDs occupy the nonzero lower half of the 64-bit ID range.
Selected marker IDs survive pan and zoom. The marker list supplies keyboard and UIA selection.
UIA help exposes the current latitude, longitude, zoom, and provider error.
The application supplies meaningful marker names, including coordinates when necessary.

`on_request` and `request_overlay` define the optional provider boundary.
Each `MapRequest` contains a generation, center, zoom, and owner-specific stop token.
Pan, zoom, source replacement, hiding, and `cancel_request` cancel the old token.
`complete` rejects foreign, obsolete, canceled, or duplicate responses.
The application runs provider work and marshals completion to the UI thread.
XUI does not create a provider worker or implement network tile loading.

### Native media and optional web content

`include\xui\runtime_hosts.hpp` contains platform-independent models. No public method exposes COM interfaces or native renderer objects.
`MediaPlayback` loads only an explicitly supplied absolute local drive path.
It rejects URL, UNC, and alternate-stream syntax. It never selects a file or starts playback automatically.
The Windows backend loads the system `mfplay.dll` only after `load_local`.
MFPlay is a legacy Media Foundation API, not DirectShow or a bundled codec library.

`play`, `pause`, `stop`, `seek`, `set_volume`, `refresh`, and `unload` operate on the real native player.
Volume ranges from zero to one. Seek uses seconds and clamps to the current duration.
The public seek limit is seven days. `refresh` reads position and duration without a periodic UI timer.
Native callbacks update playback state and errors through a bounded 16-event mailbox.
Source replacement revokes the old mailbox before player shutdown.
Windows owns codec selection, audio output, and the native video renderer.

The host renders video in a native child window.
It does not replace video with a thumbnail or capture each frame into the root renderer.
Windows codec allocations and noninterruptible native calls are outside XUI memory and cancellation bounds.
The API does not implement camera capture, DRM, streaming URLs, subtitles, or recording.
Camera access never starts as a side effect.

Separate `Button` and `RangeInput` controls provide accessible Play, Pause, Stop, Seek, and Volume actions.
The gallery demonstrates this composition and a live status element.
The native host itself exposes a named group, not unsupported transport patterns.
The generated WAV and uncompressed AVI fixtures contain only repository-authored samples.
They load and play through the Windows codec stack.

`WebContent` requires the opt-in build below and an installed Microsoft Edge WebView2 runtime.
The default build has no WebView2 dependency. An enabled build still creates no environment before an explicit load.
XUI never installs the browser runtime or copies a complete browser distribution.
The native browser owns DOM rendering, editing, and browser accessibility.
`focus_content` enters the browser. WebView2 moves focus back to the XUI traversal order at its boundary.

`set_profile_root` requires an explicit absolute local directory.
Each environment uses a unique owned child directory under that root.
`set_html` accepts at most 262,144 UTF-16 units of owned HTML.
XUI adds a restrictive content policy before the HTML. Inline scripts and styles are allowed, but network connections, frames, workers, and form submission are blocked.
`navigate` accepts only `about:blank` or an exact HTTPS origin from `set_allowed_origins`.
The allowlist has at most 16 entries. An origin contains a scheme and authority, without a trailing slash.
Navigation and ordinary resource requests receive the same origin check.
This policy is not a network firewall for arbitrary remote applications or the browser runtime itself.

New windows, downloads, and permission requests are blocked.
Developer tools, browser accelerator keys, and default context menus are disabled.
`evaluate` accepts an explicit application script and returns its JSON result or error.
At most eight scripts can be pending. Script text and results each have a 65,536-unit limit.
At most eight environment/controller creation requests can be pending across source changes.
Unload or source replacement discards pending script callbacks. Obsolete callbacks cannot restore an old controller or document.
`stop`, `reload`, and `unload` operate on the actual browser.

Hiding a runtime host, its page, or its window unloads its native resources.
A fully scrolled-out host also unloads. A later show does not resume the old source.
The application must issue another explicit load.
Native cleanup tracks each owned profile through asynchronous environment and controller creation.
It keeps a process handle for the owned browser until that process exits.
After window teardown, `Application::run` dispatches STA completion messages before COM shutdown.
This cleanup has a 30-second limit. A timeout returns an error instead of reporting successful disposal.
`BrowserProcessExited` reports completion for the browser and its associated runtime processes.
Cleanup removes each retired profile after its creation callbacks and that process collection complete.
At most eight locked retired profiles can accumulate per host before further loads report an error.
Applications can remove their dedicated profile root after all owned runtimes exit.
Tests remove only their own fixture roots.

Native media and web surfaces are not part of the root bitmap composition.
`Window::show_popup` rejects a retained popup while a native runtime is active.
Tooltips are suppressed while a media or web runtime is active. An adaptive retained overlay unloads the runtime.
This explicit boundary prevents false claims about `WM_PRINT` composition or popup occlusion.
Ordinary native parent clipping handles scroll viewports.
The document, EDIT, and RichEdit composition paths remain unchanged.

#### Enable WebView2 explicitly

Restore the pinned public SDK package from `integrations\webview2\packages.config`:

```powershell
$version = '1.0.2903.40'
New-Item -ItemType Directory -Force build\controls\packages | Out-Null
Invoke-WebRequest "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$version" `
  -OutFile build\controls\packages\webview2.zip
Expand-Archive build\controls\packages\webview2.zip `
  "build\controls\packages\Microsoft.Web.WebView2.$version" -Force
$sdk = (Resolve-Path "build\controls\packages\Microsoft.Web.WebView2.$version").Path
cmake -S . -B build\controls -DXUI_ENABLE_WEBVIEW2=ON "-DXUI_WEBVIEW2_SDK_DIR=$sdk"
cmake --build build\controls --config Release --target xui_gallery xui_hosts_window_tests
ctest --test-dir build\controls -C Release -R '^xui_hosts_window_tests$' --output-on-failure
```

The backend links the small SDK static loader for ARM64 or x64. The installed runtime remains a separate requirement.
No SDK or runtime download occurs during CMake configuration or normal application startup.
Without the feature flag or runtime, the web host displays an explicit error after a load request.
Owned-window Graphics Capture, rather than `PrintWindow`, supplies the native video and browser pixel evidence.

### Gallery and browser boundaries

The gallery has a searchable category catalog and 47 interactive pages built from actual XUI controls.
Each page includes its purpose, a C++ API excerpt, a Copy code action, and event output.
API excerpts use read-only, monospace code blocks with native text selection and copy support.
The blocks preserve indentation and line breaks. Long content scrolls within the block.
The examples cover input, typography, layout, scrolling, collections, tabs, split panes, images, charts, menus, and themes.
The grid calculates 100,000 synthetic rows without a retained row array.
The chart updates only on request. The file list uses synthetic fixtures.
The gallery adds no application-specific window procedure or drawing code.

The catalog uses `NavigationView`, not a data grid. Category groups contain the example rows.
Home and Appearance remain in the pinned header and footer. All examples remain searchable in the main section.
Search preserves the selected item identity. The page area displays a matching example or an empty state.

Search expands collapsed groups that contain matches. Groups without matches keep their previous expansion state.
Manual expansion or collapse during a search persists across query edits. An empty query restores the expansion states from before the search.
`NavigationView::item_matches` reports filter membership even when a collapsed group hides the item.

Previous and Next expand the pane and use visible example rows.
Ctrl+F expands the pane before it focuses the native search field.
The `navigation-view` page demonstrates nested groups, filtering, icons, badges, disabled items, and pinned shortcuts.
`NavigationView` is a C++ API. This addition does not expand the C ABI or language bindings.

```powershell
.\build\controls\Release\xui_gallery.exe
.\build\controls\Release\xui_gallery.exe --page combo
.\build\controls\Release\xui_gallery.exe --page popup --light
.\build\controls\Release\xui_gallery.exe --page items
.\build\controls\Release\xui_gallery.exe --page tree
.\build\controls\Release\xui_gallery.exe --page adaptive
.\build\controls\Release\xui_gallery.exe --page grid-extensions
.\build\controls\Release\xui_gallery.exe --page commands
.\build\controls\Release\xui_gallery.exe --page breadcrumb
.\build\controls\Release\xui_gallery.exe --page navigation
.\build\controls\Release\xui_gallery.exe --page navigation-view
.\build\controls\Release\xui_gallery.exe --page shell
.\build\controls\Release\xui_gallery.exe --page titlebar
.\build\controls\Release\xui_gallery.exe --page dialog
.\build\controls\Release\xui_gallery.exe --page status
.\build\controls\Release\xui_gallery.exe --page multiline
.\build\controls\Release\xui_gallery.exe --page password
.\build\controls\Release\xui_gallery.exe --page rich-text
.\build\controls\Release\xui_gallery.exe --page date-time
.\build\controls\Release\xui_gallery.exe --page color
.\build\controls\Release\xui_gallery.exe --page images --image "D:\images\sample.png"
```

Build this integration tree without replacing an existing `build\gallery` executable:

```powershell
$tools = 'C:\Program Files\Microsoft Visual Studio\2022\Preview\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
& "$tools\cmake.exe" -S . -B build\controls -G 'Visual Studio 17 2022' -A ARM64 `
  '-DCMAKE_GENERATOR_INSTANCE=C:\Program Files\Microsoft Visual Studio\2022\Preview' `
  -DBUILD_TESTING=ON -DXUI_DESKTOP_TESTS=ON
& "$tools\cmake.exe" --build build\controls --config Release
& "$tools\ctest.exe" --test-dir build\controls -C Release --output-on-failure
```

The 29 added pages build their examples on first use.
They cover foundation controls, collections, adaptive panels, command surfaces, navigation, documents, dialogs, color, and optional Windows integration.
Native tests cover dark, light, high contrast, and injected 96/144/192 DPI.
Physical mixed-monitor changes, interactive IME sessions, and screen-reader speech still require manual validation.

Use `Ctrl+F` to focus search.
Type a control name or category, then press Enter to focus the results.
Use arrow keys to select an example.
Press Enter to focus its controls.
Right-click the menu target, or use `Shift+F10`, to open the styled context menu.
The image argument fills the path field. Load image starts decoding.
The optional `--high-contrast` argument selects explicit high-contrast colors.

[Control families and roadmap](docs/control-roadmap.md) records the verified File Pilot and WinUI overlap.
It contains 25 completed family checkboxes. Web runtime integration remains an explicit build option.
The DataGrid extension, navigation recipe, optional Shell boundary, and optional custom caption are also implemented.
The research includes public sources, supplied images, and a limited live File Pilot capture.
File Pilot keyboard behavior and popup interaction remain unverified.
The gallery demonstrates the completed batches. It does not claim WinUI parity or completion of the backlog.

All three applications use `Window` for their native host.
`src\window_host.cpp` supplies COM initialization, the blocking message loop, focus traversal, child placement, and title-bar appearance.
The shared backend uses `Drawing`, theme tokens, and `NativeEditBridge`.
`src\controls.cpp` contains platform-independent behavior. `src\application.cpp` connects controls to Windows and drawing.
`src\control_accessibility.cpp` contains the control providers.

`src\list_peer.cpp` supplies list drawing, pointer input, keyboard navigation, scrolling, and the adapter for existing list providers.
`src\accessibility.cpp` remains the single implementation of the virtual-list providers.
Provider actions include the control identity and item identity. A recycled HWND cannot accept an old provider action.
`src\async.cpp` supplies reusable task delivery and cleanup.
Each window owns one Direct2D target, brush, DirectWrite factory, and set of immutable text formats.
The host draws labels, images, buttons, toggles, viewports, and visible list rows in one frame.
Transparent child HWNDs retain input and UIA behavior. Native EDIT and caption HWNDs supply current pixels through `WM_PRINTCLIENT`.
The host clips each custom control and uses pixel-rounded bounds at the current DPI.
Window closure releases graphics resources before the COM runtime stops, even if the caller retains the closed `Window`.

`demo\browser.cpp` builds the tabs, panes, address fields, lists, status labels, shortcuts, and menus through public APIs.
`demo\explorer_state.hpp` contains bounded history and tab state without a window dependency.
`demo\shell_dispatch.cpp` supplies the Windows file-association dispatcher.
`demo\directory.cpp` owns folder enumeration, sorting, and file identities. The library has no browser-specific folder service or captions.
The old `src\windows.cpp` specialized host is deleted. Its browser options and entry function now belong to `demo\browser.hpp`.
The browser composition has no native window procedure, drawing calls, backend includes, or accessibility wiring.
It creates no per-row controls.

The control host creates an HWND for each control, including one HWND for each virtual list.
General runtime tree replacement, control removal, multiline input, and application-defined control renderers are not supported.
Tab data can change without these tree operations.

### Complete text frames

The previous host presented its cleared Direct2D target before it repainted nested native fields and captions.
Transparent viewport HWNDs did not protect these descendants through `WS_CLIPCHILDREN`.
A frame-boundary capture reproduced missing native text in all 72 original frames. Custom labels and buttons remained visible.

The host now includes native pixels before `EndDraw`.
Each visible native region has a reusable bitmap, not a render target.
One GDI scratch buffer serves these regions. Each frame refreshes the pixels from the real native control.
Ancestor viewports limit the copied regions. Hidden regions release their bitmaps.
There is no window-sized CPU copy, full-target readback, additional presentation, or repaint timer.
Native EDIT still owns text, selection, caret, undo, TSF, and accessibility.

Unchanged caption text and placeholder properties no longer cause native repaint requests.
An empty EDIT paints its background and placeholder through one small buffered blit.
The regression captures the desktop immediately after presentation, before any subsequent child paint.
It observed zero missing glyph regions across 1,323 frames with status changes, hover changes, and asynchronous thumbnail arrivals.
Coverage includes three themes, injected 96/144/192 DPI, scroll clipping, split visibility, native text changes, target loss, caret pixels, and repeated closure.
Only the regression uses `DwmFlush`. Physical mixed-monitor transitions and interactive IME candidates still need manual coverage.

Three browser runs used the same initial size and measurement procedure:

| Measurement | Original | Complete frames |
| --- | ---: | ---: |
| Warm private commit | 29.61 MiB | 30.29 MiB |
| Warm private working set | 15.36 MiB | 16.00 MiB |
| Warm total working set | 45.87 MiB | 46.62 MiB |
| Input-and-paint latency | 16.74 ms | 17.83 ms |
| Startup to first paint | 196.47 ms | 189.17 ms |
| Demo executable | 637,952 bytes | 645,632 bytes |

Both versions produced zero additional idle paints and retained one root target.
The known Qualcomm allocation increase near 865×780 client pixels remains. This change does not correct that driver behavior.
A full-target GDI-compatible prototype removed the gap but increased the larger-window paint cost.
The final implementation uses small native bitmaps instead.
All 23 native tests and five binding clients passed, including their callback-failure cases.
Logs, frame samples, and measurements are in `build\flicker`. The updated demo is `build\flicker\Release\xui_demo.exe`.

## Build

Requirements: Windows 10 version 1703 or later and Visual Studio 2022 with the C++ desktop workload.
A Windows SDK and CMake 3.24 or later are also necessary.
The initial Windows backend requires a 64-bit build.

In Visual Studio Developer PowerShell, run these commands from the project directory:

```powershell
$arch = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq "Arm64") { "ARM64" } else { "x64" }
$build = "build\$arch"
cmake -S . -B $build -G "Visual Studio 17 2022" -A $arch
cmake --build $build --config Release --parallel 4
ctest --test-dir $build -C Release --output-on-failure
& ".\$build\Release\xui_demo.exe" .
& ".\$build\Release\xui_gallery.exe"
& ".\$build\Release\xui_images.exe"
```

The architecture selection avoids x64 emulation on ARM64 Windows.

If CMake is not on `PATH`, use the copy that Visual Studio supplies:

```powershell
$arch = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq "Arm64") { "ARM64" } else { "x64" }
$build = "build\$arch"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake -S . -B $build -G "Visual Studio 17 2022" -A $arch
& $cmake --build $build --config Release --parallel 4
& (Join-Path (Split-Path $cmake) "ctest.exe") --test-dir $build -C Release --output-on-failure
```

## Run

To select a folder, supply its path as the first argument:

```powershell
& ".\$build\Release\xui_demo.exe" "C:\Windows"
```

Without an argument, the demo uses the current directory. The list contains immediate children, not a recursive folder tree.
Folders appear before files. The demo does not open, rename, or delete items.
The status line shows scan errors and clipboard errors.

The search field filters file names with a case-insensitive substring.
Tab changes focus between the search field and the list. Ctrl+F focuses the search field.
Enter in the search field requests the filter and focuses the list.

Arrow keys, Home, End, Page Up, and Page Down change the list selection.
The mouse wheel and the themed scrollbar move the viewport.
Ctrl+C copies the selected path. The context menu provides the same copy command, Refresh, Search, and theme commands.
F5 refreshes the folder. Shift+F10 opens the context menu for keyboard use.

### Explorer thumbnail icons

`FileList` provides optional thumbnail icons through the C++ API.
The explorer enables this option in both panes. Other `FileList` clients retain vector icons by default.
The C ABI and the C# and Rust wrappers remain unchanged.

```cpp
auto files = std::make_shared<xui::FileList>();
files->set_thumbnails(true);
files->on_thumbnail_error([](xui::ItemId id, const std::wstring& error) {
    // Report a failed visible request without a modal dialog.
});
files->reload_thumbnails();
files->set_thumbnails(false);
```

The backend uses each `FileItem::path` as the visual source.
The direct WIC path supports `.png`, `.jpg`, `.jpeg`, `.bmp`, `.gif`, `.tif`, `.tiff`, and `.webp`.
Extension matching ignores case. Paths retain their Unicode characters.
WebP requires an installed WIC codec. GIF and TIFF use the first frame only.
The existing WIC image limits, codec restrictions, and orientation limitations also apply here.

All other extensions and folders use `IShellItemImageFactory::GetImage`.
XUI first requests `SIIGBF_THUMBNAILONLY`. If no thumbnail is available, XUI requests `SIIGBF_ICONONLY`.
EXE, DLL, shortcut, document, PDF, and unknown extensions no longer have an extension filter.
Available thumbnail handlers and file associations determine their visuals.
A document preview shows file content. An application or file-type icon identifies the application or file type, not file content.
XUI does not run the selected executable, load it as application code, or change file associations to obtain its icon.

Each icon fits inside a 24-by-24-DIP box in the existing 32-DIP row.
The decode box follows the physical DPI size. The renderer preserves aspect ratio and blends alpha over the row background.
Names, selection, focus, scrolling, and UIA item identities retain their existing behavior.
Pending requests and failed requests retain the original vector icons.
The error callback receives the stable item ID and the error text on the UI thread.
The explorer status area counts failed visual requests.
A missing thumbnail is not an error if the Shell returns an icon.
If both Shell requests fail, the callback includes an HRESULT. Missing files also produce an explicit error.
The list does not retry a failed slot every frame.

The native list adapter retains only visible rows and the existing viewport buffer.
Limits are 24 thumbnail slots per list and 48 per window. Additional rows retain vector icons.
There is no thumbnail control, HWND, render target, or worker for each file.
WIC and Shell requests use separate, lazy, process-lifetime workers.
The Shell worker initializes a COM STA and pumps messages between requests and while idle.
Shell interfaces stay on that worker. They never cross apartments.
Both workers share one 64-request queue, a 128-entry cache, and the existing 8-MiB CPU and 8-MiB GPU pixel budgets.
At most two requests are active. A blocked Shell handler does not block the WIC worker.
The host retains bitmap IDs from both lists and `Image` controls in one shared target.
Equal files, source kinds, and physical decode sizes share cached pixels and bitmaps across panes.

Shell output converts to premultiplied BGRA for the existing renderer.
The backend rejects bitmaps larger than the requested dimensions before conversion.
Legacy icons without alpha use their HICON transparency mask through WIC.
Temporary HBITMAP and HICON handles have scoped ownership. XUI does not cache these handles.
The CPU ledger includes retained output pixels and pending output reservations.
Shell allocations, temporary WIC conversion storage, handler allocations, and driver allocations are outside these pixel budgets.
These budgets are not hard limits on total process memory.

Filtering and scrolling match slots by stable item ID and path, not row position.
A new source snapshot, `reload_thumbnails`, or a DPI change cancels the old requests.
The worker checks the canonical path, file identity, modification time, size, source kind, and physical output size before cache reuse.
Shell keys also retain the absolute item path, because links can have different visuals from their targets.
File metadata access and Shell extraction stay off the UI thread.
Directory refresh provides a new source snapshot. There is no periodic file watcher.
Hidden panes, offscreen rows, disabled thumbnails, and window closure release their requests and pixel references.
The bounded cache can retain unused pixels until eviction or `ImageResources::clear_unused`.
Window closure does not wait for a codec or Shell handler. Completed visuals do not request idle frames.

Cancellation revokes delivery and releases completed pixels. It cannot interrupt a Shell call that is already active.
XUI does not terminate blocked threads or create replacement workers.
A blocked Shell handler can therefore delay subsequent Shell icons until that call returns.
Network paths and third-party handlers can block or allocate memory outside XUI's budgets.
The Shell controls provider activation and its own thumbnail cache. XUI does not guarantee isolation for every third-party handler.
XUI does not directly instantiate thumbnail providers or disable Shell-managed provider isolation.
Windows can retain an old per-path icon after a file update, even when XUI detects the new file version.
File-association changes do not invalidate XUI's cache automatically.
Third-party handlers, network stalls, and physical monitor transitions still require manual coverage.

`xui_thumbnail_tests` covers the public control and the actual explorer composition.
Original WIC fixtures include PNG, JPEG, alpha, corrupt data, Unicode paths, folders, and ordinary text files.
Pixel assertions cover aspect ratio, selection, theme changes, synthetic DPI changes, and target recreation.
Other assertions cover shared ownership, filtering, scrolling, tabs, pane visibility, navigation, file changes, deletion, and blocked-decoder closure.
A 20,006-row source and a synthetic tall viewport cover slot limits without thousands of decodes.
The existing image pipeline tests retain their budget, cache, reservation, and cancellation checks.
Physical monitor transitions, physical GPU loss, and optional third-party WIC codecs still require manual coverage.

The current executable is `build\shell-icons\Release\xui_demo.exe`.
The existing `build\header` and `build\arm64` executables remain untouched.
The compact header, native EDIT composition, and suggestion-refresh fixes remain in this build.

#### Shell validation and measurements

The ARM64 Release executable is 687,104 bytes, compared with 650,240 bytes for the preserved header build.
All 24 native tests passed across the full suite and isolated retries.
Repeated full-suite runs had transient failures in desktop foreground, UIA, suggestion, and image-idle checks.
Each failed test passed in isolation, without changes to its focus safeguards.
The final full suite passed 23 tests. Its one suggestion-test failure passed on an isolated retry.
The Shell test also passed separately after additional queue, STA, and error-delivery assertions.
All five existing binding clients passed their normal UIA and callback-failure checks.
Both C# wrapper test executables passed 17 assertions with isolated copies of the new DLL.

`xui_shell_thumbnail_tests` uses a compiled, owned fixture EXE with original icon resources. It never runs that EXE.
Pixel assertions cover resource color, transparent margins, legacy icon masks, selection, filtering, scrolling, light theme, and 150%/200% synthetic DPI.
The native `C:\Windows\System32\cmd.exe` row matched all 388 opaque pixels from its extracted Shell image.
Text, PDF, DOCX, shortcut, DLL, folder, unknown-type, and missing-file requests also have coverage.
A PNG request through the private Shell path exercises the Shell thumbnail provider.
The public list keeps its existing direct WIC path and PNG/JPEG pixel assertions.

The tests check COM STA ownership, separate WIC progress during a blocked Shell call, and cancellation before stale delivery.
Two tall panes request exactly 48 of 1,000 distinct paths. A blocked worker cannot expand the shared queue beyond 64 requests.
A missing file produces one callback per slot, with no frame-by-frame retry.
The tests also check page changes, hidden content, replacement requests, and window closure while the Shell worker is blocked.
Fifty repeated extraction cycles retained 40 GDI handles before and after the loop.
The controlled CPU and GPU pixel peaks were 87,552 and 46,080 bytes in the Shell workload.

Three fresh processes per build used the same 60-text-file fixture and initial window geometry.
The protocol remains `tests\measure-browser.ps1`. The additional `cpu_ms` field measures process CPU time.
The following values are medians. They separate private commitment, private working set, and total working set.

| Counter | Preserved header build | Shell-enabled build |
| --- | ---: | ---: |
| First-paint private commitment, MiB | 28.609 | 29.293 |
| First-paint private working set, MiB | 13.816 | 14.332 |
| Idle private commitment, MiB | 28.398 | 31.758 |
| Idle private working set, MiB | 14.066 | 15.762 |
| Idle total working set, MiB | 44.812 | 56.453 |
| Selection, scroll, and full paint, ms | 16.858 | 17.294 |

Shell icons add a real cost to text-only folders, which previously requested no file images.
Idle private commitment increased by approximately 3.36 MiB. Idle private working set increased by approximately 1.70 MiB.
The Shell build retained 11 process threads and 405–417 handles, compared with nine threads and 250 handles.
Those process totals include Windows-owned work, not only XUI workers.
All text-folder idle intervals added zero custom paints.
The Shell build recorded zero CPU time in each two-second idle interval. The baseline recorded 0–15.625 ms.
These small samples do not establish a general performance result.

Three additional fresh demo processes opened native System32, then scrolled through its files.
Observed first-visible readiness was 241.81–339.70 ms, with a 261.81-ms median.
All three settled intervals recorded zero process CPU time and zero custom paints over two seconds.
Each process retained 18 visible slots, 18 ready images, 62 GDI handles, and one render target after scrolling.
The in-process browser test measured 0.779 ms for filter input and 0.178 ms per Page Down command.
Its blocked-Shell test processed ten scroll/filter/paint cycles in 153.60 ms without waiting for the Shell gate.
Readiness measurements include polling or quiet-observation intervals. They are not raw provider-call latency.

The build, logs, measurements, and owned-window captures are in `build\shell-icons`.
Evidence includes `native-tests.log`, `native-final.log`, `native-final-retry.log`, `explorer-isolated.log`, and `shell-tests.log`.
Measurements are in `browser-before.json`, `browser-after.json`, and `system32-idle.json`.
Binding logs and compatible DLL copies are in `build\shell-icons\bindings`.
The `shell-fixtures` directory contains `system32-cmd.png`, `system32-scrolled.png`, and `fixture-200dpi.png`.
No existing browser process was closed or replaced for this work.

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Preview\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = Join-Path (Split-Path $cmake) "ctest.exe"
& $cmake -S . -B build\shell-icons -G "Visual Studio 17 2022" -A ARM64 -DXUI_DESKTOP_TESTS=ON
& $cmake --build build\shell-icons --config Release --parallel 4
& $ctest --test-dir build\shell-icons -C Release --output-on-failure
& $ctest --test-dir build\shell-icons -C Release -R "xui_(shell_thumbnail|thumbnail)_tests" --output-on-failure
```

#### Earlier WIC-only validation and measurements

All 22 native tests passed in the ARM64 Release build.
The existing five binding clients passed their UIA and callback-failure checks with the updated native runtime.
The C# framework-dependent and Native AOT wrapper tests each passed 17 assertions.
The binding clients reused their existing published binaries. This change added no wrapper API.

Three fresh explorer processes used the unchanged 60-text-file measurement protocol.
The initial client size remained 924 by 641 DIPs at 96 DPI.
First-paint private commitment was 28.80 MiB median (28.79–28.90).
First-paint private working set was 14.40 MiB median (14.37–14.49).

Idle private commitment was 29.41 MiB median. Idle private working set was 15.19 MiB median.
Selection, scrolling, and a full paint took 17.25 ms median (17.16–17.77).
Each process retained one render target, nine threads, and 250 handles.
All three two-second idle intervals added zero custom paints.

The earlier recorded first-paint values were 28.78 MiB commitment and 14.32 MiB private working set.
These small samples do not establish a general performance change.

Separate fresh processes ran the actual explorer against 100 and 1,000 generated PNG files.
Each folder also contained six format, error, text, and folder entries.

| Counter | 100 PNG files | 1,000 PNG files |
| --- | ---: | ---: |
| Observed cold readiness, ms | 335.16 | 376.51 |
| Shared-cache second-pane readiness, ms | 189.26 | 175.05 |
| Page Down input handling, ms per command | 0.129 | 0.225 |
| Observed readiness after five Page Down commands, ms | 190.90 | 193.61 |
| Maximum retained slots across both panes | 20 | 20 |
| Decodes through the complete workload | 45 | 113 |
| Cache hits through the complete workload | 37 | 37 |
| Owned CPU pixel peak, bytes | 102,528 | 259,200 |
| Controlled GPU pixel peak, bytes | 29,952 | 29,952 |
| Warm private commitment, MiB | 31.13 | 32.05 |
| Warm private working set, MiB | 17.85 | 18.82 |

Readiness measurements include six quiet timer observations. They are not raw decode latency or directly comparable to the text-only paint measurement.
The 20-ms queue sampler observed zero queued jobs in these final runs. It can miss short queue peaks.

The service still enforces the 64-job queue limit. The tests assert this limit and the 24/48-slot limits.
CPU and GPU pixel counters exclude WIC transient allocations, driver allocations, source snapshots, and other process memory.
The tests also cover thumbnail cancellation during a blocked decode and window closure without a decoder join.

Logs, JSON measurements, original fixtures, and owned-window screenshots are in `build\thumbnails`.
Dark single-pane and light dual-pane screenshots show the existing filename alignment and vector fallback icons.
The main evidence files are `native-tests.log`, `thumbnail-tests.log`, `bindings.log`, `browser-60.json`, `images-100.log`, and `images-1000.log`.

### Thumbnail sample

Run the image sample with a folder path:

```powershell
& ".\$build\Release\xui_images.exe" "C:\Pictures"
```

Without an argument, the sample displays an empty state and a folder field.
Open folder and Enter in the field start an asynchronous scan.
Unload clears the current view. Resource usage removes unused decoded entries and displays the current counters.
F6 switches between dark and light themes. The sample never changes or deletes source files.

Tab moves focus to the thumbnail viewport.
Arrow keys, Page Up, Page Down, Home, End, the mouse wheel, and UIA scrolling move that viewport.
Images remain read-only. The sample does not add selection or a nonfunctional Open image action.

`demo\thumbnail_grid.hpp` subclasses public `Stack` for sample-specific layout.
It retains ten rows of four tiles, not one control per file.
Each tile contains an `Image` and a caption. Row slots rotate as the viewport moves.
The pool covers the visible rows and a buffer within the 960-DIP maximum viewport height.
Only visible images decode. Buffered controls do not prefetch files.
The viewport keeps keyboard focus while tile names and sources change.

The source loader rejects more than 20,000 images or more than four million UTF-16 path units.
It retains a bounded immutable source, not a paged filesystem index.
Folder replacement uses the existing latest-generation `ViewTask` mailbox.
The image service separately bounds work that cannot stop inside a codec.

To create original PNG fixtures with WIC, run:

```powershell
& ".\$build\Release\xui_image_tests.exe" ".\build\phase3\fixtures" --fixtures
& ".\$build\Release\xui_images.exe" ".\build\phase3\fixtures"
```

The fixture command writes generated test files in the supplied directory.
The fixtures contain original solid colors, plus deliberate corrupt and huge-header files.

## Framework boundaries

`xui_core` has no Windows dependencies. Its public API includes stable element identity, explicit property updates, and two-pass layout.
`Stack` supports horizontal and vertical layout, preferred sizes, flex space, gaps, and padding.
Elements distinguish layout invalidation from paint invalidation. The host combines pending layout requests and uses normal invalid paint regions.
The application has no continuous render loop.

`FileList` owns navigation and viewport behavior. `FileListModel` owns items, filter results, and selection identity.
Item focus is separate from selection. An accessibility focus action does not select or deselect an item.
The renderer submits visible rows plus a small buffer. It does not create an element, text layout, or UIA provider for every item.
Items and filter indices remain in memory. This is visual virtualization, not a paged storage system.

`Commands` remains available for command tables.
The browser connects shared callback actions through `Window::on_key` and `Control::on_context_menu`.
The Windows backend separates drawing, list adaptation, asynchronous delivery, native text input, and UI Automation.
Directory enumeration belongs to the sample service. The image backend owns image-file access and WIC decoding.

Each `ViewTask` has one worker for enumeration, sorting, snapshot construction, and filtering outside the UI thread.
Generation numbers reject stale source data and stale filter results.
Window closure requests cancellation without a UI-thread join. A filesystem driver that ignores cancellation can delay final process cleanup.
The UI thread submits committed text without a typing timer. It keeps the previous view until the current result arrives.

`NativeEditBridge` uses a real Windows EDIT control. Windows owns its IME, text selection, Unicode input, undo, clipboard behavior, and accessibility.
This bridge is not a custom text engine or a completed TSF implementation.
The search field has a 1,024-character limit. Framework text drawing never substitutes for editable text.
The empty-field hint uses the theme color. It is decorative text, not part of the editable value.

The custom list exposes UIA list semantics, selection, scroll, fragment navigation, and on-demand item providers.
Selection is optional and single-item. UIA removal clears only the addressed selected item.
Item providers retain stable IDs instead of recycled row positions.
Native EDIT supplies search-field accessibility. The public `Label` provider supplies status-text accessibility.
The window uses per-monitor DPI, system high-contrast colors, and graphics-target recreation.

## Tests and measurements

The explorer adds these regressions:

| Program | Coverage |
| --- | --- |
| `xui_explorer_tests` | History commits, failed navigation, tab selection and closure, cancellation, bounded state, UNC roots, long Unicode scans, activation, and injected file associations |
| `xui_explorer_smoke` | The real explorer, address input, history, keyboard shortcuts, context commands, independent panes, tab providers, divider input, clipping, and resource bounds |
| `xui_suggestion_tests` | Folder prefixes, real and synthetic enumeration limits, deterministic cancellation, native EDIT behavior, popup input, themes, and closure during a blocked request |
| `xui_split_window_tests` | Eight window cycles with tabs, two lists, native fields, capture cancellation, simulated DPI, target recreation, and final resource disposal |

The desktop tests require `-DXUI_DESKTOP_TESTS=ON`.
The keyboard test requires foreground ownership before it sends input. It never sends a shortcut to another application.
The file-association test records the exact selected path through an injected dispatcher. It does not start fixture files.
The smoke test uses scoped UIA focus-property events, selection events, and structure events.
Its optional `--global-focus-events` argument also subscribes to desktop-wide focus events.
That optional subscription can stall inside Windows before a test action. It depends on providers outside this process.

Run the explorer checks:

```powershell
ctest --test-dir build\arm64 -C Release -R "xui_(explorer|split_window)" --output-on-failure
.\tests\measure-browser.ps1 -Runs 5 -WorkspaceCycles -Screenshots -Output build\explorer\final-memory.json
```

`-WorkspaceCycles` adds a second pane, creates 15 inactive tabs, closes those tabs, and performs 40 folder transitions.
It then hides the second pane and records two idle samples.
The protocol retains the original 60-file workload, 924×641 client extent, and selection/filter workload.
Native child discovery now supports pane hosts. The theme workload uses the Theme command instead of F6.
The baseline path still supports the original F6 command.

The four image regressions require `-DXUI_DESKTOP_TESTS=ON`:

| Program | Coverage |
| --- | --- |
| `xui_image_tests` | Exact budgets, reservations, sharing, alpha conversion, size keys, file versions, corrupt data, LRU eviction, cancellation, and device recreation |
| `xui_image_window_tests` | A 20,000-item recycled grid, folder replacements, real pixels, native geometry, clipping, scrolling, idle frames, unload, and blocked-decoder closure |
| `xui_images_smoke` | The actual thumbnail executable, external UIA, native folder input, source errors, image errors, unload, idle, and stale providers |
| `xui_gallery_image_smoke` | The gallery preview, real decoding, external image semantics, native path input, corrupt files, unload, idle, and provider disconnection |

`xui_image_tests` writes original PNG fixtures through the Windows WIC encoder.
Its malformed files cover zero dimensions, huge dimensions, truncated pixels, corrupt data, missing files, directories, and an oversized encoded file.
Deterministic gates stop the worker before decoding, during a reservation, and before completion delivery.
The tests replace a tile source at each boundary and reject the obsolete result.
The full-queue test holds one active job and fills all 64 queue slots.
The GPU test fills the controlled bitmap budget, rejects an extra upload, evicts a bitmap, and uploads again.

The window test holds a 4 MiB reservation while the decoder gate remains closed.
`Application::run` returns before the test opens that gate.
After the worker resumes, cancellation returns the reservation to zero without a stale control update.
Image assertions use owned pixel bytes and controlled bitmap estimates, not process-memory thresholds.

`xui_core_tests` covers layout, lifecycle, filtering, selection, navigation, and viewport calculations.
It uses explicit assertions that remain active in Release builds.
It also covers scrollbar geometry and text contrast for both built-in palettes.

`xui_control_tests` covers control state, disabled actions, pointer capture, cancellation, focus traversal, invalidation, Unicode limits, and retained controls.
It also covers injected text metrics, cached measurement, fixed and automatic sizes, size limits, unbounded flex measurement, scroll reveal, and content ownership.
`xui_scroll_tests` covers DirectWrite measurements, narrow windows, native viewport clipping, UIA bounds, offscreen state, native keyboard input, and focus reveal.
It also covers wheel input over EDIT, thumb drag, keyboard scrolling, disabled descendants, nested Stacks, and an embedded FileList.
Eight window cycles exercise dark, light, explicit high-contrast colors, and 96/120/144/192-DPI messages without changes to Windows settings.
Repeated paints and scrolling must reuse the cached text layouts and the single render target.
The tests compare native text-pattern support with an unmodified EDIT provider.
The tests also require bounded handle counts and rejected provider actions after closure.
The resource sample follows asynchronous snapshot disposal and a short message-pump interval for native provider cleanup.
The combined UIA client/server test reports process handles but does not attribute client thread pools to the framework.
The server-only lifecycle test retains its process-handle limit for eight ordinary windows and eight ScrollView trees.
`xui_window_tests` covers public window ownership, callback closure, startup failure, callback failure, and later runs on the same thread.
It also covers a public list beside other controls, filter delivery, selection callbacks, reentrant closure, and off-thread snapshot disposal.
Two sets of eight repeated windows exercise cancelled refreshes, theme changes, resize, simulated DPI changes, and target recreation.
The DPI cases use 96, 120, 144, and 192 without global display changes.
These cases require one live target during display and no XUI targets after closure.
After warm-up, handle, GDI, and USER counts must remain within a small fixed allowance for Windows initialization.
A blocked-loader test requires window closure to return before the loader can finish.
An isolated public-control server checks list names, automation IDs, independent focus, selection, and retained-provider rejection after closure.
It requires the same manifest and desktop as the applications.
`xui_gallery_smoke` covers UIA roles, names, identity, Invoke, Toggle, disabled action rejection, label updates, and native text.
It also covers Tab, Shift+Tab, Space, Enter, pointer cancellation, theme changes, idle paint counts, and provider invalidation after shutdown.
The native UIA text proxy can complete focus changes after a method returns.
The browser and gallery probes require stable focus before keyboard sequences instead of accepting a transient focus notification.
The gallery resolves EDIT through its automation tree, by name and control type.
It calls external UIA `SetFocus` without a `WM_NEXTDLGCTL` workaround.
Twelve activation cycles cover a competing test window, minimized restoration, native focus events, and queued Unicode input with a surrogate pair.
An external event client covers Invoke, Toggle, name, enabled, native value, and custom keyboard-focus properties.
The isolated public-list client covers selection events, focus properties, structure changes, scroll properties, and quiet repeated scroll endpoints.
The lifecycle test also exercises `Window::focus` with native EDIT.

`xui_native_integration_tests` covers native selection, replacement, undo, surrogate input, rejected focus, and composition guards.
It checks exact native font heights at 96, 120, 144, and 192 DPI.
A private renderer seam injects `D2DERR_RECREATE_TARGET` after `EndDraw`.
The test requires a replacement frame, one live target, retained text layouts, and a return to idle.
The seam belongs to the internal renderer. It adds no public window action.
Clipboard checks run in a private window station with its own desktop.
They cover public copy, native copy/paste/cut/undo, and a paste limit that preserves surrogate pairs.
The interactive clipboard sequence number must remain unchanged.

`xui_performance_tests` covers immutable views, source generations, cancellation, errors, retirement, and repeated worker shutdown.
It also covers changed collection order, removed identities, hidden selection, and independent focus.
The list cases use 60, 100,000, and 1,000,000 synthetic rows.
Allocation counters require zero allocations during 20,000 selection, focus, scroll, and lookup sequences at each size.
Tests use synchronization and operation invariants, not performance thresholds.

The desktop smoke program creates a fixture folder under its working directory and opens its own demo window.
It exercises native text input, Unicode names, UIA actions, keyboard navigation, scrolling, and shutdown.
It also invokes Refresh through the native context menu.
It exercises theme changes without loss of text, selection, or item identity.
Pointer-message sequences exercise scrollbar paging and thumb dragging.
It also reports the number of submitted rows and custom paint calls during an idle interval.
Live `WM_CHAR` input overlaps a refresh. Another sequence combines 80 query replacements, ten refresh requests, and two theme changes.
The probe requires the final requested generation and the expected row.
It also exercises simulated IME boundaries, process memory counters, and shutdown after observed worker activity.
An interactive Windows desktop is necessary.

```powershell
& ".\$build\Release\xui_windows_smoke.exe" ".\$build\Release\xui_demo.exe"
& ".\$build\Release\xui_gallery_smoke.exe" ".\$build\Release\xui_gallery.exe"
& ".\$build\Release\xui_window_tests.exe"
& ".\$build\Release\xui_scroll_tests.exe"
& ".\$build\Release\xui_native_integration_tests.exe"
```

To include the desktop smoke program in CTest, configure `-DXUI_DESKTOP_TESTS=ON`.
The default CTest configuration runs only the pure tests.

## Performance design

`FileSnapshot` shares an immutable item array. It stores lowercase names in one character buffer.
A cancellable length pass reserves this buffer once. The snapshot does not retain geometric growth capacity.
A sorted array of 32-bit row indices provides the shared ID lookup.
`FilteredView` stores matching source indices in source order.
Two binary searches resolve an ID to its visible position. No separate reverse-index table or UIA row array is necessary.

The model, renderer, and UIA providers share the same view.
Selection, focus, and UIA actions use logarithmic lookups instead of full-list scans.
Ordinary selection, focus, and scrolling do not rebuild or copy a view.
The renderer still allocates text resources for submitted rows, not for every source row.

`ViewWorker` has one pending request and one result mailbox.
Each request advances the result generation. Only a refresh advances the source generation.
A query change cancels filtering but does not restart directory access.
Both the mailbox and the host reject obsolete results.
The source loader also rejects source data that completes after cancellation.

Builders check cancellation every 256 rows and during name conversion and sorting.
Name conversion and sort comparators check at intervals of 4,096 characters or comparisons.
The worker releases retired views outside the request mutex.
An unread result also releases its old source outside that mutex.
Ordinary requests never join a worker.

The host uses `FileList::set_view` for worker results.
The synchronous `set_items` and `set_filter` methods remain available for small collections and pure callers.
These synchronous methods must not process large collections on a UI thread.
Callers must not keep mutable aliases to snapshot item arrays.

### Repeatable benchmark

Run the benchmark separately from builds and desktop tests:

```powershell
& ".\$build\Release\xui_performance_tests.exe" --benchmark |
    Tee-Object -FilePath ".\$build\performance-results.txt"
```

The benchmark reports synthetic data creation separately from snapshot construction and filtering.
It performs no directory I/O. Timed workloads exclude process startup.
Each nonempty query reports the median of five runs.
The empty-query result and the interaction sequence each report one run.
The interaction sequence includes selection, independent focus, reveal, and identity lookups.
Checksums and assertions keep that work observable in Release builds.

Windows memory counters report private bytes and the working set.
The memory sample includes source items, cached names, indices, views, and allocator retention.
It excludes the directory identity map, graphics resources, and external UIA clients.
Refreshes can temporarily retain both old and new sources.

## Known limits

This milestone supports one active application window per UI thread. The browser supports a flat list and single selection.
It does not provide a general styling system, a reactive property graph, or a custom text engine.
Filter matching uses wide-character case conversion, not full Unicode normalization or linguistic search.
Filtering still examines every source row. It runs on the worker, not the UI thread.
The index format supports at most 4,294,967,295 rows, but available memory imposes a much smaller practical limit.
All source items remain in memory. The million-row benchmark is not a million-file desktop test.
Very large scroll extents lose subpixel precision because offsets use floating-point values.
Cancellation cannot interrupt one allocator operation, one substring search, or a filesystem driver that ignores cancellation.
The `ViewTask` cleanup thread waits for cancelled workers. Process exit does not have a fixed shutdown deadline.
UIA clients that enumerate the complete tree still perform work for every item.

UIA supports the patterns that this demo uses, not every Windows control pattern.
Application context menus use the shared theme. Native EDIT keeps its Windows menu when no custom menu callback exists.
Custom touch gestures, drag-and-drop, and file modifications are outside this milestone.
The explorer uses Windows mouse promotion for touch input. Dedicated touch-hardware coverage remains incomplete.
The image sample replaces folders through its path field.
High-contrast changes and graphics-target recreation have implementation paths, but hardware coverage is not exhaustive.
After server exit, the Windows native HWND focus proxy can return `S_OK` for a stale `SetFocus` call.
The native Value provider returns `UIA_E_ELEMENTNOTAVAILABLE`, and the stale focus call must leave another window's focus unchanged.
Custom providers return `UIA_E_ELEMENTNOTAVAILABLE` after disconnection.
A native focus HRESULT alone is not proof of live keyboard focus.

## Recorded results

### Folder suggestions

The ARM64 Release implementation passed the full 21-program native suite in 197.35 seconds.
The phase 4 regression also passed C# FDD, NativeAOT, and Rust checks.
All five desktop clients passed normal-operation and callback-failure runs.
The final explorer run passed 460 assertions, including native suggestion actions through UIA.
The dark and light popup captures contain only the owned test windows.

The source tests used 307 real fixture folders and injected 100,000-entry directory cursors.
The directory cursor stopped after 64 retained folders. The file-only cursor stopped after 4,096 entries.
Both cursors closed at the budget boundary. The retained real result used 10,176 bytes of string and vector capacity.
This figure excludes allocator metadata and native LISTBOX storage.
A blocked provider received 100,000 replacement requests. It ran only the blocked request and the latest request.
The active-request peak and pending-request peak were both one.

| Measurement | Result |
| --- | --- |
| Cold native popup, including debounce | 124.49 ms |
| Warm native popup, including debounce | 104.82 ms |
| Cold popup in the final explorer desktop run | 165.70 ms |
| 100 native `WM_CHAR` messages with a blocked provider | 1.24-ms mean, 4.57-ms maximum |
| Close with a blocked provider | 30.04 ms |
| Host paint count during the closed-popup idle check | No increase |
| Direct2D targets with the popup open | One |

The gate-based tests establish cancellation order without a timing assumption.
They cover stale delivery, context changes, closed endpoints, and closure before the provider returns.
The UI tests cover keyboard acceptance, immediate Enter during debounce, mouse acceptance, UIA selection, and the native accessible default action.
They also cover both pane bases, Unicode, independent search, native undo, read-only state, disabled state, scroll selection, and theme changes.
IME coverage uses synthetic composition messages. Physical IME candidate sessions, mixed-DPI monitor moves, and slow remote servers still need manual coverage.
The native DPI-transition test checks dismissal at the current monitor DPI.

Three fresh processes per build used the same 60-item fixture and `tests\measure-browser.ps1`.
The following values are medians. These small differences do not establish a memory reduction.

| Small-folder phase | Before: private commit / private working set | After: private commit / private working set |
| --- | --- | --- |
| First paint | 29.02 / 14.50 MiB | 28.78 / 14.32 MiB |
| Warm | 29.48 / 15.23 MiB | 29.48 / 15.20 MiB |
| Idle | 29.43 / 15.20 MiB | 29.43 / 15.16 MiB |

First-paint latency changed from 193.74 ms to 200.41 ms. Both builds retained one target and nine threads at first paint.
No suggestion worker or popup existed during those first-paint measurements.
The two optional address peers added two GDI brushes before first use.
Before address focus, three one-second idle samples recorded zero additional host paints and 0.00-ms process CPU increases.
The CPU counter has limited resolution.
The previous recorded explorer executable was 588,800 bytes. The new executable is 628,224 bytes, an increase of 39,424 bytes.
The explorer still deploys as one executable and requires no new third-party runtime.

One separate desktop run measured the first popup with a UIA client attached.
Private commit changed from 30,683,136 to 31,592,448 bytes. Private working set changed from 15,282,176 to 16,453,632 bytes.
These totals include first-use worker, font, popup, and native accessibility costs. They are not an isolated allocation count for the list.
No result supports a speed comparison with Windows Explorer.

The raw results are in `build\autosuggest\before.json`, `after.json`, `comparison.json`, `native.log`, `desktop-final.log`, and `idle-cpu.json`.
The captures are `build\autosuggest\browser-dark.png`, `browser-light.png`, and `browser-right.png`.
The full regression logs are in `build\phase4` and `build\autosuggest\regression.log`.

To reproduce the suggestion checks, run:

```powershell
.\build\arm64\Release\xui_suggestion_tests.exe
.\build\arm64\Release\xui_explorer_smoke.exe .\build\arm64\Release\xui_demo.exe --suggestion-capture
.\tests\measure-browser.ps1 -Runs 3 -Output build\autosuggest\after.json
.\tests\phase4.ps1 -SkipMeasurements
```

The capture command writes BMP files. The recorded PNG files contain the same pixels.

### Explorer navigation, tabs, and panes

The September 11, 2026 native ARM64 Release build passed all 17 CTest programs in 161.57 seconds.
This run includes the existing native EDIT, gallery, image, ABI, scrolling, and zero-allocation interaction tests.
The explorer state test passed 57 assertions.
The desktop explorer test passed real keyboard, context-menu, long Unicode path, tab, pane, divider, and UIA lifetime checks.
The split-window test passed eight creation and disposal cycles.

The C# framework-dependent and NativeAOT tests each passed 17 wrapper assertions.
Rust passed five wrapper tests, one ABI test, and two compile-fail documentation tests.
Rust formatting and Clippy checks passed.
All five normal binding UIA clients passed. All five deliberate callback-failure clients also passed.
The earlier foreground-dependent failures did not recur in this final run.
No unrelated window or desktop setting was changed.

Build and test records:

- `build\explorer\build.log`
- `build\explorer\native-tests.log`
- `build\explorer\bindings-tests.log`
- `build\explorer\final-memory.json`
- `build\explorer\final-measurements.log`

The five fresh-process measurements used 60 files, 96 DPI, and the original 924×641 client extent.
The table reports medians. Latency ranges show the minimum and maximum across those runs.

| Phase | Private commit | Private working set | Total working set | Latency |
| --- | ---: | ---: | ---: | ---: |
| First painted frame | 28.78 MiB | 14.40 MiB | 45.05 MiB | 199.85 ms, range 164.00–220.02 |
| Warm selection and filter update | 29.46 MiB | 15.27 MiB | 45.95 MiB | 17.40 ms, range 16.95–18.34 |
| Idle | 29.42 MiB | 15.22 MiB | 45.90 MiB | No additional paints |
| Repeated filter stress | 30.14 MiB | 15.87 MiB | 46.59 MiB | — |

The measured executable contains 524,288 bytes.
The previous phase's recorded executable contained 419,840 bytes, a difference of 104,448 bytes.
The explorer retains the static CRT and adds no third-party runtime.
These measurements do not establish a universal memory advantage over another file manager.

#### Pane and tab costs

The single-pane process had nine threads and 250 handles after startup.
Activating the second pane added one worker thread and one handle.
Its median commitment increase was 270,336 bytes, with a range of 233,472–544,768 bytes.
The base process already owns the fixed, hidden second-pane control tree.
This delta is not the complete cost of constructing a pane.

Fifteen additional inactive tabs added no native peers, render targets, threads, or handles.
Their median process-commitment increase was 1,556,480 bytes, with a range of 1,515,520–3,751,936 bytes.
That increase includes intervening scans and allocator retention. It is not an intrinsic per-tab allocation.
The ARM64 `Tab` object occupies 40 bytes. Each `Location` object occupies 136 bytes.
Those object sizes exclude vector allocations, strings, and retained history entries.

After tab closure and 40 folder transitions, median commitment was 33.85 MiB.
The range was 32.45–34.73 MiB. Median private working set was 19.50 MiB.
The process returned to nine threads and 250 handles after the right pane stopped.
Both final idle samples had the same paint count. Each process retained one shared render target.
The repeated window tests verified that controlled resources returned to zero after closure.

Larger initial extents exposed a separate rendering allocation increase during the first populated Direct2D frame.
Experiments at 1000×700 and 1100×720 reached approximately 94 MiB of private commitment.
The final comparison retains the original extent. Resizing remains supported, but larger windows can consume substantially more memory.
The platform allocation cause is not fully attributed.
`build\explorer\memory.json` retains the earlier larger-window measurements.

The rendered single-pane, dual-pane, and 16-tab screenshots are beside `final-memory.json`.
The inspected dual-pane images show complete navigation labels and the active pane's tab position.
Real UNC-server outages, denied-access ACL fixtures, dedicated touch hardware, and mixed-monitor DPI transitions remain unverified.
The tests cover UNC root parsing, injected failures, extended local paths, simulated DPI changes, and mouse capture cancellation.
Desktop-wide UIA focus subscription remains optional for the platform limitation described in the test section.
New workspace controls currently have a public C++ API only. The existing C ABI and bindings remain compatible.

### Images and bounded resources (phase 3)

The September 11, 2026 native ARM64 Release build passed all twelve CTest programs in the final 138.28-second run.
The instrumented measurement run also passed all twelve programs in 142.51 seconds.
The eight existing programs remain active. The model tests retain their zero-allocation interaction assertions.
`build\phase3\ctest.log` contains the measurements. `build\phase3\ctest-final.log` contains the final test results.
`build\phase3\build.log` contains the build output.
The build produced no compiler warnings.

| Executable | Phase 2 bytes | Phase 3 bytes | Change |
| --- | ---: | ---: | ---: |
| `xui_demo.exe` | 392,704 | 419,840 | +27,136 |
| `xui_gallery.exe` | 317,440 | 347,648 | +30,208 |
| `xui_images.exe` | — | 390,656 | New sample |

The PE machine type is `AA64` (native ARM64).
The Release targets retain `/MT` and link-time optimization.
The import inspection shows Windows system DLLs and no dynamic MSVC runtime.
WIC loads through system COM. The application adds no third-party runtime.
`build\phase3\images-pe.txt` and `sizes.txt` contain the binary evidence.

#### Resource stress and rendered workload

The resource test decoded 656 images and performed 514 cache evictions.
Its 12 rejected operations cover file, codec, queue, and budget errors.
It retired 68 cancelled jobs, including the deliberately full queue.
The exact CPU peak and controlled GPU peak both reached 8,388,608 bytes.
All reservations and live resource counters returned to zero after release.

The window workload used 160 generated 1,024-by-1,024 PNG files and a 20,000-item immutable model.
It retained 40 tile groups, 40 `Image` controls, and 40 captions.
It performed 120 input pairs across four source generations.
Each pair sent Page Down and a mouse-wheel message through the native viewport.
Each source replacement retained the same fixed tile pool.

| Instrumented complete run | Result |
| --- | ---: |
| Decoded images | 1,595 |
| Decoded-cache hits | 309 |
| Pixel peak, including reservations | 8,388,608 bytes |
| Controlled bitmap peak | 1,310,720 bytes |
| Pixel bytes after each of three folder replacements | 8,388,608 |
| Bitmap bytes at those samples | 1,310,720 |
| Cached entries at those samples | 128 |
| Cumulative evictions at those samples | 348 / 721 / 1,094 |
| Native input pair, mean / maximum | 23.11 / 75.38 ms |
| Page-to-ready observation, mean | 280.35 ms |
| Decode-worker service, total / maximum | 30,449.6 / 53.61 ms |
| Bitmap upload calls, total / maximum | 518.14 / 5.58 ms |
| Pixel bytes before explicit unload | 8,388,608 |
| Bitmap bytes before explicit unload | 1,048,576 |
| Pixel, reservation, and bitmap bytes after unload and cache eviction | 0 |
| Pixel, reservation, and bitmap bytes after window closure | 0 |
| Idle paint delta | 0 |
| Close with a blocked decoder and a live 4 MiB reservation | 22.75 ms |

The decode-worker timer includes file-version checks, cache lookup, and WIC calls. It excludes queue wait.
The upload timer measures the CPU call to `CreateBitmap`, not GPU completion time.
The page-to-ready observation includes a 20 ms test timer and settling intervals.
These figures are measurements, not latency guarantees.
The separate resource tests use deliberate worker gates. Their maximum decode-worker time is not an interactive latency measurement.

The image host batches native placement by parent HWND.
It skips unchanged native geometry, enabled state, and accessibility snapshots.
The renderer retains the existing single target per window.
The pixel tests cover the complete first image, viewport-relative native bounds, and the header clip.
The capture is `build\phase3\images-workload.bmp`. A PNG copy is also available.

#### Ordinary browser overhead

The same 60-item protocol ran in three fresh processes before and after the image work.
The measurements ran separately from builds and desktop tests.
The source data and viewport size stayed the same.
`build\phase3\browser-before.json` and `browser-after.json` contain the samples.

| Metric, median | Before | After |
| --- | ---: | ---: |
| Idle private commit | 27.87 MiB | 27.86 MiB |
| Idle private working set | 13.42 MiB | 13.45 MiB |
| Idle total working set | 44.06 MiB | 44.10 MiB |
| First painted 60-item view | 157.48 ms | 172.32 ms |
| Protocol scroll/key/redraw stress iteration | 21.01 ms | 18.68 ms |
| Handles | 250 | 250 |
| Threads | 9 | 9 |
| Direct2D targets | 1 | 1 |
| Idle paint delta | 0 | 0 |

The startup sample increased by 14.84 ms. The stress sample decreased by 2.33 ms.
The small sample size and desktop scheduling do not establish a latency improvement or a fixed startup cost.
The ordinary browser starts no image worker and owns no decoded pixels or image bitmaps.
Process memory remains distinct from both resource ledgers.

#### Remaining scope

Physical device loss, real IME composition, Narrator speech, and mixed-monitor behavior remain manual checks.
The new device-loss tests use the private injected seam.
The image path has no orientation transform, color-profile conversion, animation, file watcher, or content hash.
The source scan is bounded but not paged. The image grid has no selection, drag action, or destructive file operation.

Phase 3 adds no C ABI, C# wrapper, or Rust wrapper.
The next phase can expose image creation, source assignment, dimensions, status, error text, reload, unload, and statistics.
That boundary must preserve UI-thread ownership, exception translation, owned string lifetimes, and cancellation on control or window disposal.
The process-wide budgets must remain shared across native and language-wrapper callers.

### Windows integration (phase 2)

The September 11, 2026 native ARM64 Release build passed all eight CTest programs.
Each program passed three consecutive runs: 24 executions in 170.28 seconds.
The seven existing programs retained their assertions. The new program covers native integration.

| Program | Three-run elapsed range, seconds |
| --- | ---: |
| `xui_core_tests` | 0.02-0.53 |
| `xui_control_tests` | 0.02-0.30 |
| `xui_performance_tests` | 1.43-1.65 |
| `xui_windows_smoke` | 6.92-7.40 |
| `xui_gallery_smoke` | 13.74-14.45 |
| `xui_window_tests` | 14.26-15.31 |
| `xui_scroll_tests` | 13.13-19.20 |
| `xui_native_integration_tests` | 1.05-7.59 |

The native focus reproduction returned `S_OK` and exposed keyboard focus while the real host remained minimized.
The message trace showed the native UIA proxy activate the child EDIT HWND.
`NativeEditBridge::subclass` now restores activation to the root HWND and focus to EDIT during that transaction.
The regression checks the restored host, stable native focus, a native focus event, and subsequent `SendInput` text.
It does not substitute host messages for external UIA focus.

The native text provider remains the Windows provider.
The clipping adapter declares provider-owned focus and releases its HWND provider reference at destruction.
Custom controls and lists also declare provider-owned focus.
The native font cache avoids same-DPI replacement during theme changes.
The composition guard now includes native end processing.
Rejected select-all focus no longer changes a disabled input's selection.
Clipboard calls without a live owner fail before any clipboard change.

List property events now compare the previous and current immutable snapshots.
They cover list names, enabled state, selection, item focus, scroll percentage, viewport fraction, and scroll availability.
Eight repeated requests for the same scroll endpoint produce no extra scroll-property events.
Filtering still preserves hidden selection IDs, and hidden providers still reject actions.
This phase added no per-row provider cache, per-control render target, or continuous render loop.

The controlled comparison used three fresh processes before the changes and three after them.
Both groups used the unchanged `tests\measure-browser.ps1` protocol and its 60-file fixture.
The client area remained 924 by 641 DIPs, at 96 DPI, with the initial dark theme.
The compiler, Windows SDK, static runtime, and build configuration remained unchanged.
Values are medians with minimum-maximum ranges. Commitment, private residency, and total residency are separate counters.

| Counter | Before phase 2 | After phase 2 |
| --- | ---: | ---: |
| First-paint private commitment, MiB | 27.79 (27.77-27.80) | 27.79 (27.77-27.81) |
| Warm private commitment, MiB | 27.85 (27.84-27.86) | 27.89 (27.85-28.03) |
| Idle private commitment, MiB | 27.82 (27.81-27.82) | 27.85 (27.85-27.99) |
| Idle private working set, MiB | 13.34 (13.18-13.47) | 13.53 (13.52-13.85) |
| Idle total working set, MiB | 43.98 (43.87-44.12) | 44.18 (44.16-44.50) |
| Stress private commitment, MiB | 28.01 (28.00-28.02) | 28.05 (28.02-28.16) |
| Stress private working set, MiB | 13.68 (13.54-13.69) | 13.75 (13.70-14.04) |
| Stress total working set, MiB | 44.37 (44.28-44.40) | 44.42 (44.40-44.72) |
| Peak commitment through stress, MiB | 28.13 (28.12-28.16) | 28.16 (28.16-28.26) |
| Startup through first paint, ms | 151.09 (148.92-174.88) | 164.21 (164.03-168.28) |
| Selection + scroll + full paint, ms | 17.99 (16.48-21.71) | 18.59 (17.80-28.90) |
| Render targets | 1 | 1 |
| Idle process handles / threads | 250 / 9 | 250 / 9 |
| Idle GDI / USER objects | 19-21 / 26-27 | 19-21 / 26-27 |
| Stress GDI / USER objects | 21-23 / 26-27 | 21-23 / 26-27 |
| Custom paints during the two-second idle interval | 0 | 0 |
| Browser executable, bytes | 391,168 | 392,704 |
| Gallery executable, bytes | 314,880 | 317,440 |

Idle commitment increased by about 0.03 MiB. Private residency increased by about 0.19 MiB.
The small samples cannot attribute every resident page or establish a general speed improvement.
The browser grew by 1,536 bytes. The gallery grew by 2,560 bytes.
No working-set trimming, stack reduction, accessibility removal, or software-rendering override contributed to these results.
PE inspection reported ARM64 and Windows system imports, without a dynamic MSVC runtime or third-party runtime.

Server-only lifecycle checks still require one live target and zero targets after closure.
Across the repeated runs, ordinary closed windows used 247-249 process handles.
Closed ScrollView windows used 261-263 handles. Both cases retained 16 GDI objects and two USER objects.
The existing two-handle allowance remained unchanged.
Combined UIA client/server diagnostics include Windows client pools and do not replace the server-only limit.
Cancelled refresh, blocked-loader closure, and off-thread disposal cases passed.

The separate model benchmark retained zero allocations for 20,000 interactions at 60, 100,000, and 1,000,000 rows.
The recorded interaction times were 6.33, 33.71, and 54.77 ms.
The 100,000-row and million-row name/index caches remained 4,400,022 and 44,000,022 bytes.
The million-row benchmark process retained 239.70 MiB of private commitment, including its source data and views.

To repeat the phase checks, run these commands from the repository:

```powershell
$tools = "C:\Program Files\Microsoft Visual Studio\2022\Preview\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
& "$tools\cmake.exe" --build build\arm64 --config Release --parallel 4
& "$tools\ctest.exe" --test-dir build\arm64 -C Release --output-on-failure --repeat until-fail:3
& .\tests\measure-browser.ps1 -Runs 3 -Output build\phase2\repeat-memory.json
& .\build\arm64\Release\xui_performance_tests.exe --benchmark
```

The measurements must run separately from builds, desktop tests, and benchmarks.
Raw launch, first-paint, warm, idle, and stress samples remain in `build\phase2\before.json` and `build\phase2\after-final.json`.
Other evidence includes `ctest-final.txt`, `test-details-final.log`, `benchmark.txt`, `dependencies.txt`, and `executables.json` in that directory.
`native-focus-trace.txt` and `focus-activation-detail.txt` contain the failing native activation sequence.
The trace code is not part of the framework.

#### Automated coverage and manual gaps

The environment was Windows `10.0.28637.0` on ARM64, with Windows SDK `10.0.26100.0`.
The configured keyboard was `0409:00000409` (English, United States).
The language list also contained `qps-ploc`, `ja`, and `zh-Hans-CN`, without input-method entries.
TSF enumeration found one Japanese and two Chinese keyboard profiles. All three were disabled and inactive.
The compatibility `ImmIsIME` result for the English layout was true. This result does not prove a usable Japanese or Chinese composition session.
No language, keyboard profile, contrast setting, or display setting changed.

| Area | Automated result | Remaining physical or manual coverage |
| --- | --- | --- |
| External native focus | Automation-tree discovery, direct UIA `SetFocus`, minimize/restore, native `EVENT_OBJECT_FOCUS`, stable focus, queued BMP and surrogate input | Foreground restrictions across integrity levels and other Windows builds |
| Accessibility events | External Invoke, Toggle, name, enabled, native Value, custom focus properties, list selection, structure, and scroll events | Desktop-wide `AutomationFocusChanged` subscription and Narrator announcements |
| Native provider lifetime | Native Value disconnection and no focus movement after server exit. Custom providers reject stale actions | Native `SetFocus` can return `S_OK` after exit, despite no focus change |
| IME | Real EDIT retains default IME dispatch. Synthetic boundaries cover commit suppression, one final callback, Tab, Enter, and shortcut guards | Japanese conversion/candidate selection and Chinese Pinyin commit/cancel, focus loss, and candidate placement |
| Clipboard | Unicode copy/paste/cut/undo and surrogate-safe limit checks on a private window station | Remote clipboard, clipboard managers, and cross-integrity clipboard access |
| DPI | Exact font sizes, layout, reveal, and clipping at 96/120/144/192 DPI through messages | Physical mixed-DPI monitor moves, candidate placement, and display reconnect |
| High contrast | Explicit mode, system palette mapping, selection/focus colors, and retained native-caption pixel regression | User-selected contrast schemes and screen-reader use |
| Graphics loss | Injected `D2DERR_RECREATE_TARGET`, replacement frame, retained layouts, one target, and idle recovery | Actual GPU removal, driver reset, and remote-session reconnect |

Desktop-wide focus-event registration blocked in an unrelated `github` process, whose wait chain continued into `explorer`.
No unrelated process was stopped.
An isolated-desktop attempt did not provide a usable visible external UIA tree.
The default suite therefore uses process-filtered native focus events and scoped UIA property events.
The gallery probe accepts `--focus-events` for a later desktop-wide UIA event run on a responsive desktop.
The default suite does not claim that this optional run passed.
The tests did not start Narrator on the shared desktop.
Synthetic IME messages are not a real IME test.

The phase adds no images, bindings, C ABI, custom text engine, or application-owned HWND plumbing.

### Reusable sizing and scrolling (phase 1)

The September 11, 2026 ARM64 Release build passed all seven CTest programs.
The final suite took 46.51 seconds. The new scroll suite also passed three consecutive runs.
The gallery retained its existing UIA, keyboard, native text, callback, and idle assertions.
Its test now protects its own window from unrelated desktop occlusion.
The synthetic thumb gesture runs on one UI turn to prevent unrelated physical mouse messages between its three messages.

The server-only lifecycle cases preserve the existing two-handle allowance after initialization.
Both ordinary trees and ScrollView trees require one live render target and zero retained targets after closure.
The scroll/UIA suite checks cached layouts, partially clipped native bounds, native focus reveal, and native caption pixels after a high-contrast repaint.
It uses a persistent UIA apartment and a message pump during provider teardown.
Its process-handle diagnostics include the UIA client and Windows thread pools, unlike the separate server-only resource limit.

The controlled browser comparison used three fresh processes before this phase and three after the final code changes.
Both groups used `tests\measure-browser.ps1`, the same 60-file fixture, the same client size, and 96 DPI.
Values are medians with minimum–maximum ranges. Private commitment and private residency are separate counters.

| Counter | Before phase 1 | After phase 1 |
| --- | ---: | ---: |
| First-paint private bytes, MiB | 27.70 (27.69–27.72) | 27.78 (27.59–27.82) |
| Warm private bytes, MiB | 27.75 (27.74–27.77) | 27.86 (27.85–27.86) |
| Idle private bytes, MiB | 27.71 (27.69–27.72) | 27.82 (27.82–27.83) |
| Idle private working set, MiB | 13.46 (13.39–13.46) | 13.43 (13.25–13.52) |
| Stress private bytes, MiB | 27.88 (27.88–27.89) | 28.02 (27.99–28.05) |
| Stress private working set, MiB | 13.66 (13.59–13.71) | 13.74 (13.58–13.77) |
| Selection + scroll + full paint, ms | 21.38 (18.34–25.54) | 16.77 (16.49–17.86) |
| Startup through first paint, ms | 154.18 (115.83–154.58) | 165.16 (108.09–184.46) |
| HWND render targets | 1 | 1 |
| Idle process handles / threads | 250 / 9 | 250 / 9 |
| Browser executable, bytes | 368,128 (359.5 KiB) | 391,168 (382 KiB) |
| Gallery executable, bytes | 275,968 (269.5 KiB) | 314,880 (307.5 KiB) |

Idle commitment increased by about 0.11 MiB. Private working-set ranges overlap.
The cached layouts add retained text resources, while the broader gallery adds controls and native peers.
The browser retains one target and unchanged idle handle and thread counts.
These small samples do not establish a general speed improvement or attribute every Windows allocation.
No working-set trimming, thread-stack change, accessibility reduction, or forced software rendering contributed to these results.

The benchmark retained zero allocations for 20,000 model interactions at 60, 100,000, and 1,000,000 rows.
One separate run recorded 4.44, 29.98, and 74.42 ms for those sequences.
The 100,000-row and million-row name/index caches remained 4,400,022 and 44,000,022 bytes.
This phase does not change immutable snapshots, filtering, logarithmic lookup, or independent list focus and selection.

The browser and gallery ran as native desktop applications.
The visual inspection covered the browser and gallery in dark mode, plus the scrolled gallery in light and explicit high-contrast modes.
The inspection found a nested native-caption paint-order error. The backend now paints nested native pixels last, and a pixel regression covers this case.
No global Windows theme or display setting changed.

Local evidence:

- `build\phase1-ctest.txt` and `build\phase1-scroll-repeat.txt`
- `build\phase1-benchmark.txt`
- `build\memory\phase1-before.json` and `build\memory\phase1-after-final.json`
- `build\memory\phase1-after-summary.txt`
- `build\gallery-phase1-dark-top.png`, `gallery-phase1-dark-bottom.png`, `gallery-phase1-light-bottom.png`, and `gallery-phase1-contrast-bottom.png`
- `build\memory\phase1-after-final.json-1.png` through `phase1-after-final.json-3.png`

Real IME sessions, Narrator, physical mixed-DPI moves, and hardware device loss remain outside this phase.
ScrollView retains all ordinary controls. It has no arbitrary-content virtualization, automatic line wrapping, or runtime content replacement.
The public API remains C++. Later C, C#, and Rust adapters need stable handles, UI-thread rules, UTF-16 transfer rules, and callback lifetime rules.
Those adapters can use the same sizing setters, DIP scroll offsets, native EDIT bridge, and backend-only accessibility providers.
This phase adds no C ABI, image API, or custom TSF implementation.

### Memory and presentation (single-target baseline)

The September 11, 2026 comparison used native ARM64 Release builds on Windows `10.0.28637`, at 96 DPI.
The compiler, SDK, static runtime, folder path, 924-by-641-DIP client area, and initial dark theme stayed the same.
The baseline is `ab9070b`, before the memory changes. Each binary ran in five fresh processes.
Values are medians, with minimum–maximum ranges. One MiB is 1,048,576 bytes.

| Counter or workload | Before | After |
| --- | ---: | ---: |
| First-paint private bytes, MiB | 29.65 (29.59–30.25) | 27.70 (27.69–27.72) |
| Warm private bytes, MiB | 29.99 (29.85–31.17) | 27.75 (27.75–27.76) |
| Idle private bytes, MiB | 29.97 (29.82–31.14) | 27.70 (27.70–27.72) |
| Idle private working set, MiB | 15.93 (15.78–17.04) | 13.39 (13.31–13.50) |
| Idle total working set, MiB | 46.52 (46.36–47.62) | 43.99 (43.91–44.12) |
| Private bytes after refresh/theme stress, MiB | 31.25 (30.64–32.19) | 27.87 (27.86–27.89) |
| Peak commit through stress, MiB | 31.40 (30.83–32.41) | 27.98 (27.96–28.03) |
| Selection + scroll + forced full paint, ms per iteration | 117.80 (110.57–123.61) | 18.56 (17.21–18.93) |
| Startup through the measured first paint, ms | 290.48 (227.69–304.92) | 183.83 (171.97–197.28) |
| HWND render targets | 9 | 1 |
| Process handles at idle | 258 | 250 |
| Threads at idle | 9 | 9 |
| Browser executable, bytes | 372,736 | 368,128 |
| Gallery executable, bytes | 281,600 | 275,968 |

The private working set decreased by about 16%. Private commitment decreased by about 8%.
The redraw workload was about 6.3 times faster. It includes Win32 dispatch and presentation, not only model operations.

Idle GDI counts stayed at 19–21. USER counts changed from 34–35 to 26–27.
Both builds recorded 0–1 extra custom paints during each two-second idle interval on the shared desktop.
The lifecycle regression also requires a quiet interval after pending window events finish.

The change removes eight target/brush pairs from the browser, not its controls or accessibility.
MSVC Release builds also use link-time optimization. There is no new runtime dependency.
Worker count, cancellation, off-thread disposal, text quality, and the default Direct2D device selection stay unchanged.
No working-set trimming or reduced thread-stack sizes contributed to these results.

A list hover now redraws other visible controls in the same frame. It still draws only visible list rows.

**Protocol.** `tests\measure-browser.ps1` creates 60 empty files named `Entry-000.txt` through `Entry-059.txt`.
The fixture path is `build\memory\fixture-60`. The script rejects an existing fixture and deletes only its own fixture.
Each process uses the same topmost window position to reduce unrelated occlusion.
The first-paint sample requires all 60 rows, completed delivery, and a paint.

After 750 ms, the script performs 30 selection/scroll/full-redraw iterations.
It waits 250 ms before the warm sample, then two seconds before the idle sample.
Stress consists of 60 text replacements, 20 refreshes, and 20 theme changes, followed by completed delivery and two seconds of rest.
The launch sample is also available, but its exact startup stage depends on scheduling.

The runner uses HWND messages, not a UIA tree or retained providers.
It does not start an inspector. Other desktop clients and global UIA listeners remain outside its control.

Private bytes measure commitment. The working set measures resident pages, including pages marked shared.
`QueryWorkingSet` supplies the separate private-resident counter. About 30.6 MiB of resident pages remain marked shared in both builds.

The reported 16 MB user observation has no identified counter or protocol, so it is not the comparison baseline.
These measurements do not attribute individual Windows, driver, or font-cache allocations.

Run these commands from the repository root, separately from builds and other desktop tests:

```powershell
.\tests\measure-browser.ps1 -Runs 5 -Output build\memory\sample.json
.\build\arm64\Release\xui_performance_tests.exe --benchmark
.\build\arm64\Release\xui_window_tests.exe --resources
```

`-Executable` selects another preserved browser binary. `-Screenshots` also captures the owned window after the idle sample.
The script prints medians and ranges and saves the individual samples as JSON.
The optional resource probe reports heap blocks, heap-region commitment, and new process-handle types.
Heap-region totals exclude direct virtual allocations and some large heap allocations.
The regression does not impose a total-memory or timing threshold.

Local evidence is in `build\memory\before-private-ws.json`, `after-private-ws.json`, and `private-ws-summary.txt`.
The build directory also contains `perf-before-final.txt`, `perf-after-final.txt`, `resources-final.txt`, and `ctest-final.txt`.

The lifecycle probe measured 3.79 MiB of busy heap blocks and 35.87 MiB of process-private commitment with its stress window open.
After final closure, these values were 0.59 MiB and 22.54 MiB.
These are diagnostic-window samples, not browser comparison values.
The remaining commitment is not attributed to individual libraries or drivers.

The baseline lifecycle test retained 19 `DwmDxBltEvent_*` handles per repeated window with 18 standard controls and one list.
A standalone Win32/Direct2D child-target probe reproduced the event retention.
The single-target host removed this growth. The regression requires zero retained XUI targets.
These diagnostic artifacts remain local build outputs, not source dependencies.

**Large data.** Five interleaved benchmark processes used the same synthetic data and toolchain.
The new length pass changes retained capacity, not the item array or logarithmic identity lookup.

| Rows | Snapshot index/cache bytes, before → after | Snapshot ms, before → after | 20,000 interaction sequences, ms, before → after |
| ---: | ---: | ---: | ---: |
| 60 | 3,320 → 2,662 | 0.043 (0.018–0.058) → 0.026 (0.016–0.043) | 7.12 (5.32–8.86) → 6.71 (3.46–7.74) |
| 100,000 | 5,030,176 → 4,400,022 | 35.89 (30.28–46.34) → 33.09 (30.30–40.26) | 33.92 (21.65–41.65) → 31.56 (28.91–40.15) |
| 1,000,000 | 55,628,012 → 44,000,022 | 428.12 (372.12–477.25) → 411.59 (242.78–434.76) | 53.65 (42.25–66.67) → 56.29 (43.99–76.88) |

The million-row cache saves 11.09 MiB of retained capacity.
Its process-private median decreased from 250.82 to 239.70 MiB. Its working-set median stayed near 244.4 MiB.
The item strings, paths, and immutable all-row index remain storage costs.

Every interaction case still requires zero allocations.
CPU ranges overlap, so these results do not establish a general model-speed improvement.
The million-row empty query increased from a 5.32 ms median to 7.78 ms, with overlapping ranges.
That work stays off the UI thread. Small-directory presentation improved without a measured interactive latency penalty.

Physical mixed-DPI moves, real high-contrast sessions, and hardware device loss still need broader hardware coverage.

### Foundation baseline

The native ARM64 Release build completed on September 11, 2026, on Windows build `10.0.28637`.
The build used MSVC `19.44.35228`, Windows SDK `10.0.26100.0`, and CMake `3.31.6-msvc6`.
The initial executable size was **265,216 bytes (259 KiB)**. Its direct imports were Windows system DLLs.

The pure core tests and the compiled Windows/UIA smoke program passed.
The smoke program used 3,001 real files, including a filename with Japanese characters and a supplementary Unicode character.
After a resize to a 700-by-500-pixel window, the renderer submitted **13 rows**, including the buffer.
During a requested two-second idle interval, the custom paint counter increased by **0**.
`GetProcessTimes` reported **0 ms** of process CPU time during that interval.
Zero is the observed counter delta, not proof of zero CPU use.
The paint counter excludes native EDIT caret painting. These observations are not a general performance benchmark.

The smoke program exercised optional selection, removal, independent item focus, hidden-selection restoration, stable IDs after refresh, scrolling, and native text access through UIA.
It exercised Tab focus traversal, Home, End, and Page Down through posted Windows key messages.
It also exercised repeated refresh cancellation, target recreation after a display-change message, visible folder errors, and shutdown immediately after observed directory activity.
A separate UI session displayed the custom drawing and exercised the native search field.

Real IME sessions, clipboard contention, mixed-DPI monitor changes, high-contrast toggles, and hardware device loss remain unverified.
Physical shortcut chords, UIA event delivery, and complete screen-reader compatibility remain unverified.
The native text bridge delegates these text services to Windows; this milestone does not claim a custom text engine.

### Visual milestone

The updated ARM64 executable is `D:\dev\private\xui\build\arm64\Release\xui_demo.exe`.
Its size is **279,040 bytes (272.5 KiB)**.
The visual changes add **13.5 KiB** to the foundation executable.
The build uses the same toolchain and adds only the Windows `dwmapi` library.

Both CTest programs passed after the visual changes.
The smoke program submitted **10 rows for 3,001 items** after the same 700-by-500-pixel resize.
The larger header leaves less space for rows than the foundation layout.
A requested two-second idle interval produced **0 additional custom paints** and **15.625 ms** of measured process CPU time.
Earlier smoke runs reported 0 ms. These counter deltas are not a general performance benchmark.
The probe permits three interval attempts for late hover or focus events on a shared desktop.
It requires a fully quiet interval and rejects continuous repainting. The recorded run needed one interval.

The dark appearance and search hint received a visual inspection.
Automated coverage includes both palettes, theme commands, text preservation, scrollbar input, keyboard behavior, and UIA selection and scrolling.
Real high-contrast sessions, mixed-DPI monitors, IMEs, and complete screen-reader behavior still require the Windows integration phase.

### Performance milestone

The September 11, 2026 ARM64 Release executable is **296,448 bytes (289.5 KiB)**.
This adds **17 KiB** to the visual milestone.
The application adds no external dependency. Benchmark and smoke programs use Windows process-memory counters.

The final build and all three CTest programs passed.
A separate repetition passed all three programs three times each.
The regression suite also requires requests to proceed while an old source destructor remains blocked.
This check prevents large result destruction under the request mutex.

The isolated benchmark recorded these times in milliseconds:

| Workload | 100,000 rows | 1,000,000 rows |
| --- | ---: | ---: |
| Synthetic item creation | 38.31 | 327.30 |
| Snapshot and ID index construction | 32.84 | 360.20 |
| Empty query, all rows | 0.45 | 4.16 |
| `.TXT`, all matches, median | 1.38 | 10.47 |
| `FILE-0000042`, one match, median | 1.20 | 10.17 |
| `not-in-any-name`, no matches, median | 0.68 | 6.58 |
| 20,000 interaction sequences | 27.94 | 40.35 |

Both interaction sequences produced **zero allocations**.
The tenfold source increase raised the measured interaction time by approximately 1.44 times.
These measurements are observations, not latency guarantees.

| Retained memory or capacity | 100,000 rows | 1,000,000 rows |
| --- | ---: | ---: |
| Cached names and source indices, bytes | 5,030,176 | 55,628,012 |
| Empty-query view, bytes | 400,000 | 4,000,000 |
| Process private bytes | 33,181,696 | 254,779,392 |
| Process working-set bytes | 38,563,840 | 249,651,200 |

The initial benchmark process used 745,472 private bytes.
The sparse view had four bytes of index capacity. The no-match view had zero bytes of index capacity.
The full-match query had 553,020 and 4,199,476 bytes of index capacity because the vector grows during matching.
The memory samples include allocator retention after the query runs.

The final desktop run used 3,001 real files and submitted **10 rows** after the 700-by-500-pixel resize.
Nine native character messages had a maximum measured dispatch time of **7.663 ms** during filter and refresh activity.
The rapid replacement sequence completed generation **108** with the expected final row and dark theme.
The two-second idle interval produced **0 custom paints** and **0 ms** of measured process CPU time.
The demo used **42,295,296 private bytes** and **65,372,160 working-set bytes** at that point.
Shutdown after observed worker activity took **56.72 ms**.
These figures exclude fixture creation and do not prove zero CPU use or a fixed shutdown bound.

The benchmark output is `build\arm64\performance-results.txt`.
CTest output is `build\arm64\Testing\Temporary\LastTest.log`.
Build-directory artifacts remain local. The tables preserve the measured results in this README.
Real IME sessions and million-file desktop workloads remain unverified.

### Reusable-control milestone

The September 11, 2026 native ARM64 Release build adds `xui_gallery.exe`.
Both applications use the static MSVC runtime and Windows system libraries. No third-party dependency is necessary.
The existing large-list tests still require zero allocations in their interaction sequences.

| Release executable | Bytes | KiB |
| --- | ---: | ---: |
| `build\arm64\Release\xui_demo.exe` | 301,056 | 294 |
| `build\arm64\Release\xui_gallery.exe` | 210,432 | 205.5 |

The browser adds 4.5 KiB to the performance-milestone executable.
The final two-second gallery sample recorded **0 custom paints** and **0 ms** of process CPU time.
The browser sample also recorded **0 custom paints** and **0 ms** of process CPU time.
These counter deltas are observations, not proof of zero CPU use.
The final test output is `build\arm64\Testing\Temporary\LastTest.log`.

All six CTest programs passed. A complete repetition passed each program three times.
The new tests cover control state, public window lifetime, gallery actions, native input, keyboard navigation, and retained-provider invalidation.
Startup failure and callback failure tests also require a later window to run on the same thread.

Window-only captures of both gallery themes received a visual inspection.
The captures are `build\arm64\gallery-dark.png` and `build\arm64\gallery-light.png`.
These captures do not constitute a physical keyboard, screen-reader, or real IME session.

External UIA `SetFocus` calls on native EDIT returned success without a stable focus change in some automated runs on this Windows build.
The gallery smoke test uses `WM_NEXTDLGCTL` for native field focus and checks the result through UIA.
`Window::focus` uses the same native focus operation. Custom-control UIA focus actions have automated coverage.
Direct native-proxy focus transitions still require Windows interoperability work.

At this earlier milestone, the form host did not provide a general scroll container or a `FileList` adapter.
It does not provide runtime tree replacement, C bindings, a reactive property system, or a custom text engine.
Real IME sessions, mixed-DPI monitor transitions, high-contrast sessions, hardware device loss, and complete screen-reader behavior remain unverified.

### Public-list migration

The browser now runs through the same public `Window` host as the gallery.
The September 11, 2026 native ARM64 Release artifacts have these sizes:

| Release executable | Bytes | KiB |
| --- | ---: | ---: |
| `build\arm64\Release\xui_demo.exe` | 372,736 | 364 |
| `build\arm64\Release\xui_gallery.exe` | 281,600 | 275 |

The browser adds 70 KiB to the previous control milestone. The gallery adds 69.5 KiB.
Both executables include the reusable list and task backend. No third-party dependency is necessary.

All six CTest programs passed in the final complete run.
The window tests include a separate public-list application and an external UIA client.
The allocation test includes a public selection callback during every interaction sequence.
The 100,000-row and 1,000,000-row sequences both produced **zero allocations** across 20,000 operations.
Their measured durations were **19.16 ms** and **39.58 ms**.

The recorded browser smoke run submitted **10 rows for 3,001 items** after resize.
Its two-second idle interval produced **0 custom paints** and **0 ms** of measured process CPU time.
Its memory sample contained **37,937,152 private bytes** and **61,784,064 working-set bytes**.
The maximum native character dispatch was **11.90 ms** during filter and refresh work.
Shutdown after observed worker activity took **49.87 ms**.
These measurements are observations, not latency or memory guarantees.

The final dark and light browser captures received a visual inspection.
They show the retained header, search field, shortcut hint, column separator, vector icons, list surface, and status label.
Local artifacts are:

- `build\arm64\browser-public-dark.png`
- `build\arm64\browser-public-light.png`
- `build\arm64\browser-public-results.txt`
- `build\arm64\performance-public-results.txt`
- `build\arm64\public-migration-ctest.txt`

One repeated gallery run failed its strict focus assertion after a native EDIT value update.
The unchanged gallery test then passed three consecutive runs and the final complete suite.
The native-proxy focus caveat remains. The assertions did not change.
Real IME sessions, mixed-DPI transitions, high-contrast sessions, and complete screen-reader behavior still require manual coverage.
The API does not include C bindings, a general scroll container, or arbitrary row templates.

## Feature bindings (1.1 extension)

### Fluent C# setters

C# configuration methods return the original object, with its concrete type.
Size methods and shared control methods preserve this type throughout a chain.

```csharp
var map = w.MapView("Offline map")
    .SetView(new(47.6, -122.3), 4)
    .SetMarkers([new(1, new(47.6, -122.3), "Seattle")])
    .FixedSize(650, 180);

var range = w.RangeInput("Zoom")
    .FixedSize(200, 24)
    .SetRange(new(0, 100))
    .SetValue(20);
```

Writable properties retain their assignment syntax and also have `Set<Property>` methods.
For example, `range.Value = 20` and `range.SetValue(20)` use the same setter.
`SetText` uses the document setter for rich and multiline text controls, including references typed as `Control`.
Setters retain their validation and UI-thread requirements.
They return the receiver only after the operation succeeds.
Events, lifecycle methods, and methods that return resources retain their existing contracts.

The native ABI does not change.
The managed method signatures change, so consumers must rebuild.
Size methods are now generic extension methods in the `Xui` namespace.
Void delegate assignments can require a lambda, such as `Action apply = () => range.SetValue(20);`.

### Feature extension

This extension supersedes earlier statements that the new controls have C++ APIs only.
The original nine control kinds remain available.
The extension adds 35 typed constructors to both C# and Rust.
These include compositions and the earlier workspace controls.
The bindings use the existing native controls, layout, drawing, input, and accessibility.
They contain no second renderer or retained row array.

### Current coverage

| Family | C# and Rust types | Usable contracts |
| --- | --- | --- |
| Numeric and exclusive choice | `RangeInput`, `NumericInput`, `RadioGroup`, `ComboBox`, `Progress` | Ranges, values, orientation, selection, editable combo creation, state, events |
| Actions and disclosure | `SplitButton`, `Expander`, `Popup` | Independent actions, expanded state, arbitrary popup content, explicit owner and anchor |
| Virtual collections | `ItemsView`, `TreeView`, `ImmutableSource` | Constant-cost identity lookup, bounded row callbacks, compact select-all, lazy tree requests, owner-bound completion |
| Layout and workspace | `Grid`, `Wrap`, `AdaptiveLayout`, `TabStrip`, `SplitView`, `PageView` | Tracks, cells, wrapping, breakpoints, retained panes, tabs, active pages |
| Data and history | `DataGrid`, `HistoryChart` | Immutable source, logical columns, resize, reorder, filter state, sort state, check selection, samples |
| Commands | `CommandBar`, `CommandSurface` | Nested command records, separate pin actions, shortcuts, hints, overflow, explicit popup display |
| Navigation | `Breadcrumb`, `NavigationPane`, `LocationPicker`, `ViewPicker` | Path segments, immutable sources, typed borrowed children, presentation and size controls |
| Documents | `MultilineText`, `RichText`, `PasswordInput` | Native editing, authored runs, selection, editing commands, read-only mode, scoped password access |
| Forms | `DateTimePicker`, `ColorPicker`, `InlineStatus`, `ContentDialog` | Local Gregorian fields, RGBA values, status dismissal, arbitrary dialog content, validation message, result events |
| Scenes and maps | `VectorCanvas`, `MapView` | Immutable shapes, transforms, clips, stable IDs, offline markers, view state, cancelable overlay tokens |
| Native hosts | `MediaPlayback`, `WebContent` | Explicit local media load, volume, seek, playback actions, allowed origins, HTML, JavaScript result tokens, stop and unload |
| Window integration | `Window` | Optional custom title bar and explicit native Shell menu fallback |

`include\xui\xui_features.h` declares the extension through `xui.h`.
`bindings\generate_features.py` generates both FFI declarations from that header.
It generates typed constructors and scalar properties from `bindings\features.json`.
The handwritten feature modules implement collections, scoped secrets, request ownership, and typed records.

The baseline `xui_abi_version()` remains `0x00010000`.
Old records, kind values, exports, and version negotiation remain unchanged.
`xui_feature_version()` returns `0x00010001`.
Top-level feature options and values contain explicit size and version fields.
Other record arrays validate their sizes.
The wrappers check the feature major version before construction.
`xui_capabilities()` reports build support for WebView2 without starting a runtime.

### Ownership and data limits

All UI calls, source callbacks, and completions require the creating UI thread.
Background tasks must deliver their results through an application-owned UI queue.
The wrappers do not capture a synchronization context or marshal worker calls automatically.
C# uses a disposable window owner. Rust uses `Rc` ownership and does not implement `Send` or `Sync` for controls.
Borrowed children retain their window owner and reject use after native disposal.

The immutable source interface supplies `Count`, `Key`, `Find`, and row content.
`Find` must not enumerate the source. The count and identity mapping must remain stable for each snapshot.
The binding copies only requested row fields, not the complete source.
Selection exposes the focused key and compact term count.
`Contains` in C#, or `contains` in Rust, tests membership without expanding all selected identities.
Each primary or secondary field has a 1,024-byte UTF-8 limit.
An oversized field fails instead of triggering a full-row-array fallback.
Source callbacks must remain nonblocking and must not call another source callback.
The ABI rejects mutations of the source window during a source callback.

Source contexts use native retain/release ownership.
C# snapshots implement `IDisposable`. Rust releases a snapshot handle when its last wrapper drops.
Attached controls and selection terms retain their own source references.
Replacing a source can therefore release its old context without waiting for window destruction.
Managed exceptions and Rust panics become callback failures.
Different controls can dispatch nested events. Recursion on the same event source fails with `XUI_BUSY`.
Window destruction remains blocked during a callback or `Run`.
Closing a window revokes delivery before destruction releases its source contexts.

Tree requests expose their stable `ItemKey`.
Map and tree completion tokens belong to one control, not merely a generation number.
Successful completion consumes a token. Cancellation or disposal revokes it.
Rust request objects cancel on drop. C# request objects implement `IDisposable`.
Window destruction also revokes all remaining tokens.

Document strings cross the ABI as UTF-8. Selections count UTF-16 code units.
The selection setter rejects offsets inside a surrogate pair.
Password controls have no ordinary plaintext getter.
C# `WithPassword` supplies a scoped UTF-8 span. Rust `with_password` supplies a borrowed byte slice.
Native temporary plaintext buffers are erased after the callback.
The password limit is at most 4,096 UTF-16 code units.

Creating a media or web control starts no optional runtime.
Only explicit source loads start the native hosts.
JavaScript evaluation returns a disposable native-owned result token.
`TryGetResult` or `try_result` reads completion on the UI thread.
No managed delegate or Rust closure remains pinned for JavaScript completion.
Stop, unload, source replacement, hidden-host unload, and window close reject stale results.
Token disposal releases storage even when WebView2 never calls its completion handler.

### Examples and validation

Regenerate the declaration files after a feature-header or manifest change:

```powershell
python bindings\generate_features.py
```

Run the isolated binding validation:

```powershell
.\tests\binding-features.ps1 -BuildDirectory build\controls
```

The script builds the native ABI tests, .NET framework-dependent clients, NativeAOT clients, and Rust clients.
It runs model tests, Clippy, formatting, and owned-window smoke cases.
It stores logs and deployment copies under `build\controls\bindings-validation`.
It does not replace older application deployments or run a memory probe.
The historical `tests\phase4.ps1` still targets `build\arm64`; it is not the entry point for this build.

Run one representative application:

```powershell
.\build\controls\bindings-validation\Sample-fdd\Sample.exe --features
.\build\controls\bindings-validation\Sample-aot\Sample.exe --features
.\build\controls\bindings-validation\rust\xui-sample.exe --features
.\build\controls\Release\xui_feature_sample.exe
```

Each application shows choices, a range, an offline map, and a million-row virtual source.
F6 opens a document and color dialog. Escape cancels it. F8 changes the range. F12 closes the window.
The `--callback-fail` argument makes the F8 event report a controlled callback failure.
The source logs its actual visible-row fetch count after a normal run.
The examples perform no Shell verb and load no remote website.

A C# range uses ordinary typed properties:

```csharp
using var window = new Xui.Window();
var root = window.Stack();
var range = window.RangeInput("Volume");
range.Range = new(0, 100, 1, 10);
range.Value = 25;
range.Event += e => Console.WriteLine(range.Value);
root.Add(range);
window.SetContent(root);
window.Run();
```

The complete source examples are `FeatureDemo.cs`, `sample\src\features.rs`, and `bindings\native\features.cpp`.
The ABI constructor test covers all 35 added kinds.
Both language test suites exercise typed properties, source limits, callback failures, and disposal.
The tests preserve the existing native focus and accessibility assertions.

### Advanced API gaps

Coverage is family-level, not full C++ method parity.
The extension does not expose custom collection groups, full-source selection domains, rectangle gestures, or custom row action/icon metadata.
It does not expose navigation-query completion, command-query completion, DataGrid filter-worker completion, or the abstract Shell provider session.
Applications can replace immutable snapshots explicitly and handle command or header events.
DataGrid filter and sort setters store state; applications supply the filtered or sorted snapshot.

Map route polylines, custom date bounds, locale selection, color swatch configuration, and live dialog validation functions remain C++ APIs.
Dialogs instead accept a validation message that applications update with their form state.
JavaScript completion uses result polling rather than a language callback subscription.
Optional browser and media adapter interfaces remain internal.
The source interface is UI-thread-only and does not supply a worker dispatcher.

The existing memory measurements in this README remain historical evidence.
This extension makes no new process-memory reduction claim and does not alter the gallery memory baseline.

## C ABI and language bindings (phase 4)

This section describes the original ABI baseline and its historical measurements.
The feature extension section supersedes its control-coverage limitations.
The native engine owns layout, drawing, input, accessibility, image work, and virtual rows.
C# and Rust supply control properties, data, and event handlers. Neither binding contains a second retained engine or a row painter.
The existing static C++ targets do not load the new DLL.

### Files and supported surface

| Path | Purpose |
| --- | --- |
| `include\xui\xui.h` | Public C header, calling convention, fixed-width structures, statuses, and exports |
| `src\c_api.cpp` | Handle registry, UTF-8 conversion, ownership checks, batches, and exception boundary |
| `bindings\dotnet\Xui` | Source-generated `LibraryImport` declarations and managed wrapper classes |
| `bindings\dotnet\Sample` | Actual managed application |
| `bindings\dotnet\Tests` | Separate executable wrapper tests |
| `bindings\rust\xui-sys` | Raw `repr(C)` declarations and import-library linkage |
| `bindings\rust\xui` | Safe `Rc`-owned wrappers, `Result`, and callback containment |
| `bindings\rust\sample` | Actual Rust application |
| `bindings\native\direct.cpp` | Equivalent static C++ application |
| `bindings\native\sample.cpp` | Equivalent C++ application through the C ABI |
| `tests\phase4.ps1` | Reproducible builds, tests, deployment copies, and measurements |

The surface covers Window, Stack, Label, Button, Toggle, TextInput, ScrollView, Image, and FileList.
Sizing includes automatic, preferred, fixed, minimum, and maximum sizes.
Properties also cover text, accessible names, automation IDs, enabled state, checked state, padding, spacing, scroll offsets, and window themes.
The engine retains the same fonts, theme tokens, cached text layouts, and one render target per window.

FileList accepts copied item arrays with stable 64-bit IDs, synchronous filters, selection, and view/selection events.
Each item array contains at most 1,000,000 rows.
Item ID zero is valid. Selection presence has a separate result.
Hidden selections retain their IDs. Row indices refer to the visible view.
`UINT32_MAX` clears selection. Duplicate item IDs return an error without replacement of the previous view.

The C FileList API is a small-data convenience, not the asynchronous `ViewTask` API.
Large source construction and filtering can block its UI thread.
The existing native asynchronous API remains available to C++ applications.
The bindings do not expose arbitrary drawing, mutable borrowed spans, context menus, clipboard operations, tree replacement, or custom row templates.

Image accepts a copied file path and decode bounds from 1 through 1,024 pixels per dimension.
An empty path unloads the image. The status query returns Empty, Loading, Ready, or Error.
The bindings do not expose image error details, image resource limits, or image resource counters.
Synchronous API errors still use the status and diagnostic contract below.

### ABI contract

Version `0x00010000` identifies this interface.
`xui_abi_version()` supplies the runtime version. Window creation checks the caller version again.
The current wrappers require an exact version match and report a mismatch before object use.
The high 16 bits identify the major version. The low 16 bits identify the minor version.

Version 1 uses exact structure sizes, not an implicit append-only layout.
Reserved fields must be zero. Unknown properties and kinds return an error.
Future additions must preserve existing exports and accepted version-1 structures.
A larger structure needs a new versioned entry point or an explicitly supported size.
A breaking change needs a new major version and a distinct deployment contract.
Replacing this DLL with an incompatible runtime is not supported.

| C structure | ARM64 size | Important offset |
| --- | ---: | ---: |
| `xui_string` | 16 | `length`: 8 |
| `xui_window_options` | 40 | `title`: 8 |
| `xui_property` | 56 | `integer`: 48 |
| `xui_event` | 24 | `source`: 8 |
| `xui_file_item` | 48 | `id`: 8 |

All exported functions use C linkage and `__cdecl`. The header exposes no STL or C++ class layout.
Handles are nonzero 64-bit generation tokens, not addresses.
A process-wide monotonic counter never reuses a token. Exhaustion returns an error instead of wraparound.
Registry lookup rejects arbitrary, stale, and wrong-kind tokens before native object access.
The registry lock also protects concurrent lookup. A valid handle still requires its creating UI thread.
Raw C buffer pointers must reference accessible caller-owned memory for their stated lengths.
The ABI cannot make an invalid arbitrary buffer pointer safe.

### Ownership, strings, and errors

A window owns an arena of at most 65,536 handles, including its own handle.
Controls remain valid until `xui_window_destroy`. There is no independent control-release operation.
The window retains detached controls too. This policy gives callback targets and tree children one explicit lifetime.
Destroy revokes every subscription and handle before native resource release.
A later call with any revoked handle returns `XUI_INVALID_HANDLE`.

All object operations, callbacks, and destruction use the creating UI thread.
Tree construction finishes before `xui_window_run`. A window runs once.
Only one window can run on a UI thread at a time.
The caller must use STA-compatible COM initialization, not MTA.
`close` requests closure. It does not destroy the arena.
Destroy during a run or callback returns `XUI_BUSY` without partial teardown.

Strings use UTF-8 with explicit byte lengths. The input spans remain valid only for the duration of the call.
The native engine copies strings and item arrays. Callers keep ownership of all input and output buffers.
No caller frees memory from the native allocator.
Embedded NUL, malformed UTF-8, nonzero reserved fields, and strings larger than 1 MiB return `XUI_INVALID_ARGUMENT`.
The managed wrapper also rejects unpaired UTF-16 surrogates. Rust `str` already requires valid UTF-8.
TextInput retains its native 1,024-UTF-16-unit limit and preserves surrogate pairs at that limit.
The text property updates native input text for TextInput and the displayed name for other controls.
Native output replaces unpaired UTF-16 units with U+FFFD.
EDIT can publish a temporary high surrogate before the next `WM_CHAR` completes the pair.
This output policy keeps foreign callbacks valid without changes to the native text buffer.
The final pair returns its original Unicode scalar. Caller-supplied malformed UTF-8 remains an error.

Copy functions return a required byte count without a terminator.
A null buffer with zero capacity requests the count.
An insufficient buffer returns `XUI_BUFFER_TOO_SMALL` without a partial copy.
The caller supplies a larger buffer and repeats the call on the same thread.
Empty output requires zero bytes and succeeds with a null, zero-capacity buffer.

Every status-returning export contains native exceptions, including allocation failures.
Successful exports clear the thread-local error. Failed exports replace it with a status and diagnostic.
Diagnostics contain at most 1,023 UTF-8 bytes without a partial code point.
Malformed bytes in an unexpected native exception message become question marks.
`xui_error_copy` is the exception: it never changes the stored error, even during a size query.
Its own return value describes the copy operation. Its `error` output identifies the original error.
This separation removes ambiguity between a failed operation and an insufficient diagnostic buffer.

A callback receives a temporary `xui_event` and returns zero on success.
The event has no borrowed text pointer. Text access uses `xui_text_copy`.
Key values contain the virtual key in the low bits, Ctrl in bit 32, and Shift in bit 33.
Key callbacks report errors, not key consumption. Native default keyboard behavior continues.
Callbacks must not let foreign exceptions escape.

Native dispatch contains unexpected C++ callback exceptions too.
A nonzero callback result becomes a persistent window failure and requests closure.
The enclosing invoke, list operation, or run returns `XUI_CALLBACK_FAILED`.
`xui_window_callback_error` returns the original callback status after that operation.
The wrappers preserve the managed exception or Rust error/panic message.
Recursive event delivery returns a callback failure instead of recursive foreign borrowing.

A null callback revokes its subscription synchronously.
Revocation inside the active callback is valid. The current dispatch finishes, but no later dispatch uses that context.
Destroy remains invalid until the callback and run return.
Callbacks can change properties, revoke subscriptions, and request closure.

### Batched updates

`xui_update` accepts at most 4,096 property records.
It checks all handles, kinds, numeric values, structure sizes, and UTF-8 strings before mutation.
An invalid record leaves the entire batch unchanged.
The apply phase does not invoke application property callbacks or pump the message loop.
Native invalidation combines the resulting work for the next frame.
Separate setter calls also combine invalidations until the message loop draws.

A batch is not a database transaction against resource exhaustion.
A native allocation or platform failure during apply can leave earlier valid properties applied.
The caller receives an error, not success.
The throughput benchmark distinguishes batched boundary calls from individual calls. It does not claim a different renderer or a paint per setter.

### Managed ownership and use

`Window` implements `IDisposable`. Each managed element belongs to that window.
Disposal revokes native handles before it frees callback contexts.
Element access after disposal throws `ObjectDisposedException`.
Wrong-thread access throws `XuiException` before the P/Invoke call.
Dispose during a callback or `Run` throws a busy error. A callback uses `Close` instead.

These UI-thread-owned handles deliberately do not use a finalizer-driven `SafeHandle`.
A finalizer cannot safely destroy HWNDs or revoke callbacks on the UI thread.
The wrapper has no finalizer. Applications must dispose their windows.
Failure to dispose retains the native arena until process exit.

Subscriptions use weak `GCHandle` contexts and strong managed ownership during `Run`.
The native registration does not permanently root the managed window.
The trampoline retains its current subscription through callback completion, including self-unsubscribe.
Every trampoline catches managed exceptions and returns a native callback error.
CsWin32 is not involved. It generates Windows API bindings, not arbitrary C++ class bindings.

This example requires a C# entry point with `[STAThread]`:

```csharp
using Xui;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        using var window = new Window("Managed XUI", 480, 240);
        var root = window.Stack();
        root.Padding(20);
        root.Spacing(12);
        var label = window.Label("Ready 😀");
        var button = window.Button("Apply");
        button.Click += () => label.Text = "Applied";
        root.Add(label);
        root.Add(button);
        window.SetContent(root);
        window.Run();
    }
}
```

`Window.Update` accepts a span of `Property` records.
`Button.Click`, `Toggle.Changed`, `TextInput.Changed`, `TextInput.Submitted`, and `Window.Key` provide managed events.
`Control.Event` supplies the lower-level event record, including FileList events.
The managed assembly is `Xui.Managed.dll`. Its name avoids a Windows loader collision with the native `xui.dll`.

### Rust ownership and use

The raw `xui-sys` functions remain unsafe. The `xui` crate exposes owned strings and checked operations.
Private raw handles prevent callers from constructing a dangling safe wrapper.
`Window` and every element contain `Rc` ownership. They are neither `Send` nor `Sync`.
Compile-fail doctests enforce these restrictions.
The last owner destroys the native arena on its UI thread.
Elements can outlive the Rust `Window` variable because they retain the same arena.

Each trampoline retains its callback slot before foreign code runs.
Unsubscribe and subscription replacement revoke native use before old slot release.
The callback closure uses checked `RefCell` access.
`catch_unwind` contains panics, records the diagnostic, and returns failure to native dispatch.
The release profile uses `panic = "unwind"` so callback containment remains active.
Abort-mode panics, process aborts, and allocation aborts are not recoverable callback errors.

Use weak captures for callbacks that refer to their own window or controls.
A strong `Rc` capture can form an ordinary Rust ownership cycle.
`downgrade` and `upgrade` provide the cycle-free path. The sample uses that path for every retained control capture.
The native renderer never calls a Rust painter or borrows a Rust item span after a call.

```rust
use xui::{Axis, Result, Window};

fn main() -> Result<()> {
    let window = Window::new("Rust XUI", 480., 240.)?;
    let root = window.stack(Axis::Vertical)?;
    let label = window.label("Ready 😀")?;
    let button = window.button("Apply")?;
    let weak = label.downgrade();
    button.on_event(move |_| {
        if let Some(label) = weak.upgrade() {
            label.set_text("Applied")?;
        }
        Ok(())
    })?;
    root.add(&label, 0.)?;
    root.add(&button, 0.)?;
    window.set_content(&root)?;
    window.run()
}
```

### Build, run, and reproduce

The recorded toolchains are native ARM64:

- MSVC compiler `19.44.35228.0`, toolset directory `14.44.35207`, Windows SDK `10.0.26100.0`.
- .NET SDK `10.0.401`, .NET runtime `10.0.12`, RID `win-arm64`.
- NativeAOT packages `Microsoft.DotNet.ILCompiler` and `runtime.win-arm64.Microsoft.DotNet.ILCompiler`, both `10.0.12`.
- Rust `1.97.1`, Cargo `1.97.1`, host and target `aarch64-pc-windows-msvc`.

NativeAOT completed with the installed Visual Studio native SDK.
NuGet supplied the matching compiler packages during publish.
No additional native framework runtime or third-party Rust crate is necessary.
Rust links the generated `xui.lib`. `XUI_LIB_DIR` can select another import-library directory.
The workspace configuration selects ARM64 and the static CRT.

From the repository root, run the complete phase:

```powershell
& .\tests\phase4.ps1 -Runs 5
```

The existing build must have `XUI_DESKTOP_TESTS=ON`.
The script runs the full native suite, managed tests, Rust tests, Clippy, actual UIA clients, and fresh-process measurements.
Logs, deployment copies, and raw JSON remain under `build\phase4`.
The script does not stage, commit, or change branches.
For an independent check without new measurements, run `& .\tests\phase4.ps1 -SkipMeasurements`.
Each command log records its exit code, including commands with no output.

For individual native commands, run:

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Preview\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --build build\arm64 --config Release --parallel 4
& (Join-Path (Split-Path $cmake) "ctest.exe") --test-dir build\arm64 -C Release --output-on-failure
```

For framework-dependent and NativeAOT builds, run:

```powershell
dotnet publish bindings\dotnet\Sample\Sample.csproj -c Release -r win-arm64 -p:SelfContained=false "-p:PublishDir=$pwd\build\phase4\dotnet\"
dotnet publish bindings\dotnet\Sample\Sample.csproj -c Release -r win-arm64 -p:PublishAot=true "-p:PublishDir=$pwd\build\phase4\aot\"
Copy-Item build\arm64\Release\xui.dll build\phase4\dotnet\
Copy-Item build\arm64\Release\xui.dll build\phase4\aot\
& .\build\phase4\dotnet\Sample.exe
& .\build\phase4\aot\Sample.exe
```

For Rust, run:

```powershell
$env:PATH = "$pwd\build\arm64\Release;$env:PATH"
Push-Location bindings\rust
cargo build --workspace --release
cargo test --workspace --release
cargo clippy --workspace --all-targets --release -- -D warnings
& .\target\aarch64-pc-windows-msvc\release\xui-sample.exe
Pop-Location
```

Each application accepts an optional image path.
The complete script also publishes and runs separate framework-dependent and NativeAOT test executables.
Measured application packages do not contain the unit tests.
F6 updates the status through the key callback. F7 reports image readiness.
F8 replaces the image request. F12 requests closure.
The UIA harness uses these controls without global clipboard, IME, display, or contrast changes.

Framework-dependent deployment includes the apphost, managed assemblies, JSON files, and `xui.dll`.
It requires the ARM64 .NET runtime on the destination machine.
NativeAOT deployment includes `Sample.exe` and `xui.dll`. Its executable contains the required compiled runtime support.
Rust and ABI C++ deployment each include one executable and `xui.dll`.
Static C++ deployment includes only its executable.
PDB files are optional diagnostics, not required deployment files.

### Binding validation and measurement scope

The C test compiles the public header as C11 and checks layouts, imports, version discovery, and lifecycle.
The adversarial ABI test covers invalid/stale tokens, kinds, threads, structure sizes, malformed strings, overflow, ownership, callbacks, and batch rejection.
It also covers self-unsubscribe, busy destruction, thrown C++ callbacks, recursive dispatch, persistent callback codes, and repeated arena teardown.
Managed tests cover Unicode, disposal, wrong-thread access, ownership, batch rejection, subscription lifetime, callback exceptions, and list identity.
Rust tests add raw layout checks, RAII ownership, panic containment, callback recursion, and compile-time thread restrictions.

The same external UIA harness runs all five actual applications.
It checks Unicode labels, Invoke, Toggle, native EDIT ValuePattern, Unicode input, submit, key callbacks, retained scrolling, images, and virtual lists.
The keyboard check sends both surrogate units through native `WM_CHAR` and reads text inside foreign callbacks.
It requires one render target, a quiet idle interval, callback closure, a successful process exit, and rejection from a retained provider.
Separate GUI runs require callback exceptions or panics to produce a nonzero application result.
The harness also requests image replacement shortly before closure.

This Windows EDIT provider reports no TextPattern in these runs. ValuePattern and real native editing passed.
The earlier native comparison tests cover parity with an unmodified EDIT provider.
The binding reload/close sequence exercises a race, not a deterministic blocked-codec gate.
The native image-window test separately covers a blocked decoder and nonblocking UI closure.
The CPU and controlled GPU ledgers do not bound WIC transient memory, hardware residency, or total process memory.

All five measured applications use a 600-by-720-DIP window, the dark theme, the same native fonts, and the same controls.
The small workload contains eight preference labels, 60 files, and one generated PNG.
No UIA client runs during memory measurements.
Startup starts before `Process.Start` and ends after a nonzero native paint counter.
The probe disables console creation for every measured GUI process.
Memory follows two seconds of idle time. A separate half-second interval supplies the idle paint delta.
Processes are fresh, but file caches are warm. These results are not reboot-cold startup measurements.

Each throughput process applies 64,000 text mutations before the message loop starts.
The individual path makes 64,000 calls. The batch path makes 1,000 calls with 64 properties each.
The managed measurements include wrapper encoding, pinning, and boundary overhead.
The native static path has no foreign boundary and no separate batch operation.
These numbers measure property submission, not layout, paint throughput, or interactive frame latency.
Raw measurements and exact deployment file lists are in `build\phase4\measurements.json`.

Real IME sessions, Narrator speech, physical mixed-monitor transitions, and actual GPU removal remain manual coverage.
The automated tests do not claim those results.

### Phase 4 results

The measurement command was `tests\phase4.ps1 -Runs 5` on September 11, 2026.
The complete output is `build\phase4\verification-final.log`.
That run passed 14 native tests in 150.44 seconds and all five normal UIA runs.
The five normal runs completed 76, 76, 78, 77, and 76 assertions, respectively.
The independent handoff check did not reproduce all desktop passes.

The independent command was `tests\phase4.ps1 -SkipMeasurements`.
Its output is `build\phase4\verification-resumed.log`.
The browser focus check failed, and the gallery, window, and scrolling tests timed out.
A separate browser retry also failed at native EDIT keyboard focus.
During the focus probe, Task Manager retained foreground ownership while the browser thread reported EDIT focus.
This observation does not establish the cause of the three timeouts.

The separate binding command was `tests\phase4.ps1 -SkipNativeTests -SkipMeasurements`.
Its output is `build\phase4\bindings-resumed.log`.
All binding builds and unit tests passed. The first normal UIA run failed at stable native keyboard focus.
Separate runs then covered all five clients. Each normal run failed at that focus check, but all five callback-failure runs passed.
`smoke-resumed-results.json` and `*-resumed.log` retain these results.
No assertion changed, and no unrelated process or Windows setting changed.
The desktop failures remain an open handoff limitation, not a new successful validation claim.

All DLL-based packages contain the same native DLL, with SHA-256 `4d6c3af486c194e1bbc6a563de9ebfea3d20f086423ff7451b8665c364ce40bf`.
`build\phase4\binary-manifest.json` records executable sizes, hashes, and PE machine fields.
The manifest reflects the independent rebuild. The measured package sizes still match every current deployment file.
The measurement process checks identified all five applications as native ARM64, without x64 emulation.
The current native executables and DLLs also retain ARM64 PE machine fields.

| Validation | Independent result | Artifact under `build\phase4` |
| --- | --- | --- |
| Native CTest suite | 10/14 passed, 254.83 seconds | `native-tests.log`, `verification-resumed.log` |
| Adversarial ABI checks | 347 assertions passed | `abi-resumed-tests.log` |
| C11 header/layout/import test | Passed | `native-tests.log` |
| C# framework-dependent tests | 17 assertions passed | `dotnet-fdd-tests.log` |
| C# NativeAOT tests | 17 assertions passed | `dotnet-aot-tests.log` |
| Rust safe-wrapper tests | 5/5 passed | `rust-tests.log` |
| Rust raw layout/import test | 1/1 passed | `rust-tests.log` |
| Rust compile-fail thread checks | 2/2 passed | `rust-tests.log` |
| Rust Clippy and format checks | Passed | `rust-clippy.log`, `rust-format.log` |
| Real UIA application runs | 5/5 failed at native keyboard focus | `*-uia-resumed.log` |
| GUI callback-failure runs | 5/5 passed | `*-callback-failure-resumed.log` |

The UIA run names are `cpp_static`, `cpp_abi`, `dotnet_fdd`, `dotnet_aot`, and `rust`.
All five measured applications retained one render target and produced zero custom paints in each measured idle interval.

#### Deployment sizes

These are uncompressed file bytes, without PDBs or development import libraries.
The .NET SDK is a build tool, not an application runtime requirement.

| Application | Executable bytes | Native DLL bytes | Required package bytes | External language runtime |
| --- | ---: | ---: | ---: | --- |
| C++ static | 501,248 | 0 | 501,248 | None |
| C++ C ABI | 259,072 | 400,384 | 659,456 | None |
| C# framework-dependent | 140,800 | 400,384 | 580,607 | ARM64 .NET 10 |
| C# NativeAOT | 1,330,688 | 400,384 | 1,731,072 | None |
| Rust | 288,256 | 400,384 | 688,640 | None |

The framework-dependent package also contains the managed assemblies and runtime JSON files.
The installed `Microsoft.NETCore.App\10.0.12` directory contains 90,017,677 file bytes.
Its `hostfxr` directory adds 359,720 bytes. Together, these shared runtime files occupy 86.19 MiB by file length.
This count excludes the SDK, ASP.NET, Windows Desktop, and filesystem allocation overhead.
`installed-runtime.json` records the separate counts.
NativeAOT contains compiled runtime support inside its executable. It does not require those installed .NET directories.
All variants still require the Windows system components.

The dependency reports are `xui.dll.dependencies.log`, `Sample.exe.dependencies.log`, and `xui-sample.exe.dependencies.log`.
The native DLL and Rust executable do not require a redistributable CRT DLL.
`exports.log` records the 22 public C exports.

#### Startup and process memory

Each row reports the median of five fresh processes.
The raw measurement timestamp is `2026-09-11T20:24:13.1984472-05:00`.
`summary.json` derives its values from that file. The independent handoff check did not repeat measurements.
The startup range retains every sample, including the 2.58-second first static-C++ sample.
Observed startup includes launcher and probe overhead. It is not a renderer-only timestamp.
The shared desktop and first-use work produce substantial variation, so these results do not establish a reliable startup ranking.

| Application | Startup median, ms | Startup range, ms | Private commit, MiB | Private WS, MiB | Total WS, MiB |
| --- | ---: | ---: | ---: | ---: | ---: |
| C++ static | 216.04 | 188.33–2,583.48 | 26.828 | 11.816 | 43.445 |
| C++ C ABI | 218.98 | 192.90–400.88 | 26.875 | 11.082 | 42.805 |
| C# framework-dependent | 339.29 | 292.92–520.45 | 32.609 | 14.520 | 60.277 |
| C# NativeAOT | 257.70 | 225.41–387.94 | 29.215 | 12.535 | 45.824 |
| Rust | 208.80 | 185.53–310.35 | 26.906 | 11.812 | 43.520 |

The ABI C++ median adds 0.047 MiB of private commit over the equivalent static C++ application.
Rust adds 0.078 MiB. Framework-dependent C# adds 5.781 MiB, and NativeAOT adds 2.387 MiB.
These are observed workload differences, not fixed runtime taxes.
The managed package comparison also excludes its shared installed runtime from per-application deployment bytes.

#### Property submission

The table reports median elapsed milliseconds for 64,000 text mutations.
The individual path uses 64,000 calls. The batch path uses 1,000 calls.
The static C++ API has no separate batch boundary in this benchmark.

| Application | Individual calls, ms | Batches of 64, ms |
| --- | ---: | ---: |
| C++ static | 9.672 | Not applicable |
| C++ C ABI | 33.794 | 27.131 |
| C# framework-dependent | 90.918 | 37.501 |
| C# NativeAOT | 34.028 | 31.019 |
| Rust | 29.541 | 20.229 |

The native renderer combines frame invalidation in both paths.
The batch benefit comes from fewer boundary calls and fewer wrapper/preparation allocations, not fewer retained controls.
The results do not include layout, painting, or per-row callbacks.

#### Existing browser regression

The browser still uses static linkage and does not import `xui.dll`.
Its DLL deployment overhead is zero bytes.
The browser, gallery, and image executable sizes remain 419,840, 347,648, and 390,656 bytes.
`xui_demo.exe.dependencies.log` records the unchanged static browser dependency boundary.

The before measurement contains three processes. The final after measurement contains five.
Both use the unchanged `tests\measure-browser.ps1` workload and no external UIA client.

| Browser metric | Before median | After median |
| --- | ---: | ---: |
| Idle private commit, MiB | 27.887 | 27.852 |
| Idle private WS, MiB | 13.469 | 13.500 |
| Idle total WS, MiB | 44.160 | 44.145 |
| Ready/painted startup, ms | 172.075 | 168.656 |
| Warm interaction sample, ms | 19.774 | 16.827 |
| Handles | 250 | 250 |
| Threads | 9 | 9 |
| Render targets | 1 | 1 |
| Additional custom idle paints | 0 | 0 |

The raw files are `browser-before.json` and `browser-after.json`.
Earlier runs remain in `browser-after-preliminary.json` and `browser-after-first-five.json`.
One earlier desktop run showed 274 handles and higher working-set values without a browser binary change.
The final repeated run returned to 250 handles. This observation is another reason to retain raw data and avoid universal memory claims.
Preliminary binding measurements also remain in `measurements-preliminary.json` and `measurements-first-no-console.json`.
