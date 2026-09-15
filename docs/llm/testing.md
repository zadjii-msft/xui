# Test coverage and measurement protocols

This reference describes regression scope and measurement methods.
Use [CONTRIBUTING](../../CONTRIBUTING.md#tests) for the build and test entry points.

## Tests and measurements

The explorer adds these regressions:

| Program | Coverage |
| --- | --- |
| `xui_explorer_tests` | History commits, failed navigation, tab selection and closure, cancellation, bounded state, UNC roots, long Unicode scans, activation, and injected file associations |
| `xui_explorer_smoke` | The real explorer, address input, history, keyboard shortcuts, context commands, independent panes, tab providers, divider input, clipping, and resource bounds |
| `xui_tab_window_tests` | Owned-window tab pixels in Classic and WinUI, light/dark/high contrast, 96/120/144/168/192 DPI, open bottom edges, empty rows, custom colors, content activation, close targets, focus, and overflow |
| `xui_suggestion_tests` | Folder prefixes, real and synthetic enumeration limits, deterministic cancellation, native EDIT behavior, popup input, themes, and closure during a blocked request |
| `xui_split_window_tests` | Eight window cycles with tabs, two lists, native fields, capture cancellation, simulated DPI, target recreation, and final resource disposal |

The desktop tests require `-DXUI_DESKTOP_TESTS=ON`.
The keyboard test requires foreground ownership before it sends input. It never sends a shortcut to another application.
The file-association test records the exact selected path through an injected dispatcher. It does not start fixture files.
The smoke test uses scoped UIA focus-property events, selection events, and structure events.
Its optional `--global-focus-events` argument also subscribes to desktop-wide focus events.
That optional subscription can stall inside Windows before a test action. It depends on providers outside this process.

The managed explorer `--smoke` also covers file transfers in its temporary fixture.
The checks include multi-selection menus, folder-row copy, empty-area move, nested folder content, and refresh in both panes.
File rows and panes with obsolete rows must reject drops.
Native text fields must retain their text clipboard shortcuts.
The smoke does not replace the interactive desktop clipboard.
These controller checks do not establish interoperability with every external application or Shell extension.
`xui_file_transfer_tests` covers grid drag gestures, clipboard serialization, the OLE drop protocol, and Shell file operations.
`xui_file_clipboard_tests` runs clipboard round trips in a job-bounded child with a private window station and desktop.
The shared `tests\private_desktop.hpp` helper also isolates the existing native editing tests.
The parent process requires the interactive clipboard sequence to remain unchanged.

Run the explorer checks:

```powershell
ctest --test-dir build\arm64 -C Release -R "xui_(explorer|split_window)" --output-on-failure
.\tests\measure-browser.ps1 -Runs 5 -WorkspaceCycles -Screenshots -Output build\explorer\final-memory.json
```

`-WorkspaceCycles` adds a second pane, creates 15 inactive tabs, closes those tabs, and performs 40 folder transitions.
It then hides the second pane and records two idle samples.
The protocol retains the original 60-file workload, 924×641 client extent, and selection/filter workload.
Native child discovery now supports pane hosts. The theme workload uses the Theme command instead of F6.
The baseline path still supports the original F6 command.

The four image regressions require `-DXUI_DESKTOP_TESTS=ON`:

| Program | Coverage |
| --- | --- |
| `xui_image_tests` | Exact budgets, reservations, sharing, alpha conversion, size keys, file versions, corrupt data, LRU eviction, cancellation, and device recreation |
| `xui_image_window_tests` | A 20,000-item recycled grid, folder replacements, real pixels, native geometry, clipping, scrolling, idle frames, unload, and blocked-decoder closure |
| `xui_images_smoke` | The actual thumbnail executable, external UIA, native folder input, source errors, image errors, unload, idle, and stale providers |
| `xui_gallery_image_smoke` | The gallery preview, real decoding, external image semantics, native path input, corrupt files, unload, idle, and provider disconnection |

`xui_image_tests` writes original PNG fixtures through the Windows WIC encoder.
Its malformed files cover zero dimensions, huge dimensions, truncated pixels, corrupt data, missing files, directories, and an oversized encoded file.
Deterministic gates stop the worker before decoding, during a reservation, and before completion delivery.
The tests replace a tile source at each boundary and reject the obsolete result.
The full-queue test holds one active job and fills all 64 queue slots.
The GPU test fills the controlled bitmap budget, rejects an extra upload, evicts a bitmap, and uploads again.

The window test holds a 4 MiB reservation while the decoder gate remains closed.
`Application::run` returns before the test opens that gate.
After the worker resumes, cancellation returns the reservation to zero without a stale control update.
Image assertions use owned pixel bytes and controlled bitmap estimates, not process-memory thresholds.

`xui_core_tests` covers layout, lifecycle, filtering, selection, navigation, and viewport calculations.
It uses explicit assertions that remain active in Release builds.
It also covers scrollbar geometry and text contrast for both built-in palettes.

`xui_navigation_window_tests` captures command palettes in Classic and WinUI at 96/144/192 DPI, with dark, light, and high-contrast themes.
Pixel assertions compare the list background with the padding on all four sides and the gap below the search field.
The same captures cover row hover colors and the shadow outside the frame.

`xui_control_tests` covers control state, disabled actions, pointer capture, cancellation, focus traversal, invalidation, Unicode limits, and retained controls.
It also covers injected text metrics, cached measurement, fixed and automatic sizes, size limits, unbounded flex measurement, scroll reveal, and content ownership.
`xui_scroll_tests` covers DirectWrite measurements, narrow windows, native viewport clipping, UIA bounds, offscreen state, native keyboard input, and focus reveal.
It also covers wheel input over EDIT, thumb drag, keyboard scrolling, disabled descendants, nested Stacks, and an embedded FileList.
Eight window cycles exercise dark, light, explicit high-contrast colors, and 96/120/144/192-DPI messages without changes to Windows settings.
Repeated paints and scrolling must reuse the cached text layouts and the single render target.
The tests compare native text-pattern support with an unmodified EDIT provider.
The tests also require bounded handle counts and rejected provider actions after closure.
The resource sample follows asynchronous snapshot disposal and a short message-pump interval for native provider cleanup.
The combined UIA client/server test reports process handles but does not attribute client thread pools to the framework.
The server-only lifecycle test retains its process-handle limit for eight ordinary windows and eight ScrollView trees.
`xui_window_tests` covers public window ownership, callback closure, startup failure, callback failure, and later runs on the same thread.
It also covers a public list beside other controls, filter delivery, selection callbacks, reentrant closure, and off-thread snapshot disposal.
Two sets of eight repeated windows exercise cancelled refreshes, theme changes, resize, simulated DPI changes, and target recreation.
The DPI cases use 96, 120, 144, and 192 without global display changes.
These cases require one live target during display and no XUI targets after closure.
After warm-up, handle, GDI, and USER counts must remain within a small fixed allowance for Windows initialization.
A blocked-loader test requires window closure to return before the loader can finish.
An isolated public-control server checks list names, automation IDs, independent focus, selection, and retained-provider rejection after closure.
It requires the same manifest and desktop as the applications.
`xui_gallery_smoke` covers UIA roles, names, identity, Invoke, Toggle, disabled action rejection, label updates, and native text.
It also covers Tab, Shift+Tab, Space, Enter, pointer cancellation, theme changes, idle paint counts, and provider invalidation after shutdown.
The native UIA text proxy can complete focus changes after a method returns.
The browser and gallery probes require stable focus before keyboard sequences instead of accepting a transient focus notification.
The gallery resolves EDIT through its automation tree, by name and control type.
It calls external UIA `SetFocus` without a `WM_NEXTDLGCTL` workaround.
Twelve activation cycles cover a competing test window, minimized restoration, native focus events, and queued Unicode input with a surrogate pair.
An external event client covers Invoke, Toggle, name, enabled, native value, and custom keyboard-focus properties.
The isolated public-list client covers selection events, focus properties, structure changes, scroll properties, and quiet repeated scroll endpoints.
The lifecycle test also exercises `Window::focus` with native EDIT.

`xui_native_integration_tests` covers native selection, replacement, undo, surrogate input, rejected focus, and composition guards.
It checks exact native font heights at 96, 120, 144, and 192 DPI.
A private renderer seam injects `D2DERR_RECREATE_TARGET` after `EndDraw`.
The test requires a replacement frame, one live target, retained text layouts, and a return to idle.
The seam belongs to the internal renderer. It adds no public window action.
Clipboard checks run in a private window station with its own desktop.
They cover public copy, native copy/paste/cut/undo, and a paste limit that preserves surrogate pairs.
The interactive clipboard sequence number must remain unchanged.

`xui_performance_tests` covers immutable views, source generations, cancellation, errors, retirement, and repeated worker shutdown.
It also covers changed collection order, removed identities, hidden selection, and independent focus.
The list cases use 60, 100,000, and 1,000,000 synthetic rows.
Allocation counters require zero allocations during 20,000 selection, focus, scroll, and lookup sequences at each size.
Tests use synchronization and operation invariants, not performance thresholds.

The desktop smoke program creates a fixture folder under its working directory and opens its own demo window.
It exercises native text input, Unicode names, UIA actions, keyboard navigation, scrolling, and shutdown.
It also invokes Refresh through the native context menu.
It exercises theme changes without loss of text, selection, or item identity.
Pointer-message sequences exercise scrollbar paging and thumb dragging.
It also reports the number of submitted rows and custom paint calls during an idle interval.
Live `WM_CHAR` input overlaps a refresh. Another sequence combines 80 query replacements, ten refresh requests, and two theme changes.
The probe requires the final requested generation and the expected row.
It also exercises simulated IME boundaries, process memory counters, and shutdown after observed worker activity.
An interactive Windows desktop is necessary.

```powershell
& ".\$build\Release\xui_windows_smoke.exe" ".\$build\Release\xui_demo.exe"
& ".\$build\Release\xui_gallery_smoke.exe" ".\$build\Release\xui_gallery.exe"
& ".\$build\Release\xui_window_tests.exe"
& ".\$build\Release\xui_scroll_tests.exe"
& ".\$build\Release\xui_native_integration_tests.exe"
```

To include the desktop smoke program in CTest, configure `-DXUI_DESKTOP_TESTS=ON`.
The default Windows CTest configuration also registers some tests that create native controls.
`XUI_DESKTOP_TESTS` adds the remaining interactive smoke and presentation tests.

### Repeatable benchmark

Run the benchmark separately from builds and desktop tests:

```powershell
& ".\$build\Release\xui_performance_tests.exe" --benchmark |
    Tee-Object -FilePath ".\$build\performance-results.txt"
```

The benchmark reports synthetic data creation separately from snapshot construction and filtering.
It performs no directory I/O. Timed workloads exclude process startup.
Each nonempty query reports the median of five runs.
The empty-query result and the interaction sequence each report one run.
The interaction sequence includes selection, independent focus, reveal, and identity lookups.
Checksums and assertions keep that work observable in Release builds.

Windows memory counters report private bytes and the working set.
The memory sample includes source items, cached names, indices, views, and allocator retention.
It excludes the directory identity map, graphics resources, and external UIA clients.
Refreshes can temporarily retain both old and new sources.

### Binding validation and measurement scope

The C test compiles the public header as C11 and checks layouts, imports, version discovery, and lifecycle.
The adversarial ABI test covers invalid/stale tokens, kinds, threads, structure sizes, malformed strings, overflow, ownership, callbacks, and batch rejection.
It also covers self-unsubscribe, busy destruction, thrown C++ callbacks, recursive dispatch, persistent callback codes, and repeated arena teardown.
Managed tests cover Unicode, disposal, wrong-thread access, ownership, batch rejection, subscription lifetime, callback exceptions, and list identity.
Rust tests add raw layout checks, RAII ownership, panic containment, callback recursion, and compile-time thread restrictions.

The same external UIA harness runs all five actual applications.
It checks Unicode labels, Invoke, Toggle, native EDIT ValuePattern, Unicode input, submit, key callbacks, retained scrolling, images, and virtual lists.
The keyboard check sends both surrogate units through native `WM_CHAR` and reads text inside foreign callbacks.
It requires one render target, a quiet idle interval, callback closure, a successful process exit, and rejection from a retained provider.
Separate GUI runs require callback exceptions or panics to produce a nonzero application result.
The harness also requests image replacement shortly before closure.

This Windows EDIT provider reports no TextPattern in these runs. ValuePattern and real native editing passed.
The earlier native comparison tests cover parity with an unmodified EDIT provider.
The binding reload/close sequence exercises a race, not a deterministic blocked-codec gate.
The native image-window test separately covers a blocked decoder and nonblocking UI closure.
The CPU and controlled GPU ledgers do not bound WIC transient memory, hardware residency, or total process memory.

All five measured applications use a 600-by-720-DIP window, the dark theme, the same native fonts, and the same controls.
The small workload contains eight preference labels, 60 files, and one generated PNG.
No UIA client runs during memory measurements.
Startup starts before `Process.Start` and ends after a nonzero native paint counter.
The probe disables console creation for every measured GUI process.
Memory follows two seconds of idle time. A separate half-second interval supplies the idle paint delta.
Processes are fresh, but file caches are warm. These results are not reboot-cold startup measurements.

Each throughput process applies 64,000 text mutations before the message loop starts.
The individual path makes 64,000 calls. The batch path makes 1,000 calls with 64 properties each.
The managed measurements include wrapper encoding, pinning, and boundary overhead.
The native static path has no foreign boundary and no separate batch operation.
These numbers measure property submission, not layout, paint throughput, or interactive frame latency.
Raw measurements and exact deployment file lists are in `build\phase4\measurements.json`.

Real IME sessions, Narrator speech, physical mixed-monitor transitions, and actual GPU removal remain manual coverage.
The automated tests do not claim those results.

## Declarative integration fixtures

## Run the integration checks

The native probe reads controls through UI Automation.
It targets the fixture's process ID.
It does not move the pointer or activate another application.

1. Build the native library and probe:

   ```powershell
   cmake --build build\xui-language --config Release --target xui xui_language_probe
   ```

2. Run the edit-loop checks:

   ```powershell
   .\tests\xui-language.ps1
   ```

The script creates an isolated fixture under `build\xui-language-check`.
It starts `dotnet watch` and changes the fixture, not the tracked sample.
It checks text, layout, input preservation, event behavior, invalid-edit recovery, and structural fallback.
It also checks input addition, renaming, deletion, no-op builds, and release references.
Logs and edit durations remain in the fixture directory.
The script stops the processes that it starts.

The compiler and MSBuild checks do not open a native window:

```powershell
dotnet run --project bindings\dotnet\GeneratorTests\GeneratorTests.csproj -c Release
.\bindings\dotnet\GeneratorTests\BuildTests.ps1
```
