# Control gallery

The gallery is an interactive catalog built from XUI controls.
Each page explains when to use the control, what to try, and which API rules affect application code.
The live example reports events so you can connect each action to its result.
The code tabs offer `.xui`, C#, Rust, and C++ examples.
The gallery starts with `.xui` and remembers the selected language across pages.
Code blocks support native text selection, preserve indentation, and use a monospace font.
Copy code copies the selected language, not the live example's state.

## Use the gallery as a reference

1. Select a control from the catalog.
2. Read the usage description and the steps under **Try this example**.
3. Use the live controls and read the event output.
4. Read **Usage and limits** for state, events, ownership, and binding differences.
5. Select a language tab under **Code example**.
6. Select **Control reference** for the control documentation.

**Control reference** opens the corresponding page in the [XUI handbook](https://zadjii-msft.github.io/xui/).
**Other links** in the navigation contains the handbook and each language guide.
Select a link, then press Enter or double-click to open it.
Link selection alone keeps the current example open.
**Copy documentation link** copies the handbook URL.
**Gallery source** opens the implementation of the live examples.

See [CONTRIBUTING](../../CONTRIBUTING.md#gallery) for build and run commands.
The optional [WinUI-style appearance](winui-style.md) has a compact experiment and a complete-catalog mode.

## Find an example

1. Press Ctrl+F to expand the navigation pane and focus search.
2. Type a control name or category.
3. Press Enter to focus the results.
4. Use the arrow keys to select an example.
5. Press Enter to focus its controls.

The catalog uses `NavigationView` with category groups.
Home, Appearance, and Other links remain in the pinned header and footer.
Search preserves the selected item identity.
The page area shows a matching example or an empty state.

Search expands collapsed groups that contain matches.
Groups without matches retain their previous expansion state.
Manual expansion or collapse during a search persists across query edits.
An empty query restores the expansion states from before the search.
`NavigationView::item_matches` reports membership even for an item inside a collapsed group.

Previous and Next expand the pane and use visible example rows.

## Example pages

The `--page` argument selects a page directly.
Page IDs include `combo`, `popup`, `items`, `tree`, `miller-columns`, `adaptive`, `grid-extensions`, `commands`, `breadcrumb`, and `navigation`.
Other IDs include `navigation-view`, `shell`, `titlebar`, `dialog`, `status`, `multiline`, `password`, `rich-text`, `date-time`, `color`, and `images`.

The grid calculates synthetic rows without a retained row array.
The chart updates only on request. The file list uses synthetic fixtures.
The initial pages are created with the window. Later pages create their examples on first use.
The gallery has no application-specific window procedure or drawing code.

The `miller-columns` page uses the public `MillerColumns` control and immutable sources for a synthetic project library.
Each column contains 28 siblings, with folder branches across eight levels.
Folder selection replaces later columns. Document selection removes later columns.
Activation reports the item without an external action.
The page performs no filesystem or network work.

Select Show deep path to display eight columns.
Use the horizontal scrollbar or horizontal wheel to inspect the path.
Scroll vertically within a column to inspect its siblings.
Select Reset path to return to the project list.

For the native menu example, right-click the menu target or press Shift+F10.
The `--image` argument fills the image path field.
Select Load image to start decoding.
The optional `--high-contrast` argument uses system high-contrast colors.

The gallery demonstrates available control families, not complete WinUI compatibility.
Optional WebView2 content requires an explicit build option and an installed runtime.
The [control roadmap](../llm/control-roadmap.md) records the reference research and remaining work.
