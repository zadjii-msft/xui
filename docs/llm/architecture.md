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
The native child tree stays fixed while the source path changes.
`MillerColumnList` stores a local hover point and resolves the current visible row without changing collection selection.
The host clears hover during capture and cancellation. Source replacement also clears hover.
`MillerColumns::separator_bounds` describes the reserved space between columns.
The host rounds separator edges and child widths to shared pixel boundaries.
This keeps fractional scrolling and DPI changes from covering a row or scrollbar.
`src\c_api_features.inc` preserves the composition callbacks when bindings subscribe to borrowed column lists.
`bindings\dotnet\Xui\MillerColumns.cs` supplies typed path records, events, and borrowed child access.
The FileExplorer controller owns asynchronous directory scans and rejects obsolete results.
`tests\miller_columns_tests.cpp` covers the source path, bounded virtualization, selection, and layout.
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

### Opt-in reveal

`include\xui\reveal.hpp` and `src\reveal.cpp` define the retained four-edge reveal.
The model owns one child, an open target, a duration, and the current presentation.
It has no platform timer or worker.
The public contract and future phases are in [the animation roadmap](../specs/animations.md).

`Window::Impl::sync_animations` supplies clock updates and the Windows motion preference.
It uses one window timer while any reveal remains active.
`Window::Impl::present_animation_frame` gives active animation timers a bounded opportunity between message dispatches and worker deliveries.
The ordinary update path does not advance animation clocks.
The service retrieves real due `WM_TIMER` messages, dispatches queued geometry updates, and paints their frames.
Filtered retrieval preserves `WM_QUIT` for the outer loop, including its exit code.
A window filter includes native child messages. The service dispatches those messages instead of discarding them.
The service also presents a terminal frame after the last animation stops.
Posted traffic cannot indefinitely suppress that work through the normal low-priority timer and paint ordering.
Both application entry points use the same service. There is no extra thread, timer, or idle wakeup.
Private metric 34 counts actual animation timer dispatches. Metric 33 reports whether the shared timer remains active.
Metrics 35 and 36 count successful paints with intermediate SplitView and Reveal presentation, respectively.
These counters separate actual frame delivery from delayed managed observations.
The service applies pending geometry and paint before it retrieves another timer.
This order prevents another timer callback from observing a new progress value with stale native bounds.
`include\xui\animation.hpp` defines the participant interface for that scheduler.
Reveal and SplitView implement the same clock, settlement, and active-state operations.
Applications configure the control-specific duration rather than driving the interface from a managed timer.
`Invalidation::placement` arranges reveal subtrees and updates native peer geometry without a complete root layout.
The ordinary update path still handles peer state, accessibility, and painting.
Entry and completed exit use ordinary layout to reserve or release the row.
`RevealLayout::expand` instead invalidates root layout on each sampled frame.
The model measures its child without a constraint along the animation axis, then multiplies that natural extent by progress.
Repeated parent measurement does not multiply the available slot by progress again.
Arrangement retains the full child extent and clips it to the animated slot.
Unbounded natural content requires an authored size.
The focus and visibility paths permit zero-extent opening clips without changing pointer clipping or the UIA offscreen calculation.

Reveal uses the existing `content_view` role and retained-child traversal.
Its native parent supplies clipping for the editor and button.
The closing target disables interaction before the exit ends.
SplitView also rejects interaction in its outgoing secondary ContentView.
Its secondary content retains the complete target width and moves inside the split viewport.
The surface painter clips pane backgrounds before recursive child painting.
Content retirement and window closure settle the model before the native peers disappear.

The renderer still captures visible native pixels on each frame.
Partial native clips can resize its composition buffers.
This stage does not add a compositor, a bitmap snapshot animation, or damage tracking.
The [test notes](testing.md#reveal-animation-checks) describe the current evidence.

### Collections and background work

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
