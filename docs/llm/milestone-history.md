# Earlier milestone results

This archive preserves earlier implementation reports and measurements from the former root README.
Results, limitations, tool paths, and artifact paths describe those runs, not the current checkout.
Local `build` artifacts are not part of the repository and can be absent.
Use [CONTRIBUTING](../../CONTRIBUTING.md) for current build instructions.

## Recorded results

### Folder suggestions

The ARM64 Release implementation passed the full 21-program native suite in 197.35 seconds.
The phase 4 regression also passed C# FDD, NativeAOT, and Rust checks.
All five desktop clients passed normal-operation and callback-failure runs.
The final explorer run passed 460 assertions, including native suggestion actions through UIA.
The dark and light popup captures contain only the owned test windows.

The source tests used 307 real fixture folders and injected 100,000-entry directory cursors.
The directory cursor stopped after 64 retained folders. The file-only cursor stopped after 4,096 entries.
Both cursors closed at the budget boundary. The retained real result used 10,176 bytes of string and vector capacity.
This figure excludes allocator metadata and native LISTBOX storage.
A blocked provider received 100,000 replacement requests. It ran only the blocked request and the latest request.
The active-request peak and pending-request peak were both one.

| Measurement | Result |
| --- | --- |
| Cold native popup, including debounce | 124.49 ms |
| Warm native popup, including debounce | 104.82 ms |
| Cold popup in the final explorer desktop run | 165.70 ms |
| 100 native `WM_CHAR` messages with a blocked provider | 1.24-ms mean, 4.57-ms maximum |
| Close with a blocked provider | 30.04 ms |
| Host paint count during the closed-popup idle check | No increase |
| Direct2D targets with the popup open | One |

The gate-based tests establish cancellation order without a timing assumption.
They cover stale delivery, context changes, closed endpoints, and closure before the provider returns.
The UI tests cover keyboard acceptance, immediate Enter during debounce, mouse acceptance, UIA selection, and the native accessible default action.
They also cover both pane bases, Unicode, independent search, native undo, read-only state, disabled state, scroll selection, and theme changes.
IME coverage uses synthetic composition messages. Physical IME candidate sessions, mixed-DPI monitor moves, and slow remote servers still need manual coverage.
The native DPI-transition test checks dismissal at the current monitor DPI.

Three fresh processes per build used the same 60-item fixture and `tests\measure-browser.ps1`.
The following values are medians. These small differences do not establish a memory reduction.

| Small-folder phase | Before: private commit / private working set | After: private commit / private working set |
| --- | --- | --- |
| First paint | 29.02 / 14.50 MiB | 28.78 / 14.32 MiB |
| Warm | 29.48 / 15.23 MiB | 29.48 / 15.20 MiB |
| Idle | 29.43 / 15.20 MiB | 29.43 / 15.16 MiB |

First-paint latency changed from 193.74 ms to 200.41 ms. Both builds retained one target and nine threads at first paint.
No suggestion worker or popup existed during those first-paint measurements.
The two optional address peers added two GDI brushes before first use.
Before address focus, three one-second idle samples recorded zero additional host paints and 0.00-ms process CPU increases.
The CPU counter has limited resolution.
The previous recorded explorer executable was 588,800 bytes. The new executable is 628,224 bytes, an increase of 39,424 bytes.
The explorer still deploys as one executable and requires no new third-party runtime.

One separate desktop run measured the first popup with a UIA client attached.
Private commit changed from 30,683,136 to 31,592,448 bytes. Private working set changed from 15,282,176 to 16,453,632 bytes.
These totals include first-use worker, font, popup, and native accessibility costs. They are not an isolated allocation count for the list.
No result supports a speed comparison with Windows Explorer.

The raw results are in `build\autosuggest\before.json`, `after.json`, `comparison.json`, `native.log`, `desktop-final.log`, and `idle-cpu.json`.
The captures are `build\autosuggest\browser-dark.png`, `browser-light.png`, and `browser-right.png`.
The full regression logs are in `build\phase4` and `build\autosuggest\regression.log`.

To reproduce the suggestion checks, run:

```powershell
.\build\arm64\Release\xui_suggestion_tests.exe
.\build\arm64\Release\xui_explorer_smoke.exe .\build\arm64\Release\xui_demo.exe --suggestion-capture
.\tests\measure-browser.ps1 -Runs 3 -Output build\autosuggest\after.json
.\tests\phase4.ps1 -SkipMeasurements
```

The capture command writes BMP files. The recorded PNG files contain the same pixels.

### Explorer navigation, tabs, and panes

The September 11, 2026 native ARM64 Release build passed all 17 CTest programs in 161.57 seconds.
This run includes the existing native EDIT, gallery, image, ABI, scrolling, and zero-allocation interaction tests.
The explorer state test passed 57 assertions.
The desktop explorer test passed real keyboard, context-menu, long Unicode path, tab, pane, divider, and UIA lifetime checks.
The split-window test passed eight creation and disposal cycles.

The C# framework-dependent and NativeAOT tests each passed 17 wrapper assertions.
Rust passed five wrapper tests, one ABI test, and two compile-fail documentation tests.
Rust formatting and Clippy checks passed.
All five normal binding UIA clients passed. All five deliberate callback-failure clients also passed.
The earlier foreground-dependent failures did not recur in this final run.
No unrelated window or desktop setting was changed.

Build and test records:

- `build\explorer\build.log`
- `build\explorer\native-tests.log`
- `build\explorer\bindings-tests.log`
- `build\explorer\final-memory.json`
- `build\explorer\final-measurements.log`

The five fresh-process measurements used 60 files, 96 DPI, and the original 924×641 client extent.
The table reports medians. Latency ranges show the minimum and maximum across those runs.

| Phase | Private commit | Private working set | Total working set | Latency |
| --- | ---: | ---: | ---: | ---: |
| First painted frame | 28.78 MiB | 14.40 MiB | 45.05 MiB | 199.85 ms, range 164.00–220.02 |
| Warm selection and filter update | 29.46 MiB | 15.27 MiB | 45.95 MiB | 17.40 ms, range 16.95–18.34 |
| Idle | 29.42 MiB | 15.22 MiB | 45.90 MiB | No additional paints |
| Repeated filter stress | 30.14 MiB | 15.87 MiB | 46.59 MiB | — |

The measured executable contains 524,288 bytes.
The previous phase's recorded executable contained 419,840 bytes, a difference of 104,448 bytes.
The explorer retains the static CRT and adds no third-party runtime.
These measurements do not establish a universal memory advantage over another file manager.

#### Pane and tab costs

The single-pane process had nine threads and 250 handles after startup.
Activating the second pane added one worker thread and one handle.
Its median commitment increase was 270,336 bytes, with a range of 233,472–544,768 bytes.
The base process already owns the fixed, hidden second-pane control tree.
This delta is not the complete cost of constructing a pane.

Fifteen additional inactive tabs added no native peers, render targets, threads, or handles.
Their median process-commitment increase was 1,556,480 bytes, with a range of 1,515,520–3,751,936 bytes.
That increase includes intervening scans and allocator retention. It is not an intrinsic per-tab allocation.
The ARM64 `Tab` object occupies 40 bytes. Each `Location` object occupies 136 bytes.
Those object sizes exclude vector allocations, strings, and retained history entries.

After tab closure and 40 folder transitions, median commitment was 33.85 MiB.
The range was 32.45–34.73 MiB. Median private working set was 19.50 MiB.
The process returned to nine threads and 250 handles after the right pane stopped.
Both final idle samples had the same paint count. Each process retained one shared render target.
The repeated window tests verified that controlled resources returned to zero after closure.

Larger initial extents exposed a separate rendering allocation increase during the first populated Direct2D frame.
Experiments at 1000×700 and 1100×720 reached approximately 94 MiB of private commitment.
The final comparison retains the original extent. Resizing remains supported, but larger windows can consume substantially more memory.
The platform allocation cause is not fully attributed.
`build\explorer\memory.json` retains the earlier larger-window measurements.

The rendered single-pane, dual-pane, and 16-tab screenshots are beside `final-memory.json`.
The inspected dual-pane images show complete navigation labels and the active pane's tab position.
Real UNC-server outages, denied-access ACL fixtures, dedicated touch hardware, and mixed-monitor DPI transitions remain unverified.
The tests cover UNC root parsing, injected failures, extended local paths, simulated DPI changes, and mouse capture cancellation.
Desktop-wide UIA focus subscription remains optional for the platform limitation described in the test section.
New workspace controls currently have a public C++ API only. The existing C ABI and bindings remain compatible.

### Images and bounded resources (phase 3)

The September 11, 2026 native ARM64 Release build passed all twelve CTest programs in the final 138.28-second run.
The instrumented measurement run also passed all twelve programs in 142.51 seconds.
The eight existing programs remain active. The model tests retain their zero-allocation interaction assertions.
`build\phase3\ctest.log` contains the measurements. `build\phase3\ctest-final.log` contains the final test results.
`build\phase3\build.log` contains the build output.
The build produced no compiler warnings.

| Executable | Phase 2 bytes | Phase 3 bytes | Change |
| --- | ---: | ---: | ---: |
| `xui_demo.exe` | 392,704 | 419,840 | +27,136 |
| `xui_gallery.exe` | 317,440 | 347,648 | +30,208 |
| `xui_images.exe` | — | 390,656 | New sample |

The PE machine type is `AA64` (native ARM64).
The Release targets retain `/MT` and link-time optimization.
The import inspection shows Windows system DLLs and no dynamic MSVC runtime.
WIC loads through system COM. The application adds no third-party runtime.
`build\phase3\images-pe.txt` and `sizes.txt` contain the binary evidence.

#### Resource stress and rendered workload

The resource test decoded 656 images and performed 514 cache evictions.
Its 12 rejected operations cover file, codec, queue, and budget errors.
It retired 68 cancelled jobs, including the deliberately full queue.
The exact CPU peak and controlled GPU peak both reached 8,388,608 bytes.
All reservations and live resource counters returned to zero after release.

The window workload used 160 generated 1,024-by-1,024 PNG files and a 20,000-item immutable model.
It retained 40 tile groups, 40 `Image` controls, and 40 captions.
It performed 120 input pairs across four source generations.
Each pair sent Page Down and a mouse-wheel message through the native viewport.
Each source replacement retained the same fixed tile pool.

| Instrumented complete run | Result |
| --- | ---: |
| Decoded images | 1,595 |
| Decoded-cache hits | 309 |
| Pixel peak, including reservations | 8,388,608 bytes |
| Controlled bitmap peak | 1,310,720 bytes |
| Pixel bytes after each of three folder replacements | 8,388,608 |
| Bitmap bytes at those samples | 1,310,720 |
| Cached entries at those samples | 128 |
| Cumulative evictions at those samples | 348 / 721 / 1,094 |
| Native input pair, mean / maximum | 23.11 / 75.38 ms |
| Page-to-ready observation, mean | 280.35 ms |
| Decode-worker service, total / maximum | 30,449.6 / 53.61 ms |
| Bitmap upload calls, total / maximum | 518.14 / 5.58 ms |
| Pixel bytes before explicit unload | 8,388,608 |
| Bitmap bytes before explicit unload | 1,048,576 |
| Pixel, reservation, and bitmap bytes after unload and cache eviction | 0 |
| Pixel, reservation, and bitmap bytes after window closure | 0 |
| Idle paint delta | 0 |
| Close with a blocked decoder and a live 4 MiB reservation | 22.75 ms |

The decode-worker timer includes file-version checks, cache lookup, and WIC calls. It excludes queue wait.
The upload timer measures the CPU call to `CreateBitmap`, not GPU completion time.
The page-to-ready observation includes a 20 ms test timer and settling intervals.
These figures are measurements, not latency guarantees.
The separate resource tests use deliberate worker gates. Their maximum decode-worker time is not an interactive latency measurement.

The image host batches native placement by parent HWND.
It skips unchanged native geometry, enabled state, and accessibility snapshots.
The renderer retains the existing single target per window.
The pixel tests cover the complete first image, viewport-relative native bounds, and the header clip.
The capture is `build\phase3\images-workload.bmp`. A PNG copy is also available.

#### Ordinary browser overhead

The same 60-item protocol ran in three fresh processes before and after the image work.
The measurements ran separately from builds and desktop tests.
The source data and viewport size stayed the same.
`build\phase3\browser-before.json` and `browser-after.json` contain the samples.

| Metric, median | Before | After |
| --- | ---: | ---: |
| Idle private commit | 27.87 MiB | 27.86 MiB |
| Idle private working set | 13.42 MiB | 13.45 MiB |
| Idle total working set | 44.06 MiB | 44.10 MiB |
| First painted 60-item view | 157.48 ms | 172.32 ms |
| Protocol scroll/key/redraw stress iteration | 21.01 ms | 18.68 ms |
| Handles | 250 | 250 |
| Threads | 9 | 9 |
| Direct2D targets | 1 | 1 |
| Idle paint delta | 0 | 0 |

The startup sample increased by 14.84 ms. The stress sample decreased by 2.33 ms.
The small sample size and desktop scheduling do not establish a latency improvement or a fixed startup cost.
The ordinary browser starts no image worker and owns no decoded pixels or image bitmaps.
Process memory remains distinct from both resource ledgers.

#### Remaining scope

Physical device loss, real IME composition, Narrator speech, and mixed-monitor behavior remain manual checks.
The new device-loss tests use the private injected seam.
The image path has no orientation transform, color-profile conversion, animation, file watcher, or content hash.
The source scan is bounded but not paged. The image grid has no selection, drag action, or destructive file operation.

Phase 3 adds no C ABI, C# wrapper, or Rust wrapper.
The next phase can expose image creation, source assignment, dimensions, status, error text, reload, unload, and statistics.
That boundary must preserve UI-thread ownership, exception translation, owned string lifetimes, and cancellation on control or window disposal.
The process-wide budgets must remain shared across native and language-wrapper callers.

### Windows integration (phase 2)

The September 11, 2026 native ARM64 Release build passed all eight CTest programs.
Each program passed three consecutive runs: 24 executions in 170.28 seconds.
The seven existing programs retained their assertions. The new program covers native integration.

| Program | Three-run elapsed range, seconds |
| --- | ---: |
| `xui_core_tests` | 0.02-0.53 |
| `xui_control_tests` | 0.02-0.30 |
| `xui_performance_tests` | 1.43-1.65 |
| `xui_windows_smoke` | 6.92-7.40 |
| `xui_gallery_smoke` | 13.74-14.45 |
| `xui_window_tests` | 14.26-15.31 |
| `xui_scroll_tests` | 13.13-19.20 |
| `xui_native_integration_tests` | 1.05-7.59 |

The native focus reproduction returned `S_OK` and exposed keyboard focus while the real host remained minimized.
The message trace showed the native UIA proxy activate the child EDIT HWND.
`NativeEditBridge::subclass` now restores activation to the root HWND and focus to EDIT during that transaction.
The regression checks the restored host, stable native focus, a native focus event, and subsequent `SendInput` text.
It does not substitute host messages for external UIA focus.

The native text provider remains the Windows provider.
The clipping adapter declares provider-owned focus and releases its HWND provider reference at destruction.
Custom controls and lists also declare provider-owned focus.
The native font cache avoids same-DPI replacement during theme changes.
The composition guard now includes native end processing.
Rejected select-all focus no longer changes a disabled input's selection.
Clipboard calls without a live owner fail before any clipboard change.

List property events now compare the previous and current immutable snapshots.
They cover list names, enabled state, selection, item focus, scroll percentage, viewport fraction, and scroll availability.
Eight repeated requests for the same scroll endpoint produce no extra scroll-property events.
Filtering still preserves hidden selection IDs, and hidden providers still reject actions.
This phase added no per-row provider cache, per-control render target, or continuous render loop.

The controlled comparison used three fresh processes before the changes and three after them.
Both groups used the unchanged `tests\measure-browser.ps1` protocol and its 60-file fixture.
The client area remained 924 by 641 DIPs, at 96 DPI, with the initial dark theme.
The compiler, Windows SDK, static runtime, and build configuration remained unchanged.
Values are medians with minimum-maximum ranges. Commitment, private residency, and total residency are separate counters.

| Counter | Before phase 2 | After phase 2 |
| --- | ---: | ---: |
| First-paint private commitment, MiB | 27.79 (27.77-27.80) | 27.79 (27.77-27.81) |
| Warm private commitment, MiB | 27.85 (27.84-27.86) | 27.89 (27.85-28.03) |
| Idle private commitment, MiB | 27.82 (27.81-27.82) | 27.85 (27.85-27.99) |
| Idle private working set, MiB | 13.34 (13.18-13.47) | 13.53 (13.52-13.85) |
| Idle total working set, MiB | 43.98 (43.87-44.12) | 44.18 (44.16-44.50) |
| Stress private commitment, MiB | 28.01 (28.00-28.02) | 28.05 (28.02-28.16) |
| Stress private working set, MiB | 13.68 (13.54-13.69) | 13.75 (13.70-14.04) |
| Stress total working set, MiB | 44.37 (44.28-44.40) | 44.42 (44.40-44.72) |
| Peak commitment through stress, MiB | 28.13 (28.12-28.16) | 28.16 (28.16-28.26) |
| Startup through first paint, ms | 151.09 (148.92-174.88) | 164.21 (164.03-168.28) |
| Selection + scroll + full paint, ms | 17.99 (16.48-21.71) | 18.59 (17.80-28.90) |
| Render targets | 1 | 1 |
| Idle process handles / threads | 250 / 9 | 250 / 9 |
| Idle GDI / USER objects | 19-21 / 26-27 | 19-21 / 26-27 |
| Stress GDI / USER objects | 21-23 / 26-27 | 21-23 / 26-27 |
| Custom paints during the two-second idle interval | 0 | 0 |
| Browser executable, bytes | 391,168 | 392,704 |
| Gallery executable, bytes | 314,880 | 317,440 |

Idle commitment increased by about 0.03 MiB. Private residency increased by about 0.19 MiB.
The small samples cannot attribute every resident page or establish a general speed improvement.
The browser grew by 1,536 bytes. The gallery grew by 2,560 bytes.
No working-set trimming, stack reduction, accessibility removal, or software-rendering override contributed to these results.
PE inspection reported ARM64 and Windows system imports, without a dynamic MSVC runtime or third-party runtime.

Server-only lifecycle checks still require one live target and zero targets after closure.
Across the repeated runs, ordinary closed windows used 247-249 process handles.
Closed ScrollView windows used 261-263 handles. Both cases retained 16 GDI objects and two USER objects.
The existing two-handle allowance remained unchanged.
Combined UIA client/server diagnostics include Windows client pools and do not replace the server-only limit.
Cancelled refresh, blocked-loader closure, and off-thread disposal cases passed.

The separate model benchmark retained zero allocations for 20,000 interactions at 60, 100,000, and 1,000,000 rows.
The recorded interaction times were 6.33, 33.71, and 54.77 ms.
The 100,000-row and million-row name/index caches remained 4,400,022 and 44,000,022 bytes.
The million-row benchmark process retained 239.70 MiB of private commitment, including its source data and views.

To repeat the phase checks, run these commands from the repository:

```powershell
$tools = "C:\Program Files\Microsoft Visual Studio\2022\Preview\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
& "$tools\cmake.exe" --build build\arm64 --config Release --parallel 4
& "$tools\ctest.exe" --test-dir build\arm64 -C Release --output-on-failure --repeat until-fail:3
& .\tests\measure-browser.ps1 -Runs 3 -Output build\phase2\repeat-memory.json
& .\build\arm64\Release\xui_performance_tests.exe --benchmark
```

The measurements must run separately from builds, desktop tests, and benchmarks.
Raw launch, first-paint, warm, idle, and stress samples remain in `build\phase2\before.json` and `build\phase2\after-final.json`.
Other evidence includes `ctest-final.txt`, `test-details-final.log`, `benchmark.txt`, `dependencies.txt`, and `executables.json` in that directory.
`native-focus-trace.txt` and `focus-activation-detail.txt` contain the failing native activation sequence.
The trace code is not part of the framework.

#### Automated coverage and manual gaps

The environment was Windows `10.0.28637.0` on ARM64, with Windows SDK `10.0.26100.0`.
The configured keyboard was `0409:00000409` (English, United States).
The language list also contained `qps-ploc`, `ja`, and `zh-Hans-CN`, without input-method entries.
TSF enumeration found one Japanese and two Chinese keyboard profiles. All three were disabled and inactive.
The compatibility `ImmIsIME` result for the English layout was true. This result does not prove a usable Japanese or Chinese composition session.
No language, keyboard profile, contrast setting, or display setting changed.

| Area | Automated result | Remaining physical or manual coverage |
| --- | --- | --- |
| External native focus | Automation-tree discovery, direct UIA `SetFocus`, minimize/restore, native `EVENT_OBJECT_FOCUS`, stable focus, queued BMP and surrogate input | Foreground restrictions across integrity levels and other Windows builds |
| Accessibility events | External Invoke, Toggle, name, enabled, native Value, custom focus properties, list selection, structure, and scroll events | Desktop-wide `AutomationFocusChanged` subscription and Narrator announcements |
| Native provider lifetime | Native Value disconnection and no focus movement after server exit. Custom providers reject stale actions | Native `SetFocus` can return `S_OK` after exit, despite no focus change |
| IME | Real EDIT retains default IME dispatch. Synthetic boundaries cover commit suppression, one final callback, Tab, Enter, and shortcut guards | Japanese conversion/candidate selection and Chinese Pinyin commit/cancel, focus loss, and candidate placement |
| Clipboard | Unicode copy/paste/cut/undo and surrogate-safe limit checks on a private window station | Remote clipboard, clipboard managers, and cross-integrity clipboard access |
| DPI | Exact font sizes, layout, reveal, and clipping at 96/120/144/192 DPI through messages | Physical mixed-DPI monitor moves, candidate placement, and display reconnect |
| High contrast | Explicit mode, system palette mapping, selection/focus colors, and retained native-caption pixel regression | User-selected contrast schemes and screen-reader use |
| Graphics loss | Injected `D2DERR_RECREATE_TARGET`, replacement frame, retained layouts, one target, and idle recovery | Actual GPU removal, driver reset, and remote-session reconnect |

Desktop-wide focus-event registration blocked in an unrelated `github` process, whose wait chain continued into `explorer`.
No unrelated process was stopped.
An isolated-desktop attempt did not provide a usable visible external UIA tree.
The default suite therefore uses process-filtered native focus events and scoped UIA property events.
The gallery probe accepts `--focus-events` for a later desktop-wide UIA event run on a responsive desktop.
The default suite does not claim that this optional run passed.
The tests did not start Narrator on the shared desktop.
Synthetic IME messages are not a real IME test.

The phase adds no images, bindings, C ABI, custom text engine, or application-owned HWND plumbing.

### Reusable sizing and scrolling (phase 1)

The September 11, 2026 ARM64 Release build passed all seven CTest programs.
The final suite took 46.51 seconds. The new scroll suite also passed three consecutive runs.
The gallery retained its existing UIA, keyboard, native text, callback, and idle assertions.
Its test now protects its own window from unrelated desktop occlusion.
The synthetic thumb gesture runs on one UI turn to prevent unrelated physical mouse messages between its three messages.

The server-only lifecycle cases preserve the existing two-handle allowance after initialization.
Both ordinary trees and ScrollView trees require one live render target and zero retained targets after closure.
The scroll/UIA suite checks cached layouts, partially clipped native bounds, native focus reveal, and native caption pixels after a high-contrast repaint.
It uses a persistent UIA apartment and a message pump during provider teardown.
Its process-handle diagnostics include the UIA client and Windows thread pools, unlike the separate server-only resource limit.

The controlled browser comparison used three fresh processes before this phase and three after the final code changes.
Both groups used `tests\measure-browser.ps1`, the same 60-file fixture, the same client size, and 96 DPI.
Values are medians with minimum–maximum ranges. Private commitment and private residency are separate counters.

| Counter | Before phase 1 | After phase 1 |
| --- | ---: | ---: |
| First-paint private bytes, MiB | 27.70 (27.69–27.72) | 27.78 (27.59–27.82) |
| Warm private bytes, MiB | 27.75 (27.74–27.77) | 27.86 (27.85–27.86) |
| Idle private bytes, MiB | 27.71 (27.69–27.72) | 27.82 (27.82–27.83) |
| Idle private working set, MiB | 13.46 (13.39–13.46) | 13.43 (13.25–13.52) |
| Stress private bytes, MiB | 27.88 (27.88–27.89) | 28.02 (27.99–28.05) |
| Stress private working set, MiB | 13.66 (13.59–13.71) | 13.74 (13.58–13.77) |
| Selection + scroll + full paint, ms | 21.38 (18.34–25.54) | 16.77 (16.49–17.86) |
| Startup through first paint, ms | 154.18 (115.83–154.58) | 165.16 (108.09–184.46) |
| HWND render targets | 1 | 1 |
| Idle process handles / threads | 250 / 9 | 250 / 9 |
| Browser executable, bytes | 368,128 (359.5 KiB) | 391,168 (382 KiB) |
| Gallery executable, bytes | 275,968 (269.5 KiB) | 314,880 (307.5 KiB) |

Idle commitment increased by about 0.11 MiB. Private working-set ranges overlap.
The cached layouts add retained text resources, while the broader gallery adds controls and native peers.
The browser retains one target and unchanged idle handle and thread counts.
These small samples do not establish a general speed improvement or attribute every Windows allocation.
No working-set trimming, thread-stack change, accessibility reduction, or forced software rendering contributed to these results.

The benchmark retained zero allocations for 20,000 model interactions at 60, 100,000, and 1,000,000 rows.
One separate run recorded 4.44, 29.98, and 74.42 ms for those sequences.
The 100,000-row and million-row name/index caches remained 4,400,022 and 44,000,022 bytes.
This phase does not change immutable snapshots, filtering, logarithmic lookup, or independent list focus and selection.

The browser and gallery ran as native desktop applications.
The visual inspection covered the browser and gallery in dark mode, plus the scrolled gallery in light and explicit high-contrast modes.
The inspection found a nested native-caption paint-order error. The backend now paints nested native pixels last, and a pixel regression covers this case.
No global Windows theme or display setting changed.

Local evidence:

- `build\phase1-ctest.txt` and `build\phase1-scroll-repeat.txt`
- `build\phase1-benchmark.txt`
- `build\memory\phase1-before.json` and `build\memory\phase1-after-final.json`
- `build\memory\phase1-after-summary.txt`
- `build\gallery-phase1-dark-top.png`, `gallery-phase1-dark-bottom.png`, `gallery-phase1-light-bottom.png`, and `gallery-phase1-contrast-bottom.png`
- `build\memory\phase1-after-final.json-1.png` through `phase1-after-final.json-3.png`

Real IME sessions, Narrator, physical mixed-DPI moves, and hardware device loss remain outside this phase.
ScrollView retains all ordinary controls. It has no arbitrary-content virtualization, automatic line wrapping, or runtime content replacement.
The public API remains C++. Later C, C#, and Rust adapters need stable handles, UI-thread rules, UTF-16 transfer rules, and callback lifetime rules.
Those adapters can use the same sizing setters, DIP scroll offsets, native EDIT bridge, and backend-only accessibility providers.
This phase adds no C ABI, image API, or custom TSF implementation.

### Memory and presentation (single-target baseline)

The September 11, 2026 comparison used native ARM64 Release builds on Windows `10.0.28637`, at 96 DPI.
The compiler, SDK, static runtime, folder path, 924-by-641-DIP client area, and initial dark theme stayed the same.
The baseline is `ab9070b`, before the memory changes. Each binary ran in five fresh processes.
Values are medians, with minimum–maximum ranges. One MiB is 1,048,576 bytes.

| Counter or workload | Before | After |
| --- | ---: | ---: |
| First-paint private bytes, MiB | 29.65 (29.59–30.25) | 27.70 (27.69–27.72) |
| Warm private bytes, MiB | 29.99 (29.85–31.17) | 27.75 (27.75–27.76) |
| Idle private bytes, MiB | 29.97 (29.82–31.14) | 27.70 (27.70–27.72) |
| Idle private working set, MiB | 15.93 (15.78–17.04) | 13.39 (13.31–13.50) |
| Idle total working set, MiB | 46.52 (46.36–47.62) | 43.99 (43.91–44.12) |
| Private bytes after refresh/theme stress, MiB | 31.25 (30.64–32.19) | 27.87 (27.86–27.89) |
| Peak commit through stress, MiB | 31.40 (30.83–32.41) | 27.98 (27.96–28.03) |
| Selection + scroll + forced full paint, ms per iteration | 117.80 (110.57–123.61) | 18.56 (17.21–18.93) |
| Startup through the measured first paint, ms | 290.48 (227.69–304.92) | 183.83 (171.97–197.28) |
| HWND render targets | 9 | 1 |
| Process handles at idle | 258 | 250 |
| Threads at idle | 9 | 9 |
| Browser executable, bytes | 372,736 | 368,128 |
| Gallery executable, bytes | 281,600 | 275,968 |

The private working set decreased by about 16%. Private commitment decreased by about 8%.
The redraw workload was about 6.3 times faster. It includes Win32 dispatch and presentation, not only model operations.

Idle GDI counts stayed at 19–21. USER counts changed from 34–35 to 26–27.
Both builds recorded 0–1 extra custom paints during each two-second idle interval on the shared desktop.
The lifecycle regression also requires a quiet interval after pending window events finish.

The change removes eight target/brush pairs from the browser, not its controls or accessibility.
MSVC Release builds also use link-time optimization. There is no new runtime dependency.
Worker count, cancellation, off-thread disposal, text quality, and the default Direct2D device selection stay unchanged.
No working-set trimming or reduced thread-stack sizes contributed to these results.

A list hover now redraws other visible controls in the same frame. It still draws only visible list rows.

**Protocol.** `tests\measure-browser.ps1` creates 60 empty files named `Entry-000.txt` through `Entry-059.txt`.
The fixture path is `build\memory\fixture-60`. The script rejects an existing fixture and deletes only its own fixture.
Each process uses the same topmost window position to reduce unrelated occlusion.
The first-paint sample requires all 60 rows, completed delivery, and a paint.

After 750 ms, the script performs 30 selection/scroll/full-redraw iterations.
It waits 250 ms before the warm sample, then two seconds before the idle sample.
Stress consists of 60 text replacements, 20 refreshes, and 20 theme changes, followed by completed delivery and two seconds of rest.
The launch sample is also available, but its exact startup stage depends on scheduling.

The runner uses HWND messages, not a UIA tree or retained providers.
It does not start an inspector. Other desktop clients and global UIA listeners remain outside its control.

Private bytes measure commitment. The working set measures resident pages, including pages marked shared.
`QueryWorkingSet` supplies the separate private-resident counter. About 30.6 MiB of resident pages remain marked shared in both builds.

The reported 16 MB user observation has no identified counter or protocol, so it is not the comparison baseline.
These measurements do not attribute individual Windows, driver, or font-cache allocations.

Run these commands from the repository root, separately from builds and other desktop tests:

```powershell
.\tests\measure-browser.ps1 -Runs 5 -Output build\memory\sample.json
.\build\arm64\Release\xui_performance_tests.exe --benchmark
.\build\arm64\Release\xui_window_tests.exe --resources
```

`-Executable` selects another preserved browser binary. `-Screenshots` also captures the owned window after the idle sample.
The script prints medians and ranges and saves the individual samples as JSON.
The optional resource probe reports heap blocks, heap-region commitment, and new process-handle types.
Heap-region totals exclude direct virtual allocations and some large heap allocations.
The regression does not impose a total-memory or timing threshold.

Local evidence is in `build\memory\before-private-ws.json`, `after-private-ws.json`, and `private-ws-summary.txt`.
The build directory also contains `perf-before-final.txt`, `perf-after-final.txt`, `resources-final.txt`, and `ctest-final.txt`.

The lifecycle probe measured 3.79 MiB of busy heap blocks and 35.87 MiB of process-private commitment with its stress window open.
After final closure, these values were 0.59 MiB and 22.54 MiB.
These are diagnostic-window samples, not browser comparison values.
The remaining commitment is not attributed to individual libraries or drivers.

The baseline lifecycle test retained 19 `DwmDxBltEvent_*` handles per repeated window with 18 standard controls and one list.
A standalone Win32/Direct2D child-target probe reproduced the event retention.
The single-target host removed this growth. The regression requires zero retained XUI targets.
These diagnostic artifacts remain local build outputs, not source dependencies.

**Large data.** Five interleaved benchmark processes used the same synthetic data and toolchain.
The new length pass changes retained capacity, not the item array or logarithmic identity lookup.

| Rows | Snapshot index/cache bytes, before → after | Snapshot ms, before → after | 20,000 interaction sequences, ms, before → after |
| ---: | ---: | ---: | ---: |
| 60 | 3,320 → 2,662 | 0.043 (0.018–0.058) → 0.026 (0.016–0.043) | 7.12 (5.32–8.86) → 6.71 (3.46–7.74) |
| 100,000 | 5,030,176 → 4,400,022 | 35.89 (30.28–46.34) → 33.09 (30.30–40.26) | 33.92 (21.65–41.65) → 31.56 (28.91–40.15) |
| 1,000,000 | 55,628,012 → 44,000,022 | 428.12 (372.12–477.25) → 411.59 (242.78–434.76) | 53.65 (42.25–66.67) → 56.29 (43.99–76.88) |

The million-row cache saves 11.09 MiB of retained capacity.
Its process-private median decreased from 250.82 to 239.70 MiB. Its working-set median stayed near 244.4 MiB.
The item strings, paths, and immutable all-row index remain storage costs.

Every interaction case still requires zero allocations.
CPU ranges overlap, so these results do not establish a general model-speed improvement.
The million-row empty query increased from a 5.32 ms median to 7.78 ms, with overlapping ranges.
That work stays off the UI thread. Small-directory presentation improved without a measured interactive latency penalty.

Physical mixed-DPI moves, real high-contrast sessions, and hardware device loss still need broader hardware coverage.

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
Build-directory artifacts remain local. These tables preserve the measured results from the former README.
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
