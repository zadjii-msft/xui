# Control gallery

The gallery is an interactive catalog built from XUI controls.
Each page explains when to use the control, what to try, and which API rules affect application code.
The live example reports events so you can connect each action to its result.
The code tabs offer `.xui`, C#, Rust, and C++ examples.
The gallery starts with `.xui` and remembers the selected language across pages.
Code blocks support native text selection, preserve indentation, and use a monospace font.
Copy code copies the selected language, not the live example's state.
An [LSH-enabled build](../../CONTRIBUTING.md#lsh-highlighting-in-xui-applications) highlights the selected language.

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
**Layout > Motion** contains a replayable motion lab.
Search for **motion** or **animations** to find it.
Its stable page ID remains `animations`, so `--page animations` opens it directly.
The `navigation-view` page provides normal, slow, and immediate group durations.
The Projects, Reports, and Reference arrows act on real nested navigation rows.
Filtering settles group motion. Header and footer shortcuts remain pinned.

Select Open all, Close all, or Reverse to control the four reveal directions.
Resize neighbors switches between expanding layout and fixed slots.
The duration choices are normal (180 ms), slow (1200 ms), and immediate (0 ms).
Toggle pane demonstrates coordinated pane widths and a nested editor reveal.
After the pane opens, More left, Equal panes, and More right change the ratio.
Rapid preset changes reverse or retarget the motion.
Dragging the divider takes direct control at its displayed position.
Wider windows provide more movement between the minimum pane widths.
The native editors retain their text across transitions.
Windows reduced motion takes precedence over the selected duration.
The `tabs` page provides normal, slow, and immediate durations.
Add document and Close selected document expose insertion and gap-closing motion.
Reverse document order preserves selected document identity.
Fill overflow adds documents. First document and Last document move the tab viewport while the New tab button stays stationary.
Overflow topology changes remain immediate.
The `disclosure` page provides animated expansion, duration choices, and Reverse expansion.
The `popup` page provides an optional Animate popup content switch for parent and nested popup entry.
Popup dismissal remains immediate.
The `dialog` page has an Animate dialog content switch.
Only the form content moves. The title and action buttons stay fixed.
Validation, modal exclusion, and results do not wait for motion.
The `feedback-motion` page demonstrates notice visibility and field-adjacent validation.
Validate the empty name, then type and erase a value.
Show notice and Hide notice control a separate status reveal.
Animate feedback switches both reveals between 180 ms and immediate layout.
These controls do not save files.

The `content-motion` page demonstrates loading, empty, and result content in one fixed slot.
These are manual state changes, not background requests.
Type a result note, select Show loading, then select Show results again.
The native result editor retains its text and undo history.
Enter in the query shows results without moving focus.
Enter in the result note shows loading and returns focus to the query.
The query editor remains outside the transition.
Normal, slow, and immediate durations expose reversal and focus-return behavior.

The `pages` page demonstrates directional entry around a retained `PageView`.
Switch content page brings the second page from the right and the first page from the left.
The old page loses input immediately. Only the incoming page moves, inside a fixed slot.
Each native editor retains its text, selection, and undo history.
Normal, slow, and immediate modes use an application-owned Reveal, not a new PageView property.
Rapid switches restart entry for the latest page. There is no outgoing crossfade.

The `document-motion` page moves retained plain and rich documents inside an expanding top reveal.
Choose Slow, edit or select text, then hide and reopen the pane before exit completes.
Compare Immediate mode and Edit moving rich document.
The native documents retain text, selection, and undo. Closing blocks their input immediately.
This example does not establish media, WebView, opacity, snapshot, or IME support.

The `progress` page provides duration choices, Reset progress, Retarget progress, and Complete progress.
The fill and percentage caption interpolate, while the logical and accessible values change immediately.
Determinate duration choices do not control the separate indeterminate animation.

The [animation contract](animations.md) describes interruption, clipping, input, and performance limits.

Dedicated presentation pages use `toggle-switch`, `toggle-button`, and `progress-ring`.
The `progress` and `progress-ring` pages share Advance, Indeterminate, Pause, and Error buttons.
Their Show indicator switch controls visibility.
These pages use the public controls, not gallery-specific drawing.

The toggle pages separate switch preferences from button-shaped toggle actions.
The progress pages demonstrate determinate, indeterminate, paused, and error states.
Indeterminate motion respects visibility, enabled state, and the system client-area animation preference.
The [foundation contract](foundation-controls.md) defines these controls and animation limits.

The Input category also includes `checkbox`, `hyperlink-button`, and `selector-bar`.
Appearance includes `info-badge`. Commands includes `menu-bar`.
The checkbox example starts in the mixed state with three-state input enabled.
The hyperlink callback reports an action without opening a browser.
The menu bar has File, Edit, View, and disabled Publish roots, with nested Recent examples.
Its sample commands do not change files, access the clipboard, or open a browser.

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
