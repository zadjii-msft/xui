# Multi-platform framework completion plan

**Status: approved implementation plan; not a shipped API or support promise.**
This plan targets shared `.xui` and C# applications on native Windows, native Android, and the browser.
It expands the [engineering roadmap](../llm/cross-platform-roadmap.md) into a release plan.
The existing [portable](experimental-portable-xui.md), [Android](experimental-android.md), and [web](experimental-dom-web.md) contracts remain authoritative as individual slices are implemented and verified.
Approval covers the complete milestone sequence, not just the shared samples or screenshots.
Unimplemented features and missing device, accessibility, performance, CI, or release approval gates remain open.

## What "finished" means

The first goal is a supported multi-platform SDK for real applications, not complete parity with every Windows control.
An author should be able to create an application outside this repository, share its UI and behavior, use normal native input, build release artifacts, and maintain it without modifying XUI internals.

A supported initial release must provide:

- Versioned packages and a template with shared application code and thin platform hosts.
- A documented common application-facing API, lifecycle, threading, error, and capability model.
- Reusable components, conditional content, and keyed dynamic collections.
- Useful layout, typography, themes, forms, images, navigation, and a virtualized linear list.
- Owned asynchronous work and explicit access to platform services.
- Native input and accessibility acceptance on a published device/browser matrix.
- Reproducible CI, release packaging, performance budgets, debugging, and migration documentation.

That is the completion boundary for the first release.
Advanced desktop controls and additional language bindings follow a separate capability roadmap.
Windows-only integration remains valid rather than being emulated badly on platforms that cannot support it.
This plan does not port the C++ renderer into Android or Wasm, promise portable C++/Rust applications, or add native iOS/macOS targets.
Those would require separate scope and architecture decisions.

## Pre-implementation baseline

At the start of this plan, the experiment demonstrated:

- One authored greeting and one richer order builder compiled for all three targets.
- A managed retained runtime for Android and web, native Android widgets, and real browser DOM controls with local C# Wasm execution.
- Native Windows execution through the existing Windows generator profile and bindings.
- A shared scenario corpus plus native input, lifetime, error, and geometry checks.
- Repository-local build/run commands, actual emulator deployment, and static web publication below a URL prefix.

The gaps at that baseline were structural, not just additional sample screens.
The portable language had six authored nodes and one fixed tree.
`Host.VerifyBuilding` rejected structural mutations after construction, and `IElementPeer` had `AddChild` but no insert/move/remove protocol.
Windows did not implement the portable `IBackend` contract.
Portable consumer packages, project templates, navigation, style/resources, a general service boundary, and portable hot reload were absent.
Repository workflows covered documentation and Windows packaging/templates, not the complete portable matrix.
These are historical starting conditions, not an inventory of the current checkout.
The [portable contract](experimental-portable-xui.md) describes implemented and capability-gated APIs.
The [integration evidence](../llm/portable-foundation.md#continuing-integration-september-23-2026) distinguishes combined-checkout results, backend acceptance, and still-open gates.

Passing state assertions did not catch the captioned-input clipping problem.
Natural-size geometry, scaled fonts, and visible output must therefore be first-class acceptance criteria, not inferred from correct model values.
Detailed dated results belong in the [maintainer evidence](../llm/README.md), not in release support claims.

## Proposed release scope

| Area | Required for the first supported release | Separate follow-on work |
| --- | --- | --- |
| Authoring | Shared `.xui` and C#, reusable components, typed properties/events, source-located diagnostics | Other authored languages or a runtime markup interpreter |
| Composition | Conditional subtrees and keyed insertion/removal/reordering | Arbitrary visual-tree mutation without ownership rules |
| Layout | Stacks, grid, per-axis auto/min/max sizing, wrapping/adaptation, documented scrolling axes | Pixel-identical layout across native engines |
| Presentation | Typography, semantic theme resources, light/dark/high-contrast behavior, essential state styling | Full Windows styling/template parity and custom drawing systems |
| Forms | Text, multiline/password input, input purpose, validation, choices, toggles, ranges, progress | Every specialized desktop editor |
| Collections | Keyed rows and a virtualized linear list with selection and accessible navigation | Data grids, trees, Miller columns, and spreadsheet-like editing |
| Resources | Packaged images/fonts, bounded image loading, caching, cancellation, and errors | Media playback, maps, arbitrary embedded web/native surfaces |
| Application model | Navigation/back, page ownership, commands, asynchronous work, state restoration | A mandatory MVVM framework or application business architecture |
| Services | Capability-aware clipboard, URI launch, file selection, storage, and permissions | Pretending browser sandbox restrictions do not exist |
| Tooling | Template, build/run/publish, debugging, source maps, diagnostics, fixture harness | Cross-platform Designer and state-preserving hot reload |
| Deployment | Native Windows distribution, signed Android package procedures, static web hosting | Automatic store submission, package publication, or hosted services |

Exact public control names and package IDs should be chosen through the contracts below, reusing existing XUI vocabulary where appropriate.
This table is not permission to expose partially implemented controls.

## Architecture decisions to close first

### A. One portable application surface

**Recommendation:** prototype an opt-in native Windows adapter for the managed portable host.
Keep the existing Windows generator profile, native bindings, C++ API, and Windows-only controls compatible.
The goal is one portable generated component API, not replacing the Windows product.

This is not just a set of widget constructors.
The prototype must prove that Windows content scopes and native ownership can satisfy portable attachment, subtree disposal, event invalidation, and update-failure semantics.
Do not wrap window-owned controls in disposable peers without proving who releases their native resources.
Use the existing content/lifecycle APIs before proposing C ABI extensions.

The decision gate is the shared fixtures running through that adapter, including native input identity and repeated attachment cleanup.
If the ownership model cannot be bridged safely, explicitly retain dual generation behind a documented common application contract and enumerate its API differences.
Resolve this before freezing public portable control references and host APIs; do not let the two paths drift accidentally.

### B. Backend-neutral contracts, native implementation

Keep `.xui` compiled to C# and keep application logic local.
The common runtime owns state, identity, subscriptions, and lifetime.
Backends own native widgets, measurement, input, and accessibility.
Do not introduce a canvas UI or translate authored C# to JavaScript to avoid native behavior differences.

Define shared semantics before widget mapping.
Equivalent behavior is required; identical pixels, fonts, or platform chrome are not.
Use shared capability metadata and generator checks to prevent independent backend interpretations of supported properties and events.

### C. Identity and mutation

Define component scopes and keyed subtree ownership before adding dynamic children.
Collection keys are scoped identity, not automation IDs.
Preserve the current allowance for duplicate automation IDs in different elements.
Specify what happens when a key survives, moves, changes component type, disappears, or reappears.

Preserve retained inputs on unrelated updates.
For moves during composition, prove that the platform operation preserves editing or define a bounded deferred-mutation policy.
Do not substitute visual CSS ordering for correct DOM, keyboard, and accessibility order.

### D. Lifetime, dispatch, and errors

Separate application state lifetime, view attachment lifetime, and terminal disposal.
Assign an owner and cancellation policy to every subscription, timer, task, stream, image request, and navigation entry.
Every accepted dispatch must complete, fault, or follow an explicitly defined cancellation contract.
Late results and callbacks must never target a replacement or disposed view.

Specify callback error boundaries across Windows, Android, and web.
Distinguish expected validation/cancellation, recoverable application failures, and terminal native failures.
Reporting an error must not become a success-shaped fallback.
Do not promise transactional state rollback: current property refresh is synchronous and can partially apply before failure.

### E. Platform capabilities

Unsupported authored UI must remain a source-located diagnostic unless an explicit platform extension is selected.
Runtime services must distinguish unsupported, denied, cancelled, and failed operations.
Capabilities cannot promise that a browser permission or user-gesture requirement will be satisfied later.
Shared code should not assume filesystem paths, persistent handles, unrestricted networking, or desktop window behavior.

## Milestone sequence

| Milestone | Deliverable | Depends on |
| --- | --- | --- |
| M0 | Scope, support matrix, invariant tests, and reproducible baseline | Current experiment |
| M1 | Common host/ownership decisions and backend conformance CI | M0 |
| M2 | Installable preview packages and an external project template | M1 application API decision |
| M3 | Reusable components and safe dynamic composition | M1; validate consumers through M2 |
| M4 | Core layout, presentation, forms, and resource slices | M1; deliver incrementally through M2 |
| M5 | Application lifecycle, navigation, async work, and services | M1; dynamic pages use M3 |
| M6 | Virtualized collections and resource/performance hardening | M3 plus required M4/M5 contracts |
| M7 | Complete author tooling, documentation, and sample coverage | M2 onward, incrementally |
| M8 | Release candidate, compatibility policy, and production acceptance | All required gates above |

M3 and M4 can proceed in parallel after their shared contracts are settled.
Service contracts and tooling can also start early.
Manual device/accessibility work runs throughout; it is a release gate, not a reason to postpone useful CI or experimental feature work.
The critical structural path is host ownership, subtree mutation, keyed collections, virtualization, then release hardening.
Do not wait until every control is implemented to test package consumers.

## M0: Establish the release baseline

1. Inventory the current controls/properties/events and mark each as implemented, experimental, proposed, or platform-only.
2. Select the initial SDK/workload versions, minimum platform versions, browser matrix, and reference hardware.
3. Preserve the greeting as a small smoke test and the order builder as the application-level regression.
4. Add independent conformance fixtures for sizing, visibility, disabled ancestry, invalid values, duplicate IDs, partial construction, cleanup failures, and queued work during disposal.
5. Make font scaling, caption/editor geometry, keyboard insets, and narrow/short viewports explicit checks.
6. Establish baseline measurements and agree on release budgets before making performance promises.

**Exit gate:** a clean machine can reproduce the baseline from documented prerequisites; missing physical or platform coverage is explicitly tracked.
Correct model values alone do not satisfy visible-output or accessibility gates.

## M1: Stabilize the host and automate conformance

Close architecture decisions A-E with small implementation spikes and recorded decisions.
Prove the Windows portable-adapter approach or document the intentional dual-profile boundary.
Define a common contract for programmatic focus, selection, accessible identity, dispatcher access, and error reporting before exposing new convenience APIs.
Logical focus restoration must use component/item identity, not process-local widget handles or globally unique automation IDs.

Build scoped CI lanes for:

- Managed generator/runtime/model tests on clean SDK installations.
- Native Windows build and conformance, with desktop/input tests on a suitable interactive runner.
- Web Debug and published Release tests, including non-root assets, disconnected callbacks, actual navigation, and clean teardown.
- Android APK construction and real emulator execution, not reference-only compilation.
- Package/template consumer checks once M2 is available.

Bound each job, capture useful failure artifacts, and redact user data in diagnostics.
Keep publishing and signing permissions out of routine pull-request jobs.
Retain native Windows regressions even if portable Windows uses a new adapter.

**Exit gate:** the declared automated matrix runs from clean checkouts without local caches, silent skips, abandoned processes, or blanket retries.
Platform-specific geometry tests complement, rather than duplicate, the shared behavioral corpus.

## M2: Make an SDK that application authors can consume

Package the compiler/build assets, common runtime, and platform adapters with explicit dependency and target-framework compatibility.
Choose package boundaries and IDs without changing the current Windows `Xui` package or `dotnet new xui` behavior unexpectedly.
Initially produce installable preview artifacts; external publication still requires approval.

Provide an experimental template containing:

- Shared application UI, ordinary C# models, resources, and a testable service boundary.
- Thin Windows, Android, and web entry points with explicit ownership and error reporting.
- Platform configuration, native runtime deployment, and a version-pinned dependency set.
- A shared test project and documented build/debug/publish entry points.

Remove checkout-relative imports from the consumer path.
Ensure packaged web JavaScript/CSS/images resolve under arbitrary supported base paths.
Keep compiler, inspector, reload transport, and test bridges out of Release output.
Do not silently install workloads, accept SDK licenses, or provision signing credentials.

**Exit gate:** generate into a fresh directory outside the checkout, restore through a controlled package source, build all targets, and run the shared scenarios.
Repeat with clean caches, paths containing spaces, and a package upgrade.
Copying runtime source into the generated application does not count.

## M3: Introduce reusable and dynamic UI safely

Deliver this as several vertical slices, not one large reconciler rewrite:

1. Reusable components with typed inputs/events, owned subscriptions, and explicit parent/child lifetime.
2. Conditional subtree creation/removal, distinct from hiding an existing control.
3. Keyed repeated children with insertion, removal, replacement, and reordering.
4. Typed collection updates and row state that do not depend on array index identity.

The implementation needs runtime and backend changes, not just parser syntax.
Extend construction scopes for partial subtree rollback, define peer mutation operations, and invalidate removed event sinks before native unmount.
Prevalidate duplicate keys and cross-host ownership before modifying the live tree.
Specify backend failure recovery consistently with the existing detach-on-update-failure contract.
An invalid update must not leak partially created native peers or resurrect old handlers.

Turn the order builder into an editable cart for this milestone.
Use stable item keys and prove that editing one row survives adding, removing, sorting, and updating other rows.
Test focused-row removal, type changes under an existing key, composition during moves, nested collections, duplicate keys, failed construction, and repeated mount/dispose.
A surviving key must not recreate its editor just to simplify rendering.

**Exit gate:** one authored dynamic sample and one shared mutation corpus pass on every backend, including native focus/selection/composition and exact listener/resource cleanup.
Document any composition-time deferral instead of hiding platform limitations.

## M4: Deliver the core UI in vertical slices

Prioritize capabilities needed by real shared applications.
Each slice includes a public contract, generator checks, runtime state, all backends, an authored example, and automated/manual acceptance as applicable.
Preserve existing Windows control names and semantics where they are suitable.

| Order | Slice | Required details |
| --- | --- | --- |
| 1 | Sizing and adaptive layout | Per-axis auto/min/max, grid, wrapping/breakpoints, overflow, scroll axes, insets, DPI and font scaling |
| 2 | Typography and resources | Semantic text roles, wrapping/truncation, font fallback, localizable strings, bidirectional layout, packaged fonts |
| 3 | Theme and state styling | Semantic colors/spacing, light/dark/forced colors, focus/disabled/error states, reduced motion; no input replacement |
| 4 | Form controls | Multiline/password text, input purpose, validation presentation, check/toggle and single-choice controls |
| 5 | Commands and feedback | Keyboard activation/shortcuts, progress/busy state, empty/error states, accessible status announcements |
| 6 | Images and assets | Packaged and external sources, decode bounds, loading/error state, bounded caches, cancellation and stale-result rejection |
| 7 | Dialog and popup primitives | Modal ownership, focus trapping/restoration, Escape/Back, dismissal and nested lifetime |

Input purpose is more than a keyboard hint: define native input, browser autofill/autocomplete, password handling, and accessibility implications.
Do not format or normalize text during composition merely to keep a model tidy.
A themed captioned input must include the complete native editor and padding at large font sizes.

**Exit gate for each slice:** no unsupported property is ignored, every promised interaction has backend coverage, and the common sample uses no alternative platform-authored UI tree.
Missing parity is documented as a capability boundary.

## M5: Make applications, not just screens

Define application, window/activity/page, and component scopes.
Provide owned asynchronous commands with cancellation, busy/error state, stale-result protection, and UI-thread delivery.
Use normal C# and existing .NET facilities; do not require a new business-logic framework.

Add navigation with stable route/page identity, explicit parameters, page activation/deactivation, and disposal.
Specify Windows window close, Android Back/gesture and Activity recreation/process death, browser history/deep links/reload, and bfcache behavior separately.
Do not confuse Activity instance-state restoration with durable storage or bfcache retention with reload persistence.

Design small service contracts for storage, file selection, clipboard, URI launch, and permissions.
Return owned streams/handles with explicit lifetime where a path is not meaningful.
Document browser secure-context/user-gesture/CORS restrictions and Android permission revocation.
Keep app-specific networking, authentication, database choices, and backend services outside the UI framework.

Evolve the cart into a small application with browse/detail/edit navigation, a restorable draft, and a delayed/cancellable data operation.
Use deterministic fakes in shared tests and real platform adapters for integration acceptance.
Exercise failures, cancellation, rapid navigation, process recreation, corrupt/versioned saved data, and disposal while work is queued.

**Exit gate:** the same application handles those transitions without stale updates, leaked resources, lost accepted tasks, retained Activities, or silent data loss.

## M6: Make scale and resource behavior predictable

Implement a virtualized linear collection only after keyed identity and row ownership are stable.
Separate data/selection identity from recycled presentation.
Define row measurement, scrolling/anchoring, incremental loading, keyboard navigation, and virtualized accessibility.
Pin or otherwise protect a focused/composing editor from recycling.
Do not fake accessibility by instantiating the entire data set.

Use published stress fixtures: a large nonvirtualized keyed update, a 10,000-item virtualized list, repeated navigation, and repeated attach/detach.
For example, a release fixture should execute at least 100 attachment/disposal cycles and verify that obsolete listeners, interop references, timers, and native peers are released.
These are proposed workloads, not existing support or performance claims.

Measure startup, download/APK size, first usable input, update latency, frame time, managed/native/JS memory, and image-cache retention.
Specify hardware, workload, warm/cold conditions, variance handling, and numeric pass/fail budgets.
The release cannot pass with budgets still undefined.
AOT, trimming, batching, and caching are optimizations to evaluate against those budgets, not substitutes for correctness.

**Exit gate:** large collections remain bounded by the visible range and documented overscan, active input survives recycling boundaries, and repeated use meets the agreed latency/resource budgets.

## M7: Complete the author experience

Ship source-mapped diagnostics, normal C# debugging, useful startup errors, and clear unsupported-feature messages.
Add opt-in diagnostics for tree ownership, layout allocations, binding refresh, dispatch, and native-resource lifetime.
Default diagnostic output must avoid collecting passwords, customer input, or application secrets.

Document the supported project structure, platform differences, cancellation, accessibility, localization, assets, deployment, and package upgrades.
Provide a control gallery authored once for all targets, the small greeting smoke, the dynamic cart, and one application with navigation/storage/async behavior.
Keep contributor commands in `CONTRIBUTING.md`, public contracts in the handbook, and dated evidence in maintainer notes.

### Application-scale acceptance

The approved follow-through also requires realistic multi-screen examples, not only isolated control workbenches.
A shared workspace should combine genuine navigation and document tabs, retained editors, a large virtualized catalog, an inspector, and cancellable application work.
A related dashboard or catalog/editor workflow should reuse the same components rather than duplicate platform-specific UI trees.
Desktop multi-pane layouts and narrow phone layouts must exercise real grid/breakpoint behavior, colors and typography, and bounded motion once those capabilities are implemented.

Navigation, tabs, retained page ownership, keyboard/accessibility order, focus/composition retention, high contrast, and reduced motion need vertical contracts and real backend coverage.
A row of ordinary buttons is not proof of a native navigation or tab primitive.
Zero-width panes are not proof that hidden content has left the accessibility/input tree.
Sample screenshots must come from actual Windows, Android, and browser renders at documented sizes.
New controls remain capability-gated until their platform implementations and acceptance are complete.
These examples supplement the smaller smoke/regression apps; they do not replace or weaken resource and performance gates.

Treat portable hot reload as a separate lifecycle feature.
It requires compatible-state migration, old-code/subscription retirement, input retention, failed-reload recovery, and Release exclusion.
Until that gate is met, describe rebuild/restart accurately rather than advertising hot reload.
A multi-platform Designer is useful follow-on tooling, not a prerequisite for correct runtime behavior.

**Exit gate:** an application author can create, debug, diagnose, test, publish, and upgrade the samples using documented packages and tools without reading implementation notes.

## M8: Release readiness

Freeze the supported API and capability matrix only after external consumer feedback and all required milestones.
Define compatibility/versioning policy, deprecation rules, package alignment, upgrade tests, supported SDK versions, and a platform-defect triage process.
Removing `Experimental` is a deliberate compatibility decision, not a namespace cleanup.

Validate actual distributed artifacts:

- Windows: packaged native dependencies, x64/ARM64 execution, manifests, self-contained deployment options, and install/upgrade behavior.
- Android: Release trimming/AOT execution, APK/AAB packaging, signing/upgrade procedures, permissions, process death, and a physical ARM64 device.
- Web: published files on a normal static host, MIME types, base paths, cache/version upgrades, CSP and asset restrictions, failures/offline transitions, and browser history.

Review untrusted text/URIs/assets, interop boundaries, resource exhaustion, privacy, accessibility, and dependency/license obligations.
Keep signing keys out of source and PR jobs.
Do not publish packages, submit to stores, or turn on hosted deployment without separate approval.

**Exit gate:** a release candidate installed from its distribution artifacts passes the approved matrix, manual input/accessibility checks, external-consumer tests, and performance budgets.
Every known limitation is either outside the declared scope or an explicit release blocker.

## Acceptance matrix

The following is the proposed coverage, not current certification.
Choose exact maintained versions at the M0 cutoff and publish them with the release.

| Surface | Automated coverage | Required human/device coverage |
| --- | --- | --- |
| Windows | Native execution on x64 and ARM64; legacy and portable paths; geometry, keyboard, lifetime | Physical IME, Narrator, high contrast, DPI/font changes on supported Windows/.NET combinations |
| Android | Minimum declared API (currently 26) and current API at release; emulator lifecycle, geometry, keyboard-visible scrolling | Physical ARM64 device, real IME/candidate input, TalkBack, Back gestures, rotation/multi-window and process death |
| Web | Chromium, Firefox, and WebKit engines; Debug/published builds; root/subpath hosting; real navigation and cleanup | Shipping supported browsers, including actual Safari if claimed; screen readers, physical/mobile keyboards, zoom/reflow |
| Packages | Fresh external consumers, version compatibility, asset/native runtime resolution, upgrade tests | Documented install/debug/publish walkthrough by an author outside the implementation team |

Playwright WebKit is not proof of shipping Safari behavior.
An x86_64 emulator is not proof of ARM64 device behavior.
Reference compilation and successful APK/AOT construction are not execution evidence.
Synthetic composition and accessibility properties do not establish real IME or screen-reader acceptance.

Built-in controls should meet applicable accessibility requirements, including names/roles/states, keyboard order, focus visibility, announcements, contrast, reflow, and reduced motion.
Application content and authored styling still need application-level accessibility review.

## Delivery organization

Keep one owner for shared language/runtime contracts and one owner per backend.
Use a packaging/CI/tooling owner when that work can proceed independently.
These are responsibilities, not a requirement to run a fixed number of agents or maintain long-lived divergent branches.

For every feature:

1. Agree on behavior, lifetime, capability limits, and shared fixture.
2. Integrate the shared contract/runtime change with explicit unsupported behavior until adapters are ready.
3. Implement platform adapters against that exact baseline.
4. Integrate all targets and rerun acceptance in the combined tree.
5. Update public documentation and record actual evidence before declaring the feature complete.

Avoid simultaneous incompatible backend API proposals, platform-specific sample forks, or accumulating features without consumer validation.
Use small reviewable changes, but do not present a partially wired feature as supported.
A speculative fallback is not a substitute for a missing platform implementation.

## Recommended next changes

The table below records the initial implementation sequence, not the remaining work in the current checkout.
Use the [maintainer handoff's ordered resume checklist](../llm/cross-platform-maintainer-handoff.md#resume-in-this-order) for the current integration and unresolved gates; the milestone exit criteria above remain unchanged.

| Change | Scope | Completion evidence |
| --- | --- | --- |
| 1. Preserve the baseline | Integrate the current experiment, richer sample, and clipping regression through the normal review process | Reproducible combined-tree results; no premature support-label change |
| 2. Add portable CI | Scope automated jobs to generator/runtime, native Windows, browser publication, and Android device execution | Clean runner exits and retained failure artifacts |
| 3. Close the Windows host decision | Prototype portable ownership/dispatch/native peer mapping using existing Windows scopes | Common fixtures plus focus, IME, failure, and cleanup evidence |
| 4. Write mutation contracts | Component scopes, conditional children, keyed identity, removal/move/failure semantics | Agreed public proposal and failing regression fixtures |
| 5. Ship the first keyed cart slice | Shared generator/runtime and all backends together | Add/remove/reorder while editing; no editor replacement or stale callbacks |
| 6. Exercise a preview consumer | Package build assets/adapters and a fresh-directory template | One external app builds and runs on all targets |

Package prototyping can overlap the mutation work once the host surface is agreed.
Actual calendar estimates should follow the Windows ownership and keyed-move spikes; those are the largest early architectural uncertainties.
Do not extrapolate a framework completion date from the speed of building the fixed-tree samples.

## Beyond the first supported release

Use real application demand to order advanced controls: virtualized grids/trees, richer document editors, additional pickers, charts/canvas, media/maps, and specialized native hosts.
For each family, decide whether it is portable, capability-gated, or intentionally Windows-only.
Shell integration, unrestricted native handles, and desktop-specific behavior do not acquire a meaningful web equivalent merely by sharing a class name.

Portable C++/Rust bindings, native iOS/macOS backends, multithreaded Wasm, server rendering, hot reload, and a multi-platform Designer are separate initiatives.
Revisit the supported-release scope if one becomes a concrete requirement.
The framework is finished for a declared release when its promised application model and capability set are reliable and supportable, not when every possible platform feature has an API.
