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
Transparent child HWNDs retain input and UIA behavior. Native EDIT and document HWNDs supply current pixels through `WM_PRINTCLIENT`.
The host draws captions in the retained frame. Their STATIC HWNDs supply accessible names.
The host clips each custom control and uses pixel-rounded bounds at the current DPI.
Window closure releases graphics resources before the COM runtime stops, even if the caller retains the closed `Window`.

During host synchronization, `navigation_procedure` adds `SWP_NOREDRAW | SWP_NOCOPYBITS` to each peer's `WM_WINDOWPOSCHANGING` flags.
This shared subclass covers custom controls, native fields, captions, and deferred image placement.
Without these flags, Windows copies old child pixels during layout, before the root presents the new positions.
Transparent input HWNDs do not own independent visual frames, so those copies corrupt the visible shared frame.
The host invalidates the root after synchronization and composes native field pixels before presentation.
Standalone native edit bridges retain their normal placement behavior.

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

## Tab drag ownership

`src\application_tab_drag.inc` contains the native title-bar drag session inside `Window::Impl`.
A tab press records a stable ID and screen position.
The system drag threshold forwards `WM_NCLBUTTONDOWN` with `HTCAPTION` to native window processing.
One move-size loop stays on the source HWND.
`WM_WINDOWPOSCHANGING` controls full-window dragging. `WM_MOVING` controls the outline-only fallback.
After the tear-out callback, the same loop moves the source window beneath the dragged tab.
The application creates the remainder window rather than transferring native peers.
Nonactivated windows created during this gesture appear directly below its HWND.

A thread-local `WH_KEYBOARD` hook observes Escape without consuming it.
The hook exists only during the native move loop.
`WM_CANCELMODE` also cancels the gesture.
Cleanup clears target markers and the active session after callbacks or failures.
The source implementation stays alive until native dispatch returns.

Target discovery walks current top-level Z order, including foreign windows as occluders.
It checks current visibility, enabled state, application ownership, modal state, and strip geometry.
`query_drop` checks application acceptance without changing models.
An accepted `join` transfers the model during hover.
The moving HWND becomes hidden through `SWP_HIDEWINDOW` in the native position change, not a separate `ShowWindow` call.
`leave` restores the model to that HWND before `SWP_SHOWWINDOW` reveals it.
The previous target moves below the dragged HWND.
`drop` commits the current transfer only after the native loop returns.
The target is queried again at release, even when the tab is already joined.
Outline-only dragging and handlers that reject `join` retain release-only transfer.
The target marker uses the same tab geometry and palette as normal drawing.
Layered pass-through overlays, alpha-zero windows, and window-region holes do not block targets.
Opaque layered windows still block targets.

The WinUI research reference is `microsoft/microsoft-ui-xaml`, commit `4eabc71e72bbf11039604cd37f475ace0ff4fc02`.
Its `TabView.cpp` selects a new move-loop HWND before entry through `MoveSizeWindowId`.
It does not move all remaining tabs into another HWND.
XUI uses the existing HWND because its Win32 backend does not depend on Windows App SDK input redirection.

The supplied TabsSample reference uses joined and detached states within one native loop.
It modifies `WINDOWPOS` to hide or show the moving HWND and lowers the previous host after departure.
XUI uses that behavior without the sample's placement helpers or non-full-drag cloaking workaround.
The public [`EnterMoveSizeLoop` API](https://learn.microsoft.com/en-us/windows/win32/winmsg/winuser/nf-winuser-entermovesizeloop) is not required.
The documented [`IsWindowArranged` API](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-iswindowarranged) prevents tab positioning from overriding native snap.
XUI resolves that query dynamically for SDK compatibility.

`bindings\dotnet\FileExplorer\Models\ExplorerTabJoin.cs` retains the temporary model transfer.
`ExplorerTabDrag.cs` coordinates that transfer, rollback snapshots, target guards, and deferred window retirement.
Equivalent hosted insertion slots do not cancel navigation or rebuild content.

`tests\tab_drag_window_tests.cpp` drives native messages through a deterministic caption-down boundary.
It covers stationary reordering, retained-HWND tear-out, target acceptance, rejected drops, cancellation, markers, and retirement.
Occluded desktop targets exercise rejection instead of acceptance.
Dedicated hover fixtures use temporary topmost windows without activation.
They exercise live join, repeated join, departure, rejoin, rollback, and rejected commit through real window-position messages.
They also check first-show remainder Z-order, maximized remainder state, and opaque versus transparent layered overlays.
The fixture also requests an actual native move and checks cleanup if user32 rejects it without a held mouse button.
`tests\tab_window_tests.cpp --drag-indicator` checks insertion-marker pixels across styles, themes, and DPI values.
These fixtures do not prove physical pointer continuity or mixed-monitor behavior during an actual system move loop.
Those checks require a desktop interaction with FileExplorer.

## Performance design

### Opt-in reveal

`include\xui\reveal.hpp` and `src\reveal.cpp` define the retained four-edge reveal.
The model owns one child, an open target, a duration, and the current presentation.
It has no platform timer or worker.
The public contract and future phases are in [the animation roadmap](../specs/animations.md).

`Window::Impl::sync_animations` supplies clock updates and the Windows motion preference.
It uses shared transition timer 43 while any opt-in transition remains active.
`Window::Impl::present_animation_frame` gives active animation timers a bounded opportunity between message dispatches and worker deliveries.
The ordinary update path does not advance animation clocks.
The service retrieves real due `WM_TIMER` messages, dispatches queued geometry updates, and paints their frames.
Filtered retrieval preserves `WM_QUIT` for the outer loop, including its exit code.
A window filter includes native child messages. The service dispatches those messages instead of discarding them.
The service also presents a terminal frame after the last animation stops.
Posted traffic cannot indefinitely suppress that work through the normal low-priority timer and paint ordering.
Both application entry points use the same service.
This transition service adds no worker thread or idle wakeup.
Private metric 34 counts actual animation timer dispatches. Metric 33 reports whether the shared timer remains active.
Metrics 35 and 36 count successful paints with intermediate SplitView and Reveal presentation, respectively.
These counters separate actual frame delivery from delayed managed observations.
The service applies pending geometry and paint before it retrieves another timer.
This order prevents another timer callback from observing a new progress value with stale native bounds.
`include\xui\animation.hpp` defines the participant interface for that scheduler.
Reveal, SplitView, TabStrip, NavigationView, Expander, and determinate Progress implement the same clock, settlement, and active-state operations.
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

### Indeterminate progress animation

`Window::Impl::sync_progress_animation` owns separate native timer 44 for eligible indeterminate Progress and ProgressRing controls.
Eligibility does not depend on Classic or WinUI style. Unknown progress remains static.
This timer does not use the opt-in duration or the shared transition participant interface.
Private metrics 37 and 38 report its active state and timer dispatch count.
The timer requests periodic paint while eligible indicators remain visible and effectively enabled.
Hidden or minimized windows, content retirement, disabled ancestors, and window closure remove affected controls from eligibility.
The Windows client-area animation preference suppresses motion without changing the logical progress state.
When no eligible indicator remains, the timer stops.
Zero-duration determinate transitions do not disable this automatic indeterminate presentation.

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
