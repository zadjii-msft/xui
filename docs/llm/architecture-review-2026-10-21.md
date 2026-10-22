# Architecture review and sequential refactoring roadmap

Date: October 21, 2026.
Original review baseline and source inspected for this document:
`99016573d633636c54da71af5db247e5c249051e`.

This is a maintainer investigation and implementation plan, not a new public contract.
It preserves the seven structural findings from the architecture review and puts all of their refactors in an acceptance-driven rollout.
The findings identify responsibility, ownership, and change-amplification risks; they are not measured regressions.
This documentation session's inspection confirms source structure only; it ran no runtime suites.
The coordinator separately reported the pristine correctness run recorded below.
No paired performance acceptance or desktop measurements were collected for this documentation stage.
Historical line numbers below locate the reviewed baseline, not future revisions.

Read the [maintainer source map](architecture.md), [test context](testing.md), and
[public references](../specs/README.md) before implementation.
Build and test commands belong in [CONTRIBUTING](../../CONTRIBUTING.md).
Current source takes precedence over older handoffs.

## Program rules and preserved foundations

The user requested one refactor at a time: file, verify, and **merge each PR before starting the next**.
Each PR gets one session based on the newly merged `main`; do not stack or implement later stages ahead.
Every intermediate merge must leave a working, bisectable main branch.
The documentation PR is stage 0; it authorizes no runtime or build changes by itself.
The parent coordinator verified and merged it before stage 1 began.

There are no external consumers constraining this cleanup.
Backward compatibility is not a reason to retain redundant abstractions, obsolete entry points, or compatibility shims.
That freedom does not waive behavioral correctness, performance acceptance, documentation, or coordinated repository updates.
Any intentional contract change must be explicit in its implementation PR and relevant public documentation.
This roadmap does not prescribe speculative public interfaces.

Keep the foundations that are already useful:

- Platform-independent control and layout models, explicit property updates, stable element identity, and two-pass layout.
- Immutable stable-key sources, indexed lookup, bounded collection selection, and generation-checked asynchronous delivery.
- Native editing bridges: real EDIT/RichEdit input, IME, caret, selection, clipboard, undo/redo, and accessibility.
- Explicit ownership, UI-thread mutation boundaries, cancellation, stale-result rejection, and surfaced native/foreign/managed errors.
- Optional sparse styling, shared drawing resources, retained native identity, and viewport-bounded collection work.

Do not introduce a continuous render loop, per-row visual objects, a general reactive runtime,
a heavy property dictionary, a public plugin engine, or a template-system rewrite as a prerequisite.
Do not replace functioning native input with custom text rendering.
Use the [application](../specs/application.md), [documents](../specs/documents.md),
[collections](../specs/collections.md), [bindings](../specs/bindings.md), and
[styling performance](../specs/styling-and-templates-design.md#performance-acceptance) contracts as behavioral constraints.

## Findings

### 1. Control-family behavior is concentrated in the window host

**Evidence.** The original review recorded a roughly 7,299-line `src/application.cpp`.
Extracting `.inc` fragments alone does not address this finding.
At the inspected baseline, `Window::Impl::Peer` starts near line 88, `collect` near 1045,
`update` near 2024, `paint_control` near 3609, and `control_message` near 4971.
The translation unit already includes `.inc` fragments; those inclusions are not independent ownership or dependency boundaries.
[`Peer`](../../src/application.cpp) stores editor, document, runtime, swap-chain, list, image,
row-image, grid-drag, collection-selection, tab, choice, and accessibility state together.
Family cleanup is spread across its destructor, `detach_peer` near 473, retirement in `prune_popups`,
and `WM_NCDESTROY` near 6006.
[`ControlSnapshot`](../../src/control_accessibility.hpp) similarly aggregates tabs, split geometry,
grid columns and selection, choices, range values, collections, and other family fields.
These are current source observations at the stated baseline, not size or speed comparisons.

**Why change.** Adding one family requires changes to unrelated host paths and shared state.
Attachment, property synchronization, input, paint, UIA publication, cancellation, and destruction
can drift apart when their ownership is distributed through family checks.
Moving methods verbatim into `.inc` files does not remove this coupling.

**Approach.** Extract meaningful control-family peer implementations that own their attachment,
synchronization, input, painting, detachment, and family payloads.
Separate common UIA identity/state from family-specific snapshots and publication work.
Keep a narrow window-wide coordinator for focus, modal/popups, frame scheduling, placement ordering,
window lifecycle, and shared drawing resources.
Define only the internal host services a family actually needs; do not grant arbitrary `Window::Impl` access.
Use [`ListPeer`](../../src/list_peer.hpp), [`NativeDocumentBridge`](../../src/native_document.hpp),
and [`NativeSwapChainHost`](../../src/native_swap_chain_host.hpp) as existing prior art, not a requirement to copy one interface everywhere.
The native document bridge already owns editor-specific synchronization; the swap-chain host already exposes synchronization and owner cancellation.

**Constraints and non-goals.** Preserve peer/HWND/provider identity, native subclass notification ordering,
parents remaining alive until their children are destroyed, lookup removal before synchronous destruction notifications,
shared render resources, and popup/native occlusion behavior.
No extra HWNDs or render targets, per-row allocations, public extension engine, or speculative inheritance hierarchy.
Measure any added dispatch, peer storage, or family-payload allocation rather than assuming extraction is free.
A pilot family does not complete this finding; remaining families must remain visible in the ledger.

### 2. Invalidation loses the origin and broadens synchronization work

**Evidence.** [`Element::InvalidationState`](../../src/core.cpp), near line 144, propagates
`Invalidation` categories through parent states without the originating element identity.
In [`Window::Impl::invalidate`](../../src/application.cpp), near 584, any non-scroll invalidation,
including paint, sets `state_pending`.
`update`, near 2024, distinguishes a scroll-only path, but otherwise prunes and recollects the root and popups.
[`publish_control`](../../src/control_accessibility.cpp), near 540-675, constructs a complete next snapshot
before testing equality, including grid columns/filter/selection and collection selection data.
This establishes the work path, not its measured cost in any workload.

**Why change.** A local property edit need not imply topology reconciliation, unrelated native synchronization,
or reconstruction of unrelated accessibility payloads.
Category-only propagation cannot reliably identify the narrowest safe work.

**Approach.** Separate topology reconciliation from property synchronization first.
Then carry a lifetime-safe originating identity/subtree with explicit topology, layout, placement,
native-property, accessibility, and paint dirty work.
Coalesce work without losing category escalation or dependency ordering.
Use family knowledge to publish only relevant native/UIA state; keep conservative whole-window invalidation
for theme, modal, DPI, or other genuinely global changes.
Do not optimize away observable accessibility property/structure/focus events.

**Constraints and non-goals.** Preserve invalidations raised during measurement, arrangement, synchronization,
callbacks, and destruction; consume a captured work set without clearing newer work.
Removed/replaced elements cannot leave dangling work records.
Test nested synchronization, popup replacement, callback-driven close, and new dirtiness during a pass.
Retain scroll-only placement/editor-identity behavior and active-only animation scheduling.
No timer-based suppression, dropped updates, perpetual frame loop, or claimed speedup without repeated old/new measurements.

### 3. Child ownership and traversal policy are conflated

**Evidence.** [`Stack::child_at`](../../include/xui/core.hpp) and
[`Control::retained_children`](../../include/xui/controls.hpp) provide different child access paths.
`ScrollView`, `ContentView`, and `SplitView` use separately handled content slots.
Host-created clear buttons are adornments outside the ordinary model child list.
`PageView` collects selected content, and [`Reveal`](../../include/xui/reveal.hpp) retains content
whose native realization can be deferred.
The host repeats child-family traversal in `claim_deferred_tree` near 1017, `collect` near 1045,
`visit_visual_tree` near 1394, `content_ids` near 1764, and `paint_content_surface` near 2853.
`defer_reveal_children` explicitly retains ownership and typography without allocating unopened HWNDs.

**Why change.** Repeated type checks make new composite children easy to omit from ownership validation,
retirement, inspection, or rendering.
But replacing those checks with one identical traversal would erase intentional differences.

**Approach.** Define allocation-free owned-child enumeration for model ownership.
Keep distinct policies for presentation, selected/lazy realization, clipping/coordinate spaces,
inspection/hit testing, accessibility, and host adornments.
Hidden pages, closed reveals, and other lazy children remain owned even when they have no native peer.
Represent the host's adornment lifetime explicitly without pretending it is an application-authored model child.
Migrate affected consumers against a documented policy matrix, including ABI content-scope ownership and retirement.

**Constraints and non-goals.** Enumeration must not allocate a temporary child vector or create native peers.
Preserve adoption/cycle/duplicate-owner validation, child order, deferred realization, popup ownership,
replacement-host exceptions, scroll clipping, and peer identity.
Test hidden/lazy descendants and retained child handles, not only visible traversal.
Do not impose identical logical, visual, accessibility, and realized trees or rewrite all controls/templates.

### 4. Composition behavior shares replaceable observer slots and manual lifetime lists

**Evidence.** [`NumericInput`](../../src/foundation.cpp), near 588-599, installs editor-change
and step-button behavior through public `on_change`/`on_click` slots and clears them in its destructor.
Editable `ComboBox`, near 485, uses its editor's replaceable change callback similarly.
[`c_api.cpp::wire`](../../src/c_api.cpp), near 288-355, captures/chains prior callbacks;
[`abi_callbacks.hpp`](../../src/abi_callbacks.hpp) stores callback families.
[`c_api_features.inc`](../../src/c_api_features.inc) adds more chaining and special cases for
navigation-owned buttons and Miller child ownership.
[`Window.Dispose`](../../bindings/dotnet/Xui/Window.cs), near 171, and
[`RetireContent`](../../bindings/dotnet/Xui/ContentHost.cs), near 293, manually enumerate
general, menu, file, and Miller subscriptions and delegate roots.
These patterns establish coupling and coverage risk, not proof that every current subscription loses behavior.

**Why change.** Application observation must not replace the behavior that makes a composite control function.
Capturing and chaining an old slot requires each adapter to know whether it is preserving composition,
another observer, or a special ownership relationship.
Adding a callback family also expands multiple cleanup lists.

**Approach.** Separate control-composition behavior from public application/binding observation.
Specify invocation order, replacement/unsubscription semantics, reentrant mutation, and detach behavior explicitly.
Preserve model updates before observer delivery where required by the existing semantic action contract;
do not invent one global ordering rule that ignores native notification phases.
Use scope-owned registration retirement across native/ABI/.NET lifetimes so new registrations participate
without adding family-specific cleanup loops.
Keep registration storage and ownership explicit; generation/cancellation and content-scope boundaries remain enforceable.

**Constraints and non-goals.** Native HWND destruction, foreign-context release, and managed delegate/root release
have different thread, phase, and in-flight callback constraints.
Logical cancellation must precede unsafe delivery, while physical release waits until the relevant callback/native phase is safe.
Handle failed registration, candidate-content rollback, callback-driven close, repeated replacement, and disposal without double release.
Keep explicit callback failure reporting and application-post execute/cancel ownership semantics.
No global event bus, unbounded observer history, per-keystroke subscription allocation, or silent exception swallowing.

### 5. ABI, wrapper, and compiler contracts overlap without one authoritative description

**Evidence.** C headers and native feature switches describe IDs and validation;
[`features.json`](../../bindings/features.json) supplies numeric control/member/action metadata.
[`generate_features.py`](../../bindings/generate_features.py) combines those inputs with generator-specific shape/category rules.
C# and Rust contain both generated and handwritten adapters.
[`Parser.ParseNode`](../../bindings/dotnet/Xui.Generator/Parser.cs), near 221,
separately lists supported controls, arguments, and container rules.
[`XuiGenerator.Collect`](../../bindings/dotnet/Xui.Generator/XuiGenerator.cs), near 145,
maps mutable updates, while `Shape`, near 317, independently describes structural/hot-reload identity.
The existing [`generate_control_styles.py`](../../bindings/generate_control_styles.py) provides
native-exported schema comparison and `--check` generation parity against the checked-in catalog.
`CMakeLists.txt` registers that native catalog comparison when Python is available.

**Why change.** A feature can be accepted by a wrapper but rejected by native validation,
advertised by tooling but not constructible, or treated differently by parser and hot reload.
Generated code alone is not authoritative if its source metadata omits these distinctions.

**Approach.** Extend the existing native schema/export and generation/parity pattern into an authoritative
description of feature IDs, value/record shapes, applicable controls and events, child cardinality,
construction-only versus mutable properties, and language support.
Establish one owner per fact and explicit adapters where native and language representations differ.
Generate repetitive ABI/wrapper adapters and compiler metadata/validation tables from those facts.
Derive structural identity/rebuild rules and mutable update eligibility from the same description.
Keep handwritten complex behavior, ownership, secret access, cancellation, and diagnostics where generation would obscure them.

**Constraints and non-goals.** Check duplicate/unknown IDs, unsupported combinations, argument/value limits,
marshalling shapes, event targets, and generated-file drift.
Unsupported coverage must be explicit; full C++/C/C#/Rust/markup parity is not required.
A style target enum member is not proof of a markup constructor.
Preserve diagnostic source locations and native/thread/error boundaries.
No runtime reflection, runtime property dictionary, or broad generic dispatch in hot paths.
Compatibility is optional; simultaneous repository consumers must still build and agree.

### 6. Legacy file-list infrastructure leaks into the foundation

**Evidence.** [`core.hpp`](../../include/xui/core.hpp), near 167 onward, declares `FileItem`,
`FileSnapshot`, `FilteredView`, `ViewWorker`, and `FileListModel` beside geometry and layout.
New collections instead use [`CollectionIndex`, `ItemsSource`, and `CollectionSelection`](../../include/xui/collections.hpp),
with [`RowImages`](../../src/images.hpp) for shared row-image work.
Legacy `FileList` retains separate [`ListPeer`](../../src/list_peer.hpp),
[`accessibility.cpp`](../../src/accessibility.cpp), and thumbnail admission/reconciliation paths.
Its worker comments distinguish source cancellation from filter cancellation and background snapshot retirement.
Current collection bounds include 4,096 selection terms and row-image limits of 512 rows and 48 queued requests;
record and preserve applicable bounds when sharing these algorithms.

**Why change.** General model users inherit file-specific concepts and dependencies.
Separate viewport/image/identity algorithms can accumulate fixes inconsistently.
Those observations do not mean file-list, general collection, and grid semantics are identical.

**Approach.** First move file-specific declarations and implementation dependencies out of the foundation,
updating repository consumers directly rather than adding unnecessary compatibility shims.
Then identify genuinely shared viewport, stable-identity, and image-admission/reconciliation helpers,
and consolidate them behind adapters that preserve each consumer's semantics.
Document any algorithms deliberately left separate and why.

**Constraints and non-goals.** Preserve stable selection and item-focus identity, filtered hidden selections,
query-versus-refresh cancellation, worker delivery and retirement, UIA identity, thumbnail error reporting,
and source/viewport replacement behavior.
No UI-thread filesystem work or joins, unbounded thumbnail retention, per-row visual allocation,
or forced `FileList`/`DataGrid` inheritance chain.
Measure CPU, allocations, queue/cache bounds, and retained resources for both old and new consumers.

### 7. Architectural boundaries need build enforcement and ordinary PR gates

**Evidence.** [`CMakeLists.txt`](../../CMakeLists.txt) has broad `xui_core`, `xui_windows`,
and C ABI targets; `.inc` files do not independently enforce dependencies.
The inspected workflow inventory contains [`docs.yml`](../../.github/workflows/docs.yml) on PRs,
[`templates.yml`](../../.github/workflows/templates.yml) with path-specific triggers, and
[`release.yml`](../../.github/workflows/release.yml) on `release/*` tags.
There is no general framework PR gate in that inventory.

The current test partition is not a reliable headless guarantee.
`XUI_DESKTOP_TESTS` is declared near line 670, after unconditional Windows test registrations:
swap-chain panel/overlay/input tests already carry `desktop` labels outside the option;
content-host, content-inspection, and hosts-window tests are registered outside it without that label.
Their fixtures use actual windows.
The early `xui_menu_tests` target also links `xui_windows` and uses MSVC options outside the later Windows-only block.
Thus neither `XUI_DESKTOP_TESTS=OFF` nor excluding the `desktop` label alone proves a headless selection,
and cross-platform configure/build viability has not been demonstrated by this source inspection.
Stage 1 must investigate and reconcile target creation, registration, labels, and documented commands.

**Why change.** Tests only exercised through releases or local procedures cannot protect every refactoring merge.
Informal boundaries regress unless dependency rules and representative tests fail when they are crossed.
Misclassified desktop tests make CI coverage and unattended results misleading.

**Approach.** Add ordinary PR headless core, binding/compiler, and generated-contract checks;
separate interactive desktop correctness from controlled performance measurement.
Enforce no Win32 dependencies in core, narrow host services for family peers,
and explicit runtime services at binding boundaries.
Use build target/include/dependency checks and focused regression tests rather than file-size thresholds.
Fail mandatory checks if required tools or native exports are missing; do not silently omit coverage.

**Constraints and non-goals.** Do not run focus/input/pixel tests on an unsuitable runner and call skips a pass.
Keep desktop runs serial and performance runs isolated from builds and other suites.
A source split or passing headless suite cannot substitute for native/accessibility/lifetime acceptance.
No blanket rewrite of the build or all test tooling before establishing the smallest trustworthy gates.

## Acceptance and evidence policy

Apply the existing [styling proposal's reproducible greater-than-5% regression guardrail](../specs/styling-and-templates-design.md#performance-acceptance)
to default-path latency, CPU work, and private memory.
Also report absolute deltas; a percentage alone is insufficient for small/noisy baselines.
Measure unstyled and styled paths separately where relevant.
Structural cleanup is not itself evidence of improved performance.

Stage 1 must make the following protocol reproducible in contributor procedures and identify the actual fixtures:

1. Record baseline/candidate commits, architecture, compiler/toolchain, build configuration, Windows/desktop/GPU state,
   workload sizes, warmup, sample count, measurement method, and interference.
   Use isolated baseline/candidate artifacts, never a shared mutable build output.
2. Compare repeated old/new runs under the same conditions, alternating order where applicable.
   Do not overlap measurements with builds, other desktop suites, or other performance work.
   Preserve raw samples, summaries, absolute deltas, and uncertainty.
3. Count heap allocations and retained storage, peer/control sizes, HWNDs, render targets, idle paints/wakeups,
   native/UIA synchronization work, and native identity changes as applicable.
   Verify viewport/cache/queue/selection bounds under large sources, not just small example screens.
4. Exercise repeated scrolling, theme/style changes, content replacement, lazy open/close, popup/modal cycles,
   cancellation, stale delivery, and closure; require bounded resource retention after settling.
   Existing virtual sources gain no per-item visuals, and unchanged controls gain no extra HWNDs, targets, or idle paints.
5. Record native focus, caret, IME composition, selection, undo/redo, clipboard, UIA patterns/events, and stable peer identity
   with real desktop evidence for changes touching those paths.
   Preserve explicit error and cancellation outcomes, including failed callbacks and registration rollback.

A missing interactive desktop, unavailable capture/provider, missing dependency, invalid baseline,
or noisy/inconclusive measurement is a **blocker or explicitly unresolved evidence**, not acceptance.
Document any approved scope reduction and remaining risk; do not mark the affected criterion passed.
Do not turn the 5% guardrail into permission for extra windows, per-row allocation, lost updates, broken editing,
unbounded caches, or other violations of hard invariants.
Measurements and dated results belong here or in related maintainer notes, never the root README.

### Reported pristine baseline correctness evidence

On October 21, 2026, the coordinating architecture-review session reported building pristine
`99016573d633636c54da71af5db247e5c249051e` as x64 Release with Visual Studio 2022 Preview,
IPO at its default ON, and `XUI_REQUIRE_LSH=ON`.
Its serial CTest selection was
`^xui_(core|control|layout_performance|performance|row_image_performance)_tests$` with `-j 1`;
the reported result was **5/5 passed**.
This is attributed coordinator evidence, not a suite rerun or independent artifact inspection by this documentation session.

The run establishes correctness/allocation checks only, not paired performance acceptance.
It supplies no candidate comparison, latency/memory deltas, desktop/input/UIA evidence, or binding/compiler coverage.
The coordinator retained pristine executables in its own worktree's `build\architecture-pristine\Release`
for later comparisons; that local artifact location is not a reproducible substitute for the commit/toolchain/protocol record.
Stage 1 must still establish the gates, full applicable baseline protocol, and remaining evidence.

## Ordered rollout ledger

Status vocabulary: **in review preparation** means documentation or implementation is awaiting its PR/merge;
**not started** means no implementation is authorized yet; **blocked** names unmet acceptance or environment requirements;
**merged** requires a PR link, exact accepted head/merge identity, and evidence.
Writing this plan does not complete any runtime finding.

| Stage | PR scope | Current status | Must merge first | Finding coverage |
| --- | --- | --- | --- | --- |
| 0 | `architecture-refactor-roadmap` | Merged: [#57](https://github.com/zadjii-msft/xui/pull/57); accepted `b00835a76d10d0188722f25551b6c375e66e4a30`, squash `3f86e085aad8ac36445a47061e1f7640da407fb6` | None | Records all seven findings and the complete rollout |
| 1 | `baseline-gates` | Verified merged: [#58](https://github.com/zadjii-msft/xui/pull/58); accepted `dce6ae447cc566816b32653fe3dd43de5fb9432a`, squash `8fdc2269a065837737b1dc5dcc0962a574ce10cd`; recorded desktop/timing limits remain unresolved | 0 | Establishes evidence and ordinary PR protection for every later stage |
| 2 | `child-contract` | In review: [#59](https://github.com/zadjii-msft/xui/pull/59), based on fetched `8fdc2269`; implementation and evidence below; not merged | 1 | Finding 3: owned children and separate traversal policies |
| 3 | `host-boundaries` | Not started | 2 | Finding 1: family peers and family-specific peer/UIA state |
| 4 | `dirty-work` | Not started | 3, including all approved subdivisions | Finding 2: originating identity and targeted ordered work |
| 5 | `callback-ownership` | Not started | 4 | Finding 4: composition/observation separation and scoped retirement |
| 6 | `contract-metadata` | Not started | 5 | Finding 5: authoritative contracts, adapters, compiler/reload rules |
| 7 | `legacy-file-list` | Not started | 6 | Finding 6: foundation isolation and shared algorithms |
| 8 | `boundary-enforcement` | Not started | 7 | Finding 7: dependency enforcement and final evidence/source maps |

### Stage 0: Documentation roadmap

**Scope:** this document and its maintainer-index entry only.
Preserve all seven findings, their evidence limits, existing foundations, non-goals, and every rollout stage.
Do not change runtime/build behavior, expand the root README, or publish a speculative API.

**Acceptance:** inspect the current source against the original review, distinguish structural evidence from measurements,
check document links using existing documentation checks, and create a non-draft PR targeting `main`.
Runtime tests are not required for this documentation-only change.
The parent verifies and merges the PR; only then may stage 1 begin.

**Merged evidence:** the parent verified documentation and merged
[#57](https://github.com/zadjii-msft/xui/pull/57) at accepted head
`b00835a76d10d0188722f25551b6c375e66e4a30`, squash merge
`3f86e085aad8ac36445a47061e1f7640da407fb6`.
Stage 1 fetched and started from that current `origin/main`.

### Stage 1: Baseline gates and measurement protocol

**Scope:** establish ordinary PR core, ABI/binding/compiler, and generated-contract checks using existing runners/tools.
Resolve the CMake target/registration/label inconsistencies before promising desktop separation or cross-platform core builds.
Inventory what truly requires native DLLs, windows, focus, capture, UIA, or controlled performance hardware.
Keep interactive desktop and performance execution separately addressable.

**Acceptance:** an ordinary non-template framework PR triggers the intended gates;
intentional failing core/compiler/contract fixtures fail those checks.
The headless selection is enumerated and demonstrated to require no interactive desktop.
Unsupported hosts are explicit rather than accidental configure failures.
Native-export schema checks cannot disappear merely because Python is absent.
Document exact contributor commands and baseline/candidate recording procedures, runner prerequisites,
hard allocation/HWND/target/idle/resource invariants, and unresolved evidence.
Collect a reproducible pre-refactor baseline for representative core/layout, local property edits,
large virtual collections, native text, content replacement, and lifetime workloads.
A correctness gate is not a timing benchmark; do not claim an unmeasured improvement.

**Implementation/evidence (October 21, 2026):** the unfiltered ordinary Framework PR/main
workflow runs a Windows x64 MSVC headless target plus generated-contract/compiler/managed/Rust
coverage using the existing CMake, Python, .NET and Cargo tools.
All 45 selected native tests and the binding/compiler sequence executed successfully on a
private non-visible window station, not an interactive desktop.
The headless ABI partition preserves full basic C/C++ and detached-document checks and
all feature ABI checks except the two realized desktop sections.
Managed `--headless` preserves model/ownership/style/callback coverage while excluding native runs;
all 39 Rust library tests, sys layouts and thread-safety doc tests run without name-filter skips.
Required Python/native exports/LSH cannot silently disappear.
Core assertion/compiler-error/generated-drift negative checks demonstrated nonzero failure.
Desktop OFF/ON inventories are reconciled (45 headless + 1 system; plus 129 serial desktop tests
when ON). Menu target placement and the collection fixture's outdated Drawing output type
were repaired without runtime architecture changes.
See the [exact selection and evidence](testing.md#ordinary-framework-pr-gate) and
[contributor comparison protocol](../../CONTRIBUTING.md#baselinecandidate-acceptance-protocol).

**Remaining limitations:** this stage claims no Linux validation, ARM64 ordinary CI matrix,
runtime speedup, or controlled timing acceptance. GitHub branch protection still needs the
workflow check required by a repository administrator.
The coordinator's pristine model/ABI/compiler/binding/resource baseline is recorded separately
from its unresolved multiwindow, window-resource, retained-scroll and native-input failures.
There is no valid paired local-edit/native-text/content-replacement/realized-lifetime timing
baseline or physical IME/Narrator acceptance yet. Those criteria remain explicitly unresolved;
the headless gate must not be substituted for them in later refactors.
The parent independently verified the exact accepted head
`dce6ae447cc566816b32653fe3dd43de5fb9432a` and hosted checks, accepted the recorded
stage-1 evidence limits, and merged [#58](https://github.com/zadjii-msft/xui/pull/58)
as `8fdc2269a065837737b1dc5dcc0962a574ce10cd`.
Stage 2 fetched and verified that merged main before implementation.
This does not resolve the remaining desktop/performance criteria or authorize stage 3.

### Stage 2: Owned-child contract and traversal policies

**Scope:** implement the allocation-free ownership enumeration and migrate the affected native/ABI consumers.
Record which policy each traversal applies, including selected pages, deferred reveals, scroll clipping,
split/content slots, popups, inspection, and clear-button adornments.

**Acceptance:** ownership visits all retained descendants exactly once in their defined order,
including hidden/lazy children, without forcing realization.
Cycle/duplicate-owner/foreign-window and candidate-content validation still reject invalid trees.
Separate presentation/realization tests prove unopened content gains no HWNDs;
replacement, retirement, inspection, clipping, focus, and accessibility retain their intended behavior.
No allocation is added to steady-state child enumeration.
Update the source map and public ownership contract only where its documented behavior actually changes.

**Implementation:** `Element::owned_children()` provides one borrowed direct-child range
for layouts and controls. Stack projects its existing Child records; contiguous composites
borrow their existing shared handles. No per-Element/control storage, per-walk vector,
ownership copy or native realization is added. SplitView's two slots use the same total
shared-handle storage. The obsolete retained-child API is removed throughout repository
consumers rather than kept as a compatibility shim.

All affected native ownership, collection, visual/paint, inspection and dialog consumers use
the contract. Ownership registration still includes every page and closed/lazy descendant;
active collection, visited native retention, replacement exceptions, scrolling/clipping and
host adornments remain separate. Ordinary PageView collection does not apply typography or
claims to unvisited pages. Dialog presentation now reaches nested ContentView/SplitView
slots; inspection uses actual axis-aware split-pane clips. These are explicit tightly
coupled coverage corrections, not a promise that logical/native/UIA trees are identical.
See the [source map and policy matrix](architecture.md#owned-model-tree-and-traversal-policies).

#### Stage 2 evidence: October 21, 2026

The implementation starts from fetched merged main `8fdc2269a065837737b1dc5dcc0962a574ce10cd`.
The readonly original `99016573d633636c54da71af5db247e5c249051e` artifacts supply native
and performance comparisons; they are never rebuilt or modified by this stage.
Both use x64 MSVC 19.44.35217, SDK 10.0.26100, Release with default IPO ON;
the current gate requires LSH.

**Coupled Release build correctness:** moving the new virtual after the existing
`measure`/`arrange` declarations exposed repeated native `0xC0000005` exits after an
ordinary incremental build, despite a green headless gate. The link command used
`/LTCG:incremental`; the pristine native fixture still passed. With unchanged object/library
inputs, removing only the fixture's generated `.iobj`/`.ipdb` and forcing its relink restored
the focused native contracts. A fresh isolated build also passed the complete gate and
native fixture. Failed executables/caches, hashes, link commands and logs are retained;
this is evidence of a cache-reuse failure in this toolchain/configuration, not a claim
about every MSVC release.

The supported mitigation is an explicit Release `/LTCG` CMake link option when IPO is ON.
Visual Studio generation selects `UseLinkTimeCodeGeneration`; actual link commands use
full `/LTCG`, while actual compile commands retain `/GL`.
Both header/vtable declaration-order transitions were then rebuilt with ordinary commands
in the same existing output directory, without manual cache eviction, and the Classic/WinUI
focused native contracts passed in both directions.
The permanent native gate checks actual core compile and DLL/native-fixture link directives,
and hosted artifacts retain those command logs. MSBuild can still emit an `LTCGOUT` path;
full mode does not reuse it incrementally.
The final declaration keeps the old layout virtual slots ahead of child discovery.
No IPO/optimization-off workaround or obsolete virtual-slot shim is retained.
The final source correction also compares SplitView ancestor eligibility against its
borrowed second owned slot, not the typed convenience getter's returned shared handle.
This removes the transient ownership increments/decrements introduced by that getter's
storage migration. The complete gate and full native ContentHost fixture were rerun
after this correction at code commit `cd9abd4877af21dba8474240d8631abcc6c19953`.

Completed focused checks:

| Command / comparison | Actual evidence |
| --- | --- |
| `tests\framework-pr.ps1` | 47/47 native headless tests pass on the private non-visible station, including the new mandatory owned-child and ContentHost model registrations; 37 core / 2 model classifications are selected, not zero-test name filters. Generator drift/native-export guards, 45,403 generator and 11 MSBuild assertions, managed 80/374/11,006/58 Reveal/23 tab assertions, and complete Rust 1/39/2-doc tests pass. |
| `xui_owned_children_tests` | Every family/direct slot, exact identity/order/reference addresses, empty leaves, custom typed-member projection, nested dialog presentation, invalid topology and weak retirement pass. Repeated direct enumeration and 17,474,000 nested visits allocate zero times. Hidden ColorPicker swatch slots remain owned after its visible palette shrinks. |
| `xui_owned_children_tests --benchmark` and invalid selectors | Explicit benchmark mode reports only its work/allocation checks. Unknown, empty and extra arguments return 2 before work. The ContentHost fixture's new modes reject the same malformed selectors before model/native work. |
| `xui_content_host_window_tests --model-only` | Existing candidate ownership/thread/duplicate/cycle/foreign-host errors plus nested slots, inactive inspection registration, selected/clipped pointer policy and discarded descendants pass without `show`/`run`. |
| `xui_abi_features_tests --headless` | 264,826 assertions pass, including 32 inactive-page/closed-Reveal/ScrollView/SplitView/NumericInput/ColorPicker content-scope commits and clears; stale child/root handles fail and each cycle restores the exact handle baseline. |
| `xui_content_host_window_tests --owned-only` | Classic and WinUI selected-only initial realization, 100 exact model walks without new HWNDs, visited/inactive replacement, closed-Reveal replacement exception and open/close retention pass. Active updates perform exactly 32 active typography calls and zero inactive calls with both 128 and 2,048 unvisited items, retaining exactly one HWND. |
| Full original/candidate `xui_content_host_window_tests` | Both exit 0: 200 native replacement cycles, outside editor text/selection/undo/identity, popup retirement, map/runtime disposal, UIA and explicit fatal failure remain covered. Candidate additionally runs the new focused contracts. Both focus traces report zero activation/focus requests and zero foreground changes. |
| Original/candidate `xui_content_inspection_window_tests` | Both exit 1 with the same `Pointer inspection encountered an unproven native descendant` / `Native inspection fixture completed` failure. Not passed or waived. |
| Original/candidate `xui_content_highlight_window_tests` | Earlier pairs pass 100 highlight/clear/replacement cycles with exact HWND/USER/GDI baseline, then exit 1 in the clipped-layout fixture at `Newly active page is visible`. The final full-LTCG pair instead stops earlier at the physical EDIT hit probe: both report the same external `Tauri Window` blocking the editor. Both exit 1; neither observation is converted into a pass or hidden by changing assertions. |
| Attributed independent coordinator verification at `cd9abd` code | The coordinator reports a new `build\ar2-parent-cd9abd`, not reused cross-vtable outputs: full 47-test gate and complete compiler/managed/Rust coverage pass; actual `/GL` and full DLL/native `/LTCG` are checked. Focused/full candidate and paired pristine ContentHost pass with zero focus/activation/foreground changes; all six malformed selector cases exit 2. Its independent raw audit confirms all 1,278 process cardinalities, all 66 final owned exact-work/storage/allocation/private-growth/CPU-arithmetic checks, and all three loops in each of 66 long processes. Documentation 55-page/747-link and 24-test checks pass. This is attributed coordinator evidence, not a child rerun; frozen-head/hosted review remains pending. |

#### Stage 2 ownership measurements and attribution

The [raw companion](stage2-owned-children-evidence.json) preserves every warmup and
recorded stdout/stderr, exit, order and timestamp, environment/options, full commit IDs,
library/executable/source hashes, actual link commands, and reproducible runner/probe
sources. There are **1,278 successful benchmark processes**, not selected favorable
samples. Each workload/version gets two discarded warmups; process order alternates,
and the first version is counterbalanced between blocks. Builds/suites finished before
each coordinated quiet window. No timing outliers are removed or pooled across revisions.

| Block | Candidate source and purpose | Recorded pairs per workload | Processes including warmups |
| --- | --- | --- | --- |
| `paired-clean-7125ef67` | Initial implementation `7125ef67f583ad9cf711a41d1f6283b4c9aade2a`; preserved original defaults | 7 across five workloads | 90 |
| `paired-extended-7125ef67` | Same intermediate implementation; increased sampling, opposite starting order | 31 across five workloads | 330 |
| `paired-full-ltcg-236f22f0` | Layout virtual declarations restored before child discovery, plus full-LTCG mitigation; preserved pristine stock `/LTCG:incremental` versus candidate full `/LTCG` | 31 across six workloads, including warmed selection | 396 |
| `paired-matched-ltcg-cd9abd48` | Final code `cd9abd4877af21dba8474240d8631abcc6c19953`, including borrowed-pane correction; both stock versions full `/LTCG` | 31 across seven workloads, adding long layout/interaction CPU probes | 462 |

The initial source is **not** the final code: its virtual declaration order and link
configuration precede the corrections above. Its candidate stock link commands were not
preserved contemporaneously; its CMake source used the then-default VS IPO configuration.
The companion retains the actual pristine default and both final matched stock link
commands, not an inference that IPO=ON means identical optimization.
The matched pristine build uses unchanged readonly original source with a small
`CMAKE_PROJECT_INCLUDE` supplying the same directory link option, writing only separate
candidate-worktree outputs. Both versions retain `/GL`, x64 Release `/MT`, MSVC
19.44.35217 and SDK 10.0.26100. All auxiliary traversal/selection/layout/interaction
probes explicitly use full `/LTCG` on both sides. No unrelated algorithm or repeated
code-layout perturbations were used to obtain the matched results.

**Exact ownership work:** 32 PageViews, four pages each, containing closed
Reveal/ContentView/ScrollView/Stack/NumericInput composition: **8,737 nodes, 2,000
walks, 17,474,000 verified visits and checksum**. The original-adapter probe follows
the old ownership accessors; it is test instrumentation, not a runtime compatibility
shim. Checked thread CPU surrounds precisely the same loop as wall time, excluding
construction. In every sampled version, walk allocations are **zero**, construction
allocated bytes are **11,621,480**, and Element/Control/SplitView sizes are **88/440/672**.
There is no within-process private-byte growth during these ownership walks, no HWND,
and no source-backed row allocation. Matched private-byte medians are identical at
32,817,152. The real-default block's 176,128-byte startup/allocator difference is not
a general memory-saving claim.

| Matched ownership metric | Original median (range), ms | Candidate median (range), ms | Absolute / relative delta |
| --- | --- | --- | --- |
| Loop wall | 1,874.21 (1,714.94-2,776.07) | 114.216 (107.481-203.629) | -1,759.994 / -93.91% |
| Loop thread CPU | 1,875 (1,703.12-2,671.88) | 125 (93.75-203.125) | -1,750 / -93.33% |

This is approximately 9.32 versus 139.79 million verified visits per CPU second.
No candidate ownership-loop wall or CPU pair is slower in the matched 31 pairs.
It quantifies ownership discovery only, **not application latency or a whole-window speedup**.

**Independent guards remain mixed, not universally performance-green.** The initial
31-pair block had Stack medians +5.30% unstyled/+6.84% styled, million-row interaction
+6.90%, single-key cold selection +12.11%, and 512-row image work +7.38%.
The later real-default optimizer-transition block still had unstyled Stack
53.734 to 57.0462 ms (+3.3122/+6.16%, 26/31 slower) and million-row interaction
23.1822 to 25.0712 ms (+1.889/+8.15%, 22/31 slower).
Those observations are retained; traversal improvements do not offset or erase them.

Under **matched full-LTCG** settings, stock Stack medians are
53.9263 to 54.0002 ms unstyled (+0.0739/+0.14%, 15/31 slower) and
78.765 to 79.8074 ms styled (+1.0424/+1.32%, 14/31).
Stock million-row interaction is 22.3399 to 23.1729 ms
(+0.833/+3.73%, 18/31). The longer identical public-operation probes exclude setup,
warm first, and enforce exact geometry/measurement counts, identity/checksum/callback
counts, zero allocations, immutable view identity and viewport bounds:

| Matched long workload | Wall medians original/candidate, ms (delta) | CPU medians original/candidate, ms (delta) | Paired CPU median change; slower pairs |
| --- | --- | --- | --- |
| Unstyled Stack, 300,000 frames | 2,846.33 / 2,680.86 (-165.47/-5.81%) | 2,843.75 / 2,671.88 (-171.87/-6.04%) | -4.07%; 12/31 |
| Styled Stack, 300,000 frames | 3,519.91 / 3,520.38 (+0.47/+0.01%) | 3,500 / 3,531.25 (+31.25/+0.89%) | +3.40%; 18/31 |
| Million-row list, 2,000,000 interactions | 2,386.76 / 2,428.37 (+41.61/+1.74%) | 2,375 / 2,406.25 (+31.25/+1.32%) | +0.62%; 16/31 |

Matched warmed selection uses 10,000 warmups then 10 million single-key or two million
32-key enumerations. CPU medians change +46.87 ms/+3.06% and +31.24 ms/+1.68%;
wall changes +38.43 ms/+2.49% and +34.18 ms/+1.84%. Exact returned identities,
allocation/byte counts and checksums pass. This diagnostic is distinct from the stock
100-call cold measurements; it does not overwrite those records.

The matched block still has **outliers/uncertainty**, including the 100k-row sparse and
no-match filter medians 0.4894 to 0.5252 ms (+0.0358/+7.32%, 23/31 slower) and
0.4637 to 0.499 ms (+0.0353/+7.61%, 25/31), and projection
3.7174 to 3.9377 ms (+0.2203/+5.93%, 20/31).
These reverse the earlier real-default block's improvements; other small source/filter
metrics and broad tails are also visible in the companion. Its per-metric medians,
ranges, MAD, nearest-rank p95, paired median percentages and slower-pair counts preserve
dispersion. For example, long styled CPU ranges are 3,218.75-4,125 versus
3,296.88-4,453.12 ms; long interaction CPU ranges are 2,031.25-3,203.12 versus
2,125-3,453.12 ms. CPU accounting is quantized; endpoint ticks are retained.
These observations do not establish that every guard is within 5%, nor that changing
the default optimizer mode is free. Comparisons across separate windows alone cannot
assign the whole timing difference to that mode.

The coordinator authorized **scoped ownership acceptance for PR review** after examining
the exact-work/allocation/storage/native contracts and matched long evidence.
The earlier layout/interaction signals do not persist under the matched measurements;
remaining stock timing and desktop uncertainties stay explicit, not waived or called
green. Source-effect attribution under matched settings is separate from the retained
real-default optimizer-transition observations. Final functional code was verified at
`cd9abd4877af21dba8474240d8631abcc6c19953`; following evidence/index/PR-link commits
change documentation only. Independent exact-head review and hosted checks still precede
any parent merge.

The focused native evidence does not establish physical IME/Narrator behavior, valid
foreground-native input latency/private-memory comparisons, or a green complete desktop
suite. Stage-1 desktop limitations remain unresolved; the two additional baseline-matched
inspection/highlight failures above are also explicit. This stage does not alter those
assertions or attempt unrelated host fixes. Stage 3 and every later stage remain **not started**.

### Stage 3: Meaningful control-family host boundaries

**Scope:** extract lifecycle and behavior, not just text fragments, with family-specific Peer and UIA payloads.
Keep the window-wide focus/modal/frame/lifecycle coordinator and shared drawing resources narrow.
Cover native editors/documents; collections, grids, and the legacy-list adapter; images and native/runtime/swap-chain hosts;
choices/ranges/progress; navigation/tabs/commands; and composite/popup/layout-specific integration.
Exact group boundaries follow current source evidence rather than requiring a universal peer base class.

**Acceptance:** family attachment, sync, input, painting, cancellation, UIA publication, and detachment have explicit owners.
Unrelated controls no longer carry all family fields.
Moved families use only documented narrow host services and compile under the intended dependency boundaries.
Test creation failure, retirement during callbacks, native destruction reentrancy, focus/capture/modal transitions,
popup/scroll/native clipping, theme/DPI, editing/IME/undo, accessibility, and repeated close/replacement.
Peer/HWND/provider identity and resource bounds remain stable; dispatch/storage costs satisfy the evidence policy.
Merely moving monolithic methods into `.inc` files is not acceptance.

The coordinator may subdivide this stage into successive family PRs if required for safe review.
Before each subdivision, add a ledger entry identifying the family, remaining families, prerequisite merge,
acceptance evidence, and PR status.
Use one session per PR and merge it before the next starts.
Do not mark stage 3 or finding 1 complete after a pilot or begin stage 4 with unrecorded remaining scope.

### Stage 4: Origin-aware targeted dirty work

**Scope:** split topology reconciliation from property sync first, then propagate originating identity/subtree
and the topology/layout/placement/native/accessibility/paint categories through affected paths.
Use family-specific sync/publication from stage 3.

**Acceptance:** instrumented paint-only changes do not recollect topology or republish unrelated native/UIA state;
local native/accessibility changes touch the relevant family/state, and layout/topology escalation remains correct.
Global theme/modal/DPI changes retain conservative full coverage.
Reentrant measure/arrange/sync/callback invalidations survive to the required pass;
replacement/removal/closure never leaves stale identity work.
Desktop checks preserve focus, IME/caret/selection/undo and necessary UIA events.
Repeated old/new comparisons report actual traversal, snapshot, allocation, and latency deltas,
including scroll-only, large-source, and callback-driven cases; no speedup is assumed.

### Stage 5: Callback ownership and scoped lifetime

**Scope:** separate internal composite behavior from application/binding observers and replace manual registration-family
retirement with explicit scope ownership across native, C ABI, and .NET.
Remove obsolete capture/chaining/special cases only after their real behavior is covered.

**Acceptance:** observing/replacing/unsubscribing callbacks on NumericInput/ComboBox children does not disable composition;
navigation-owned buttons and Miller child focus/selection still behave correctly.
Document and test event order, single delivery, silent property setters, reentrant subscription mutation,
candidate-content rollback, content replacement, callback close, repeated dispose, and failed registration.
Verify foreign contexts and managed roots release exactly once at the correct thread/phase,
without stale callbacks or release during an unsafe in-flight/native notification.
Preserve failure statuses and application-post cancellation ownership.
New registrations require no added family cleanup loop; allocation/retention and native identity satisfy the evidence policy.

### Stage 6: Authoritative contract metadata

**Scope:** extend the native-export/parity pattern and existing generators to authoritative feature IDs,
shapes, supported controls/events, child cardinality, construction-only/mutable properties, and per-language coverage.
Generate repetitive adapters and parser/generator/structural reload metadata; keep complex ownership logic handwritten.
Update all affected repository consumers together; compatibility shims are not required.

**Acceptance:** headers, native validation/export, generated C#/Rust adapters, markup acceptance, mutable setters,
and hot-reload shape/rebuild decisions agree on every fact within scope.
Negative tests cover unknown/duplicate IDs, wrong shapes/controls/events, unsupported language surfaces,
invalid child counts, illegal mutable updates, and structural changes.
Generated checks fail on drift and do not rewrite files in check mode.
Compiler diagnostics retain source locations, and Designer reload preserves or intentionally rebuilds native identity
according to tested structural rules.
Document unsupported language coverage explicitly without promising full parity.
No runtime reflection or hot-path dictionary lookup is introduced.

### Stage 7: Legacy file-list isolation and shared algorithms

**Scope:** move file-specific foundation declarations first, then consolidate genuinely shared viewport,
image-admission/reconciliation, and stable-identity helpers through adapters.
Update includes/build dependencies and all repository consumers directly.

**Acceptance:** generic foundation use no longer pulls in file-list-specific declarations or worker dependencies.
FileList, virtual collections, trees, and grids retain their distinct selection/focus/filter/thumbnail/UIA semantics.
Cover source/query replacement, cancellation/stale delivery, fast scrolling, failed thumbnail requests,
background release, and closure without UI-thread I/O or joins.
Compare old/new allocations, work, cache/queue bounds, retained resources, and native identity for both legacy and new paths.
Shared helpers actually remove duplication; intentionally separate algorithms have an explicit rationale.
No forced shared inheritance chain or unnecessary compatibility layer remains.

### Stage 8: Boundary enforcement and final evidence

**Scope:** enforce the boundaries established by earlier stages in build/test dependency rules and ordinary PR checks.
Core must stay free of Win32 dependencies, family peers must not reach arbitrary host internals,
and bindings must use explicit runtime services.
Refresh maintainer source maps, contributor procedures, and any affected public contracts.

**Acceptance:** deliberate dependency-rule violations fail automation;
normal PR checks include core, binding/compiler, and generated-contract coverage with a proven desktop partition.
Run the relevant serial desktop and isolated performance suites against the recorded baseline and final candidate.
Publish absolute and relative results, resource/allocation/idle/native-identity counts,
measurement environment, unresolved evidence, and approved behavioral changes.
Reconcile every finding and any stage-3 subdivision with merged PRs and exact accepted commits.
Nothing is marked complete solely because a file shrank or one family passed.

## Risks and implementation handoff

The highest-risk seams are synchronous native destruction reentrancy, deferred-child ownership versus realization,
popup/modal focus and clipping, invalidation raised while processing dirty work, cross-thread UIA snapshot/action delivery,
foreign-context and managed-root release, and compiler reload structure.
Order the changes to expose these seams to tests before optimizing or consolidating them.
Do not combine a new template architecture, custom text stack, or unmeasured generic runtime with this program.

Each implementation PR must update this ledger with its scope/status, PR and accepted commit identities,
acceptance results and limitations, remaining families/subdivisions, and the next permitted stage.
It must also update directly related source maps and public documentation if behavior changes.
Keep commands in CONTRIBUTING and dated evidence in maintainer notes.
The coordinator must verify the merge before creating the next implementation session.
Any unresolved behavioral or performance requirement remains visible as blocked/unresolved, never a success-shaped fallback.

At stage 0, evidence is limited to the source inspection and the separately attributed pristine correctness report above.
Paired baseline/candidate measurements, runtime fixes, automation, dependency enforcement, and all later stages remain unimplemented.
