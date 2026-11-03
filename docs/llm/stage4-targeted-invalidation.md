# Origin-aware targeted invalidation

## Scope and source map

This is stage 4 only, implemented directly without a child session.
It is submitted in [#62](https://github.com/zadjii-msft/xui/pull/62), open for review,
not merged.
The immediate baseline is merged stage 3:
`c0173871ddf6c17d25e66bddca83cf1ebf3a2e8b`
([#61](https://github.com/zadjii-msft/xui/pull/61)).
Callback ownership/registration, metadata authority, legacy foundation isolation
and general dependency enforcement remain later stages.

| Source | Responsibility |
| --- | --- |
| `include/xui/core.hpp`, `src/core.cpp` | `InvalidationEvent` propagates the stable originating element ID with its category through the existing weak parent states. `set_invalidator` remains a category-only adapter; `set_invalidation_handler` receives the event. |
| `src/dirty_work.hpp` | Coalesces value-only origin/work requests; topology escalates layout/placement/native/accessibility/paint. Separate retained buffers hold pending and captured requests. |
| `src/application.cpp` | Captures and consumes one pass without clearing newly raised work; reconciles only topology; selects affected peer subtrees and composite ancestors; preserves whole-window geometry, themes, modal/focus policy and native lifetime barriers. |
| Model setters and the C ABI selection adapter | Semantic changes request state/native/accessibility instead of disguising them as paint. Owned-child and lazy-page changes explicitly request topology. Pure visual state still requests paint. |
| `src/native_field_host.hpp`, `src/text_input_host.cpp` | Clear adornment requests identify their originating TextInput rather than widening all fields. The existing native EDIT/RichEdit bridges retain text/selection/undo/composition ownership. |
| `tests/dirty_work_tests.cpp` | Origin propagation, destroyed ancestors, category escalation, global scope and capture/coalescing. |
| `tests/dirty_work_window_tests.cpp` | Real HWND/UIA work isolation, reentrant native synchronization/measurement, stale replacement requests, lazy PageView realization, theme and closure; explicit optional measurement mode. |

The public model contract is described in
[application composition](../specs/application.md#ownership-and-property-updates).
Reusable commands are in [CONTRIBUTING](../../CONTRIBUTING.md#targeted-invalidation-checks).

## Work policy

Paint requests repaint the shared frame without recollecting topology or
synchronizing unrelated native/provider state. Help/automation metadata publishes
only relevant accessibility state. Native-property and combined state requests
synchronize the originating peer/subtree and enclosing composite state, not sibling
controls. Scope IDs do not retain models or native windows. The claim-backed origin
lookup is removed with the same existing ownership retirement.

Layout and placement retain conservative window-wide geometry/ordering; they do
not imply rediscovering the model tree. Topology is explicit for owned-child changes,
lazy-page selection, Reveal opening and popup/content
replacement. Native input can initiate topology, but retirement reconciliation stays
queued until the existing input barrier permits it.
Native clear adornments are collected and placed with their owner; their visibility
and inset changes do not schedule another whole-window layout. The broader editor
UIA fixture exposed that coupled geometry issue during this refactor; the correction
preserves exact clipped HWND bounds across real clear Invoke/native undo.

Global themes, DPI, window visibility/enabled state and modal coordination retain
full required native/accessibility work. Native runtime/producer clipping and the
single shared image-admission pass remain coordinated by Window. Changing an
ancestor surface recomputes native presentation from owned ancestry without
recollecting unrelated topology.

New requests raised by measurement, arrangement, provider/native synchronization
or callbacks go into the pending buffer. The captured buffer cannot erase them.
Reentrant `update` remains guarded by the existing synchronization and lifetime
scopes. Native clear controls retain their owner identity and lazy realization;
scroll-only updates preserve native editor identity and skip unchanged editor writes.

## Validation and measurement: November 3, 2026

Implementation commit: `ea11dd147ef3c63f9ab2a31f10abd804f48eeee1`.
The [raw companion](stage4-targeted-invalidation-evidence.json) preserves 133
byte-hashed artifacts, a 358-file source manifest, both timing blocks, final native
receipts and retained failures. Embedded bytes reconstruct with
`base64.b64decode(base64)`; no newline normalization is required.

The complete Framework gate passes 52 native tests on a noninteractive station,
including five lifetime registrations; generator/compiler/MSBuild, managed and Rust
coverage remain required. Release compilation retains `/GL` and full `/LTCG`.
The origin-aware state adds one 64-bit identity to the existing invalidation state;
it adds no field to public `Element` instances or per-row host resources.
The legacy category adapter retains its callback through a shared listener so
copying an in-flight callback does not allocate on the warmed notification path.
Window also keeps one non-owning origin-lookup entry per claimed model and two
capacity-retaining request vectors. This is bounded by owned model controls/layouts,
not virtual source row count; startup storage is not included in the timing table.

The final native checks pass: targeted dirty-work/UIA, the canonical real-editor
probe, 63 native-owner retirements/destructive callbacks, document range
editing/undo/redo, 200 ContentHost replacements, precision scrolling and navigation.
The ContentHost inactive-population check now requires zero typography/recollection
work for both active and inactive pages during ordinary local interactions.

The native fixture has 134 peers, two actual EDIT controls, a replaceable host and
lazy pages. For 16 verified edits per phase it observes:

| Phase | Topology / placement passes | Native peer syncs | Accessibility publications | Unrelated native property messages |
| --- | --- | --- | --- | --- |
| Paint-only | 0 / 0 | 0 | 0 | 0 |
| Local text | 0 / 0 | 16 | 16 | 0 |
| Help metadata | 0 / 0 | 0 | 16 | 0 |

The same fixture checks actual target EDIT text, unrelated text/selection and HWND
identity, real OS UIA HelpText, and zero GDI/USER growth. It checks that a sibling
invalidated during native synchronization is excluded from the captured pass and
delivered in the next pass; layout-time requests survive too. Replacement clears
old ownership before a stale retained element can enqueue more work.

The final fixed paired block uses separately built merged-baseline/candidate libraries,
the same fixture source/resource/runtime/compiler and `/GL /MT` full `/LTCG`.
The only baseline fixture define disables assertions about the new work counters;
it does not change measured effects or iterations. Two warmup pairs and seven
recorded pairs alternate order, retaining all 18 process outcomes and 54 phase rows.
No builds or other native work overlap the block. All work/resource checks pass.

Each phase performs 2,048 property edits plus real host synchronization over the
same retained 134-peer scene. Results for this workload alone:

| Phase | Median wall, baseline -> candidate | Median owner CPU, baseline -> candidate | Ordinary C++ allocations, baseline -> candidate |
| --- | --- | --- | --- |
| Paint-only | 778.123 -> 29.2849 ms (-96.24%) | 718.75 -> 31.25 ms | 606,208 -> 26,624 |
| Local native text | 878.387 -> 155.081 ms (-82.34%) | 796.875 -> 140.625 ms | 610,304 -> 49,152 |
| Help metadata | 763.037 -> 23.8522 ms (-96.87%) | 750 -> 15.625 ms | 606,208 -> 28,672 |

No median/sum/paired-median wall or owning-thread CPU statistic exceeds the 5%
regression threshold in these three phases. Raw kernel/user counters are retained
separately. Owning-thread CPU has Windows scheduling/timer granularity; ordinary
new/new[] excludes aligned allocations and COM/OS/DLL heaps.
The native observer records 2,048 unrelated EDIT property messages per baseline
phase and zero per candidate phase.

The earlier block for the pre-clear-correction candidate remains separately
retained, with its own distributions; it is not pooled into the final block.
The final block's sum-of-measured-components median wall is 2526.608 -> 210.6849 ms
(-91.66%); all per-phase and aggregate threshold statistics are checked.

These measurements are property synchronization, not repeated displayed frames,
physical input/foreground latency, graphics throughput or a universal speedup.
They do not supersede the historical stage-3 collection-paint timing concern or
unresolved physical IME/Narrator/private-memory/desktop evidence.
Retained failed build/native logs remain separate from timing samples; no timing
attempt was excluded or replaced.
