# Explorer validation history

This archive preserves earlier implementation reports and measurements from the former root README.
Results, limitations, tool paths, and artifact paths describe those runs, not the current checkout.
Local `build` artifacts are not part of the repository and can be absent.
Use [CONTRIBUTING](../../CONTRIBUTING.md) for current build instructions.

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
