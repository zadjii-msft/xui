# Windows host family boundaries

## Scope and delivery

Stage 3 extracts materialized Windows control hosts, not the public model tree.
Collection/grid/legacy-list hosts landed in [#60](https://github.com/zadjii-msft/xui/pull/60).
On October 22, 2026 the maintainer changed the delivery plan: finish the remaining
stage-3 work directly in the architecture-review worktree, without child sessions,
and submit one consolidated PR. Stages 4-8 are deferred.

The native editor commits were incorporated before the remaining family extraction.
The former `Window::Impl::UnextractedPeer` and its window-owned paint/input dispatch
are removed, rather than renamed or moved into another universal peer.
Public model and binding APIs are unchanged.

## Ownership map

| Owner | Responsibilities |
| --- | --- |
| `host_peer.hpp/.cpp` | Common identity, parent and native handles, placement caches, shared frame/input contracts and restricted window services. No family payload. |
| `collection_host`, `grid_host`, `file_list_host` | Existing concrete row engines, virtual sources, gestures, images, typed providers and native legacy adapter. NavigationList, MillerColumnList and CommandMenu remain shared row-engine specializations. |
| `native_field_host`, `text_input_host`, `document_host`, `password_host`, `date_time_host` | Real EDIT/RichEdit/password/date controls; captions, clear adornments, suggestions, native editing, embedding and independent metadata providers. |
| `control_host.hpp/.cpp` | Common input surface/provider lifetime and generic activation. `ControlHostServices` exposes named window operations, not `Impl`, arbitrary owner callbacks, or a recovery cast. |
| `basic_host.hpp/.cpp` | Label/button/toggle text layout and measurement, choices and type-ahead, repeat activation, ranges/numeric input, combo behavior, progress/status/chart/color painting and family UIA actions. |
| `scene_host.hpp/.cpp` | Standalone image ownership, VectorCanvas/MapView paint/input, native runtime and swap-chain adapters, cancellation and producer teardown. |
| `navigation_host.hpp/.cpp` | Tab artwork and retained image work, tab press/drag initiation, Miller-container scrolling/focus and navigation/title-bar painting. |
| `composite_host.hpp/.cpp` | Content/Popup/Expander surfaces, scroll-content HWND and placement state, split/scroll gestures and composite accessibility actions. |

Window still owns model topology and claims, lazy PageView/Reveal policies,
popup/modal/focus coordination, command/menu scheduling, cross-window tab moves,
shared Drawing/composition, frame/placement batching and exception/InputScope/close
barriers. Stack, Grid, Wrap, PageView and AdaptiveLayout remain model/layout objects;
their existing presentation traversal does not gain unnecessary host peers.
ContentHost replacement remains a window ownership transaction.

Family paint methods use the current frame's Drawing and palette. They do not add
render targets or per-item windows. Image admission still gathers all participating
families together, cancels hidden work before visible admission, and retains images
across the shared target. Window-wide timers remain coordinated by Window; button
repeat state and its input-surface timer belong to the basic host.

## Accessibility and lifetime

Native field metadata, collection and grid provider states remain independently
retained. The remaining control snapshot now has a closed, disjoint payload:
identity-only, scroll, selection, numeric value, or split geometry/value. Ordinary
labels/images no longer carry tab vectors, scroll data and range data together.
Writing a second incompatible family payload is an explicit logic error.
Read-only non-applicable properties preserve their previous empty/default metadata
without allocating or mutating the snapshot.

Provider publication prepares replacement data before taking the mutex.
Snapshot/action retirement and native/COM operations stay outside state locks.
Native HWNDs and providers are retired through the existing input/lifetime barriers;
moving paint/input ownership does not replace native text, clipboard, composition,
caret, selection, undo or UIA Text/Text2 with a framework emulation.

`tests/control_host_tests.cpp` covers detached family identity/capabilities, storage,
cancellation, null-model rejection, exact retirement and disjoint accessibility
payloads. Existing family/native provider and lock-retirement tests remain required.
The ordinary gate requires 51 headless and four lifetime registrations.
Build and test commands are in [CONTRIBUTING](../../CONTRIBUTING.md#tests).

## Consolidated implementation checks

The final code commit is `fc9a74fc53e5f51b43d844b050c1a8395a3c5095`.
The [raw evidence companion](stage3-host-boundaries-evidence.json) contains 58
byte-hashed artifacts, final test logs/receipts, all retained native failures and
the 355-file code manifest rehashed after execution. Embedded artifacts reconstruct
with `base64.b64decode(base64)`. Initial local compile failures are retained too;
the final gate and focused checks below are subsequent successful builds.

The direct worktree build uses Windows x64, MSVC 19.44.35217, SDK 26100,
Release `/GL` and full `/LTCG`. The complete `framework-pr.ps1 -Suite All`
passes: 51 native tests on a noninteractive station, 45,403 generator assertions,
11 MSBuild assertions, managed 80 basic/374 feature/11,006 style/58 Reveal/23 tab
assertions, and Rust 1 sys/39 library/2 compile-fail documentation tests.
The final inventory retains the existing ten lock-retirement cases.
Documentation checks pass (55 pages/748 local links and 24 site tests).

The new detached test measures actual materialized peer types at 256 bytes
(basic), 208 (scene), 224 (navigation) and 216 (composite), compared with the
368-byte pre-extraction native-editor-stage remainder. `ControlSnapshot` is
304 bytes with disjoint payloads. These are x64 type-storage observations, not
heap/private-memory or throughput measurements.

The focused native pass uses the existing bounded runner on a unique, unswitched
desktop in visible WinSta0: no desktop switch, global input injection or clipboard
mutation. Native owner attachment/rollback/destructive callbacks, native range
editing, the complete real-editor/UIA verifier and full 200-replacement ContentHost
fixture pass. Collection exact work/allocation/native identity, navigation-view
styles/layout, precision scrolling and swap-chain lifetime pass; swap-chain pixel
capture is explicitly disabled, not claimed passed.
The independent final audit checks all 56 editor records and 126 exact editor
work fields against the retained baseline. All four collection phases preserve
their exact source/selection/callback/checksum/allocation work, four peers, five
child HWNDs, one target and zero unchanged-editor scroll writes. The single
collection run's wall/CPU output is retained but is not a controlled timing result.

Two additional fixtures fail their foreground-dependent popup-focus assertions:
Foundation at `Popup focuses native EDIT`, and MenuBar at `Down automatically
opens and focuses the menu`. The same fixture objects and unchanged fixture/header
sources, linked with the retained pre-extraction `68f9a571` production libraries,
fail at the identical assertions on the same isolated desktop. Both retained
libraries were rehashed unchanged before/after that correctness-only comparison.
These failures are retained, not converted into passes or repaired by changing
window focus policy. Their standalone comparison links are not timing evidence.

## Preserved evidence and limits

The earlier editor-only extraction has extensive correctness and performance
evidence. It is historical evidence for that candidate, **not a final consolidated
phase-3 performance pass**. Its exact raw companions are losslessly compressed:

| Companion | SHA-256 of decompressed JSON |
| --- | --- |
| [Initial evidence](stage3b-native-editor-host-evidence.json.gz) | `66E944908DE603F23CFAF5B44FF09D30361A4697BC84B2EDD0CF052A1EE7A325` |
| [Opposite-order confirmation](stage3b-native-editor-host-confirmation-evidence.json.gz) | `DC5E141456F2D02C844934DEB725C649687842405CA14954DCA0A324B866CCCC` |

Both gzip files have zero modification timestamps and decompress to the original
JSON bytes, including their embedded artifact paths, encodings, BOMs and hashes.
Use Python's standard-library `gzip.open(path, "rb")` to read them. The embedded
schema-1 artifact bytes are `base64.b64decode(bom_base64) + text.encode(codec)`;
do not normalize their newlines. A known early failed owner-fixture executable was
overwritten before archival; its retained hash is not a retained executable.

The initial block retained 36 process trees/198 timed rows. The separate,
opposite-order confirmation retained 132 trees/726 timed rows. No attempts or
outliers were removed or pooled. Confirmation left a collection-paint median-wall
crossing: 1105.3598 -> 1165.9440 ms, +60.5842 ms / +5.480948%.
Its paint CPU median was +4.347826%; paired wall median was +2.445247%, with
18/31 candidate pairs slower. This does not diagnose the cause or establish that
the consolidated implementation has fixed it.

Two symmetric 4096-frame standalone harness images were subsequently compiled
against the frozen libraries. They were not executed: the maintainer redirected
work to completing the refactor and PR. No sustained-workload result, favorable
relink, timing waiver or universal speedup is claimed.

Physical IME/Narrator, foreground latency, complete private-memory attribution,
all-desktop green, earlier inspection/highlight/presentation/retained-scroll
limitations and recorded baseline failures remain unresolved. The PR must expose
these limits; structural completion is not performance acceptance or authorization
to start stage 4.
