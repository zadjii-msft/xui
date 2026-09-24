# Experimental DOM web backend

The DOM backend runs the [portable XUI subset](experimental-portable-xui.md) locally through .NET WebAssembly.
It does not require a server event connection or `xui.dll`.
The browser owns fonts, native controls, text editing, and CSS layout.
The experiment does not promise pixel identity with Windows or Android.

The [WebDemo project](../../bindings/dotnet/Experimental/WebDemo/WebDemo.csproj) links the exact `SharedDemo/Greeting.xui` file.
The generator compiles its layout, state, bindings, and handlers into C#.
Blazor WebAssembly supplies startup and JavaScript interop, not a second Razor implementation of the UI.
The sample has no canvas, JavaScript implementation of authored methods, or copied HTML control tree.
Build and browser-test commands are in [CONTRIBUTING](../../CONTRIBUTING.md#experimental-dom-web).

## Shared order-builder sample

[WebOrderDemo](../../bindings/dotnet/Experimental/WebOrderDemo/WebOrderDemo.csproj) is a second thin browser host.
It links the exact [OrderBuilder.xui](../../bindings/dotnet/Experimental/SharedDemo/OrderBuilder.xui) and [OrderModel.cs](../../bindings/dotnet/Experimental/SharedDemo/OrderModel.cs) shared with the other platform hosts.
The `.xui` file remains the sole authored UI; the HTML page contains only mount, error, and bootstrap elements.
The model and authored callbacks run as compiled C# in local WebAssembly.

The sample uses the existing fixed portable tree: three native text inputs, fixed Coffee/Tea/Cocoa quantity controls, dependent totals, validation, and review/edit/reset actions.
It does not add dynamic collections, routing, network requests for order processing, payment handling, or a server event connection.
Review is a local state transition, not a submitted or persisted purchase.
Native fields remain editable during review; an actual edit leaves review.
The browser host uses the same `DomBackend`, dispatcher, and page lifetime as the greeting sample.
It has no application test bridge in Debug or Release.
Its error alert occupies a separate bounded scroll area instead of covering the application controls.
Errors remain visible and keyboard-focusable while native input and Reset remain available.

The browser checks read the same literal [OrderScenarios.json](../../bindings/dotnet/Experimental/SharedDemo/OrderScenarios.json) used by the other acceptance runners.
Expected quantities, totals, validation, and review text come from that fixture, not a JavaScript implementation of the model.

## Shared application gallery

[WebGalleryDemo](../../bindings/dotnet/Experimental/WebGalleryDemo/WebGalleryDemo.csproj) hosts three distinct shared `.xui`/C# applications.
Select `?app=task-board`, `?app=expense-ledger`, or `?app=session-planner`; omission selects the task board.
An unknown selection produces an explicit startup error.
Each application links its shared source and model rather than duplicating controls in the browser host.
The fixed task board edits three priority slots; the expense ledger validates USD drafts and totals; the session planner calculates local focus intervals without starting a timer.
Their shared literal scenarios drive native controls with the network disconnected after startup.
The gallery has no application test bridge in either configuration.

`?app=dynamic-tasks` additionally hosts the shared Dynamic Task Board using `DynamicTaskBoard.Create`.
It is a separate application, not a replacement for the fixed three-slot Task Board.
Adding, deleting, filtering, and reversing rows use the shared keyed component contract.
Surviving keys retain native editor identity; a filtered-out or deleted row is removed, not merely hidden.
The component's `GetState` and `RestoreState` contract owns the retained model; the browser host does not add reload persistence.
Its separate shared scenario corpus supplies the expected values.

`?app=settings` hosts the shared `SettingsShowcase.xui` fixture with native Toggle, CheckBox, and Progress controls.
The three-state checkbox cycles Unchecked, Checked, and Indeterminate when enabled for three states.
In binary mode, a mixed value remains representable but the next activation becomes Unchecked.
Programmatic values remain silent.
This preview does not send notifications, synchronize data, or start background work.

### Profile Workspace

`?app=profile-workspace` runs the shared Profile Workspace with real `IndexedDbApplicationStorage`.
The host links the shared edit/preview `.xui` pages, controller, model, and session types without duplicating the UI.
Startup does not read, write, or delete a stored draft.
Preview and in-memory edits remain unsaved until an explicit Save succeeds.
Reload starts with a blank editor; use Load draft to read a saved document.
Clear editor is not Delete stored draft.
Malformed or failed loads retain the current draft and show the shared error/status state.

The default database name is `profile-workspace`.
The optional `profile-store` query selects another application-owned namespace; it must start with `profile-` and satisfy `StorageKeys.Validate`.
Browser acceptance uses unique test namespaces, never the default user's stored draft.
The controller is owned by the root component lifetime.
Terminal cleanup disposes the host/controller first, then the storage adapter and imported modules, continuing cleanup and aggregating failures.
Real bfcache retention keeps the live controller, incomplete drafts, and native input objects; it does not serialize them into a persistent document.
The host adds no automatic session or reload persistence.

The gallery uses `BrowserErrorReporter` for the shared controller's fatal-error callback.
It logs independently of UI dispatch, marshals DOM reporting to the captured browser UI context, and does not throw from the reporting callback.
DOM/dispatcher/console reporting failures remain observable together with the original error through `LastError`; disposal stops DOM use but not console/retained-error reporting.
Console records escape embedded line breaks, while the error element and retained exception preserve full details.

## Static deployment

Publish the Release application and serve its generated `wwwroot` as static files.
The host must return `application/wasm` for `.wasm` files and the appropriate JavaScript, CSS, and JSON content types.
No development server or server-side callback endpoint is required after publication.
The sample uses a directory-relative HTML base, so the same published files work at `/` or a subdirectory such as `/nested/xui/`.
Use the directory's trailing-slash URL, or configure the static host to redirect to it.
This does not add routing or persistence across full reloads.

After startup, the sample's authored callbacks continue to run locally when the network is disconnected.
This is not an offline-installation promise: there is no service worker or application cache for a new visit.

## Host integration

The adapter library is [Xui.Web](../../bindings/dotnet/Experimental/Xui.Web/Xui.Web.csproj).
The application supplies an empty mount element, a separate error element, and a bounded mount size.
It also loads `_content/Xui.Web/xui-dom.css`.
Both elements must have unique HTML IDs.

```csharp
using var module = await js.InvokeAsync<IJSInProcessObjectReference>(
    "import", "./_content/Xui.Web/xui-dom.js");
void Report(Exception error) =>
    module.InvokeVoid("reportError", "errors", error.ToString());
using var host = new Host(new BrowserDispatcher(Report));
var demo = new PortableDemo.Greeting(host);
host.Attach(new DomBackend(module, "app", "errors"));
```

The host owns the backend after attachment starts.
The application owns the imported module.
The module must remain available until the host and its peers finish disposal.
The sample also uses `BrowserLifetime` for terminal `pagehide` cleanup.

`BrowserDispatcher` records the managed UI thread and its synchronization context.
Without a synchronization context, it uses the single-threaded WebAssembly task queue.
This experiment does not support multithreaded WebAssembly.
`Host.DispatchAsync` reports rejected dispatch, wrong-thread delivery, callback errors, and disposal through its task.
Raw dispatcher callbacks report errors through the required error delegate.

## Browser platform services

`BrowserPlatformServices` implements the shared `IPlatformServices` interface.
Import its separate module and keep it alive for the duration of service calls:

```csharp
using var servicesModule = await js.InvokeAsync<IJSInProcessObjectReference>(
    "import", "./_content/Xui.Web/xui-services.js");
var services = new BrowserPlatformServices(servicesModule);
var availability = services.GetAvailability(ServiceCapability.Clipboard);
```

Clipboard text and URI launch are reported as `RequiresUserGesture` when the browser exposes the required APIs.
Clipboard is `Unsupported` without a secure context or the required Clipboard API.
Open/save file are explicitly `Unsupported`.
Application storage is `Available` when IndexedDB is exposed, using the separate adapter below.
Availability is advisory, not a permission grant.
Invoke a supported operation from a live user gesture; activation can expire during unrelated asynchronous work.
Every operation checks activation again, and browser permission or popup blocking may still deny it.

Clipboard calls use native `navigator.clipboard.readText` and `writeText`.
Write validation rejects null and NUL but accepts Unicode and multiline text.
URI launch accepts only absolute HTTP, HTTPS, mailto, and tel URIs without control characters or HTTP credentials.
Both the shared managed policy and the JavaScript boundary validate inputs.
The URI helper opens a blank browser window, detaches its opener, then requests navigation; a blocked popup is `Denied`.
`Completed` means that the browser accepted the operation, not that a remote page finished loading or an external mail/telephone application completed an action.

Results distinguish `Completed`, native `Cancelled`, `Denied`, `Unsupported`, and `Failed`.
Read `Value` only after `Completed`; failures retain explicit error information.
Invalid input throws instead of becoming a false success.
External cancellation cancels the managed task, including before interop starts.
Cancellation cannot undo a native write or launch that has already occurred.
The application owns the imported module; the service does not retain event listeners or create a background server.
The helper is an ordinary local ES module with no dynamic code evaluation.
Host CSP and permission policy still need to allow the .NET application and any intended navigation; the adapter does not weaken them.

Automated clipboard round-trip checks use an injected isolated Clipboard API double.
They do not read or alter the machine's clipboard.
Real no-activation denial and controlled loopback popup behavior are verified separately.
Physical permission prompts, external protocol handlers, and an actual user clipboard round trip remain manual acceptance gaps.

## IndexedDB application storage

`IndexedDbApplicationStorage` implements the shared `IApplicationStorage` contract for bounded application-owned byte documents.
The application selects a stable database name and owns both the adapter and imported module:

```csharp
using var storageModule = await js.InvokeAsync<IJSInProcessObjectReference>(
    "import", "./_content/Xui.Web/xui-storage.js");
using var storage = new IndexedDbApplicationStorage(storageModule, "my-application");
var result = await storage.ReadAsync("profile", cancellationToken);
if (result.Status == OperationStatus.Completed && result.Value.Exists)
{
    ReadOnlyMemory<byte> saved = result.Value.Data;
}
```

Use and dispose the adapter on the browser UI thread.
Database names and document keys follow `StorageKeys.Validate`: 1-128 lowercase ASCII letters, digits, hyphens, or underscores.
The physical database name is prefixed with `xui-app-`; schema version 1 uses a `documents` object store.
Choose an application-specific name: two adapters using the same origin and name intentionally address the same data.
The adapter never enumerates or deletes unrelated databases.

The default maximum document size is 1 MiB (1,048,576 bytes), configurable through the constructor.
Writes snapshot caller bytes before asynchronous work and use an IndexedDB read-write transaction.
`Completed` is returned only after the transaction commits.
Reads distinguish an absent document (`Exists=false`) from a present empty document (`Exists=true`, zero bytes).
Oversized or malformed stored records are explicit failures, not empty values.
Deleting a missing key completes successfully.
There is no localStorage fallback.

External cancellation aborts an unfinished transaction and cancels the managed task.
Cancellation after commit cannot undo a successful write.
Disposal closes the adapter, aborts its unfinished transactions, and rejects subsequent operations.
Each completed or failed operation closes its database connection.
An open request already handed to the browser is closed or its upgrade aborted when it eventually completes after cancellation.
Permission denial, unsupported APIs, quota errors, blocked opens, and transaction failures remain explicit results.

IndexedDB is browser-managed storage, not an encrypted secret store or a guarantee against quota exhaustion, user clearing, private-session loss, or browser eviction.
Applications must handle these outcomes and choose their own backup, migration, and conflict policy.
The current version provides individual atomic documents, not a public multi-document transaction API.

## DOM and accessibility

`VStack` and `HStack` create flex containers.
`Text` creates a text container, and `Button` creates a native `button`.
`TextInput` creates one wrapper with an associated `label` and a native single-line `input`.
`Toggle` uses a native checkbox input with switch semantics and an associated label.
`CheckBox` uses native `checked`/`indeterminate` state and checkbox semantics.
Both retain browser-native focus and Space activation.
`Progress` uses a labeled native `progress` element.
Its visual ratio is normalized to the authored range while `aria-valuemin`, `aria-valuemax`, and determinate `aria-valuenow` expose the actual authored units.
Indeterminate mode removes the current-value attribute without discarding the retained logical value.
Its animation runs only while attached, visible, and enabled, and respects reduced-motion preferences.
`ScrollView` creates a named region with vertical overflow.
The children retain their authored order.

Control names supply accessible names.
Input captions use an explicit HTML label association.
A hidden caption does not remove the input name.
Help text supplies `title` and `aria-description`.
Buttons retain browser-native Enter and Space activation.
Input Enter submits, except during IME composition.

### Programmatic native input

DOM peers implement the optional `Host.TryFocus`, `Host.HasFocus`, `Host.GetSelection`, and `Host.SetSelection` operations.
Focus requests target the actual input, checkbox, button, or keyboard-focusable scroll element.
Labels and progress do not become focusable merely because an application requests focus.
Hidden/disabled controls and browser-denied requests return `false`, without moving focus to another control.
`HasFocus` reads `document.activeElement`, not a retained managed flag.

Text selection uses ordered UTF-16 offsets on the existing native text input.
Setting a range clamps against its current DOM value, even when that value differs from the model.
Reversed offsets are ordered, out-of-bounds offsets are capped, and surrogate-pair boundaries follow the shared `TextSelection.ClampTo` rule.
The operation does not write the input value, dispatch a change event, or implicitly focus an unfocused editor.
Reading a selection reports the native range; the adapter maintains no managed selection cache.
Explicit selection changes deliberately affect native editing state and are not a guarantee of preserving an active IME composition.
The shared host rejects detached/disposed access and unsupported backend capabilities.

### Form editor boundaries

`TextInput` purposes remain advisory: Email, URL, Telephone, and Number set native input-mode/autocomplete hints while keeping `type=text`.
The browser does not silently apply HTML email/number coercion or application validation.
`MultilineText` uses a native textarea with canonical LF text, its authored UTF-16 length limit, and native read-only state.
Enter remains a newline, not a submit callback.
Programmatic updates remain silent and preserve the existing native editor.

`PasswordInput` uses `type=password`, not a masked ordinary TextInput or a managed text property.
The serialized DOM state contains no password, and a native change notification has no value payload.
`Length` and `WithPassword` query the actual native value, including changes made by autofill.
Only explicit `SetPassword` writes the value; it validates limits/UTF-16 before mutation and does not raise Changed.
Detach and subtree retirement clear the native password before unmount/removal; a new attachment starts empty.
Native copy, cut, and context-menu defaults are suppressed for this control, and the browser reveal affordance is not exposed by the adapter.
This is not a secure vault: explicit interop reads/writes necessarily make temporary JavaScript/.NET string copies, and browser extensions or developer tools are outside the control's isolation guarantee.
Do not log or serialize an explicitly read password.

### Typed typography and scoped themes

Label, Button, TextInput, Toggle, and CheckBox support the frozen typed typography contract.
The browser maps the resolved size to `fontSize / 16` rem without modifying the document's root font size.
This follows browser root-font preference and zoom; it does not claim independent OS-only text-scaling behavior.
Supported weights are 400 and 700; null removes the owned override and restores inherited native styling.
The new multiline/password editors do not gain authored typography support implicitly.

`Host.Theme=null` inherits the host/document appearance.
A nonnull System theme explicitly follows the browser's color-scheme preference.
Validated RGB24 foreground/background/accent roles are scoped to the owned mount, not global document styles.
Forced colors override authored palette colors, and existing reduced-motion/focus behavior is unchanged.
Theme media listeners are released when the override is cleared or the attachment retires.
Only acquired overrides restore mount styles; inherited/null themes leave independently changed host styles alone.
Disabled native controls retain their native disabled colors rather than inheriting authored foreground/accent overrides.
The `forms` and `presentation` gallery routes use the exact shared `.xui` workbenches; no HTML/Razor copy of their UI is introduced.

### Retained workspace controls

`PageView` keeps inactive page instances and native editors attached, but hides/inerts inactive panels and excludes them from layout and accessibility.
Switching pages does not dispose their lifetimes or clear native password values; permanent removal still does.
Selection and visibility preflight reject hiding an active editor or composing subtree.
A focused non-editable action inside a page may move focus to its own linked selector after a successful page change; unrelated focus is not stolen.

`TabStrip` uses a real ARIA tablist with roving native buttons, tab/tabpanel relationships, and separate non-nested close buttons.
Arrow/Home/End selects while keeping focus in the strip; Enter/Space activates through the shared event contract.
Close is an application request, not automatic removal.
`NavigationView` uses native navigation/list semantics and typed links to the same authoritative PageView snapshot.
Native IDs for panels and their labels are generated within the host scope; application automation IDs are not used as an association protocol.
Page and choice identifiers are exact decimal strings across interop.
`SingleChoice` is a native select with disabled options, absent selection, and silent programmatic updates.

`IHostViewportBackend` observes the mount's actual client width/height, matching the root layout allocation rather than the outer window or scroll extent.
The initial metadata is supplied synchronously; the shared Host coalesces later application callbacks.
Subscriptions and native observers retire with their attachment.
Responsive Studio changes the authored Grid tracks and pane visibility at 720/1120 logical-pixel boundaries without reconstructing document editors.

Explicit label layout preserves full source/accessibility text.
SingleLine uses preserved whitespace and native clipping/ellipsis; prohibited paragraph breaks are rejected before accepted mutation.
Wrap uses native line breaking and a native-font line-height probe for a clipped line cap, not a guessed multiplier or multiline ellipsis.
Native measurement probes are inert, hidden from accessibility, and removed even on failure.

The `studio` route runs the exact shared Workspace Studio, including retained document tabs, a virtual catalog, local analysis, and a native-select Operations dashboard.
Its mount-scoped Alt+Left command invokes the shared Back policy before browser traversal; composing editors/select popups take precedence, and only an unhandled command delegates once to browser history.
Browser toolbar navigation remains ordinary document navigation and bfcache behavior, not a claimed internal-history router.
A full reload starts a new workspace; no automatic draft persistence is invented.
Debug-only explicit-test-query quiescence observation awaits the root `LastOperation`; published tests observe real UI results and do not label busy=false as full producer drainage.

### Packaged native images

Image sources are explicit embedded assembly resources, not URLs or filesystem paths.
The browser uses the shared bounded header/format/CRC preflight before invoking its native codec.
Because the cross-engine browser API does not expose metadata-only codec inspection, actual coded dimensions are checked after raw `ImageBitmap` allocation and before rescaling or publishing Ready.
A separate source-pixel reservation and a maximum of two native decodes remain held until uncancelable native work settles and the bitmap closes, including canceled requests.
Output canvas and displayed-image pixel copies have separate reservations in the output budget.
These estimates do not claim a hard bound on internal codec, process, or GPU allocations.

The displayed control is a native `img` with its complete alt name.
Resampling uses an off-DOM canvas only as a codec step, not authored UI.
The logical desired viewport remains 192x144 across Empty/Loading/Error/Ready unless authored sizing/constraints override it.
Decode quality does not change that layout box, and containment uses the original source aspect ratio rather than a rounded output ratio.
Generation strings retain exact Int64 identity across interop; late canceled results release their bitmap/URL and do not revive an old attachment.
An initially hidden image can become Ready without requiring the application to show it first.

### Opt-in retained Reveal

Reveal uses a native browser animation-frame clock and the shared `RevealLayoutMath` measure/arrange contract.
Its retained child stays full-sized on the animation axis; the outer clip reserves only the presented extent, capped by the parent's slot.
This is real layout/clipping, not a translated bitmap or transform-only overlay.
An initially attached Reveal is settled; zero duration, reduced motion, or forced colors settles without an animation clock.
Logical close makes the subtree inert and inaccessible immediately, even while exit pixels remain visible.
Unsafe focused/composing descendants veto close before accepted state mutation.
Closed settled content does not run nested indeterminate progress animation.
Owned animation frames and preference listeners stop on retirement.
Same-motion reversal starts from actual presentation; changing direction/duration first settles the old logical endpoint before applying the new atomic pair.
The Studio Operations drawer remains explicitly opt-in through `motion=1`; the default workspace does not silently gain animation.

Authored IDs appear as `data-xui-id` on control wrappers.
The adapter assigns separate HTML IDs with a unique surface prefix.
Duplicate authored IDs remain separate elements.
An automation query must scope `data-xui-id` to its host.

Disabled controls use `aria-disabled` and `inert`.
Buttons and inputs also use their native `disabled` property.
A disabled ancestor prevents descendant events.
Invisible controls use `hidden`, occupy no space, and accept no events.
Text uses `textContent` or `value`, never authored HTML.

## Browser layout

Lengths use CSS pixels.
Trees without explicit axis constraints or Grid retain the existing CSS-flex layout path.
Stacks use CSS `gap` and uniform, border-box padding.
Hidden children do not create gaps.
Without explicit sizes, browser control chrome and content determine the desired size.

`preferredSize` supplies CSS width and height.
`size` takes precedence and constrains both dimensions within the parent.
Unconstrained cross-axis dimensions stretch.
Minimum sizes are zero, and maximum sizes prevent a child box from exceeding its parent allocation.
Label content clips if the allocated height cannot contain the text.

Non-flex children use an automatic basis.
Positive flex weights divide the remaining main-axis space.
Their percentage basis resolves to content size in an unbounded main axis.
Children can shrink when the parent cannot supply their desired space.
This uses browser flexbox rounding and native measurement, not the Windows layout engine.

A scroll region bounds its width and exposes vertical overflow.
Its content has no parent-height cap.
It does not provide horizontal scrolling.
The sample mount fills the viewport.

### Independent axes and static Grid

An explicit per-axis constraint or a Grid opts the attachment into managed layout using the shared C# `LayoutMath` and `GridLayoutMath` implementations.
The DOM adapter measures native leaf controls, then applies the shared solver's rectangles to the same nodes.
It does not approximate star tracks with CSS `fr` or substitute CSS visual ordering for the authored document order.
Removing all explicit constraints from a Grid-free attachment restores its original CSS layout path.

Null axis metadata inherits the corresponding legacy `size`/`preferredSize` dimension.
Explicit Auto overrides only that dimension and retains cross-axis stretching.
Fixed lengths constrain native width measurement before wrapped height is measured.
Minimum/maximum constraints remain subject to the actual parent allocation.
Finite caps retain their unbounded layout context where specified by the shared contract; they do not implicitly create a flex budget.

Grid supports static cell placement and spans, atomic row/column track changes, and ordinary keyed stacks within a cell.
Column allocation precedes height measurement at the actual clipped span width.
Fixed, Automatic, and Star tracks use the shared deficit/cap redistribution policy.
Unbounded Star tracks keep their natural size, including inside a finite scroll viewport.
An all-fixed cell span establishes a definite child budget; mixed intrinsic spans retain their unbounded context.
This slice does not provide keyed Grid children, Grid gaps, or Grid padding.

Measurement uses ephemeral inert, accessibility-hidden native elements with resolved typography and box metrics from the real control.
It does not copy input values, IDs, form/name associations, event listeners, or resource URLs.
The elements are removed even if measurement fails.
Real controls are never reparented or replaced for measurement.
Property changes and child mutations invalidate layout directly; mount resizing, ancestor style/class changes, stylesheet DOM changes, and font-load notifications trigger coalesced remeasurement.
All observers and scheduled frames are released on detach or when managed layout is disabled.
A layout pass has a four-step viewport stabilization bound and reports a failure rather than silently accepting an unstable result.
An automatic observer failure is reported and stops automatic scheduling; an application can handle the error and reattach.
Normal property-update failures retain the shared detach-on-failure behavior.

The gallery routes `?app=axes` and `?app=grid` host the shared authored layout fixtures in Debug and published Release.
They preserve ordinary C# callbacks and native input without an application test bridge.

### Leased virtual viewport

The opt-in `?app=virtual-list` gallery sample uses the shared 10,000-row C# controller and native text inputs.
It does not create one editor per item.
The viewport owns a native scroll-intent element with a logical extent spacer, plus a separate clipped content layer.
Scrolling intent does not itself move that content layer.
The layer exposes only an explicitly committed offset, width, height, and source snapshot.

`Host.BeginVirtualViewport` returns an attachment-owned lease.
Its asynchronous requests contain exact Int64 epochs/source versions, transported as decimal strings and compared without JavaScript number rounding.
`TryBeginUpdate` reserves one immutable request for synchronous row staging.
`TryCommit` validates each visible logical index against actual native row metadata and geometry before publishing the viewport.
The adapter rejects a missing row, duplicate index/key, stale source, or invalid physical placement instead of showing an unrealized committed gap.
Ordinary OS accessibility can observe valid intermediate removals/additions; this is not an atomic OS-tree snapshot guarantee.
New scrolling intent during staging waits for a later request.

Active composition holds scrolling and viewport growth; a smaller allocation clips to the previous committed intersection.
Source changes additionally wait while an actual row editor has focus.
The native content layer preserves row identity, selection, and value while other rows and gaps are inserted or removed.
Focus/blur/composition snapshots and edits use the same ordered browser callback queue.
A new input event or unblock can retry retained intent; there is no polling timer or posted scroll-offset rollback.

Row metadata uses the shared opaque key and zero-based logical index, not automation-ID parsing.
Native list items expose `aria-posinset` and `aria-setsize`.
Tab follows the currently realized native controls in source order, including retained pins.
The shared First/Previous/Next/Last actions and row Enter operation navigate the logical source.
Continuous Tab traversal and accessibility realization of every offscreen item are not promised by this slice.

Create the shared viewport application before attachment with no realized rows or gaps.
For reattachment, capture editing state, detach, and call `PrepareForViewportAttachment` while detached before the next native `Attach`/`BeginVirtualViewport`.
This preserves external drafts, selection, source intent, and offset without briefly constructing an ordinary huge sparse scroll tree.
Lease disposal invalidates callbacks and hides/inerts its virtual content; it never reveals an ordinary sparse-scroll fallback.
Browser extent limits are checked before use, and an unrepresentable extent is an explicit failure.

The DOM lease also implements the optional `ISettledVirtualViewportLease` capability.
`FlushCommitted(epoch)` completes pending post-prune layout and validates the actual native rows for the current committed epoch.
It requires no active reservation and does not consume newer queued intent or invoke authored callbacks inline.
The shared sample requires this capability before use and invokes it after controller pruning, before its completion notification.
This reports settled native geometry, not presentation at vsync or an atomic accessibility snapshot.

## Events and lifetime

Each attachment creates new DOM peers from retained state.
Property updates change the existing nodes.
Label and button text updates arrive as `ElementProperty.Name`.
Input text updates arrive as `ElementProperty.Text`.

A surface queues asynchronous .NET callbacks in browser event order.
All authored handlers run as compiled C# in the browser.
Native input changes do not write the same value back into the input.
Unrelated state changes preserve the input, focus, selection, and composition.
Programmatic edits stay silent and invalidate queued text events from an earlier text revision.
Programmatic choice changes use the same revision protection for queued Toggle/CheckBox values.

The adapter checks serialized state, property values, ownership, event types, and text revisions.
Interop or authored callback errors appear in the error element and the browser console.
After an error report, the event queue can process later events.
The browser tests reject unexpected console errors, page errors, and rejected callback promises.

Detach first invalidates the event sinks and unmounts the DOM tree.
The host then disposes peers in reverse creation order.
Each peer removes its listeners and releases its .NET callback reference.
The backend releases its JavaScript surface reference after the final peer.
Backend disposal does not dispose peers itself.
Old nodes and queued events stay inactive after a later attachment.

### Keyed subtree mutation

DOM peers implement the portable `IMutableElementPeer` capability.
New subtrees are built unmounted, then inserted at the requested native child index.
Removed subtrees stop accepting events before host-owned reverse peer disposal.
Surviving keyed children move as the same DOM nodes in actual document, keyboard, and accessibility order.
The adapter does not use CSS ordering as a substitute for native tree order.

When the browser provides `Element.moveBefore`, a same-parent move uses that state-preserving operation.
Otherwise it restores the moved editor's focus and selection after native insertion, without rewriting its value.
If an input in the moved subtree is composing and state-preserving moves are unavailable, read-only move preflight rejects the update explicitly.
The caller can retry after composition ends; there is no silent composition cancellation or automatic deferred retry.
The runtime preflights the whole move plan before factories or model mutation.
Native operation failure follows the shared detach-on-failure contract and retains the committed model for reattachment.

The sample disposes its host, test bridge, and imported module on a terminal `pagehide`.
If `pagehide.persisted` is true, the browser retains the live application in its back-forward cache.
A cache restore therefore retains the same application and input state.
The browser releases the entire page realm if it discards a cached page without another lifecycle notification.

## Limits

This backend supports only the declared portable subset and its scoped keyed composition contract.
It has no arbitrary cross-parent tree mutation, hot reload, router, server rendering, or general Windows control parity.
The sample has no persistence across a full page reload.
It requires a browser with WebAssembly, flexbox, JavaScript modules, and `inert`.
Automated composition events do not establish compatibility with every operating-system IME or assistive technology.

The current automated acceptance target is Microsoft Edge on Windows x64.
Its coverage includes the Debug suite and published Release at root and subdirectory URLs, including real back-forward-cache navigation.
The browser runs without an XUI-specific GPU-disable flag.
This is a provisional experimental test target, not a general browser support guarantee.
Other Chromium distributions, Firefox, Safari, mobile browsers, and Windows ARM64 need their own complete acceptance runs.
Physical IME, screen-reader output, and browser zoom remain manual acceptance gates.
The [maintainer evidence](../llm/dom-web.md#clean-browser-acceptance-september-23-2026) records exact versions and outcomes.

The greeting sample's optional `?test` bridge exists only in Debug builds.
It exposes test commands for retained state, dispatch, attachment, and intentional error cases.
Release builds do not contain its C# implementation.
The static JavaScript bridge alone cannot access an application or execute those commands.
The Debug-only `?mutation=true` page compiles the shared generated mutation fixtures for browser conformance.
Those fixtures are not included in Release, and that page uses native authored actions rather than an application test bridge.
