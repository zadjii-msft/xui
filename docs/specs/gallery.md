# Control gallery

The gallery is an interactive catalog built from XUI controls.
Each page includes its purpose, a C++ API excerpt, a Copy code action, and event output.
Code blocks support native text selection and preserve indentation.

See [CONTRIBUTING](../../CONTRIBUTING.md#gallery) for build and run commands.
The optional [WinUI-style appearance](winui-style.md) has a compact experiment and a complete-catalog mode.

## Find an example

1. Press Ctrl+F to expand the navigation pane and focus search.
2. Type a control name or category.
3. Press Enter to focus the results.
4. Use the arrow keys to select an example.
5. Press Enter to focus its controls.

The catalog uses `NavigationView` with category groups.
Home and Appearance remain in the pinned header and footer.
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
Page IDs include `combo`, `popup`, `items`, `tree`, `adaptive`, `grid-extensions`, `commands`, `breadcrumb`, and `navigation`.
Other IDs include `navigation-view`, `shell`, `titlebar`, `dialog`, `status`, `multiline`, `password`, `rich-text`, `date-time`, `color`, and `images`.

The grid calculates synthetic rows without a retained row array.
The chart updates only on request. The file list uses synthetic fixtures.
Pages create their examples on first use.
The gallery has no application-specific window procedure or drawing code.

For the native menu example, right-click the menu target or press Shift+F10.
The `--image` argument fills the image path field.
Select Load image to start decoding.
The optional `--high-contrast` argument uses system high-contrast colors.

The gallery demonstrates available control families, not complete WinUI compatibility.
Optional WebView2 content requires an explicit build option and an installed runtime.
The [control roadmap](../llm/control-roadmap.md) records the reference research and remaining work.
