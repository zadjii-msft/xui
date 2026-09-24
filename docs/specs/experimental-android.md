# Experimental Android backend

The Android adapter runs generated C# locally through .NET for Android.
It uses native Android widgets, not a WebView.
The [portable contract](experimental-portable-xui.md) defines the authored subset.
This backend does not port the Windows C++ framework.

The application targets `net10.0-android` and requires Android 8.0, API 26, or later.
The adapter namespace is `Xui.Experimental.Android`.
The [Android demo](../../bindings/dotnet/Experimental/AndroidDemo/AndroidDemo.csproj) links the exact shared `Greeting.xui` file.
Its Activity contains startup and lifetime code, not a second copy of the UI.
The Windows generator profile remains unchanged.

## Application integration

The application imports `Xui.Portable.targets` and references `Xui.Android`.
All host construction, element access, and native operations require the Android UI thread.

```csharp
var surface = new Android.Widget.FrameLayout(activity);
activity.SetContentView(surface);
var dispatcher = new Xui.Experimental.Android.AndroidDispatcher();
var host = new Xui.Experimental.Portable.Host(dispatcher);
var component = new PortableDemo.Greeting(host);
host.Attach(new Xui.Experimental.Android.AndroidBackend(surface, dispatcher));
```

The surface must be empty before attachment.
The caller owns the surface and the Activity.
The host owns its backend and peers after attachment starts.
`AndroidBackend.Dispose` only unmounts the root.
The host then disposes each peer in reverse creation order.
The adapter removes native listeners before it releases widgets.

`AndroidDispatcher` posts to the process main looper.
A rejected post throws.
The dispatcher does not cancel accepted callbacks when an Activity stops.
`Host.DispatchAsync` reports a disposed host through its returned task.
Applications must observe that task.

Native callback failures go to Android logcat under `Xui.Android`, then propagate.
The adapter does not replace an authored failure with success.
Platform update failures follow the shared host detachment contract.

## Native mapping

Stacks use a `LinearLayout` subclass with portable measurement and arrangement.
Static grids use a native `ViewGroup` with the shared track/span solver.
Labels use `TextView`.
Buttons use `Button`.
Inputs use one retained `EditText` and a `TextView` caption inside a `LinearLayout`.
Binary `Toggle` and three-state `CheckBox` use retained native checkbox widgets.
`Progress` uses the native horizontal progress bar.
Vertical scrolling uses `ScrollView`.
Each element also owns a measurement wrapper that applies its size constraints.

The backend implements the documented native subset; unsupported kinds or capabilities fail explicitly rather than being ignored.
It converts logical lengths from dp to physical pixels with the display density.
Positive half pixels round away from zero.
Fractional allocation budgets round down to whole pixels so children remain inside their slots.
Dimensions saturate at Android's 24-bit measured-dimension limit.
Native fonts, button minimums, chrome, and font scaling remain platform-specific.

Stack spacing applies only between visible children.
Non-flex children receive their desired size first, bounded by the remaining allocation.
Flex weights divide the remaining bounded main axis.
Flex children use their desired size when that axis is unbounded.
The cross axis stretches children unless a fixed size limits them.
Padding, preferred size, and fixed size include the element's complete outer box.

The scroll child receives a bounded width and an unbounded height.
The adapter does not force that child to the viewport height.
Disabled scroll containers also reject touch, wheel, and keyboard input.
Invisible elements use Android `Gone` and consume no layout space.
Disabled ancestors disable their native descendants.

### Independent axes and static Grid

Native peers support the shared per-axis constraints contract.
Null width/height constraints inherit the corresponding legacy fixed/preferred size.
Explicit Auto ignores legacy hints only on that axis; fixed, minimum, and maximum values remain bounded by the parent offer.
Width is resolved before natural height, including a second native measurement when minimum-width expansion requires it.
Constraint and track updates reuse existing widgets.

Grid columns resolve before row heights, and row measurement uses the actual pixel-bounded span width.
Static row/column placement and spans use shared validation and layout math.
Keyed content may live inside a static cell.
Unbounded star tracks and flex descendants retain natural sizing through later finite arrangement and Auto maximum caps.
All-fixed cell spans establish a definite child budget; mixed spans preserve their unbounded context.
Android measurement specs cannot represent that distinction, so the adapter carries it separately and invalidates native measure caches when only the context changes.
Grid gaps and padding are not added implicitly; use an enclosing stack where needed.

### Keyed native children

Android stack peers implement the portable mutable-child protocol.
Insertion, removal, and movement use native child order, including children with no visible content.
Move preflight does not mutate widgets or reject a future index merely because earlier insertions have not run yet.
Same-parent movement reorders the native child array without a window detach.
The generated mutation fixture verifies retained editor identity, focus, selection, and composing spans, with no native attach/detach notifications during the move.
This automated evidence does not replace physical IME acceptance.

Removed peers unregister their automation lookup entries and release their own listeners and native views.
The host, not the parent peer, disposes removed descendants.
Removing and then reintroducing a key creates a new native editor; stale callbacks cannot target it.
The shared runtime owns pre-commit rejection and post-commit detach/recovery semantics.

## Text and accessibility

Unrelated updates do not assign `EditText.Text`.
They retain the input identity, selection, focus, and composition spans.
A programmatic text replacement suppresses native change callbacks and clamps the previous selection to the new text.
That replacement can end the current composition because it changes the edited value.
The adapter does not synthesize delayed text events.

Native text changes call the authored change handler.
The single-line editor uses `ImeAction.Done` for submission.
Hardware Enter submits once, on non-canceled key-up, including Android versions that also label hardware Enter as `Done`.
An Enter sequence that starts during native composition is suppressed through its repeats and matching key-up, even if the IME ends composition between those edges.
A soft Done action is suppressed while the native editor is composing.
The editor requests `ImeFlags.NoFullscreen` so a landscape keyboard does not replace the application with a fullscreen extracted editor.
This is a native IME request, not a custom keyboard; individual keyboards control whether they honor it.
Buttons and labels use their native text as the accessible name.

The visible input caption uses Android `LabelFor`.
A hidden caption supplies the input name through `ContentDescription`.
The placeholder remains the native editor hint.
Help uses Android's native tooltip, which Android also exposes through accessibility node information.
TalkBack behavior still requires device acceptance checks.

`AutomationId` uses the native control's string `Tag`, not an Android resource ID.
`AndroidBackend.FindViews(id)` returns all matches within that backend.
Duplicate authored IDs remain distinct widgets.
Android generates a separate view ID for each input's label association.

### Programmatic focus and selection

Android peers implement the portable host's native focus and text-selection interfaces.
`Host.TryFocus` calls the retained native control's `RequestFocus`; `Host.HasFocus` reads its actual native focus.
Focus can fail for controls that are not natively focusable.
The host returns `false` for disabled or hidden focus targets and rejects detached, foreign, and disposed input targets according to the common contract.

Selection is reported as ordered UTF-16 offsets.
An editor with no native selection spans reports a collapsed range at zero.
`Host.SetSelection` clamps against the current native text using the shared `TextSelection.ClampTo` rules, including surrogate-pair boundaries.
It calls `EditText.SetSelection` without assigning text or replacing the widget.
Explicit focus and selection changes deliberately affect editing state; they do not promise to preserve an active physical IME session.
An explicit focus request can use Android's native touch-mode focus transition when ordinary `RequestFocus` cannot focus a focusable toolbar control.
Pointer clicks do not implicitly steal editor focus.
Native focus and composing-span changes are coalesced into posted interaction snapshots; unchanged selection spans do not generate duplicate snapshots.
Observation follows replacement native `Editable` objects, and queued callbacks are invalidated when the peer is disposed.

### Choices and progress

`Toggle` is a binary checkbox presentation, not a `ToggleSwitch`.
`CheckBox` retains unchecked, checked, and indeterminate state.
Mixed state uses a native minus indicator; it is not collapsed to `checked=true`.
Three-state user activation cycles unchecked, checked, mixed, unchecked.
Turning three-state cycling off leaves an existing mixed value intact until the next activation changes it to unchecked.
Programmatic updates are silent, and inactive ancestry rejects native callbacks.

On API 30 and later, mixed state has the native state description `Mixed`.
API 36 also receives its native partial checked-state value.
On API 26-29 the accessible label retains its text and appends `, Mixed`; that fallback still requires device/TalkBack acceptance.
Normal labels and checked-state semantics return when the state stops being mixed.

Progress is read-only and has no input callbacks.
Its native visual position is a quantized 0-10000 ratio, while accessibility describes authored units.
`AccessibilityNodeInfo.Extras` contains exact doubles under `Xui.Android.Progress.Minimum`, `.Maximum`, and `.Value`, plus the boolean `.Indeterminate`.
Indeterminate progress omits `.Value` and standard range information.
Android's standard `RangeInfo` is float-only: it uses authored units when representable, but is omitted if conversion is nonfinite, collapses the bounds, or puts the value outside the converted range.
An exact unit description and the double extras remain available instead of exposing misleading range values.
Native fonts, indicator styling, and screen-reader output still require platform acceptance.

### Native form boundaries

Text-input purpose changes only the native keyboard hint through `SetRawInputType`, retaining the text key listener.
Email, URL, telephone, and number hints do not validate, trim, coerce, or digit-filter authored/native text.
`MultilineText` uses a native multiline editor: Enter inserts LF, incoming CRLF/CR is canonicalized before the UTF-16 limit, and read-only blocks native editing and accessibility mutation while programmatic updates remain silent.
Native edit filters report rejected invalid edits without rewriting the existing value.

`PasswordInput` uses a native masked editor and password accessibility.
Its value belongs to the native attachment, not the managed component state.
Notifications contain no text; explicit scoped reads use bounded temporary character buffers cleared in `finally`.
Set/read operations validate UTF-16 pairs, limits, NUL, and line breaks.
Native copy/cut are blocked, autofill is disabled, and the IME is asked not to learn the value.
Hiding or disabling does not clear it; retirement clears before unmount and a new attachment starts empty.
This is logical clearing and bounded managed-buffer ownership, not a guarantee that Android or an IME erases every internal copy.

### Typography and host themes

Explicit portable typography uses native SP sizing, so Android font scaling applies to the editor and its caption.
The five supported targets are label, button, text input, toggle, and checkbox.
Normal/bold use the existing native family; null restores the captured native typeface and size.
No editor text is assigned and no input widget is replaced by a typography or theme change.

Themes affect only the owned host surface and its native controls.
Null inherits the caller's existing appearance; explicit System is an opted-in scheme.
The first non-null application captures the current surface background, clear restores it and releases ownership, and reacquisition captures any intervening caller change.
Native text, hint, background, and choice color-state lists are restored on reset, including disabled states.
A configuration observer is owned by the theme attachment; there is no idle timer.
Native high-contrast text rendering remains platform-owned and is not represented as a Windows-style forced-color palette.
Theme changes add no animation or override of Android's reduced-motion policy.

### Retained pages, selection, viewport and labels

`PageView` keeps native page children alive while inactive pages are hidden, disabled through ancestry, and excluded from accessibility.
Switching an affected composing page or hiding a focused pane is rejected before model commit.
The adapter does not force composition to end to make a page mutation succeed.
Tabs use Android `TabWidget` inside a horizontal scroll view; navigation uses `ListView`, with stable 64-bit IDs rather than row positions.
A deliberate navigation activation moves actual native focus only when the attachment has no composing editor.
Close is a request to authored code, not automatic disposal.
`SingleChoice` uses a native `Spinner`; empty/all-disabled choices display a nonselectable absence row, never a fabricated model ID.
Programmatic selection is silent and callback-initiated changes win over the older native event.

Viewport observation reports the owned surface's final content allocation after padding, in density-independent units.
Native layout notifications and subscriptions retire with their attachment.
Label wrapping and ellipsis preserve the complete text/accessibility value; null restores captured native layout settings.
Horizontal stacks measure wrapped native content at its allocated width before deriving natural row height.
They do not pin a height or shorten a button caption to hide clipping.

The gallery includes `WorkspaceStudioActivity`, using the shared Studio sources, local generated documents and explicit local analysis.
The Activity persists only the shared transient-session codec and an authored focused-field ID with clamped selection.
It restores focus only once that native editor has visible positive geometry; a temporarily zero-height editor is not declared usable.
Composition, keyboard state and scroll position are not persisted.

## Activity-scoped services

`AndroidPlatformServices(activity, dispatcher)` implements the shared clipboard and URI service contract.
Create it, query availability, and dispose it on the UI thread.
Asynchronous operations may originate on a worker thread and are dispatched to the UI thread.
The service holds only a weak Activity reference; a finishing, destroyed, or disposed owner cannot be reused.
Applications should dispose the service when its owning Activity is destroyed.

Clipboard and URI availability is advisory.
Each operation requires a focused, live Activity.
Unfocused calls and native security rejections return `Denied` without an empty-value success fallback.
Clipboard reads support text only; missing/unreadable data fails explicitly and nontext data is unsupported.
An actual non-null empty text value is valid `Completed("")`.
Writes use the shared null/NUL validation.

URI launch uses the shared absolute HTTP, HTTPS, mailto, and tel policy, including control-character and HTTP-credential rejection.
A missing native handler returns `Unsupported`.
Completion means Android accepted the launch intent, not that the external application completed an action.
File selection and application-storage capabilities remain `Unsupported`.
External cancellation cancels the returned task; disposal faults accepted queued work without requiring the queue to drain.
Cancellation cannot undo a native side effect that already occurred.

## Activity lifetime

The demo attaches in `OnStart` and detaches in `OnStop`.
The host retains the model between those calls.
The demo disposes the host in `OnDestroy`.
Old widgets and callbacks cannot reach a later attachment.

The demo surface applies current system-bar, display-cutout, and IME insets as padding on Android API 30 and later.
Earlier versions use the legacy system-window insets with `AdjustResize`.
The scroll viewport stays within that usable area even with edge-to-edge window behavior.
Content larger than the keyboard-reduced viewport remains reachable by native scrolling.
Applications providing their own surface must handle their window insets.

Rotation creates a new Activity and host.
The demo saves `Count`, `Entry`, `Message`, focus, and selection in the instance-state bundle.
It restores authored state before attachment.
It does not retain Activity objects across rotation.
Composition sessions, keyboard visibility, and scroll position do not survive recreation.
Applications with additional state need their own state restoration.

## Shared order-builder host

The separate [Android order demo](../../bindings/dotnet/Experimental/AndroidOrderDemo/AndroidOrderDemo.csproj) links `SharedDemo/OrderBuilder.xui` and `OrderModel.cs`.
It uses the same generated component and immutable `OrderState` as the other platforms, not a second Android-authored form.
The fixed catalog, quantity limits, validation, discount arithmetic, and local review behavior remain shared C#.
Review does not place an order or take payment.
Its application ID is `dev.xui.portable.orders`; installing it does not replace the greeting application.

The order Activity follows the same attach/detach/dispose lifecycle and inset handling.
It saves all seven authored fields: customer name, email, discount code, three quantities, and review mode.
Recreation restores a new immutable state before attachment.
Focus restoration identifies the actual focused input (`customer-name`, `email`, or `discount-code`) and restores that field's selection, rather than always selecting the first editor.
No Activity, native widget, composition session, keyboard visibility, or scroll position is serialized.

The separate native order assertion app links the shared `OrderScenarios.json` and `OrderScenarioRunner.cs`.
It drives actual Android buttons and editors and reads native text, enabled state, and visibility against literal fixture expectations.
Additional Android checks cover each editor's identity, selection, composition spans, silent reset, inactive event rejection, and stale widgets.
The order-specific device script checks the actual Activity's multi-field restoration and keyboard-visible scrolling.
These automated checks do not replace physical IME and TalkBack acceptance.

## Shared application gallery

`AndroidGalleryDemo` links the shared Task Board, Expense Ledger, and Session Planner components and their models.
One package (`dev.xui.portable.gallery`) contains distinct launcher Activities, without Android-specific authored forms.
Each Activity saves state through the shared source-generated `GalleryStateCodec` and separately restores the focused field and selection.
Malformed saved JSON is an error, not an implicit reset.
The existing keyboard-aware surface is shared with the order host.

The explicit Activities are `dev.xui.portable.gallery.TaskBoardActivity`, `dev.xui.portable.gallery.ExpenseLedgerActivity`, and `dev.xui.portable.gallery.SessionPlannerActivity`.
The separate `DynamicTaskBoardActivity` uses the shared keyed task application and `DynamicTaskBoardCodec`.
Its focused input identity includes the stable task key, so recreation can restore a particular surviving row rather than an array position.
The installed .NET Android `Run` target selects one with `-p:RunActivity=<fully-qualified Activity>`.
The gallery capture helper launches only explicit sample Activities and validates their native window and expected content before saving PNGs.
It captures the shared initial states; screenshots are device output, not mocked or redrawn controls.

### Profile workspace

`ProfileWorkspaceActivity` hosts the exact shared profile editor and preview application.
Its document store is `Activity.FilesDir\xui-profile`, using `DirectoryApplicationStorage`.
Creating, mounting, navigating, and recreating the Activity never automatically load, save, or delete a stored document.
The directory is private application storage, not an encrypted secret store.

The root component lifetime owns the controller.
Activity state uses the shared versioned transient-session codec: editable draft, last-confirmed saved snapshot, page, and interruption status.
It does not serialize tasks, the controller, or native/navigation UI keys.
Restoration creates fresh idle navigation entries and performs no storage IO.
Interrupted work is reported as uncertain; cancellation cannot undo an already committed provider write.
Destruction observes the retired controller's operation asynchronously rather than blocking the UI or letting a late result target the replacement.

Native Back routes to the controller only when navigation depth is greater than one and storage is not busy.
Otherwise normal Activity Back behavior applies.
Worker failures are logged and may post a notification through a weak, live Activity reference.
The native profile corpus uses a separate GUID-scoped directory in the test package; it never runs against the user's profile store.

## Acceptance status

The reference-only check compiles the production sources against Microsoft's real Android reference assemblies.
The arithmetic tests run without Android.
Neither check produces an APK or establishes native execution.
The native test application requires an Android device or emulator.
Build commands and the device smoke procedure are in [CONTRIBUTING](../../CONTRIBUTING.md#experimental-android-backend).

Actual APK construction, deployment, native assertions, and automated demo interaction have passed on an API 35 x86_64 emulator.
The reusable device check exercises counter limits, injected text, selected-range preservation, rotation, background restoration, and reachable controls with a docked keyboard.
It does not establish physical IME composition, TalkBack usability, Activity-retention profiling, or the minimum-API support matrix.
Stage 1A's physical-device and accessibility gates remain open.
The [maintainer evidence](../llm/android-experiment.md) records the environment, commands, results, and remaining limits.
