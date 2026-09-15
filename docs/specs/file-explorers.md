# File explorer samples

The C# and C++ explorers are separate applications with different shortcuts and layouts.
See [CONTRIBUTING](../../CONTRIBUTING.md#c-file-explorer) for the C# build and watch commands.
See [CONTRIBUTING](../../CONTRIBUTING.md#native-samples) for the C++ executable.

## C# file explorer

`bindings\dotnet\FileExplorer` contains the C# explorer demo.
It uses the XUI controls through the public .NET bindings.
Its `.xui` files define the workspace, reusable file panes, navigation sidebar, and palette.
The C# controllers retain filesystem operations, commands, event handlers, and persistent state.
The explorer selects `VisualStyle.WinUI` for both light and dark themes.
Other applications retain the classic style unless they explicitly select WinUI.
The existing C++ explorer remains available as `xui_demo.exe`.

### Edit the markup

`ExplorerLayout.xui` composes the sidebar, file panes, and notification.
`FilePaneLayout.xui` defines each pane, including its toolbar, file grid, Find row, and status.
`SidebarLayout.xui` defines the navigation control.
`PaletteLayout.xui` defines the folder and command palette.
Generated control references connect these layouts to their C# controllers.
The `FindOpen` state updates the Find row height and control visibility.

After the native build, use the [restart-on-save command](../../CONTRIBUTING.md#c-file-explorer) for markup changes.

### Explorer controls

The title bar contains a navigation button, independent tab strips for each pane, and Windows caption controls.
Each tab row follows its pane, including splitter and window-size changes.
Both styles use attached tabs with rounded top corners and an open selected bottom edge.
The row inherits its parent background. Empty rows draw no baseline.
Clicking a tab selects it and moves focus into its file pane, including clicks on the current tab.
Arrow keys select tabs while focus stays on the strip. Enter or Space moves focus into the selected pane.
The tab focus rectangle appears only during keyboard navigation.
See [tab colors and activation](menus-and-input.md#tabs-split-panes-and-activation) for the shared control APIs.
The title bar does not repeat the window title.
The navigation pane contains Recents, Bookmarks, Storage drives, Places, and the path tree for the active folder.
It has no title header.
Its filter searches item names and paths.
The folder tree shows the ancestors and immediate child folders of the active location.
Selecting a folder updates the tree.
Unchanged navigation rows retain their loaded icons and pending image requests across folder changes.
The collapsed navigation pane is completely hidden.
The navigation button stays at the left edge of the title bar.
When navigation is hidden, the first tab starts after that button.

Each pane has its own tabs, navigation history, details view, and Find bar.
Each tab also has an optional Columns view.
Details remains the default.
Find uses a single-line field with placeholder text and an X button, without labels or internal scrollbars.
While Find has focus, Up, Down, PageUp, and PageDown move the file selection without moving input focus.
Shift extends the selection. Ctrl+Home and Ctrl+End select the first and last matching files.
Left, Right, Home, and End retain their text-editing behavior.
Mouse Back and Forward use the history of the pane under the pointer, including its native Find field and title tabs.
Outside either pane, these buttons use the active pane. An open palette blocks mouse history navigation.
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

The size column sorts by byte count, not by the formatted text.
Folder scans and palette suggestions run outside the UI thread.
Canceled or obsolete requests cannot replace the current view.

### Columns view

The toolbar button switches between **Columns** and **Details**.
Its label names the view that the button will open.
The command palette also contains **Use Columns view** and **Use Details view**.
Each tab retains its own view choice.

Columns view starts at the committed folder.
A single selection of a folder loads its children in the next column.
Ancestor columns remain visible. A sibling selection replaces the columns to its right.
A file selection does not open the file.
Enter or a double-click opens the selected file through its Windows association.

Each column scrolls vertically on its own.
Left and Right move focus between existing columns.
The horizontal navigation buttons reveal earlier or later columns.
Horizontal wheel input and Shift+wheel scroll the path without changing the selected folder.
When the path exceeds the pane width, a bottom scrollbar supports thumb dragging and track paging.
The control supports at most 32 columns in one path.
The application reports an error at the limit instead of discarding ancestors.

A successful directory scan commits the address, history, and current folder.
A failed scan preserves the committed folder and displays an error.
Find filters the rightmost folder. Its navigation keys retain native text-input focus.
Context menus use the selected row in the column under the pointer.
Tabs retain their column paths. Explicit navigation, history movement, and Refresh start a new path at the requested folder.
Mode and tab changes detach obsolete native sources and cancel pending work.

### Keyboard and palettes

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
| Ctrl+C / Ctrl+Insert with file-view focus | Copy the selected files and folders |
| Ctrl+X with file-view focus | Cut the selected files and folders for a later move |
| Ctrl+V / Shift+Insert with file-view focus | Paste files into the current folder |
| Ctrl+Shift+C with file-view focus | Copy quoted full paths, one per line |

### File transfers

Copy, Cut, Paste, and Copy paths are also available in the context menu and command palette.
In Details, these commands use all selected visible rows, not only the focused row.
Ctrl+A selects the visible Details rows. Ctrl-click changes individual selections. Shift-click selects a range.
In Columns, clipboard commands use the selected item in the active column.
File shortcuts apply only while Details or a column has focus.
Find, navigation filters, and palette fields retain their native text clipboard behavior.

File copy uses the Windows file clipboard format, not a list of text paths.
Other Windows applications can paste these files.
The demo also accepts file clipboard content from Windows Explorer and other applications that supply local file paths.
Copy paths replaces the clipboard with text instead of file content.
Keyboard Paste targets the current Details folder or the active column folder.
The context menu for a single folder also offers Paste into this folder.

File drag-and-drop uses Details view. Columns retains its folder-selection and horizontal-scroll gestures.
A drag starts only after pointer movement crosses the Windows drag threshold.
The drag uses the selected files and folders.
A drop on a folder row targets that folder. A drop on empty file-grid space targets the pane folder.
File rows and column headers do not accept drops.
Drag-and-drop also works with other applications that accept or supply Windows file paths.
Ctrl requests a copy. Shift requests a move. The pointer indicates the accepted operation.
Transfers between demo panes can move files.
For conventional external drag targets, exported drags retain the originals and report a copy.
Escape cancels a drag before the drop.

Windows Shell performs file and folder transfers, including transfers between drives.
Its dialogs handle conflicts, progress, and cancellation.
The demo refreshes visible panes after a transfer attempt.
Cut does not delete files before Paste.
A canceled or failed transfer can leave some items transferred. The notification does not claim that the whole transfer succeeded.
The demo does not delete source files based only on a drag result.

File commands do not use obsolete rows during folder scans, tab changes, or filter updates.
The transfer destination and source paths are fixed when the operation starts.
The demo blocks another file transfer while the current transfer is active.
Virtual attachments without local paths, link creation, and right-button drag menus are not supported.
App commands accept large selections. Shell context-menu integration remains limited to 256 paths.
Larger selections show the app commands without Shell extension commands.

Both palettes appear at the center of the window, independent of the active pane.
They contain a query field and results, without duplicate headings, navigation buttons, or shortcut footers.
The palette frame and results share one background color.
Command shortcuts use separate keycaps on the right, beside each command title.
Command rows capture their labels, shortcuts, and enabled state when the palette opens or its query changes.
Row callbacks read that snapshot without querying native controls.
The controller checks current availability again before it executes a command.
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
`FileTransfers` connects clipboard commands and pane drops to the Windows transfer APIs.
These classes use explicit model updates rather than a separate MVVM package.

The demo does not provide dedicated rename, delete, or recursive-search commands.
It does not claim full File Pilot parity.
The navigation pane limits very large lists to the native control capacity and shows a notice for omitted entries.
The details view still exposes all entries from the folder scan.

## C++ explorer appearance

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

The [native sample commands](../../CONTRIBUTING.md#native-samples) accept an optional initial folder.

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
