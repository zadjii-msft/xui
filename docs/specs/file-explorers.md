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

### Search and customize context actions

File and folder menus include **Search / customize context actions**.
Built-in app actions show their current configured shortcut hints, including aliases and removed bindings.
The popup searches app actions and supported Windows Shell leaves by label or canonical verb.
Its native text editor retains standard text selection and keyboard input.
**Run** uses the paths captured when the context menu opened.
Selection, tab, or folder changes cancel stale Shell actions.

**Pin** saves an explicit app identity or a unique canonical Shell verb.
The popup explains when a Shell entry does not support pinning.
No temporary Shell command ID enters the saved state.
Pinned app actions appear first in the app portion of the context menu.
**Pinned context actions** shows favorites against a fresh Shell snapshot.
Unavailable favorites remain visible and can be unpinned.

**Hide app action** removes a built-in app action from the context menu.
The search popup retains hidden actions and provides **Show app action** to restore them.
These preferences persist in the Explorer customization state.
**Show Windows menu** retains native submenus, dynamic commands, and owner-drawn Shell extensions.
**Customize Explorer** always remains in the context menu, including menus for empty space.
Hidden-action preferences cannot remove this recovery command.
It opens customization even if the toolbar, sidebar, and command-palette keybinding are disabled.

### Edit the markup

`ExplorerLayout.xui` composes the sidebar, file panes, and notification.
`FilePaneLayout.xui` defines each pane, including its toolbar, file-view placement, compact Tree style, Find row, and status.
It creates the Details and Items controls and accepts the controller-owned Tree and Columns controls through `Content`.

`BreadcrumbAddressLayout.xui` defines the address container and its parent-folder button.
`BreadcrumbMenuLayout.xui` defines the folder popup, list, and message.
`BreadcrumbSegmentLayout.xui` defines each name-and-chevron pair and its name-button style.
`BreadcrumbSpaceLayout.xui` defines the keyboard-accessible trailing button without a visible glyph.

`SidebarLayout.xui` defines the navigation control.
`PaletteLayout.xui` defines the folder and command palette, including its reactive status text, visibility, and height.
`ViewMenuLayout.xui` defines the footer's view-choice flyout.
`FilePaneView` creates a compact command menu for folder and file order beside it.

`PreviewLayout.xui` defines the content of an independent preview window.
Its overlay grid accepts the image, native text, and status controls from `PreviewSession`.
It also defines the native text and image styles.
`PreviewMetadataLayout.xui` defines the large icon and file details for folders and unsupported formats.
Its constructor receives the file entry, and its state controls metadata visibility.

Generated control references connect these layouts to their C# controllers.
The `FindOpen` state controls the [bottom reveal](animations.md) for the Find row.

The controllers retain event handlers, filesystem work, cancellation, and immutable collection sources.
They also configure native features without a declarative argument.
Shared styles for title-bar icons and nested navigation controls remain in `ExplorerStyles`.
Dynamic breadcrumb composition retains its `ContentHost` ownership and 64-segment limit.
An unchanged path does not rebuild its controls.
The generated components share immutable styles and create the existing native controls without a runtime parser or per-frame reconciliation.

After the native build, use the [restart-on-save command](../../CONTRIBUTING.md#c-file-explorer) for markup changes.

### Explorer controls

The title bar contains a navigation button, independent tab strips for each pane, and Windows caption controls.
Each tab row follows its pane, including splitter and window-size changes.
Each pane has a New tab icon immediately after its last visible tab, not in the address toolbar.
The navigation, New tab, and address-toolbar icons use the shared `ExplorerStyles.IconButton` style.
Their idle backgrounds match the window in light and dark themes, without borders.
Hover, pressed, disabled, keyboard focus, and high-contrast feedback remain available.
The address bar uses borderless breadcrumb buttons with separate folder dropdowns.
Both styles use attached tabs with rounded top corners and an open selected bottom edge.
The row inherits its parent background.
The title-bar border continues across the navigation area, pane divider, and caption area, except below selected tabs.
Clicking a tab selects it and moves focus into its file pane, including clicks on the current tab.
Arrow keys select tabs while focus stays on the strip. Enter or Space moves focus into the selected pane.
The tab focus rectangle appears only during keyboard navigation.
See [tab colors and activation](menus-and-input.md#tabs-split-panes-and-activation) for the shared control APIs.
The title bar does not repeat the window title.
The native window caption uses `{folder name} ({full path}) - FileExplorer.xui` for the active pane.
Drive roots use their root path as the folder name.
The navigation pane contains Recents, Bookmarks, Storage drives, Places, and the path tree for the active folder.
Recents retains the 10 most recently visited folders, newest first.
Revisiting a folder moves it to the top without creating a duplicate.
Older saved lists are trimmed on load. Bookmarks are unchanged.
It has no title header.
Its filter searches item names and paths.
The `ExplorerStyles.NavigationFilter` style matches the sidebar background in both themes, with only a thin bottom border.
The filter retains native text input and keyboard focus feedback.
The shared `ExplorerStyles.NavigationItems` style gives navigation lists compact rows, smaller text, and smaller icons.
It uses 28-DIP rows, 12-DIP text, 16-DIP icons, and no extra row padding.
The folder tree shows the ancestors and immediate child folders of the active location.
Selecting a folder updates the tree.
Unchanged navigation rows retain their loaded icons and pending image requests across folder changes.
Navigation folders use the same context menu as Details rows, including Windows Shell commands and the Windows menu fallback.
Right-click targets the navigation row without opening its folder or changing the Details selection.
The menu includes open, new-tab, other-pane, bookmark, copy, cut, paste, path-copy, and refresh commands.
Keyboard context-menu requests use the focused navigation row.
Section headers, disabled rows, and empty space have no file menu.
Source or filter changes cancel an open menu instead of changing its target.

Folder hover cards appear after at least 1,000 ms over the same row.
Each card sits to the right of that row, within the window bounds.
It shows the folder name, full path, creation time, recursive subfolder and file counts, and total file size in bytes.
The background scan starts only after the hover delay.
Pointer exit, row changes, filtering, scrolling, and window closure cancel obsolete scans.
The card does not move input focus and offers only the supported click-to-open hint.

Metadata scans exclude links and junctions.
They stop after 100,000 entries or five seconds between filesystem calls.
Unreadable folders show unavailable totals instead of zero.
Skipped entries, access errors, and scan limits produce partial totals with an explanation.
The shared `NavigationView` exposes `SetHoverDelay` and `SetHoverHelp` in C#.
Its `Preview` event reports row changes, and its `Request` event starts delayed metadata work.

The collapsed navigation pane is completely hidden.
Its expanding Reveal slides from the left while the file area and title tabs follow the changing width.
The search editor retains its native identity and full width.
The navigation button stays at the left edge of the title bar.
When navigation is hidden, the first tab starts after that button.

Each pane has its own tabs, navigation history, details view, and Find bar.
The secondary pane enters from the right and retains its full target width inside the split viewport.
The primary pane, divider, and title-tab positions change together.
Closing returns focus and disables secondary interaction before the exit finishes.
Each tab also supports icon galleries, List, Tree, and Columns.
Details remains the default.
Find uses a single-line field with placeholder text and an X button, without labels or internal scrollbars.
The bar uses expanding layout on entry and exit.
Its row height and the file-view height change together on each animation frame.
The shared edge moves without a preceding resize or a second slide.
Disabled system animations make these transitions immediate.
Typing with file-view focus opens Find and sends the first key to its native editor.
Keyboard layouts, dead keys, and IME input use native text translation.
Shortcuts, the navigation filter, and palette editors do not start a file filter.
Outside Columns, successful navigation to a different folder clears the filter but retains the Find bar state.
Refresh, failed navigation, and tab switches preserve the filter.
While Find has focus, Up, Down, PageUp, and PageDown move the file selection without moving input focus.
Shift extends the selection. Ctrl+Home and Ctrl+End select the first and last matching files.
Left, Right, Home, and End retain their text-editing behavior.
Mouse Back and Forward use the history of the pane under the pointer, including its native Find field and title tabs.
Outside either pane, these buttons use the active pane. An open palette blocks mouse history navigation.
Each tab retains its folder, filter, sort order, partition, selection, and scroll position.
Right-clicking a tab opens its context menu without selecting it.
The menu contains tab shifting, duplication, path copying, and closing commands.
Closing commands affect the target tab, other tabs, tabs to either side, or all tabs in its pane.
Unavailable directions and duplication at the tab limit appear disabled.
Tab changes invalidate an open menu instead of changing its target.

### Customization

The **Customize Explorer** command opens a searchable settings editor.
The command palette includes this command even when the toolbar or sidebar is hidden.
The editor has **General**, **Toolbar**, **Navigation**, and **Keyboard** pages.
Toolbar and navigation pages group visibility controls, sections, and commands.
Command rows show their names and icons. Shared instructions appear once above each group.
Search finds settings across all pages. Selecting a page clears the search without discarding text drafts.
Opening the popup creates native controls for the current page, not every hidden settings page.
Search and page changes create additional native controls when those rows become visible.
Each settings row has its own control beside its name and description.
Switches control visibility and behavior. Dropdowns select the theme and thumbnail fit.
A slider controls row spacing. A numeric stepper controls font size.
Sidebar sections and command rows have a visibility switch and a position stepper.
These controls save changes immediately.

Font, date, and keyboard rows have native text fields.
Press **Enter** or the row's **Save** button to apply a text value.
Search and unrelated settings changes preserve text drafts.
An invalid value stays in its field with an error message. Saved settings do not change.
Each row has a borderless reset icon with an accessible name and a tooltip.
The icon appears only when the saved value or a text draft differs from the default.
Hidden reset icons do not change the field width. Reset restores the default and clears the row's draft and error.
**Reset all** restores all customization defaults.

Keyboard rows support multiple aliases and sequences of up to three strokes.
For example, `Ctrl+K, Ctrl+R; Ctrl+Shift+R` assigns a sequence and a separate alias.
An empty value removes all mappings for that command.
The value `default` restores its default mappings.
The editor rejects duplicate shortcuts, conflicting sequence prefixes, reserved Windows shortcuts, and AltGr combinations.
The command palette shows the current mappings.

A sequence expires after 1.8 seconds between strokes.
Escape cancels an incomplete sequence.
An invalid continuation returns the key to normal input.
Custom mappings and sequences run only while a file view has focus.
Default global navigation and tab shortcuts retain their normal behavior.
File actions never replace native editor shortcuts.

Toolbar and sidebar command rows accept a position.
Position `0` hides the command. Other positions select its place in the command order.
Toolbar labels are optional.
Commands use the same icons in the toolbar, navigation pane, command palette, and settings rows.
For example, the folder bookmark command uses the bookmark icon rather than the overflow icon.
Each sidebar section has its own visibility switch and position stepper.
A hidden section returns at the end of the list when enabled.
The settings also control toolbar commands, sidebar visibility, item status, and the three Home widgets.

File rows use the configured row height and font size.
Tree rows stay compact: their height is 8 DIPs less, with a minimum of 20 DIPs.
Navigation rows use 4 DIPs less, with the same minimum.
Tree and navigation fonts use 2 DIPs less, with a minimum of 9 DIPs.
The defaults remain 24/12 DIPs for Tree rows and 28/12 DIPs for navigation rows.
Gallery heights add the configured row height to the image area.

Import and export use a versioned JSON document.
An absent field uses its default.
Import rejects invalid values, duplicate identities, unsupported versions, unknown fields, and shortcut conflicts.
A failed import leaves the current settings and other saved state unchanged.
Settings use the optional `Customization` field in `state.json`.
Older state files remain valid.

Commands retain stable identities independently of their display names when they supply an explicit `Id`.
The existing `ExplorerCommand(Name, Shortcut, Execute, CanExecute)` constructor remains valid.
Extensions can supply the optional `Id` and `Aliases` arguments.
Every command runs through an availability check immediately before execution.

### Breadcrumb address bar

The C# demo uses `BreadcrumbAddressBar`, a demo-local control inspired by the File Pilot address bar.
Each ancestor name opens that folder.
The right chevron after each name opens a dropdown of its immediate subfolders.
The dropdown appears below the address bar and excludes files.
One click activates a folder in either the subfolder dropdown or the ancestor menu.
Arrow keys change selection without navigation. Enter activates the selected folder.
Empty folders and directory-read errors have distinct messages.

Earlier segments collapse as the pane narrows.
The current folder remains visible.
The **Parent folders** button lists the complete path, including hidden ancestors.
The bar retains at most 64 segment pairs, but the ancestor menu retains every path component.
Left and Right move between visible name and chevron buttons.
Down opens the focused segment's folder dropdown.

Segment names have two DIPs of horizontal padding on each side.
Measured text widths determine each segment's width. Character counts do not determine layout.
When the complete path does not fit, earlier segments disappear into the ancestor menu instead of shrinking short names.
The current folder name uses its measured text width rather than the remaining bar width.
Clicking the current name or the empty space after it opens the navigation palette.
Ctrl+L and Alt+D open the same palette.
There is no inline address editor.
The palette retains its native input, path suggestions, and keyboard commands.

Each pane owns its breadcrumb controls and folder dropdown.
Navigation, tab changes, pane closure, and dropdown dismissal cancel obsolete folder requests.
Canceled requests cannot replace newer dropdown contents.
The searchable navigation palette remains available through Ctrl+G and the **Go to folder** command.

### Tab tear-out and merge

Dragging a tab within its strip changes its position.
Dragging outside the strip separates the tab into a window.
The dragged tab keeps the original native window so the Windows move-size loop can continue.
Another window receives the remaining workspace.
Before showing that remainder window, the application disables initial activation with `SetShowActivated(false)`.
The framework places it immediately below the moving window, without taking activation from the drag.
A single-tab workspace does not create an empty remainder window.

Dragging onto another visible strip shows an insertion marker.
With full-window dragging, an accepted hover join temporarily hosts the tab in that destination.
Further movement within that strip can change its insertion position.
Leaving the destination returns the tab to its original strip before another destination receives it.
Releasing the pointer commits the hosted transfer, without transferring the tab again.
If hover joining is unavailable or rejected, release transfers the tab to the accepted insertion position.
Outline-only window dragging uses this release-only fallback.
Both panes can receive tabs, subject to the pane tab limit.
The windows must belong to the same running application.
Ctrl+N creates another window in that application.
Windows from separate FileExplorer processes do not merge.

Transfers preserve the tab identity, folder history, Find state, filter, sorting, selection, scroll position, and Columns state.
Obsolete asynchronous work cannot update the receiving pane.
Each window retains its own native controls and subscriptions.
The initiating window keeps the original drag identity even while another window temporarily hosts the tab.
All participating windows and control trees remain alive until the move loop completes.
An external join first separates the dragged tab from the remainder workspace while preserving the original HWND.
Escape restores the saved workspace instead of closing the dragged tab.
For a joined tab, restoration first returns the tab to its initiating strip.
The [framework protocol](menus-and-input.md#tab-dragging-between-windows) defines window ownership and callback behavior.

### Tab menu actions

**Duplicate tab** inserts a copy after its source and selects it.
Copies retain independent history, filter, Find state, sorting, selection, scrolling, and Columns state.
**Duplicate in new pane** opens the other pane with a copy.
An existing pane receives an additional tab without replacement of its current tabs.
**Duplicate tab to new window** starts another explorer at the target folder.
**Copy path** copies the quoted folder path.

Closing the last tab or all tabs closes that pane.
Closing the left pane preserves the other pane's tabs in the remaining workspace.
Closing the last pane closes the window.

Details column headers support sorting and width adjustment.
File and folder rows highlight under the pointer without changing the selection.
File rows, navigation folders, and navigation-palette results show asynchronous Windows thumbnails or Shell icons.
Visible Miller rows load images incrementally without a fixed allowance for completed icons.
When the decode queue fills, remaining rows wait for capacity instead of remaining on vector icons permanently.
Pending or failed image requests retain their vector icons.
Navigation sections also have icons.
Right-click selects the target row and opens its context menu.
The XUI menu combines supported Windows Shell commands with folder navigation, bookmarks, and Refresh.
It uses the same styled native context menu as the gallery's Menus and confirmation page, not a `CommandSurface` popup.
Shell commands still run through their original Windows handlers.
The Show Windows menu... item opens the full native menu for extension-specific content that requires native handling.
Shell commands in the XUI menu show their supported native icons.
Commands without a supported bitmap have no icon. The Windows-menu fallback also has no icon.
Supplied icons retain their colors. Disabled commands and high-contrast menus use theme-colored glyphs instead of bitmap colors.
Checkmarks remain visible beside the icons.
Native submenus and owner-drawn entries use the Windows menu fallback.
Open in this pane keeps folder navigation in the demo. Shell Open uses Windows behavior.
Files omit folder-only commands and duplicate Open actions.
Selection or source changes cancel pending menu actions instead of changing their target.

`--prefetch-shell-menus` enables an experimental background warmup after each successful directory navigation.
The title includes `[menu prefetch]`. Without the flag, navigation does not request a menu.
The warmup discovers one menu for the destination directory, releases its handlers, and retains no commands.
Navigation changes and closure cancel obsolete warmups.
Interactive requests take priority, but cannot interrupt a Shell extension inside COM.
Failures and busy-worker skips produce Windows debugger diagnostics.
The [contributor procedure](../../CONTRIBUTING.md#try-prefetch-inside-fileexplorer) describes the comparison build.

The size column sorts by byte count, not by the formatted text.
Folder scans and palette suggestions run outside the UI thread.
Canceled or obsolete requests cannot replace the current view.

### View choices

The footer contains the item count and a **Choose view** icon.
The icon opens a flyout above the footer.
The choices appear in this order: **XL Icons**, **L Icons**, **M Icons**, **List**, **Tree**, **Details**, and **Columns**.
Columns is a separate view below Details, not a variation of Details.
The flyout identifies the current view. Escape closes it without a view change.
The command palette contains a **Use ... view** command for each choice.
Each tab retains its own view choice.
New tabs start in Details. Tab duplication and window transfers retain the source view choice.

The partition icon beside **Choose view** opens an upward flyout.
It offers **Folders, then files**, **Files, then folders**, and **Mixed**.
Each menu item shows its partition icon beside the label. A checkmark identifies the current partition.
Arrow keys move between the items. Enter applies the choice, and Escape dismisses the menu without a change.
The button shows the active partition: folders above files, files above folders, or an alternating arrangement.
Its accessible name and tooltip identify the active setting.
The original SVGs are [Folders first](../../assets/icons/folders-first.svg),
[Files first](../../assets/icons/files-first.svg), and [Mixed](../../assets/icons/mixed.svg).
Native vector paths render these designs in the button's foreground color, including high contrast.
Folders first remains the default.
Details, List, icon galleries, Tree, and every column in Columns view use the tab's partition.
Tree applies the same order to each folder's children when it loads them.
The selected sort applies within each partition, or across all entries for Mixed.
Descending sort does not reverse the folder and file partitions.

Each tab retains its own partition across navigation, refresh, and view changes.
A new tab or pane inherits the active tab's partition.
A duplicate inherits its source tab's partition, even when that source is not active.
Changes to a new tab do not affect its source.
Tab transfers preserve the partition.
New windows also inherit the active or duplicated source tab's partition.

View changes do not animate. The selected view appears in its final position.
The old view loses input immediately through the native update.
The toolbar and footer stay stationary. Selection and the saved scroll offset remain intact.
Obsolete filter results cannot replace a later view choice.
The Find bar, navigation pane, and split pane retain their separate transitions.

The three icon choices use the same virtual gallery with different tile sizes.
M, L, and XL use minimum widths of 96, 160, and 256 DIPs, respectively.
Their tile heights are 128, 192, and 288 DIPs.
Each tile shows an image or icon above its filename.
The gallery adjusts the number of columns to the pane width.
List shows compact rows without metadata columns. Details retains its sortable metadata columns.
Both views and the galleries use the tab's filter and sort order.

Tree starts with the current folder's immediate children.
It uses compact 24-DIP rows, 12-DIP text, 16-DIP icons, and 16-DIP indentation per level.
Alternating row backgrounds and full-row selection distinguish adjacent entries.
Names show nesting. Date modified, Type, and Size remain in aligned columns, using the same metadata as Details.
Tree has no column header or separate sort controls. It retains the tab's sort order.
Right Arrow or a disclosure arrow expands a folder without changing the address or history.
Left Arrow collapses the folder or selects its parent.
Enter or a double-click opens the selected item through the usual file action.
Child scans run outside the UI thread. Canceled child results cannot update a collapsed branch or another view.
Read errors appear on the branch and in the application notification. Right Arrow retries the branch.

Find filters names within each requested sibling list. It does not search unopened descendants.
The Tree count reports matching immediate children of the current folder.
Refresh, filter changes, and view changes reset expanded branches.
Tree restores the ancestors of a retained selected descendant when that descendant still matches the filter.
Selection, previews, file clipboard commands, and context menus use the active view, including loaded Tree descendants.
Native file drag and drop remains available in Details.

### Columns view

Columns view starts at the committed folder.
Its rows use the same 32-DIP height as Details, with unchanged icons and text.
A single selection of a folder loads its children in the next column.
Ancestor columns remain visible. A sibling selection replaces the columns to its right.
A file selection does not open the file.
Enter or a double-click opens the selected file through its Windows association.

Each column scrolls vertically on its own.
Rows highlight under the pointer without changing selection or keyboard focus.
Vertical separators distinguish adjacent columns.
Left and Right move focus between existing columns.
Clicking a directory header or empty space below its rows focuses that column without changing selection or the open path.
New columns scroll fully into view without moving keyboard focus.
Horizontal wheel input and Shift+wheel scroll the path without changing the selected folder.
When the path exceeds the pane width, a bottom scrollbar supports thumb dragging and track paging.
The control supports at most 32 columns in one path.
The application reports an error at the limit instead of discarding ancestors.

A successful directory scan commits the address, history, and current folder.
A failed scan preserves the committed folder and displays an error.
Find filters the focused column. Each column remembers its own query.
The pane shares one native Find field. Its placeholder identifies the target folder.
Typing in a column opens Find for that column without losing the first character.
Focusing another column restores its query without moving focus into Find.
Closing Find clears only the target column's query.

Filtering an ancestor preserves its descendants, even when the selected folder no longer matches.
Clearing the filter restores that folder's selection without navigation.
Selecting a different folder still replaces the descendants. Selecting a file removes later columns.
Find navigation keys move selection in the target column and retain native text-input focus.
Opening a child preserves the ancestor's query. New child columns start without a filter.
The footer reports matches in the target column.
Context menus use the selected row in the column under the pointer.
Tabs retain their column paths, queries, and active column. Tab duplication copies this state independently.
Explicit navigation, history movement, and Refresh start a new path at the requested folder.
Refresh preserves that folder's query. Navigation to a different root clears it.
Mode and tab changes detach obsolete native sources and cancel pending work.

### File preview

Space opens an independent preview window for one selected item in Details or Columns view.
The context menu contains **Preview**. The command palette contains **Preview selected item**.
Preview does not open the file through its association.
The titlebar **Open** button performs that separate action. **Open folder** uses the Windows Shell, without an originating pane.

The titlebar contains the filename, Open glyph, and normal Windows caption controls.
The content starts directly below it, without a second filename or Close row.
Escape or the caption Close button closes only that preview.
The preview does not assign initial focus to a caption button.
Its loaded content uses an opt-in 180 ms entry, authored in `PreviewLayout.xui`.
The native editor retains focus and selection during entry.
The titlebar, status row, and native window stay stationary.
Reduced motion settles entry immediately. Closure does not wait for the animation.
Entry belongs to the preview window and continues independently if Explorer closes.
Failed file reads report their error without opening the content reveal.
Tab moves between preview controls. Enter activates a focused button.
A held Space cannot activate the preview's Open or Close button.
Native text selection, scrolling, and copying remain available.
The text preview uses Cascadia Mono and has no editor border or read-only banner.
The preview keeps this document font when the UI font changes, but uses the configured font size.
An [LSH-enabled build](../../CONTRIBUTING.md#lsh-highlighting-in-xui-applications) highlights supported source files, including `.xui`, C#, C++, JSON, and Python.
Unknown extensions remain plain text.
Highlighting does not change the preview's file-read restrictions, content limit, or cancellation scope.
It retains the native document control because ordinary labels do not support text selection.
The Open button uses the Open glyph and retains its accessible name.
Each HWND has small and large file-type icons for Windows taskbar and window-switching surfaces.
Eligible files use their extension's association icon without access to the target file.
Restricted and unknown-origin targets retain stock document or folder icons, without association or provider calls.
Window closure releases the icon handles. DPI changes replace them with the corresponding sizes.
Explorer shortcuts and mouse history navigation continue to work in the Explorer window.
Preview windows do not route shortcuts to Explorer.
Space in Find or another text input retains its text-input behavior.

The preview supports these content types:

| Content | Behavior |
| --- | --- |
| Text and code | Selectable, borderless text for common text, source, configuration, and extensionless files |
| Images | PNG, JPEG, BMP, GIF, TIFF, and WebP through installed WIC codecs |
| Folders | Large generic icon, name, file type, modification date, and an uncalculated-size field |
| Other formats | Large generic icon, name, file type, size, and modification date |

The metadata view does not scan folders recursively or report their size as zero.
File sizes include a readable unit and the exact byte count.
The image and metadata views omit routine informational banners.
Truncation, empty text files, and errors retain explicit messages.

Text supports UTF-8 and BOM-marked UTF-16 in either byte order.
Invalid encoding, binary control characters, and UTF-32 produce an explicit error.
The loader reads at most 262,145 bytes, including one byte that detects truncation.
It decodes at most 262,144 bytes and displays at most 65,536 UTF-16 code units.
Truncation preserves complete surrogate pairs and includes a visible message.
Line endings use the native document format. Empty files have an explicit empty-file message.

Images use a 1,024-by-1,024-pixel decode box and preserve their aspect ratio.
The [image contract](images.md) defines file-size limits, shared memory budgets, codec support, and orientation restrictions.
The image control displays its own loading and decode errors.
The decode limit belongs to the shared image service. The preview does not display it as a warning.
Basic preview does not execute documents, media, or web content.
Restricted or unknown-origin paths show generic metadata before text or WIC decoding.
Preview metadata uses retained vector icons, not `ShellSource`, because the pathname thumbnail API cannot retain a checked file identity.
The preview does not load installed Windows preview handlers or require a helper executable.
PDF and Office files use the metadata view.

Each preview captures its own immutable target and creates its own controls and cancellation scope.
Selection, navigation, tab changes, and opener closure do not change or close an existing preview.
The target snapshot identifies a path. It does not freeze the bytes of a file that another application changes.
Ownerless preview windows move independently and use normal Windows taskbar, Alt-Tab, and z-order behavior.
All basic previews share the application's STA dispatcher and process.
The application exits after the final window closes and its deferred cleanup completes.
Preview closure cancels pending delivery and retires its native text, images, and other resources.
One preview's teardown does not invalidate images in another window.

### Keyboard and palettes

Command and location palettes appear immediately, without scrolling their results into place.
The query editor, results, status area, and popup frame stay stationary.
Typing and command execution work immediately.
Cold queries clear old suggestions immediately. Canceled requests cannot replace newer results.
Escape and execution dismiss immediately, without an animated exit.
There is no animated palette entry.

| Input | Action |
| --- | --- |
| Navigation button | Expand or collapse the navigation pane |
| Alt+F | Focus the navigation filter |
| Ctrl+L / Alt+D / Ctrl+G / current breadcrumb / trailing space | Open the navigation palette at the active folder |
| Up / Down in the palette | Select the previous or next result |
| Tab in the navigation palette | Insert the selected full path without navigation |
| Ctrl+Backspace in the navigation palette | Delete the selection or previous word or path component |
| Enter in the navigation palette | Open the selected result in the active pane |
| Ctrl+Enter in the navigation palette | Open the selected result in the other pane |
| Alt+Left / Alt+Right in the palette | Previous or next completed query |
| Alt+Up in the palette | Show the parent folder |
| Escape in the palette | Close the palette without navigation |
| Ctrl+Shift+P | Open the searchable command palette |
| Space with one selected item and file-view focus | Open a file preview |
| Escape in the preview | Close only that preview window |
| Ctrl+T / Ctrl+W | Add a tab / close the active tab |
| Ctrl+F4 | Close the active tab |
| Ctrl+Shift+PageUp / Ctrl+Shift+PageDown | Shift the active tab left / right |
| Ctrl+N | Open the active tab's folder in another explorer window |
| Ctrl+Shift+W | Close all tabs in the active pane |
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
| Delete with file-view focus | Delete the selected files and folders through Windows Shell |
| Ctrl+V / Shift+Insert with file-view focus | Paste files into the current folder |
| Ctrl+Shift+C with file-view focus | Copy quoted full paths, one per line |

### Deletion and directory changes

Delete uses the Windows Shell Delete action for the current selection.
Windows controls confirmation, cancellation, and Recycle Bin behavior.
The command does not force permanent deletion or delete files through a separate filesystem API.
Delete retains its native text-editing behavior in Find, address, and other text fields.
The command palette also contains **Delete selected items**.
Shell deletion supports up to 256 selected paths. Larger selections produce an explicit message.
A changed selection, tab, or view cancels a pending command before Shell execution.

Visible panes watch their active directories for file creation, deletion, renames, and metadata changes.
Changes through either Shell context menu and changes from other applications update the rows automatically.
The watcher combines nearby events before a background scan.
An active scan, filter, or app file operation delays the next scan until that work finishes.
Watch errors appear in the notification area. Manual Refresh remains available.
Hidden panes and inactive tabs do not retain watches. Navigation and window closure retire the previous watches.

Automatic refresh retains the folder filter and sort order.
Details, List, and icon views retain selections for surviving items.
Columns watches each directory in its displayed path.
Automatic refresh retains surviving columns, their queries, and their scroll offsets.
Deletion of an open child folder removes its column and later columns.
Tree watches descendants and uses the existing refresh behavior for expanded branches.

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
Successful clipboard commands show temporary feedback to the left of the item count in the originating pane.
The feedback clears after three seconds. A new message restarts that pane's timeout.
Successful transfer feedback uses the same footer area.
Errors and incomplete-transfer warnings remain visible in the application notification area.
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
Font customization preserves this layout. Commands without a shortcut have no empty keycap.
Aliases and key sequences retain their separators between keycap groups.
Long shortcut groups can use an ellipsis in narrow palettes. Accessible text retains the complete shortcut.
Command rows capture their labels, shortcuts, and enabled state when the palette opens or its query changes.
Row callbacks read that snapshot without querying native controls.
The controller checks current availability again before it executes a command.
Status messages appear only for pending requests, empty results, and errors.
Keyboard history, completion, acceptance, and dismissal remain available.
The navigation palette initially adds a trailing slash to the current folder and shows its children.
Typed paths support relative paths, quoted paths, environment variables, and UNC paths.
Without a trailing slash, the final component filters the parent folder by name, even for an exact directory match.
Enter on an exact directory match opens that directory, not its first child.
A trailing `\` or `/` lists the directory's children.
Bare drive letters such as `D:` resolve to the drive root, not the drive's last working directory.
Drive-root queries do not select a child automatically, including queries such as `D:\`.
Enter opens the root. Down selects a child for subsequent navigation.
The same rule applies after quote removal and environment-variable expansion.
Prefix matches appear before other substring matches.
Tab completion adds a trailing slash to a directory and leaves the caret at the end of the completed path.
Within a loaded folder, the palette filters its snapshot immediately without a delay or an empty intermediate view.
For a different folder, the palette clears old rows and disables results until the new scan finishes.
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

The demo does not provide dedicated rename or recursive-search commands.
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
