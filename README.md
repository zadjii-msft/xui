# XUI

XUI is a small C++20 UI framework prototype for Windows.
The file browser and control gallery both use the public control API.
The browser shows a searchable list for a real folder.
Win32 owns the windows and message loop. Direct2D draws the custom UI. DirectWrite draws text.
The executable uses the static MSVC runtime and Windows system libraries. It has no third-party runtime dependencies.

## Visual milestone

The demo starts with a dark theme. F6 switches between dark and light themes.
The context menu also contains both theme commands. The theme lasts for the current window.
System high contrast overrides the theme colors.

The window includes a folder header, an integrated search field, column headers, vector file icons, and a status area.
Rows have separate hover, selection, and keyboard-focus states.
The scrollbar supports thumb dragging and track paging. The list exposes scrolling through UIA `ScrollPattern`.
The scrollbar does not create a separate UIA element.

`include\xui\theme.hpp` contains shared colors, typography sizes, spacing values, and scrollbar geometry.
`WindowOptions::theme` selects the initial theme. The browser sample also accepts `BrowserOptions::theme`.
The icons use drawing primitives, not image files, shell queries, or per-row image caches.
Supported Windows versions use matching title-bar colors. Older versions retain system title-bar colors.

## Reusable controls

`include\xui\application.hpp` provides `Window` and `Application::run`.
Applications describe a control tree and callbacks. They do not supply a window procedure or drawing callbacks.
`demo\gallery.cpp` and `demo\browser.cpp` contain the application compositions.

| API | Purpose |
| --- | --- |
| `Stack` | Layout, padding, spacing, flex space, an optional surface, and a separator |
| `Label` | Text, heading or caption appearance, semantic color, and an accessible name |
| `Button` | An enabled command with an `on_click` callback |
| `Toggle` | A checkbox with `checked`, `set_checked`, and an `on_change` callback |
| `TextInput` | Native EDIT, committed-text and submit callbacks, search appearance, placeholder, and shortcut hint |
| `FileList` | Immutable views, stable selection and item focus, navigation, viewport, empty text, and change callbacks |
| `ViewTask` | Cancellable source and filter work, latest-generation delivery, and progress counters |
| `Window` | Content ownership, size, focus, themes, key callbacks, clipboard text, tasks, closure, and error text |

`Control::set_automation_id` supplies an optional application identity for custom controls.
Without an override, custom controls use their stable element IDs.
`Control::on_context_menu` returns native menu items with actions, enabled state, and checked state.
Native EDIT retains its system context menu and provider.

For example, this application updates a label through a button callback:

```cpp
#include "xui\application.hpp"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    xui::Window window({L"My application", {480, 240}});
    auto content = std::make_shared<xui::Stack>(xui::Axis::vertical);
    content->set_padding({24, 24, 24, 24});
    content->set_spacing(12);
    auto label = std::make_shared<xui::Label>(L"Ready");
    auto button = std::make_shared<xui::Button>(L"Update label");
    button->on_click([&] { label->set_text(L"Updated"); });
    content->add(label);
    content->add(button);
    window.set_content(content);
    return xui::Application::run(window);
}
```

Link the executable to `xui_windows`.
Include the manifest resource from `demo\xui.rc`, or supply an equivalent common-controls v6 and per-monitor-DPI manifest.
For MSVC builds that use this resource, set `/MANIFEST:NO`, as the gallery target does.

### Ownership and property updates

The window retains its content through `std::shared_ptr`. A control has one layout parent and a stable `Element::id`.
Build the tree before `Application::run`. Set properties and use callbacks on that same UI thread.
The window supports one active run on the thread. Each `Window` runs once, but separate windows can run in sequence.
The caller must not initialize COM as MTA.

Callbacks must not outlive the objects that they reference.
Reference captures in the example remain valid during the blocking `Application::run` call.
Strong captures that refer back to their own control can create an ownership cycle.
Window teardown clears the host invalidator and disconnects accessible providers.
Retained controls remain valid after window destruction.

`set_enabled`, text, name, checked state, and heading appearance request paint updates.
Preferred size, spacing, and padding request layout and paint updates.
The host combines pending updates. It has no animation timer or continuous render loop.
Controls use fixed preferred sizes and single-line text with ellipsis. Text changes do not resize a control.

Standard-control property setters do not call application action callbacks.
List selection operations call `on_selection_change`. View assignments call `on_view_change`.
Both callbacks observe the updated model. Item focus does not invoke the selection callback.
Pointer, keyboard, and UIA actions use the same activation behavior and enabled-state check.
Native committed text calls `TextInput::on_change`. IME preedit text does not call this callback.
Text input supports 1,024 UTF-16 units on Windows. The property setter preserves surrogate pairs at this limit and stops at the first NUL.
Callbacks can update other controls, change the theme, or request window closure.
`Window::focus(control, select_all)` requests native focus. The optional selection flag applies to text inputs.
Focus requests reject disabled controls, foreign controls, and closed windows.
The input-state methods on `Control` are backend boundaries, not application focus commands.
Startup errors and callback exceptions return a nonzero result. `Window::error()` supplies the error text.

### Input and accessibility

Buttons and checkboxes support hover, pressed state, pointer capture, drag-out cancellation, and capture loss.
Window deactivation and cancellation also cancel pending presses.
Tab and Shift+Tab move focus through enabled controls. Space activates a focused button or checkbox on release.
Enter activates a focused button. Enter does not change a checkbox.
The native EDIT retains Windows text selection, undo, clipboard, IME, and native UIA text behavior.

Custom controls expose UIA text, button, or checkbox roles.
Their accessible names track the public names. Their automation IDs use the stable element IDs.
Custom control fragments expose explicit focus, Invoke, and Toggle actions where applicable.
The HWND adapters supply native tree placement. Native EDIT keeps its own provider identity and a preceding native label.
The backend publishes name, enabled, focus, and toggle changes, plus button invocation events.
Retained providers reject actions after window teardown.

### Virtual lists and asynchronous delivery

`FileList` derives from `Control`. A `Window` accepts it beside labels, buttons, toggles, and text inputs.
The native backend creates one list window, not one control per row.
The application supplies data and actions. It does not supply paint callbacks or accessibility providers.

This example uses the same public APIs as the real browser:

```cpp
#include "xui/application.hpp"

int run_results() {
    xui::Window window({L"Results", {600, 480}});
    auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto search = std::make_shared<xui::TextInput>(L"Filter");
    auto results = std::make_shared<xui::FileList>(L"Results");
    root->add(std::make_shared<xui::Label>(L"Available files"));
    root->add(search);
    root->add(results, 1);
    window.set_content(root);
    auto task = window.create_view_task(
        [](const xui::CancelCheck& cancel) {
            auto items = std::make_shared<const std::vector<xui::FileItem>>(
                std::vector<xui::FileItem>{
                    {1, L"Alpha.txt", L"C:\\Data\\Alpha.txt", false},
                    {2, L"Beta.txt", L"C:\\Data\\Beta.txt", false}});
            return xui::SourceResult{xui::FileSnapshot::build(items, cancel), {}};
        },
        [results](xui::ViewResult result) {
            if (result.view) results->set_view(std::move(result.view));
        });
    search->on_change([task](const std::wstring& query) { task->request(query); });
    task->request(L"");
    return xui::Application::run(window);
}
```

The task owns one worker and a bounded result mailbox. The window waits for a shared task event without polling.
The loader runs off the UI thread. Result callbacks run on the window thread.
Query changes cancel obsolete filters. `request(query, true)` also cancels and reloads the source.
Only the latest generation reaches the callback. `generation`, `applied_generation`, and `busy` expose task progress.
The loader must own its data and honor `CancelCheck`. Shared captures keep source services alive until cancellation finishes.

Retain the task handle while the task is necessary. Handle destruction, `cancel`, and window closure revoke delivery.
Cancellation is permanent. Later requests return zero.
`Window::close` revokes tasks immediately, including during a result callback.
The backend joins cancelled workers on a cleanup thread. Window closure does not join the worker on the UI thread.
Result delivery and list replacement also arrange off-thread snapshot disposal.
Retained controls keep their model after closure. Their final snapshot disposal uses the same cleanup facility.
Process exit waits for cleanup. A loader that ignores cancellation can still delay process exit.

### Gallery and browser boundaries

The gallery edits a name, saves a greeting, enables or disables the save command, and changes the theme.
Its status labels show callback results. It adds no application-specific window procedure.

Both applications use `Window` for their native host.
`src\window_host.cpp` supplies COM initialization, the blocking message loop, focus traversal, child placement, and title-bar appearance.
The shared backend uses `Drawing`, theme tokens, and `NativeEditBridge`.
`src\controls.cpp` contains platform-independent behavior. `src\application.cpp` connects controls to Windows and drawing.
`src\control_accessibility.cpp` contains the control providers.

`src\list_peer.cpp` supplies list drawing, pointer input, keyboard navigation, scrolling, and the adapter for existing list providers.
`src\accessibility.cpp` remains the single implementation of the virtual-list providers.
Provider actions include the control identity and item identity. A recycled HWND cannot accept an old provider action.
`src\async.cpp` supplies reusable task delivery and cleanup.
Control renderers share the window's Direct2D factory, DirectWrite factory, and immutable text formats.
Each control retains its own render target.

`demo\browser.cpp` builds the header, search field, columns, list, status label, shortcuts, and menu through public APIs.
`demo\directory.cpp` owns folder enumeration, sorting, and file identities. The library has no browser-specific folder service or captions.
The old `src\windows.cpp` specialized host is deleted. Its browser options and entry function now belong to `demo\browser.hpp`.
The browser composition has no native window procedure, drawing calls, backend includes, or accessibility wiring.
It creates no per-row controls.

The control host creates an HWND for each control, including one HWND for each virtual list.
It does not provide a general scroll container.
Runtime tree replacement, removal, automatic text measurement, multiline input, and application-defined control renderers are not supported.

## Build

Requirements: Windows 10 version 1703 or later and Visual Studio 2022 with the C++ desktop workload.
A Windows SDK and CMake 3.24 or later are also necessary.
The initial Windows backend requires a 64-bit build.

In Visual Studio Developer PowerShell, run these commands from the project directory:

```powershell
$arch = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq "Arm64") { "ARM64" } else { "x64" }
$build = "build\$arch"
cmake -S . -B $build -G "Visual Studio 17 2022" -A $arch
cmake --build $build --config Release --parallel 4
ctest --test-dir $build -C Release --output-on-failure
& ".\$build\Release\xui_demo.exe" .
& ".\$build\Release\xui_gallery.exe"
```

The architecture selection avoids x64 emulation on ARM64 Windows.

If CMake is not on `PATH`, use the copy that Visual Studio supplies:

```powershell
$arch = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq "Arm64") { "ARM64" } else { "x64" }
$build = "build\$arch"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake -S . -B $build -G "Visual Studio 17 2022" -A $arch
& $cmake --build $build --config Release --parallel 4
& (Join-Path (Split-Path $cmake) "ctest.exe") --test-dir $build -C Release --output-on-failure
```

## Run

To select a folder, supply its path as the first argument:

```powershell
& ".\$build\Release\xui_demo.exe" "C:\Windows"
```

Without an argument, the demo uses the current directory. The list contains immediate children, not a recursive folder tree.
Folders appear before files. The demo does not open, rename, or delete items.
The status line shows scan errors and clipboard errors.

The search field filters file names with a case-insensitive substring.
Tab changes focus between the search field and the list. Ctrl+F focuses the search field.
Enter in the search field requests the filter and focuses the list.

Arrow keys, Home, End, Page Up, and Page Down change the list selection.
The mouse wheel and the themed scrollbar move the viewport.
Ctrl+C copies the selected path. The context menu provides the same copy command, Refresh, Search, and theme commands.
F5 refreshes the folder. Shift+F10 opens the context menu for keyboard use.

## Framework boundaries

`xui_core` has no Windows dependencies. Its public API includes stable element identity, explicit property updates, and two-pass layout.
`Stack` supports horizontal and vertical layout, preferred sizes, flex space, gaps, and padding.
Elements distinguish layout invalidation from paint invalidation. The host combines pending layout requests and uses normal invalid paint regions.
The application has no continuous render loop.

`FileList` owns navigation and viewport behavior. `FileListModel` owns items, filter results, and selection identity.
Item focus is separate from selection. An accessibility focus action does not select or deselect an item.
The renderer submits visible rows plus a small buffer. It does not create an element, text layout, or UIA provider for every item.
Items and filter indices remain in memory. This is visual virtualization, not a paged storage system.

`Commands` remains available for command tables.
The browser connects shared callback actions through `Window::on_key` and `Control::on_context_menu`.
The Windows backend separates drawing, list adaptation, asynchronous delivery, native text input, and UI Automation.
Directory access belongs to the sample service.

One persistent worker performs enumeration, sorting, snapshot construction, and filtering outside the UI thread.
Generation numbers reject stale source data and stale filter results.
Window closure requests cancellation without a UI-thread join. A filesystem driver that ignores cancellation can delay final process cleanup.
The UI thread submits committed text without a typing timer. It keeps the previous view until the current result arrives.

`NativeEditBridge` uses a real Windows EDIT control. Windows owns its IME, text selection, Unicode input, undo, clipboard behavior, and accessibility.
This bridge is not a custom text engine or a completed TSF implementation.
The search field has a 1,024-character limit. Framework text drawing never substitutes for editable text.
The empty-field hint uses the theme color. It is decorative text, not part of the editable value.

The custom list exposes UIA list semantics, selection, scroll, fragment navigation, and on-demand item providers.
Selection is optional and single-item. UIA removal clears only the addressed selected item.
Item providers retain stable IDs instead of recycled row positions.
Native EDIT supplies search-field accessibility. The public `Label` provider supplies status-text accessibility.
The window uses per-monitor DPI, system high-contrast colors, and graphics-target recreation.

## Tests and measurements

`xui_core_tests` covers layout, lifecycle, filtering, selection, navigation, and viewport calculations.
It uses explicit assertions that remain active in Release builds.
It also covers scrollbar geometry and text contrast for both built-in palettes.

`xui_control_tests` covers control state, disabled actions, pointer capture, cancellation, focus traversal, invalidation, Unicode limits, and retained controls.
`xui_window_tests` covers public window ownership, callback closure, startup failure, callback failure, and later runs on the same thread.
It also covers a public list beside other controls, filter delivery, selection callbacks, reentrant closure, and off-thread snapshot disposal.
A blocked-loader test requires window closure to return before the loader can finish.
An isolated public-control server checks list names, automation IDs, independent focus, selection, and retained-provider rejection after closure.
It requires the same manifest and desktop as the applications.
`xui_gallery_smoke` covers UIA roles, names, identity, Invoke, Toggle, disabled action rejection, label updates, and native text.
It also covers Tab, Shift+Tab, Space, Enter, pointer cancellation, theme changes, idle paint counts, and provider invalidation after shutdown.
The native UIA text proxy can complete focus changes after a method returns.
The probe requires stable focus before keyboard sequences instead of accepting a transient focus notification.
It uses the standard host focus route for EDIT and UIA `SetFocus` for custom controls.
The lifecycle test also exercises `Window::focus` with native EDIT.

`xui_performance_tests` covers immutable views, source generations, cancellation, errors, retirement, and repeated worker shutdown.
It also covers changed collection order, removed identities, hidden selection, and independent focus.
The large-list cases use 100,000 and 1,000,000 synthetic rows.
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
```

To include the desktop smoke program in CTest, configure `-DXUI_DESKTOP_TESTS=ON`.
The default CTest configuration runs only the pure tests.

## Performance design

`FileSnapshot` shares an immutable item array. It stores lowercase names in one character buffer.
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

## Known limits

This milestone supports one active application window per UI thread. The browser supports a flat list and single selection.
It does not provide a general styling system, a reactive property graph, or a custom text engine.
Filter matching uses wide-character case conversion, not full Unicode normalization or linguistic search.
Filtering still examines every source row. It runs on the worker, not the UI thread.
The index format supports at most 4,294,967,295 rows, but available memory imposes a much smaller practical limit.
All source items remain in memory. The million-row benchmark is not a million-file desktop test.
Very large scroll extents lose subpixel precision because offsets use floating-point values.
Cancellation cannot interrupt one allocator operation, one substring search, or a filesystem driver that ignores cancellation.
The cleanup thread waits for cancelled workers. Process exit does not have a fixed shutdown deadline.
UIA clients that enumerate the complete tree still perform work for every item.

UIA supports the patterns that this demo uses, not every Windows control pattern.
The native context menu retains Windows appearance.
Touch gestures, drag-and-drop, folder navigation, and file operations are outside this milestone.
High-contrast changes and graphics-target recreation have implementation paths, but hardware coverage is not exhaustive.

## Recorded results

### Foundation baseline

The native ARM64 Release build completed on September 11, 2026, on Windows build `10.0.28637`.
The build used MSVC `19.44.35228`, Windows SDK `10.0.26100.0`, and CMake `3.31.6-msvc6`.
The initial executable size was **265,216 bytes (259 KiB)**. Its direct imports were Windows system DLLs.

The pure core tests and the compiled Windows/UIA smoke program passed.
The smoke program used 3,001 real files, including a filename with Japanese characters and a supplementary Unicode character.
After a resize to a 700-by-500-pixel window, the renderer submitted **13 rows**, including the buffer.
During a requested two-second idle interval, the custom paint counter increased by **0**.
`GetProcessTimes` reported **0 ms** of process CPU time during that interval.
Zero is the observed counter delta, not proof of zero CPU use.
The paint counter excludes native EDIT caret painting. These observations are not a general performance benchmark.

The smoke program exercised optional selection, removal, independent item focus, hidden-selection restoration, stable IDs after refresh, scrolling, and native text access through UIA.
It exercised Tab focus traversal, Home, End, and Page Down through posted Windows key messages.
It also exercised repeated refresh cancellation, target recreation after a display-change message, visible folder errors, and shutdown immediately after observed directory activity.
A separate UI session displayed the custom drawing and exercised the native search field.

Real IME sessions, clipboard contention, mixed-DPI monitor changes, high-contrast toggles, and hardware device loss remain unverified.
Physical shortcut chords, UIA event delivery, and complete screen-reader compatibility remain unverified.
The native text bridge delegates these text services to Windows; this milestone does not claim a custom text engine.

### Visual milestone

The updated ARM64 executable is `D:\dev\private\xui\build\arm64\Release\xui_demo.exe`.
Its size is **279,040 bytes (272.5 KiB)**.
The visual changes add **13.5 KiB** to the foundation executable.
The build uses the same toolchain and adds only the Windows `dwmapi` library.

Both CTest programs passed after the visual changes.
The smoke program submitted **10 rows for 3,001 items** after the same 700-by-500-pixel resize.
The larger header leaves less space for rows than the foundation layout.
A requested two-second idle interval produced **0 additional custom paints** and **15.625 ms** of measured process CPU time.
Earlier smoke runs reported 0 ms. These counter deltas are not a general performance benchmark.
The probe permits three interval attempts for late hover or focus events on a shared desktop.
It requires a fully quiet interval and rejects continuous repainting. The recorded run needed one interval.

The dark appearance and search hint received a visual inspection.
Automated coverage includes both palettes, theme commands, text preservation, scrollbar input, keyboard behavior, and UIA selection and scrolling.
Real high-contrast sessions, mixed-DPI monitors, IMEs, and complete screen-reader behavior still require the Windows integration phase.

### Performance milestone

The September 11, 2026 ARM64 Release executable is **296,448 bytes (289.5 KiB)**.
This adds **17 KiB** to the visual milestone.
The application adds no external dependency. Benchmark and smoke programs use Windows process-memory counters.

The final build and all three CTest programs passed.
A separate repetition passed all three programs three times each.
The regression suite also requires requests to proceed while an old source destructor remains blocked.
This check prevents large result destruction under the request mutex.

The isolated benchmark recorded these times in milliseconds:

| Workload | 100,000 rows | 1,000,000 rows |
| --- | ---: | ---: |
| Synthetic item creation | 38.31 | 327.30 |
| Snapshot and ID index construction | 32.84 | 360.20 |
| Empty query, all rows | 0.45 | 4.16 |
| `.TXT`, all matches, median | 1.38 | 10.47 |
| `FILE-0000042`, one match, median | 1.20 | 10.17 |
| `not-in-any-name`, no matches, median | 0.68 | 6.58 |
| 20,000 interaction sequences | 27.94 | 40.35 |

Both interaction sequences produced **zero allocations**.
The tenfold source increase raised the measured interaction time by approximately 1.44 times.
These measurements are observations, not latency guarantees.

| Retained memory or capacity | 100,000 rows | 1,000,000 rows |
| --- | ---: | ---: |
| Cached names and source indices, bytes | 5,030,176 | 55,628,012 |
| Empty-query view, bytes | 400,000 | 4,000,000 |
| Process private bytes | 33,181,696 | 254,779,392 |
| Process working-set bytes | 38,563,840 | 249,651,200 |

The initial benchmark process used 745,472 private bytes.
The sparse view had four bytes of index capacity. The no-match view had zero bytes of index capacity.
The full-match query had 553,020 and 4,199,476 bytes of index capacity because the vector grows during matching.
The memory samples include allocator retention after the query runs.

The final desktop run used 3,001 real files and submitted **10 rows** after the 700-by-500-pixel resize.
Nine native character messages had a maximum measured dispatch time of **7.663 ms** during filter and refresh activity.
The rapid replacement sequence completed generation **108** with the expected final row and dark theme.
The two-second idle interval produced **0 custom paints** and **0 ms** of measured process CPU time.
The demo used **42,295,296 private bytes** and **65,372,160 working-set bytes** at that point.
Shutdown after observed worker activity took **56.72 ms**.
These figures exclude fixture creation and do not prove zero CPU use or a fixed shutdown bound.

The benchmark output is `build\arm64\performance-results.txt`.
CTest output is `build\arm64\Testing\Temporary\LastTest.log`.
Build-directory artifacts remain local. The tables preserve the measured results in this README.
Real IME sessions and million-file desktop workloads remain unverified.

### Reusable-control milestone

The September 11, 2026 native ARM64 Release build adds `xui_gallery.exe`.
Both applications use the static MSVC runtime and Windows system libraries. No third-party dependency is necessary.
The existing large-list tests still require zero allocations in their interaction sequences.

| Release executable | Bytes | KiB |
| --- | ---: | ---: |
| `build\arm64\Release\xui_demo.exe` | 301,056 | 294 |
| `build\arm64\Release\xui_gallery.exe` | 210,432 | 205.5 |

The browser adds 4.5 KiB to the performance-milestone executable.
The final two-second gallery sample recorded **0 custom paints** and **0 ms** of process CPU time.
The browser sample also recorded **0 custom paints** and **0 ms** of process CPU time.
These counter deltas are observations, not proof of zero CPU use.
The final test output is `build\arm64\Testing\Temporary\LastTest.log`.

All six CTest programs passed. A complete repetition passed each program three times.
The new tests cover control state, public window lifetime, gallery actions, native input, keyboard navigation, and retained-provider invalidation.
Startup failure and callback failure tests also require a later window to run on the same thread.

Window-only captures of both gallery themes received a visual inspection.
The captures are `build\arm64\gallery-dark.png` and `build\arm64\gallery-light.png`.
These captures do not constitute a physical keyboard, screen-reader, or real IME session.

External UIA `SetFocus` calls on native EDIT returned success without a stable focus change in some automated runs on this Windows build.
The gallery smoke test uses `WM_NEXTDLGCTL` for native field focus and checks the result through UIA.
`Window::focus` uses the same native focus operation. Custom-control UIA focus actions have automated coverage.
Direct native-proxy focus transitions still require Windows interoperability work.

At this earlier milestone, the form host did not provide a general scroll container or a `FileList` adapter.
It does not provide runtime tree replacement, C bindings, a reactive property system, or a custom text engine.
Real IME sessions, mixed-DPI monitor transitions, high-contrast sessions, hardware device loss, and complete screen-reader behavior remain unverified.

### Public-list migration

The browser now runs through the same public `Window` host as the gallery.
The September 11, 2026 native ARM64 Release artifacts have these sizes:

| Release executable | Bytes | KiB |
| --- | ---: | ---: |
| `build\arm64\Release\xui_demo.exe` | 372,736 | 364 |
| `build\arm64\Release\xui_gallery.exe` | 281,600 | 275 |

The browser adds 70 KiB to the previous control milestone. The gallery adds 69.5 KiB.
Both executables include the reusable list and task backend. No third-party dependency is necessary.

All six CTest programs passed in the final complete run.
The window tests include a separate public-list application and an external UIA client.
The allocation test includes a public selection callback during every interaction sequence.
The 100,000-row and 1,000,000-row sequences both produced **zero allocations** across 20,000 operations.
Their measured durations were **19.16 ms** and **39.58 ms**.

The recorded browser smoke run submitted **10 rows for 3,001 items** after resize.
Its two-second idle interval produced **0 custom paints** and **0 ms** of measured process CPU time.
Its memory sample contained **37,937,152 private bytes** and **61,784,064 working-set bytes**.
The maximum native character dispatch was **11.90 ms** during filter and refresh work.
Shutdown after observed worker activity took **49.87 ms**.
These measurements are observations, not latency or memory guarantees.

The final dark and light browser captures received a visual inspection.
They show the retained header, search field, shortcut hint, column separator, vector icons, list surface, and status label.
Local artifacts are:

- `build\arm64\browser-public-dark.png`
- `build\arm64\browser-public-light.png`
- `build\arm64\browser-public-results.txt`
- `build\arm64\performance-public-results.txt`
- `build\arm64\public-migration-ctest.txt`

One repeated gallery run failed its strict focus assertion after a native EDIT value update.
The unchanged gallery test then passed three consecutive runs and the final complete suite.
The native-proxy focus caveat remains. The assertions did not change.
Real IME sessions, mixed-DPI transitions, high-contrast sessions, and complete screen-reader behavior still require manual coverage.
The API does not include C bindings, a general scroll container, or arbitrary row templates.
