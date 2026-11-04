# Phase-5 callback correction: final measurements

## Result and disposition

The final measured implementation is
`4dea0022ff1b747bdaaa70bb0a3525c85308c077`, compared with merged phase 4,
`de9ee24ea4664f0f6d0fcaf87f573383086244da`.
This is the final frozen `r7` candidate, not an earlier, more favorable iteration.
The [original comprehensive study](stage5-comprehensive-performance.md) remains
historical evidence, unchanged.

**The major composition regressions are substantially reduced, but performance
acceptance remains blocked.** The 61-phase matrix retains **84 overlapping
over-5% screening statistics across 22 ordinary phases**; none of its five group
sums crosses the threshold. The separate original Button/NumericInput comparison
adds six statistics for numeric stepping. These are not 90 independent failures.
Faster group sums do not waive individual regressions.

| Fixed group | Median wall B -> C, ms | Wall change | Owner CPU change |
| --- | ---: | ---: | ---: |
| 36 C++ control/model phases | 13517.700 -> 13184.158 | -2.47% | -2.32% |
| 8 C ABI phases | 2883.987 -> 2616.971 | -9.26% | -9.24% |
| 6 managed phases | 1707.986 -> 1692.168 | -0.93% | -0.91% |
| 7 real-editor phases | 26693.967 -> 25854.989 | -3.14% | -3.69% |
| 4 large-collection host phases | 3631.048 -> 3377.813 | -6.97% | -6.14% |

These are synthetic fixed-weight component sums, not application usage weights
or a single whole-framework performance score.

## What changed

- UI-thread intrusive composition references retain callables through dispatch,
  instead of copying large callable captures or taking atomic shared ownership.
  An explicit in-flight guard preserves retirement/destructive-callback safety.
- Scope links replace vectors of weak registrations. Scope retirement cancels
  every registration before destroying captures that might reenter another one.
  Registrations unlink before capture destruction.
- Events with no child observer capture only composition, not an empty observer
  and a second Callback wrapper. CommandBar retains its unexposed fast path and
  checks keyed observers only while their binding is active.
- Miller selection rechecks immutable source identity/focus without an extra
  `available()` item query. Fixed child identities remain eagerly retained, but
  activation/focus bindings are installed only when a column is first populated.
  Empty Miller views no longer create 64 dormant callback registrations.
- Managed subscriptions embed intrusive lifetime registrations; a window caches
  at most one fully released general/menu token. In-flight tokens cannot be
  recycled. Old content-scope references are cleared before reuse.
- Window handler cleanup uses cached static typed delegates, rather than a new
  capturing cleanup delegate per window/handler. ABI Nodes need a cancellation
  bit, not an otherwise empty callback scope; existing dispatch ownership retains
  the Node/context until return.

An intermediate inline-observer design reduced some setup costs but substantially
regressed dispatch. It was rejected, not presented as the final implementation.
The final public setter signatures and compact immutable observer records remain.

## Protocol and evidence scope

The main matrix has five serial groups, each with two warmup and seven recorded
alternating baseline/candidate pairs: **90 processes / 1,098 timed phase rows**.
The same frozen counts, fixture sources, original baseline executables/DLLs,
compiler/runtime and full `/LTCG` protocol are retained. The final candidate's
measured libraries/DLLs match the functionally verified production artifacts.
The original two-workload protocol is a separate **18-process / 36-timed-row**
block, not pooled into the matrix.

Final group UTC envelopes on November 4, 2026:

| Group | Start UTC | Completion UTC |
| --- | --- | --- |
| Model | 15:13:02.821760 | 15:17:05.097291 |
| ABI | 15:17:05.941954 | 15:17:58.114837 |
| Managed | 15:17:58.905800 | 15:18:33.608995 |
| Editor | 15:18:34.469152 | 15:26:54.734783 |
| Collection | 15:26:55.501499 | 15:28:03.466847 |

There are no excluded, replaced or retried samples within these completed blocks.
No builds or test suites overlap them. A user-requested Git/PR/directory status
read occurred while the background editor block was running; this was not a
perfectly untouched idle-machine reservation. External interference is not proven
absent. No sample was removed because of that status read.

The independent main audit reconstructs all 430 comparisons, group sums,
median/sum/paired statistics, 84 threshold flags, raw output, serial order and
277 frozen input identities. The separate narrow audit reconstructs its eight
comparisons, all six flags and exact work/allocation counts.

The following tables show median milliseconds for each fixed phase. Positive
percentages mean slower/more CPU. **Flag** means any wall or owner-CPU median,
sum or paired median exceeds 5%; editor query/process CPU is included too.
A row can therefore be flagged even when its displayed wall median is lower.
Seven-sample P90 is the maximum, not a well-estimated application P90.
The raw companion retains every sample, min/mean/P90/max and every flag.

## Complete final timing tables

### C++ control/model matrix

| Phase | Baseline ms | Candidate ms | Wall change | Owner CPU change | Screening |
| --- | ---: | ---: | ---: | ---: | --- |
| toggle-invoke | 392.077 | 395.871 | +0.97% | +0.00% | - |
| checkbox-invoke | 380.838 | 381.182 | +0.09% | +0.00% | Flag |
| control-focus-cycle | 376.411 | 386.209 | +2.60% | +4.17% | Flag |
| label-property | 371.983 | 371.705 | -0.07% | +0.00% | - |
| progress-value | 384.476 | 370.617 | -3.60% | -4.00% | - |
| text-commit | 403.430 | 390.215 | -3.28% | +0.00% | - |
| text-submit | 384.369 | 356.156 | -7.34% | -8.00% | - |
| radio-selection | 375.728 | 425.705 | +13.30% | +12.50% | Flag |
| radio-accept | 396.381 | 326.136 | -17.72% | -16.00% | - |
| selector-selection | 336.124 | 342.311 | +1.84% | +0.00% | - |
| range-change | 320.959 | 331.778 | +3.37% | +0.00% | - |
| combo-editor-commit | 357.066 | 369.332 | +3.44% | +4.35% | Flag |
| combo-selection | 390.992 | 407.435 | +4.21% | +4.00% | Flag |
| multiline-commit | 290.934 | 341.623 | +17.42% | +15.79% | Flag |
| richtext-commit | 306.233 | 364.859 | +19.14% | +15.00% | Flag |
| password-commit | 381.682 | 414.184 | +8.52% | +8.00% | Flag |
| date-change | 384.880 | 402.687 | +4.63% | +13.04% | Flag |
| items-selection | 395.817 | 426.675 | +7.80% | +12.00% | Flag |
| items-activate | 407.462 | 415.182 | +1.89% | +0.00% | - |
| tree-selection | 414.833 | 409.942 | -1.18% | -3.70% | - |
| grid-selection | 453.941 | 384.110 | -15.38% | -13.79% | - |
| miller-selection | 400.149 | 402.203 | +0.51% | +0.00% | - |
| commandbar-invoke | 427.936 | 285.414 | -33.30% | -32.14% | - |
| breadcrumb-invoke | 390.209 | 329.640 | -15.52% | -16.00% | - |
| titlebar-caption | 393.684 | 111.061 | -71.79% | -72.00% | - |
| menubar-heading | 372.262 | 310.158 | -16.68% | -16.67% | - |
| tabs-new-button | 377.592 | 275.375 | -27.07% | -25.00% | - |
| status-dismiss | 375.500 | 393.805 | +4.87% | +4.17% | Flag |
| color-channel | 341.164 | 361.614 | +5.99% | +9.09% | Flag |
| scene-accessible-selection | 338.135 | 367.524 | +8.69% | +9.09% | Flag |
| navigation-toggle | 367.197 | 344.523 | -6.17% | -8.33% | - |
| navigation-search | 379.297 | 357.472 | -5.75% | -8.00% | - |
| command-surface-search | 347.895 | 336.558 | -3.26% | -4.55% | Flag |
| location-search | 322.150 | 331.930 | +3.04% | +0.00% | - |
| viewpicker-size | 328.976 | 328.207 | -0.23% | +0.00% | - |
| dialog-primary | 348.787 | 332.160 | -4.77% | -4.35% | - |

### C ABI

| Phase | Baseline ms | Candidate ms | Wall change | Owner CPU change | Screening |
| --- | ---: | ---: | ---: | ---: | --- |
| abi-observed-invoke | 351.284 | 303.467 | -13.61% | -13.04% | - |
| abi-subscribe-replace | 355.194 | 230.609 | -35.08% | -39.13% | - |
| abi-unsubscribe-resubscribe | 357.869 | 249.937 | -30.16% | -30.43% | - |
| abi-range-value | 369.278 | 355.465 | -3.74% | +0.00% | - |
| abi-miller-registration | 382.384 | 364.620 | -4.65% | -4.00% | - |
| abi-content-rollback | 324.447 | 364.672 | +12.40% | +9.52% | Flag |
| abi-content-commit-clear | 353.026 | 362.123 | +2.58% | +0.00% | - |
| abi-window-retirement | 353.135 | 370.989 | +5.06% | +9.09% | Flag |

### Managed bindings

| Phase | Baseline ms | Candidate ms | Wall change | Owner CPU change | Screening |
| --- | ---: | ---: | ---: | ---: | --- |
| managed-observed-invoke | 297.052 | 300.108 | +1.03% | +5.26% | Flag |
| managed-subscribe-remove | 346.235 | 367.825 | +6.24% | +9.09% | Flag |
| managed-choice-selection | 282.410 | 274.520 | -2.79% | +0.00% | - |
| managed-content-rollback | 201.684 | 193.194 | -4.21% | -7.69% | - |
| managed-content-commit-clear | 289.405 | 255.405 | -11.75% | -10.53% | - |
| managed-window-retirement | 293.738 | 291.801 | -0.66% | -5.26% | Flag |

### Real native editors

| Phase | Baseline ms | Candidate ms | Wall change | Owner CPU change | Screening |
| --- | ---: | ---: | ---: | ---: | --- |
| editor-source-sync | 2733.598 | 2636.066 | -3.57% | -3.66% | - |
| native-edit-notifications | 5986.106 | 5911.683 | -1.24% | -2.72% | - |
| synthetic-native-composition | 6312.154 | 5924.265 | -6.15% | -3.76% | - |
| native-selection-caret | 4196.036 | 4032.124 | -3.91% | -3.86% | - |
| retained-editor-scroll | 2403.863 | 2501.783 | +4.07% | +4.03% | Flag |
| native-editor-paint | 530.563 | 531.630 | +0.20% | +0.00% | Flag |
| real-hwnd-uia-getters | 4478.189 | 4408.194 | -1.56% | -6.17% | Flag |

Query/process CPU must not be hidden by lower owner-CPU medians:

| Phase | Query CPU median B -> C, ms | Change | Process CPU median B -> C, ms | Change |
| --- | ---: | ---: | ---: | ---: |
| editor-source-sync | 2562.500 -> 2468.750 | -3.66% | 2562.500 -> 2468.750 | -3.66% |
| native-edit-notifications | 5750.000 -> 5593.750 | -2.72% | 5750.000 -> 5593.750 | -2.72% |
| synthetic-native-composition | 5406.250 -> 5203.125 | -3.76% | 5406.250 -> 5203.125 | -3.76% |
| native-selection-caret | 4046.875 -> 3890.625 | -3.86% | 4046.875 -> 3890.625 | -3.86% |
| retained-editor-scroll | 2328.125 -> 2421.875 | +4.03% | 2328.125 -> 2421.875 | +4.03% |
| native-editor-paint | 531.250 -> 531.250 | +0.00% | 531.250 -> 531.250 | +0.00% |
| real-hwnd-uia-getters | 1000.000 -> 1140.625 | +14.06% | 4359.375 -> 4375.000 | +0.36% |
| Component sum | 22140.625 -> 21421.875 | -3.25% | 25281.250 -> 24687.500 | -2.35% |

### Large native collection/grid/list host

| Phase | Baseline ms | Candidate ms | Wall change | Owner CPU change | Screening |
| --- | ---: | ---: | ---: | ---: | --- |
| scroll-sync | 1490.839 | 1434.339 | -3.79% | -4.21% | - |
| selection-publication | 838.147 | 735.743 | -12.22% | -9.62% | - |
| source-publication | 356.224 | 314.263 | -11.78% | -4.76% | - |
| paint-visible-rows | 904.486 | 851.898 | -5.81% | -5.17% | - |

### Separate original Button/NumericInput protocol

| Phase | Iterations | Median wall B -> C, ms | Wall change | Median owner CPU B -> C, ms | CPU change |
| --- | ---: | ---: | ---: | ---: | ---: |
| observed-button | 134217728 | 820.5052 -> 497.2046 | -39.40% | 812.500 -> 500.000 | -38.46% |
| composed-numeric-step | 1048576 | 739.3842 -> 796.0215 | **+7.66%** | 734.375 -> 796.875 | **+8.51%** |

Numeric stepping also crosses in wall sum **+6.5860%**, wall paired median
**+8.1897%**, owner-CPU sum **+6.5868%** and owner-CPU paired median **+8.3333%**.
The wall median adds **56.6373 ms / 1,048,576 steps**, about **54.01 ns/step**.
The preceding candidate's lower numeric median is not substituted or pooled.
Both versions have exactly 4,291,458 ordinary allocations / 187,658,304 requested
bytes in every warmup/recorded numeric process; ordinary Button dispatch allocates
zero times. The independent audit verifies full callbacks, checksum and final value.

## Allocation, source work and resources

All per-phase ordinary C++ allocation counts/requested bytes match the baseline
in the model matrix except caption action, which improves as below. The native
collection phase allocation counts also match. These hooks do not include aligned
allocations or allocations inside DLL/COM/OS heaps.

| Workload | Baseline | Final candidate | Change |
| --- | ---: | ---: | ---: |
| Caption phase ordinary allocations | 11132704 | 0 | -100% |
| Caption phase requested bytes | 801554688 | 0 | -100% |
| Managed subscribe/remove bytes | 108111400 | 86489224 | -20.00% |
| Managed rollback bytes | 19288696 | 13133272 | -31.91% |
| Managed commit/clear bytes | 59429688 | 28614312 | -51.85% |
| Managed window-retirement bytes | 74938888 | 91906184 | **+22.64%** |

Managed allocation totals are thread-local GC allocation observations, not native
heap or full retained-memory measurements. Window lifecycle still has a setup
storage tradeoff; a lower wall median is not a waiver.

The 278,752-selection Miller phase now matches baseline exactly:
278,752 item reads, 557,504 key calls, 836,256 finds, 557,505 ordinary allocations
and 17,840,208 requested bytes. Aggregate million-source reads/keys/finds also
match: 6,056,864 / 6,335,621 / 10,425,058. The original doubled work is gone,
without claiming every timing difference has a known cause.

ABI content still returns to six retained handles. Native editor and collection
fixtures retain their exact effects, UIA/password/retirement contracts,
13 editor destructions, four collection peers/five child HWNDs/one target, and
zero unchanged-editor scroll writes. Managed post-study median handles are
235 -> 235; GDI 0 -> 0; USER 1 -> 1. Managed live bytes are
389,720 -> 390,224; process private bytes 19,480,576 -> 19,849,216.
Those are process observations after collection, not complete memory attribution.

## All 32 construction/storage observations

This inventory constructs/destroys actual models outside measured dispatch.
Object bytes exclude referenced heap state. More setup allocations can coexist
with smaller model objects.

| Type | Object bytes B -> C | Setup allocations B -> C | Setup requested bytes B -> C |
| --- | ---: | ---: | ---: |
| Label | 584 -> 536 | 3 -> 3 | 704 -> 656 |
| Button | 592 -> 448 | 2 -> 2 | 696 -> 552 |
| Toggle | 512 -> 464 | 2 -> 2 | 616 -> 568 |
| CheckBox | 584 -> 536 | 2 -> 2 | 688 -> 640 |
| TextInput | 880 -> 736 | 2 -> 2 | 984 -> 840 |
| ComboBox | 656 -> 624 | 13 -> 14 | 3296 -> 3112 |
| NumericInput | 640 -> 560 | 57 -> 60 | 5560 -> 5600 |
| RangeInput | 696 -> 600 | 2 -> 2 | 800 -> 704 |
| RadioGroup | 624 -> 480 | 2 -> 2 | 728 -> 584 |
| SelectorBar | 624 -> 480 | 3 -> 3 | 760 -> 616 |
| TabStrip | 960 -> 928 | 5 -> 6 | 1792 -> 1800 |
| NavigationView | 1160 -> 1128 | 44 -> 49 | 8784 -> 8712 |
| NavigationPane | 624 -> 656 | 24 -> 24 | 4536 -> 4232 |
| MillerColumns | 768 -> 696 | 226 -> 226 | 64264 -> 56512 |
| Breadcrumb | 528 -> 496 | 8 -> 8 | 1608 -> 1432 |
| CommandBar | 552 -> 520 | 6 -> 6 | 1432 -> 1256 |
| CommandMenu | 1128 -> 952 | 3 -> 5 | 1264 -> 1456 |
| TitleBar | 632 -> 600 | 34 -> 36 | 8464 -> 7824 |
| ColorPicker | 624 -> 592 | 275 -> 296 | 27988 -> 29052 |
| InlineStatus | 568 -> 536 | 8 -> 9 | 2160 -> 2024 |
| VectorCanvas | 568 -> 536 | 7 -> 9 | 1960 -> 2104 |
| MultilineText | 904 -> 856 | 3 -> 3 | 1040 -> 992 |
| RichText | 896 -> 848 | 3 -> 3 | 1032 -> 984 |
| PasswordInput | 560 -> 512 | 3 -> 3 | 696 -> 648 |
| DateTimePicker | 592 -> 544 | 2 -> 2 | 696 -> 648 |
| ItemsView | 864 -> 672 | 3 -> 3 | 1016 -> 824 |
| TreeView | 928 -> 736 | 3 -> 3 | 1176 -> 984 |
| DataGrid | 1368 -> 1320 | 3 -> 3 | 1504 -> 1456 |
| Progress | 584 -> 536 | 3 -> 3 | 720 -> 672 |
| ContentDialog | 320 -> 336 | 36 -> 39 | 7936 -> 7704 |
| LocationPicker | 96 -> 112 | 47 -> 49 | 9192 -> 8856 |
| ViewPicker | 64 -> 80 | 19 -> 21 | 3944 -> 3848 |

The separate observed-Button setup is 2 -> 3 allocations / 232 -> 304 bytes.
Observed NumericInput setup is 60 -> 64 / 5096 -> 5288 bytes. Those include
observation/range setup absent from the construction-only inventory above.
Actual common model sizes are Control 440 -> 392, Button 592 -> 448,
TextInput 880 -> 736, NumericInput 640 -> 560 and ComboBox 656 -> 624 bytes.

## Remaining signals and limits

Strong displayed wall crossings include RadioGroup selection +13.30%,
multiline commit +17.42%, RichText commit +19.14%, password commit +8.52%,
ItemsView selection +7.80%, color channel +5.99%, scene selection +8.69%,
ABI rollback +12.40%, ABI window lifecycle +5.06% and managed subscribe/remove
+6.24%. Numeric stepping is separately +7.66%; UIA query CPU is +14.06%.
All 22 matrix flagged phases remain marked, including sub-5% medians with
sum/paired/CPU crossings. None is dismissed as noise or waived by faster controls.

For scale, ABI rollback adds 40.2252 ms / 425,600 operations
(94.51 ns/operation); multiline adds 50.6887 ms / 4,024,128 commits
(12.60 ns/commit); RichText adds 58.6256 ms / 4,103,136 commits
(14.29 ns/commit); managed subscribe/remove adds 21.5904 ms / 450,464 cycles
(47.93 ns/cycle). These averages of phase differences are not latency percentiles
and do not resolve attribution or accept the tradeoff.

The completed final correctness gate passes 53 native tests (six lifetime-labelled),
compiler/generated/MSBuild/binding coverage, 23 managed scope
assertions and the existing Rust suites (1 sys / 39 library / 2 compile-fail docs).
Seven selected owned-desktop native cases pass again. New checks cover scope-wide
cancellation before reentry, unlink-before-capture-destruction, in-flight rebinding,
over-aligned observer snapshots, typed cleanup, self-unsubscribe/re-subscribe and
dormant-child observers configured before Miller population.

This representative matrix is not every alias, style, application scene,
foreground frame or physical input path. Physical IME/Narrator/foreground latency,
media/web/Shell I/O, full private-memory attribution and historical phase-3 limits
remain outside the demonstrated acceptance. Phase 5 is not merged; phases 6-8
are not started. No additional optimization or measurement cycle follows this
final handoff without a new direction.

## Persistent raw evidence and reconstruction

[The compressed correction companion](stage5-callback-correction-evidence.json.gz)
retains **1,843 byte-hashed artifacts / 26,801,920 original bytes**, including
all final/intermediate/stopped blocks, exact copied final source, input hashes,
commands, build/test failures, audits and raw distributions.
Executables/libraries are fingerprinted, not embedded.

Decompress with `gzip.decompress`, parse the JSON, then reconstruct each artifact's
`base64` bytes without newline normalization and verify its `bytes`/`sha256`.
The 36,132,651-byte decompressed JSON has SHA-256
`6B17D34EB9D0EDBF6619C713B703340B32314537F50B61B534263E5F1F3ED106`.
The deterministic filename-free/mtime-zero gzip is 6,406,716 bytes, SHA-256
`75357868F4CF6F7FE11769DD4E332ADA05CA2003694053F4951788FFA815D730`.
Repeated compression and every artifact's decode/hash/length roundtrip passed.

Intermediate r1 has only three measured groups and a subsequently edited live
header pin, so complete r1 source reproducibility is not claimed. R2 model
completed, then ABI stopped on an old probe's count mismatch. R3 and r5/r6 have
their own separate completed studies; r4 images were built/verified but not timed.
The rejected inline observer and the invalid nested-ABI-call test attempt remain
visible in their original outputs. No earlier block is pooled with r7.
