# Native architecture

These notes map retained controls to their native implementation.
Public lifecycle contracts are in [the application reference](../specs/application.md).

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
General runtime tree replacement and application-defined control renderers are not supported.
`ContentHost` supplies an explicit single-root replacement boundary.
`Window::replace_content` preserves peers outside that host.
It removes obsolete peers in leaf-first order and disconnects their providers.
`src\c_api_content.inc` owns candidate handles, resource retirement, and construction guards.
`bindings\dotnet\Xui\ContentHost.cs` owns managed subscription retirement and scoped callback error delivery.
Ordinary control creation and topology changes remain before-run operations.
Multiline input uses the native document controls described in [the document reference](../specs/documents.md).
Tab data can change without these tree operations.

`include\xui\miller_columns.hpp` and `src\miller_columns.cpp` supply the retained Miller columns composition.
Applications supply a path of immutable sibling sources, not filesystem callbacks.
Column lists reuse virtual collection drawing, input, image resources, and accessibility.
`RowImages` in `src\images.cpp` retains image state for every current visible row, including deferred requests.
`try_request_image` pauses admission at the row queue threshold, with headroom for explicit images and native window icons.
The image service stores one weak wake reference per waiting window.
Queue removal and cancellation wake these owners to admit the next visible requests.
Completed row images do not consume an admission allowance. Actual decode failures stay terminal for the current row image.
`Drawing::image` evicts its least-recently drawn bitmap at the cache entry limit and uploads retained pixels again when needed.
The native child tree stays fixed while the source path changes.
The child tree contains only column headers and lists, without navigation buttons or a toolbar.
`set_columns` reveals the final column when the path grows, without changing the active column.
Zero-width layouts retain a pending reveal until the viewport has a width.
Focused offscreen lists keep their native peers visible but clipped, so host focus repair cannot undo the reveal.
Collection accessibility uses the shared clipped bounds for list and row visibility.
The FileExplorer restores this horizontal position after it restores parent focus.
`MillerColumnList` stores a local hover point and resolves the current visible row without changing collection selection.
The host clears hover during capture and cancellation. Source replacement also clears hover.
`MillerColumns::separator_bounds` describes the reserved space between columns.
The host rounds separator edges and child widths to shared pixel boundaries.
This keeps fractional scrolling and DPI changes from covering a row or scrollbar.
`src\c_api_features.inc` preserves the composition callbacks when bindings subscribe to borrowed column lists.
`bindings\dotnet\Xui\MillerColumns.cs` supplies typed path records, events, and borrowed child access.
The FileExplorer controller owns asynchronous directory scans and rejects obsolete results.
`ExplorerColumn.Filter` owns each column's query. `ExplorerTab.Filter` retains the Details query.
`FilePaneView` binds its shared native Find field to a column object, not a reusable slot index.
Column focus changes restore the query and footer counts without moving focus into the editor.
Filter jobs capture every column's query and reuse unchanged `ColumnPresentation` sources.
Cancellation and identity checks reject results for obsolete paths or queries.
`SaveViewport` retains a selected path when its row is absent from the filtered source.
Filtering therefore preserves descendants without exposing hidden rows as selected command targets.
`tests\miller_columns_tests.cpp` covers the source path, bounded virtualization, selection, layout, and appended-column visibility.
`tests\collections_window_tests.cpp --miller-only` covers native focus, context selection, horizontal reveal, and window closure.
The binding tests cover borrowed peers, source ownership, and callback errors.
FileExplorer `--smoke` covers view changes, folder selection, tabs, Find, and cancellation.

`TabStrip` retains one optional native Button beside its tab data.
The hidden button keeps a fixed child identity for live visibility changes.
`tab_viewport_width` reserves button space. `new_tab_button_bounds` places the button after the final visible tab.
The button uses the existing input and UIA providers rather than a tab identity or a second virtual action protocol.
`src\c_api_layout.inc` exposes its visibility. `src\c_api_features.inc` forwards its callback as `XUI_ACTION` with ID zero.
`bindings\dotnet\Xui\TabActions.cs` supplies the managed property and fluent setter.

The title-bar painter fills baseline gaps outside the populated tab-strip peers.
It uses pixel-rounded peer bounds and never paints beneath a selected tab.
The tab-strip painter remains responsible for its own baseline, colors, and open selected edge.
`tests\tab_window_tests.cpp` covers gap pixels, themes, DPI, button capture, cancellation, keyboard input, and UIA invocation.
`tests\collections_window_tests.cpp --miller-only` also covers hover transitions and separator pixels after fractional scrolling.
These tests need a Windows desktop. Static checks do not establish a passing native test result.

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
