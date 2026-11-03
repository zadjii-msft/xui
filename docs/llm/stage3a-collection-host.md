# Stage 3a: Collection-family Windows host boundary

This is the first family pilot, **not completion of stage 3 or finding 1**.
The [finite 3a-3f ledger](architecture-review-2026-10-21.md#finite-stage-3-family-ledger)
assigns every remaining family; stage 4 is blocked until all are accepted and merged.
No editor/runtime/basic/navigation-container/composite extraction, invalidation redesign,
callback redesign, metadata generation or stage-7 foundational list move is included.

## Base and ownership

Stage 3a was independently accepted at
`b655d0aa2564b58c4420917a6c8781a048ee7b04` and squash-merged as
`9e6673cffbb2f4fb320da652937729f1b7a03099` in #60. That merged revision is
the immediate baseline for 3b; it does not resolve the historical limitations below.

The immediate pre-3a baseline is verified merged stage 2,
`a3f112c9293d41998276f3e97e03e6f1f4b1ffd2` (#59 accepted
`38f377f4cea2e4632421c4871616dd54501045b4`), not original `99016573`.
Fetched main ancestry and the pinned source export were checked before implementation.
The coordinator independently compared all 954 exported files, native Git EOL conversion
and 60 frozen binary/library hashes. Baseline source and outputs were not rebuilt after
the candidate edits.

The [source map](architecture.md#collection-family-host-boundary) describes the actual
`HostPeer`, `CollectionHostPeer`, `GridHostPeer`, embedded `FileListHostPeer`/ListPeer and
temporary UnextractedPeer storage. Concrete row-family translation units own attach/sync,
paint/input/capture/gesture/wheel/image state, cancellation, publication and native teardown.
Collection/grid UIA use separate snapshots/shared state, not a universal state plus a
second behavior object or a largest-inline-variant allocation.

`PeerHostServices` names closed native-surface, eligibility/focus/reveal/invalidation,
tooltip and restricted OLE delivery/lifetime needs. Family files do not include or recover
Window::Impl. The window retains focus/modal/popup and cross-window coordination, topology,
shared Drawing/placement batching, scheduling, exception/dispatch barriers and whole-window
close. Shared row engines for NavigationList, MillerColumnList and CommandMenu move with
VirtualCollection; their enclosing containers and commands remain 3e.

Build/test procedures belong in [CONTRIBUTING](../../CONTRIBUTING.md#collection-family-host-checks).

## Accessibility and negative evidence

Typed collection/grid providers retain their own shared state independently of peer/root
owner lifetime. Snapshot replacement/disconnection moves retained sources/presentation/
selection and action maps out before releasing them. Source/query/release and application
callbacks do not run under accessibility state locks. Legacy WM_NCDESTROY likewise moves
the retained view out before deferred disposal. Common identity is separate from family
payload. Mailbox admission remains 32 collection / 64 grid, with exact live HWND/control/
token validation and explicit stale/error HRESULTs.

The baseline disconnect held collection source, grid source/full source and selection-only
source releases under its universal mutex. Parent's second-thread probe reproduced this;
ordinary publication was the positive control. The new ten-case test covers publication and
disconnect for those five retention slots, exactly one release each, bounded waits and
actual nonempty selection retention. It has no baseline-expectation escape selector.
Typed provider tests retain real virtual child/cell COM objects after state owner release,
reject stale/replayed tokens and preserve non-unique role capability validation.

## Native baseline and limitations

The frozen baseline native gate passed 47/47 with actual `/GL` and full `/LTCG`.
The final candidate used the independently fresh `stage3a-candidate-final4`
directory, not a relink into either earlier final directory.
Its ordinary `framework-pr.ps1 -Suite All` gate passed all 49 registered native tests
and generated, managed and Rust checks. The baseline has 47 native tests; the two added
tests cover typed provider capability/lifetime and lock-safe source retirement.
The tightened Native gate also passed: both named family tests are mandatory, with
minimum 49 headless / two lifetime registrations (the core minimum remains 37).
The coordinator independently passed final3 complete All against its separate native build,
including full `/GL`/`/LTCG` guards and shared binding checks; all 970 checked source
files were byte-identical throughout 10:31:19-10:32:52Z. The raw companion attributes
those reported counts separately from this worktree's own logs. This describes the
earlier final3 production/gate revision, before the probe-only hover control; those
frozen libraries did not change, and final4 was built independently afterward.
The prior complete All was rerun successfully on probe commit
`0d42d1bc371211cce4ccde4cfca806ce00672329`; both measured production library hashes
remained identical. That final3 log is retained separately from the later final4 gate.
The native matrix retains earlier failures alongside the final4 focused passes:

| Fixture | Immediate stage-2 baseline | Observed candidate (build noted for earlier results) |
| --- | --- | --- |
| Collections `--case 1 96` (native and isolated UIA client) | Initial pass; intermittent retained-popup-peer failure reproduced below | Final4 pass; original final2 isolated-client retained-popup-peer failure and passing matched reruns retained below |
| Full grid | Pass | Pass |
| Style collections/grid | Pass / Pass | Pass / Pass |
| File transfer default | Pass | Pass |
| Collections `--miller-only` | Fails header pointer focus/selection contract before UIA phase | Same failure on final2; not reclassified by later focused passes |
| Collection presentation | Fails outgoing-pixels native UIA point lookup | Same failure on final2; not reclassified by later focused passes |
| Scroll `--retained-only` | Fails translated native editor notification contract | Same failure on final2; not reclassified by later focused passes |
| Precision scroll / owned-child native policy | Existing fixtures | Pass / Pass |
| Full ContentHost replacement | Existing fixture | Pass, including 200 native replacements, retained UIA and explicit fatal materialization failure |
| Identical real-host probe | Pass | Pass, including partial collection/grid attach failure and complete teardown |
| Full thumbnail fixture | Fails browser filtering previous-row-thumbnail pixel assertion after public-list cases | Same failure on final3 |
| Existing public-list / tall-list / blocked-close thumbnail cases | Pass | Final3 pass: identical wrappers, unchanged assertions, disable/GPU release, hidden 24-slot cap and blocked filtering/close cancellation |
| Existing blocked-Shell wrapper | Fails dedicated-COM-STA assertion before hidden-page/shared-48 integration | Same assertion on final3; not positive hidden-page coverage |

These failures are not green, not removed assertions, and not blanket waivers for the
extraction. The first attempted fresh final directory failed compiling a test that
incorrectly supplied a Control rather than Stack to `Window::set_content`; after fixing
that fixture, the complete gate ran in a new independent final2 directory. No SUT
correctness workaround, cache deletion or IPO disabling was used.
The initial final3 focused build also requested a nonexistent target,
`xui_file_transfer_window_tests`; correcting it to the existing
`xui_file_transfer_tests` completed the build without a SUT compile change.

### Retained-popup-peer diagnostic

The original final collection failure is retained, not replaced by passing reruns.
An identical diagnostic fixture logged the one actual peer-count read at the unchanged
`peers == 8` assertion and at the native/driver handoff and both filter actions.
Five counterbalanced pairs failed twice on each revision: all failures observed
**18 peers / zero active popups**. Owned-child enumeration identifies the extra ten
as hidden, closed-popup subtrees. This is an observed matching failure signature,
not statistical failure-rate equivalence.

The requested bounded follow-up used separate instrumented application objects/archive
copies, never the frozen baseline or real candidate libraries. Three further paired
runs reproduced the failure once per revision. At the same failed metrics14 read,
both report `input_depth=1`, `dispatch_depth=0`, `syncing=0`, `painting=0`, and
`pending/layout_pending/placement_pending/state_pending/scroll_pending=0`, with 18 peers
and no popups. Preceding prune attempts have `input_depth=2`, `syncing=1` and return
without retiring peers. Both eventually report eight peers after close-time pruning.

Current and baseline code have the same scheduling: InputScope exit is not a retirement
barrier, update attempts pruning before its own InputScope, prune returns when an outer
input scope exists, and metrics14 is only an observation. The metrics message itself
enters InputScope: its observed depth of one does **not** prove that an earlier outer
input scope remains active. The snapshots establish a matching retained-peer/handoff
failure signature, not a complete scheduler root-cause proof. They do not fix it or make the
desktop matrix green. Scheduling redesign and changed tolerances/sleeps/assertions are
out of this PR. Diagnostic investigation stops here.

The stage-1 desktop and stage-2 stock/default optimizer noise, 1,278-process companion,
physical IME/Narrator, valid foreground-native latency/private memory and inspection/
highlight limitations remain unresolved. New probe resource counters are not evidence
that those previously invalid native comparisons have become valid.

## Final evidence

### Concrete peer and accessibility storage

MSVC class layout and actual `operator new` requests were measured with the same x64
Release compiler/CRT/IPO settings. Shared control blocks and MSVC map sentinel nodes
are included in state requests:

| Accessibility state | `sizeof` | Allocations | Requested bytes | Individual requests |
| --- | ---: | ---: | ---: | --- |
| Baseline universal ControlAccessibility | 984 | 3 | 1,216 | 1,000 + 128 + 88 |
| Candidate reduced ControlAccessibility | 536 | 2 | 640 | 552 + 88 |
| Candidate CollectionAccessibility | 432 | 2 | 576 | 448 + 128 |
| Candidate GridAccessibility | 560 | 2 | 704 | 576 + 128 |
| Legacy AccessibilityState, both | 240 | 1 | 256 | 256 |

The baseline universal Peer is 728 bytes. HostPeer is 144; concrete collection/grid/
file-list peers are 368/312/744; the pending-family UnextractedPeer is 488.
The 744-byte file-list owner **includes** the existing 600-byte ListPeer.
ControlSnapshot shrinks from 864 to 432. RowImages remains the existing lazy 96-byte
indirect allocation, not an additional per-row or per-leaf object.

| Peer plus initial accessibility state | Baseline requested bytes / allocations | Candidate requested bytes / allocations |
| --- | ---: | ---: |
| Collection | 1,944 / 4 | 944 / 3 |
| Grid | 1,944 / 4 | 1,016 / 3 |
| Pending families | 1,944 / 4 | 1,128 / 3 |
| Legacy native list | 2,800 / 6 | 1,000 / 2 |

These sums include the concrete peer allocation and the state allocations above, but
exclude unchanged provider/model/lazy-image allocations and allocator bucket rounding.
They are requested storage, **not** measured heap bucket sizes or process-private memory.
The embedded legacy adapter removes its old independent allocation and redundant generic
accessibility state; other concrete peers retain one materialization allocation.

### Matched real-host work

The same workload is compiled against frozen baseline, pre-correction final2,
image-corrected final3 and enabled-query-corrected final4 libraries,
with MSVC 19.44.35217 / SDK 10.0.26100, x64 Release `/O2 /Ob2 /MT /GL` and full `/LTCG`.
The baseline source archive SHA256 is
`423b5a6f5218e6207eae239e579f9e731456c4123bdafd5db25347dd4997d80c`.
Response files, compiler version output, source/library/executable hashes and all raw
samples are retained separately from both builds. Smoke processes are correctness
checks, not reserved performance samples.

Each process synchronizes a real Window with 1,000,000 collection rows, 100,000 grid rows
and 10,000 legacy rows. It validates 32,768 scroll updates, 8,192 selection updates,
8,192 source updates (16,384 actual identity transitions) and 256 actual painted frames.
It reports wall time, raw kernel/user owning-thread CPU stamps, allocations, source
reads/keys/lookups, callbacks, updates and deterministic effect checksums. Selection
assertions use real nonempty selections, not empty-selection stand-ins.

All phases preserve four peers, five child HWNDs, one shared target and stable native
identities. Explicit scroll creates no animation timer, no editor text/font/selection
writes and no additional root layout; two selection terms remain bounded. Teardown
requires zero live Drawing targets. The controlled partial-attach failure requires the
actual throwing grid source after an earlier collection attachment, explicit error/
rejection, closed native owner and zero targets.

The [raw evidence companion](stage3a-collection-host-evidence.json) preserves input and
binary hashes, actual commands, all warmups/recorded samples and diagnostic failures.
The following **initial pre-correction** block used a parent-reserved quiet period,
two warmups and seven recorded processes per revision alternating AB/BA. No samples
were removed.

| Phase | Wall median baseline -> candidate (ms) | Wall delta | Owning-thread CPU median baseline -> candidate (ms) | CPU delta |
| --- | ---: | ---: | ---: | ---: |
| 32,768 scroll synchronizations | 2,596.6845 -> 2,609.5437 | +0.50% | 2,421.875 -> 2,468.75 | +1.94% |
| 8,192 selection publications | 1,009.3837 -> 1,096.9455 | +8.67% | 1,000 -> 1,093.75 | +9.375% |
| 8,192 source publications | 846.7155 -> 925.7636 | +9.34% | 750 -> 812.5 | +8.33% |
| 256 painted frames | 4,246.3296 -> 4,253.9238 | +0.18% | 1,281.25 -> 1,296.875 | +1.22% |

Initial-block measured source-work counts, callbacks, updates and effect checksums
match in every process. Phase allocation counts also match exactly: 2,429,509 / 1,605,631 / 802,816 /
49,917. Retained GDI/user objects are 17/12 for both; handles range 1,064-1,066 baseline
and 1,066 candidate. Private bytes overlap (41,164,800-43,352,064 versus
41,648,128-43,302,912), not a validated application memory improvement.

The selection/source publication signals exceed the five-percent investigation
threshold; source is slower in all seven recorded pairs. **Performance acceptance was
blocked pending bounded investigation**, not averaged away with the other phases.
Raw distributions, tails, paired deltas and absolute medians remain in the companion.
Paint wall duration includes presentation wait and is not application input latency.

A second reserved block used an identical diagnostic probe with
`QueryThreadCycleTime` brackets around model mutation and native flush/paint, still
against the unchanged frozen libraries. All work assertions passed in two warmups
and seven recorded processes per revision. Source model cycle medians were
132,692,837 -> 135,244,066; native-flush cycles 2,447,264,371 -> 2,445,016,275.
Diagnostic selection model/native-flush cycle medians were 166,672,425 ->
160,679,661 and 3,309,499,866 -> 3,163,612,926. These diagnostic executables did
not reproduce the original selection/source signal.

This is attribution evidence only. Microsoft explicitly cautions that
[`QueryThreadCycleTime`](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime)
frequency behavior depends on the CPU and cycles must not be converted to elapsed
time. It does not prove frequency variation. Relinking the instrumented whole-LTCG
executables can also change final optimization/layout despite unchanged libraries.
The diagnostic block does **not** replace or clear the original uninstrumented
regressions.

### Extended confirmation and discontinuity

A second uninstrumented confirmation used the exact original executables, opposite
starting order, two warmups and 31 recorded pairs. All 66 processes remain; there was
no rebuild, relink, sample deletion or restarted run. This was **not an uninterrupted
acceptance block**. Candidate recorded round 5 spans stdout creation at
06:48:34.5235Z to completion at 09:55:53.7153Z. Its scroll wall duration is
11,241,246.8501 ms versus 6,703.125 ms owning-thread CPU; scroll allocations increase
from 2,429,509 to 2,429,513, with retained handles/GDI/USER 1,092/23/21 instead of the
normal 1,064-1,066/17/12.

Resources already transition before that interruption: round 4 and baseline round 5
show approximately 806-808 handles / 11 USER objects and unusually fast paint.
The raw companion labels pre-normal, pre-transition, interrupted and post regimes;
it does not combine every pre-gap record into a supposedly homogeneous group.
The all-31 source wall/CPU median deltas are +5.71%/+7.69%; the 27 complete post-gap
pairs are separately descriptive +5.71%/+5.77%, with candidate slower in 21/27 wall
and 19/27 CPU pairs. Neither the mixed-regime aggregate nor automatic promotion of
the post-gap subset clears acceptance. The source signal still required a concrete
investigation. The parent independently audited all 84 initial/extended records,
raw CPU calculations, exact measured work, revision order and original binary hashes.

Only measured-phase work is exact. Untimed retained-phase `item_reads` already varies
642,871 / 642,883 / 642,907 in the original block; no identical whole-lifetime work
claim is made.

### Coupled image-eligibility correction

Source inspection found a real boundary discrepancy: the baseline rejects a FileList
with thumbnails disabled and no retained slots **before** clipping,
`IsWindowVisible` and `IsIconic`. The initial extraction rejected it only after that
shared work. It also eagerly evaluated input-enabled state for all participants;
the baseline needs that query only when resolving gallery/tree row styles.

The final3 correction gives concrete families an `image_sync_required` predicate
before shared geometry/visibility work, and evaluates enabled state only in those
collection row-style paths. FileList remains eligible while retained slots exist,
even after disabling thumbnails; empty collection/grid owners still participate to
cancel old work. The shared hidden-before-visible loop and 48-slot legacy allowance
are unchanged. A fail-fast detached-services test covers disabled/empty rejection,
enabled eligibility and empty collection/grid cancellation participation. Snapshot
transfer helpers were deliberately not speculatively optimized.

The reconstructible pre-correction implementation is commit
`1b469365467918e66da0a0ac8a38d467f8a491c5`; corrected production/gates/runner are
`deb5559a3e0899efd377c34a4559efd93230c1f3`. Final3 libraries and the identical
uninstrumented probe are frozen separately. The rebuilt baseline probe is preserved,
but the final comparison reuses the **exact original baseline executable**, not that
relink. PE `.text`, `.data`, `.pdata`, `.fptable`, `.rsrc` and `.reloc` match the
relink; `.rdata` and PE metadata differ, without inferring a precise cause. Both
identities, response files, section hashes and copy provenance are retained.
That exact-original-baseline comparison is the stopped warmup block below, not the
later symmetric hover-controlled probe.

The final runner adds UTC endpoints and preserves parsed raw records before validation,
rejecting measured-work differences immediately rather than only after a whole block.
Synthetic controls exercise all 18 valid process records and immediate rejection at
the fourth mismatched process, plus rejection of a successfully exited process whose
elapsed wall time exceeds 90 seconds; these are protocol checks, **not** native samples.

### Stopped corrected warmup block

The first corrected block was explicitly reserved candidate-first to oppose the
original seven-pair initial order. It stopped after two warmup processes, with **zero
recorded samples**. Only paint source-key work differs: candidate 19,678 versus
baseline 19,422, exactly one extra key per each of the 256 painted frames. Every
other measured work counter, effect checksum, allocation count and resource bound
matches; both processes finish in under 12 seconds. All UTC/raw/parsed records and
the failure remain in `host-comparison-fixed`; nothing was excluded or restarted.

Both baseline and extracted paint paths perform an extra source-key lookup for a
hovered collection row. The probe initially left pointer entry uncontrolled; final3's
earlier smoke and all 18 original samples have 19,422 paint keys. This is evidence
consistent with a hover-state confound, not proof of its cause or performance
acceptance. The quiet reservation was explicitly released after the fail-fast stop.

The coordinator approved a bounded **symmetric fixture correction**, not a production
change: send `WM_MOUSELEAVE` only to the probe-owned collection HWND, flush setup
outside measured counters/timers, require nonhover state before every phase and at
paint completion, and require the canonical 19,422 paint source keys. No cursor,
focus, foreign window, work count or existing assertion is manipulated or relaxed.
The identical revised source passes serial correctness smokes against both immutable
libraries. Probe/runner commit `0d42d1bc371211cce4ccde4cfca806ce00672329` has source
SHA256 `a74f229ba8d7962217dff7933fbdaba0d259a7b828e637029181e196025830c6`;
its new baseline/candidate executables are
`c6355dd88587d8c8d3e848c5d5f9501b82b61157e0f5a29b4202256e4ed1ca89` /
`080a6671d270b7d04cfb3f9afb5132205f65191aae091cb39246f312743955e9`.
Full-LTCG code generation changes on **both** revisions. This is a separately
attributed controlled probe, not pooled with old binaries or called only a
statistical rerun. Original probes, two failed warmups and every prior block remain.

### Pre-final4 seven-pair symmetric controlled block

After reviewing the fixture correction and controls, the coordinator reserved a new
candidate-first block with no competing parent jobs. All 18 processes passed:
two warmups and seven recorded pairs, with unchanged exact measured work, canonical
19,422 paint keys, allocations, raw CPU arithmetic and retained bounds. UTC endpoints
span 10:45:30.4361-10:48:20.8925Z; process durations are 8.89-13.19 seconds, with no
90-second-budget failure or observed clock interruption. No records are removed.

| Phase | Wall median baseline -> candidate (ms) | Wall delta | Owning-thread CPU median baseline -> candidate (ms) | CPU delta |
| --- | ---: | ---: | ---: | ---: |
| 32,768 scroll synchronizations | 2,532.8166 -> 2,503.9966 | -1.14% | 2,359.375 -> 2,437.5 | +3.31% |
| 8,192 selection publications | 1,018.8259 -> 997.5992 | -2.08% | 1,000 -> 984.375 | -1.56% |
| 8,192 source publications | 864.1458 -> 897.99 | +3.92% | 781.25 -> 796.875 | +2.00% |
| 256 painted frames | 4,250.4786 -> 4,252.2358 | +0.041% | 1,390.625 -> 1,390.625 | 0% |

The source result is **not a blanket performance pass**: its paired wall median is
**+6.52%, candidate slower in six of seven pairs**, versus paired owning-thread CPU
median -1.92%, slower in three of seven. Pair wall deltas are -1,172.6703, +22.3323,
+54.9529, +132.2270, +79.2834, +352.3361 and +11.6601 ms. Baseline recorded round 2
also has much larger scroll/selection/source CPU and wall durations; it remains in
every distribution, as does candidate recorded round 7's larger source duration;
neither is deleted or relabeled. Arithmetic wall-minus-thread-CPU
differences vary substantially and do **not** prove a scheduler, frequency or wait
root cause. CPU accounting is quantized; those differences are not exact wait time.
The residual paired-wall signal remains an explicit acceptance limitation for parent
review, not averaged away with other phases or hidden by unpaired medians below 5%.

Phase allocations remain exactly 2,429,509 / 1,605,631 / 802,816 / 49,917. Both revisions
retain 1,064-1,066 handles, 17 GDI and 12 USER objects; private-byte ranges
40,972,288-42,823,680 and 41,238,528-42,483,712 overlap and are not a validated application
memory improvement. Untimed retained item-read counts vary on both revisions.
Resource/queue/identity/editor preservation and zero-target teardown assertions pass.
All raw samples, absolute/relative distributions, paired deltas, source/binary hashes
and response files are in the companion. These host-operation/paint measurements
still do not establish physical input latency, IME/Narrator behavior or a green
complete desktop matrix.

### Pre-final4 exact-binary opposite-order confirmation

The coordinator requested one bounded persistence check: baseline-first, two warmups
and 31 recorded pairs, using byte-identical copies of the hover-controlled executables.
There was **no rebuild, relink, production change, fixture change, exclusion or restart**.
The explicitly reserved quiet block completed all 66 records from
10:52:04.5965-11:02:20.0731Z, with every canonical work/allocation/raw-CPU/native-resource
guard passing and process durations 8.84-12.81 seconds. The original 18 controlled
records stay separate; results below are the 31-pair confirmation alone, not pooled.

| Phase | Wall median baseline -> candidate (ms) | Wall absolute / relative delta | UI-thread CPU median baseline -> candidate (ms) | CPU absolute / relative delta |
| --- | ---: | ---: | ---: | ---: |
| Scroll synchronization | 2,359.085 -> 2,369.5765 | +10.4915 / +0.445% | 2,296.875 -> 2,281.25 | -15.625 / -0.680% |
| Selection publication | 1,017.9507 -> 1,037.0839 | +19.1332 / +1.880% | 1,000 -> 1,015.625 | +15.625 / +1.563% |
| Source publication | 835.1203 -> 868.257 | +33.1367 / +3.968% | 750 -> 765.625 | +15.625 / +2.083% |
| Visible-row paint | 4,251.7165 -> 4,250.4572 | -1.2593 / -0.030% | 1,343.75 -> 1,343.75 | 0 / 0% |

Source paired wall median is +2.670%, candidate slower in 18/31 pairs; paired CPU
median is -2.041%, slower in 14/31. The first seven-pair source-wall signal above 5%
**does not persist** in this exact-binary opposite-order confirmation. Its original
+6.518%/six-of-seven result, all outliers and all earlier blocks remain reported,
not averaged away with unrelated phases. The parent independently audited every
extended record, original/copy/source/library/input hashes, canonical work/allocations,
CPU arithmetic, UTC/budget/order and resources, and said performance no longer blocks
PR finalization; final architecture acceptance and merge remain parent-owned.

Kernel and user components are not hidden behind total CPU:

| Phase | Kernel CPU median baseline -> candidate (ms) | Kernel delta | User CPU median baseline -> candidate (ms) | User delta |
| --- | ---: | ---: | ---: | ---: |
| Scroll | 796.875 -> 765.625 | -3.92% | 1,546.875 -> 1,484.375 | -4.04% |
| Selection | 281.25 -> 312.5 | +11.11% | 734.375 -> 703.125 | -4.26% |
| Source | 265.625 -> 281.25 | +5.88% | 515.625 -> 500 | -3.03% |
| Paint | 218.75 -> 250 | +14.29% | 1,125 -> 1,093.75 | -2.78% |

Component medians can exceed 5% even when phase total CPU does not; independent
medians are not additive. Raw per-record stamps and separate component distributions
are retained for every measured block, not treated as proof of a frequency or kernel
root cause. Native bounds and phase allocations remain unchanged. Private-byte ranges
40,853,504-43,294,720 baseline / 40,759,296-43,126,784 candidate overlap; untimed retained
item reads still vary. No claim of globally green desktop behavior, application input
latency, private-memory improvement or resolution of earlier optimizer/IME/Narrator/
inspection/highlight limitations follows from this confirmation.

### Final4 captured-enabled correction and identity

Exact-source review found one more extraction-introduced duplicate:
`publish_control` queried `control.enabled() && IsWindowEnabled(window)` and then
`accessibility_identity` immediately queried the same state again for every
remaining-family peer. The local value is needed only for later UIA events. Commit
`08f87ec411681178dab9b5ad1e50d22b1a5db8b1` instead builds identity first and captures
`const bool enabled = next.enabled`; event logic and all other semantics are unchanged.
This removes a concrete per-update OS query, not a speculative layout optimization.

The independent fresh final4 complete All passes all 49 native tests and generated/
managed/Rust checks; all nine focused cases pass again, including native/isolated UIA
collection, grid, both style fixtures, file transfer, precision scroll, owned policy,
full ContentHost and the rollback/identity/actual-work probe. Identical matched
external-probe correctness smokes also pass. These results are separate from final3;
no earlier timing acceptance is promoted automatically to the revised code.

Final4 `xui_windows.lib` / `xui_core.lib` hashes are
`a64b79bff82251ae4692ad36ac8e565fd2915746086ce06d610a00cc2b1d662e` /
`edd1041aa88d525933687dc8dc0b438f6c9950d3b5ee1e2b641868683c13088d`.
Probe source remains `a74f229b...`; the exact controlled baseline executable
`c6355dd8...` is reused, while the new final4 candidate is
`867bfff65946550cd38563dc2c64905f27fbfb6f8fc11c7d853050afb2adb1d7`.
The newly relinked baseline `abd792df...` is preserved but not measured.
Full hashes, source archive/manifest, actual compiler/link responses and smoke logs
are retained. All final3 seven/31-pair blocks remain explicitly pre-this-fix evidence.

The coordinator independently passed final4 complete All and full ContentHost against
its own fresh build, including actual `/GL`/full `/LTCG`, 200 replacements, retained
UIA/fatal failure, 32 active / zero inactive typography at 128 versus 2,048 inactive
items, one editor HWND and zero activation/focus/foreground/dropped changes. Its source
was stable throughout 11:24:10-11:25:27Z except two documentation files.

### Final4 seven-pair block and exact-binary confirmation

The candidate-first final4 block passed all 18 process guards but **blocked performance**
again: source wall median 841.1039 -> 928.456 ms (+10.385%), owning-thread CPU
781.25 -> 828.125 ms (+6%). Paired medians are +10.035% wall / +10.417% CPU, with
candidate slower in 7/7 wall and 4/7 CPU pairs. Candidate rounds 4 and 6 include
source wall/CPU 3,197.3861/2,421.875 and 2,472.3228/2,031.25 ms; all remain unchanged.
Earlier final3 acceptance is not substituted for these final4 results.

The parent then reserved one baseline-first confirmation: two warmups plus 31 pairs
with byte-identical copies of the **same final4** executables. No rebuild, relink,
code change, pooling, restart or exclusion occurred. All 66 processes pass exact
canonical work/allocations, raw CPU arithmetic, resource/identity/editor/queue bounds
and elapsed-budget checks. UTC endpoints span 11:35:11.6138-11:45:31.6171Z; process
elapsed times are 8.84-14.58 seconds, with no observed interruption.

| Phase | Wall median baseline -> candidate (ms) | Wall absolute / relative delta | UI-thread CPU median baseline -> candidate (ms) | CPU absolute / relative delta |
| --- | ---: | ---: | ---: | ---: |
| Scroll synchronization | 2,423.9394 -> 2,418.1336 | -5.8058 / -0.240% | 2,343.75 -> 2,328.125 | -15.625 / -0.667% |
| Selection publication | 1,026.3603 -> 1,026.8601 | +0.4998 / +0.049% | 1,015.625 -> 1,015.625 | 0 / 0% |
| Source publication | 865.4411 -> 889.9188 | +24.4777 / +2.828% | 781.25 -> 796.875 | +15.625 / +2.000% |
| Visible-row paint | 4,252.7742 -> 4,253.1291 | +0.3549 / +0.008% | 1,375 -> 1,390.625 | +15.625 / +1.136% |

Source paired wall median is +3.291% (23/31 slower); paired total CPU is +4.348%
(20/31 slower). This is a **small residual source cost within the five-percent
guardrail**, not a speedup or "all noise." The greater-than-five-percent phase
wall/total-CPU signal did not persist in this unchanged-binary confirmation.
The parent independently audited all 66 records and exact source/library/input/binary
hashes, work, allocations, CPU, order, UTC and bounds, and requested no further timing.
Every earlier block, source tail and failed warmup remains separate.

| Phase | Kernel CPU median baseline -> candidate (ms) | Kernel delta | User CPU median baseline -> candidate (ms) | User delta |
| --- | ---: | ---: | ---: | ---: |
| Scroll | 781.25 -> 812.5 | +4.00% | 1,578.125 -> 1,546.875 | -1.98% |
| Selection | 312.5 -> 296.875 | -5.00% | 703.125 -> 734.375 | +4.44% |
| Source | 281.25 -> 265.625 | -5.56% | 500 -> 546.875 | **+9.375%** |
| Paint | 203.125 -> 234.375 | **+15.38%** | 1,156.25 -> 1,187.5 | +2.70% |

Source user-CPU paired median is +10%; paint kernel paired median is +14.29%.
These component costs and all tails are preserved, not hidden by total CPU or called
globally green. Independent component medians are not additive. Handles remain
1,064-1,066 / GDI 17 / USER 12 on both revisions; phase allocations remain exactly
2,429,509 / 1,605,631 / 802,816 / 49,917. Private-byte ranges overlap at
41,046,016-42,975,232 baseline / 40,841,216-43,122,688 candidate; untimed item reads
vary and no application private-memory improvement is claimed.

No acceptance or merge is claimed.
