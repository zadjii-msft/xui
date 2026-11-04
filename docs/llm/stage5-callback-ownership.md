# Composition callbacks and registration scopes

## Scope and implementation

Phase 5 is implemented directly in the architecture-review worktree, without
child sessions. It is submitted in [#63](https://github.com/zadjii-msft/xui/pull/63),
open for review, not merged. The original implementation's comprehensive
performance failures and the subsequent correction remain separately recorded.
Its immediate baseline is merged phase 4,
`de9ee24ea4664f0f6d0fcaf87f573383086244da`
([#62](https://github.com/zadjii-msft/xui/pull/62)).
Metadata authority, legacy foundation isolation and general dependency enforcement
remain phases 6-8 and are not part of this change.

| Source | Responsibility |
| --- | --- |
| `include/xui/callback.hpp` | Separate composition and observer roles, explicit composition scope retirement, immutable observer snapshots and current-identity gates. No global event bus. |
| Core control/composite implementations | NumericInput/ComboBox editor behavior, child step buttons, color channels, navigation/search, command/menu/caption actions, tab-new actions and Miller child behavior bind the composition role rather than public observer slots. |
| `src/basic_host.cpp`, `src/navigation_host.cpp` | Host-owned combo acceptance and Miller focus use host scopes independently of application observers. |
| `src/c_api.cpp`, `src/c_api_features.inc` | Foreign subscriptions install observers without capturing previous callbacks. The old `abi_callbacks.hpp` preservation variant, navigation-owned-button interception and Miller parent-to-child relay are removed. |
| `src/c_api_content.inc` | Content retirement closes a handle's delivery gate before releasing its retained Node, rather than clearing an expanding list of callback-family fields. |
| `bindings/dotnet/Xui/CallbackLifetime.cs` | Owns callback roots, cleanup and in-flight leases for a window or content scope. Cancellation prevents new dispatch; physical release waits until existing dispatch has returned. |
| Managed window/content/subscription adapters | Each registration joins its owning lifetime when created; retirement has no general/menu/file/Miller family loop. Long-lived window handler roots use the same mechanism. |

Synchronous receiver pins, immutable source retain/release callbacks and posted
execute/cancel ownership retain their existing distinct contracts. A callback scope
must not turn a borrowed receiver into a persistent registration, free a source
while an in-flight read owns it, or release a posted action twice.

## Delivery contract

Public `on_change`, `on_click`, `on_focus`, and related observer setters replace
only the public observer. Passing an empty callback removes that observer, not the
composition that makes the child work. Bindings use the same observer role.
Property setters remain silent unless the particular control already documents an
action notification; this change does not invent a global property-changed event.

Each event captures the selected observer and composition before delivery.
Composition commits its model behavior before that event's observer is delivered.
Replacing or removing the observer during delivery affects the next event, not the
already selected one. Reentrant invocation retains the existing ABI busy/error
behavior; callback errors retain the explicit failure/close contract.

Keyed command/breadcrumb children additionally validate that the same child is
still current after its action. Removing/replacing a path during a parent event
must not deliver an obsolete child notification. Disabling a still-current child
does not erase its completed action notification. Miller selection delivers its
parent event before the child observer and checks the current source/focus again
before child delivery. Focus composition selects the owning column before an
application focus observer runs.

Composition scope retirement first prevents every new internal delivery, then
releases registrations. Copies already executing retain their callable until they
return. Retained children cannot call a destroyed composite; their independent
application observers remain theirs. Replacing a composition binding reuses its
registration when no captured copy needs the old one.

Scopes use intrusive registration links rather than a vector of weak pointers.
Composition snapshots use non-atomic, UI-thread reference counts and retain the
callable itself instead of copying its captures for each dispatch. Public
observers retain their immutable callable through a non-atomic reference-counted
record. An event without an observer captures only the composition registration,
not an empty observer slot and a second Callback wrapper. An explicit
in-flight count prevents retirement from destroying the executing callable when
its owner releases the last non-dispatch reference. Current-identity predicates
run only while their composition is still active; ungated retained-child
observers keep their independent delivery contract.

Native EDIT/RichEdit text, caret, IME composition, selection, clipboard, undo and
provider behavior remain OS-owned. Host/input exception barriers and deferred
native destruction remain unchanged.

## Managed registration lifetime

Windows and content candidates each own a `CallbackLifetime`. New callback
families add their cleanup when registering; they do not add another loop to
`Window.Dispose` or `RetireContent`. The registration owns its weak `GCHandle`
and dictionary/delegate cleanup. Window/content owners retain live registrations.

Unsubscription first detaches native delivery, then retires the managed token.
A token retired from inside its own callback remains pinned until that callback's
lease ends. Content rollback/replacement cancels the entire scope before releasing
roots. Repeated retirement/disposal is idempotent. Failed initial registrations
retire their new token, while failed replacements preserve an existing observer.
Window disposal still requires returning from native execution/callbacks.

General, menu, Miller and file subscription objects embed their registration;
there is no separate token, cleanup closure or registration hash-set entry.
Intrusive links permit constant-time removal. A window may retain at most one
fully released general/menu subscription for reuse. It retains neither an old
content scope nor a GCHandle. A self-unsubscribed token is not reusable until its
last dispatch returns, even if a replacement subscription is added immediately.
Native Node delivery needs only a cancellation bit: the dispatch's existing
shared Node reference already retains its callback context through return.

## Verification

The required `xui_callback_ownership_tests` covers composition/observer order,
replacement/unsubscription, silent setters, retained child handles, owner destruction,
reentrant observer changes, release after in-flight return and Miller path retirement.
The existing allocation fixture checks empty scopes and warmed callback dispatch.
The complete native/binding gates retain ABI source-release, callback-error,
subscription and content-replacement coverage.

`CallbackOwnershipTests` additionally exercises managed root retirement during an
in-flight lease, cancellation, exactly-once cleanup, candidate rollback across
general/menu/file/Miller registrations, weak-capture collection, self-unsubscription,
numeric child observation, repeated disposal and long-lived window handler removal.

Commands belong in
[CONTRIBUTING](../../CONTRIBUTING.md#callback-ownership-checks).
Structural source changes alone are not a performance or desktop acceptance claim.

## Results: November 4, 2026

The later [comprehensive study](stage5-comprehensive-performance.md) expands this
two-workload result to 61 phases / 90 processes. It finds additional model,
managed-registration and native-host signals, including per-event allocation
regressions. **The blocker is not limited to NumericInput.** The earlier results
below remain historical, separate evidence, not an all-up acceptance statement.

The original measured implementation commit is `075196b2ee0e29e7d63d001709ceaa8d3fd1bf4d`.
The [raw companion](stage5-callback-ownership-evidence.json) preserves 257 byte-hashed
artifacts / 1,582,662 original bytes, all blocks and stopped attempts, seven final
native receipts, tool sources and a 429-file source manifest. Artifact bytes
reconstruct with `base64.b64decode(base64)` without newline normalization.
Earlier draft implementation trees were not separately frozen; their libraries,
images, probe sources and commands remain retained. Only the final implementation
is claimed reproducible from its committed source.

The following results describe that historical implementation, not acceptance of
the subsequent performance correction.
Its Framework gate passes **53 native tests / six lifetime registrations**, the
existing compiler/generated/native-export guards, managed callback/wrapper/style
tests and complete Rust coverage (1 sys, 39 library and 2 compile-fail doc tests).
Generator/MSBuild checks pass 45,403/11 assertions; managed baseline coverage
remains 80/374/11,006/58 Reveal/23 tab assertions. New managed scope checks include
18 assertions. Documentation checks cover 55 pages/751 local links and 24 site tests.
Actual Release compilation/link logs retain `/GL` and full `/LTCG`.
The native callback fixture exercises real NumericInput and editable ComboBox
EDIT notifications, observer replacement/removal, stable native identity, callback
close and explicit callback failure. The same fixture linked to verified merged
phase-4 libraries reproduces lost numeric composition; the candidate passes.
The negative control's initial expected inner-error string was too narrow: native
failure closes the window, then the posted continuation reports the failed model
effect. Both the original output/receipt and its assessment remain retained.

The existing allocation fixtures still require empty scopes to allocate nothing,
16,384 warmed small/large observer deliveries to allocate nothing, and warmed
CommandBar refresh to allocate only its child/ID vectors. No weakening of those
thresholds was used. UI-thread observer snapshots retain an immutable callable
record with non-atomic ownership under the existing model thread-affinity contract.
Composed delivery consumes an already captured event; ordinary Button invocation
does not snapshot an unused toggle or construct empty composition/gate functions.
The final native cases also pass 63-owner retirement/destructive callbacks,
document range editing/undo, the full real-editor/UIA/clear-retirement verifier,
200 ContentHost replacements, phase-4 exact-work isolation and navigation.

### Fixed callback comparison and remaining blocker

**Performance acceptance remains blocked.** The final fixed block retains two
warmup pairs and seven recorded alternating pairs: **18 processes / 36 timed
phase rows**, with no failed attempt, exclusion or replacement in that block.
Baseline and candidate use the same probe source and compiler settings
(`/O2 /Ob2 /GL /MT`, full `/LTCG /INCREMENTAL:NO`). The baseline libraries match
the committed phase-4 evidence hashes; its headers are exported from merged #62.
The final baseline image is reused unchanged across correction blocks.

| Model workload | Iterations per process | Median wall baseline -> candidate | Median owner CPU baseline -> candidate |
| --- | --- | --- | --- |
| Ordinary observed Button | 134,217,728 | 862.2644 -> 540.6853 ms (-37.29%) | 859.375 -> 546.875 ms (-36.36%) |
| Composed NumericInput step | 1,048,576 | 765.4202 -> 846.7379 ms (**+10.62%**) | 765.625 -> 843.75 ms (**+10.20%**) |

The numeric phase also crosses 5% in wall sum (+8.3171%) and paired median
(+7.9939%), plus owner-CPU sum (+8.1006%) and paired median (+8.3333%).
These are six statistics for one phase, not six independent failures. The absolute
median wall difference is 81.3177 ms per 1,048,576 steps, about 77.55 ns per step.
Its cause is not declared resolved or dismissed as noise. Aggregate measured wall
improves 1629.3559 -> 1394.4597 ms (-14.42%); that does **not** waive this phase.

Both versions invoke every action and verify the full callback count, checksum
and numeric value. Ordinary action dispatch allocates zero times. Numeric stepping
has identical 4,291,458 ordinary C++ allocations / 187,658,304 requested bytes in
every baseline/candidate process, including warmups. Long localized number strings
cross SSO boundaries, so these counts are pinned to the first positive baseline
warmup and checked exactly, not naively extrapolated from short runs.

Setup is outside measured dispatch. Ordinary Button setup changes 2 -> 3
allocations / 232 -> 304 requested bytes; NumericInput setup changes 60 -> 67 /
5096 -> 5408 bytes. Those costs own registrations rather than recurring per event.
Actual x64 model storage changes:

| Type | Baseline | Candidate |
| --- | --- | --- |
| Control | 440 | 400 |
| Button | 592 | 472 |
| TextInput | 880 | 760 |
| NumericInput | 640 | 592 |
| ComboBox | 656 | 648 |

These are object and requested-allocation observations, not complete retained
heap/private-memory measurements. Model callbacks follow the existing owning
UI-thread contract; this is not a new cross-thread event API.

All earlier images, shorter/older-candidate blocks and the stopped long-work
warmup remain separate. The first implementation exposed real redundant snapshot
overhead; subsequent corrections remove that work without relinking favorable
baseline images. The first long-work warmup stopped because default stream
precision rounded the serialized million-step value; both probes now emit exact
double precision. No outcome from that stopped block is pooled into final results.
The larger final workload supplies finer owning-thread CPU resolution than the
shorter initial phases. These are model dispatch measurements, not displayed
frames, native typing latency or a claim that the whole framework is faster.

## Remaining evidence limits

This phase does not waive the historical stage-3 collection-paint concern, physical
IME/Narrator/foreground latency, full private-memory attribution, or previously
recorded unrelated desktop failures. Ordinary C++ new/new[] counters exclude
aligned allocations and COM/OS/DLL heaps. Registration setup storage and per-event
allocation must be reported separately; replacing observer slots with scoped roles
must not be described as a universal speedup.
