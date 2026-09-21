# Explorer validation history

This archive preserves earlier implementation reports and measurements from the former root README.
Results, limitations, tool paths, and artifact paths describe those runs, not the current checkout.
Local `build` artifacts are not part of the repository and can be absent.
Use [CONTRIBUTING](../../CONTRIBUTING.md) for current build instructions.

## Settings popup opening, 2026-09-21

The ARM64 Release settings popup created native children for every hidden page.
Stage timing attributed almost all opening time to `Popup.Show`, not managed synchronization or search.
Each dismissal released those children, so reopening repeated the cost.
The native fix deferred children of closed, settled reveals without caching dismissed popups.

The same managed `--settings-open-smoke` fixture ran with the old and new native DLLs.
Each run used two panes, one first opening, and seven repeated openings.

| Measurement | Before | After |
|---|---:|---:|
| First opening handler | 2253.54 ms | 155.68 ms |
| Repeated opening median | 2171.62 ms | 160.93 ms |
| First opening with native paint | 2280.65 ms | 165.83 ms |
| Repeated opening median with native paint | 2183.36 ms | 173.60 ms |
| Added native child windows | 1364 | 116 |

The final fixture also visited every page and opened global search across all settings.
Reopening General then took 180.02 ms with native painting and added 116 native child windows.
Draft retention, deferred native text input, and saved keyboard edits passed.
The customization, address, partition, views, and context-action desktop checks also passed.
The settings scroll check recorded 4.65 ms for median dispatch and 14.38 ms with native painting.
The ContentHost suite passed, including 200 replacements and native editor state.
The complete reveal suite passed, including deferred peers, native undo state, ownership, popup cleanup, and hidden content replacement.
Popup fixtures preserved the foreground owner and explicitly requested local editor focus when Windows did not activate their window.
The foundation suite reached its existing high-contrast ring assertion after six successful popup cleanup runs.
The same assertion failed with the two new deferral guards disabled.
The assertion expected the Windows animation preference alone, while the existing runtime also disabled motion in high contrast.
The fixture flushed pending native painting, but did not measure compositor presentation or physical display latency.
These are local measurements, not performance guarantees for other computers.

Artifacts: `build\settings-open-comparison.out`, `build\settings-open-final.out`, and `build\settings-open-native-final-build.log`.
The matching executable was `build\settings-open-validation\FileExplorer.exe`.

## Immediate switches and compact Tree rows, 2026-09-18

User feedback removed the incoming animation for file-view changes.
`FilePaneLayout.xui` now places the file controls directly in a retained Grid.
`FilePaneView` no longer starts or settles an entry Reveal.
The separate Find, sidebar, and split-pane transitions remain unchanged.
`--view-switch-smoke` checks all view choices for stationary native peers, retained state, and no animation timer.
The old `--view-entry-smoke` flag runs the same check.

`ExplorerStyles.FileTree` sets 24-DIP rows, 12-DIP text, 16-DIP icons, and 16-DIP indentation.
`FileRows.Columns` supplies the shared Name, Date modified, Type, and Size definitions for Details and Tree.
Tree metadata uses the same immutable source callbacks for roots and loaded descendants.
The compact-view smoke checks selection through metadata cells on root and nested rows.

The ARM64 Release build passed without warnings.
Both `--view-switch-smoke` and `--views-smoke` passed.
The switch fixture uses a 128-DIP offset because the medium gallery clamps larger offsets with its short data source.
Model checks passed 78,527 assertions, and documentation adapter checks passed 24 tests.
Native checks covered collections, rendering, UI Automation, image lifetimes, and nine window configurations.
Managed feature checks passed 403 assertions, and the Rust check passed.

The updated executable is `build\compact-tree-explorer\FileExplorer.exe`.
The separate output directory avoids the locked DLL in the running application.
The existing application process remains untouched.

## Additional file views, 2026-09-18

`FilePaneView` selects the active DataGrid, ItemsView, TreeView, or MillerColumns control.
The active control supplies file focus, selection, context menus, preview targets, and Find navigation.
`ViewMenuLayout.xui` lists XL Icons, L Icons, M Icons, List, Tree, Details, and Columns in that order.
All icon choices share the opt-in `ItemsPresentation.Gallery` renderer.
The existing tile presentation remains unchanged.

`FileTreeView` owns child requests, immutable snapshots, descendant lookup, and selection restoration.
Directory keys use version 1. File keys use version 0.
The root source uses this immutable distinction to answer `HasChildren` for descendant keys without filesystem access or mutable callback state.
View changes cancel the managed work and dispose its native request tokens.
Native status 11 rejects a result after branch collapse. Other binding errors retain their normal error path.

The model suite covers every view choice through refresh, transitions, duplication, and transfer.
The focused `--views-smoke` fixture covers the production menu and controls.
The complete `--smoke` run includes the same fixture.

The ARM64 Release build, `--views-smoke`, and `--view-entry-smoke` passed on 2026-09-18.
The model suite, managed feature tests, Rust check, and documentation adapter tests passed.
Native collection, style, ABI, image, and collection-window tests passed, including gallery pixels and accessibility.
The complete explorer smoke stopped at the existing palette Escape focus assertion.
An archive of commit `bce8588bb8174a291c29fe9da430db853b2d5988`, with its own unchanged native build, failed at the same assertion.
This comparison does not identify the cause of that existing failure.

## Breadcrumb address composition, 2026-09-18

`BreadcrumbAddressBar.cs` replaces the C# pane's address button with a demo-local composition.
`FilePaneLayout.xui` accepts its root as an element parameter.
`Models/BreadcrumbPath.cs` separates drive, UNC, and extended roots from their descendant components without filesystem access.
The shared native `Breadcrumb` contract remains unchanged.

Each path segment has independent name and subfolder buttons.
Nested `AdaptiveLayout` elements hide earlier segments as available width decreases.
Their content-sized mode measures both children for each breakpoint and navigation extent.
The original character-count estimate truncated `dev` despite sufficient space.
Each segment now uses a horizontal stack with measured name and chevron widths.
The ancestor menu retains the complete path, including components outside the 64-pair retained limit.
One `ContentHost` owns each generation of segment controls.
Button actions use application dispatch because a content-scoped callback cannot replace its own content.
The generation check rejects queued actions from retired segments.

The dropdown and its source belong to the window, not the replaceable path content.
The dropdown uses the stable path presentation as its anchor because popup and anchor handles must share an ownership scope.
`UiWork` and a request cancellation token reject late directory results.
`FilePaneView` retains responsibility for navigation commits, errors, history, and tab state.

The flyout list opts into `ItemsView.SetSingleClickActivation(true)`.
Pointer activation remains separate from selection, so arrow keys and initial selection do not navigate.
The native handler rejects activation if a selection callback replaces the source or hides the control.

The current name has measured width and two DIPs of horizontal padding on each side.
The remaining space contains a separate named button with a zero-size icon.
This keeps the empty area accessible without a visible label or glyph.
The current name, trailing space, Ctrl+L, Alt+D, and Ctrl+G open the existing navigation palette.
The initial inline editor was removed after maintainer feedback.
`ButtonIcon::chevron_right` appends ABI value 29 and uses the existing Fluent symbol or two Classic strokes.

The ARM64 Release build, model suite, and focused `--address-smoke` passed.
The refined address smoke checks native mouse activation of trailing space and stable current-name width after resize.
The focused pane-animation smoke also passed.
Native control, image, Fluent lifecycle, and software-rendering checks passed.
The pixel checks cover right chevrons and the blank trailing button in Classic and WinUI.
The focused Rust icon test and the managed feature assertions passed.

The full native window run stopped at the unchanged page composition-buffer growth assertion.
The full managed runner stopped at the style-cleanup error-message assertion.
Unmodified `HEAD` managed sources reproduced the style-cleanup assertion with the same native runtime.
The complete `--smoke` run stopped at the palette's native Escape focus-restoration assertion.
An exported, unmodified `HEAD` demo reproduced that assertion with the same native runtime.
This run did not include File Pilot pixel comparison or physical IME interaction.

## Navigation filter appearance, 2026-09-18

`ExplorerStyles.NavigationFilter` gives the retained search editor a surface that matches the WinUI window background, with only a thin bottom border.
`NavigationSidebar` applies this style only to `View.Search`.
The shared `TextInput` renderer retains native editing, focus feedback, and high-contrast defaults.
The ARM64 Release build and `FileExplorer.exe --smoke` passed in this checkout.
The smoke checks the attached style, theme colors, border thickness, and corner radius, alongside existing navigation and input checks.
This run did not include screenshot comparison.

## Folder identity and Find behavior, 2026-09-16

Tabs previously supported text only.
`TabItem` now accepts a vector icon and an image path.
The C ABI exposes `xui_tab_items_visual`. The managed binding exposes `TabEntry` and `TabStrip.SetTabItems`.
FileExplorer supplies the folder path and a folder glyph for each tab.
The shared image worker resolves Shell icons without blocking the UI thread.
Existing text-only tab calls retain their behavior.

The navigation sidebar uses the same command builder and Shell menus as Details rows.
The menu captures the clicked path without changing the pane's current folder.
`NavigationList::prepare_context_menu` changes row focus without selecting a navigation destination.
Headers, disabled rows, and empty space do not open a menu.
The managed binding covers the main, header, and footer lists.

The complete explorer smoke passed with native pointer and keyboard menu requests.
It covers shared command availability, clicked-path snapshots, bookmarks, and opening the folder in a new tab.
The native navigation model suite passed its pointer, keyboard, header, disabled-row, and pinned-section assertions.

The integrated native build, explorer model tests, feature ABI tests, and complete managed binding suite passed.
The complete explorer smoke also passed with tab icons and sidebar menus enabled.
The image suite passed decoding, replacement, cancellation, cache limits, and repeated idle reconciliation.
The tab-window suite passed icon pixels, icon clicks, close targets, keyboard focus, and overflow checks.
It covered both visual styles, all three themes, and five DPI settings from 96 through 192.
Separate navigation-window reruns failed at the existing idle-repaint assertion or real Shell discovery.
These failures also occurred with the binary from before the tab-icon change.

`ExplorerApplication.UpdateTitle` derives the native caption from the active pane's committed path.
`ExplorerTab.Apply` clears the filter only when the normalized folder path changes.
This common commit path covers direct navigation, history, column drilling, and ancestor-column selection.
Refresh and failed navigation retain the filter.

`Window::Impl::translate` identifies text-producing keys before native translation.
The application opens Find through `UiKeyEvent.IsTextInput` and returns false.
The host then directs the original key to the newly focused native editor.
The host does not convert virtual keys into query text or consume dead-key state.
The explorer smoke covers the first and subsequent keys in Details and Columns, native non-ASCII input, and navigation keys.
Physical keyboard layouts and IME candidate-window interaction still need manual coverage.

`src\window_icon.hpp` owns the Shell image request and both native window icons.
`NavigationSidebar.cs` requests metadata only after the native row-hover delay.
`FolderMetadata.cs` supplies cancellable recursive totals and explicit partial-result descriptions.
The shared navigation control owns row tracking and card placement.

The x64 Release library and `win-x64` explorer build passed.
The complete explorer `--smoke` passed with caption, icon, typing, and folder-filter assertions.
The managed model suite passed 360 assertions. The native text binding suite passed 31 assertions.
The native navigation model, navigation window, feature ABI, and window-icon suites passed.
The icon suite covers Shell decoding, cancellation, replacement, errors, and handle cleanup.
The navigation window suite covers the hover delay, row-relative placement, multiline content, and input focus.

## Managed file transfers, 2026-09-15

The managed explorer now connects file clipboard commands and pane drops through `FileTransfers.cs`.
`FileContextMenu.cs` retains all selected paths when a menu opens.
`FilePaneView.cs` supplies visible selection membership and rejects obsolete rows during transfers.
The native implementation uses `src\file_transfer.cpp` and `src\c_api_file_transfer.inc`.
The public interfaces are in `include\xui\file_transfer.hpp`, `include\xui\xui_file_transfer.h`, and the managed `FileTransfers.cs` binding.

The local Release build used `build\file-transfer` with the x64 toolchain.
The managed explorer built for `win-x64` against that DLL.
Its complete `--smoke` passed, including real folder copy, file move, pane refresh, and native text shortcut handling.
The model program passed 200 assertions.
The native transfer, control, collection, and ABI tests passed.
The transfer program covered clipboard serialization, the OLE drop protocol, grid gestures, and Shell operations.

The clipboard round-trip fixture uses a private window station and desktop.
The isolated clipboard suite passed three consecutive runs without a change to the interactive clipboard sequence.
The native integration suite also passed, including native text editing.
An earlier clipboard-snapshot fixture skipped unsupported storage. The isolated fixture replaced that approach.
The explorer smoke does not replace the desktop clipboard.
Manual coverage still includes external application interoperability, Shell conflict choices, physical drag feedback, and cross-volume transfers.
An ARM64 app cannot load the x64 test DLL. The application and native DLL must use the same architecture.

The merge with the Columns-view changes retains separate native drag and horizontal-scroll handlers.
Clipboard commands use the active column selection and folder. File drag-and-drop remains a Details-view operation.
The combined explorer smoke passed, including ancestor-column targets, empty-column selection, and native Find clipboard keys.
The merged model suite passed 246 assertions.
The transfer, isolated clipboard, Miller model, Miller window, and feature ABI suites also passed.

## Shell alpha correction (2026-09-15)

The C# explorer uses the shared Shell decoder in `src/images.cpp`, not a thumbnail-size setting in its project file.
The decoder treated straight-alpha Shell HBITMAPs as premultiplied pixels.
This error made translucent icon edges too bright on dark backgrounds.
The correction uses `WICBitmapUseAlpha`. The existing format converter then produces premultiplied BGRA for Direct2D.

The local ARM64 build is `build\icon-quality`.
The new translucent-icon assertion failed before the correction.
The Shell, thumbnail, and image suites passed after the correction.
Coverage includes physical icon sizes from 20 through 48 pixels, legacy masks, and translucent pixels over dark, selected, and light rows.
Shell thumbnail pixels also match direct WIC output for an original translucent PNG.
The window regression also covers synthetic 150% and 200% DPI.
Physical mixed-monitor transitions and third-party Shell handlers still need manual coverage.

The C# explorer built with the corrected native DLL.
Its first smoke run timed out during palette history navigation. An unchanged retry passed.
The native test logs and both smoke logs remain in `build\icon-quality`.

## Compact header validation

The current validated executable is `build\header\Release\xui_demo.exe` (650,240 bytes).
This separate build leaves existing executables available to open applications.
The full ARM64 Release build and all 23 native CTest tests passed.
The explorer smoke passed 1,863 assertions, including title transitions, native address editing, and 18 width/DPI/theme combinations.
The matrix uses 924, 600, and 460 DIP outer widths, injected 96/144/192 DPI, long Unicode paths, and 16 tabs.
Both themes retain the global commands and independent pane tabs without overlap.
The frame regression retained zero missing glyph regions across 1,323 presentations, including a visible-to-hidden address caption transition.
All five existing binding clients passed normal and callback-failure UIA checks against the new binaries.
Both C# wrapper variants also passed their 17 assertions. The bindings used isolated copies under `build\header\bindings`.

Three fresh processes used the existing 60-file measurement procedure without a resize or memory trim.
The warm median private commit was **28.52 MiB**. The private working set was **14.12 MiB**.
The warm total working set was **44.85 MiB**. Input-and-paint latency was **17.29 ms**, with a 16.76–18.36 ms range.
The preceding complete-frame build recorded 30.29 MiB private commit, 16.00 MiB private working set, and 17.83 ms latency.
Those earlier results are historical measurements, not a simultaneous comparison.
The target count remained one. These results do not change the documented driver allocation threshold at larger window sizes.
Separate geometry probes measured a list height increase from **269 to 443 DIPs** at the unchanged default client size.
The **174-DIP gain** comes from the removed header rows and footer, not a smaller file row.

The full suite log is `build\header\ctest-final.log`. The final targeted frame rerun is `build\header\ctest-rerun.log`.
Memory and geometry samples are in `build\header\browser-memory.json` and `build\header\viewport-comparison.json`.
Owned-window captures are in `build\header\captures`, including single-pane, split-pane, narrow, dark, and light views.
Native EDIT exposes an editable `ValuePattern` on this machine. Its system `TextPattern` is unavailable for both address and search.
The compact field preserves that platform behavior rather than supplying a replacement text provider.
Physical mixed-monitor transitions and interactive IME candidate windows still need manual coverage.
IME composition suppresses application shortcuts until committed text reaches the control.

File activation calls `ShellExecuteExW` with the exact selected path and no command-line parameters.
Only an explicit row activation or Open command calls this dispatcher.
Windows associations can start executable files.
The sample does not copy, move, rename, or delete files.
The optional Shell menu can expose extension verbs, including Properties.
It uses an explicit native fallback. XUI does not reinterpret third-party owner-drawn menu items.
The sample has no drag-and-drop or tab reorder.

### Shell validation and measurements

The ARM64 Release executable is 687,104 bytes, compared with 650,240 bytes for the preserved header build.
All 24 native tests passed across the full suite and isolated retries.
Repeated full-suite runs had transient failures in desktop foreground, UIA, suggestion, and image-idle checks.
Each failed test passed in isolation, without changes to its focus safeguards.
The final full suite passed 23 tests. Its one suggestion-test failure passed on an isolated retry.
The Shell test also passed separately after additional queue, STA, and error-delivery assertions.
All five existing binding clients passed their normal UIA and callback-failure checks.
Both C# wrapper test executables passed 17 assertions with isolated copies of the new DLL.

`xui_shell_thumbnail_tests` uses a compiled, owned fixture EXE with original icon resources. It never runs that EXE.
Pixel assertions cover resource color, transparent margins, legacy icon masks, selection, filtering, scrolling, light theme, and 150%/200% synthetic DPI.
The native `C:\Windows\System32\cmd.exe` row matched all 388 opaque pixels from its extracted Shell image.
Text, PDF, DOCX, shortcut, DLL, folder, unknown-type, and missing-file requests also have coverage.
A PNG request through the private Shell path exercises the Shell thumbnail provider.
The public list keeps its existing direct WIC path and PNG/JPEG pixel assertions.

The tests check COM STA ownership, separate WIC progress during a blocked Shell call, and cancellation before stale delivery.
Two tall panes request exactly 48 of 1,000 distinct paths. A blocked worker cannot expand the shared queue beyond 64 requests.
A missing file produces one callback per slot, with no frame-by-frame retry.
The tests also check page changes, hidden content, replacement requests, and window closure while the Shell worker is blocked.
Fifty repeated extraction cycles retained 40 GDI handles before and after the loop.
The controlled CPU and GPU pixel peaks were 87,552 and 46,080 bytes in the Shell workload.

Three fresh processes per build used the same 60-text-file fixture and initial window geometry.
The protocol remains `tests\measure-browser.ps1`. The additional `cpu_ms` field measures process CPU time.
The following values are medians. They separate private commitment, private working set, and total working set.

| Counter | Preserved header build | Shell-enabled build |
| --- | ---: | ---: |
| First-paint private commitment, MiB | 28.609 | 29.293 |
| First-paint private working set, MiB | 13.816 | 14.332 |
| Idle private commitment, MiB | 28.398 | 31.758 |
| Idle private working set, MiB | 14.066 | 15.762 |
| Idle total working set, MiB | 44.812 | 56.453 |
| Selection, scroll, and full paint, ms | 16.858 | 17.294 |

Shell icons add a real cost to text-only folders, which previously requested no file images.
Idle private commitment increased by approximately 3.36 MiB. Idle private working set increased by approximately 1.70 MiB.
The Shell build retained 11 process threads and 405–417 handles, compared with nine threads and 250 handles.
Those process totals include Windows-owned work, not only XUI workers.
All text-folder idle intervals added zero custom paints.
The Shell build recorded zero CPU time in each two-second idle interval. The baseline recorded 0–15.625 ms.
These small samples do not establish a general performance result.

Three additional fresh demo processes opened native System32, then scrolled through its files.
Observed first-visible readiness was 241.81–339.70 ms, with a 261.81-ms median.
All three settled intervals recorded zero process CPU time and zero custom paints over two seconds.
Each process retained 18 visible slots, 18 ready images, 62 GDI handles, and one render target after scrolling.
The in-process browser test measured 0.779 ms for filter input and 0.178 ms per Page Down command.
Its blocked-Shell test processed ten scroll/filter/paint cycles in 153.60 ms without waiting for the Shell gate.
Readiness measurements include polling or quiet-observation intervals. They are not raw provider-call latency.

The build, logs, measurements, and owned-window captures are in `build\shell-icons`.
Evidence includes `native-tests.log`, `native-final.log`, `native-final-retry.log`, `explorer-isolated.log`, and `shell-tests.log`.
Measurements are in `browser-before.json`, `browser-after.json`, and `system32-idle.json`.
Binding logs and compatible DLL copies are in `build\shell-icons\bindings`.
The `shell-fixtures` directory contains `system32-cmd.png`, `system32-scrolled.png`, and `fixture-200dpi.png`.
No existing browser process was closed or replaced for this work.

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Preview\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = Join-Path (Split-Path $cmake) "ctest.exe"
& $cmake -S . -B build\shell-icons -G "Visual Studio 17 2022" -A ARM64 -DXUI_DESKTOP_TESTS=ON
& $cmake --build build\shell-icons --config Release --parallel 4
& $ctest --test-dir build\shell-icons -C Release --output-on-failure
& $ctest --test-dir build\shell-icons -C Release -R "xui_(shell_thumbnail|thumbnail)_tests" --output-on-failure
```

### Earlier WIC-only validation and measurements

All 22 native tests passed in the ARM64 Release build.
The existing five binding clients passed their UIA and callback-failure checks with the updated native runtime.
The C# framework-dependent and Native AOT wrapper tests each passed 17 assertions.
The binding clients reused their existing published binaries. This change added no wrapper API.

Three fresh explorer processes used the unchanged 60-text-file measurement protocol.
The initial client size remained 924 by 641 DIPs at 96 DPI.
First-paint private commitment was 28.80 MiB median (28.79–28.90).
First-paint private working set was 14.40 MiB median (14.37–14.49).

Idle private commitment was 29.41 MiB median. Idle private working set was 15.19 MiB median.
Selection, scrolling, and a full paint took 17.25 ms median (17.16–17.77).
Each process retained one render target, nine threads, and 250 handles.
All three two-second idle intervals added zero custom paints.

The earlier recorded first-paint values were 28.78 MiB commitment and 14.32 MiB private working set.
These small samples do not establish a general performance change.

Separate fresh processes ran the actual explorer against 100 and 1,000 generated PNG files.
Each folder also contained six format, error, text, and folder entries.

| Counter | 100 PNG files | 1,000 PNG files |
| --- | ---: | ---: |
| Observed cold readiness, ms | 335.16 | 376.51 |
| Shared-cache second-pane readiness, ms | 189.26 | 175.05 |
| Page Down input handling, ms per command | 0.129 | 0.225 |
| Observed readiness after five Page Down commands, ms | 190.90 | 193.61 |
| Maximum retained slots across both panes | 20 | 20 |
| Decodes through the complete workload | 45 | 113 |
| Cache hits through the complete workload | 37 | 37 |
| Owned CPU pixel peak, bytes | 102,528 | 259,200 |
| Controlled GPU pixel peak, bytes | 29,952 | 29,952 |
| Warm private commitment, MiB | 31.13 | 32.05 |
| Warm private working set, MiB | 17.85 | 18.82 |

Readiness measurements include six quiet timer observations. They are not raw decode latency or directly comparable to the text-only paint measurement.
The 20-ms queue sampler observed zero queued jobs in these final runs. It can miss short queue peaks.

The service still enforces the 64-job queue limit. The tests assert this limit and the 24/48-slot limits.
CPU and GPU pixel counters exclude WIC transient allocations, driver allocations, source snapshots, and other process memory.
The tests also cover thumbnail cancellation during a blocked decode and window closure without a decoder join.

Logs, JSON measurements, original fixtures, and owned-window screenshots are in `build\thumbnails`.
Dark single-pane and light dual-pane screenshots show the existing filename alignment and vector fallback icons.
The main evidence files are `native-tests.log`, `thumbnail-tests.log`, `bindings.log`, `browser-60.json`, `images-100.log`, and `images-1000.log`.

## Environment paths and mouse navigation

The ARM64 Release build in `build\navigation` passed all 24 native tests, including the existing flicker, image, and Shell thumbnail tests.
Environment coverage includes UIA submission of `%SystemRoot%\System32`, quoted Unicode paths, unknown references, bounds, and expanded suggestion names.
Mouse coverage uses posted or sent Win32 messages on owned windows, not physical mouse hardware.
It covers both panes, native EDIT, search, FileList, tabs, root coordinates, application commands, unavailable history, and query restoration.
The checks also cover one request per click and unchanged EDIT focus and selection.
The build and regression logs are `build\navigation\build.log` and `build\navigation\regression.log`.

## Shell-icon build location

The current executable is `build\shell-icons\Release\xui_demo.exe`.
The existing `build\header` and `build\arm64` executables remain untouched.
The compact header, native EDIT composition, and suggestion-refresh fixes remain in this build.

## Tab context menus, 2026-09-16

The current C# demo uses `TabContextMenu.cs` for tab commands and snapshot guards.
`TabStrip::prepare_context_menu` records pointer or keyboard targets without selection.
The shared ABI menu subscription passes the target ID and guards the tab revision.
`ExplorerTab.Duplicate` copies mutable view and history state.
`ExplorerPane` retains tab identities during reordering and assigns new identities to copies.
Closing a pane cancels its pending work. Closing the left pane copies the surviving pane before hiding the right pane.
`FileExplorer.Tests` covers model copies and ordering.
`ExplorerSmoke` covers native context targeting, commands, shortcuts, stale snapshots, and pane closure.

## Per-tab partitions

`ExplorerTab.Partition` stores folder-first, file-first, or mixed order.
`FileSystemService.FilterAndSort` applies the partition before the selected sort.
`FilePaneView` captures the partition for each asynchronous sort and includes it in the retained column cache key.
Partition changes cancel obsolete filter work without canceling folder navigation.
`FilePaneView` creates the footer menu beside View with `Window.MenuFlyout`.
The three command rows have icons, labels, and a checkmark for the active partition.
The retained `CommandMenu` supplies keyboard navigation and menu accessibility. It replaces the original popup with three buttons.
New tabs inherit the active partition. Copies inherit their source partition.
`ExplorerApplication.InitializeRight` copies the active partition when a new pane opens.
The model suite covers every sort column and direction with each partition.
`ExplorerSmoke` includes the focused `--partition-smoke` mode for all views, lazy Tree children, and inheritance.

The partition button displays the active `ButtonIcon` instead of text.
`FilePaneView.UpdatePartitionButton` updates its icon, accessible label, and tooltip on setting changes and tab rendering.
The three original SVGs are in `assets/icons`.
`Drawing::button_icon` renders matching vector geometry without font glyphs or asynchronous image loading.
The icon values append to the existing C++, C, C#, and Rust contracts.
`style_basic_render_tests.cpp` checks distinct shapes, bounds, size scaling, foreground colors, and Classic/WinUI parity.
The partition smoke also checks icon updates after setting changes, tab switches, and inheritance.
