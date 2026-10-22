# Native architecture

These notes map retained controls to their native implementation.
Public lifecycle contracts are in [the application reference](../specs/application.md).

## Application-discovered regressions

`bindings\dotnet\Xui.Generator\XuiGenerator.cs` distinguishes component identifiers from member names on external expressions.
`GeneratorTests.TestMemberHandlerNames` covers enum values that share handler names and direct component calls.

`src\navigation_view.cpp` shares image metadata across snapshots within one navigation generation.
`set_item_image` changes this metadata without `NavigationList::replace`.
The metadata mutex protects retained snapshots during accessibility reads from another thread.
`NavigationList::disclose` focuses the disclosure row before snapshot replacement.
`navigation_view_tests.cpp` covers image updates and generation isolation.
`navigation_window_tests.cpp --spotify-navigation` sends native pointer messages to scrolled chevrons.

`src\c_api_features.inc` adds a separate action callback through `xui_source_create_extended`.
The original source structures remain unchanged.
`bindings\dotnet\Xui\Features.cs` supplies `ItemContent.Action` through a NativeAOT-compatible callback.
`ItemActionTests.cs` covers pointer delivery, row menus, obsolete keys, disabled rows, and managed callback errors.
The public contracts are in [bindings](../specs/bindings.md) and [commands and navigation](../specs/commands-and-navigation.md).

## Owned model tree and traversal policies

`Element::owned_children()` in `include\xui\core.hpp` is the single model topology contract.
`OwnedChildren` borrows existing shared handles through a three-word view; its iterators borrow
the storage, not the view object. Contiguous shared-handle storage uses a span; Stack projects
the `element` member of each `Child` record with a type-checked member-pointer adapter.
No copied vector, shared-handle acquisition, reinterpret cast, or per-Element field is required.
SplitView stores two base-element handles in the same space as its former two typed handles;
its typed pane accessors return shared handles by value.
All former `Control::retained_children` overrides now override the Element contract.
See the [public lifetime/order/null contract](../specs/application.md#owned-child-enumeration).

The common contract discovers owned edges, not traversal policy:

| Consumer | Ownership discovery and separate policy |
| --- | --- |
| `Window::Impl::content_ids` | All model children, including inactive pages/closed disclosures; duplicate/cycle/null/standard-role validation and foreign-host checks remain explicit. Popup/modal roots are added separately. |
| `claim_deferred_tree` | All children under an unopened Reveal receive claims/presentation/typography, but no native peers. |
| `collect` | Stack and control edges share enumeration. PageView collects only the selected page, plus the established explicit replacement-host exception. Unvisited pages are **not** re-claimed/re-styled on ordinary active updates. Closed Reveal defers peer creation unless replacing its nested host. Expander retains its native visibility policy. |
| `visit_visual_tree` | Enumerates owned edges, then filters through existing peer presence, visibility and viewport logic. Existing all-Stack descent is unchanged; clear-button adornments are explicitly appended. |
| `paint_surfaces` / `paint_content_surface` | Owned edges share discovery, while Stack surfaces, scroll/content viewport clips, split pane surfaces, Expander body clips and popup/adaptive paint ownership stay specialized. |
| `input_fill` / role-specific painting/input | Enumerates composite children for hover; meaningful channel/button/segment indexing remains role-specific. |
| `inspection::enumerate` / `supported` | Walks all owned children directly, including inactive registration targets. No temporary child vector. |
| `inspection::Children`, `hit`, `host_path`, `element_clip` | Borrowed policy view; selected-page filtering, visibility, reverse paint-order hit testing and clipping are separate. Split clips use its axis-aware `first_pane_area` / `second_pane_area` rather than reconstructing horizontal geometry. |
| `ContentDialog::set_visual_style` | Recurses through every owned edge, including formerly omitted ContentView/SplitView slots and hidden descendants, before measuring presentation. |
| `src\c_api_content.inc` and bindings/compiler | Commit delegates native tree validation/inspection to Window. Candidate handle registries and subscription scopes are resource ownership, not layout topology; semantic `xui_feature_child` indices and managed/compiler construction rules are intentionally unchanged. |

ComboBox's inline editor is in its model tree; its popup/choices are a separately coordinated
popup root. Native EDIT clear buttons remain host-peer adornments, excluded from authored
children. `collect_clear_button`, visual traversal, internal-surface painting, `content_ids`
callers' explicit live-ID augmentation, inspection native-target mapping and retirement keep
their existing host responsibilities.
There is no new promise of hosting arbitrary Element subclasses.

`tests\owned_children_tests.cpp` covers exact direct identity/order, borrowed shared-handle
addresses/refcounts, every layout/composite family, hidden slots, custom record projection,
dialog presentation propagation, discarded trees and allocation-free nested traversal.
`tests\content_host_window_tests.cpp --model-only` adds headless candidate rejection,
replacement/lifetime and ownership-versus-hit-test coverage; it never shows/runs a window.
Its `--owned-only` path checks native lazy realization, inactive replacement and a hosted
typography-call invariant with 128 versus 2,048 unvisited items.
`tests\abi_features_tests.cpp` exercises 32 nested inactive-page/Reveal/scroll/split/composite
scope commits and retirements with exact live-handle bounds.
`tests\owned_children_probe.cpp` shares the 8,737-node workload and exact visit/allocation
checks with the core fixture. Its baseline-only adapter is test instrumentation for building
against pristine/stage-1 headers/libraries, not a runtime compatibility layer.
The [stage-2 ledger](architecture-review-2026-10-21.md#stage-2-owned-child-contract-and-traversal-policies)
records actual results and remaining desktop evidence.

## Native hosts

All three applications use `Window` for their native host.
`src\window_host.cpp` supplies COM initialization, the blocking message loop, focus traversal, child placement, and title-bar appearance.
The shared backend uses `Drawing`, theme tokens, and `NativeEditBridge`.
`src\controls.cpp` contains platform-independent behavior. `src\application.cpp` connects controls to Windows and drawing.
`src\control_accessibility.cpp` contains the control providers.

### Collection-family host boundary

`src\host_peer.hpp/.cpp` defines the common materialized peer and the closed
`PeerHostServices` needs implemented by `Window::Impl`. Common storage contains identity,
native input/provider handles, topology and placement/paint caches, not row payloads.
The family translation units do not receive or include `Window::Impl`.

| Owner | Actual state and behavior |
| --- | --- |
| `src\collection_host.hpp/.cpp` | VirtualCollection capability, collection gesture/hover/selection/wheel and lazy row-image state; attach, shared-frame row paint/input, typed UIA publication, cancellation and detach. Includes NavigationList, MillerColumnList and CommandMenu row specialization. |
| `src\grid_host.hpp/.cpp` | Grid gesture/header/resize/reorder/file-press/OLE target/wheel and lazy row-image state; attachment, grid painting/input, publication and retirement. The OLE owner lease aliases the existing window control block through restricted services. |
| `src\file_list_host.hpp/.cpp` | Embeds the existing ListPeer without an extra adapter allocation or redundant generic UIA state; owns its Window lifecycle/theme/placement/thumbnail/shared-frame integration. |
| `src\collection_accessibility.hpp/.cpp`, `src\grid_accessibility.hpp/.cpp` | Concrete immutable collection/grid snapshots, independently retained provider state and bounded value-only action mailboxes (32/64). `src\host_accessibility.hpp` shares identity and locked swap/disconnect mechanics, not family payload storage. |
| Private `Window::Impl::UnextractedPeer` | Only remaining image/runtime/basic/navigation/composite payloads. Collection/grid/list and native-editor payloads are physically absent. The [finite stage-3 ledger](architecture-review-2026-10-21.md#finite-stage-3-family-ledger) assigns the remainder. |

Window-wide focus/modal/popup coordination, exception/dispatch lifetime barriers,
whole-window close, topology/borrowed-child traversal, shared Drawing, clipping and
native-placement batching remain in the window host. It also preserves timer 43 and the
single shared image pass: gather in peer order, cancel hidden work before visible
admission, keep the existing cross-control thumbnail allowance and retain all active
target images together. Offset-only synchronization still bypasses unchanged UIA
publication and editor text/font/selection writes.
Family image eligibility rejects a disabled, empty legacy thumbnail adapter before
shared geometry/visibility work; retained slots keep it participating for cancellation.
Collection/gallery style eligibility is queried only where used, not eagerly for
every shared image participant.

Concrete destructors retire native resources while their state is alive; lookup removal
precedes synchronous native destruction. Providers copy typed snapshots under locks and
use sources outside locks. Replaced/disconnected snapshots and queued actions retire
outside the state mutex; retained children become unavailable after disconnection rather
than retaining live controls. ListPeer likewise moves its old view out before disposal.
The [3a companion](stage3a-collection-host.md) records storage, matched-source evidence
and unresolved native/performance limits. This pilot does not complete finding 1.

### Native editor host boundary

`src\native_field_host.hpp/.cpp` supplies only shared native override identity,
clipping, field-frame/placement primitives and the closed `NativeFieldServices` seam.
`src\text_input_host.hpp/.cpp` owns the real `NativeEditBridge`, caption HWND/state,
optional clear adornment and its dedicated `TextClearHostPeer`, native presentation,
notifications and suggestion integration. `src\document_host.hpp/.cpp` owns
`NativeDocumentBridge`, narrowed to the meaningful `DocumentText` capability shared
by MultilineText and RichText. Password and date/time/calendar each have concrete
host/bridge translation units (`password_host`, `native_password`, `date_time_host`,
`native_date_time`), without document TOM/syntax/range-command payload.

`src\native_field_accessibility.hpp/.cpp` retains independent identity/metadata state,
not live controls or Window. Queries copy only the requested metadata under the
mutex; native/COM operations occur outside it. RichEdit delegates Text/Text2 to its
OS provider. Password value queries remain denied/empty. Clear retains its Pane-root/
semantic-Button tree, runtime identity and identity-checked Invoke/ScrollItem actions.
Native resources retire before provider release and runtime termination; bridge
lifetime tokens protect post-callback revision writes.

Window retains topology, shared Drawing/frame/capture/placement batching, native
navigation interception, focus/modal/popup policy, scheduling and exception/InputScope/
dispatch/close barriers. A phase-local embedding descriptor preserves NumericInput/
ComboBox field presentation without extracting their enclosing behavior; NumericInput
select-all/stepping remains in the finite remainder. Native EDIT/RichEdit still owns
text, composition, selection, caret, undo and clipboard behavior. Scroll-only updates
bypass unchanged editor writes. In particular, idle clear `WM_CANCELMODE` does not
manufacture repaint work; `WM_CAPTURECHANGED` retains its explicit repaint.

This is only stage 3b. The finite 3c-3f remainder and all prior acceptance limitations
remain; stage 4 cannot begin until every family subdivision is accepted and merged.

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

Optional transparent top-level windows are configured before Show/Run in `WindowOptions` and `src\application.cpp`.
Only they use a layered `WS_POPUP` host and `src\drawing.cpp`'s premultiplied-alpha Direct2D DC target plus `UpdateLayeredWindow`; the ordinary HWND target remains unchanged.
The native host paints the first layered frame after layout and before `ShowWindow`, so alpha hit testing is correct from first visibility.
The renderer discards and recreates the layered DIB/DC on resize or device loss, and a 4,096-physical-pixel edge limit bounds the buffer.
`src\c_api.cpp` separates the borrowed native HWND from the C ABI's registry `xui_handle`.
`bindings\dotnet\Xui\WindowNative.cs` retains the one-shot pre-show callback and exposes the checked borrowed `nint`.
`tests\transparent_window_tests.cpp` checks actual premultiplied pixels, resizing, and alpha hit testing; the [C# sample](../../bindings/dotnet/FloatingCard/README.md) checks a painted card's click-through, drag region, and native EDIT.

`include\xui\swap_chain_panel.hpp` defines the Windows-only `SwapChainPanel` interop control.
`src\native_swap_chain_host.cpp` owns its DirectComposition device, target, visual, and producer reference.
`src\application.cpp` supplies native peer lifetime, viewport clips, size/DPI notifications, and native-surface popup restrictions.
The producer owns buffer resizing and presentation.
The handle path imports composition surfaces for renderers such as Windows Terminal's Atlas engine.
The [public contract](../specs/swap-chain-panel.md) separates graphics hosting from the future terminal input and accessibility adapter.
`demo\swap_chain.cpp` and `tests\swap_chain_panel_tests.cpp` exercise native composition without a terminal dependency.
`src\c_api_swap_chain.inc` exposes graphics attachment and metrics through the C ABI.
`bindings\dotnet\Xui\SwapChainPanel.cs` uses the existing scoped subscription trampoline, including NativeAOT callback ownership.
`tests\swap_chain_abi_tests.cpp` covers malformed queries, thread affinity, native peer lifetime, and callback failures.

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
`RowImages` in `src\images.cpp` retains image state for every current visible row, including deferred requests.
`sync_visuals` reconciles at most 512 rows with a sorted call-local lookup instead of two quadratic scans.
Identity includes the item key/version, path, source kind, and physical image size.
Only lookup pointers are sorted: retained slots keep their insertion order, while admission and retained pixel IDs follow input order.
Duplicate visual identities share a slot, and obsolete slots cancel before replacement requests enter the queue.
The two lookup arrays total 16 KiB on x64 and add no retained cache or temporary heap allocation.
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

## System backdrops and acrylic

The [materials contract](../specs/materials.md) describes the public behavior.

`Window::Impl::sync_backdrop` in `src\application.cpp` runs from `apply_theme`, after `platform::appearance`.
`material_popup` records, at HWND creation, whether a transparent window also requested a backdrop.
Such a window omits `WS_EX_LAYERED`, requests `DWMWCP_ROUND`, and uses the premultiplied HWND target; `layered()` selects the `UpdateLayeredWindow` path only for the other transparent windows.
A frameless `WS_POPUP` receives only the flat fallback color (`#545454` for dark Desktop Acrylic), which is what the first version of the acrylic card showed.
The material window therefore uses `WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX`, returns 0 from every `WM_NCCALCSIZE` so the client covers the window, and answers `WM_NCHITTEST` with `HTCAPTION` for the drag region and `HTCLIENT` elsewhere.
It uses the same bottom-band margins as a custom title bar; `-1` margins made DWM draw its caption buttons over the card.
It reads the transparency preference, maps the request to a `DWM_SYSTEMBACKDROP_TYPE`, and calls `platform::system_backdrop` only when the applied type changes.
High contrast and disabled transparency request type zero, which restores the frame margins and `DWMSBT_AUTO`.
A change in presentation discards the root `Drawing` target and toggles the opaque native paint subclass on EDIT and document peers.
New native peers receive the subclass in `collect` when a backdrop is already active.
`WM_SETTINGCHANGE`, `WM_THEMECHANGED`, `WM_SYSCOLORCHANGE`, and `WM_DWMCOMPOSITIONCHANGED` reach `apply_theme`.

`src\window_host.cpp` owns the DWM calls, the build check, the registry preference, and the native paint subclass.
`DwmExtendFrameIntoClientArea` is required: DWM ignores client alpha without it.
Ordinary windows use `-1` margins.
A custom title bar uses a bottom band covering the whole client area instead: a nonzero top margin makes DWM draw its own caption and buttons beneath the XUI title bar (see the frame notes in Windows Terminal's `NonClientIslandWindow`).
While a backdrop is active, `Palette::backdrop` makes resting caption buttons, unstyled collection backgrounds, and unselected tab rails transparent instead of filling the window color.
`E_INVALIDARG` and `DWM_E_COMPOSITIONDISABLED` become a debugger diagnostic and a solid fallback. Other failures throw.
The caption color is reset to `DWMWA_COLOR_DEFAULT`; an explicit caption color hides the material in the system title bar.

`Drawing::begin` takes a `backdrop` flag.
It recreates the HWND target with `D2D1_ALPHA_MODE_PREMULTIPLIED` and grayscale text when the flag changes.
The default HWND target uses `D2D1_ALPHA_MODE_IGNORE`, which DWM treats as opaque; overlay popup child surfaces keep it.
Native pixels composed into the root frame use an ignore-alpha bitmap and stay opaque.

GDI writes zero alpha. Without repair, EDIT backgrounds add to the material and dark text disappears.
`opaque_paint` replaces `WM_PAINT` with a buffered `WM_PRINTCLIENT` and `BufferedPaintSetAlpha(255)`.
`WM_PRINTCLIENT` passes through the whole subclass chain, so the search hint, line numbers, and occlusion clipping still apply.
Input that EDIT or RichEdit draws immediately triggers a synchronous `RedrawWindow`; programmatic text and format changes only invalidate.
Layered child windows would also make native peers opaque, but they require a Windows 8 manifest entry in every host executable.

`Drawing::acrylic` implements the in-app material.
`CopyFromRenderTarget` fails while clips or layers are active, so `Drawing` now records its clip stack.
`suspend_clips` pops every clip, the copy runs, and `resume_clips` restores each clip with its original transform.
The effect graph is Gaussian blur (24 DIPs, hard border) into a luminosity blend with a flood color, followed by a tint fill and a 64-pixel noise brush at 2% opacity.
The source bitmap matches the copied rectangle and is recreated when its size changes. Effects and the noise brush live until the target is discarded.
`ID2D1DeviceContext` comes from the existing render target; `E_NOINTERFACE` uses the solid fallback.
Direct2D's luminosity blend inverts hues when the target lightness is near white, which turned blue swatches yellow under the light material.
Light tints therefore blend inverted colors between two `ColorMatrix` inversions; `1 - SetLum(1 - c, 1 - l)` equals `SetLum(c, l)`.
A 16-bit float buffer precision did not change the artifact.

`tests\backdrop_window_tests.cpp` checks acrylic pixels through the software layered target, which exposes its DIB.
That part needs no desktop capture. The window part needs an unlocked desktop for owned Graphics Capture.
A non-activated test window can receive the solid inactive material, so the test checks DWM attributes rather than material pixels.

### Material evidence (October 6, 2026)

Windows 11 build 26200, x64 Release build in `build\x64`, Visual Studio 2022 Preview, SDK 10.0.26100.0.

- A standalone Win32 probe showed that a premultiplied HWND target with `-1` frame margins exposes the DWM material layer behind cleared pixels, and that an ignore-alpha HWND target is opaque.
- The same probe showed GDI EDIT backgrounds adding to the material. The buffered-paint subclass made them match the surrounding opaque surface, including text typed through `WM_CHAR`.
- `xui_backdrop_window_tests` passed: layered acrylic pixels in both themes, clip restoration, the solid fallback, DWM attributes for all three materials, and identical opaque surface and EDIT pixels with and without a backdrop.
- `xui_abi_features_tests`, `xui_abi_features_tests --backdrop`, `xui_gallery_catalog_tests`, and the C# `--window-native` tests passed.
- Screen captures of the gallery with a custom title bar and with `--system-titlebar` showed the DWM material layer in the caption, catalog, and margins, with no DWM caption buttons beneath the XUI title bar.
- The workstation was locked for part of the session. During that time, Graphics Capture omitted Direct2D content and many existing window tests failed identically on an unmodified baseline build.
- Not established: battery-saver behavior, transparency-setting changes while running, remote sessions, mixed-DPI moves, and Narrator review.
- The FloatingCard `--smoke` and `--acrylic --smoke` runs passed when started directly. Under `dotnet run`, both failed hit testing while two earlier topmost card instances still covered the screen center.
- A screen capture of the first `FloatingCard --acrylic` (a frameless `WS_POPUP`) showed rounded corners, a compositor shadow, and a flat `#545454` fill: the Desktop Acrylic fallback color, not a blur. An earlier note in this session misreported it as blurred.
- Before Explorer was restarted, every DWM material on the machine, including the taskbar, rendered as a flat fallback. After the restart the taskbar was translucent again.
- Afterwards, screen captures of XUI windows over a striped test window showed real Desktop Acrylic: blurred, saturated stripe edges behind both an ordinary captioned window and the material card. The frameless `WS_POPUP` still showed only `#545454`; the captioned frame with its non-client area removed showed the blur. With `-1` margins the card also showed DWM caption buttons; the bottom-band margins removed them.
- Single-pixel samples at stripe centers can look nearly unchanged under dark acrylic, because blurring a wide uniform stripe preserves its color. Use edge softness, not center pixels, to judge the blur.

## Composition presentation prototype

Status: experimental, opt-in (`WindowOptions::composition`). Classic windows are unchanged.
The [public contract](../specs/materials.md#experimental-composition-presentation) describes the API and restrictions.

### Why a separate presentation

DWM system backdrops fill the whole window rectangle, and a window region makes DWM fall back to its flat color.
In-app acrylic (`Drawing::acrylic`) can only blur pixels the window has already painted.
A `Windows.UI.Composition` `HostBackdropBrush` blurs the desktop in any clipped shape, but a composition tree attached with `CreateDesktopWindowTarget` always composes above the HWND's redirection surface, even with `isTopmost=FALSE`.
A probe confirmed that a Direct2D HWND target drawn on that surface disappears under the backdrop visual.
So the window content must itself be a composition visual: a premultiplied swap chain above the backdrop sprites, in a `WS_EX_NOREDIRECTIONBITMAP` window.

### Source map

- `src\composition_presenter.{hpp,cpp}`: the only C++/WinRT code. Lazily creates a CoreMessaging `DispatcherQueue`, a `Compositor`, a `DesktopWindowTarget`, a backdrop container of `HostBackdropBrush` sprites (rounded `RectangleClip` and a tint child), and a content sprite whose surface brush wraps the swap chain. Content uses `RelativeSizeAdjustment` so a resize needs no composition commit; an explicit `Size` made every resize show one stale frame.
- `src\drawing.cpp`: `begin_composition` creates a D3D11 device (`d3d11.dll` resolved with `LoadLibraryExW` on first use), a D2D device context, and a `FLIP_SEQUENTIAL` composition swap chain with premultiplied alpha. Resizes release the target bitmap before `ResizeBuffers`. `backdrop_hole` suspends the clip/layer stack, clips to the visible pixels, and clears the rounded area with `D2D1_PRIMITIVE_BLEND_COPY`; a COPY fill inside a rounded-clip layer only clears the layer, which then blends over the parent (found in review).
- `src\application.cpp`: `options.composition` selects `begin_composition`; `present_composition` mirrors the frame's holes into the presenter after `Present`. `composition_card()` windows reuse the hidden-caption frame, square corners and no DWM border (`platform::composition_frame`), and a `SetWindowRgn` built from painted surfaces and controls (`add_hit_shape`, `apply_hit_region`, keyed by DPI). Composition cards paint once before show so the hit region exists before the first input.
- `platform::layered_child` makes EDIT, RichEdit, and date/time peers layered: without a redirection surface, only layered children show their pixels. This needs a Windows 8+ compatibility manifest; `demo\xui.manifest` has one.
- `SwapChainPanel` producers join the window's composition tree, and `RuntimeHost` children draw above it (see [Panels and runtime hosts](#panels-and-runtime-hosts-october-7-2026)).
- Linking: `xui_windows` links `runtimeobject` for WinRT activation. `windowsapp` was tried first and pulled about 30 `api-ms-win-core-*` imports into `xui.dll`. With `runtimeobject`, `xui.dll` gains only `api-ms-win-core-winrt-l1-1-0` and `api-ms-win-core-winrt-error-l1-1-1`; CoreMessaging and Direct3D 11 load on demand.

Known gaps (October 6, 2026): popups, overlay surfaces, and native menus are untested in composition windows. Device-loss recovery reuses the generation counter but has not been exercised. A review flagged `TextInput` caption `STATIC` children as invisible without a redirection surface; that does not apply, because `paint_edit` draws the caption text in the frame and the `STATIC` only supplies the accessible name (the test card shows its caption).

### Overlays and device loss (October 7, 2026)

`tests\composition_window_tests.cpp --overlays` runs the same flow on a composition window and, with `--overlays-classic`, on a classic window. It checks a root-drawn popup with its own native editor over another native editor, backdrop acrylic inside a popup, a combo box popup, a menu bar submenu, a command surface, a tooltip, a content dialog's dim, the separate suggestion list window, and a native `#32768` context menu driven from a worker thread. `--card-overlays` checks the transparent card hit region with an open popup and an open dialog. `--device-loss` injects `D2DERR_RECREATE_TARGET` and `DXGI_ERROR_DEVICE_REMOVED` at `EndDraw`, sends `WM_DISPLAYCHANGE`, and repeats the removal with a popup open; it compares composed pixels, the backdrop visual count, live targets, and editor text after each recovery.

Defects found and fixed:

- Clearing a native editor's occlusion region (`SetWindowRgn(hwnd, nullptr)`) left the layered child showing black where a dismissed popup had covered it. Layered children keep their last pixels; `sync_native_occlusion` now invalidates them after removing a region in composition windows.
- `backdrop_acrylic` inside a popup punched a hole to the desktop through the window content beneath. Popups (`owner != 0`) now paint the in-app material.
- A content dialog's dim on a transparent card did not block input to the windows behind the card. The dim now adds a full-window hit shape.
- `Drawing::end` treated only `D2DERR_RECREATE_TARGET` as recoverable. A composition device can also report `DXGI_ERROR_DEVICE_REMOVED` or `DXGI_ERROR_DEVICE_RESET` from `EndDraw`; those now discard and recreate the target too.

Not covered: a real GPU reset (TDR) or driver update. Injected results exercise the same recovery path, and recovery always creates a fresh D3D device. Popups were tested at one DPI and in the dark theme. The test drives tooltips with the delay timer message, as `foundation_window_tests` does, because a synthetic hover is cancelled by the real cursor position.

### Panels and runtime hosts (October 7, 2026)

A classic panel creates a `CreateTargetForHwnd(topmost)` DirectComposition target on its peer HWND. In a composition window that target would sit above the window's own tree, so popups would need the opaque child-window surfaces and acrylic could never show through producer pixels. Instead:

- `CompositionPresenter` has a panels container above the content sprite and an overlay sprite above the panels. `add_panel` creates a sprite; `panel_content` wraps either the producer's swap chain or its surface handle (`ICompositorInterop::CreateCompositionSurfaceForHandle`); `place_panel` sets the offset (peer origin mapped to the root), size, `InsetClip`, and visibility.
- `NativeSwapChainHost` receives a `weak_ptr` to the presenter. `SwapChainPanel::State` keeps the producer (`chain`, or a duplicated surface handle so composition hosts can import it after the caller closes its copy) in both modes. Composed hosts bind it into the presenter; classic hosts keep the old DirectComposition path.
- `paint()` paints popups and tooltips into a second composition `Drawing` (`composition_overlay`) when a panel is active: it ends and presents the root frame, begins the overlay with a transparent clear, paints popups and tooltips, and presents the overlay sprite. Without an active panel, popups draw into the root frame as before and the overlay is hidden. `prepare_popup_surface` is skipped in composition windows.
- The panel field fill is skipped while a composed producer is attached so translucent producer pixels show the content beneath, including `backdrop_acrylic` holes.
- A panel inside a popup (`popup_owner != 0`) moves to a `popup_panels` container above the overlay and does not count for `active_panel()`; otherwise its own popup face, drawn in the overlay, would cover it. A popup nested above such a panel does not cover it (the classic path stacks child HWND surfaces instead). The overlay `Drawing` takes part in `keep_images`, so popup images do not stay charged against the shared GPU image budget.
- A failure while binding a composed producer closes the host binding and keeps the producer, so the panel can be hosted again.
- `MediaPlayback` (MFPlay `STATIC` child) and `WebContent` (WebView2 child windows) needed no changes: child HWNDs draw above the composition tree. The classic rule that an XUI popup cannot open over active runtime content still applies, because those children would cover the popup.

`tests\composition_window_tests.cpp --panels` checks a premultiplied producer over a rounded acrylic card (opaque square visible, transparent pixels show the blurred stripes and not the field color), a surface-handle producer whose caller closed its handle, a popup and a tooltip above an active panel through the overlay layer, a panel inside a popup above its own face, hide/show, window target recovery with panels attached, detachment, and closure. `--runtime-hosts` plays a local video and, in a WebView2 build (`XUI_ENABLE_WEBVIEW2`), checks web pixels, the popup restriction, and popups after unload. `xui_swap_chain_sample --acrylic` shows the triangle over desktop acrylic.

Test notes: the initial focus offers a tooltip on the first focusable button, and a dismissed popup returns focus to a button that can then offer one, so the panel and overlay tests use long tooltip delays and drive the timer themselves; the WinRT compositor commits batched visibility changes when the thread pumps messages, so captures of overlay visibility need a pump. `RuntimeHost::visual_bounds()` is local to the control.

Regression runs: classic `xui_swap_chain_panel_tests` fails at "Y-axis quarter turn makes the triangle edge-on" identically on the baseline commit. `xui_swap_chain_popup_input_tests` failed 3 of 25 runs at popup-injection checks during a busy period and passed 6 of 6 on the baseline and 18 consecutive runs afterwards; the classic paths it exercises are unchanged.

### Measurements (October 6, 2026)

Machine: Windows 11 build 26200, AMD Radeon 780M, 120 DPI, x64 Release, Visual Studio 2022 Preview. Desktop unlocked, transparency effects on.
Baseline: commit `6353a24f` built in a separate worktree with the same probe source (`XUI_PROBE_BASELINE` removes the composition option).
Tool: `tests\composition_probe.cpp` (one configuration per process) driven by `tests\measure-composition.ps1` with `-Runs 8 -Sizes 800x600,1600x1000 -ResizeCycles 40 -HoldSeconds 8`; configurations alternate in ABBA order. Values are medians of 8 processes.
Workload: WinUI window, heading, a rounded panel with six labels, a native editor, and a button. `classic` uses a solid surface panel; `classic-acrylic` uses in-app acrylic; `composition` uses backdrop acrylic; `composition-card` is the transparent card form.

Opt-out cost (classic windows, prototype vs baseline):

| Measure | 800×600 baseline | 800×600 prototype | 1600×1000 baseline | 1600×1000 prototype |
| --- | --- | --- | --- | --- |
| Paint, Mcycles per full repaint | 13.22 | 13.32 | 14.39 | 14.10 |
| Paint p95, ms (vsync-bound) | 18.27 | 18.10 | 18.14 | 18.34 |
| Resize p50 / p95, ms | 31.7 / 47.8 | 32.0 / 62.7 | 30.6 / 34.2 | 30.6 / 33.4 |
| Private commit shown / after 40 resizes, MiB | 41.6 / 50.2 | 41.3 / 49.9 | 56.5 / 80.4 | 56.1 / 80.0 |
| GPU local memory, MiB | 23.2 | 23.2 | 44.0 | 44.0 |
| Threads / loaded modules | 20 / 48 | 20 / 48 | 20 / 48 | 20 / 48 |
| Idle paints in 2 s | 0 | 0 | 0 | 0 |

`xui.dll` grew from 2,915,328 to 2,949,120 bytes (+33 KB; this includes the earlier backdrop work). Classic windows load no new modules (CoreMessaging absent, same module count). The single 800×600 resize p95 outlier (62.7 ms) is within run-to-run noise: the same configuration's 1600×1000 p95 is below baseline, and its resize Mcycles (71.4 vs 65.3) overlap the baseline spread. In-app acrylic (`classic-acrylic`) matches its baseline too.

Opt-in cost (composition vs classic, prototype):

| Measure | classic 800×600 | composition 800×600 | classic 1600×1000 | composition 1600×1000 |
| --- | --- | --- | --- | --- |
| First paint, ms from launch | 210 | 245 | 192 | 199 |
| Paint, Mcycles per full repaint | 13.32 | 16.08 (+21%) | 14.10 | 15.19 (+8%) |
| Paint p50 / p95, ms | 16.6 / 18.1 | 16.6 / 17.7 | 16.6 / 18.3 | 16.6 / 18.2 |
| Resize p50 / p95 / max, ms | 32.0 / 62.7 / 84.4 | 31.7 / 47.5 / 55.6 | 30.6 / 33.4 / 40.4 | 30.1 / 44.4 / 46.2 |
| Resize Mcycles per step | 71.4 | 66.6 | 47.8 | 49.2 |
| Private commit shown / after resizes, MiB | 41.3 / 49.9 | 40.3 / 45.3 | 56.1 / 80.0 | 46.7 / 56.5 |
| GPU local memory shown / after resizes, MiB | 23.2 / 25.5 | 20.1 / 20.6 | 44.0 / 46.5 | 35.9 / 39.9 |
| Threads / modules | 20 / 48 | 22 / 53 | 20 / 48 | 22 / 53 |
| Idle Mcycles in 2 s / idle paints | 5.2 / 0 | 6.0 / 0 | 4.3 / 0 | 5.6 / 0 |
| Resize frames with stale edge or missing backdrop (20 steps × 8 runs) | 0 | 0 | 0 | 0 |

The composition card form (`composition-card`) matched `composition` within noise (paint 17.4 / 16.9 Mcycles).
Compared with in-app acrylic on a classic window, backdrop acrylic used less memory at 1600×1000 (46.7 vs 78.9 MiB private, 35.9 vs 63.0 MiB GPU) because the blur runs in the compositor rather than in a per-window Direct2D effect graph.

Compositor load: a 60 Hz full-screen GDI stripe animation behind each window, system-wide 3D engine utilization sampled each second for 6 s, 4 runs per configuration. Medians: none 5.45%, classic 5.42%, composition 5.57%, composition card 5.58% at 800×600; none 7.25%, classic 7.27%, composition 7.30%, composition card 7.50% at 1600×1000. The backdrop blur's added GPU load was within the variance of this counter. The counter is coarse: `dwm.exe` reported no 3D usage of its own, so compositor work is attributed elsewhere (for example `csrss`), and this does not measure frame latency or power.

Method limits: process cycles exclude DWM. Synchronous paint wall time is vsync-bound (~16.6 ms) for both paths, so cycles are the useful paint cost. One machine, one GPU, one DPI. Resize uses `SetWindowPos` from the UI thread, not an interactive drag; interactive live-resize smoothness was not measured. The artifact check samples the newly exposed edge and the panel interior over a solid magenta window after each 8-pixel growth step. The raw JSON was kept outside the repository; rerun the command above to reproduce.

### Decision inputs

- Not opting in costs nothing measurable at runtime; the binary grows by tens of kilobytes and links `runtimeobject`.
- Opting in costs about 1–3 Mcycles more CPU per full repaint (8–21%), two threads, five modules, and about 35 ms more first-paint time at the smaller size. It saved private and GPU memory compared with classic presentation in this workload.
- Resizes showed no stale frames after the content sprite switched to relative sizing.
- The remaining risk is breadth, not cost: native runtime hosts, swap chain panels, and interactive resize need work or verification before this could be more than an opt-in experiment. Popups, menus, and device loss are covered by [the overlay and device-loss tests](#overlays-and-device-loss-october-7-2026).

## Tab drag ownership

`src\application_tab_drag.inc` contains the native title-bar drag session inside `Window::Impl`.
A tab press records a stable ID and screen position.
The system drag threshold forwards `WM_NCLBUTTONDOWN` with `HTCAPTION` to native window processing.
One move-size loop stays on the source HWND.
Single-tab detection uses native peer visibility, including arranged geometry, rather than the strip's visibility property alone.
This excludes the retained tab in FileExplorer's collapsed second pane from the initial reorder decision.
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
The Explorer hover smoke also covers a fresh single-tab source, cancellation, and transfer into a destination with multiple tabs.

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

### Retained scrolling

`ScrollView::set_offset` requests `Invalidation::scroll` instead of a full layout.
`Window::Impl::update` translates an internal `Xui.ScrollContent.1` HWND when only the offset changes.
The logical bounds change with the native geometry, but individual editor HWNDs do not move within that layer.
This preserves native selection, composition, drafts, and editor identity.
Native child enumeration must account for this internal layer rather than assume that editors are direct children of the viewport.

The offset-only path avoids repeated tree collection, typography updates, and root measurement.
Indexed peer lookup replaces repeated linear searches.
Mixed mutations and active animations retain the full update path.
Stationary popups do not force root layout during scrolling.
Clipped controls skip drawing and native pixel capture.
Native controls without popup overlap skip occlusion-region allocation.

Wheel dispatch still completes the update synchronously, and the Windows wheel-distance setting stays unchanged.
`src\wheel.hpp` converts signed deltas to 16-DIP Windows wheel lines (or viewport pages).
FileList, ScrollView, VirtualCollection, and DataGrid use the same distance instead of row-sized steps.
The collection wheel animation remains for whole notches, while partial deltas update immediately to
avoid repeatedly restarting an animation during a trackpad gesture. Scrollbar offsets remain continuous.
`tests\scroll_tests.cpp --retained-only` covers the native path.
The FileExplorer `--settings-scroll-smoke` check measures both dispatch and pending paint work with the complete inline settings editor.

### Layout and frame hot paths

`Window::Impl::update` collects the retained tree and synchronizes peers.
A pending layout request also calls root measurement and arrangement.
`Stack::layout_children` measures fixed children before flex children on each pass.
Nested stacks therefore repeat measurement during root measurement and descendant arrangement.
The framework does not cache arbitrary application measurements.

Each Stack retains a scratch vector for child sizes.
A layout pass takes exclusive ownership of that vector and returns it after completion or an exception.
A reentrant pass uses separate storage, so it cannot overwrite pending outer sizes.
Each pass still measures children against the current constraints.
Retained capacity depends on the largest child count, not the number of frames.
Cross-axis alignment reads the selected alignment directly instead of copying a complete `PartStyleValues` for each child.

Grid track sizing uses two call-local arrays bounded by the existing 256-track-per-axis limit.
Each array starts with desired sizes and is resolved in place to fixed, automatic, or star allocations.
This removes the temporary desired/result vectors without caching measurements or adding retained storage to a Grid.
The arrays contain 2 KiB of floats per active sizing result; nested and reentrant passes have independent storage.
Child measurement order, star saturation, spans, gaps, and alignment are unchanged.

`Window::Impl::paint` submits visible peers and captures native control pixels into the shared frame.
Complete native capture preserves text, selection, scrolling, and popup composition.
Tree collection, peer synchronization, and native capture remain profiling candidates for large forms.
They are not measured bottlenecks in this pass.
Partial native capture or measurement caches need separate invalidation and frame-correctness evidence.

### Text cache lookup

`Drawing::styled_format` and `Drawing::styled_layout` run during styled text measurement and painting.
Their retained keys contain only typography fields.
Cache hits compare the supplied style fields without a temporary `PartStyleValues` or a new shared font-family reference.
Cache entries retain font-family ownership only on insertion.
Equivalent family names still share cache entries.

The format cache retains at most 64 entries. The layout cache retains at most 128 entries.
Layout keys still include text, width, line limits, and effective wrapping.
Long text and private text retain their cache exclusions.
Cache hits still restore mutable layout bounds.
Color and other non-typography properties do not change text identity.
The optimization reduces lookup work and key storage, not DirectWrite shaping cost on a miss.

### Collection projection and selection

Immutable collection projections retain a row offset for each span.
Row access uses binary search over these offsets instead of a scan from the first span.
The index costs one `size_t` per span, not one entry per source row.
Group headers, collapsed content, and virtual source ranges retain their existing identities.

Projection identity searches still visit spans in order.
Consecutive ranges from the same source reuse one source lookup, including ranges separated by headers or gaps.
`CollectionSelection::contains` applies the same rule to range terms separated by point terms.
A source change resets the reused lookup.
Each call owns its lookup state, so later calls cannot use stale results.
Source exceptions still propagate.

`CollectionSelection::selected_keys` buffers matches locally for sources of at most 256 rows, then sorts and deduplicates them before allocating the returned vector.
Empty results need no heap storage; nonempty results allocate only for the selected keys, plus allocator overhead.
Larger sources within the caller's enumeration limit use a vector and sort, not per-key tree nodes.
Sources exceeding the limit retain the term-based bounded enumeration path and its explicit refusal of uncounted selections.
Enumeration never retains new data in the source or selection.

`DataGrid::select` snapshots selection history only when the focused identity is unchanged.
A changed identity already guarantees notification, so copying and comparing the term vector serves no purpose.
Same-focus gestures retain full equality comparison, including anchors and term structure, rather than substituting a revision counter.
Invalidation and callback ordering remain unchanged, including callback removal and reentrant selection during dispatch.

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
`collect` defers native children of closed, settled reveals.
`claim_deferred_tree` still claims their retained models and applies window typography.
The deferred tree uses the same supported-control validation as replacement content.
Opening a reveal collects its children before layout and native focus.
Existing peers remain during reveal closure, so native selection and undo state survive reopening.
Replacement of a hidden ContentHost bypasses deferral for its containing reveal.
Popup dismissal still prunes its peers and releases claims. This path does not cache closed popups.
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
