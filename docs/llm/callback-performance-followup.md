# Callback delivery and subscription churn follow-up

## Result and scope

This follow-up keeps phase 5's separate composition/observer roles and scoped
retirement. It removes redundant immediate-delivery scaffolding and repeated
managed forwarding-delegate construction. It does **not** make performance
acceptance green: the single final comparison reports **55 overlapping >5%
statistics across 17 ordinary phases and the collection group sum**.
The separate Button/NumericInput block adds no threshold flags, but numeric
stepping is still 4.29% slower by wall median.

The primary comparison is directly against unmerged
[#63](https://github.com/zadjii-msft/xui/pull/63), frozen at
`233f042f63003f929a75a272cf0282f524b5ec1f` (functional implementation
`4dea0022ff1b747bdaaa70bb0a3525c85308c077`). The measured follow-up code is
`a328626c32ca6023b85e89724abe474e9efb377e`. The measurement date is
November 5, 2026 UTC / November 4 local time.

Managed subscribe/remove improves 17.34% wall / 19.23% owner CPU, with
86,489,224 -> 264 allocated managed bytes over 450,464 cycles. Radio selection
improves 3.83% wall; RangeInput changes improve 5.06%. Conversely, color channels
regress 8.82%, ABI window lifecycle 9.47%, collection scrolling 11.27%, and
collection selection publication 7.50%. Group totals do not waive these phases.
This remains a draft-quality performance result, not a no-regression claim.

The [phase-5 final correction report](stage5-callback-correction-performance.md)
retains the separate historical comparison against merged phase 4,
`de9ee24ea4664f0f6d0fcaf87f573383086244da`. There is **no newly paired phase-4
arm** here. Multiplying percentages from the two studies would not establish
paired acceptance against phase 4.

## Changes and preserved contracts

- `src/foundation.cpp`: immediate RadioGroup, RangeInput and NumericInput
  notifications call the existing `Callback` entry point after model mutation.
  Unchanged numeric values no longer take an unused callback snapshot.
- `src/collections.cpp`: immediate selection notification likewise lets
  `Callback` capture and deliver instead of copying, checking and consuming a
  whole Callback at the caller.
- `bindings/dotnet/Xui/Controls.cs`: Control's event forwarder and Button's typed
  adapter are cached lazily. They reference the wrapper, not a saved user
  invocation list. Removing an unregistered handler still validates the native
  handle, including after content rollback.
- `bindings/dotnet/Xui/Window.cs`: general/menu unsubscription records that its
  dictionary entry was already removed. Deferred release does not look it up
  again. Scope-driven cleanup retains the identity check so an older in-flight
  registration cannot remove its replacement.

No changes were made to `include/xui/callback.hpp`, Button's native activation
contract, public setter semantics, native hosts, ABI wiring, Miller/file
registration removal, numeric formatting, or document/password/date commits.
Explicit early snapshots remain where intervening work matters, including
Button toggle and TextInput's stable text value.

The shared helper still captures the selected observer before composition,
delivers internal behavior first, keeps in-flight callables alive, and applies
current-key gates. Scope cancellation still precedes reentrant cleanup.
Retained children, all 32 Miller child identities, owning-UI-thread rules,
weak GCHandles, in-flight managed leases, rollback, exact busy/error contracts,
and native EDIT/RichEdit/IME/caret/clipboard/undo/UIA ownership are unchanged.
No global pool, shared-window-root redesign, observer-inline experiment,
padding/layout tuning, IPO disable, or phase-6 work is included.

## Attribution before timing

### Immediate native dispatch

Matched diagnostic full-LTCG relinks show RadioGroup selection's out-of-line
symbol extent shrinking from 336 to 176 bytes, NumericInput change from 336 to
192 bytes, and RangeInput change from 352 to 208 bytes. VirtualCollection's
`changed` no longer has a separate out-of-line symbol in the candidate map.
RadioGroup's caller no longer contains the local snapshot/refcount/preflight
and destruction sequence; it calls the existing capture-safe helper.

These are symbol extents in diagnostic map/disassembly outputs, including
alignment, not dynamic instruction counts or a general instruction-cache
explanation. Capture work still occurs inside the helper. The real measurement
uses the original matched link options, not these diagnostic images.

### ABI first-subscription storage

The test-only census compiles the actual `src/c_api.cpp` into an executable
linked to each revision's static libraries. Its ordinary new/new[] hooks can
therefore see ABI implementation allocations that an external DLL caller
cannot. The matched x64 results are identical:

| Operation | Button allocations / bytes | TextInput allocations / bytes |
| --- | ---: | ---: |
| First subscription | 3 / 216 | 3 / 216 |
| Replacement | 0 / 0 | 0 / 0 |
| Remove/resubscribe | 0 / 0 | 0 / 0 |

These match three 72-byte immutable observer records for the adapters installed
by `wire` in `src/c_api.cpp` (focus plus click/toggle or change/submit).
The census is not a DLL timing result and excludes aligned/COM/OS heap activity.
It confirms setup work, not the cause of the ABI window timing regression.
One heap record is the current design's cost, not a universal safety lower bound.

### Managed storage and churn

The allocation-only census warms each operation, then records 256 iterations:

| Object or operation | PR63 bytes/operation | Follow-up bytes/operation |
| --- | ---: | ---: |
| CallbackLifetime object | 32 | 32 |
| Typed registration | 72 | 72 |
| Bare Window construction | 864 | 864 |
| Observed Button setup | 456 | 472 |
| Key handler setup | 72 | 72 |
| Navigation handler setup | 72 | 72 |
| Icon handler setup | 72 | 72 |
| Window disposal | 24 | 24 |

Thus a window's lifetime plus closed/key/navigation/icon registrations accounts
for 320 bytes of current registration infrastructure. This is not a complete
decomposition of the older study's roughly 288-byte net increase over phase 4:
net allocation also depends on the surrounding wrapper and teardown changes.
The follow-up leaves that infrastructure intact and adds 16 bytes to the
observed Button wrapper. Retaining those two delegate references buys zero
managed allocation for 1,024 warmed add/remove cycles in the regression test,
instead of constructing three forwarding/method-group delegates per cycle.
The final long churn phase's remaining 264 bytes include fixed setup/measurement
cost, not 264 bytes per cycle.

Cold content/window workloads pay for the added wrapper references. The full
study below reports those increases rather than hiding them behind churn savings.
The cache retains no removed user invocation list or old content scope.
Registration state remains bounded, but neither the historical +22.64% window
allocation increase nor the exact new totals are declared irreducible.

An initial candidate census output accidentally loaded the baseline managed
assembly through an incremental copy. Its hash mismatch was detected; the
invalid output is preserved as `managed-stale-reference.jsonl`. After explicit
copy/hash verification, the corrected output and a separate run of the
documented census agree. No timing sample was affected or replaced.

## Fixed comparison protocol

One final serial matrix: five groups, each with two warmup and seven recorded
alternating pairs; 90 processes / 1,098 timed phase rows. The separate original
Button/NumericInput protocol adds 18 processes / 36 timed rows. The first
matrix process began at 04:22:56 UTC; no build, test suite, diagnostic process
or status probe was launched alongside the timing blocks.

The baseline EXEs, DLLs, libraries and managed probe are byte-identical copies
of #63's frozen r7 candidate, checked against its retained SHA-256 manifest.
Its headers come from the exact PR63 source export, not phase-4 headers.
The candidate uses its own production libraries, native resource and matching
headers. The four C++ matrix fixtures and original numeric fixture are
byte-identical source; the managed Study harness EXE/DLL/runtime configuration
is reused byte-for-byte, with only the selected managed/native libraries changed.
Counts, phase order, effects and runner behavior remain unchanged.

MSVC x64 Release retains `/O2 /Ob2 /GL /MT` for matched fixture compilation
and full `/LTCG /INCREMENTAL:NO` for linking. The production gate independently
checks actual `/GL` and non-incremental `/LTCG` commands. Compiler/build command
logs, resource/library hashes, source snapshot, managed dependencies and copied
baseline provenance are in the evidence archive. Candidate frozen libraries
and DLLs were hash-checked against the functionally verified production outputs.
Resource/compiler fingerprints were recorded after the run from unchanged
build inputs; the actual measured images were pinned before and after timing.
Native work uses the existing unswitched owned-desktop runner.

All samples are retained. There are no retries, replacements, exclusions,
count retuning, pooled candidates, or timing-driven edits after this run.
External machine interference was not proven absent. Seven-sample P90 is the
maximum, not a well-estimated application latency percentile. Differing signs
and unchanged code paths are reasons for uncertainty, not grounds to discard
signals or declare them noise.

**Flag** below means any wall/owner CPU median, sum or paired median exceeded
5%; query/process CPU also participates for editors. All 55 raw flagged
statistics and every distribution/sample remain in `analysis.json`.

<!-- BEGIN GENERATED TABLES -->
## Complete timing tables

B = frozen PR63; C = this follow-up. Milliseconds and percentage changes are medians.
The last column is **historical PR63 versus phase 4**, not another arm of this run.

### Model

| Phase | B wall ms | C wall ms | Wall change | Owner CPU change | Screening | Historical wall change |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| toggle-invoke | 383.054 | 378.779 | -1.12% | +0.00% | - | +0.97% |
| checkbox-invoke | 413.312 | 374.069 | -9.49% | -7.69% | - | +0.09% |
| control-focus-cycle | 376.758 | 381.158 | +1.17% | +4.17% | - | +2.60% |
| label-property | 366.525 | 364.337 | -0.60% | +0.00% | - | -0.07% |
| progress-value | 366.359 | 369.100 | +0.75% | +0.00% | - | -3.60% |
| text-commit | 386.474 | 396.892 | +2.70% | +0.00% | Flag | -3.28% |
| text-submit | 313.141 | 314.374 | +0.39% | +0.00% | - | -7.34% |
| radio-selection | 384.142 | 369.434 | -3.83% | -4.00% | - | +13.30% |
| radio-accept | 313.613 | 337.915 | +7.75% | +10.00% | Flag | -17.72% |
| selector-selection | 327.896 | 321.073 | -2.08% | +0.00% | - | +1.84% |
| range-change | 331.101 | 314.348 | -5.06% | -9.09% | - | +3.37% |
| combo-editor-commit | 368.033 | 360.248 | -2.12% | +0.00% | - | +3.44% |
| combo-selection | 368.634 | 359.815 | -2.39% | -4.17% | - | +4.21% |
| multiline-commit | 287.060 | 282.345 | -1.64% | +0.00% | - | +17.42% |
| richtext-commit | 300.287 | 291.460 | -2.94% | +0.00% | Flag | +19.14% |
| password-commit | 369.754 | 385.420 | +4.24% | +4.17% | - | +8.52% |
| date-change | 366.129 | 361.283 | -1.32% | -4.17% | - | +4.63% |
| items-selection | 363.242 | 369.382 | +1.69% | +0.00% | - | +7.80% |
| items-activate | 357.170 | 363.770 | +1.85% | +4.35% | - | +1.89% |
| tree-selection | 373.969 | 366.671 | -1.95% | +0.00% | - | -1.18% |
| grid-selection | 372.815 | 366.105 | -1.80% | +0.00% | - | -15.38% |
| miller-selection | 345.889 | 346.460 | +0.16% | +0.00% | - | +0.51% |
| commandbar-invoke | 284.928 | 282.750 | -0.76% | -5.26% | - | -33.30% |
| breadcrumb-invoke | 294.726 | 299.032 | +1.46% | +0.00% | Flag | -15.52% |
| titlebar-caption | 97.321 | 101.425 | +4.22% | +0.00% | - | -71.79% |
| menubar-heading | 279.467 | 288.504 | +3.23% | +5.56% | Flag | -16.68% |
| tabs-new-button | 244.509 | 260.459 | +6.52% | +6.25% | Flag | -27.07% |
| status-dismiss | 362.284 | 368.915 | +1.83% | +0.00% | - | +4.87% |
| color-channel | 349.638 | 380.465 | +8.82% | +9.09% | Flag | +5.99% |
| scene-accessible-selection | 372.966 | 365.991 | -1.87% | +0.00% | Flag | +8.69% |
| navigation-toggle | 383.434 | 387.226 | +0.99% | +0.00% | - | -6.17% |
| navigation-search | 400.495 | 385.777 | -3.67% | -3.85% | - | -5.75% |
| command-surface-search | 332.941 | 322.720 | -3.07% | +0.00% | - | -3.26% |
| location-search | 327.274 | 330.015 | +0.84% | +0.00% | Flag | +3.04% |
| viewpicker-size | 319.649 | 347.707 | +8.78% | +4.76% | Flag | -0.23% |
| dialog-primary | 339.164 | 359.326 | +5.94% | +9.52% | Flag | -4.77% |
| sum-of-measured-components | 12427.110 | 12453.024 | +0.21% | +0.51% | - | -2.47% |

### Abi

| Phase | B wall ms | C wall ms | Wall change | Owner CPU change | Screening | Historical wall change |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| abi-observed-invoke | 301.295 | 305.276 | +1.32% | +0.00% | - | -13.61% |
| abi-subscribe-replace | 228.958 | 227.709 | -0.55% | -6.67% | - | -35.08% |
| abi-unsubscribe-resubscribe | 246.103 | 249.197 | +1.26% | +0.00% | - | -30.16% |
| abi-range-value | 327.490 | 317.435 | -3.07% | +0.00% | - | -3.74% |
| abi-miller-registration | 346.976 | 351.305 | +1.25% | +0.00% | - | -4.65% |
| abi-content-rollback | 366.742 | 366.769 | +0.01% | +0.00% | - | +12.40% |
| abi-content-commit-clear | 385.480 | 364.395 | -5.47% | -4.17% | - | +2.58% |
| abi-window-retirement | 350.697 | 383.908 | +9.47% | +13.64% | Flag | +5.06% |
| sum-of-measured-components | 2576.149 | 2646.947 | +2.75% | +3.05% | - | -9.26% |

### Managed

| Phase | B wall ms | C wall ms | Wall change | Owner CPU change | Screening | Historical wall change |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| managed-observed-invoke | 297.954 | 293.207 | -1.59% | +0.00% | - | +1.03% |
| managed-subscribe-remove | 394.876 | 326.414 | -17.34% | -19.23% | - | +6.24% |
| managed-choice-selection | 287.696 | 262.834 | -8.64% | -5.56% | - | -2.79% |
| managed-content-rollback | 193.141 | 197.068 | +2.03% | +8.33% | Flag | -4.21% |
| managed-content-commit-clear | 252.942 | 260.318 | +2.92% | -5.88% | - | -11.75% |
| managed-window-retirement | 316.105 | 305.224 | -3.44% | -5.00% | Flag | -0.66% |
| sum-of-measured-components | 1757.869 | 1675.127 | -4.71% | -5.36% | - | -0.93% |

### Editor

| Phase | B wall ms | C wall ms | Wall change | Owner CPU change | Screening | Historical wall change |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| editor-source-sync | 2516.519 | 2510.902 | -0.22% | +1.95% | - | -3.57% |
| native-edit-notifications | 5773.310 | 5745.623 | -0.48% | +0.28% | - | -1.24% |
| synthetic-native-composition | 6329.953 | 6271.962 | -0.92% | +1.17% | - | -6.15% |
| native-selection-caret | 3837.121 | 3898.428 | +1.60% | -1.25% | - | -3.91% |
| retained-editor-scroll | 2590.764 | 2563.449 | -1.05% | -0.65% | - | +4.07% |
| native-editor-paint | 554.514 | 542.686 | -2.13% | -2.86% | - | +0.20% |
| real-hwnd-uia-getters | 4530.294 | 4664.223 | +2.96% | -4.76% | - | -1.56% |
| sum-of-measured-components | 26443.443 | 26134.013 | -1.17% | -0.65% | - | -3.14% |

### Collection

| Phase | B wall ms | C wall ms | Wall change | Owner CPU change | Screening | Historical wall change |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| scroll-sync | 1445.051 | 1607.844 | +11.27% | +7.61% | Flag | -3.79% |
| selection-publication | 746.665 | 802.667 | +7.50% | +6.38% | Flag | -12.22% |
| source-publication | 320.819 | 328.436 | +2.37% | +4.76% | Flag | -11.78% |
| paint-visible-rows | 846.993 | 849.864 | +0.34% | +0.00% | - | -5.81% |
| sum-of-measured-components | 3409.675 | 3643.995 | +6.87% | +6.51% | Flag | -6.97% |

The component sums are fixed synthetic weights, not application usage weights.

### Native query and process CPU

| Phase | B query ms | C query ms | Query change | B process ms | C process ms | Process change |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| editor-source-sync | 2406.250 | 2453.125 | +1.95% | 2406.250 | 2453.125 | +1.95% |
| native-edit-notifications | 5609.375 | 5625.000 | +0.28% | 5609.375 | 5625.000 | +0.28% |
| synthetic-native-composition | 5359.375 | 5421.875 | +1.17% | 5359.375 | 5421.875 | +1.17% |
| native-selection-caret | 3750.000 | 3703.125 | -1.25% | 3750.000 | 3703.125 | -1.25% |
| retained-editor-scroll | 2406.250 | 2390.625 | -0.65% | 2406.250 | 2390.625 | -0.65% |
| native-editor-paint | 546.875 | 531.250 | -2.86% | 546.875 | 531.250 | -2.86% |
| real-hwnd-uia-getters | 1078.125 | 1031.250 | -4.35% | 4531.250 | 4484.375 | -1.03% |
| sum-of-measured-components | 21531.250 | 21265.625 | -1.23% | 24828.125 | 24781.250 | -0.19% |

### Original Button/NumericInput comparison

| Phase | Iterations | B wall ms | C wall ms | Wall median | Wall sum | Wall paired median | CPU median | CPU sum | CPU paired median |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| observed-button | 134,217,728 | 510.1099 | 508.3341 | -0.35% | -3.68% | -0.60% | +0.00% | -4.15% | -3.03% |
| composed-numeric-step | 1,048,576 | 766.9816 | 799.9065 | +4.29% | +1.95% | -0.45% | +4.08% | +1.98% | +0.00% |

Ordinary Button dispatch allocates zero times. Numeric stepping retains exactly
4,291,458 ordinary allocations / 187,658,304 requested bytes in every process.
Observed setup remains Button 3 / 304 bytes and NumericInput 64 / 5,288 bytes.

## Allocation and work inventory

| Managed phase | B bytes | C bytes | Change | Iterations |
| --- | ---: | ---: | ---: | ---: |
| managed-observed-invoke | 168 | 168 | +0.00% | 1,437,312 |
| managed-subscribe-remove | 86,489,224 | 264 | -100.00% | 450,464 |
| managed-choice-selection | 72 | 72 | +0.00% | 1,119,008 |
| managed-content-rollback | 13,133,272 | 13,426,392 | +2.23% | 7,328 |
| managed-content-commit-clear | 28,614,312 | 29,348,008 | +2.56% | 45,856 |
| managed-window-retirement | 91,906,184 | 92,848,776 | +1.03% | 58,912 |

The wrapper caches add 40 bytes per rollback fixture (one Button and three other
Control wrappers), and 16 bytes per commit/clear or window fixture. These exact
increments account for the cold-phase managed allocation increases above.

All model and collection ordinary allocation counts/requested bytes and source-work
counts match PR63 exactly in every process, including warmups. Model nonzero
allocation phases are listed below; all other model phases allocate zero times:

| Model phase | Allocations, B = C | Requested bytes, B = C |
| --- | ---: | ---: |
| multiline-commit | 4,024,128 | 128,772,096 |
| richtext-commit | 4,103,136 | 131,300,352 |
| items-selection | 3,810,689 | 121,942,096 |
| items-activate | 7,745,536 | 247,857,152 |
| tree-selection | 2,966,977 | 94,943,312 |
| grid-selection | 1 | 80 |
| miller-selection | 557,505 | 17,840,208 |
| color-channel | 2,443,584 | 104,259,584 |
| scene-accessible-selection | 1,550,177 | 124,014,160 |
| navigation-toggle | 5,368,896 | 348,381,696 |
| navigation-search | 4,530,688 | 303,556,096 |
| command-surface-search | 4,693,504 | 150,192,128 |
| location-search | 4,556,608 | 145,811,456 |

Miller's item/key/find work is unchanged; no extra available/item lookup or
retained-child deletion was introduced. ABI content returns to six retained
handles. Collection resources remain four peers / five child HWNDs / one target,
with zero unchanged-editor scroll writes. Editor getter checks require no model
callbacks, editor writes, recreation or geometry notifications.

| Post-study managed resource (median) | B | C |
| --- | ---: | ---: |
| managed_live_bytes | 390,232 | 390,560 |
| private_bytes | 19,750,912 | 19,857,408 |
| handles | 235 | 235 |
| gdi | 0 | 0 |
| user | 1 | 1 |

These post-GC process observations are not complete retained-heap attribution.

### All 32 construction/storage observations

Every value below is identical between PR63 and the follow-up in all processes.
Object sizes exclude referenced heap state; counts are outside timed dispatch.

| Type | Object bytes, B = C | Setup allocations, B = C | Requested bytes, B = C |
| --- | ---: | ---: | ---: |
| Label | 536 | 3 | 656 |
| Button | 448 | 2 | 552 |
| Toggle | 464 | 2 | 568 |
| CheckBox | 536 | 2 | 640 |
| TextInput | 736 | 2 | 840 |
| ComboBox | 624 | 14 | 3,112 |
| NumericInput | 560 | 60 | 5,600 |
| RangeInput | 600 | 2 | 704 |
| RadioGroup | 480 | 2 | 584 |
| SelectorBar | 480 | 3 | 616 |
| TabStrip | 928 | 6 | 1,800 |
| NavigationView | 1,128 | 49 | 8,712 |
| NavigationPane | 656 | 24 | 4,232 |
| MillerColumns | 696 | 226 | 56,512 |
| Breadcrumb | 496 | 8 | 1,432 |
| CommandBar | 520 | 6 | 1,256 |
| CommandMenu | 952 | 5 | 1,456 |
| TitleBar | 600 | 36 | 7,824 |
| ColorPicker | 592 | 296 | 29,052 |
| InlineStatus | 536 | 9 | 2,024 |
| VectorCanvas | 536 | 9 | 2,104 |
| MultilineText | 856 | 3 | 992 |
| RichText | 848 | 3 | 984 |
| PasswordInput | 512 | 3 | 648 |
| DateTimePicker | 544 | 2 | 648 |
| ItemsView | 672 | 3 | 824 |
| TreeView | 736 | 3 | 984 |
| DataGrid | 1,320 | 3 | 1,456 |
| Progress | 536 | 3 | 672 |
| ContentDialog | 336 | 39 | 7,704 |
| LocationPicker | 112 | 49 | 8,856 |
| ViewPicker | 80 | 21 | 3,848 |

<!-- END GENERATED TABLES -->

## Disposition and remaining uncertainty

The scoped changes have demonstrated allocation and code-generation benefits,
especially managed subscription churn. They do not establish that all phase-5
regressions are fixed, or that this layer makes nothing worse:

- Numeric stepping retains identical 4,291,458 allocations / 187,658,304 bytes
  but adds 4.29% wall / 4.08% CPU by median against PR63. Its paired wall median
  is -0.45%; that disagreement is retained, not used to waive the increase.
- Color-channel composition is directly coupled to NumericInput and remains a
  strong new regression. Items selection improves neither its wall median
  (+1.69%) nor the historical acceptance result. Radio selection improves, but
  radio acceptance, a different unmodified dispatch path, regresses.
- ABI rollback is essentially unchanged (+0.01%); window lifecycle is +9.47%.
  The first-subscription census confirms unchanged record counts, not a timing
  cause. No speculative teardown/wiring rewrite follows this result.
- Multiline/RichText/password/date still use unchanged `std::function` payload
  callbacks. This run's signs do not prove a layout explanation or erase the
  historical regressions. RichText remains flagged despite its lower median.
- Native editor scroll/paint/UIA have no >5% screening flags in this particular
  comparison. UIA wall is +2.96%, query CPU -4.35%, owner CPU -4.76% and process
  CPU -1.03%; there is no new paired phase-4 acceptance. Native collection
  scroll/selection regress despite exact source/effect/allocation invariants.
- Checkbox, focus, combo, date, status, command-search and managed-invoke
  historical flags remain visible in the older report, not reclassified as
  resolved simply because this direct-PR63 comparison is below threshold.
  All newly flagged phases, including sub-5% medians, remain marked above.

The necessary costs are the safety guarantees: independent observations,
in-flight ownership, current-key checks and cancellation before release.
The exact percentages here are not proven necessary costs. Further acceptance
would require an explicit maintainer decision about each residual, not an
aggregate waiver, weakened threshold or another sample-until-green loop.

## Correctness and evidence

The stable implementation passes the complete Framework gate: 53 native
headless tests (six lifetime registrations), compiler/generated/native-export
guards, 45,403 generator / 11 MSBuild assertions, 26 managed callback ownership
assertions and existing wrapper/style/feature/reveal/tab suites, plus Rust
1 sys / 39 library / 2 compile-fail tests. New cases cover immediate observer
replacement, unchanged numeric values, composition exceptions/retirement,
zero-allocation warmed subscription churn, multicast self-removal and stale
handle rejection before an adapter was ever cached.

Owned-desktop callback tests pass real NumericInput/ComboBox EDIT composition,
observer replacement/removal, native identity and callback close/failure.
Dirty-work tests preserve targeted synchronization and reentrant capture.
The final native-editor fixture verifies native composition, selection/caret,
UIA/password output shapes, retained geometry and 13 editor destructions.
The matrix audit checks all 108 process records, frozen inputs, serial order,
raw logs, 430 matrix comparisons, all thresholds, source work, ordinary
allocations and the 32 setup observations.

This does not demonstrate physical IME/Narrator/foreground latency, all desktop
scenes, complete native/private heap attribution, or removal of historical
stage-3 collection-paint concerns. Build setup errors (an export with a
nonexistent path, a premature managed build before xui.dll existed, and an
editor probe invocation without its required `--verify` argument) were corrected
before timing; none is silently counted as a passing verification or excluded
timing attempt.

Reproduction commands belong in
[CONTRIBUTING](../../CONTRIBUTING.md#callback-allocation-attribution).
The [lossless evidence archive](callback-performance-followup-evidence.json.gz)
contains byte-preserving base64 artifacts and SHA-256/length checks, including
raw logs, manifests, source snapshots, command logs, allocation census outputs,
diagnostic dispatch excerpts and audits. Binaries are fingerprinted, not embedded.
Local originals are under `build\callback-followup`; the archive does not require
that directory or the parent worktree to remain available.

The archive contains 486 artifacts / 11,891,300 original bytes, compressed to
2,490,436 bytes. Its SHA-256 is
`4E7012273509ED6D533E88DF4E4C92DC22CF18270790F24661149AA7EF10D096`.
Every decoded artifact's length and hash was checked after compression.
