# Task Manager and memory investigations

This archive preserves earlier implementation reports and measurements from the former root README.
Results, limitations, tool paths, and artifact paths describe those runs, not the current checkout.
Local `build` artifacts are not part of the repository and can be absent.
Use [CONTRIBUTING](../../CONTRIBUTING.md) for current build instructions.

## Task Manager verification and measurements

The column update passed all 24 native tests on September 12, 2026, in 262.71 seconds.
The existing mouse resize remains in place, with a shared boundary cursor and keyboard commands.
The column tests passed 335 model assertions, 328 Task Manager desktop assertions, and 14 grid and sample lifecycle assertions.
They cover permutations through 64 columns, retained UIA identities, drag cancellation, keyboard input, narrow viewports, and snapshot persistence.
Native drag tests also exercise simulated 96-, 144-, and 192-DPI layouts. The actual Task Manager desktop run used 96 DPI.
The tests preserve the million-row virtualization, native EDIT, rendering, thumbnail, and ABI checks.
The isolated executable is `build\columns\Release\xui_task_manager.exe` (655,872 bytes).
The full test log is `build\columns\full-tests.log`.
The desktop smoke saves `build\columns\task-manager-columns.bmp` and `build\columns\task-manager-columns-narrow.bmp` under CTest.
These screenshots contain only the test-owned Task Manager window.

The final ARM64 Release verification completed on September 11, 2026.
The full compatibility command passed all 20 native tests in 184.94 seconds, including the previous 17 tests.
C# framework-dependent and NativeAOT tests each passed 17 assertions.
Rust passed six unit/layout tests, two compile-fail examples, Clippy, and formatting.
All five normal UIA clients and all five callback-failure UIA clients passed.

The new tests cover counter math, unavailable values, process identity, filtering, sorting, overflow, and bounded chart history.
A controlled busy fixture verifies CPU scaling, memory allocation, process exit, identity rejection, and confirmed termination.
The desktop smoke covers search, sort, focus, native EDIT Tab, header resizing, pages, pause, resume, and context commands.
Only a fixture started by the test can be terminated during verification.
The window test renders synthetic sources with 100,000 and 1,000,000 rows and checks bounded visible text requests.
It also checks delivery affinity, error retention, cancellation, and pause.
A delayed 1.5-second loader closed its window in approximately 48–60 ms without a UI-thread join.

Run these commands from the repository root:

```powershell
$tools = "C:\Program Files\Microsoft Visual Studio\2022\Preview\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
& "$tools\cmake.exe" --build build\arm64 --config Release --parallel 4
& "$tools\ctest.exe" --test-dir build\arm64 -C Release -R "xui_(task_manager|grid_window)" --output-on-failure
.\tests\phase4.ps1 -SkipMeasurements
.\tests\measure-task-manager.ps1 -Runs 3 -Screenshots -Output build\task-manager\final-measurements.json
.\build\arm64\Release\xui_task_manager_tests.exe --sample-bench
```

Run desktop tests and measurements separately. Concurrent windows can change focus and measurement results.
Final compatibility logs are copied to `build\task-manager\validation`.
Raw measurements, sampler timings, and screenshots are in `build\task-manager`.

The final measurement used three fresh processes, a 1080×780-DIP client area, 96 DPI, and 12 logical processors.
The outer window measured 1096×819 physical pixels. The earlier description incorrectly called the client dimensions the outer dimensions.
Each run observed 544 process rows. The renderer requested 10 visible or boundary rows.
The live interval was one second. Each live observation lasted five seconds.
Paused and minimized observations lasted three and two seconds, respectively.
Values below are medians. CPU is a percentage of total machine capacity, not one processor.

| Counter | Live, one-second interval | Paused |
| --- | ---: | ---: |
| Process private commit | 93.50 MiB | 93.66 MiB |
| Process private working set | 78.98 MiB | 79.27 MiB |
| Process total working set | 109.62 MiB | 109.93 MiB |
| Measured machine-capacity CPU | 0.233% | 0% |
| Additional paints per observation | 3–5 | 0 |

Live CPU ranged from 0.208% to 0.260%.
Each live run had eight threads, 249 handles, and one render target.
Every paused and minimized observation had zero measured CPU and zero additional paints.
These short observations do not establish a long-duration CPU or resource bound.
The first snapshot took 327.03 ms, with a 304.98–345.60 ms range.
Selection, scroll, and a forced full paint took 18.49 ms, with a 17.60–20.01 ms range.
Ten separate sampler-only collections took 28.50–63.80 ms for 544 processes.
That sampler-only process used 946,176–1,024,000 bytes of private commit and 56 handles.

The final `xui_task_manager.exe` is 588,800 bytes. `xui.dll` is 503,808 bytes.
The explorer executable is also 588,800 bytes, an increase of 64,512 bytes from the preceding explorer milestone.
Three fresh explorer runs used the same 60-file protocol and original window extent.
Explorer first-paint private commit was 28.79 MiB, compared with 28.78 MiB before these controls.
Its private working set was 14.34 MiB, compared with 14.40 MiB before.
First paint took 203.64 ms, compared with 199.85 ms before.
Warm selection, scroll, and a forced full paint took 17.91 ms. Idle caused no additional paints.
The larger Task Manager window is not a same-size memory comparison.
Earlier explorer experiments at larger extents also reached approximately 94 MiB of private commit.
The size investigation below locates the main increase in the graphics path.
It does not establish that this footprint is unavoidable or identify every allocator.

Screenshots cover standard and narrow layouts at 96, 144, and 192 injected window DPI.
They include Processes, Performance, selected Details, light colors, and high-contrast colors.
The physical monitor used 96 DPI. Injection tests layout; it does not replace mixed-monitor hardware testing.
The 192-DPI captures use a shorter window to fit the physical display.
Physical IME, mixed-monitor transitions, systems with more than 64 logical processors, and long-duration operation remain manual coverage.
External shell and clipboard gestures also remain manual coverage.
The sample does not claim full Windows Task Manager feature parity.

## Retained-resource reduction: September 13, 2026

`PageView` creates native peers on first selection, not for every inactive page.
Visited peers remain alive, including native EDIT selection and undo.
The renderer releases hidden text layouts and unused scene geometry.
Its native composition buffer now matches the current visible regions instead of retaining its largest historical dimensions.

Three fresh processes per variant used the preserved baseline and the final ARM64 Release build.
Both variants used dark colors, 96 DPI, and identical physical client sizes.
No build or test ran during these measurements. No working-set trim occurred.

| Workload and client size | Private commit before → after, MiB | Private working set before → after, MiB |
|---|---:|---:|
| Gallery initial, 1040×731 | 95.30 → 94.61 | 80.96 → 80.38 |
| Gallery after two 46-page cycles and popup disposal, 1040×731 | 110.40 → 102.31 | 92.30 → 88.02 |
| Live Task Manager, 1080×780 | 94.13 → 94.08 | 79.87 → 79.80 |
| Explorer with 60 owned files, 924×641 | 32.08 → 32.02 | 15.95 → 15.83 |

These values are medians. The gallery reduction is 8.09 MiB of private commit.
The small Task Manager and Explorer differences do not establish a reduction.
Initial gallery peers decreased from 357 to 35. After both cycles, cached text layouts decreased from 594 to 28.
Visited native editors remain allocated. The change does not discard their undo history.

Warm navigation averaged 46.92 → 48.22 ms, with overlapping three-run ranges.
Warm navigation CPU time was 1265.63 → 1250.00 ms per cycle.
First visits averaged 38.73 → 47.24 ms because native peer creation moved from startup to first selection.
Initial process CPU time decreased from 312.50 to 218.75 ms.
Settled gallery and Explorer windows produced zero idle paints.

The gallery workload did not load media or browser engines. Separate native tests cover those engines and their shutdown.
The approximately 64 MiB allocation jump remained in that renderer configuration.
These measurements did not isolate ownership between Direct2D and the driver.
The full native suite passed all 36 tests, including C ABI, native composition, retained editor undo, and real host cleanup.
Raw samples, ranges, hashes, and commands are under `build\controls\memory-evidence`.
The complete report is `build\controls\memory-delivery.json`.

## No-slowdown memory investigation: September 13, 2026

This investigation kept the retained-resource changes above and preserved new baseline binaries under `build\memory-tight\baseline`.
It did not accept another default renderer change.

At startup, a GDI-compatible hardware target saved 2.48–3.31 MiB of median private commit across the three native samples.
Six matched runs did not establish equivalent startup, first-use, and warm performance.
A separate six-run gallery comparison used normal input updates instead of extra forced updates.
Its uncertainty interval also did not establish performance equivalence.
The final build therefore uses the original hardware target configuration.

Other target properties and a raw flip-model probe retained substantial memory in fixed-size experiments.
The flip probe did not exercise application resize cycles or the later `explicit-swap-chain` implementation.
The earlier conclusion that flip-model rendering cannot solve resize retention exceeded the evidence.
The performance rejection concerned the GDI-compatible target, not the explicit-swap-chain branch.
The [dedicated report](../specs/windows-gui-memory.md#audit-of-the-earlier-flip-model-claim) records this correction.
The measurements separate private commit, private working set, process CPU time, and process cycles.
They include per-interaction percentiles and paired-run uncertainty intervals.
The complete evidence and rejection reasons are in `build\memory-tight\delivery.json`.
The measurement script supports `-Performance` with `-Sizes @()` to retain each initial client size.

## Explicit swap-chain branch comparison

The review compared Leonard Hecker's `explicit-swap-chain` commit `4c1f9ab` with its baseline `74bc1f8`.
Both exact source snapshots built as ARM64 Release.
Three matched resize runs per variant showed a transient private-working-set improvement at the first enlargement: **70.22 → 53.13 MiB**.
However, final private commit after shrinking and 30 seconds of idle was **99.80 → 102.32 MiB**.
The branch did not remove retained commit in this workload on the measured Qualcomm driver.
This result does not rule out other configurations or hardware.

Median synchronous fixed-resize time was **25.51 → 30.22 ms**, not an input-to-photon measurement.
No performance threshold stopped the comparison.
The branch passed 32/37 native tests, with unresolved capture and interaction failures.
Owned-window Graphics Capture showed the demo at five sizes.
The review also identified a source-level device-loss recovery gap and a change in default text antialiasing.
The [full comparison](../specs/windows-gui-memory.md#matched-branch-results) separates these findings and their limits.
Commands, hashes, and raw results remain in `build\swapchain-review\benchmark-delivery.json`.
The review did not merge the branch or change production code.

## Earlier window-size memory investigation

That investigation did not change the production renderer or sample binaries.
It did not identify a correction within the approaches it exercised.
That result did not rule out a different swap-chain implementation.
The extra 64 MiB reproduced in the graphics path without process-row storage or hidden controls.

The measured driver is `qcdx11arm64xum.dll`, version `31.0.133.1`.
At 96 DPI and a 780-pixel client height, one additional client pixel crossed the allocation threshold.
Each table entry is the median of three fresh processes after ten forced full paints.
All runs within each comparison used the same executable, controls, DPI, and dark theme.

| Fixture and physical client size | Private commit | Private working set |
| --- | ---: | ---: |
| Task Manager, 864×780 | 28.33 MiB | 13.71 MiB |
| Task Manager, 865×780 | 92.93 MiB | 78.54 MiB |
| Direct2D, one filled rectangle, 864×780 | 24.90 MiB | 10.69 MiB |
| Direct2D, one filled rectangle, 865×780 | 89.43 MiB | 75.34 MiB |

The rectangle fixture has no XUI controls, text, images, clipping layers, or worker.
Adding an axis-aligned clip did not remove the increase.
A blank XUI window and a Direct2D target with only `Clear` did not show the increase.
Fresh blank XUI windows at 1080×780 used approximately 25 MiB of private commit after full paints.
A native window without Direct2D used approximately 2.3 MiB.
The threshold also changed with height. A 1080×600 rectangle fixture stayed small, while 1080×640 crossed the threshold.

`VirtualQueryEx` identified the growth in `MEM_PRIVATE` regions.
Mapped-file and image commit stayed unchanged across the rectangle boundary comparison.
`HeapWalk` busy memory stayed near 3.1 MiB at first paint on both sides.
The report retains process commit and virtual-region totals separately. These counters are not interchangeable.

A process-local debugger traced only owned rectangle fixtures.
The larger filled target caused eight `NtGdiDdDDICreateAllocation` calls with approximately 8 MiB of additional commit each.
Their combined increase was 64.06 MiB.
The stacks pass through Direct2D, DXGI, Direct3D 11, the Qualcomm driver, and the Windows graphics allocation interface.
The smaller filled target and the larger clear-only target had no such increases.
Target creation alone stayed small. The increase occurred during the first filled frame.

The trace used local module information, not downloaded private symbols.
The debugger source is a local investigation artifact in the evidence directory.
Its breakpoints reject unknown ARM64 syscall instruction sequences.
Module names and offsets identify the allocation path. Nearby exported symbol names do not identify internal driver routines.
The trace does not identify the purpose of each driver-private allocation.
It does not establish the same threshold on other drivers, adapters, or DPI configurations.
The evidence does not support a framework text-cache, row-cache, or clipping-layer correction.

The additional commit persisted after shrink and after target, brush, and factory release.
Both traced sizes ended with zero targets, 215 handles, nine GDI objects, and three USER objects after window closure.
The large fixture still retained approximately 64 MiB more commit before process exit.
All owned fixtures exited. These observations do not establish long-duration resource bounds.
The investigation used no working-set trimming, software-rendering substitution, feature removal, or default-window reduction.

Two follow-up batches each used three fresh production processes at 1080×780 client pixels and 1096×819 outer pixels.
They observed 532–536 processes, ten visible rows, eight threads, 249 handles, and one target.
The executable hash stayed unchanged across both batches.
These medians compare measurements before and after validation, not different renderer versions.

| Counter | Before validation | After validation |
| --- | ---: | ---: |
| Live private commit | 93.50 MiB | 93.51 MiB |
| Live private working set | 79.12 MiB | 79.05 MiB |
| Live total working set | 109.70 MiB | 109.64 MiB |
| Machine-capacity CPU | 0.312% | 0.364% |
| First snapshot | 219.57 ms | 612.05 ms |
| Selection, scroll, and full paint | 17.30 ms | 17.93 ms |

Live CPU ranged from 0.260% to 0.416% across these six runs.
First-snapshot time ranged from 211.58 ms to 4665.84 ms, including one unexplained 4.67-second startup observation.
These short measurements do not establish a startup or CPU bound.
The paused CPU median was zero in both batches. The first batch included one 0.087% observation.
All paused and minimized observations caused no additional paints. All final-batch paused and minimized CPU observations were zero.

The sample remains 588,800 bytes. The DLL remains 503,808 bytes.
The sample SHA-256 stayed unchanged throughout the investigation.
There is no before/after memory-reduction claim because the production code did not change.
The historical measurements remain in their original files.
New measurements and allocation traces are in `build\task-manager\memory-investigation`.
The main evidence files are:

- `final-boundary-task-864.json` and `final-boundary-task-865.json`: fresh Task Manager comparison.
- `final-boundary-fill-864.json` and `final-boundary-fill-865.json`: fresh rectangle comparison with the same diagnostic executable.
- `lifetime-fill.json`, `lifetime-clear.json`, and `lifetime-xui.json`: resize history and resource lifetime.
- `allocation-fill-864-final.log`, `allocation-fill-865-final.log`, and `allocation-clear-865-final.log`: allocation-stack comparison.
- `production-repeat.json` and `production-final.json`: both production measurement batches.
- `summary.json`: medians, ranges, hashes, and allocation totals.
- `validation\compatibility.log` and `validation\phase4`: current build and compatibility results.

The ARM64 Release build succeeded. All 20 native tests passed in 190.23 seconds.
`tests\phase4.ps1 -SkipMeasurements` also passed both C# assertion suites, Rust tests, Clippy, and formatting.
All five normal UIA clients and all five callback-failure clients passed.
The original visual coverage remains applicable because the production renderer and executable did not change.

Run the size probe from the repository root:

```powershell
.\tests\measure-window-memory.ps1 -TaskManager -Runs 3 -Regions -Output build\task-manager\memory-investigation\repeat-production-sweep.json
& "$tools\cmake.exe" --build build\arm64 --config Release --target xui_window_memory_probe --parallel 4
foreach ($width in @(864,865)) {
    .\tests\measure-window-memory.ps1 -Executable build\arm64\Release\xui_window_memory_probe.exe -Arguments "task $width 780" -TaskManager -Runs 3 -Regions -Sizes "${width}x780" -Output "build\task-manager\memory-investigation\repeat-task-$width.json"
}
```

The first command measures resize history in the original production executable.
The optional fixture changes the initial window extent through a thread-local creation hook, before first paint.
It includes the unchanged Task Manager source.
The fixture is excluded from normal builds and is not a pass/fail benchmark.
Its other modes are `native`, `clear`, `fill`, `clip`, `rounded`, and `xui` (blank).
The JSON records actual client and outer dimensions, DPI, executable hash, allocation regions, mapped names, working sets, and paint latency.
`-CaptureOutput` also records fixture heap and resource counts.
`-ReleaseProbeTarget` releases graphics resources only in the raw Direct2D fixture modes.
