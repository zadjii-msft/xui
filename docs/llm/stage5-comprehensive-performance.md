# Phase-5 comprehensive performance study

## Conclusion

**The delta is not performance-neutral. Do not treat phase 5 as accepted.**
The previous Button/NumericInput comparison was too narrow to characterize this
change. The expanded study finds 161 over-5% threshold statistics across **29 of
61 ordinary phases**, plus two group aggregates. They are overlapping screening
statistics, not 161 independent regressions or significance tests.

The strongest additional problems are composed command/caption actions,
TextInput submit and choice acceptance, ComboBox edits/selections, and managed
subscription churn/rollback. There are also real-host signals in editor painting,
editor source synchronization and collection scrolling. Several other paths are
flat or faster, but those improvements do not cancel the regressions.

No production code was changed to obtain these results. The study compares merged
phase 4, `de9ee24ea4664f0f6d0fcaf87f573383086244da`, with the phase-5 PR head
`83b7d18548743d238b1c3128dec7a6e01dacabd0` (functional code `075196b2`).
Both production library/DLL identities match their retained evidence.
Earlier Button/NumericInput blocks remain separate, not pooled into this matrix.

## Protocol and coverage

| Group | Timed phases | Processes including warmups | Actual timed rows |
| --- | ---: | ---: | ---: |
| C++ control/model matrix | 36 | 18 | 648 |
| C ABI observations/registration/retirement | 8 | 18 | 144 |
| Managed events/registration/content/window lifetime | 6 | 18 | 108 |
| Real native editor/notification/composition/selection/UIA | 7 | 18 | 126 |
| Large virtual collection/grid/list host | 4 | 18 | 72 |
| **Total** | **61** | **90** | **1,098** |

Each group is one fixed two-warmup/seven-recorded-pair block. Even rounds run
baseline/candidate, odd rounds candidate/baseline. Every warmup and recorded
process, raw output, exit, UTC envelope, phase and exact logical effect is retained.
There are no exclusions, retries or replacements within these completed blocks.
Groups run serially; no agent-initiated builds or other native tests overlap them.
This is not dedicated performance hardware, and external machine interference is
not proven absent.

For model/ABI/managed work, one separately retained baseline-only sizing pilot
sets fixed iteration counts targeting 350 ms per phase (rounded to 32 operations,
capped at 134,217,728). The exact same counts apply to both revisions and all
processes, including warmups. Native probes retain their existing canonical work.
Counts, derivation and binaries are frozen before execution. Native compilation
uses the same compiler/resource/runtime, `/O2 /Ob2 /MT /GL` and full
`/LTCG /INCREMENTAL:NO`; managed fixtures use identical source with each revision's
own managed assembly and native DLL on .NET 10.

Workload invariants include exact callback counts/checksums, model state and real
native editing/UIA assertions. Resource checks retain native peer/HWND/target bounds,
13 editor destructions, password denial, real Text/Text2, zero unchanged-editor
scroll writes and GDI/USER bounds. ABI content operations restore six retained
handles. Managed post-study GDI/USER are 0/1 on both revisions.

The first model block stopped after its first baseline/candidate warmup because
the validator incorrectly required *source call counts*, not just logical effects,
to be equal across revisions. It exposed genuine extra Miller work. That stopped
block is preserved. The final model fixture reports reads/keys/finds per phase;
those counts must be identical within a revision, while baseline/candidate deltas
remain visible. Iterations, actions, selections, callbacks and checksums still
match across revisions. The other group images and iteration protocol did not
change. This is not a waived work failure or an excluded timing sample.

## How to read the tables

All times are **median milliseconds per fixed phase**, not per-event latency.
Positive percentages mean slower/more CPU. Owner CPU comes from raw kernel/user
100-ns counters; these counters have scheduling granularity, not 100-ns effective
resolution. The raw report also retains query/process CPU for native editors,
all samples, min/mean/P90/max, absolute deltas, sum changes, paired median changes
and how many candidate pairs are slower. P90 is the maximum of seven recorded
samples; it is not a well-estimated application P90.

**Flag** means *any* median, sum or paired-median wall/owner-CPU statistic exceeds
5%; query/process CPU is included for the editor group. A displayed median under
5% can therefore still be flagged. No confidence interval or all-noise claim is
made, particularly for marginal signals. The same threshold is applied to controls
whose implementation changed and negative-control paths that did not.

Group sums add each process's measured components before computing statistics.
These are synthetic, fixed-weight mixes, not application usage weights. Groups
are not pooled into a universal framework score. In particular, a nearly flat
editor total cannot waive slower editor painting.

### C++ control and composition paths

| Phase | Baseline ms | Candidate ms | Wall change | Owner CPU change | Threshold |
| --- | ---: | ---: | ---: | ---: | --- |
| `toggle-invoke` | 377.166 | 379.897 | +0.72% | +0.00% | - |
| `checkbox-invoke` | 381.145 | 379.710 | -0.38% | +0.00% | - |
| `control-focus-cycle` | 396.547 | 383.016 | -3.41% | -7.69% | - |
| `label-property` | 369.275 | 367.805 | -0.40% | +4.35% | - |
| `progress-value` | 368.883 | 370.813 | +0.52% | +0.00% | - |
| `text-commit` | 371.763 | 394.699 | +6.17% | +4.17% | **flag** |
| `text-submit` | 371.896 | 610.703 | +64.21% | +62.50% | **flag** |
| `radio-selection` | 369.716 | 402.578 | +8.89% | +8.33% | **flag** |
| `radio-accept` | 388.777 | 587.676 | +51.16% | +52.00% | **flag** |
| `selector-selection` | 330.913 | 346.168 | +4.61% | +0.00% | **flag** |
| `range-change` | 330.688 | 338.094 | +2.24% | +4.76% | **flag** |
| `combo-editor-commit` | 366.214 | 462.123 | +26.19% | +30.43% | **flag** |
| `combo-selection` | 369.008 | 432.736 | +17.27% | +12.50% | **flag** |
| `multiline-commit` | 287.225 | 285.700 | -0.53% | +0.00% | - |
| `richtext-commit` | 292.304 | 291.808 | -0.17% | +0.00% | - |
| `password-commit` | 376.482 | 376.831 | +0.09% | +0.00% | - |
| `date-change` | 368.688 | 368.199 | -0.13% | +0.00% | - |
| `items-selection` | 382.744 | 370.288 | -3.25% | +0.00% | - |
| `items-activate` | 377.710 | 372.601 | -1.35% | -4.00% | - |
| `tree-selection` | 372.088 | 373.300 | +0.33% | +0.00% | - |
| `grid-selection` | 383.051 | 372.611 | -2.73% | +0.00% | - |
| `miller-selection` | 352.000 | 382.531 | +8.67% | +8.70% | **flag** |
| `commandbar-invoke` | 363.739 | 918.508 | +152.52% | +145.83% | **flag** |
| `breadcrumb-invoke` | 362.044 | 593.642 | +63.97% | +65.22% | **flag** |
| `titlebar-caption` | 358.735 | 816.510 | +127.61% | +126.09% | **flag** |
| `menubar-heading` | 356.254 | 385.589 | +8.23% | +8.70% | **flag** |
| `tabs-new-button` | 335.180 | 434.558 | +29.65% | +22.73% | **flag** |
| `status-dismiss` | 346.608 | 388.373 | +12.05% | +13.64% | **flag** |
| `color-channel` | 340.215 | 348.761 | +2.51% | +0.00% | - |
| `scene-accessible-selection` | 335.903 | 331.129 | -1.42% | +0.00% | - |
| `navigation-toggle` | 381.556 | 346.571 | -9.17% | -4.35% | - |
| `navigation-search` | 357.904 | 359.163 | +0.35% | +0.00% | - |
| `command-surface-search` | 325.399 | 340.609 | +4.67% | +4.76% | **flag** |
| `location-search` | 319.280 | 372.469 | +16.66% | +20.00% | **flag** |
| `viewpicker-size` | 325.365 | 360.686 | +10.86% | +9.52% | **flag** |
| `dialog-primary` | 334.342 | 474.909 | +42.04% | +36.36% | **flag** |
| **Per-process component sum** | **13152.442** | **15442.008** | **+17.41%** | **+17.68%** | **flag** |

These are actual public model actions. Examples: ColorPicker changes a retained
NumericInput channel; VectorCanvas selects through its accessible ItemsView;
ViewPicker changes its real RangeInput and checks the target item's height.
Composite child observers are not added where the baseline would disable internal
behavior: the fixed workload observes the enclosing semantic event identically.
The existing correctness negative control covers the broken baseline semantics.

### C ABI paths

| Phase | Baseline ms | Candidate ms | Wall change | Owner CPU change | Threshold |
| --- | ---: | ---: | ---: | ---: | --- |
| `abi-observed-invoke` | 346.469 | 305.789 | -11.74% | -9.09% | - |
| `abi-subscribe-replace` | 358.683 | 230.877 | -35.63% | -34.78% | - |
| `abi-unsubscribe-resubscribe` | 354.748 | 251.854 | -29.00% | -30.43% | - |
| `abi-range-value` | 357.603 | 334.355 | -6.50% | -4.35% | - |
| `abi-miller-registration` | 377.062 | 355.980 | -5.59% | -4.17% | - |
| `abi-content-rollback` | 324.933 | 383.985 | +18.17% | +19.05% | **flag** |
| `abi-content-commit-clear` | 358.640 | 363.159 | +1.26% | +0.00% | - |
| `abi-window-retirement` | 357.093 | 363.855 | +1.89% | +0.00% | - |
| **Per-process component sum** | **2914.846** | **2649.094** | **-9.12%** | **-9.63%** | - |

### Managed paths

| Phase | Baseline ms | Candidate ms | Wall change | Owner CPU change | Threshold |
| --- | ---: | ---: | ---: | ---: | --- |
| `managed-observed-invoke` | 292.160 | 303.724 | +3.96% | +11.11% | **flag** |
| `managed-subscribe-remove` | 358.260 | 444.875 | +24.18% | +21.74% | **flag** |
| `managed-choice-selection` | 284.019 | 289.549 | +1.95% | +11.76% | **flag** |
| `managed-content-rollback` | 202.178 | 262.745 | +29.96% | +30.77% | **flag** |
| `managed-content-commit-clear` | 297.635 | 260.138 | -12.60% | -10.53% | - |
| `managed-window-retirement` | 296.260 | 299.044 | +0.94% | +0.00% | **flag** |
| **Per-process component sum** | **1758.187** | **1922.927** | **+9.37%** | **+9.82%** | **flag** |

Managed rollback binds general/menu/file/Miller handlers, then disposes its
candidate; commit/clear invokes its handler before retirement. Window retirement
includes key/navigation/icon registrations. GC collection counts and thread
allocation totals are retained, not discarded as outliers. Default .NET tiering/JIT
remains enabled. There is a common small pre-JIT warmup, but cold first-use costs
within a process can remain in these measured phases; these are not pure JIT-free
dispatch estimates.

### Real native editors and UIA

| Phase | Baseline ms | Candidate ms | Wall change | Owner CPU change | Threshold |
| --- | ---: | ---: | ---: | ---: | --- |
| `editor-source-sync` | 2452.853 | 2597.536 | +5.90% | +3.90% | **flag** |
| `native-edit-notifications` | 5973.270 | 5558.262 | -6.95% | -6.25% | - |
| `synthetic-native-composition` | 6175.965 | 6385.282 | +3.39% | +1.47% | - |
| `native-selection-caret` | 4006.269 | 3983.914 | -0.56% | -1.21% | - |
| `retained-editor-scroll` | 2424.044 | 2476.478 | +2.16% | +2.68% | - |
| `native-editor-paint` | 542.192 | 640.818 | +18.19% | +14.29% | **flag** |
| `real-hwnd-uia-getters` | 4571.007 | 4575.282 | +0.09% | +6.02% | **flag** |
| **Per-process component sum** | **26354.334** | **26401.370** | **+0.18%** | **+1.31%** | - |

The existing 56-record canonical fixture covers TextInput, password, date/time,
calendar, MultilineText/RichText, clear actions and NumericInput/ComboBox embedding;
it performs real HWND UIA/Text/Text2 queries and native teardown while the root
apartment is alive. Query and process CPU are retained separately. Outside the
cross-thread getter phase, its query-thread CPU observation aliases the owner
thread; correlated paint CPU crossings are not independent regressions.
Painting's absolute median difference is **98.6254 ms for 128 requested frames**.
No new ordinary C++ allocations were observed in any native-editor phase.
Uninstrumented OS/render work and causality are not inferred from equal counters.

### Large native-host collections

| Phase | Baseline ms | Candidate ms | Wall change | Owner CPU change | Threshold |
| --- | ---: | ---: | ---: | ---: | --- |
| `scroll-sync` | 1472.907 | 1594.714 | +8.27% | +9.78% | **flag** |
| `selection-publication` | 782.009 | 755.590 | -3.38% | -2.08% | - |
| `source-publication` | 327.649 | 322.365 | -1.61% | +0.00% | - |
| `paint-visible-rows` | 865.212 | 858.381 | -0.79% | +0.00% | - |
| **Per-process component sum** | **3493.259** | **3554.561** | **+1.75%** | **+1.37%** | - |

This unchanged scene retains a million-row virtual ItemsView, 100,000-row grid,
10,000-row legacy list and an unchanged native editor. Four peers, five child HWNDs,
one target, compact selection, exact source/callback/checksum work and zero editor
scroll writes match. Allocation counts match for all four phases in every process.
The scroll wall delta is **121.8066 ms per 32,768 updates**. This block does not
reclassify the historical stage-3 collection-paint signal: it uses the immediate
merged phase-4 baseline and a different frozen comparison.

## Allocation and source-work regressions

| Workload | Baseline -> candidate | Interpretation |
| --- | --- | --- |
| Miller model selection | 557,505 -> 1,115,009 ordinary allocations; 17,840,208 -> 35,680,336 requested bytes | Approximately doubled; not zero-cost ownership checking |
| TitleBar caption action | 11,132,704 -> 22,265,408 allocations; 801,554,688 -> 1,959,355,904 requested bytes | Doubled allocation count / +144.44% requested bytes |
| Managed add/invoke/remove | 108,111,400 -> 176,581,928 managed bytes | +63.33% |
| Managed content rollback | 19,288,696 -> 20,050,808 managed bytes | +3.95%, separate from its larger time regression |
| Managed window retirement | 74,938,888 -> 128,668,072 managed bytes | +71.70%, despite a nearly flat wall median |

These are totals for each phase's fixed count, not per-control object sizes.
The [machine-readable comparison](stage5-comprehensive-performance-evidence.json.gz)
retains unchanged/decreased allocation results too. ABI allocator internals are
not counted by the executable's ordinary new hooks, so no ABI zero-allocation
claim is made. Managed totals use `GC.GetAllocatedBytesForCurrentThread`; they
exclude native allocations and other threads.

Miller's 278,752 selections produce:

| Source operation | Baseline | Candidate |
| --- | ---: | ---: |
| `item()` reads | 278,752 | 557,504 |
| `key()` calls | 557,504 | 1,115,008 |
| `find()` lookups | 836,256 | 1,115,008 |

Current source checks `list.available(key)` after the parent callback, including
source lookup and item-enabled access. The fixture's source returns real row text;
the extra calls therefore include its allocation/atomic-counter costs. This
explains extra *work*, not every timing change. Caption composition copies retained
callables in `Callback::operator() &&` and the TitleBar wrapper; the observed
allocation increase is a concrete optimization target, not a claimed completed
causal fix. This request is an investigation; production behavior was not changed.

## Construction storage and costs

The following inventory constructs and destroys each actual model once per
process, outside measured dispatch. Counts are deterministic observations, not
construction timing estimates. Object bytes exclude referenced heap state, which
is why a smaller object can still cost more allocations.

| Type | Object bytes B -> C | Setup allocations B -> C | Setup requested bytes B -> C |
| --- | ---: | ---: | ---: |
| Label | 584 -> 544 | 3 -> 3 | 704 -> 664 |
| Button | 592 -> 472 | 2 -> 2 | 696 -> 576 |
| Toggle | 512 -> 472 | 2 -> 2 | 616 -> 576 |
| CheckBox | 584 -> 544 | 2 -> 2 | 688 -> 648 |
| TextInput | 880 -> 760 | 2 -> 2 | 984 -> 864 |
| ComboBox | 656 -> 648 | 13 -> 15 | 3296 -> 3192 |
| NumericInput | 640 -> 592 | 57 -> 63 | 5560 -> 5752 |
| RangeInput | 696 -> 616 | 2 -> 2 | 800 -> 720 |
| RadioGroup | 624 -> 504 | 2 -> 2 | 728 -> 608 |
| SelectorBar | 624 -> 504 | 3 -> 3 | 760 -> 640 |
| TabStrip | 960 -> 952 | 5 -> 7 | 1792 -> 1848 |
| NavigationView | 1160 -> 1152 | 44 -> 54 | 8784 -> 9072 |
| NavigationPane | 624 -> 680 | 24 -> 24 | 4536 -> 4312 |
| MillerColumns | 768 -> 720 | 226 -> 302 | 64264 -> 73112 |
| Breadcrumb | 528 -> 520 | 8 -> 8 | 1608 -> 1480 |
| CommandBar | 552 -> 544 | 6 -> 7 | 1432 -> 1392 |
| CommandMenu | 1128 -> 1000 | 3 -> 7 | 1264 -> 1520 |
| TitleBar | 632 -> 624 | 34 -> 38 | 8464 -> 8048 |
| ColorPicker | 624 -> 616 | 275 -> 314 | 27988 -> 30060 |
| InlineStatus | 568 -> 560 | 8 -> 10 | 2160 -> 2096 |
| VectorCanvas | 568 -> 560 | 7 -> 11 | 1960 -> 2176 |
| MultilineText | 904 -> 864 | 3 -> 3 | 1040 -> 1000 |
| RichText | 896 -> 856 | 3 -> 3 | 1032 -> 992 |
| PasswordInput | 560 -> 520 | 3 -> 3 | 696 -> 656 |
| DateTimePicker | 592 -> 552 | 2 -> 2 | 696 -> 656 |
| ItemsView | 864 -> 704 | 3 -> 3 | 1016 -> 856 |
| TreeView | 928 -> 768 | 3 -> 3 | 1176 -> 1016 |
| DataGrid | 1368 -> 1328 | 3 -> 3 | 1504 -> 1464 |
| Progress | 584 -> 544 | 3 -> 3 | 720 -> 680 |
| ContentDialog | 320 -> 352 | 36 -> 42 | 7936 -> 7888 |
| LocationPicker | 96 -> 128 | 47 -> 52 | 9192 -> 9144 |
| ViewPicker | 64 -> 96 | 19 -> 23 | 3944 -> 3960 |

These differ from the earlier isolated Button/NumericInput setup figures because
the construction/observation context differs; the earlier setup results are not
overwritten. Post-study managed recorded-process medians were:

| Resource | Baseline | Candidate |
| --- | ---: | ---: |
| Live managed bytes after full GC | 389,704 | 390,176 |
| Private bytes | 19,599,360 | 19,611,648 |
| Handles | 235 | 235 |
| GDI / USER objects | 0 / 1 | 0 / 1 |

These end snapshots do not establish leak-free behavior, peak retained memory,
graphics memory or full native heap attribution. Resource distributions and native
retirement snapshots are retained in the raw evidence.

## Limits, reproducibility and disposition

The matrix spans every changed composition/callback role through representative
public API operations, plus unchanged negative-control paths. It is not a literal
benchmark of every alias, style, source type or application scene. ToggleSwitch,
ToggleButton/HyperlinkButton, ProgressRing, MapView, choice aliases and related
surfaces share tested mechanisms but do not each have a separate timing row.
Runtime media/web loading, Shell/thumbnail I/O, native menu interaction, physical
IME/Narrator, foreground latency, pixels and complete renderer throughput remain
outside this study. No new claim is made for those surfaces.

Baseline and candidate have 36 model + 8 ABI + 6 managed phase counts frozen in
`counts.txt`. The probes accept `XUI_PERF_COUNTS` and reject invalid selectors;
the group runner refuses existing block directories, persists attempts before
launch, bounds each process at 200 seconds and checks raw output against recorded
rows. The native launcher uses a kill-on-close job with a 180-second native bound
on a unique unswitched desktop, not global input or clipboard mutation.
See [contributor commands](../../CONTRIBUTING.md#comprehensive-callback-performance-study).

An independent audit reconstructs all 90 process outputs and all per-phase/group
wall and owner-CPU medians, verifies 48 frozen identities and serial UTC envelopes,
and matches all 161 threshold crossings. Eight repository unit tests plus 21
retained offline protocol checks cover fail-closed validation; those are not
substitutes for the actual measured work.
The compressed companion preserves raw logs, samples, distributions/tails,
counts, commands, tool identities, source and validation failures. It does not
embed EXEs/libraries: their exact hashes and local retained paths are recorded.
It contains **236 byte-hashed artifacts / 3,307,427 original bytes**, with a
5,069,414-byte decompressed JSON payload (SHA-256
`5553B8BEFAB31BD25C41955284247BBE01C3A4ECC6A963017E8425628E7BFFA1`).
The deterministic filename-free gzip has modification time zero. Decompress with
Python's standard-library `gzip.decompress`; embedded artifact bytes reconstruct
with `base64.b64decode(base64)` and no newline normalization.
Production source is the referenced commit; the benchmark sources are committed
with this study. New link images are symmetric comparisons, not proof about every
previous binary layout.

**Disposition: performance acceptance remains blocked on more than NumericInput.**
Prioritize the definite per-event allocation/work regressions and large
model/registration slowdowns. Marginal/native signals require bounded diagnosis
or a declared confirmation protocol, not an all-noise assumption. Do not merge
or claim unchanged performance based on green correctness tests or group totals.
