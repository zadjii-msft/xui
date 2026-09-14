# A useful minimum for a Windows GUI

## What this experiment asks

How small can a useful Windows application remain without sacrificing text input, accessibility, or responsive interaction?
XUI explores that question with a native C++20 framework, a file browser, a Task Manager sample, and a control gallery.
This report includes September 13, 2026 measurements, the subsequent resize investigation, and an audit of the earlier flip-model claim.

**Correction:** the earlier flip-model experiment did not establish that an explicit swap chain cannot fix resize retention.
It measured a different raw-renderer configuration at one fixed size, not the application through grow/shrink cycles.
The statement that flip-model rendering failed to solve the resize problem exceeded that evidence.
The [claim audit](#audit-of-the-earlier-flip-model-claim) records the implementation differences and preserved run files.

The subsequent matched comparison used the exact `explicit-swap-chain` branch and its baseline.
The branch reduced transient private working set, but did not eliminate retained private commit on this machine.
After resize cycles and 30 seconds at the original small size, median commit was **99.80 MiB baseline versus 102.32 MiB branch**.
The [branch results](#matched-branch-results) separate these counters, performance observations, and incomplete regression coverage.

The result is not a claim of the smallest possible GUI.
A blank HWND is smaller, but it does not provide a usable application.
Nor is this a comparison against Qt, Electron, or WinUI.
That comparison requires an equivalent application, workload, and measurement protocol.

Three findings matter:

- One shared Direct2D target removed substantial presentation overhead in an earlier browser.
- Deferred native creation and smaller retained caches reduced warm gallery private commit by **8.09 MiB**.
- A later renderer candidate saved memory, but failed to establish performance equivalence. The production change was reverted.

A separate hardware-rendering experiment exposed a roughly **64.53 MiB allocation jump** across one pixel of client width.
That result limits framework-only explanations for the observed memory footprint.
It does not establish that every retained allocation is correct.

## A useful minimum, not a blank window

The implemented roadmap contains **25 control families**, not merely 25 primitive widgets.
The gallery contains 46 pages.
Its scope includes collections, navigation, documents, command surfaces, vector graphics, and optional runtime hosts.
The applications use the public control API rather than application-specific drawing callbacks.

The architecture separates responsibilities:

```text
Application composition and callbacks
                  |
Identity and state -> Layout -> Interaction
                  |               |
                  +---- Drawing --+---- UI Automation
                           |
        Win32 + Direct2D + DirectWrite + WIC
                    Native text peers
```

Stable identities connect selection, focus, asynchronous results, and UI Automation (UIA).
Layout uses device-independent coordinates.
The backend handles native windows, drawing, and accessibility.
This separation permits cache release without deletion of the application model.

Win32 owns the windows and message loop.
One Direct2D HWND render target serves each root window, including its custom controls.
DirectWrite supplies text layout.
Windows Imaging Component (WIC) supplies image decoding.
Native EDIT and RichEdit peers preserve platform editing behavior.

The minimum includes keyboard navigation, Unicode text, selection, native undo, and IME-aware input handling.
Removing those features to obtain a smaller memory number changes the question.
Custom UIA providers and native text providers also remain part of the application.
Their cost is not an optional defect.

Virtual collections draw visible rows rather than retaining a native control for every row.
Bounded queues, cancellation, and generation checks limit asynchronous work.
Ordinary idle windows use event-driven updates rather than a continuous render loop.
The live Task Manager still samples periodically because its data changes.

The base build uses the static MSVC runtime and Windows system libraries.
It needs no third-party runtime.
Optional WebView2 is different: its browser runtime and child processes have real costs.
The native memory workloads did not initialize that browser.
Their process counters therefore do not describe an active browser application.

## What the counters mean

One MiB means 1,048,576 bytes.
These counters answer different questions:

| Counter | Meaning and limit |
|---|---|
| EXE bytes | File size on disk, not the complete runtime footprint |
| Private commit | Process-private committed memory, including allocations outside ordinary application heaps |
| Private working set | Private pages currently resident in physical memory |
| Total working set | Resident private and shared pages |
| Virtual-region totals | Region classifications from `VirtualQueryEx`, not interchangeable with process private commit |
| GPU/system allocation | Graphics and operating-system costs, not fully described by any single process counter |

`Process.PrivateMemorySize64` supplies private commit in the measurement script.
`QueryWorkingSet` identifies private resident pages.
Framework counters separately report targets, peers, text layouts, and estimated native bitmap bytes.
The bitmap estimate uses four bytes per pixel and excludes driver allocations.

Working-set trimming can reduce resident pages without reducing committed memory.
These experiments used no working-set trim, forced garbage collection, or heap compaction.
A small executable does not imply a small runtime footprint.
The runtime, fonts, system components, graphics surfaces, and driver allocations also consume resources.

Timing needs the same care.
Process CPU time measures work charged to the target process, but Windows reports it in quantized increments.
Process cycle counts are separate measurements, not elapsed milliseconds.
Wall time includes scheduling, synchronous dispatch, and presentation waits.
Frame timing can reflect display synchronization rather than drawing cost alone.

The startup probe observes a completed root paint.
It does not identify the first pixel that the compositor displays.
Forced full-paint timings and normal input timings are distinct workloads.

## Conditions and binary scope

The recent experiments used native ARM64 binaries, not x64 emulation.
The recorded machine used a Qualcomm Adreno X1-85 GPU with driver `31.0.133.1`.
The configuration log records Windows `10.0.28637`.
The compiler was MSVC 19.44, with Windows SDK `10.0.26100.0`.
Release compilation used `/O2`, `/Ob2`, `/MT`, `/GL`, and `/LTCG`.

Matched comparisons retained the same hardware, architecture, data, dark theme, and 96 DPI.
They used equal physical client sizes, not equal outer-window dimensions.
Builds and tests stopped before measurement.
The recent performance runs required an owned foreground window.
The Task Manager used live process data, so its row counts varied.

The final retained-feature binaries had these file sizes:

| Artifact | Bytes |
|---|---:|
| Gallery EXE | 2,007,040 |
| Explorer EXE | 1,403,392 |
| Task Manager EXE | 1,221,120 |
| C ABI DLL | 1,740,288 |

These are individual artifacts, not a combined deployment total.
This validation build enabled optional WebView2 support, although the measured native workloads did not load it.
The default CMake configuration disables that integration.

An early foundation executable occupied 265,216 bytes.
That historical milestone had a much smaller feature scope.
It is not a head-to-head comparison with the current gallery or a complete distribution-size claim.

## Two accepted changes, two different baselines

### One target instead of nine

The September 11 browser comparison used five fresh processes per binary.
Both variants used 60 fixture files and a 924-by-641 client area at 96 DPI.
The historical baseline was `ab9070b`.
Values below are medians, with observed ranges.
These historical aggregates come from the README.
Their original run files are absent from this checkout.

| Measurement | Before | After |
|---|---:|---:|
| HWND render targets | 9 | 1 |
| Idle private working set, MiB | 15.93 (15.78-17.04) | 13.39 (13.31-13.50) |
| Idle private commit, MiB | 29.97 (29.82-31.14) | 27.70 (27.70-27.72) |
| Selection, scroll, and forced full paint, ms/iteration | 117.80 (110.57-123.61) | 18.56 (17.21-18.93) |

The change removed eight target/brush pairs, not eight controls.
The approximately 6.3-fold improvement applies to that combined dispatch-and-presentation workload.
It is not a claim about every operation.
The smaller private working set is also not a private-commit result.

### Retain state, release expendable resources

The September 13 resource pass used three fresh processes per variant.
The gallery completed two cycles through all 46 pages.
Each cycle included a retained popup open/close and a return to the first page.

| Workload and physical client size | Private commit before/after, MiB | Private working set before/after, MiB |
|---|---:|---:|
| Initial gallery, 1040x731 | 95.30 / 94.61 | 80.96 / 80.38 |
| Gallery after both cycles, 1040x731 | 110.40 / 102.31 | 92.30 / 88.02 |
| Live Task Manager, 1080x780 | 94.13 / 94.08 | 79.87 / 79.80 |
| Explorer, 60 owned text files, 924x641 | 32.08 / 32.02 | 15.95 / 15.83 |

These values are medians.
The gallery saved 8.09 MiB of private commit.
The smaller Task Manager and Explorer differences do not establish reductions beyond run variation.

`PageView` now materializes native peers on first selection.
Visited peers remain alive, including native editor selection and undo.
Hidden controls release cached text layouts.
The native composition buffer matches current clipped regions instead of independent historical maximum dimensions.
The renderer also releases scene geometry unused by the completed frame.

| Gallery measurement | Before | After |
|---|---:|---:|
| Initial native peers | 357 | 35 |
| Initial USER objects | 385 | 53 |
| Cached text layouts after both cycles | 594 | 28 |
| Initial process CPU time, ms | 312.50 | 218.75 |
| Mean first-visit navigation, ms | 38.73 | 47.24 |
| Mean warm navigation, ms | 46.92 | 48.22 |

The initial CPU counter includes startup and settlement before the sample.
Deferred creation reduced that work but increased first-visit latency.
The warm ranges overlapped, which does not prove performance equivalence.
Settled gallery and Explorer observations recorded zero idle paints.
The Task Manager retained its normal sampler.

This comparison used forced layout and full-paint navigation.
It must remain separate from the later normal-input experiment.
Neither accepted change makes every interaction faster.

## A memory saving that did not ship

The next pass applied a stricter requirement: another memory reduction must not sacrifice performance.
Its baseline already included the accepted resource changes.
The candidate set `D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE` on the hardware target.

Six matched runs per application produced these initial private-commit medians:

| Application | Baseline, MiB | Candidate, MiB | Saving, MiB |
|---|---:|---:|---:|
| Gallery | 94.58 | 91.70 | 2.87 |
| Task Manager | 94.20 | 90.89 | 3.31 |
| Explorer | 32.46 | 29.98 | 2.48 |

Run order alternated between baseline-first and candidate-first.
A further six matched gallery runs used normal input updates instead of forced updates.
The analysis used 10,000 deterministic paired bootstrap resamples.
Its independent units were matched runs, not correlated page events.

The following differences are candidate minus baseline for mean first-visit navigation:

| Protocol | Mean wall-time difference | 95% bootstrap interval |
|---|---:|---:|
| Forced update/full paint | +8.66 ms | +3.25 to +14.31 ms |
| Normal input/completed paint | -1.17 ms | -12.26 to +10.78 ms |

The forced workload became slower.
The normal-input interval permitted both improvement and regression.
It did not establish equivalence, nor did it prove a universal slowdown.

The candidate was therefore reverted.
All four final production `.text` sections matched the preserved baseline.
**This pass delivered no new runtime memory improvement.**
Retain-contents, minimum feature level 9, bitmap remoting, and immediate presentation produced no established improvement in the exploratory fixed-size runs.
A raw flip-sequential target also retained substantial memory at 1080x780.
That result did not cover changed-size cycles or an explicit swap chain integrated into XUI.
The performance-based rejection applied to the GDI-compatible target, not an application-level flip-model implementation.

## The hardware allocation boundary

A separate raw probe exercised Direct2D without XUI controls, text, images, or application workers.
The recent boundary comparison used its `rounded` drawing mode.
That path draws a rectangle and rounded rectangle within a clip.
It does not represent an application framework benchmark.

| Fresh raw probe, 96 DPI | Private commit, bytes |
|---|---:|
| 864x780 physical client pixels | 26,132,480 |
| 865x780 physical client pixels | 93,794,304 |
| Difference | **67,661,824 (64.53 MiB)** |

At 865 pixels, a separate first-paint snapshot recorded 3,313,070 bytes of busy heap memory against 93,450,240 bytes of private commit.
Those are different counters and sampling stages.
The disparity argues against ordinary retained control objects as the dominant explanation.
Mapped and image commit remained unchanged across the boundary samples.

The evidence supports a graphics-triggered allocation.
This latest pass did not collect exact driver allocator stacks.
It does not prove that all framework code is correct or that resize-related leaks are impossible.

A cache retains reusable resources.
A high-water allocation retains capacity after peak demand.
A leak retains resources without a valid continuing owner.
Those definitions do not diagnose a particular memory curve.
The [resize investigation](#resize-investigation--september-13-2026) examines growth through repeated size changes.
Its release-boundary evidence does not establish that this footprint is unavoidable with another graphics configuration.

## Reproduce the measurements

### Build the native baseline

Install Visual Studio 2022 with ARM64 C++ tools, CMake, and a Windows SDK.
Open a developer PowerShell with CMake on `PATH`.
Run these commands from the repository root:

```powershell
cmake -S . -B build\report-repro -G "Visual Studio 17 2022" -A ARM64 `
  -DBUILD_TESTING=ON -DXUI_ENABLE_WEBVIEW2=OFF
cmake --build build\report-repro --config Release --parallel 4
cmake --build build\report-repro --config Release --target xui_window_memory_probe
```

The probe target is excluded from the default build.
The generator selects an installed Visual Studio instance.
Multiple installations can require an explicit `CMAKE_GENERATOR_INSTANCE`.
This base build excludes optional browser support, unlike the recorded integration build.

### Measure owned application windows

Stop unrelated builds and tests.
Keep the measurement window in the foreground.
Create a new fixture directory without overwriting an existing directory:

```powershell
$fixture = Join-Path (Get-Location) 'build\report-repro\owned-files-60'
if (Test-Path $fixture) { throw 'Fixture already exists. Use a new owned directory.' }
New-Item -ItemType Directory $fixture | Out-Null
1..60 | ForEach-Object {
    Set-Content (Join-Path $fixture ('owned-file-{0:D2}.txt' -f $_)) `
      'Owned XUI memory comparison fixture.'
}
& .\tests\measure-window-memory.ps1 `
  -Executable build\report-repro\Release\xui_demo.exe `
  -Arguments ('"' + $fixture + '"') -Runs 3 -Sizes @() `
  -Performance -ResourceMetrics `
  -Output build\report-repro\explorer.json
& .\tests\measure-window-memory.ps1 `
  -Executable build\report-repro\Release\xui_gallery.exe `
  -Gallery -Runs 3 -Sizes @() -Performance -ResourceMetrics `
  -Output build\report-repro\gallery.json
& .\tests\measure-window-memory.ps1 `
  -Executable build\report-repro\Release\xui_task_manager.exe `
  -TaskManager -Runs 3 -Sizes @() -Performance -ResourceMetrics `
  -Output build\report-repro\task.json
```

`-Sizes @()` preserves the initial extent and avoids a size sweep.
The JSON records the actual physical client size and DPI.
The performance mode requires exactly 60 Explorer rows and completed visible thumbnails.
Gallery mode requires exactly 46 catalog pages.
It includes two navigation cycles and popup closure.
`-ForceInputPaint` selects the distinct forced-update protocol.

### Measure fresh raw targets

Run each size in a fresh process:

```powershell
foreach ($width in 864,865) {
    & .\tests\measure-window-memory.ps1 `
      -Executable build\report-repro\Release\xui_window_memory_probe.exe `
      -Arguments "rounded $width 780 0" -Runs 1 -Sizes @() `
      -Regions -CaptureOutput `
      -Output "build\report-repro\raw-$width.json"
}
```

The probe accepts positional arguments: kind, width, height, automatic-close flag, usage, presentation flags, and minimum feature level.
There is no `--raw-d2d` switch.
The script owns and closes each process.
These commands generate new observations, not guaranteed copies of the recorded results.
Historical before/after comparisons additionally require their preserved binaries.

## Evidence, coverage, and limits

The latest default-renderer build passed all **37 native tests on its first attempt**.
Coverage includes native composition, UIA, editor retention, simulated device recreation, and injected DPI.
These tests do not replace physical mixed-monitor moves, real IME sessions, screen-reader use, or long-duration operation.
They also do not prove performance equivalence.

The first binding smoke attempt hit a key-posting race after an intentional-failure client closed its window.
The unmodified retry passed all eight smoke cases.
Both attempts remain in the local evidence.

Key source references:

- `include\xui\controls.hpp`: `Control` and `PageView` identity, layout, and retained state.
- `src\application.cpp`: `Window::Impl` peer management and `Application::run`.
- `src\drawing.cpp`: `Drawing::begin`, `Drawing::scene`, and resource counters.
- `tests\measure-window-memory.ps1`: `Snapshot` and `WindowMemoryProbe.Measure`.
- `tests\window_memory_probe.cpp`: `wmain`, `paint`, and `resources`.
- `tests\measure-browser.ps1`: the historical 60-file browser protocol.
- `CMakeLists.txt`: runtime, architecture, optional integration, and probe targets.

Local provenance, relative to the repository root:

| Evidence | Location |
|---|---|
| Historical single-target aggregates | `README.md`, "Memory and presentation (single-target baseline)" |
| Accepted resource pass | `build\controls\memory-delivery.json`, `build\controls\memory-evidence\summary.json` |
| Accepted-pass raw samples and commands | `build\controls\memory-evidence\final-*-*.json`, `build\controls\memory-evidence\commands.ps1` |
| Rejected candidate and statistics | `build\memory-tight\delivery.json`, `build\memory-tight\summary.json`, `build\memory-tight\summarize.py` |
| Paired performance samples | `build\memory-tight\paired-*-*.json`, `build\memory-tight\natural-*-gallery-*.json` |
| Raw boundary and heap snapshots | `build\memory-tight\raw-cliff-864.json`, `build\memory-tight\raw-cliff-865.json`, and their `.stdout.txt` sidecars |
| Build and restoration | `build\memory-tight\configure.log`, `build\memory-tight\binary-comparison.json` |
| Native and binding results | `build\memory-tight\final-tests.log`, `build\memory-tight\binding-features.log`, `build\memory-tight\binding-features-rerun.log` |

The listed build artifacts are local, ignored evidence, not downloadable attachments.
This report includes the central aggregates so readers do not need those files to understand its conclusions.
Raw artifacts can contain local paths and require sanitization before sharing.

The practical lesson is to minimize duplicated rendering resources and expendable caches while preserving useful state.
Memory, input latency, and correctness need separate evidence.
When its performance cost remains uncertain, a rejected optimization is a valid result.

## Resize investigation — September 13, 2026

### Result and scope

The demo reproduced the large increase and retained most of it after a resize back to the small size.
The evidence indicates bounded, factory-associated graphics retention in these runs, not an accumulating XUI resource leak.
This result does not establish that every XUI workload is free of leaks.
No production renderer change resulted from this investigation.

The reported “10 MB commit” remains an observation with an unknown counter and sampling time.
The current demo reached about 30 MiB private commit and 15 MiB private working set at the first completed paint observed.
It reached about 33 MiB private commit after ten seconds.
After enlargement, private commit exceeded 100 MiB.
After shrinking and thirty seconds of idle time, private working set remained near 83 MiB.
Thus, the large increase and retention reproduced, but the exact initial 10 MB commit value did not.

### Method

The investigation used the current `build\controls\Release\xui_demo.exe` and an independent rebuild in `build\resize-memory`.
The controls, memory-tight, and resize-memory demo builds had identical `.text` hashes.
The rebuild used ARM64 Release, MSVC 19.44.35228.0, `/MT`, LTO, SDK 10.0.26100.0, and the existing optional WebView2 SDK.
Windows reported version 10.0.28637.0 and Adreno X1-85 driver 31.0.133.1.

Each workload used three fresh processes.
The demo used an owned directory with exactly 60 text files.
Each process completed a fixed-size batch and a varied-size batch:

- The small client measured 800 × 600 physical pixels.
- The fixed batch enlarged the client to 1100 × 850, then restored 800 × 600.
- The varied batch used 1400 × 500, 800 × 1000, and intermediate dimensions before each return to 800 × 600.
- The current demo, raw probe, and empty XUI probe completed 24 cycles per batch.
- The rebuilt demo completed 50 cycles per batch, or 100 cycles per process.

All measured windows reported 96 DPI.
The script recorded requested and actual dimensions.
It required completed paints and new XUI layouts after size changes.
The final raw probe also counted `WM_SIZE` messages and exposed the actual target pixel dimensions.
The demo minimum outer size was 460 × 420 DIPs, so it did not constrain these dimensions.

The script recorded startup, the first completed paint observed, and one-second and ten-second samples.
Thirty same-size paints at 800 × 600 preceded the first large resize.
Each cycle included a sample after the shrink.
Final idle samples occurred after 0.5, 2, 10, and 30 seconds.
The earliest process sample preceded window discovery, but it was not a synchronized prepaint breakpoint.
The raw probe separately recorded entry and prepaint heap counters.

Measurements included private commit, private working set, allocation bases, commitment histograms, handles, GDI objects, USER objects, and existing XUI resource counters.
The histogram groups committed bytes by allocation base, not by individual VirtualQueryEx subregion.
HeapWalk samples covered the raw and empty XUI probes, not the unmodified demo.
The counters came from separate API calls, so startup and immediate-resize samples were not atomic.
The script used no working-set trimming, heap compaction, or forced collection.

Only newly created processes received window messages.
The main script required the launched PID and the expected root window class.
No existing user window received a resize or close message.
These were synchronous redraw experiments, not foreground-input or resize-latency benchmarks.

### Memory results

Values are medians of three processes, in MiB.
The initial sample occurred after ten seconds.
The demo initially measured 924 × 641 pixels.
The other workloads initially measured 800 × 600 pixels.

| Workload | Initial commit / private WS | First large commit | Small client after 30 seconds: commit / private WS |
|---|---:|---:|---:|
| Current demo, 48 cycles | 33.05 / 16.91 | 103.25 | 99.80 / 83.25 |
| Rebuilt demo, 100 cycles | 32.82 / 16.72 | 103.02 | 99.44 / 82.89 |
| Raw Direct2D, rounded rectangle, 48 cycles | 24.36 / 8.33 | 92.32 | 90.10 / 75.50 |
| Empty XUI, 48 cycles | 23.88 / 7.70 | 27.84 | 24.30 / 9.04 |

Immediately after the final shrink, the rebuilt demo retained more temporary memory.
Its median private commit was 113.25 MiB at 0.5 seconds and 113.09 MiB at two seconds.
It decreased to 99.44 MiB by ten seconds without an explicit release request.
The thirty-second value was also 99.44 MiB.

Some shorter batches contained discrete allocation steps rather than an immediately flat curve.
The extended demo runs separated those steps from continued growth.
The table uses cycles 25–50 within each batch, with least-squares slopes from the individual process samples.

| Extended demo batch | Within-process commit range across those cycles | Commit slope across the three runs |
|---|---:|---:|
| Fixed dimensions | 76–84 KiB | +0.35 to +1.04 KiB/cycle |
| Varied dimensions | 80–112 KiB | −0.62 to +0.27 KiB/cycle |

The small-window resource counters stayed constant through both batches in all six demo processes.
Each sample had one render target, 49 retained peers, 14 live text layouts, 65 GDI objects, and 74 USER objects.
The native DIB returned to 51,360 bytes, and the native bitmaps returned to 93,440 bytes.
At the first large size, those buffers occupied 75,360 and 141,440 bytes respectively.
The cumulative text-layout creation counter increased, but the live layout count did not.
These results exclude the previously corrected independent width/height buffer-retention pattern from this reproduction.

### Drawing-operation and lifetime isolation

An empty window alone did not reproduce the large allocation.
Three additional fresh-process runs per mode compared raw `Clear`, a clipped rectangle, and an unclipped rectangle.
All modes used the same hardware-capable HWND target configuration and the same physical sizes.

| Raw drawing operation | Initial commit | Large-client commit | Small-client commit at the short sample |
|---|---:|---:|---:|
| Clear only | 23.80 | 27.63 | 27.63 |
| Clear plus clipped rectangle | 24.36 | 92.31 | 94.25 |
| Clear plus unclipped rectangle | 24.37 | 92.32 | 94.26 |

These short samples included repeated paints and a one-second idle interval.
They were not the thirty-second samples from the main experiment.
A rectangle without clipping was sufficient to reproduce the large increase.
Neither rounded geometry, text, native controls, nor XUI buffer ownership was necessary.
Therefore, a clear-only window is not a representative memory baseline for a populated interface on this system.

The raw probe then released resources without exiting its process.
After the resize batches, releasing the brush and render target left a median 90.03 MiB private commit after thirty seconds.
The factory remained alive, and the render-target counter was zero.
Releasing the factory as well reduced private commit to 3.87 MiB and private working set to 2.38 MiB.
Recreating the factory and a small target restored a usable window.

An independent repeat used 40 resize cycles in one fresh raw-probe process.
Target-only release retained 90.03 MiB of private commit after thirty seconds.
Factory release reduced commit to 3.88 MiB by ten seconds, without process exit.
The samples remain in `build\resize-memory\parent-raw-verification.json`.

VirtualQueryEx showed large commitment changes inside existing private reservations.
The repeated batches did not retain a new large allocation base for each resize.
The factory-release experiment removed the large reservations.
Raw HeapWalk busy bytes remained near 3.2–3.3 MiB during the resize batches.
All sampled heaps supported HeapWalk.
The large retained allocation therefore did not correspond to comparable growth in the enumerated heap blocks.

The raw and empty XUI probes also destroyed and reopened five root windows in each process.
The raw probe retained its factory across those root lifetimes.
Its large allocation survived root destruction, without another 64 MiB increase for each root.
The empty XUI probe destroyed each complete `Window` object before the next root.
Its handles and GUI object counts remained bounded, and later reopened-window memory stayed in the same range.
These checks used the same process throughout, not process exit as evidence of resource release.

The strongest attribution is the Direct2D factory-associated rendering path on this hardware and driver.
The experiment did not collect allocator call stacks.
It cannot distinguish an internal Direct2D cache from a driver cache with the same lifetime.
It also cannot assign every small retained page to one owner or exclude smaller, slower leaks.
Factory recreation on each shrink discards useful rendering resources.
Such a change requires separate correctness and performance evidence.
This investigation did not make that production change.

### Reproduction and validation

Use the build procedure earlier in this report to create the demo and probe.
Use the owned 60-file fixture procedure from the same section.
For the recorded build profile, enable the existing optional WebView2 SDK and `XUI_DESKTOP_TESTS`.
Use fresh output names for each run.

```powershell
& .\tests\measure-resize-memory.ps1 `
  -Executable build\report-repro\Release\xui_demo.exe -Kind demo `
  -Fixture build\report-repro\owned-files-60 -Runs 3 -Cycles 50 `
  -Output build\report-repro\resize-demo.json
& .\tests\measure-resize-memory.ps1 `
  -Executable build\report-repro\Release\xui_window_memory_probe.exe `
  -Kind raw -Runs 3 -Cycles 24 -Lifetimes 5 `
  -Output build\report-repro\resize-raw.json
& .\tests\measure-resize-memory.ps1 `
  -Executable build\report-repro\Release\xui_window_memory_probe.exe `
  -Kind xui -Runs 3 -Cycles 24 -Lifetimes 5 `
  -Output build\report-repro\resize-empty-xui.json
foreach ($kind in 'clear','clip','noclip') {
    & .\tests\measure-window-memory.ps1 `
      -Executable build\report-repro\Release\xui_window_memory_probe.exe `
      -Arguments "$kind 800 600 0" -Runs 3 `
      -Sizes @('1100x850','800x600') -Regions -CaptureOutput `
      -Output "build\report-repro\primitive-$kind.json"
}
```

The probe now accepts an eighth positional argument for the number of root lifetimes in raw and `xui` modes.
Its test-only messages support heap snapshots, target-only release, factory release, and recreation.
The main script performs the ownership checks before these messages.

Both targeted regressions passed: `xui_window_tests` and `xui_split_window_tests`.
The checks covered existing native-resource lifetimes, retained editor state, resize, and simulated DPI changes.
The evidence checker also passed for all twelve main measurement processes.
It checked completed cycles, physical dimensions, constant retained-resource counters, root lifetimes, and executable hashes.
It did not impose a driver-specific memory threshold.

The first raw attempt exposed an error in the measurement script, not a renderer failure.
Generic visible-window discovery was too broad for the gap between root lifetimes.
The corrected script filters by PID and root class.
The failed attempt remains in the evidence, separate from the three successful raw repeats.
An initial CTest command selected no tests because desktop registration was disabled.
After registration, the final command used `--no-tests=error` and passed both tests.

Local evidence remains under `build\resize-memory`:

- `delivery.json`, `baseline.json`, and `manifest.json`: conclusions, binary hashes, source hashes, and build profile.
- `demo-current.json` and `demo-extended.json`: six demo processes and all resize samples.
- `raw-final.json` and `empty-xui.json`: repeated resize and same-process lifetime samples.
- `primitive-clear.json`, `primitive-clip.json`, and `primitive-noclip.json`: drawing-operation comparisons.
- The `.stdout.txt` sidecars: heap counters and resource-release boundaries.
- `summary.json`, `summarize.py`, `verification.json`, and `verify.py`: aggregates and evidence checks.
- `window-tests-final.log`: the two successful native regressions.
- `raw.json` and `window-tests.log`: the failed harness attempt and the initial empty test selection.

## Audit of the earlier flip-model claim

The `explicit-swap-chain` branch prompted a review of the earlier claim.
Leonard Hecker supplied commit `4c1f9ab21d7e72c9f105176537a941c58e28aece`, based on `74bc1f8d64a8f4b889b52017eab7932d8dc0126b`.
The accompanying account reports WPR allocation traces through `ID2D1HwndRenderTarget::Resize`.
That trace attribution is supplied evidence, not an independently inspected trace in this report.

### What the earlier experiment actually did

The earlier probe was real.
`tests\window_memory_probe.cpp::paint` creates a D3D11 device, a flip-sequential swap chain, and a Direct2D surface render target.
The preserved build log and two output files record the flip path.
The later output also reports `hardware_supported=1`.

The final recorded invocation was:

```text
xui_window_memory_probe.exe flip 1080 780 0 0
```

`build\memory-tight\raw-flip-final.json` records 93,528,064 bytes of initial private commit, or 89.20 MiB.
Its executable hash is `545FF967335DE0ED36B7BC4B7BFB884471592F53B1BC83E8E07E2FEC96963A41`.
The earlier `raw-flip.json` records an initial sample and another sample at the same 1080x780 size.
Neither file establishes behavior through a changed-size cycle.
The small reduction in that experiment was not a performance-based rejection of the later branch.

### Why it is not the same experiment

| Property | Earlier raw probe | `explicit-swap-chain` branch |
|---|---|---|
| Scope | Rectangle fixture at fixed size | Integrated XUI renderer |
| D3D11 creation flags | `BGRA_SUPPORT` | Also `SINGLETHREADED` and `PREVENT_INTERNAL_THREADING_OPTIMIZATIONS` |
| Swap effect | `FLIP_SEQUENTIAL` | `FLIP_SEQUENTIAL` |
| Buffer count | 2 | 3 |
| Scaling | Default `STRETCH` | `NONE` |
| D2D target type | `HARDWARE` | `DEFAULT` |
| D2D alpha mode | `IGNORE` | `PREMULTIPLIED` |
| Presentation | `Present(1, 0)` | `Present1(1, 0, ...)` |
| Resize evidence | No changed-size samples in the cited runs | Matched application cycles described below |

The branch releases target-dependent resources before `ResizeBuffers` and explicitly manages presentation.
The old application instead called `ID2D1HwndRenderTarget::Resize`.
This is a substantive renderer change, not the rejected GDI-compatible target flag.

The original writeup generalized from one raw configuration to a class of solutions.
That was an error in experimental scope and interpretation.
A high memory result in the earlier probe cannot establish that this branch has the same behavior.
Likewise, several changed properties prevent attribution of any improvement to the swap effect alone without a controlled comparison.

The earlier measurements remain in the report as historical evidence, not as grounds to reject the branch.
The subsequent comparison separates memory savings, performance costs, and rendering correctness.

### Matched branch results

Both builds used the exact commits identified earlier, exported with `git archive`.
Neither source snapshot contained renderer edits or diagnostic instrumentation.
The builds used ARM64 Release, MSVC 19.44.35228, Windows SDK 10.0.26100.0, `/MT`, and LTO.
Both enabled the existing optional WebView2 SDK for the regression suite.
The memory workload did not initialize WebView2.

The machine used Qualcomm Adreno X1-85 graphics, driver 31.0.133.1, and 96 DPI.
Module snapshots from both measured demo processes contained the Qualcomm D3D11 driver.
Neither snapshot contained the WARP module.
This supports hardware rendering, but does not replace a direct device query or an ETW trace.
The measurement procedure did not force WARP.

Three fresh demo processes per variant used the same 60-file fixture.
The run order was baseline, branch, branch, baseline, baseline, branch.
Each process completed 24 fixed grow/shrink cycles and 24 varied-size cycles.
The fixed client sizes were 800x600 and 1100x850 pixels.
The varied workload also included 1400x500 and 800x1000 sizes.
Final samples followed 0.5, 2, 10, and 30 seconds at 800x600.

The table reports medians across the three processes, in MiB.
Private commit and private working set are separate counters.

| Observation | Baseline commit | Branch commit | Baseline private working set | Branch private working set |
|---|---:|---:|---:|---:|
| Small window after 30 same-size paints | 34.25 | 37.07 | 18.02 | 20.89 |
| First enlargement | 102.35 | 101.39 | 70.22 | 53.13 |
| After the varied-size batch | 113.70 | 104.85 | 97.57 | 88.73 |
| Small window after 30 seconds of idle | 99.80 | 102.32 | 83.29 | 86.03 |

The first enlargement used **17.09 MiB less private working set** with the branch.
The varied-batch sample used **8.85 MiB less private commit**.
These are real improvements at those observation points, not proof of a lower continuous peak.
Neither improvement persisted in the final settled counters.
Final commit ranged from **99.73 to 100.24 MiB** for the baseline and **102.16 to 104.32 MiB** for the branch.

Thus, the exact branch did not remove the retained-commit increase in this workload.
This result does not disprove the supplied WPR attribution.
Allocation stacks and retained process counters answer different questions.
It also does not rule out another swap-chain configuration or a different hardware result.
No configuration ablation isolated the effects of the device flags, buffer count, scaling, target type, or alpha mode.

### Performance observations

The fixed-resize workload produced 144 synchronous timing observations per variant, across three processes.
Median wall time was **25.51 ms baseline versus 30.22 ms branch**.
The 95th percentile was **39.84 ms versus 53.30 ms**.
These observations include layout, the requested repaint, and presentation waits.
They are not input-to-photon measurements or 144 independent process runs.

Separate interaction profiles completed three runs per variant for Explorer and Task Manager.
The gallery completed three baseline runs and two branch runs.
Foreground loss interrupted additional attempts.
The evidence retains those attempts but excludes them from numerical aggregates.
Other system activity remained uncontrolled, so these samples do not establish performance equivalence.
No performance threshold stopped the branch comparison or caused its rejection.

### Rendering and recovery findings

The unmodified branch passed **32 of 37 native tests**.
The five corresponding baseline tests passed.
All five branch retries failed, although some assertions depend on capture or desktop interaction rather than renderer correctness alone.

| Test | Observed failure and limit |
|---|---|
| `xui_thumbnail_tests` | Pixel-color assertion. Its `GetDC`/`GetPixel` capture does not establish the contents of a flip-presented frame. |
| `xui_shell_thumbnail_tests` | The same capture limitation applies to the Shell pixel-color assertion. |
| `xui_flicker_tests` | First run timed out. The retry failed the visible-glyph reference setup. Cause remains unresolved. |
| `xui_task_manager_smoke` | Drag-order and cursor assertions failed. Both depend on desktop interaction. Cause remains unresolved. |
| `xui_hosts_window_tests` | Fixture cleanup failed initially. The retry failed suggestion-window capture. Earlier media and WebView2 checks passed. |

Separate Windows Graphics Capture checks used only each test process's root HWND.
They showed the unmodified demo at five physical sizes, with visible labels and file icons, followed by normal exit.
The document-control test also passed native text composition, UIA, target recreation, and nine theme/DPI cases.
Those results do not replace the incomplete flicker and Shell-thumbnail coverage.
The suite is not a clean pass, and the capture failures do not prove that users see missing icons.

Source inspection found a recovery gap in `Drawing::begin`, at branch `src\drawing.cpp:203-205`.
If `ResizeBuffers` returns `DXGI_ERROR_DEVICE_REMOVED` or `DXGI_ERROR_DEVICE_RESET`, the method discards graphics resources and returns `false`.
`Native::paint` in `src\application.cpp` then calls `EndPaint` without scheduling another paint.
An unrelated later event can trigger recovery, but this failure path does not request recovery itself.
This is a source finding, not a reproduced device-loss event.
Existing target-recreation tests do not establish coverage of that new path.

The branch also changes the Direct2D alpha mode from `IGNORE` to `PREMULTIPLIED`.
Microsoft documents that a non-`IGNORE` alpha mode changes default text antialiasing from ClearType to grayscale.
The branch does not explicitly override that default.
This is a rendering behavior change, not by itself a reason to reject the branch.
The reference is [ClearType and alpha modes](https://learn.microsoft.com/en-us/windows/win32/direct2d/supported-pixel-formats-and-alpha-modes#cleartype-and-alpha-modes).

### Branch evidence and reproduction

The checkout remains on `main`.
This review did not merge the branch, edit production code, or create a commit.
The baseline demo is 1,403,392 bytes, and the branch demo is 1,405,440 bytes.
Their local paths are:

```text
D:\dev\private\xui\build\swapchain-review\baseline-build\Release\xui_demo.exe
D:\dev\private\xui\build\swapchain-review\branch-build\Release\xui_demo.exe
```

`build\swapchain-review\benchmark-delivery.json` records commands, source archives, binary hashes, measurements, test results, and limitations.
The six `resize-*.json` files contain the raw resize samples.
The `perf-*.json` files contain the completed interaction profiles.
The `branch-tests.log`, `baseline-diagnostic-tests.log`, and `branch-diagnostic-tests.log` files retain every regression result.
The `compositor-*.bmp` files contain the owned-window captures.
Git does not track these local artifacts.

For an additional measurement with the existing branch build, use a fresh output filename:

```powershell
& .\tests\measure-resize-memory.ps1 `
  -Executable build\swapchain-review\branch-build\Release\xui_demo.exe `
  -Kind demo -Fixture build\swapchain-review\fixture -Runs 1 -Cycles 24 `
  -Output build\swapchain-review\additional-branch-resize.json
```
