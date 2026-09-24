# Experimental portable XUI

The `Portable` generator profile compiles a small `.xui` subset into managed retained elements.
It shares the Windows parser, state dependency analysis, and C# emitter.
It does not port `xui_core`, replace `Xui.Managed`, or promise equivalent platform appearance.
Windows remains the default profile.

The runtime targets `net10.0` without native DLLs, platform SDKs, or runtime code generation.
The namespace is `Xui.Experimental.Portable`.
The runtime project is [Xui.Portable](../../bindings/dotnet/Experimental/Xui.Portable/Xui.Portable.csproj).
This experiment has no hot reload, general styles, or general control parity.
New managed Forms and Presentation contracts are capability-gated; their native projections are still being integrated, as described below.
Scoped reusable components and keyed children are opt-in features requiring a mutable backend.

## Project integration

For a repository-local portable application, import `bindings/dotnet/Experimental/Xui.Portable.targets`.
This import sets `XuiGeneratorProfile=Portable` and references the generator and runtime.
It includes local `.xui` files and exposes the profile to Roslyn.
It rejects `XuiHotReload=true`.
An absent profile, or `Windows`, preserves existing Windows output.
An unknown profile produces `XUI001`.

The following project fragment links the shared demo from a sibling experimental project:

```xml
<Import Project="..\Xui.Portable.targets" />
<ItemGroup>
  <AdditionalFiles Include="..\SharedDemo\Greeting.xui" Link="Greeting.xui" />
</ItemGroup>
```

The [shared demo](../../bindings/dotnet/Experimental/SharedDemo/Greeting.xui) contains all UI and application behavior.
The portable tests and [Windows demo](../../bindings/dotnet/Experimental/WindowsDemo/WindowsDemo.csproj) compile that exact file.
The Windows demo uses the existing Windows generator profile and native bindings.
Platform hosts supply only startup, dispatch, and widget adapters.
Build procedures are in [CONTRIBUTING](../../CONTRIBUTING.md#experimental-portable-foundation).

Authored `code csharp` stays real compiled C#.
The profile does not rewrite authored types, expressions, or strings.
Portable applications must use APIs available on their target.
Browser applications run this C# locally through .NET WebAssembly, not a server event connection.
Android applications run this C# through .NET for Android.

## One application, three hosts

The repository contains one working application definition, not three independently maintained UIs:

| Project or directory | Responsibility |
| --- | --- |
| `Experimental/SharedDemo/Greeting.xui` | Shared layout, state, bindings, and compiled C# event handlers |
| `Experimental/WindowsDemo` | Native Windows startup and lifetime, using the default Windows generator profile |
| `Experimental/AndroidDemo` | Android Activity startup, native widgets, and recreation state |
| `Experimental/WebDemo` | Browser startup and page lifetime, using local .NET WebAssembly and real DOM controls |

These paths are relative to `bindings/dotnet`.
Every host links the same `.xui` file as an `AdditionalFiles` item.
The generated class is compiled into each host; it is not a shared binary that depends on all three platforms.
Windows uses `Xui.Declarative.targets`, while Android and web opt in to `Xui.Portable.targets`.
Do not switch the Windows host to the portable profile: its constructor accepts a native `Xui.Window`, not a portable `Host`.
That restriction describes the original `WindowsDemo` project, not the opt-in Windows adapter.
`WindowsGalleryDemo` uses `WindowsBackend` and `WindowsDispatcher` with a native `ContentHost`, so portable-generated components accept the same managed `Host` on all three platforms.
The original Windows generator profile remains available and unchanged.

Keep application behavior in the shared source and platform startup in the host projects.
Additional platform-independent C# classes can also be linked into each project with ordinary MSBuild `Compile` items.
Avoid referencing Windows-only, Android-only, or browser-only APIs from shared code.
Platform-specific services need an application-owned boundary.
The helpers below provide explicit service outcomes, owned work, navigation history, and small-document storage; they are not an unrestricted cross-platform filesystem or a complete platform router.

The [single build/run workflow](../../CONTRIBUTING.md#build-one-app-for-windows-android-and-web) builds all three hosts from one command.
Use it as a repository-local starting point and stay within the supported language below.
The [experimental preview packages](packages.md#experimental-portable-preview) also provide an external template with one compiled shared library and thin hosts.
The existing `dotnet new xui` template remains Windows-only.

### Order builder sample

The [order layout](../../bindings/dotnet/Experimental/SharedDemo/OrderBuilder.xui) and
[immutable C# model](../../bindings/dotnet/Experimental/SharedDemo/OrderModel.cs) form a more complete application using the same six-control subset.
`WindowsOrderDemo`, `AndroidOrderDemo`, and `WebOrderDemo` link these files directly.
The smaller greeting application remains available separately.
See [build and scenario commands](../../CONTRIBUTING.md#order-builder-conformance).

The catalog is deliberately fixed: coffee costs $12.50, tea $8.00, and cocoa $10.00.
Each quantity ranges from zero to nine.
Optional code `SAVE10` applies a ten-percent discount; matching ignores case and surrounding spaces without rewriting the editor.
Totals use decimal arithmetic and invariant two-decimal USD formatting, not the host's current currency or locale.
The model rounds discounts to cents away from zero at a midpoint; these catalog prices already produce exact-cent discounts.

Customer name and email are required, at least one item must be selected, and a nonblank invalid discount code prevents review.
Email validation is a small client-side syntax check for an unquoted local part and dotted domain, not deliverability verification or complete international email support.
It uses the existing single-line text control, not a new email-specific keyboard or autofill API.
Validation remains visible and explains invalid input.
**Review order** displays a local summary; it does not place an order, submit customer data, contact a server, or take payment.
Editing a field or quantity leaves review mode.
**Edit** keeps all data, and **Reset** clears all three inputs, quantities, coupon, and review state.

The generated component exposes an immutable `OrderState State` snapshot.
Handlers replace that value, and generated bindings update existing controls.
The typed input refs are `CustomerNameInput`, `EmailInput`, and `DiscountInput`.
Unrelated changes do not replace or write back into the editors.
The Android host saves the authored snapshot and the focused field's identity and selection during recreation.
The web host retains the live application during back-forward cache navigation; a full reload starts a new order.
The Windows application retains state for its window lifetime.
There is no application-managed order database or cross-device synchronization.

The [shared scenario corpus](../../bindings/dotnet/Experimental/SharedDemo/OrderScenarios.json) drives the recording backend and real Windows, Android, and browser controls.
Its expected values are literals, independent of the production calculations.
Additional platform checks cover editing, scrolling, lifecycle, and cleanup.
The catalog rows are authored statically: this sample adds neither dynamic collections nor new portable controls.
Physical IME and assistive-technology acceptance remain separate from injected-input automation.

### Additional shared applications

The [screenshot gallery](../llm/portable-gallery.md) contains actual Windows, Android, and web renders of four distinct shared applications.
`TaskBoard`, `ExpenseLedger`, and `SessionPlanner` add seeded status/filter editing, decimal budget calculations, and time arithmetic.
Their fixed-slot/category/block limits are explicit; they do not masquerade as dynamic collections.
`DynamicTaskBoard` separately exercises keyed insertion, removal, filtering, and reordering.
`ProfileWorkspace` exercises real owned asynchronous storage and edit/preview navigation.
These samples supplement, rather than replace, the smaller greeting and order regressions.

### Editable cart

`EditableCart` adds distinct browse, product-detail, and cart-edit pages with stable-key quantity editors.
It uses the original order catalog prices and `SAVE10`, but its lines can be added, removed, sorted, and reversed.
Invalid quantity drafts remain editable rather than being coerced to numbers.
The customer name, coupon, line identities, draft text, and logical navigation history have a versioned session representation.
Restoration creates fresh page identities; an interrupted quote becomes idle with an explicit warning.

Create it with `EditableCart.Create(host, new LocalCartQuoteService(), reportUnhandled, session)`.
Its root lifetime owns the controller, navigation, and cancellable work.
`Controller.LastOperation` includes quote delivery and producer quiescence; do not block the UI thread waiting for it.
The local quote service deliberately delays before calculating an estimate.
Nothing is ordered, charged, sent over the network, or saved automatically.
The shared generated sample and literal scenarios are implemented; platform gallery entry points and native navigation/input acceptance are a separate integration gate.

`WorkshopRegistration` adds a local workshop form using email/number purpose hints, a real multiline learning-goals editor, attendance and consent choices, decimal estimates, and read-only notes during review.
Its explicit seat and text limits preserve invalid drafts for correction.
It does not book a place, send email, or take payment.
The shared form requires the new Forms capabilities; its model and recording scenarios do not establish native IME or scaled-font geometry acceptance.

## Application helpers

`UiWorkScope` is explicitly application-owned and should be disposed before its host.
It links producer cancellation and guards UI delivery, so cancelled, superseded, or disposed work cannot apply a late result.
Producers still own their resources and must honor cancellation; cancellation cannot forcibly stop arbitrary work.
Callers must observe returned tasks and handle their failures.
`LatestUiWork` cancels the previous request before starting another and prevents an old result from overwriting a new one.
Neither helper can undo a side effect that already committed.

Generated component `Lifetime` ownership can hold these helpers and supply a cancellation token.
Permanent component retirement cancels its work; an ordinary detach keeps the retained component alive.
Avoid capturing an Activity, native widget, or browser module in work that outlives its owner.

`NavigationStack<TState>` owns logical route entries and optional per-entry resources.
Back/replacement/reset retire entries, cancel their lifetime tokens, and release resources in reverse order.
Cleanup and observer failures are aggregated after the logical transition; they do not silently resurrect retired entries.
The stack is not an automatic browser-history or Android-Back integration.
Applications coordinate native navigation and view changes, including explicit handling of keyed precommit rejection.

`IPlatformServices` distinguishes capability availability from each operation's outcome.
Clipboard and URI operations report Completed, Denied, Unsupported, native UI Cancelled, or Failed.
Reading `OperationResult<T>.Value` for a noncompleted operation throws rather than returning an empty success.
External cancellation tokens cancel the task; they are distinct from native picker/dialog cancellation.
URI launch accepts only absolute HTTP, HTTPS, mailto, and tel targets, rejects HTTP credentials/control characters, and remains subject to native handlers and browser user activation.
Clipboard text can legitimately be empty, but denied or malformed responses must not be converted to empty text.
Real clipboard permission/format behavior remains a separate manual gate; automated protocol tests do not modify the user's clipboard.

`IApplicationStorage` stores small byte documents under validated lowercase keys.
`StoredValue.Exists` distinguishes missing documents from existing empty documents.
`DirectoryApplicationStorage` uses an application-selected private directory, a configurable byte bound, and same-directory replacement; failed or denied replacement preserves the prior document.
`IndexedDbApplicationStorage` uses an application-selected database namespace, bounded real transactions, and explicit cancellation/close handling.
Neither is an encrypted secret store, database abstraction, or synchronization service.
Use application-owned schema/version validation and display invalid persisted data explicitly.
No helper automatically saves user input.

`IFilePicker` is a separate disposable service for opening one native-selected file.
`FileSelectionOptions` requires a positive byte limit.
A completed result transfers a `PickedFile` and its read-only, nonseekable stream to the caller, who must dispose them.
`DisplayName` is untrusted presentation text, not a filesystem path or storage key.
`Length` is advisory; reads enforce the limit even when native metadata is missing or inaccurate.
Reading past the exact boundary throws instead of silently returning a truncated file.
Native dismissal is a Cancelled result; external token cancellation cancels the task and releases any later native result.
Cancellation does not promise to close an already displayed operating-system picker.

Windows uses its native dialog, Android returns a content-URI-backed stream without inventing a filesystem path or taking a persistent grant, and browsers require a preloaded picker module and native user activation.
Browser streams use asynchronous reads on the browser UI thread.
Keep the browser module alive until selected files are disposed.
Save-file selection is explicitly unsupported in this bounded slice.
Picker protocol tests use owned temporary data and synthetic bridges; they do not establish real permission-dialog or provider acceptance.

### Attachment-owned message dialogs

`Host.ShowMessageAsync` accepts a bounded `MessageDialogRequest.Alert` or `Confirm` and returns an `OperationResult<MessageDialogDecision>`.
Confirm defaults to the declining action; native dismissal is distinct from acceptance.
Titles allow 256 UTF-16 code units, messages 8,192, and button captions 80, with well-formed text and single-line titles/captions.
This is a capability-gated message contract, not custom modal content or a popup system.
Query `GetMessageDialogAvailability`; an unsupported backend returns an explicit Unsupported result.

An implementing backend needs explicit authority over the enclosing window, Activity, or document, not merely a borrowed content container.
Only one request may be active per attached host, and originating-host input is blocked until that request closes.
Begin returns its owned request before a posted UI-thread completion; completing inline or during structural mutation is an error.
`Cancel` is an any-thread request, not proof of native dismissal.
A cancellation-transport failure faults the task explicitly; on either calling thread, the native request remains attachment-owned and the one-active guard remains until UI completion or owner teardown can close it.
Terminal disposal runs on the UI thread and must release the native prompt before releasing its ownership guard.
The shared recording fixtures do not establish native prompt rendering, focus trapping/restoration, Escape/Back, or assistive-technology acceptance.

## Packaged asset bytes

The preview compiler package and repository-local `Xui.Portable.targets` accept explicitly declared `XuiAsset` items in the shared application library:

```xml
<ItemGroup>
  <XuiAsset Include="Assets\logo.png" AssetId="images/logo.png" />
</ItemGroup>
```

The build embeds bytes in that library; it does not discover arbitrary asset directories or copy them to a public web URL.
An asset ID is case-sensitive canonical lowercase ASCII with slash-separated segments.
Each segment starts and ends with a letter or digit and may contain dots, hyphens, and underscores.
IDs are limited to 160 characters and a manifest to 4,096 entries.
Missing files, invalid or duplicate IDs, and reserved `Xui.Asset.` embedded-resource aliases fail explicitly.

Open an asset from the explicitly selected shared assembly:

```csharp
var assets = new PackagedAssetManifest(typeof(AppMarker).Assembly);
await using var asset = await assets.OpenReadAsync("images/logo.png", 1_048_576, cancellationToken);
// Consume asset.Content asynchronously; this scope owns and disposes the stream.
```

`AppMarker` is an application-defined type in the assembly declaring the assets.
Each successful open returns an independent, bounded, read-only, nonseekable stream.
The caller owns the returned `OwnedAssetRead`; early disposal and failed opens release the underlying resource.
Known oversized lengths are rejected before exposure, and subsequent reads still enforce the byte bound.
Cancellation is not an empty successful read.
Missing declarations and missing payloads are errors; there is no fallback path, assembly, filesystem extraction, or network fetch.

Descriptor kind and MIME type are inferred only from the ID extension.
They do not prove valid image encoding, licensed font embedding, a supported face, or successful rendering.
This byte-transport slice does not decode images, register fonts, load external URLs, cache decoded pixels, or satisfy those later resource milestones.

### Bounded image resource foundation

The shared image helpers add preflight and ownership, not a rendered image control.
`PackagedImageSource` selects one explicit manifest and canonical asset ID.
`ImageDecodeOptions` bounds encoded input to at most 32 MiB, source dimensions to 16,384 per axis and 16,777,216 pixels, and requested output to at most 1,024 pixels per axis.
Its default 192-by-144 output request is a decode bound, not an element layout size.
Contain planning preserves aspect ratio and never enlarges a smaller source.

Preflight recognizes static PNG and supported baseline/progressive JPEG by their bytes, not the file extension.
Malformed/truncated structures, PNG CRC failures, animated PNG, unsupported JPEG formats, and nonidentity EXIF orientation fail explicitly.
Header validation is not proof that a codec can decode the data.
Backends must validate codec source dimensions, current decode options, and exact output dimensions before publishing a ready image.

`ImageResourceCache` is explicitly owned and bounded.
The default is 8 MiB of encoded buffers, 32 entries, and two in-flight loads; hard configuration limits are 64 MiB, 128 entries, and four loads.
Pinned leases remain valid until disposal and cannot be evicted to pretend a full cache has room.
Budget exhaustion is an explicit error, not an unbounded wait queue.
`ImagePixelBudget` separately reserves owned RGBA output estimates before allocation; its default output budget is 8 MiB.
`ReserveSource` uses a separate budget instance, up to 64 MiB, to admit an owned raw source bitmap using the preflight dimensions.
Every concurrently owned output copy needs its own output reservation.
An uncancellable decoder keeps its source reservation and in-flight permit until decoding settles and the raw bitmap is closed, even after source replacement or detachment.
When a codec cannot inspect metadata without decoding, actual source dimensions are checked after raw decode and before allocating output or publishing Ready.
These reservations are admission estimates: codec scratch buffers, opaque intermediate allocations, browser/native driver allocations, and GPU storage are not covered by that accounting.
These are not claims about a hard total process-memory ceiling.

`ImageRequestLifetime` belongs to one attachment and its UI thread.
Source replacement retires the old request; late decoded resources are disposed instead of being published into a replacement attachment.
Cancellation does not prove that a native codec has stopped.
The backend must retain the owning UI/module context until late cleanup can finish.
Actual native decoders, image peers, accessible image presentation, and their cache/cancellation acceptance remain separate implementation gates.

The managed `Image` element and `ImageWorkbench` now expose the corresponding capability-gated lifecycle.
`SetImage(source, decodeOptions)` publishes source/options atomically; equivalent manifest/asset/options values are a no-op.
The accessible name is alternate text, not a source URL or file path.
`State` is Empty, Loading, Ready, or Error, with generation, validated pixel dimensions, and a bounded error message.
The first native image projection uses a stable, unsized desired viewport of 192 by 144 logical units in every state.
Ordinary authored size, preferred size, axis constraints, and parent allocation take precedence.
Decode quality and decoded pixel dimensions never become implicit logical layout dimensions; rendering contains the original source aspect ratio within the allocated viewport.
A nonnull detached source is Loading in the sense of awaiting attachment, not proof of active work.
Detachment and source replacement invalidate old generations before native cancellation.
Completion must match both the current generation/attachment and the exact requested decode plan; stale results cannot restore an old ready image.
Expected decode failure is observable Error state, while malformed callbacks and infrastructure cleanup failures fail the attachment explicitly.

`StateChanged` is coalesced onto queued UI delivery, never invoked inline from native creation, validation, or source assignment.
Delivery during component construction or keyed reconciliation is rejected.
Native interaction notices are deferred until the complete image operation has committed or rejected.
The current shared tests use real headers and recording peers; they are not proof of native decoding, rendered pixels, or total native/GPU memory bounds.

Backends may report an unrecoverable presentation failure during Loading or after Ready through the optional `IImagePresentationEvents.ImagePresentationFailed(generation, error)`.
Delivery must run on the attachment UI thread after native operations, construction, reconciliation, and viewport staging have unwound.
Stale attachment, peer, and request identities return false.
A current failure detaches the entire attachment through normal resource cleanup and then throws the original error, aggregated with cleanup failures where necessary.
The backend catches and reports that failure at its owned managed asynchronous boundary, never across a native ABI.
Invalid delivery timing is rejected instead of retiring native resources inside an operation.
This is not a second decode completion or a Ready-to-Error transition: ordinary `ImageFailed` remains Loading-only.
Hidden, disabled, and modal input state do not suppress terminal reporting.

### Packaged font foundation

The initial font helpers validate and own encoded resources; they do not register a font or make a control render with it.
`PackagedFontSource` identifies explicit font/license assets, expected hashes, and license provenance.
`FontResourceCache` returns bounded, caller-owned encoded-byte leases and parsed metadata.
The first admission policy is deliberately narrow: one normal 400-weight TrueType face, at most 4 MiB, 64 tables, and 8,192 glyphs, with a 16 MiB/four-face owner budget.
Unsupported collections, variable/color formats, CFF/OTF, and synthesized weights are rejected rather than called ready.
Embedding flags and hashes are checks, not an automated legal verdict.

The approved, unmodified Abel fixture includes its [OFL license](../../assets/fonts/abel/OFL.txt) and [pinned provenance](../../assets/fonts/abel/provenance.json); it is not relicensed under XUI's MIT license.
Explicit application font items can embed the same font/license bytes in Shared and project them into Android assets at build time.
The Android host must import those application-owned items as well; a project reference does not magically propagate them.
There is no runtime filesystem extraction or system font installation in this foundation.

Actual GDI/DirectWrite, Android Typeface, and browser FontFace registration, collision handling, composition-safe control assignment, rendered glyph/metric checks, and registration retirement remain native feature gates.
An API 26 minimum declaration or APK build is not API 26 execution.
Encoded-byte accounting is not a hard limit on native font-engine, glyph-cache, or GPU memory.

The registered-font ownership helpers separately define attachment-scoped resource and binding lifetime.
Disposing a ready resource rejects new bindings while existing binding and measurement pins retain its native registration and encoded bytes.
Bindings validate the resolved normal 400-weight style and composition state before changing native presentation; unsupported synthesized styles fail explicitly.
Partial native assignment or cleanup failure retains identifiable owners for terminal cleanup or retry instead of declaring the font released.
These helpers use fake native tokens in their regression suite and do not add a production `Host.SetFontResource` capability by themselves.

## Native focus and selection

`Host.TryFocus` and `Host.HasFocus` operate on current native peers through optional backend capabilities.
Detached, retired, wrong-host, unsupported, and wrong-thread operations fail explicitly; hidden/disabled/nonfocusable focus requests return false.
`GetSelection` and `SetSelection` address a `TextInput`'s current native UTF-16 range.
Ranges clamp without splitting surrogate pairs and setting a range does not rewrite text or implicitly focus an editor.
A deliberate selection change may affect native editing/composition; unrelated property updates must continue to preserve both.

## Supported language

The root must be `VStack` or `HStack`.
Ordinary children form a fixed tree; only the explicit keyed containers below change their owned children.
All elements accept `ref`, `size`, `preferredSize`, `width`, `height`, and `flex`.
`flex` requires a direct stack parent and cannot depend on state.
References expose the corresponding runtime type.

Stacks accept `spacing` and uniform `padding`.
They do not accept `id`, `enabled`, `visible`, or `help`.
`VStack` produces `Stack` with `Axis.Vertical`.
`HStack` produces `Stack` with `Axis.Horizontal`.

`Text` produces `Label`.
Its positional string supplies both text and accessible name.
`Button` uses the same name rule and accepts `click`.
These controls do not accept an independent accessible-name argument.

`Toggle` accepts `checked` and `change`; its change handler takes `bool`.
`CheckBox` accepts `checkState`, `threeState`, and `change`; its handler takes `Xui.Experimental.Portable.CheckState`.
Both use their positional string as the visible caption and accessible name.
They do not emit button click events.
`Progress` accepts a constructor-time `range`, `currentValue`, and `progressState`.
It is read-only, has no change or click event, and uses its positional string as its accessible name.
The managed `SingleChoice` declaration accepts `items`, `selected`, and `change` with a `ulong` handler argument.
The managed `RangeInput` declaration accepts `range`, `currentValue`, and `change`, `preview`, and `cancel` with `double` arguments.
These two new control families require the capability-gated native projections described below.

`TextInput` accepts `name`, `text`, `change`, `submit`, `captionVisible`, and `placeholder`.
Its positional string supplies the accessible name unless `name` overrides it.
The default text and placeholder are empty.
The default `captionVisible` value is `true`.
Hiding the caption does not remove the accessible name.
The input is single-line.

`ScrollView` requires one child and a positional accessible name.
It provides vertical scrolling, a bounded content width, and an unbounded content height.
It does not provide horizontal scrolling.

`Grid` is a static layout element with `rows` and `columns`.
Its positional string is a diagnostic label, not a separate visible caption or accessible control.
Direct children accept constructor-time `row`, `column`, `rowSpan`, and `columnSpan`.
These default to zero, zero, one, and one; they cannot depend on component state.
`flex` remains a stack-child property, not a grid placement.
Grid does not accept control properties, styles, gap, or padding arguments in this slice.

`Text`, `Button`, `TextInput`, `ScrollView`, `Toggle`, `CheckBox`, and `Progress` also accept `id`, `enabled`, `visible`, and `help`.
They default to enabled and visible, with empty identity and help strings.
`id` supplies the automation identity, not a process-global object key.
The backend must scope external identifiers to its host.
Duplicate IDs do not replace or merge elements.

Events name C# methods.
`click` and `submit` handlers take no arguments.
`change` handlers take one `string` argument.
Parameters, state declarations, and state-driven expressions use the existing language rules.
View expressions cannot call component methods or contain assignments, increments, lambdas, or `await`.

Unsupported nodes, properties, styles, and resources produce source-located `XUI001` errors.
C# type and method errors retain source mapping.
There are no ignored portable properties.

### Forms contracts awaiting native projection

These managed contracts and generator checks are implemented.
They are not yet a claim that every platform adapter can display the controls.
Attaching an unsupported kind or input-purpose capability fails explicitly.

`TextInput` additionally accepts constructor-time `purpose`: `InputPurpose.Normal`, `Email`, `Url`, `Telephone`, or `Number`.
Purpose is advisory native keyboard/autofill metadata, not validation or conversion.
The text remains an ordinary string; number purpose does not parse decimals and email purpose does not establish address validity.
A purpose expression cannot depend on component state.

`MultilineText` accepts `text`, `change`, `readOnly`, and constructor-time `maximumLength`.
Its change handler receives a string with CRLF and CR converted to LF, preserving other characters.
The default maximum is 65,536 UTF-16 code units; supported limits are 1 through 1,048,576.
Limits apply after newline canonicalization.
NUL, unpaired surrogates, and oversized text are rejected explicitly.
Read-only prevents native user changes but not silent programmatic setters.
User change delivery must not rewrite the native editor or normalize an in-progress composition.

`PasswordInput` deliberately has no `Text` property or authored plaintext binding.
Its `change` handler has no argument.
The password belongs to the current native attachment, not the retained model or a saved session.
`SetPassword(ReadOnlySpan<char>)`, `WithPassword(PasswordReceiver)`, and `Length` require a live attachment.
The receiver runs synchronously and must not retain the supplied span.
The default maximum is 256 UTF-16 code units; supported limits are 1 through 4,096.
NUL, line breaks, unpaired surrogates, and oversized values are rejected.
Programmatic writes are silent; hide and disable do not clear an attached value.
Subtree removal, detachment, and terminal disposal clear native secrets before unmount or peer release, including failure cleanup.
Reattachment starts empty and never restores a managed password snapshot.
This is an ownership/privacy contract, not a secure vault or a guarantee that an operating system never copies input.

`FormsWorkbench` and its literal scenarios exercise these boundaries without collecting credentials.
Physical IME, password manager, autofill, and assistive-technology acceptance remain native release gates.

### Presentation contracts awaiting native projection

`Control.Typography` is nullable: null preserves the host's inherited native font.
Explicit typography is currently limited to `Label`, `Button`, `TextInput`, `Toggle`, and `CheckBox`.
Other targets reject it rather than styling a diagnostic name as visible text.
The `.xui` equivalents are `textRole`, `fontSize`, and `fontWeight`.
`TextRole.Body` uses 14 logical units and normal weight, `Caption` uses 12 and normal weight, and `Title` uses 24 and bold weight.
Explicit sizes range from 8 through 32; weights are 400 or 700.
Native accessibility font scaling still applies.
This typography slice does not select a font family or package/register a font.
Label wrapping/truncation has a separate capability contract below.

`Host.Theme` is also nullable.
Null inherits the native host; explicit `ThemeSettings.System` follows its operating-system scheme.
Light and dark modes can carry immutable semantic foreground, background, and accent overrides, each an opaque RGB24 light/dark pair.
Adapters must validate support before committing a theme or typography change.
Native high-contrast/forced-color and reduced-motion behavior takes precedence over authored resources.
A Windows adapter borrowing a content surface must not change unrelated window content: explicit exclusive window-theme authority and baseline restoration are required for window-wide changes.
Mode-only support must not silently accept unsupported color resources.
These are frozen managed contracts with recording coverage; platform projection, native state styling, geometry, and accessibility evidence are still required.

### Explicit label wrapping and overflow

`Label.TextLayout` and the `Text(..., textLayout: ...)` argument accept a nullable `LabelTextLayout`.
Null restores inherited native defaults.
`SingleLine(TextOverflow.Clip)` and `SingleLine(TextOverflow.CharacterEllipsis)` use native clipping or ellipsis without rewriting the source or accessible text.
Explicit single-line text rejects CR, LF, NEL, line separator, and paragraph separator before either the layout descriptor or text changes.
Other whitespace is not collapsed or normalized.
`Wrap(maximumLines)` preserves hard breaks and uses native word/grapheme line breaking; zero is uncapped and 1 through 32 clips after that many lines.
This slice does not promise multiline ellipsis or identical line breaks between text engines.

Only labels support this descriptor.
Button captions and native editors need separate geometry/input contracts and reject the argument.
`ITextLayoutPeer` validates support before attachment or mutation.
An old backend must not silently ignore clipping, wrapping, or reset.
The shared workbench and generator/runtime tests are implemented; each platform still needs actual native measurement, font-scale, rendering, and full-accessible-text acceptance.

### Bounded retained reveal

The portable `Reveal` owns exactly one retained child and uses `RevealMotion` with Bottom or Right expansion.
Duration is 0 through 400 milliseconds, defaulting to zero; initial attachment is settled at the requested open state.
`TrySetState(open, motion)` commits the pair atomically, returning false only when closing would hide a focused or composing descendant.
Unsupported content or motion is an explicit error, not a false focus veto.
Move focus deliberately and finish composition before closing; the framework does not cancel an editor's composition.
`Host.GetRevealPresentation` reads actual native progress without starting a managed animation clock.

The child retains its full finite extent on the animation axis; clipping is bounded by both parent allocation and progress.
Fixed parent slots can retain unused space, and authored stack spacing remains around a closed zero-length slot.
Size the child rather than the Reveal: outer fixed/preferred sizes, axis constraints, and nonzero flex are rejected.
Changing motion settles the old logical target before applying the new pair; reversing with unchanged motion starts from current progress.
Closed content becomes input- and accessibility-inert immediately, even while exit pixels remain.
Reduced motion, high contrast, owner invisibility, and disposal must settle or stop native motion; settled content has no idle animation clock.

This optional peer capability is distinct from the broader legacy Windows animation API.
The [Windows opt-in](animations.md#bounded-portable-windows-opt-in) explicitly excludes RichEdit-backed multiline/rich text, FileList, native plugin hosts, and leased virtual viewports.
Its complete-tree and prospective-subtree checks run before attachment or model commit.
Do not infer all-platform motion or application-drawer acceptance from shared geometry tests or one qualified native backend.

### Localized application strings

The localization workbench uses ordinary `.resx` files, `ResourceManager`, and an immutable string snapshot, not a new framework-wide localization service.
Its shared item import is `SharedDemo\Localization\LocalizationWorkbench.items.props`.
The import includes one `.xui` component, its C# catalog, and English/German/Arabic resource bundles in the shared assembly, independently of the host assembly name.
Explicit logical resource names and `WithCulture=false` keep all three languages available without asynchronous satellite activation.

`LocalizationWorkbench.SelectLanguage` accepts English, German, and Arabic cultures and their regional variants.
Formatting uses the explicitly selected `CultureInfo`; neither ambient current culture is changed.
Regional requests select their supported language bundle.
An intentionally missing translated key falls back to English, but a missing required language bundle is a deployment error, not successful English translation.
Unknown languages and missing required keys fail explicitly.

Language changes update captions, placeholders, help, and other application text without translating or normalizing a user draft.
The editor, automation IDs, and keyed identities remain stable.
The sample's recorded selection-retention checks do not establish physical IME acceptance.
Arabic strings alone do not implement RTL layout, bidirectional hit testing, native glyph coverage, or screen-reader pronunciation.
The browser uses the same catalog and live language actions, without a private Blazor loader flag, page reload, or dynamic assembly-loading workaround.
Trimmed distribution, actual browser language actions, and native input retention must still be verified on the consumer host.
Applications choosing separate satellite assemblies need their own supported activation and deployment contract; including ICU data alone does not load every translation.

### Stable single selection and interactive ranges

The shared runtime, generator, and recording fixtures implement these contracts.
Native selection, pointer capture, cancellation, and accessibility acceptance are still pending; older adapters reject the new kinds explicitly.

`SingleChoice` is a compact, noneditable single-selection control.
It uses `Choice(ulong Id, string Text, bool Enabled = true)` entries, not independently authored row elements.
There may be zero through 4,096 entries.
IDs must be unique, nonzero, and at most `long.MaxValue - 100`.
Native positions are only an adapter detail; a reordered entry retains its identity.
Interop must preserve the exact ID, using canonical decimal strings where a numeric bridge would lose precision.

`SetItems(items, selected)` defensively copies and validates the complete snapshot before either field changes.
An explicit selection must identify an enabled item.
An omitted selection preserves a surviving enabled current selection, otherwise chooses the first enabled item, otherwise becomes absent.
Empty and all-disabled sources have no selection.
`SetSelected(id)` is silent and requires an enabled existing ID; it is not a clear-selection API.
Programmatic item updates, fallback, and property selection do not emit `Changed`.
Native selection records the final valid ID before the authored handler without writing it back into the widget.
An Android adapter may show a nonselectable `No selection` placeholder, but must not present a disabled item as the selected logical entry.

`ISingleChoiceElementPeer.ValidateChoices` preflights representation before native mutation.
Rejection preserves the previous model and attachment.
Failure after native update begins detaches while retaining the committed snapshot.
The generator binds `items` and `selected` together; `selected` requires an `items` snapshot.
An ordinary tuple or record state is the recommended way to publish both atomically.

`RangeInput` uses the same validated `NumericRange` vocabulary as progress.
It has a committed `Value` and a nonnullable effective `PreviewValue`.
Preview does not commit: `Previewed` reports a native preview change, `Changed` reports a changed committed value, and `Canceled` reports the committed value restored by user cancellation.
Callbacks observe the new model first.
`PreviewValue` does not expose whether a drag is active.
No fabricated terminal change is added when a native drag returns to its starting value and commits unchanged.

`SetValue` accepts any finite in-range value, including one not aligned to `SmallStep`.
Even setting the same committed value clears an outstanding preview silently.
`SetRange` validates and atomically clamps the committed value, clearing preview silently.
Disable, hide, ancestor availability changes, detachment, and retirement also clear preview without an authored cancellation event.
Adapters must clear native previews before programmatic availability changes and suppress delayed cancel/change events from an old revision.
Unrelated changes must not replace or write text into retained sibling editors.

`IRangeElementPeer.ValidateRange` preflights native representation before model mutation.
An integer-based native track may quantize its visual position, but semantic values and keyboard steps remain in authored double units.
An adapter must reject unsupported representation instead of rounding the committed value.
In particular, assigning an HTML step that sanitizes off-step programmatic values is not equivalent.
`RangeMath.Step` provides bounded small/page/Home/End arithmetic.
`RangeMath.Snap` provides pointer-fraction clamping and step snapping with midpoint rounding away from zero.

The additive event sinks are `ISelectionControlEvents.SelectionChanged(ulong)` and
`IRangeControlEvents.RangePreviewed(double)`, `RangeChanged(double)`, and `RangeCanceled(double)`.
They retain UI-thread, attachment-generation, reentrancy, and availability guards.
Cancellation can clear a now-disabled range's preview without creating a user-cancel notification for programmatic disable.
This slice has no editable numeric text mode, vertical orientation, reversal, or automatic typography support.
The default Windows generator retains its existing range surface and rejects the new portable single-choice and preview/cancel declarations.

The [shared workbench](../../bindings/dotnet/Experimental/SharedDemo/ChoiceRangeWorkbench.xui),
[literal scenarios](../../bindings/dotnet/Experimental/SharedDemo/ChoiceRangeScenarios.json), and
[driver contract](../../bindings/dotnet/Experimental/SharedDemo/ChoiceRangeScenarioRunner.cs)
cover stable IDs, reorder, absence, disabled choices, preview/commit/cancel, off-step values, keyboard boundaries, and silent reset.
Native fixtures must read actual item, selection, and range state rather than use the production model as the expected-value oracle.

### Binary choices, mixed choices, and progress

These are bounded portable counterparts of the existing Windows controls, not new aliases for buttons or text.
`Toggle` is a native binary checkbox with `Checked` and `Changed(bool)`.
`SetChecked` and the `Checked` setter are silent.
This slice does not include `ToggleSwitch` or `ToggleButton`.

`CheckBox.State` uses `Unchecked`, `Checked`, and `Indeterminate`.
`ThreeState` defaults to `false`.
With three-state input enabled, activation cycles unchecked, checked, indeterminate, unchecked.
With it disabled, checked or indeterminate activates to unchecked; unchecked activates to checked.
Programmatic `SetState` accepts all three values regardless of the cycle mode.
Changing `ThreeState` does not normalize an existing mixed value.
Both setters are silent, and invalid enum values fail without changing the model.
Adapters must display a distinct mixed mark and expose mixed semantics, not collapse it to a boolean.
Android's API 30+ state description is `Mixed`; its older-API fallback retains the accessible name and appends `, Mixed`.
This fallback is an explicit implementation boundary, not certification of all TalkBack versions.

`Progress` defaults to `NumericRange(0, 100)`, value zero, and `ProgressState.Determinate`.
The portable enum supports only `Determinate` and `Indeterminate` in this slice.
Paused, error, unknown, ring presentation, orientation, and duration are not portable promises.
Unsupported authored nodes/properties are diagnostics; invalid runtime enum values throw.
The generator qualifies its own enum/range types for the selected profile without rewriting authored C# types.

`NumericRange` carries `Minimum`, `Maximum`, `SmallStep`, and `LargeStep`, with step defaults of one and ten.
Bounds must be finite and strictly increasing, their difference finite, and both steps positive and finite.
Progress uses the bounds; step metadata does not create interactive progress actions.
`SetValue` rejects nonfinite or out-of-range values.
`SetRange` atomically retains the new range and clamps the previous value into it before notifying the peer once with `ElementProperty.Range`.
The adapter reads both range and value during that callback.
All progress setters are silent.
`range` in `.xui` is constructor-time; mutable range changes use the typed `Progress` API.

The visual fraction is `(Value - Minimum) / (Maximum - Minimum)`.
Native integer-based widgets may quantize that fraction, but accessible bounds/current values use the authored units rather than normalized widget ticks.
For example, the settings fixture's range is -10 through 30, with values 2.50 and 2.75.
Indeterminate progress omits a determinate current-value accessibility value while retaining its stored logical value for a later return to determinate.
Native indeterminate motion must honor attachment, effective visibility/enabled state, and platform reduced-motion behavior.
A disabled progress control is still read-only; it does not become an input control.
Native geometry, accessibility and animation assertions are adapter acceptance gates, not inferred from the managed value.

Adapters receive the additive `IValueControlEvents : IControlEvents` sink with
`bool ToggleChanged(bool value)` and `bool CheckChanged(CheckState value)`.
The host's sink always implements it.
An accepted final native value is recorded before the authored `Changed` handler runs, without writing the value back to the same native widget.
Unchanged values do not fire another handler.
Wrong control/event combinations throw while active; disabled, hidden, detached, or retired sinks return `false`.
Setters suppress synchronous echoes; adapters must also suppress delayed events caused by programmatic updates and stale deferred native values.
Three-state activation remains the backend's native input responsibility, not a fake call to an authored handler.

The [settings showcase](../../bindings/dotnet/Experimental/SharedDemo/SettingsShowcase.xui) uses these primitives with a retained draft editor and a local progress preview.
It performs no background work or network operation.
Its [shared literal scenarios](../../bindings/dotnet/Experimental/SharedDemo/SettingsScenarios.json) and
[driver contract](../../bindings/dotnet/Experimental/SharedDemo/SettingsScenarioRunner.cs)
check native checked/mixed states, disabled boundaries, authored progress units, indeterminate semantics, and silent reset.
The driver reads native widgets or accessibility values, not the production model as its expected-value oracle.
The showcase requires portable generation on each host, including an opt-in portable Windows adapter; it is not a legacy Windows-profile sample.

## Reusable components and keyed children

Generated portable components implement `IPortableComponent`, whose `Root` is an `Element`.
The root of each generated component remains a `VStack` or `HStack`.
Use `Content(new Child(window, argument).Root)` to construct a reusable component inside its parent's construction scope.
`Content` accepts `ref`, sizing, and fixed `flex`, but no control properties or styles.
Its element expression is evaluated once and cannot depend on mutable component state.
Constructor parameters provide typed values and ordinary C# delegates; parameters are not writable bindings.
Reusable child state and typed refs remain local to that child.

Nested `BeginBuild` scopes must close in reverse order.
Every element created in a scope must belong to its completed root; foreign, already-parented, orphaned, and previously constructed elements are rejected.
Failed nested construction releases only its candidate elements.
An outer construction failure also releases completed nested candidates.
A factory cannot mutate the existing live tree while it constructs a candidate.
The host still owns exactly one completed root, not several independently attached components.

`KeyedVStack` and `KeyedHStack` produce `KeyedStack` with the corresponding axis.
They accept a positional `KeyedItem[]` state or constructor parameter, spacing, padding, sizing, `ref`, and fixed placement.
Like ordinary stacks, they have no `id`, `visible`, `enabled`, or `help`.
They have no authored child block; their child components come from typed descriptors.
The positional expression must directly name the state or parameter.
One descriptor state may drive only one keyed container so precommit rejection cannot partially update several containers.
The default Windows generator profile rejects keyed nodes with `XUI001`; a portable Windows adapter must explicitly support the mutable peer contract before it can display them.

```text
component ListPage {
    state global::Xui.Experimental.Portable.KeyedItem[] Rows = [];
    view {
        VStack() {
            KeyedVStack(Rows, spacing: 8, ref: List);
        }
    }
    code csharp {
        public void Show(string key, RowModel model) {
            Rows = [
                global::Xui.Experimental.Portable.KeyedItem.Create(
                    key,
                    host => new Row(host, key),
                    row => row.Model = model)
            ];
        }
    }
}
```

`Row` and `RowModel` in this example are application-defined.
`Row` is another portable generated component; its constructor takes the key parameter and its `Model` is a generated state property.
Build descriptor arrays in ordinary C# methods, not lambdas embedded in view bindings.
Publish a fresh array for each update; mutating a published array in place does not trigger generation bindings.
The runtime snapshots the array and compares keys with ordinal, case-sensitive equality.
Keys must be nonempty and cannot contain NUL.
Keys are scoped to one container, independent of automation IDs.

`KeyedItem.Create<T>(key, create, update)` retains a component while both its key and its declared component type `T` survive.
Its factory runs only for a new key/type.
The optional typed update callback runs for both new and surviving components, after structural commit.
Use it to update explicit row data while preserving other row-local state.
It may update properties but cannot recursively reconcile, attach, detach, or dispose the host.
Do not hide unrelated application side effects inside construction or update callbacks.

Reordering a surviving key keeps its component, elements, peers, and editor identity.
Changing its component type replaces the subtree.
Removing and later readding a key constructs a new component; it does not resurrect old drafts or callbacks.
Moving a key between containers is removal plus creation, not a cross-parent move.
An empty/one-item descriptor array supplies conditional owned content; this is distinct from hiding a retained control.
Ordinary `Stack.Add` still cannot modify a completed tree, and `KeyedStack.Add` is rejected.
Inline `if`/`foreach` syntax, arbitrary templates, virtualization, and automatic subscription/task ownership are outside this slice.

`KeyedUpdateException.ModelCommitted` distinguishes two failure stages:

- `false`: invalid descriptors, unsafe moves, or factory/ownership failures leave the previous tree and attachment intact. Newly staged components are released, and a generated descriptor-state setter restores its previous value before propagating the error. Other bindings on that state have not run.
- `true`: the new child model has committed. Native creation/mutation/disposal or typed-update failure detaches the backend and preserves that model and the new descriptor state. No implicit rollback is attempted. Reattachment uses the retained tree; it does not rerun the failed factory or update callback.

The exception retains the original cause, including cleanup failures.
Other ordinary state bindings remain synchronous and nontransactional.
If an application maintains a separate model, publish descriptors before committing that model, and handle the explicit failure stage.
Removed components' generated state getters/setters and element access reject further use with `ObjectDisposedException`.
External async work or subscriptions remain application-owned unless explicitly registered with the component lifetime below.
Retirement never discovers or cancels arbitrary application tasks.

The [generated mutation fixtures](../../bindings/dotnet/Experimental/Xui.Portable.Tests/Fixtures/MutationBoard.xui) and
[literal mutation corpus](../../bindings/dotnet/Experimental/Xui.Portable.Tests/Fixtures/MutationScenarios.json)
cover reusable content, retained drafts/counters, reordering, insertion/removal, reset, and conditional lifetime.
Recording tests establish runtime invariants, not platform focus, composition, native resource release, or accessibility.

## Retained pages and linked navigation

The managed `PageView`, `TabStrip`, and `NavigationView` contracts are implemented behind explicit backend capabilities.
They are not wrappers claiming that a row of buttons is a native tab strip.
The existing Windows-only page control is not automatically a qualified retained portable page host.

`PageItem.Create<T>(id, title, create, update, enabled)` describes one retained component with a stable nonzero integer ID.
A surviving ID and component type retain the page, its state, and native editors; removal or a type change retires them.
`PageView.SetPages` atomically publishes the descriptor/header/selection snapshot.
An omitted selection preserves an enabled survivor, otherwise selects the first enabled page, otherwise becomes absent.
Duplicate IDs, invalid ownership, and failed construction are rejected with the existing precommit/postcommit failure distinction.
Hidden and inactive pages remain owned but cannot accept input or appear as active accessible content.
`PageView.Visible` hides the entire pane without discarding its selected ID or page components.
Closing or switching away from a focused/composing descendant requires the backend's explicit preflight policy, not an implicit editing cancellation.

Selectors bind once to a typed `PageView`; they do not maintain an independent selected-page model.
In `.xui`, `pages` names that page-view ref, with connections established after the native peers exist and before exposure.
`change` reports committed selection, `activate` is distinct from selection, and a tab close request does not automatically remove a page.
Application code owns navigation policy, draft retention, and any focus movement following activation.
Native optimistic selection must return to the previous header state if the common preflight rejects the change.
Unsupported disabled-tab or retained-page capabilities must fail explicitly rather than silently lose editor identity.

The shared `WorkspaceStudio` exercises this graph with a 10,000-record virtual catalog, at most sixteen retained open pages, responsive panes, retained closed-document drafts, and cancellable local analysis.
Its versioned session stores draft changes and logical identities, not native handles, running tasks, or the entire generated catalog.
Its shared scenarios do not establish native navigation/tab/accessibility completion on all three platforms.
An Operations dashboard occupies a real page in the same tab host rather than duplicating the navigation shell.
It scans a captured local catalog/draft snapshot for category, word, checklist, open, and modified counts, and opens existing document tabs from its bounded result list.
It does not display invented cloud telemetry or automatically refresh stale results.
Closing the page cancels its owned work; reports are not serialized.
The newer session format reads the older document-only format and rejects invalid tab kinds/identities.

The optional `WorkspaceStudio.Create(..., enableOperationsDrawer: true)` adds a real retained Reveal around the Operations scan controls.
It is off by default: the ordinary workspace constructs no Reveal and requires no motion capability.
Opt-in is chosen at construction, is not serialized, and requires a qualified backend rather than silently falling back.
The drawer requests 180 ms Bottom motion, starts open, and leaves the multiline document editor and virtual catalog outside the reveal subtree.
Native reduced-motion and high-contrast policies may settle it immediately.
Busy scans cannot be hidden, preserving access to Cancel; native focus/composition vetoes leave the drawer open and show feedback without moving focus or scheduling an automatic retry.
Returning to a closed Operations tab focuses its external toggle without reopening it.
Open-tab activation selects the scope input, the enabled Cancel action while busy, or the tab strip while cancellation leaves no enabled content target.
Existing controls, results, and drafts remain retained across close/reopen, and reattachment restores the logical state without replaying an opening animation.

## Responsive allocation

`WidthBreakpoints` is an immutable application policy in logical layout units.
The default modes are Compact below 720, Medium from 720 through values below 1,120, and Expanded from 1,120.
Applications may choose other validated positive, increasing thresholds.
Zero is a valid observed allocation; invalid or nonfinite dimensions are rejected.

After attachment, `Host.ObserveViewport` subscribes to the actual available rectangle used to lay out that host's root.
It is not the outer window size, a virtual scroll extent, or an intrinsic measurement probe.
The backend supplies initial metadata synchronously; the host delivers authored notifications through one pending queued callback containing the latest allocation.
That is not a guarantee of one callback per operating-system frame.
The attachment retires the subscription before unmount and rejects stale notifications.
Unsupported backends fail explicitly; there is no polling or timer fallback.

A responsive layout should keep its editor tree and identity stable while updating grid tracks and explicit pane visibility.
A zero-width column alone does not remove its contents from input or accessibility.
The sample defers a layout change when native editing policy prevents hiding a pane.
Breakpoint changes are immediate in this slice; they do not imply animated grid resizing.

Workspace Studio also reacts to height-only allocation changes.
Below 640 logical units of available host height, it reduces descriptive chrome while retaining the same title/body editors, navigation, tabs, validation, and accessible cancellation actions.
The threshold is an application policy, not an orientation check or a promise that every font scale fits every viewport.
Hiding a focused secondary command or a page whose native preflight rejects hiding defers the layout with visible feedback and an explicit retry.
The virtual catalog keeps its own bounded viewport; the whole workspace is not placed in an unbounded scroll container.

## Virtualized list integration contract

The fixed-pitch `VirtualizationController` and generated `VirtualList` sample are implemented.
Native viewport leases and their actual geometry, input, lifetime, and performance acceptance are still being integrated.
This section defines that opt-in boundary; it does not advertise ordinary `ScrollView` as virtualized.

The sample uses 128-logical-unit rows and two overscan rows on each side.
Actual native measurements must prove that each row fits the declared pitch at the selected font scale and viewport width.
Variable-height rows are not supported.
The backend exposes the full logical extent without creating a native child for each item or materializing a giant native editor surface.
It validates committed coverage using actual mounted row metadata and rectangles, not an automation-ID naming convention.

Keys identify source items independently of recycled presentation and selection.
Virtualized keys are ordinal, nonempty, well-formed UTF-16 strings of at most 4,096 code units, without NUL.
That additional bound does not change ordinary keyed-component keys.
Source versions distinguish pending filters, reorders, and removals from the committed projection.
An epoch reservation holds the native visible viewport until the prepared rows have valid geometry.
Preparation may temporarily retain the union of the old and new visible windows plus protected editors.
After commit, old-only unprotected rows are pruned.
Native publication failure detaches instead of claiming a rollback or exposing a blank fallback.

Focused or composing editors are protected from recycling.
Unrelated changes do not rewrite their text, recreate their peers, or erase a saved unfocused caret.
A request that cannot preserve ongoing composition remains held until the native editing boundary permits it.
The initial navigation contract combines native Tab order for realized rows with explicit First, Previous, Next, Last, and Enter navigation.
Mounted rows expose list-item position/count metadata.
Continuous offscreen Tab, accessibility realization of an arbitrary offscreen item, complete selection patterns, and screen-reader certification remain separate gates.

Use `VirtualList.CreateForViewport` for an initially empty presentation.
For same-host reattachment, the required sequence is:

1. Capture current editing state and detach the host.
2. Call `PrepareForViewportAttachment` while detached, clearing realized rows and gap elements while retaining source data, drafts, selection intent, and offset.
3. Attach the new backend with no realized row editors, begin its virtual viewport lease, and call `AttachViewport`.
4. Restore the offset through the lease and let a validated request realize and publish the new viewport.

Calling preparation after native attachment is too late: retained gap elements could already have materialized an oversized native surface.
The sample rejects reuse of an uncleared committed presentation.
Lease disposal and obsolete epochs must reject late callbacks.
The framework does not promise an atomic operating-system accessibility snapshot across every intermediate native event.

Settled completion is an additional capability: `ISettledVirtualViewportLease.FlushCommitted(long expectedEpoch)`.
The host exposes that interface only when the native lease implements it.
The shared sample requires the capability up front rather than treating a posted callback as a native layout barrier.
After committing and pruning, it updates its status/navigation feedback, flushes the exact committed epoch, and then raises `ViewportCommitted`.
Flush requires the current committed epoch with no active staging reservation.
It completes the pending native geometry and retirement work for that commit, without processing newer requested intent or invoking authored callbacks inline.
Failure detaches the attachment explicitly; it does not report a successful completion event.
This is not a promise of completed compositor presentation, operating-system event delivery, or an atomic accessibility snapshot.

## Values and layout

Lengths use logical units: Windows DIPs, Android dp, and CSS pixels.
Spacing, padding, flex weights, and size dimensions must be finite and nonnegative.
Invalid values throw before the corresponding element property changes.
Strings cannot be null or contain NUL.
Empty strings and Unicode text are supported.

The backend owns native measurement and arrangement.
A stack places children in authored order along its axis.
Padding applies to all four edges.
Spacing occurs between visible children, not outside the first and last child.
An invisible control occupies no layout space and accepts no input.

Without explicit sizing, controls use their native content size.
`preferredSize` supplies a desired size, including the stack padding.
`size` constrains both dimensions to the requested size, within the parent allocation.
`size` takes precedence over `preferredSize` when both exist.
No element forces overflow from a smaller parent allocation.

Non-flex children use their desired main-axis size first.
Positive flex weights divide the remaining main-axis space proportionally, after padding and spacing.
Cross-axis children stretch within their allocation unless `size` constrains that dimension.
In an unbounded main axis, flex children use their desired size.
Native fonts, control chrome, and text measurement can differ between backends.
This contract does not promise pixel parity with the Windows engine.
For captioned text inputs, prefer native content sizing rather than a small fixed or preferred height.
The element includes both the caption and editor, including native padding and scaled fonts.
The greeting and order samples leave their input sizes unset so each platform can measure the complete control.

### Independent axis constraints

Per-axis constraints are opt-in; existing `size` and `preferredSize` retain their two-axis semantics.
`AxisConstraints` contains nullable `Length`, nonnegative `Minimum` (default zero), and nullable `Maximum`.
`AxisConstraints.Auto` means natural content sizing with normal parent stretch.
`AxisConstraints.Fixed(length)` and an implicit conversion from `float` specify one fixed axis.
Limits must be finite and nonnegative, maximum cannot be below minimum, and a supplied fixed length must be inside both limits.
An absent maximum means no authored upper bound.
Invalid values are rejected before changing either axis.

```text
TextInput("Name", width: 280,
    height: global::Xui.Experimental.Portable.AxisConstraints.Auto);
```

`Element.WidthConstraints` and `HeightConstraints` are nullable, read-only snapshots.
Their default `null` means inherit the corresponding existing fixed/preferred dimension.
An explicit Auto value instead selects natural sizing on only that axis, ignoring its legacy fixed/preferred hint.
It does not clear the stored legacy values or change the other axis.
Setting the axis back to `null` restores its most recent legacy semantics.
Legacy size setters can still update those stored values while an override is active.

`SetWidth` preserves the current height override, and `SetHeight` preserves the current width override.
`SetConstraints(width, height)` validates and updates both together with one `ElementProperty.Constraints` notification.
If only `width` is authored, generation calls `SetWidth`; a separately assigned height is not reset.
If both arguments are authored, their bindings update as one tuple.
The default Windows profile rejects these portable arguments until its own public authoring contract exposes them.
Portable Windows requires the additive native axis-override support, not a min/max approximation that loses legacy measurement flags.

The parent allocation always wins over fixed sizes and minimum limits.
On a stretched axis, Auto accepts the parent allocation, capped by its maximum.
It does not turn ordinary cross-axis stretch into intrinsic-only alignment.
A capped flex child does not redistribute its unused part of a slot to siblings.
On an unbounded axis, Auto and flex content use their natural desired extent instead of filling an infinite share.
Fixed width must constrain native measurement before calculating a captioned input's natural height.
Minimum-induced width changes can require a further intrinsic-height measurement at the resolved width.
Constraint updates must retain the same editor, selection, composition, and native event bindings.

`IConstrainedElementPeer : IElementPeer` is an explicit backend capability marker.
It promises the complete per-axis semantics on that peer, including null restoration and parent precedence.
Attachment rejects configured constraints on an unmarked peer and releases partial native resources.
Setting an override on an already attached unmarked peer fails before the element model changes; it does not silently ignore the override or detach a working fixed-tree attachment.
Once a supported native update starts, failure follows normal detach-and-retain-model semantics.
Generated axis-binding caches follow that committed model, so returning to a previous axis value after failure does not skip recovery.

#### Shared measurement helpers

`MeasureConstraint` combines `MeasureMode` and a finite nonnegative size.
`Unspecified` has size zero, explicitly representing an unbounded offer; infinity is not serialized as a layout input.
`AtMost(size)` and `Exactly(size)` describe bounded offers.
The optional third `UnboundedContext` field preserves intrinsic-layout context alongside a finite clipping offer; existing two-argument calls remain valid.
`IsUnbounded` is true for an unspecified offer or an explicitly preserved context.
An Auto maximum can produce a finite `AtMost` offer without manufacturing a new flex-fill budget.
An explicit axis length or inherited legacy fixed/preferred size ends that intrinsic context.
Explicit Auto ignores those legacy hints only on its own axis.
`LayoutMath` works in the caller's units, so Android can use native pixel measurements without converting intrinsic text sizes twice.
Authored constraints must first be converted to the same units as the native offer.

| Offer | Explicit Auto behavior |
| --- | --- |
| `Exactly` | Use the offered allocation, capped by an authored maximum; a minimum never forces overflow |
| `AtMost` | Use measured intrinsic content, apply minimum/maximum, then clip to the offered bound |
| `Unspecified` | Use intrinsic content and minimum/maximum without stretching into unbounded space |

`ConstrainMeasure` returns the native content offer before measurement.
A fixed length returns an exact offer clipped to the parent; an explicit maximum bounds otherwise unbounded measurement.
`MeasureAxis` resolves a measured intrinsic extent against that offer and the axis constraints.
`ArrangeAxis` applies fixed/maximum caps to an actual allocation.
When an axis override is null, the helpers retain legacy fixed precedence and the preferred-size hint for non-exact offers.

`AllocateStack` takes an offer, spacing, visible-child desired extents, and flex weights.
Non-flex children consume the bounded remaining main-axis budget in authored order before flex children share what remains.
In an unbounded offer, flex children use their desired extents.
With preserved intrinsic context and a finite offer, flex children still use natural extents, clipped to the actual budget instead of growing to fill it.
The result contains a finite container extent and read-only `(Offset, Length)` slots.
Offsets and allocations never exceed the parent budget, even if gaps or child minima are larger than it.
Hidden children must be excluded before calling the helper.
An unbounded extent beyond finite float coordinates throws rather than manufacturing infinity.
Native rounding must keep device-pixel placement inside the offered budget.

The [axis showcase](../../bindings/dotnet/Experimental/Xui.Portable.Tests/Fixtures/AxisSizingShowcase.xui) exercises natural caption height, legacy override/restoration, independent single-axis bindings, competing minima, capped flex, and unbounded content.
The [literal math corpus](../../bindings/dotnet/Experimental/Xui.Portable.Tests/Fixtures/AxisLayoutScenarios.json) defines independent expected offers, sizes, offsets, and allocations.
These checks establish numeric policy; actual native caption geometry at scaled fonts and narrow parent allocations remains an adapter acceptance gate.
CSS min/max properties alone do not establish this contract when remaining flex allocation is smaller than an authored minimum.

### Static Grid layout

`Host.Grid(name)` creates `Grid`, a layout `Element`, not a mutable `Stack`.
Generated component roots remain `VStack` or `HStack`; a Grid is nested inside that root.
`GridTrack` and `TrackSizing` reuse the existing Windows vocabulary:

| Sizing | Meaning |
| --- | --- |
| `Fixed` | The supplied value, clamped to the track's minimum and maximum |
| `Automatic` | Intrinsic child demand, subject to limits |
| `Star` | Positive-weight share of bounded leftover space; intrinsic sizing when unbounded |

Each track carries `Value`, `Minimum`, and `Maximum`, with defaults one, zero, and `float.MaxValue`.
Values and limits must be finite and nonnegative, minimum cannot exceed maximum, and star weight must be positive.
An automatic track's value is unused, matching the existing track vocabulary; it is not an extra size hint.
Each axis requires one through 256 tracks.
The initial grid has one star track on each axis.

`Grid.SetTracks(rows, columns)` validates both axes and every existing cell before changing either snapshot.
It defensively copies the tracks and emits one `ElementProperty.Tracks` update.
`Rows` and `Columns` expose read-only snapshots.
Equal track values are silent; caller mutation of its original arrays cannot change the grid.
Native update failure detaches and retains the committed track model, including the generated binding cache needed to restore an earlier snapshot.

`Grid.Add(child, row, column, rowSpan, columnSpan)` only runs during construction.
Indices and positive spans must fit within the tracks.
The child's read-only `Cell` is a `GridPlacement`; non-grid children have no cell.
Invalid placement does not partially adopt a child.
Cells may overlap, preserving authored child order rather than inventing selection or z-order APIs.
Track updates can resize cells but cannot invalidate existing placements.

Grid children and placements do not change after construction.
A normal keyed stack inside a grid cell may reconcile its own children; this does not mutate Grid's cell metadata.
The mutable-stack peer protocol is not permission to insert, remove, or move direct Grid children.
Native implementations that restrict mutation to exact Stack types must keep that restriction.
There is no keyed Grid, virtualized Grid, or dynamic placement promise.

#### Shared track and cell arithmetic

`GridLayoutMath.ResolveTracks` accepts immutable track descriptions, intrinsic span demands, and a `MeasureConstraint`.
`GridLayoutMath.Measure` additionally accepts fixed placements and a callback that measures native children.
It returns finite resolved tracks, a size, and clipped cell rectangles; it does not render the controls.
The result records whether each measurement axis was unbounded.
`GridLayoutMath.Arrange` accepts the actual finite allocation and those prior unbounded flags.
It preserves intrinsic star sizing on a previously unbounded axis instead of treating a larger scroll viewport as a new weighted budget.
Its rectangles still clip to the actual allocation, and row measurement still uses the actual clipped column width.
The same contextual rule applies to Stack flex: a finite scroll arrangement must not discard the earlier unbounded main-axis decision.
The result's read-only `CellContexts` records the intrinsic context passed to each child independently on each axis.
A cell spanning only fixed tracks ends inherited unbounded context on that axis and establishes a finite child budget.
A span containing any automatic or star-as-auto track preserves intrinsic context.
For example, a vertical stack with desired heights 32 and 48, spacing 8, and weights one and three receives slots 48 and 144 inside a fixed 200-unit row.
The same stack spanning a fixed-plus-automatic pair retains 32 and 48 even when its finite cell is 200 units tall.
Width and height decisions are independent; a fixed column does not end vertical intrinsic context.
Native adapters must use cell rectangles and context metadata for their final child measurement/arrangement, not infer intrinsic context from a finite measure mode alone.
A context change must invalidate a native measurement cache even if its finite width/height and native measure-mode bits are unchanged.
The first measurement pass gathers intrinsic widths.
After columns resolve, children are measured again at each actual parent-clipped column-span width to determine row demand.
This matters when a fixed 280-unit track is offered only 100 units: a caption must be measured at 100 before choosing its natural row height.

Intrinsic allocation is deterministic:

1. Seed fixed tracks with their clamped fixed values and nonfixed tracks with their minima.
2. Process single-cell demands first, then longer spans by increasing span length and stable authored order.
3. Compute each span's remaining deficit after its existing covered extents.
4. Distribute that deficit equally among nonfixed tracks, respecting maxima and redistributing shares from saturated tracks.

Fixed tracks contribute to a span but never grow because of its child demand.
For bounded layout, automatic tracks retain their intrinsic sizes and star tracks receive weighted leftover space starting at their minima.
Saturated star tracks return their remaining share to other eligible star tracks.
For an unbounded axis, stars retain their intrinsic sizes as automatic tracks; their weights do not fill an infinite space.
These are explicit XUI rules, not a claim that raw CSS `fr` and spanning-content algorithms are equivalent.

This slice uses zero Grid gaps and padding.
Use surrounding or cell-local stacks for insets; unsupported Grid spacing arguments are not ignored.
When fixed/minimum track demand exceeds the bounded offer, cells are clipped to the actual parent rather than forcing overflow.
Even zero-sized cell origins stay inside the parent content bounds.
An unbounded total beyond finite float coordinates is an explicit overflow error.
Native backends translate both infinity and their unbounded-size sentinel into the shared unbounded policy before arithmetic.

The [Grid showcase](../../bindings/dotnet/Experimental/Xui.Portable.Tests/Fixtures/GridSizingShowcase.xui) demonstrates row/column spans, retained input, atomic track changes, and an ordinary keyed stack inside a static cell.
It also links the existing [reusable banner fixture](../../bindings/dotnet/Experimental/Xui.Portable.Tests/Fixtures/MutationBanner.xui).
The [literal Grid corpus](../../bindings/dotnet/Experimental/Xui.Portable.Tests/Fixtures/GridLayoutScenarios.json) covers fixed/auto/star allocation, capped redistribution, span deficits, and extreme weights.
Shared arithmetic tests do not establish native font measurement, scroll behavior, or accessibility.
Grid with per-axis overrides requires a peer whose native implementation actually supports constraints for Grid itself.
The existence of older native axis exports for basic controls is not sufficient to advertise that capability.

## Host ownership and thread access

An application creates `Host` with an `IUiDispatcher` on the target UI thread.
The generated constructor accepts that host, followed by declared component parameters.
The host owns exactly one component tree.
The generated constructor uses `BeginBuild`, `SetContent`, and `BuildScope.Complete`.
A failed constructor releases partial elements and leaves the host available for another construction attempt.

```csharp
using var host = new Xui.Experimental.Portable.Host(dispatcher);
var component = new PortableDemo.Greeting(host);
host.Attach(backend);
```

The application owns the host lifetime.
The generated component does not own a native window and does not implement `IDisposable`.
`Host.Dispose` releases the attachment and all managed event handlers.
It is terminal and idempotent on the UI thread.
State and element access after disposal throws `ObjectDisposedException`.

The tree rejects cross-host children, duplicate parents, cycles, orphan elements, and structural changes outside explicit keyed reconciliation.
The element hierarchy exposes `Kind`, `Parent`, `Children`, `Flex`, `FixedSize`, and `PreferredSize`.
Concrete types expose their current control and layout properties.
The backend needs no internal fields.

`IUiDispatcher.CheckAccess` reports UI-thread access.
`Post` accepts an action exactly once, or throws if dispatch is unavailable.
The platform must execute accepted actions while the dispatcher remains alive.
An optional `ICancellableUiDispatcher` overload accepts an action and a cancellation callback carrying an exception.
It must invoke exactly one terminal path for accepted work, including during platform shutdown.
`Host.DispatchAsync` uses this overload when available, faults on cancellation, and suppresses a late action after cancellation.
`Host.DispatchAsync` can be called from any thread.
Its task reports dispatcher errors, callback errors, wrong-thread delivery, or host disposal.
Queued work cannot mutate a disposed host.
Disposal does not require the backend to drain a platform queue.

`VerifyAccess` guards reads.
`VerifyMutation` also rejects mutation during backend callbacks.
Generated state access additionally verifies the component root is alive.
Generated state setters reject mutation of live components from a candidate construction scope.
State refresh is synchronous and not transactional across several bindings.
A binding error can leave the authored state changed and earlier bindings applied.
The error propagates to the caller.

### Component-owned cancellation and resources

Each generated portable component exposes `ComponentLifetime Lifetime`, rooted in its completed component tree.
`Lifetime` is a reserved generated member in the portable profile.
The existing `IPortableComponent` interface remains unchanged.
Manual components can call `Host.GetComponentLifetime(root)` for a root completed by a build scope.
Access requires the host UI thread and must not create a lifetime during a native callback or for an incomplete, foreign, or retired root.
Creation is lazy.
Once a lifetime exists, its getter returns that same object even after retirement or host disposal; it never creates a new live owner in its place.

`ComponentLifetime.Token` is a cached `CancellationToken`.
It may be passed to producers and observed on worker threads, including after retirement.
`Lifetime.Own<T>(resource)` accepts an `IDisposable`, returns it, and transfers cleanup responsibility to the component.
Registration requires the host UI thread and a live component.
Late or invalid registration leaves ownership with the caller.
A host-wide reference-identity registry rejects registering the same disposable object twice, including across two component lifetimes.
Separate boxed copies of a value-type disposable and registration across different hosts cannot establish unique ownership; do not duplicate ownership that way.
The lifetime has no public manual `Dispose`: the component root determines when it retires.

```csharp
var work = row.Lifetime.Own(new Xui.Experimental.Portable.UiWorkScope(host));
Task pending = work.RunAsync(
    token => LoadDraftAsync(token),
    draft => row.Entry = draft,
    row.Lifetime.Token);
// Observe pending; the producer still owns its own resources and must honor cancellation.
```

`row` and `LoadDraftAsync` in this example are application-defined.
Owned UI work uses the existing `UiWorkScope`; that class also remains usable as an explicitly application-owned scope.
There is no implicit host-wide work registration.
Subscriptions can be owned by registering their explicit unsubscribe disposable.
`IAsyncDisposable` is not implicitly awaited or blocked on by this synchronous UI-thread cleanup contract.

Permanent keyed removal, component-type replacement, construction rollback, and `Host.Dispose` retire affected lifetimes.
Reordering, ordinary property updates, `Host.Detach`, and detach-on-native-failure do not.
A postcommit native failure preserves the newly committed component's lifetime together with its model.
Removing and readding a key gets a new lifetime and cancellation token.

Retirement runs child components before their parent.
For each component it first cancels the token, invoking all registered cancellation callbacks, then disposes owned resources in reverse registration order.
All of this occurs before any native unmount of the retiring tree.
Callbacks cannot reenter structural mutation, host disposal, ownership registration, or the same build-scope rollback.
All cancellation, resource, and native cleanup errors are collected; later cleanup still runs.
Retirement is terminal even when cleanup throws, and no owned resource is disposed again during subsequent attachment cleanup.
Failed generated construction preserves its original exception alongside rollback errors.

An owned work scope and component token prevent a queued apply from reaching a retired row.
They do not forcibly stop arbitrary producers or observe their tasks for the application.
Callers must still observe returned tasks and propagate or handle their cancellation/errors.
The token is not a replacement for native callback-generation guards or the generated root-alive checks.

## Backend attachment contract

The public interfaces are in [Contracts.cs](../../bindings/dotnet/Experimental/Xui.Portable/Contracts.cs).
The lifecycle implementation is in [Host.cs](../../bindings/dotnet/Experimental/Xui.Portable/Host.cs).
`IBackend` creates peers, mounts the root, and releases its host surface.
`IElementPeer` adds native children, updates one property, and releases one peer.

Before `Attach` accepts a backend, the caller owns it.
Argument or host-state rejection does not dispose it.
Once creation starts, the host owns the backend and every returned peer.
Each element requires a distinct, non-null peer.
The backend must release partial native resources if its own `Create` call throws before it returns a peer.

Creation proceeds in parent-first tree order.
`Create` reads all current properties and creates an unmounted native element.
It can inspect the typed element throughout the attachment lifetime.
`AddChild` receives completed child peers in authored order.
`Mount` attaches the completed root to the platform surface.
Events remain inactive until `Mount` returns.

`Update(ElementProperty)` reads the new property from the same element.
It must update the existing peer, not replace the tree or input widget.
Label and button text changes use `ElementProperty.Name`.
Text input value changes use `ElementProperty.Text`.
An update must preserve focus, selection, and IME composition unless the changed property itself requires a native edit.
Programmatic input edits must not produce user change events.

Backend callbacks can read the model but cannot mutate it or reenter attachment operations.
The runtime rejects synchronous event echoes during `Create`, `AddChild`, `Mount`, and `Update`.
Backends must also suppress delayed native echoes themselves.

`Detach` invalidates all event sinks before native cleanup.
It first calls `IBackend.Dispose` to unmount the surface.
It then disposes peers in reverse creation order.
Backend disposal must not dispose the peers.
Peer disposal must remove native listeners and release interop handles.
Cleanup continues after errors and reports all failures through `AggregateException`.

Detach retains the model and authored handlers.
A later attachment creates new peers from current state.
Old event sinks never become active again.
An attachment failure releases the partial attachment and preserves the model.
An update failure retains the new model value, detaches the failed backend, and propagates the error.
An application can attach a new backend after it handles the error.
Disposal remains terminal even if native cleanup throws.

### Mutable peer capability

`IElementPeer` remains unchanged for fixed trees.
An attached `KeyedStack` requires a peer implementing `IMutableElementPeer`, even if the container is initially empty.
Missing capability throws `NotSupportedException` and releases the partial attachment.
There is no rebuild fallback.

```csharp
public interface IMutableElementPeer : IElementPeer
{
    void InsertChild(int index, IElementPeer child);
    void RemoveChild(IElementPeer child);
    void ValidateMove(IElementPeer child, int index);
    void MoveChild(IElementPeer child, int index);
}
```

Initial attachment still uses parent-first `Create`, completed-child `AddChild`, and final `Mount`.
Before a reconciliation commits, the runtime validates all planned moves against the original native tree.
`ValidateMove` is read-only and cannot mutate the model or invoke authored callbacks.
It receives the child's current peer and its intended final index at the later move operation.
Indices include invisible children.
The index can exceed the old child count when earlier planned insertions will increase it; validate current membership and nonnegativity, not that old upper bound.
No-op moves are omitted.

An optional `IMutationPreflightPeer : IMutableElementPeer` adds `void ValidateMutation()`.
The runtime calls it once before all move validations and before any factory or commit when child membership or order changes.
This lets a native aggregate ownership scope conservatively reject insertion or removal during composition as well as moves.
It is read-only and has the same precommit failure behavior.
An unchanged-key snapshot that only updates retained component properties does not call this structural preflight.

An adapter must inspect the entire moved subtree for editing hazards.
If it cannot preserve active composition, `ValidateMove` must reject before any factory, model, or native mutation.
The application receives a precommit exception and can explicitly retry after composition ends.
This slice has no automatic deferred queue.
Moving an editor must not rewrite its value, recreate its widget, silently cancel composition, or use visual-only ordering that disagrees with keyboard/accessibility order.
Focus and selection preservation require native platform evidence.

After model commit, the runtime removes retired roots, disposes their peers in reverse creation order, then performs insertions and moves in final child order.
Inserted subtrees are created parent-first while unmounted.
`InsertChild` receives a completed subtree; `MoveChild` receives an existing direct child and an index after that child is taken out of the current native list.
All native indices at execution are in range.
During these operations, `Element.Children` already describes the complete new model, not the intermediate native list.
Adapters must track native child order separately.

All event delivery is blocked during native mutation, including removed sinks before `RemoveChild`.
Removed event sinks stay inactive after cleanup or later reuse of their key.
`RemoveChild` unmounts but does not dispose children.
Only the host disposes individual peers; neither parent peers nor backend cleanup recursively dispose them.
A failed peer disposal is not retried during whole-attachment cleanup.
Native resources owned by an aggregate backend arena must still be released by that backend on full detach.
Such an arena does not imply support for dynamic per-subtree release.

## Native event delivery

Each peer receives an `IControlEvents` sink.
Native callbacks call `Click`, `Change`, or `Submit` on the UI thread.
A button accepts only `Click`.
A text input accepts only `Change` and `Submit`.
Wrong event types throw while the sink is active.

`Change(text)` records the native input value before it calls authored handlers.
It does not write that value back to the peer.
An identical text value does not call the change handler again.
An authored handler can set another value explicitly.
Setters remain silent, including setters inside handlers.

Event methods return `false` for stale, detached, disposed, invisible, or disabled targets.
An invisible or disabled ancestor also rejects descendant events.
They return `true` for accepted events, including unchanged text.
Wrong-thread delivery throws.
Handler errors propagate to the native callback boundary.
The backend must report those errors explicitly, not discard them.

## Platform implementation boundary

The DOM backend maps stacks to real layout containers and controls to native DOM text, button, input, and scroll elements.
The Android backend maps stacks to native linear layouts and controls to native text, button, edit, and scroll widgets.
A text-input peer can own a wrapper for its caption and edit widget.
That wrapper remains one runtime element.
The platform adapter must preserve semantic labels, enabled state, keyboard input, and accessible identity.

Platform projects link the shared `.xui` file directly.
They must not copy its UI into Razor, JavaScript, Android XML, or hand-written C#.
Platform-specific startup, dispatch, lifecycle callbacks, and widget mapping remain outside the shared sample.
Actual browser and Android execution require separate platform implementation and acceptance checks.

The [Android adapter](experimental-android.md) implements this subset with native widgets and a shared-source Activity sample.
Its APKs and native checks have run on an API 35 emulator; physical input, accessibility, and broader platform acceptance remain separate gates.
Reference-only compilation and arithmetic tests alone do not establish Android device execution.

The [experimental DOM backend](experimental-dom-web.md) implements this subset with local .NET WebAssembly and native DOM controls.
Its public contract describes browser layout, event ordering, input identity, and page lifetime.
