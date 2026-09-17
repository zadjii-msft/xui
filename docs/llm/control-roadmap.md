# Control families and gallery

Research date: September 12, 2026.

## Current choice, badge, and menu additions

The September 17, 2026 extension adds CheckBox, HyperlinkButton, SelectorBar, InfoBadge, and MenuBar.
The [maintainer update](winui-maintainer-handoff.md#additional-choices-badges-and-menu-bars) records the scope and source map.
The [control catalog](../specs/controls/README.md) contains their application guides.
TeachingTip, Rating, and color, dialog, and calendar upgrades remain outside this extension.
The original research and delivery notes retain their historical scope.

## Current toggle and progress presentations

The September 17, 2026 implementation adds ToggleSwitch, ToggleButton, and ProgressRing.
Indeterminate bars and rings now use lifecycle-bound animation.
The [foundation contract](../specs/foundation-controls.md) defines the current behavior.
The [WinUI update](winui-maintainer-handoff.md#toggle-and-progress-update) records the implementation scope and remaining runtime checks.
Later delivery notes preserve their historical batch scope, including earlier static-progress and binding limits.

## Scope and evidence

This inventory compares File Pilot evidence with the official Windows control catalog and the current XUI C++ API.
File Pilot evidence includes public material, user screenshots, and a limited live inspection.
It identifies reusable control families, not one control for each product feature.
The gallery demonstrates the completed foundation, collection, command/navigation, document, scene, and native-host families. It does not claim WinUI parity.

The initial research used public sources. File Pilot was not open during that phase.
The September 12 follow-up found File Pilot v0.8.5 open and captured its current window.
The five user screenshots extend the visual evidence to additional popup surfaces.
The live walkthrough is partial: the main surface received inspection, but safe foreground activation failed.
No file operation, folder navigation, clipboard access, installation, or settings change occurred.
Still images do not establish keyboard behavior, accessibility, input latency, animation quality, or error handling.

Sources:

- **[W]** [Windows controls and patterns](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/).
  The retrieved page identifies documentation commit `4991cf9fe638325704c1330aa6a700ce85406e42`.
- **[G]** [WinUI Gallery catalog JSON](https://github.com/microsoft/WinUI-Gallery/blob/be5624441fa8667761053bb5bae4db5d5a7ad4e7/WinUIGallery/SampleSupport/Data/ControlInfoData.json).
  The repository commit is `be5624441fa8667761053bb5bae4db5d5a7ad4e7`.
  The JSON contains 120 sample entries, not 120 independent controls.
  Gallery entries include design, platform, and animation samples. They are not all controls.
- **[F]** [File Pilot product page](https://filepilot.tech/).
  It documents panels, tabs, search, Inspector, batch rename, GoTo, command palette, context menus, and customization.
- **[F81]** [File Pilot 0.8.1 release](https://filepilot.tech/starlog/2026-07-15-v0.8.1).
  The full HTML documents trees, column lists, collapsible groups, checkbox drag selection, and column reordering.
  This is a referenced release, not a claim about the latest release.
- **[F71]** [File Pilot 0.7.1 release](https://filepilot.tech/starlog/2026-03-16-v0.7.1).
  It documents size-dependent image decoding, tooltip timing, popup scrolling, and keyboard behavior.
- **[FS]** [Official folder-view image](https://filepilot.tech/Assets/FolderViews.png), linked from [F81].
  Visual inspection shows breadcrumb segments, icon actions, exclusive circular choices, a separator, and a popup surface.
  This is an observation of a public still, not an observation of a live application.
- **[FR]** [File Pilot roadmap](https://filepilot.tech/roadmap).
  Its full HTML distinguishes Planned, Future, and Launched sections.
  Miller columns, larger thumbnails, virtual folders, and undo/redo appear under Planned.
  They are not evidence of shipped controls.
- **[S1]–[S5]** User-supplied screenshots, September 12, 2026.
  Their supplied descriptions identify the main view, GoTo popup, grouped quick-access picker, view popup, and searchable context menu, respectively.
  These are visual references, not independent live interaction results.
- **[L1]** Local window capture of File Pilot v0.8.5, September 12, 2026.
  It shows the navigation sidebar, details list, selected row, header checkbox, header filter icon, breadcrumb, tabs, and view trigger.
- **[L2]** Local capture after the failed foreground and UIA focus attempts.
  The location, selected row, geometry, and closed-popup state match [L1].

The live probe matched process `FPilot`, PID `22696`, and window `19665424`, with window class `File Pilot`.
The initial rectangle was `(-1685, 375, 1024, 792)` in physical screen coordinates.
`PrintWindow` produced both window-only captures without a desktop capture.
The scoped UIA tree exposed one Window element with Window and Transform patterns.
It exposed no child controls. This result does not establish accessible selection, text, menu, or range contracts.
The inactive window reported no focused child through `GetGUIThreadInfo`.
`SetForegroundWindow` did not establish target focus. UIA `SetFocus` then reported that the target could not receive focus.
No click or key reached File Pilot. The probe did not retry through global input or open another application.
Foreground ownership changed outside the probe, so no forced focus restoration occurred.
Remaining live work covers two safe popup open/dismiss cycles, child focus return, and keyboard/UIA behavior.

Private evidence stays in `build\filepilot-review`.
The captures contain user paths, so this document does not embed them or repeat those paths.
The probe did not enumerate network locations or open files.

Raw HTML was necessary for [F81] and [FR]. The text extractor omitted most of their sections.
No File Pilot assets or implementation code form part of the XUI gallery.

## Existing overlap

| Family | WinUI names or patterns | File Pilot evidence | Current XUI |
| --- | --- | --- | --- |
| Actions | Button, HyperlinkButton, AppBarButton | Icon actions observed in [FS] | `Button`, vector `ButtonIcon`, callbacks, enabled state |
| Independent choice | CheckBox, ToggleSwitch | Checkbox selection documented in [F81] | `Toggle` exposes checkbox semantics, not switch or radio semantics |
| Single-line input and suggestions | TextBox, AutoSuggestBox | Search and GoTo documented in [F] | Native `TextInput`, search style, `SuggestionSource`, shared path input |
| Typography | TextBlock, labels | Text observed in [FS] | Measured `Label`, heading, caption, semantic tones |
| Linear layout and scrolling | StackPanel, Border, ScrollViewer | Panels documented in [F] | `Stack`, surfaces, `ScrollView` |
| Flat virtual collections | ListView, ItemsView | File views documented in [F81] | `ItemsView` has virtual rich rows, tiles, groups, shared multi-selection, and explicit select-all scope. `FileList` remains file-specific |
| Hierarchical collections | TreeView | Trees documented in [F81] | `TreeView` has lazy, cancelable expansion and bounded virtual child indexes |
| Data tables | Table presentation, not a stock Gallery DataGrid entry | Details columns, header checkbox, and filter icon in [L1] | `DataGrid` has immutable sources, sort, resize, reorder, header filters, selection check columns, and shared multi-selection |
| Tabs and page selection | TabView, Pivot, SelectorBar, FlipView | Tabs documented in [F] | `TabStrip`, `PageView`, `ContentView` |
| Split content | SplitView, two-pane pattern | Arbitrary panels documented in [F] | Two-pane `SplitView`, not arbitrary docking |
| Images | Image, PersonPicture as a presentation | Inspector documented in [F], decoding limits in [F71] | Bounded `Image`, WIC previews, Shell thumbnail boundary |
| Menus | MenuFlyout | Search, icons, pin action, shortcut hints, and submenus in [S5] | Flat styled owner-drawn native `MenuItem` menus. No Shell provider contract |
| Confirmation | ContentDialog shares a purpose, not an implementation | No live dialog inspection | `Window::confirm` uses TaskDialog or MessageBox |
| History visualization | No matching stock chart entry in [G] | Not established | `HistoryChart`, fixed 60-sample storage |
| Themes and focus | Theme and accessibility patterns | Customization documented in [F] | Dark/light/high contrast, keyboard focus, UIA, native EDIT |

The C ABI, C#, and Rust bindings now expose usable APIs for every implemented control family in this inventory.
The extension adds 35 typed constructors, including compositions and the earlier workspace controls.
It preserves the original nine kinds and their record layouts.
Coverage includes numeric values, choices, virtual sources, lazy trees, commands, dialogs, documents, scenes, maps, and explicit native-host loads.
It does not expose every C++ method or every gallery recipe.
The [binding coverage section](../specs/bindings.md#feature-bindings-11-extension) lists the remaining advanced API gaps and validation commands.

## Deduplication rules

Button, icon button, hyperlink button, repeat button, and split button belong to the action family.
Their activation, repeat, toggle, or popup contracts can require extensions. They do not require unrelated base controls.
CheckBox and RadioButton stay separate because independent and exclusive selection have different keyboard and UIA contracts.
ComboBox also needs a popup selection contract. It is not an editable suggestion field with a different border.

List, tiles, and grouped items share a virtual collection family.
Data tables retain a distinct column contract. Trees retain a distinct hierarchy contract.
Multi-selection belongs to collection selection models, not a standalone visual control.

Pivot, SelectorBar, PipsPager, and FlipView can use tab/page selection recipes.
NavigationView and list/details can use selection, pages, and adaptive layout recipes.
The sidebar and quick-access picker share grouped selection, disclosure, icons, secondary text, and optional capacity meters.
They do not require separate navigation controls for each location or group.
The rich folder picker composes a popup host, input, virtual collection, navigation toolbar, and footer shortcut hints.
Folder enumeration and navigation remain provider callbacks, not popup-host responsibilities.
The view picker composes exclusive choice and a vertical range editor inside an anchored popup.
It is not only a ComboBox.
TeachingTip shares popup positioning with Flyout. ToolTip still needs a separate noninteractive timing contract.
ProgressBar and ProgressRing share task progress state.
InfoBadge and InfoBar share status presentation and accessible announcements.
RatingControl is a discrete range presentation, not a new general-purpose family.
CalendarDatePicker, CalendarView, DatePicker, and TimePicker share a date/time family with distinct editors.

Menus, a command palette, and toolbars share command records.
Shortcut keycaps are a text-style recipe within command surfaces, not an independent control family.
The searchable context menu adds an independent pin action. A pin action must not activate its command.
Shell `IContextMenu` integration is a Windows service, not another styled `MenuItem`.
Caption buttons, drag regions, and titlebar tabs require a Windows integration contract, not another `TabStrip`.
Search, batch rename, shell verbs, process metrics, filesystem I/O, thumbnails, network shares, and undo are application or platform services.
They do not each become a framework control.
Storage pickers, notifications, JumpList, ContentIsland, and multiple-window support are platform integrations.
Brushes, styles, resource dictionaries, and animation recipes are not independent controls.

## Individual backlog

Each checkbox in the three family sections identifies one family.
The separate extension, recipe, and platform section does not change the family count.
An extension remains pending even when XUI already implements part of that family.
**P1** means common desktop coverage. **P2** means the next reusable contract. **P3** identifies optional integration priority, not implementation status.

All family, extension, recipe, and platform entries require keyboard access and meaningful UIA roles/patterns.
They require disabled-state, dark/light/high-contrast, and 96/144/192-DPI checks.
They also require bounded retained state, no idle repaint loop, cancellation on hide/close, and resource stability after repeated use.
Virtual collections require stable identities across filtering and source replacement, with storage proportional to visible content rather than total item count.
Async providers require cancelable queries, stale-result rejection, and no UI mutation after the owning surface closes.
Those shared acceptance criteria apply in addition to the criteria on each line.

### Common desktop families

- [x] **control-exclusive-choice** — **P1**, exclusive choice. Add a radio group with one selected value, arrow navigation, and UIA selection semantics. Support vertical popup choices without activation of adjacent range editors. WinUI: RadioButton. File Pilot: choices in [FS] and view modes in [S4].
- [x] **control-selection-picker** — **P1**, selection picker. Add a ComboBox with stable selected IDs, keyboard type-ahead, popup dismissal, and optional editing. Separate preview from committed selection and restore focus on cancel. WinUI: ComboBox [G]. File Pilot: [S2]–[S4] show richer compositions, not proof of a standard ComboBox.
- [x] **control-tree-view** — **P1**, hierarchical collection. Add lazy expansion, stable node IDs, parent/child navigation, and bounded visible-node storage. WinUI: TreeView. File Pilot: documented in [F81].
- [x] **control-items-view** — **P1**, generic virtual collection. Extend shared selection to tiles, groups, multi-selection, range selection, and rectangular selection without per-item peers. Add grouped headers, icon/secondary-text slots, and select-all semantics with explicit filtered versus full-source scope. Preserve focused and selected IDs through filtering and group collapse. Reuse this selection model in DataGrid. WinUI: ListView/GridView/ItemsView/ItemsRepeater. File Pilot: [F81], [S1]–[S3], and [L1].
- [x] **control-popup-host** — **P1**, anchored content popup. Add placement, viewport clipping, focus return, light dismissal, and interactive content. Compose rich pickers from an input, virtual collection, navigation toolbar, and footer keycaps. Keep enumeration and navigation behind cancelable provider callbacks. Define child focus order, nested-popup dismissal, Escape handling, and stale-result rejection after close. WinUI: Popup/Flyout/TeachingTip. File Pilot: [FS] and [S2]–[S4].
- [x] **control-tooltip** — **P1**, tooltip. Add hover/focus help with delay, cancellation, accessible descriptions, and no focus theft. WinUI: ToolTip. File Pilot: timing documented in [F71].
- [x] **control-command-surfaces** — **P1**, command surfaces. Extend flat menus to nested menus, toolbar overflow, and searchable command palettes with shared command identity. Add item icons, labels, multiple shortcut hints, checked/disabled states, separators, and an independent pin action. Preserve command IDs and selection through filtering, with deterministic focus repair for missing commands. Add cancelable queries, submenu arrow navigation, Escape dismissal, and separate accessible pin/command actions. Keycap hints do not register shortcuts. Keep Shell command discovery outside the visual menu. WinUI: MenuBar/MenuFlyout/CommandBar/CommandBarFlyout. File Pilot: [F] and [S5].
- [x] **control-disclosure** — **P1**, collapsible content. Add an Expander with a header, expand/collapse semantics, focus repair, and hidden-content inactivity. Preserve group identity and selection without focus in hidden rows. WinUI: Expander. File Pilot: [F81], grouped sidebar in [L1], and quick access in [S3].
- [x] **control-breadcrumb** — **P1**, breadcrumb navigation. Add stable segments, overflow, keyboard traversal, and current-location semantics. Connect segments and the rich location picker through explicit navigation callbacks. Cancel must retain the current location and selection. WinUI: BreadcrumbBar. File Pilot: [FS], [S2], and [L1].
- [x] **control-progress** — **P1**, progress indicator. Add determinate/indeterminate state, accessible values, and paused/hidden animation suspension. Support a read-only capacity-meter recipe with used/total text and unknown/error states. Static capacity meters need no timer and must not imply an active task. WinUI: ProgressBar/ProgressRing. File Pilot: drive fill bars in [F81] and [L1].

### Next reusable contracts

- [x] **control-action-variants** — **P2**, action-family extensions. Add repeat, toggle-action, dropdown, and split activation without duplicate base controls. Give secondary actions separate keyboard targets and accessible names without activation of the primary command. WinUI: RepeatButton/ToggleButton/DropDownButton/SplitButton/ToggleSplitButton. File Pilot: icon actions in [FS] and an independent pin action in [S5]. File Pilot repeat timing remains unverified.
- [x] **control-numeric-input** — **P2**, numeric editor. Add locale-aware parsing, finite bounds, step actions, invalid-input feedback, and UIA value semantics. WinUI: NumberBox [G]. File Pilot: numeric customization is not verified.
- [x] **control-range-input** — **P2**, range editor. Add bounded values, steps, arrow/Page/Home/End keys, and UIA RangeValue. Support horizontal and vertical orientation, with explicit direction and preview/commit callbacks. Define cancellation during pointer capture and popup dismissal. Rating is a discrete presentation. WinUI: Slider/RatingControl [G]. File Pilot: vertical size slider in [S4]. Its bounds, steps, and live behavior remain unverified.
- [x] **control-content-dialog** — **P2**, custom content dialog. Add modal content, validation, initial/default/cancel actions, focus trapping, and focus return. Native confirmation remains separate. WinUI: ContentDialog [G]. File Pilot: live dialog behavior unverified.
- [x] **control-inline-status** — **P2**, inline status. Compose labels and actions with severity, dismissal, accessible announcements, and noninterruptive badges. WinUI: InfoBar/InfoBadge [G]. File Pilot: recent-file indicator documented in [F81].
- [x] **control-adaptive-layout** — **P2**, adaptive two-dimensional layout. Add grid/wrap sizing and breakpoint recipes for navigation and list/details without per-size control duplication. Preserve selection and focus across inline and overlay navigation layouts. Compose grouped quick access from shared collections and disclosure, with optional icon, secondary-text, and capacity-meter slots. WinUI: Grid/RelativePanel/VariableSizedWrapGrid/NavigationView [G]. File Pilot: [F], [S1], [S3], and [L1].
- [x] **control-multiline-text** — **P2**, multiline plain text. Add editable/read-only modes, selection, scrolling, IME/TSF behavior, and bounded documents. WinUI: multiline TextBox. File Pilot: text Inspector documented in [F]. This is not a single-line style.

### Optional families outside the first minimal set

- [x] **control-password-input** — **P3**, password input. Define masking, secure value exposure, reveal policy, and clipboard/automation restrictions before implementation. WinUI: PasswordBox [G]. No File Pilot requirement established.
- [x] **control-rich-text** — **P3**, rich text. Define document runs, links, selection, editing, and document limits. WinUI: RichTextBlock/RichEditBox [G]. Plain Inspector text does not establish rich editing.
- [x] **control-date-time** — **P3**, date/time selection. Define calendar, date, and time editors with locale and invalid-range behavior. WinUI: four date/time controls [G]. File dates are data, not proof of a picker.
- [x] **control-color-picker** — **P3**, color selection. Define color space, alpha, keyboard entry, and accessible values. WinUI: ColorPicker [G]. Theme customization alone does not prove a spectrum picker.
- [x] **control-media-playback** — **P3**, media host. `MediaPlayback` uses lazy Windows Media Foundation for explicit local audio and video. It supplies transport, seek, volume, status, and hidden/closed release. Camera capture is outside this API. WinUI comparison: MediaPlayerElement/CaptureElementPreview [G].
- [x] **control-web-content** — **P3**, web host. Optional `WebContent` embeds WebView2 with owned HTML, explicit HTTPS origins, script results, runtime accessibility, and guarded asynchronous lifetime. It does not install a runtime. WinUI comparison: WebView2 [G].
- [x] **control-map-view** — **P3**, offline coordinate map. `MapView` supplies Mercator projection, graticule, authored overlays, stable markers, pan, zoom, and cancelable provider requests. It has no street basemap or tile-loading service. MapControl is present in [G].
- [x] **control-vector-canvas** — **P3**, public vector scene. `VectorCanvas` supplies immutable paths, shapes, affine transforms, canvas-space clips, hit testing, and a virtual semantic list. It shares the root renderer without public drawing-context access. WinUI comparison: Shape/Line/Canvas/Viewbox [G].

There are **25 completed family entries and no pending family entries**.
Navigation recipes, switch styling, person pictures, pagination dots, and animated icons do not add separate family checkboxes.
Touch swipe, pull-to-refresh, semantic zoom, and motion effects remain optional interaction extensions to their owning families.
InkCanvas is not part of the stable WinUI 3 baseline in [W]. It is not silently counted as an existing XUI gap.
GroupBox and StatusBar are not catalog entries in the checked [G]. This inventory does not invent them.

### Scene and native-host delivery limits

The September 13 host batch implements the four remaining optional families.
`build\controls\hosts-delivery.json` records its APIs, dependencies, runtime version, commands, captures, and integration limits.
The four new gallery pages are `vector-canvas`, `map`, `media-playback`, and `web-content`.
Page creation does not load codecs or create a browser environment.
Media and browser loads require explicit commands.

Vector scenes support at most 4,096 shapes, 65,536 vertices, and 256 interactive elements.
Clips use canvas coordinates after affine transformation. Stroke width remains in DIPs.
The semantic list exposes stable, single-selection UIA children without per-shape HWNDs.
The renderer caches at most eight scene snapshots and uses the existing root target.

The offline map uses an authored graticule, markers, and polylines.
It wraps longitude, clamps Mercator latitude, and supports mouse and keyboard pan/zoom.
No geographic dataset, commercial tile service, or network worker is bundled.
Provider completion uses owner-specific cancellation tokens and generation checks.
The application owns provider work and UI-thread completion.

Media Foundation plays generated, repository-authored WAV and AVI fixtures.
Owned-window Graphics Capture records actual video pixels.
Transport actions, duration, seek, volume, hidden release, and callback disposal receive native coverage.
Windows owns codec resources. Noninterruptible codec calls are not subject to a hard XUI time or memory limit.
Camera capture, DRM, streaming URLs, subtitles, and recording are not implemented.

The optional WebView2 SDK is `Microsoft.Web.WebView2` version `1.0.2903.40`.
The verified ARM64 runtime is `153.0.4234.32`.
Native tests render owned HTML and query the actual DOM.
The default build has no browser dependency and reports a visible error after an unsupported web load.
Navigation policy, permission denial, blocked new windows/downloads, bounded script callbacks, and stale-environment rejection belong to the host boundary.
Browser process allocations and locked profile files remain runtime-owned until shutdown.

Active media and web surfaces reject retained XUI popups.
They do not claim `WM_PRINT` composition support. Scroll clipping uses native parent windows.
Hiding unloads the runtime. A later show requires another explicit load.
The existing EDIT and RichEdit root-composition path remains unchanged.
Native tests cover theme/DPI combinations, root target recreation, idle paints, virtual selection, and retained providers after disposal.
Physical monitor changes and screen-reader speech remain manual checks.

### Foundation delivery limits

The foundation families have retained C++ APIs in `xui/foundation.hpp`, shared Windows input/rendering, UIA patterns, gallery pages, and automated tests.
`Control` supplies tooltip help. `Button` supplies action modes. `SplitButton` composes two independent action targets.
The gallery contains 47 pages. Later examples build on first use.
Core tests check invalid input, stable identity, locale parsing, callback behavior, and silent setters.
Native tests check dark/light/high contrast, injected 96/144/192 DPI, nested popup cycles, focus repair, capture cancellation, native clipping, and UIA.
Physical monitor transitions, interactive IME sessions, and screen-reader speech remain manual checks.

Popup placement is client-bound, not a separate top-level window.
Open content uses ordinary child input/UIA peers and the existing root render target.
Dismissal releases those peers after input dispatch. The maximum nested depth is eight.
Applications cancel their own provider queries and use popup generations to reject late results.
Choice controls have a 4,096-item bound. `ItemsView` supplies the generic virtual collection.
Editable ComboBox text remains separate from its selected ID.
Invalid numeric text remains visible without replacing the last valid value.
Indeterminate progress is static; progress rings and animated progress remain optional presentations, not delivered animation features.
No new C ABI, C#, or Rust bindings are included in this batch.
See the [foundation reference](../specs/foundation-controls.md) for contracts.
Use [CONTRIBUTING](../../CONTRIBUTING.md) for build and test commands.

### Collection delivery limits

`xui/collections.hpp` supplies immutable virtual indexes, shared selection, `ItemsView`, and `TreeView`.
`xui/adaptive_layout.hpp` supplies track-based `Grid`, measured `Wrap`, and retained `AdaptiveLayout`.
`DataGrid` reuses the selection model and adds filter requests plus selection check columns.
The four gallery pages use synthetic sources. They do not enumerate files or allocate 100,000 row objects.

Ranges, rectangles, and select-all retain compact index terms.
The selection limit is 4,096 terms. Replacement gestures release previous terms.
Sources must preserve stable IDs and provide nonblocking identity lookup.
Large summaries use a containment proof or the explicit full-source contract.
Without that proof, a selection from another snapshot has a conservative mixed summary.
Individual selected IDs remain exact.

Trees retain at most 4,096 branch records and permit 128 expansion levels.
Pending requests expose error, cancellation, and stale-result boundaries.
Applications own worker execution and UI-thread delivery.
The tree keeps cached child indexes across collapse. Source replacement releases those indexes.
Tree selection survives collapse, while focus returns to the collapsed ancestor.

UIA collection roots expose Selection, Scroll, and ItemContainer.
Virtual items expose SelectionItem, ScrollItem, VirtualizedItem, and appropriate Invoke or ExpandCollapse patterns.
Fragment enumeration covers visible rows and necessary tree ancestors.
Selected arrays have a 256-identity limit. Larger or uncounted results fail explicitly instead of allocating a huge provider array.
Providers have no retained row cache. External clients own the providers that they request.

Adaptive navigation supports inline, stacked, and nonmodal overlay arrangements of the same retained controls.
The overlay shares the root target and masks underlying native fields.
It remains client-bound. `LocationPicker` and the navigation gallery add the anchored navigation recipe.
Grid check columns represent row selection, not independent editable Boolean fields.
Header sort, filter, and check actions have separate keyboard and UIA contracts.

Core tests cover million-row indexes, compact selections, source replacement, group collapse, lazy trees, filters, and size constraints.
Native tests cover Win32 input, isolated UIA clients, theme/DPI cases, popup and tooltip compatibility, repeated resource cycles, and target recovery.
Physical mixed-monitor behavior and screen-reader speech remain manual checks.
File Pilot behavior beyond the documented and supplied evidence remains unverified.
C ABI, C#, and Rust bindings remain outside this batch.
`build\controls\collections-delivery.json` records commands, results, captures, and integration notes.

### Command and navigation delivery limits

The document batch resolves the native-child occlusion gate with an independent owned-window compositor capture.

`xui/commands.hpp` supplies shared command snapshots, explicit bindings, virtual menus, searchable palettes, and toolbar overflow.
Nested menus publish their expanded state. Disabled parents disable their descendants.
The pin action has a separate callback and UIA Button.
Source replacement and completion revoke query tokens. Pending queries keep existing rows visible.
The old flat native menu remains available.

`xui/navigation.hpp` supplies breadcrumbs, grouped navigation, location pickers, and view pickers.
The explorer uses breadcrumbs, a command palette, and cached path locations.
Its native asynchronous address suggestions remain separate.
The gallery view picker changes the actual `ItemsView` presentation and item size.
It does not change the explorer's legacy `FileList` view mode.

The optional Shell service owns its COM interfaces and native menu on one STA thread.
Snapshots preserve identities, labels, checks, enabled states, icon metadata, and shortcut hints.
Native fallback preserves extension-owned images and dynamic submenu messages.
Discovery never invokes a verb. The real Shell test only discovers commands for its owned empty folder.
Cancellation revokes XUI actions, but cannot interrupt third-party COM calls.
HRESULT failures are contained at the service boundary. Native extension crashes are not isolated in another process.

The optional caption uses the existing TabStrip, native hit tests, and Windows system commands.
Tests exercise resize corners, system-menu open/cancel, caption UIA, maximize/restore, and injected DPI changes.
Physical monitor transitions, Windows Snap flyout appearance, and screen-reader speech remain manual checks.
The standard caption remains the default. The gallery opts in.

Native tests cover separate UIA clients, stale providers, callback exceptions, host closure, and public-window deletion.
They check native field region masks and both native print messages.
Full-window `PrintWindow` images can still include underlying native child pixels over the fixture popup.
Those fixture pictures are not proof of live compositor occlusion.
The document test finds no underlying ink in four root presentations immediately after `EndDraw`.
Windows Graphics Capture selects the owned test HWND when foreground activation is blocked.
It never selects a monitor or the desktop.
The underlying native field retains working selection and editing after popup dismissal.
The differing `PrintWindow` picture is a capture artifact. The existing native print-mask assertions remain intact.
No memory optimization or new language bindings are included in this batch.
`build\controls\navigation-delivery.json` records the verification scope, results, captures, and remaining manual checks.

### Document delivery limits

`xui/documents.hpp` supplies seven retained families.
`ContentDialog` reuses `Popup`, disables owner input, traps focus, preserves validation errors, and restores focus on dismissal.
Its UIA Window pattern reports modal state and supports Close.
Validation can close or reopen the dialog. Generation checks reject an obsolete result.
Native confirmation remains separate.

`InlineStatus` has severity symbols, an independent action, dismissal, and UIA live-region semantics.
Its message limit is 4,096 UTF-16 code units.
The badge recipe has no action or dismiss button.

`MultilineText` and `RichText` use Windows RichEdit from `Msftedit.dll`.
Their default document limit is 65,536 UTF-16 code units. The hard limit is 1,048,576.
They normalize paragraph separators, reject malformed property text, and retain at most 16 native undo actions.
Windows owns composition, selection, clipboard operations, and scrolling.
Native text updates occur by revision, not per paint.
RichEdit supplies its own Text provider and native numeric automation ID.
The XUI name reaches that provider, but the custom automation-ID override does not.

Rich text accepts at most 4,096 authored runs.
Bold, italic, underline, and HTTP/HTTPS links have actual native formatting.
Link callbacks require a click or Ctrl+Enter. XUI does not open a browser.
Plain Unicode streaming keeps RTF-looking input literal. OLE insertion and rich clipboard imports are blocked.
There is no RTF, HTML, image, or file importer.
The run model is not a serializer for arbitrary native rich-formatting commands.

`PasswordInput` always keeps a masked native EDIT, including during an explicit visual reveal.
Reveal uses a separate noninteractive preview. UIA never receives its plaintext.
Copy, cut, and the native context menu are disabled.
Change callbacks carry no password. `with_password` is the explicit application read boundary.
The default limit is 256 code units. The maximum is 4,096.
XUI erases its old retained value. Native and application memory are not a credential vault.

`DateTimePicker` shares one API across native date, time, and calendar presentations.
Windows owns the locale format and calendar popup. Native colors follow Windows rather than the custom dark theme.
Values use local Gregorian fields for years 1601–9999. Invalid values and ranges throw.
`ColorPicker` has four labeled byte editors, alpha preview, and at most 16 reusable swatch buttons.
Its color space is unpremultiplied sRGB. Fractional channel edits round to the nearest byte.

The seven gallery pages have real event output and native interaction tests.
Tests cover external UIA clients, injected 96/144/192 DPI, three themes, validation, owner isolation, native undo, selection, and target recovery.
The owned compositor probe preserves the single-root-frame design.
Physical monitor transitions, interactive IME candidate windows, and screen-reader speech still require manual checks.
This batch adds no language bindings or memory-optimization claim.
`build\controls\documents-delivery.json` records APIs, bounds, test commands, captures, and integration notes.

### Existing-control extension, recipe, and platform backlog

These four tasks do not add control families.
The DataGrid task extends an existing column contract. Its row selection model belongs to `control-items-view`.
The navigation task implements a composition under `control-adaptive-layout`.
The two Windows tasks define platform boundaries, not portable controls.

- [x] **control-data-grid-extensions** — **P2**, existing-control extension. Extend DataGrid with header filter actions, check columns, and select-all/multi-selection through the shared collection model. Preserve source column identity through resize/reorder and stable row identity through filtering. Define filtered/full-source select-all scope, mixed header state, and separate sort/filter/check keyboard actions. Expose header and cell UIA patterns without one retained peer per row. Cancel stale filter queries. Preserve existing sorting, resize, and reorder behavior. File Pilot: checkbox and filter affordances in [L1]. Their exact interaction remains unverified.
- [x] **recipe-navigation-pane** — **P2**, composition recipe. Compose grouped navigation and quick access from virtual selection, disclosure, icon/secondary-text slots, and optional read-only capacity meters. Support inline and anchored-popup layouts through shared item IDs and explicit navigation callbacks. Preserve focus and selection across filtering, collapse, dismissal, and layout changes. Cancel provider queries on hide and ignore late results. Bound visible rows and expose group/selection semantics through UIA. File Pilot: [S1], [S3], and [L1].
- [x] **platform-shell-context-commands** — **P2**, Windows integration. Define an optional Shell command provider, separate from styled app menus and filesystem navigation. Specify `IContextMenu` ownership, COM apartment rules, command identity, verb mapping, and message forwarding for extension submenus. Preserve icons, labels, enabled/checked states, and multiple shortcut hints where available. Define cancellation, stale-result rejection, and extension failure isolation without an idle worker loop. Discovery must not execute verbs. Activation must dispatch only the explicit selected command, never a pin action. Require keyboard/UIA checks at the visual menu boundary and synthetic provider tests before real Shell integration. File Pilot: Shell context-menu presentation in [S5], not proof of its internal provider implementation.
- [x] **platform-custom-titlebar** — **P3**, Windows integration. Define caption buttons, drag regions, and tab placement around the existing TabStrip. Preserve system menu access, resize behavior, keyboard navigation, accessible caption names, and DPI transitions. Require hit-test separation between tab actions, caption actions, and window dragging. Reuse the tab selection contract rather than create another tab family. File Pilot: titlebar/tab presentation in [L1]. Native integration behavior remains unverified.

## Follow-up implementation order

1. Implement shared selection, command identity, and the popup host.
2. Add exclusive choice, vertical range input, disclosure, and command-menu extensions.
3. Extend the existing DataGrid column contract.
4. Compose rich folder/view pickers and grouped navigation from those families.
5. Add the optional Shell provider after the visual menu contract has acceptance coverage.
6. Keep the optional custom caption off by default. Keep browser runtime integration opt-in.

These stages now have implementations and acceptance tests within the delivery limits above.
The remaining family checkboxes are still open.

## Gallery use

Run `build\controls\Release\xui_gallery.exe`.
Use the left catalog to select an example.
Use `Ctrl+F` to expand the navigation pane and focus search.
Type a control name or category.
Press Enter to focus the result list.
Use arrow keys to select a result.
Press Enter to focus its example.
Use Previous example or Next example to move through the filtered results.
Use Copy code to copy the current C++ excerpt.

Each page uses real public XUI controls and includes its purpose, API excerpt, and event output.
The gallery has 47 pages across eight categories.
They cover input, layout, collections, navigation, media, commands, appearance, and documents.
The catalog uses the reusable C++ `NavigationView` with nonselectable category groups and stable example IDs.
Home and Appearance remain pinned while search filters the main section.
Search preserves selection identity. The page area displays a matching example or an empty state.
The final `navigation-view` page demonstrates nested groups, pane collapse, filtering, icons, badges, and a disabled item.
This addition does not expand the C ABI or language bindings.
The grid calculates 100,000 rows on demand. Its source stores no row array.
File-list fixtures contain 200 synthetic names and empty paths. They do not enumerate a user directory.
The image page loads only a path that the user supplies.
The chart has deterministic data and no timer.
The suggestion page uses four in-memory names and performs no filesystem access.

Examples:

```powershell
.\build\gallery\Release\xui_gallery.exe --page grid
.\build\gallery\Release\xui_gallery.exe --page menus --light
.\build\gallery\Release\xui_gallery.exe --page images --image "D:\images\sample.png"
.\build\gallery\Release\xui_gallery.exe --page themes --high-contrast
.\build\gallery\Release\xui_gallery.exe --page navigation-view
```

The image argument fills the field. The Load image action starts decoding.
Right-click Context menu target to open its real styled menu.
Use `Shift+F10` for the keyboard menu.
F6 cycles themes outside data grids. Inside a data grid, F6 selects the header.
The grid supports header drag, boundary drag, `Ctrl+Left/Right` resize, and `Ctrl+Shift+Left/Right` reorder.
The split example needs at least 610 DIPs of content width for two panes.

The gallery uses retained `PageView` pages, not one window or worker per page.
The API excerpts are immutable labels. XUI does not yet have a read-only multiline code editor.
The gallery is a control explorer, not a copy of WinUI Gallery assets or an implementation of the pending families.

## Validation snapshot

The September 12 ARM64 Release run passed all 26 native tests.
The five binding clients passed their normal and callback-failure runs against the rebuilt DLL.
C# framework-dependent and AOT wrapper tests each passed 17 assertions.
The native clipboard test uses a private window station and leaves the interactive clipboard unchanged.
The gallery Copy code callback was not separately automated against the interactive clipboard.

The gallery tests cover category search, empty results, stable selection, arrow/Enter navigation, Ctrl+F, button events, and enabled-state changes.
They also cover real image decoding, menu checked/disabled semantics, 100,000 grid rows, and 51 page switches.
The peer count stays fixed. GDI and USER counts pass the repeated-navigation bounds.
Inactive pages do not repeat measurement during active-page layout.
The two-second idle sample recorded zero custom paints and zero measured process CPU milliseconds.
The post-navigation idle check also recorded zero custom paints.

The first full run encountered an existing Shell fixture write error (`Win32 error 5`).
The unchanged Shell test passed its retry and the final full run.
No fixture assertion was weakened.

Window-only captures received visual inspection for forms, the grid, the open menu, narrow layout, light mode, and explicit high contrast.
The 96/144/192-DPI captures use synthetic `WM_DPICHANGED` messages.
They do not replace physical mixed-monitor or screen-reader speech checks.
Native EDIT exposes ValuePattern in this environment. The binding probes report no native TextPattern.

The same-size memory comparison uses three runs per executable and a 1040 × 700 physical client area.
Both executables use the same backend source, except for the active-page layout optimization.
No working-set trim occurs.

| Measurement | Previous form gallery | Searchable gallery |
| --- | ---: | ---: |
| Executable bytes | 570,368 | 772,608 |
| Median private commit bytes | 99,168,256 | 99,278,848 |
| Median private working-set bytes | 84,107,264 | 84,287,488 |
| GDI objects | 29 | 43 |
| USER objects | 47 | 321 |
| Render targets | 1 | 1 |
| Threads | 7 | 7 |

The gallery adds 202,240 executable bytes and retains more native peers for its bounded page set.
The sampled median memory differences are small, not proof of zero memory cost.
These process totals include graphics and driver allocations, not only retained XUI controls.
The previous executable starts at 680 × 740 before the common resize. The new executable starts at 1040 × 700.
The comparison uses the common final geometry, not the unequal initial samples.
Startup history and allocator noise limit precise attribution.

Artifacts:

- `build\gallery\control-backlog.json`: the initial 25-family backlog snapshot, before the live inventory refinement.
- `build\filepilot-review\inventory-delta.json`: updated family descriptions and four separate extension/recipe/platform tasks.
- `build\filepilot-review\L1-main.png`: private live main-window evidence.
- `build\filepilot-review\L2-after-focus-attempt.png`: private unchanged-state evidence after blocked focus attempts.
- `build\gallery\winui-source.json` and `winui-catalog.json`: pinned official catalog evidence.
- `build\gallery\native-final-tests.log`: final native test results.
- `build\gallery\*-normal.log` and `*-failure.log`: five binding client results.
- `build\gallery\dotnet-tests.log` and `aot-tests.log`: wrapper results.
- `build\gallery\memory-comparison.json`: same-size medians and resource counts.
- `build\gallery\baseline-memory-final.json` and `gallery-memory-final.json`: complete measurement records.
- `build\gallery\gallery-captures\*.png`: owned gallery captures.
