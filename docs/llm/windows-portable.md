# Windows portable adapter

The experimental Windows adapter runs the `Portable` generator profile through
the existing native XUI controls. It does not replace the Windows generator
profile or its window-owned application API.

## Source map

- `bindings/dotnet/Experimental/Xui.Windows/WindowsBackend.cs`: native peers,
  content ownership, deferred event delivery, and optional keyed mutation.
- `WindowsDispatcher.cs`: UI dispatch and cancellation of accepted work when
  the window closes or is destroyed before showing.
- `WindowsPlatformServices.cs`: explicit clipboard and URI service results.
- `bindings/dotnet/Xui/ContentMutation.cs`: additive append, mutation, and
  detached-handle release bindings.
- `bindings/dotnet/Xui/LayoutConstraints.cs`: validated per-axis values and
  atomic native sizing overrides with independent capability detection.
- `bindings/dotnet/Xui/ScrollLayout.cs`: explicit viewport-filling behavior,
  preserving the legacy native default.
- `bindings/dotnet/Xui/GridLayout.cs`: the versioned portable Grid capability.
- `bindings/dotnet/Experimental/WindowsPortableTests`: shared fixed, settings,
  mutation, and dynamic application corpora plus native ownership fixtures.
- `bindings/dotnet/Experimental/WindowsGalleryDemo`: thin shared-application
  host and owned-window PNG capture. It accepts Order, Tasks, Expenses, Planner,
  DynamicTasks, Settings, Axes, Grid, and Profile.

## Ownership and compatibility

A `WindowsBackend` borrows a `ContentHost` and its window. Each attachment owns
one `ContentUpdate` native arena. Backend disposal releases that arena; it never
destroys the window. Peer wrappers release their managed references after arena
retirement. Do not externally replace or clear the borrowed host while attached.
Two portable attachments cannot concurrently claim the same managed surface.

The original DLL remains usable for fixed trees. The adapter checks all eight
new native mutation exports before returning a mutable peer. Missing exports
mean explicit rejection of keyed content, not rebuilding a whole window or
pretending that window-owned controls have individual disposal.

With a mutation-capable DLL, append construction creates fresh nodes in the
committed attachment arena. Completion exits the construction context before
insertion. Aborting an append removes its new handles and callback roots and
cancels work queued by that append. New subscriptions on pre-existing handles
are not incorrectly retired.

Removal first invalidates event delivery and detaches the native subtree.
Reverse peer disposal then releases each unreachable native handle and its
managed callback registration. Surviving nodes keep their HWNDs and editing
state during moves. Preflight rejects structural edits during native text
composition before factories or portable model changes. Whole-attachment
cleanup remains the recovery path after a post-commit native failure.

Native inspector/picking restrictions remain explicit; the portable adapter
does not bypass those guards.

## Independent axis constraints

Axis support is separate from content mutation. A DLL can support keyed
mutation without exporting `xui_element_set_axis_constraints` and
`xui_element_get_axis_constraints`. Raw managed axis access requires those two
exports. The portable adapter additionally requires explicit scroll viewport
filling support before exposing `IConstrainedElementPeer`. Unsupported live
constraint setters reject before the portable model or attachment changes.

The managed `Xui.AxisConstraints` value validates finite, nonnegative minimum
and optional length/maximum, ordered bounds, and a length inside those bounds.
An explicit `Auto` differs from a nullable value of null: Auto uses native
natural measurement for that axis; null inherits its current legacy sizing.
Cross-axis Auto still stretches into the parent allocation. Minimums never
force overflow beyond a smaller parent.

`Element.SetAxisConstraints(width, height)` sends the complete pair in one
native call. `GetAxisConstraints` preserves both optional axes and their
individual sizing modes. The ABI uses a 40-byte versioned pair of 16-byte axis
values. An invalid second axis cannot partially apply the first.

The implementation does not approximate overrides with `MinimumSize`,
`MaximumSize`, or `AutoSize`. Those older setters share global preferred-size
flags, and resetting them cannot restore independent natural caption height.
The native override instead leaves all legacy sizing fields intact. Legacy
setters update their backing values even while masked; null reset restores the
latest values on that axis.

Native `ScrollView.FillViewport` retains the Windows default of true. Portable
scroll peers explicitly set false when the new exports are available, preserving
unbounded layout through arrangement as well as measurement. Merely shrinking
the content to its measured total is insufficient: finite arrangement could
redistribute that total among flex weights and change natural child slots.
An older axis-only DLL therefore does not qualify for portable axis support.

An explicit Auto reference must be compared with another explicit Auto
reference, not the untouched legacy default. For example, the recorded WinUI
default-font editor was 60 DIPs with inherited legacy sizing and 62 DIPs with
font-aware explicit Auto. Neither value is a cross-platform constant.

`Program.Constraints.cs` contains value, old-runtime rejection, native
round-trip/atomicity, legacy-reset, font-aware editor, and generated-source
geometry fixtures. `--require-constraints` is the explicit native acceptance
gate; `--constraints-contracts-only` and `--constraints-fallback-only` do not
prove native axis rendering. The gallery's Axes option compiles the exact
shared `AxisSizingShowcase.xui`, including minimum pressure, capped flex
allocation, and unbounded-scroll cases.

## Static Grid boundary

Portable Grid creation requires `xui_grid_layout_version() == 0x00010000`,
both axis exports, and the explicit scroll-filling exports. Existing Grid
constructors alone do not establish corrected unbounded-track or span sizing.
An unqualified DLL rejects Grid before allocating its native peer.

The adapter maps fixed, automatic, and star tracks and immutable cell placement
to the existing native Grid API. Track replacement updates the retained native
Grid instead of rebuilding editors. Grid is not a mutable-stack peer; keyed
insert/remove/move is not added to its cells by this adapter.

## Native focus and selection

Windows peers implement the optional portable focus contract. Nonfocusable
labels and progress indicators return false; focusable controls query and
request actual native focus. Only text-input peers expose the selection
interface. Selection reads use the native range, and setters clamp and
normalize UTF-16 offsets against the current native text, not a potentially
older portable value waiting for deferred change delivery.

The portable Host supplies thread, ownership, mounted-lifetime, and disabled or
hidden ancestry guards. Selection is an intentional editing operation, not a
model setter, and does not generate a change event. Native focus rejection is
reported as false; unrelated native failures still propagate explicitly.
Permanent keyed removal invalidates the input, whereas reordering retains its
native focus and selection.

The native `Focused` feature must query the owner window's actual focused
HWND, not only the retained control's styling flag. Coalesced disable/enable
and hide/show changes can clear that flag without a physical focus transition.
The native focus-query correction preserves reads without synthesizing new
focus events.

Hidden controls do not participate in Stack measurement, gaps, alignment, or
flex weights. Authored child indices remain stable and hidden geometry is
cleared. A visible zero-size control or empty stack still participates, so
visibility cannot be inferred from a zero measurement. Native and Profile
fixtures distinguish these cases explicitly.

## Profile application host

The gallery's Profile option compiles the shared edit/preview/navigation
application. Its storage provider is created lazily beneath
`LocalApplicationData\Xui\PortableGallery`; startup and capture neither create
that directory nor read stored profile data. Only explicit application storage
commands instantiate the provider. The default capture is an empty editor;
displayed sample names are placeholders, not loaded records.

The root component owns its controller and asynchronous work. Shutdown
disposes the host before awaiting the controller's last operation so producers
can settle cancellation without requiring delivery to a closed UI. Fatal
reporting is thread-safe and nonthrowing: errors are logged and retained, and
a window-owned close is posted when possible.

Native Profile tests use a newly owned temporary directory, never the gallery's
private directory. They run the shared 45-expectation corpus, verify no startup
storage calls, and check that navigation and clearing the editor do not
implicitly save. A transient session round-trip creates new page identities
without storage access. A deliberately late provider result after host disposal
cannot recreate native peers.

## Events, dispatch, and services

Native input callbacks queue authored handlers in unscoped window context so
handlers can safely remove their own subtree. Queues preserve delivery order.
Attachment lifetime and peer revisions reject retired or superseded callbacks.
Value setters are silent. If an availability change invalidates a queued value
event, the peer restores its native value from the authoritative model rather
than leaving the two values divergent. This correction is limited to peers
with pending value events; unrelated updates do not rewrite active editors.

Callback failures retain the original exception in `WindowsBackend.CallbackError`,
write an explicit diagnostic, and propagate through the native run loop.
There is no success-shaped callback fallback.

`WindowsDispatcher` implements `ICancellableUiDispatcher`.
The additive `Window.PostUnscoped(Action, Action canceled)` overload observes
the existing native discarded-post callback. Accepted work faults on shutdown
without executing the application action, including destruction of a window
that was never shown. Existing `Window.Post` behavior is unchanged.

Clipboard and URI operations use the shared service policy, UI dispatch,
external cancellation, and explicit operation outcomes. Clipboard reads are
bounded Unicode reads; writes use the existing native managed API. Only
validated HTTP, HTTPS, mailto, and tel targets reach ShellExecute. No file
picker or storage capability is advertised.

Service protocol fixtures use an injected fake bridge. They deliberately never
read, replace, or restore the user's clipboard and never launch a browser or
external application. Real clipboard/URI integration remains a separate manual
acceptance gate.

## Recorded evidence: 2026-09-23

The full Release Windows portable run passed **2,746 assertions** with
`--require-mutation --require-constraints --require-grid`, using the native DLL with SHA-256
`3E23160C1978A104B7D119C7285F2EE465A589BC310D57FA11B24788FAC294BA`.
The run covered the shared OrderBuilder, three fixed gallery applications,
SettingsShowcase, mutation fixtures, DynamicTaskBoard, axis and Grid showcases,
and ProfileWorkspace. It included native
Edit identity and selection, real mouse/Enter/Tab delivery, native geometry
order, append rollback, repeated handle-baseline checks, focused-row removal,
type replacement, pre-commit rejection, post-commit recovery, and accepted
dispatch completion. Closing a window during an unfinished append also released
its arena before the deferred Closed event. Composition guard tests used synthetic native composition
messages, not physical IME acceptance.
Axis checks cover atomic two-axis validation, legacy override/reset, font-aware
caption height, minimum pressure, capped flex slots, and unbounded layout.
Grid checks cover spans, track changes without replacing editors, fixed versus
mixed cell context, and owned nested content. Focus and UTF-16 selection use
the public portable Host API. Hidden-child gap and flex checks include the
exact 12-DIP gap above the Profile editor.

The unchanged Windows-profile Greeting smoke and OrderBuilder smoke also
passed against that DLL; OrderBuilder reported 172 assertions.
The independent fake service protocol reported 30 assertions.
An earlier fixed-only compatibility run passed 1,677 assertions before the new
focus and visibility acceptance cases were added. Current older-DLL probes
explicitly reject unsupported constraints and Grid without losing an existing
attachment; those probes do not claim the old DLL contains later native fixes.

Nine real client-window PNGs were captured and visually inspected. Capture uses
the application's HWND with `PrintWindow`, not a desktop screenshot, and
dismisses transient owned tooltips without moving the user's cursor. Execute
the manifest-bearing apphost (or `dotnet run`), not `dotnet application.dll`,
for native common-control activation.

Native UI tests need an exclusive desktop lane. Simultaneous HWND fixtures and
the app's own overlay can steal focus or occlude physical hit tests. No retry
or skip was added to conceal that environmental interference.

The native recovery fixture also exposed a generator cache defect: a keyed
binding retained its earlier cached descriptor array after a model-committed
failure. The generator owner fixed that cache and added regressions before
this run passed. Clearing back to the earlier empty array is part of the
native acceptance fixture; it is not worked around in the adapter.
