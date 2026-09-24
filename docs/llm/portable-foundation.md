# Portable foundation handoff

For the current integration state and ordered resume checklist, start with the [September 24 maintainer handoff](cross-platform-maintainer-handoff.md).
This document retains chronological evidence; earlier missing-feature descriptions and test counts are not the current inventory.
The subsequent user-authorized WIP preservation commit captures the integrated source; references below to uncommitted work describe its pre-checkpoint state, not a release or publication.

The [public contract](../specs/experimental-portable-xui.md) defines the experimental API and its limits.
The [contributor procedures](../../CONTRIBUTING.md#experimental-portable-foundation) contain build commands.
This note covers the shared foundation and Windows host.
The [Android](android-experiment.md) and [DOM](dom-web.md) notes cover the platform adapters.

## Source map

- `bindings/dotnet/Xui.Generator/XuiGenerator.cs`: Profile selection, portable subset rejection, and shared emission.
- `bindings/dotnet/Xui.Generator/Parser.cs`: Shared syntax and source locations, including explicit portable additions.
- `bindings/dotnet/Experimental/Xui.Portable.targets`: Repository-local portable project integration.
- `bindings/dotnet/Experimental/Xui.Portable/Contracts.cs`: Backend, peer, event, and dispatcher interfaces.
- `bindings/dotnet/Experimental/Xui.Portable/Elements.cs`: Typed retained element state and ownership.
- `bindings/dotnet/Experimental/Xui.Portable/Host.cs`: Build scopes, attachment, dispatch, event delivery, and cleanup.
- `bindings/dotnet/Experimental/SharedDemo/Greeting.xui`: The single shared application source.
- `bindings/dotnet/Experimental/WindowsDemo`: The existing Windows profile with that shared source.
- `bindings/dotnet/Experimental/Xui.Portable.Tests`: Actual generated demo execution against a recording backend.
- `bindings/dotnet/GeneratorTests/Program.Portable.cs`: Profile switching, source mapping, and diagnostic rejection.
- `scripts/Build-PortableDemo.ps1`: Repository-local build/run entry point for all three hosts, including isolated native Windows output and explicit Android device selection.
- `tests/portable-build.ps1`: Command selection, argument boundaries, prerequisite/run guards, nonzero tool failures, and caller-directory restoration.
- `bindings/dotnet/Experimental/SharedDemo/OrderBuilder.xui` and `OrderModel.cs`: Fixed-catalog shared application and immutable validated C# snapshot.
- `bindings/dotnet/Experimental/SharedDemo/OrderScenarios.json` and `OrderScenarioRunner.cs`: Literal cross-platform scenario corpus and strict C# driver contract.
- `bindings/dotnet/Experimental/Xui.Portable.Tests/Program.Order.cs`: Model, generated order tree, scenario schema, retained peers, silent updates, and stale-event coverage using the existing recording backend.
- `bindings/dotnet/Experimental/WindowsOrderDemo`: Thin native host and real-widget scenario driver, native Enter/Tab, narrow layout, input focus/selection, and cleanup checks.
- `bindings/dotnet/Experimental/Xui.Portable/Forms.cs`, `Host.Forms.cs`, and `Host.Attachments.cs`: Bounded multiline/input-purpose contracts and attachment-owned password reads, writes, and pre-unmount clearing.
- `bindings/dotnet/Experimental/Xui.Portable/Presentation.cs` and `Host.Presentation.cs`: Nullable typography/theme values and precommit backend validation; native projections have separate acceptance gates.
- `bindings/dotnet/Experimental/Xui.Portable/VirtualizationController.cs`, `VirtualizationViewport.cs`, and `Host.Virtualization.cs`: Source/epoch reservations, protected rows, explicit detached preparation, and attachment-owned native leases.
- `bindings/dotnet/Experimental/SharedDemo/VirtualList.cs`: Shared generated list composition and native viewport coordination; actual row metadata is independent of automation IDs.
- `bindings/dotnet/Experimental/SharedDemo/EditableCart*.cs` and `Cart*.xui`: Stable-key cart rows, browse/detail/edit navigation, local cancellable quotes, and transient session restoration.
- `bindings/dotnet/Experimental/EditableCart.Tests`: Shared generated cart and literal scenario coverage; does not substitute for native input/history/recreation acceptance.

## Continuing integration, September 23, 2026

The integration checkout remains on user-owned `c346ec5` with the accumulated uncommitted work.
The following selective source packets were integrated without replacing older application helpers or unrelated root documentation:

| Packet | SHA-256 | Combined-checkout evidence |
| --- | --- | --- |
| Forms and Presentation hooks | `A30C6B1C705BD5F0C086006A9AB07E443C08C8DE7B50AB7F4D43441D70194010` | 17 source files, no M5 helper replacement |
| Presentation values and fixtures | `3B91400449EC546A436AB0BA6899CA07129A0B26521FA29D9F65E4412B3A6280` | Six owned files |
| Presentation target/reset tests | `2BD3D4E24282BD9DEFF9E48486E1DBE6FD40EB7C9383C821D21FE75DB05737BF` | Rejected reset preserves prior state; five supported targets |
| Password length guard | `FC9804208BD3167FDE3AE1C7005EF4A5B722DE54FDA44619FEF1BC0F6D1787C9` | Invalid native length rejected; exact maximum accepted |
| Detached virtualization bootstrap | `5DA33FE01225AEAA7D932723C4801FDA6F7B8EE8718A99F3F9853D262C2E7ABB` | Empty realized tree before every recorded reattachment |
| Editable cart | `183ABDC0097A6B82E7E36814C2DA42985AC236A8D96918FC5ADC057668472F04` | 11 new files; 200 assertions including 40 literal expectations |

After forced rebuilds, the combined portable suite passed 2,053 assertions, generator tests passed 42,129, and detached-bootstrap tests passed 40,044 math assertions plus 30,569 controller/sample fixture assertions.
Adjacent application helpers passed 130 assertions, fixed gallery 459, dynamic task board 218, and profile workspace 441.
Browser managed dispatcher/services/storage/error fixtures passed 14/13/15/11; Android layout arithmetic passed 4,170 without executing widgets.
Windows Gallery Release and Android Gallery Debug compiled with zero warnings and errors against the additive shared contracts.
These builds did not instantiate the new Forms or Presentation controls on native platforms.

An initial incremental virtualization command executed the previous 40,043/30,662 assertions because imported ZIP source timestamps predated existing binaries.
A forced `--no-incremental` build produced the expected 40,044/30,569 output.
Do not count an incremental green result after a source archive import without proving the rebuilt code ran.

The native viewport, Forms, Presentation, assets, broader services, bidi/adaptive text, dialogs, and release gates remain open.
The four-application screenshot gallery is real evidence of those sample surfaces, not completion of the full approved plan.
Windows native acceptance encountered timeout failures under concurrent build/browser/emulator load.
An unchanged quiet retry passed the two failing native suites: features in 38.91 seconds and application ABI in 16.07 seconds.
Together with the original eleven passing suites, this is a 13-suite union, not one clean complete-run exit.
The dedicated native virtual-viewport fixture passed 934 assertions.
The subsequent managed Windows full-prefix run failed earlier in a dynamic task-board enabled-state assertion, before collecting viewport timings; an isolated 292-assertion dynamic run did not close that full-prefix failure.
Native and browser viewport timings must exclude external test-driver polling and report the actual workload, environment, all measured samples or a reproducible summary, and maximum as well as percentiles.
The initial warm-transaction target is 100 samples after ten warmups with p95 at most 50 ms, measured from the local offset request through native commit and row pruning.
It is not a release frame-rate, cold-start, or supported-device performance claim.

Subsequent combined-checkout source integration passed 2,271 portable assertions after the choice/range packet, 40,049 math and 30,610 controller/sample assertions after terminal virtualization cleanup, 165 workshop assertions, and 98 packaged-asset assertions through the repository-local asset import.
The terminal cleanup fixture retains a disposed application while checking that backend/lease/performance-subscriber references can be collected.
These are recording and ownership tests, not native frame measurements.

Actual DOM bootstrap testing completed 100 reattachments in 52.0 seconds with 101 empty-before-attach checks and matching listener/observer baselines.
The separate quiet DOM workload failed its original timing budget: p50 78.4 ms, p95 172 ms, maximum 213.2 ms, for 100 retained samples after ten warmups.
The runner recorded all samples before asserting the 50 ms limit.
Its Edge version was 153.0.4234.48, viewport 600 by 700, device scale factor 1.
Correctness and bounded attachment cleanup do not convert that performance failure into a pass.
Android separately completed 100 attachment cycles with at most nine mounted rows and 8,106 released view handles; its initial Debug timing result under concurrent load also failed.
Quiet Android phase measurements and backend hot-path fixes remain required.

The DOM follow-up passed the original 50 ms target after measured native-layout/interop changes: p50 32.8 ms, p95 44.2 ms, maximum 56.3 ms.
All 100 samples were retained after exactly ten warmups; the earlier failed samples remain part of the evidence.
That result belongs to the recorded pre-explicit-flush cohort, not an unmeasured later build.
The subsequent explicit-settlement protocol calls `FlushCommitted(expectedEpoch)` after row retirement and status/navigation feedback, with `ViewportCommitted` last.
Its combined shared tests passed 2,322 portable assertions and 40,049 math plus 30,683 controller/sample assertions.
Windows and Android native performance gates remain open.

Native Windows profiling identified expensive real HWND creation, placement, font setup, and destruction rather than shared layout arithmetic.
An owned plain-USER32 diagnostic, without XUI hooks or custom accessibility providers, created, placed, and destroyed twelve EDIT controls in 341-349 ms wall time and 156-203 ms UI-thread CPU time on this machine.
Creation alone took approximately 78-81 ms.
That diagnostic does not replace the failed application workload.
It records why the 50 ms full far-window replacement target remains an explicit platform/environment blocker rather than justification for unbounded tuning, hidden deferred work, disabled accessibility, or silent cross-key native editor reuse.

### External consumer upgrades and localization

The local preview.1 to preview.3 upgrade ran in a fresh external directory with spaces, an isolated template hive and cache, and explicit old/new package feeds.
It generated the older template, ran its shared scenarios and all three Release builds, changed only XUI package references, and checked that authored `.cs`/`.xui` hashes were unchanged.
The upgraded shared scenarios, Windows/Android/web Debug and Release builds, native/static payload byte checks, and real published Edge Wasm interaction under a nested URL passed.
Android Release output included trimming/AOT; this consumer runner did not execute a device app or a Windows GUI.
No package was published.

Review found that the compiler is a private dependency of Shared, so checking host lockfiles alone cannot prove a coherent generator version.
The guard now checks Shared as well as each restored host/test project.
Wrong private compiler, mixed runtime/adapter, and empty-package negative fixtures pass.
A fresh Web upgrade rerun exercised the corrected guard, and the retained all-three-target lockfiles were independently checked.
The first focused rerun exposed a nested PowerShell helper declaration; moving it to script scope and requiring top-level functions in the regression fixture fixed that error before the successful rerun.
Exact package/native/static hashes and the original/corrected script identities are retained with the upgrade artifacts.

The shared localization workbench uses explicit English/German/Arabic resource bundles in its main assembly.
This replaced a satellite-based draft whose browser startup behavior did not support live language buttons.
The replacement uses public `ResourceManager` APIs without private Blazor loader hooks or reloads.
The combined shared suite passed 54 assertions.
The external preview.2 consumer passed trimmed Windows execution, three missing-language negative builds, and real published browser language actions preserving the editor object, draft, and selection.
Pointer language-button activation retains normal native button focus; separate programmatic activation tests verify updates while the editor remains focused.
The Android localization consumer built a Release/AOT APK; that is not device input, geometry, or assistive-technology certification.

### Subsequent native and shared checkpoints

The combined root checkout subsequently passed native Windows Forms/Workshop (238 assertions), typography (84), and Cart/Services application fixtures (433) against its rebuilt native library.
The application fixture used fake external services only.
The later native semantic-theme and same-offset viewport-request packets were built in the root checkout; their separately reported owner acceptance is not a substitute for running each new combined native gate.
Windows' 100-reattachment resource fixture passed 655 assertions after the explicit same-offset request generated a fresh posted epoch.
That fix closed a real focus-after-commit wait, not a timeout adjustment.
It did not make the 50 ms performance target pass.

The shared workspace then passed 302 assertions including 66 literal workflow expectations after adding its Operations tab, strict session migration, and bounded snapshot scan.
Image lifecycle integration passed 2,605 combined portable assertions and 42,187 generator assertions after review reproduced and fixed two reentrancy defects.
Queued image notices cannot run during construction/reconciliation, and native interaction notices wait until the entire source operation has committed or rejected.
Native decoders and rendered image acceptance remain pending.

The first DOM Forms/Presentation packet passed its initial fixtures but review found that disposal could restore styles the backend never owned and that theme CSS could override native disabled colors.
The follow-up captures a baseline only when theme authority is acquired, preserves unowned styles, and excludes disabled controls from those overrides.
Five focused actual-browser cases passed in the root checkout, followed by six current-runtime password/theme/viewport cases after rebuilding.
The old packet alone is not the qualified state.

Cross-engine tooling now selects Chromium, Firefox, or WebKit explicitly and retains the real navigation fixtures.
The first thirteen-case Firefox and WebKit runs each passed twelve cases and failed one fifteen-second Wasm startup wait with an empty app and no recorded JavaScript error.
The corresponding unchanged, traced targeted case passed afterward in each engine.
These are preserved startup failures plus targeted evidence, not one complete green three-engine matrix or proof of the failures' cause.
Playwright WebKit is not shipping Safari certification.

### Overnight application-scale continuation

The September 23 user directive extends useful implementation through at least September 24, 04:30 at UTC-05:00, with a 05:00 review handoff.
The work is not complete merely because the four original apps have twelve screenshots.
The next shared suite includes a document workspace with genuine navigation, retained document tabs, a 10,000-record virtual catalog, responsive multi-pane layouts, local draft/session ownership, and cancellable analysis.
The framework work includes the corresponding native page/selector contracts, semantic colors and typography, explicit allocation observation, and bounded reduced-motion-aware effects.
Actual cross-platform captures and input/accessibility/lifetime evidence remain required.
No deadline changes a failed performance result into a pass, authorizes package publication, or substitutes for physical-device/IME/assistive-technology acceptance.

### September 24 combined-checkout continuation

The root checkout incorporated the source-pixel admission helper (`69FD7D40`), both-axis Reveal allocation tests (`992E5711`), image layout-independence tests (`5B4A9484`), and dialog cancellation ownership correction (`67BD9802`).
Forced Release builds passed 647 image-resource assertions, 2,881 portable assertions, and 214 Reveal assertions.
The dialog regression first reproduced a real bug: UI-thread cancellation transport failure released the native request and modal guard before native completion.
The corrected path retains attachment ownership and the one-active/input guards after a transport failure on either thread, while still faulting the task.
These tests use recording peers and fake prompts, not rendered images or actual native dialogs.

The reviewed Android stateful-tint correction (`012027D2`) was integrated and the root `Xui.Android.ReferenceCheck` forced Release build passed.
Its owner recorded 52 actual API 35 Debug assertions; this does not extend the earlier 40-assertion Release result to the new source, API 26, or other OEM palettes.
The neutral registered-font ownership packet (`AB565374`) adds three runtime files and a standalone fixture.
Its combined-checkout forced Release run passed 490 assertions with fake native tokens, including bound pins after caller disposal, cancellation/publication races, and identifiable retryable cleanup.
No production font-assignment or native registration capability follows from that result.

The root produced a complete local `0.1.0-preview.6` feed and exercised NuGet font targets in a new external directory with spaces and an isolated package cache.
Shared embedded bytes, exact font/license Android APK bytes, import order, empty declarations, duplicate-import prevention, and invalid metadata/hash diagnostics passed.
A separate unchanged-source preview.3-to-preview.6 upgrade passed shared scenarios, private Shared compiler/runtime cohort checks, all three Debug and Release host builds, payload checks, and actual published Edge interaction below a nested URL.
The feed predates the subsequent dialog fix, font-ownership helper, and native Reveal integration; it is not evidence for those later features.
It was not published, and the consumer runs did not execute a Windows GUI or Android device.

Native portable Reveal source `00FEBAC3` and Windows adapter increment `FDAF1A7F` were then integrated.
An initial native link failed because imported source timestamps left old core/accessibility objects in place; refreshing only the imported source timestamps and rebuilding closed that stale-object failure.
The root model test passed and both managed native hosts compiled against the rebuilt library.
The separately qualified owner cohort recorded 90 ABI assertions, actual exit pixels with hidden UIA Control/Content nodes, retained native EDIT identity/range, and 21 managed Reveal assertions.
RichEdit, FileList, native plugin hosts, and leased virtual viewports remain explicitly unsupported within this first Windows Reveal.
The owner's initial legacy idle-paint failure and later unchanged isolated pass remain separate evidence, not a clean full-suite pass or a performance-budget waiver.
Application-level Operations-drawer adoption and Android/browser native motion are distinct gates.

The rebuilt root native library `8A898649220AC5E07F9996F3042F7B8D959F2A2A066E4B80A4EC89C8B541E79A` subsequently passed its own coordinated desktop run: 90 portable Reveal ABI assertions, the native clipped-pixel/UIA/EDIT/focus/composition/high-contrast/idle-clock fixture, and managed Reveal 21, Studio/Operations 26, retained pages 18, and selection/range 17 assertions.
These executed the combined root source rather than copying the owner's native binary.
The new shared opt-in drawer packet (`A41238BF`) separately passed 435 shared assertions and the unchanged 80 literal workflow expectations.
It defaults off, preserving a no-Reveal graph for backends without the capability.
Review then found a real closed-drawer navigation gap: native page activation attempted to focus its hidden scope selector.
That app-level issue requires a separate corrected packet and actual drawer acceptance; it does not invalidate the default-off baseline or convert the control-level run into application-motion proof.

The mandatory four-file follow-up `17841F5A` fixes that activation path: closed drawers focus their external toggle, open idle drawers focus Scope, busy drawers focus enabled Cancel, and cancellation waiting falls back to the tab strip.
The combined shared suite then passed 460 assertions and the unchanged 80 literal expectations.
The Windows owner separately recorded 34 actual drawer assertions, including native tab selection plus Enter while the drawer remained closed, focus veto, real progress, retained editor/choice state, busy-close refusal, and a completed 10,000-document local scan.
Root application-motion execution and those later captures have their own cohort boundary.
After the default-off controls refactor, the current root Debug Studio/page/label selection passed twelve cases each on Edge, Firefox, and WebKit.
Those 36 cases precede the focus-fix rebuild and do not certify all browser suites, published motion, shipping Safari, or erase the older startup failures.

The subsequent nested published Studio selection included real toolbar navigation: Edge passed all fourteen cases; Firefox 155 and WebKit 26.6 each passed thirteen and failed the strict back-forward-cache retention case.
Both returned the fresh seeded document rather than the unsaved draft.
A separate nine-case plain-HTML probe used no XUI, Wasm, or application lifecycle code and varied `no-cache`, `private, max-age=0`, and `public, max-age=300` responses.
Edge reported persisted pagehide/pageshow and retained JavaScript object identity for all three; the automated Firefox and WebKit runs reported nonpersisted navigation and replaced identity for all three.
This narrows the observed limitation beyond Studio, but does not establish its browser/tooling cause or prove shipping-browser behavior.
The strict retention failures remain open; no engine was skipped and no assertion was weakened.
The Studio fixture now records browser version and pagehide/restore evidence before asserting retention so an eligibility failure cannot discard its diagnostic evidence.

Upstream investigation then identified an explicit tooling boundary: Playwright's [navigation documentation](https://github.com/microsoft/playwright/blob/85b947350fd16ee50e73b8d765dec378cc619982/docs/src/navigations.md#backforward-cache-bfcache) declares BFCache restoration testing unsupported and cache disabling intentional.
Its [Firefox preferences](https://github.com/microsoft/playwright/blob/85b947350fd16ee50e73b8d765dec378cc619982/browser_patches/firefox/preferences/playwright.cfg) disable parent-process BFCache, and [Juggler initialization](https://github.com/microsoft/playwright/blob/85b947350fd16ee50e73b8d765dec378cc619982/browser_patches/firefox/juggler/content/main.js) explicitly sets `docShell.disallowBFCache`.
The observed nonpersisted terminal cleanup is consistent with that policy, not evidence of a Studio-specific retention defect.
The successful Edge override remains scoped diagnostic evidence, not a promise that Playwright supports this qualification.
A supported alternative harness or real-browser procedure is still needed before claiming cross-engine cache acceptance; the strict failures and original test selection remain visible.

The root later ran `--studio-drawer-only` itself: all 34 native Operations/workspace assertions passed with its rebuilt `8A898649` library and the `FCAFF415` host plus `A41238BF`/`17841F5A` shared application.
Both inspected owner captures were copied into the durable gallery with their exact hashes; they show settled open controls, while the interaction run separately proves close/reopen behavior.
Image integration also exposed a missing asynchronous fatal-error route after Ready.
The additive optional callback packet `769E5F55` uses real Host attachment retirement rather than an ignored second decode-failure callback.
Its forced combined suite passed 2,991 assertions, including stale identities, cleanup failures, protected delivery phases, and modal/viewport retirement.
Native rendered-image pipelines still need their own qualification against that interface.

### Final application and native-resource checkpoint

The shared short-height/pane-deferral packet `FFD295C5` passed 488 assertions and the unchanged 80 literal expectations in the root checkout.
Actual root browser tests passed the seven existing Studio cases plus a height-only 914-by-800 to 914-by-314 transition.
That native DOM check requires a title at least 24 CSS pixels high, body at least 96, no bottom overflow, unchanged editor objects/focus/selection, and successful editing before restoring the larger viewport.
Android independently exposed the original wrapped-button clipping and height-starved landscape editor.
Width-first native horizontal-stack measurement fixed the caption at the measured 1x/2x widths; shared height-aware chrome fixed the documented 1x landscape case without replacing editors.

After integrating Android checkpoints `10AE7B09`, `5CB9CB85`, `5391A69B`, and `38359134`, the root preserved its newer NuGet/font packaging, added missing Studio/Page reference-check links, and built both actual APK projects.
Its final combined Gallery APK `944676EEF3B2DEDF618585D7958270A1A0DB5B6E19FA2D35F1258C16E178BF64` passed all seventeen device checks on API 35 x86_64, PID 19675.
The run includes retained edits and selected ranges across background/rotation, positive title/body landscape rectangles (118/381 physical pixels), native Library/Spinner behavior, a completed 10,000-document scan, and two Back transitions.
Four inspected root captures and their exact APK/hash boundaries are in the [gallery](portable-gallery.md#combined-checkout-android-workspace).
An earlier automated-scroll-during-scan ANR remains a separate unresolved stress risk.

The first root reinstall failed because the emulator had insufficient storage.
Only two confirmed disposable test packages were removed: the synthetic font fixture and native Orders assertion fixture.
The latter's exact external performance JSON was pulled first; Gallery/Orders demos and their private application data were preserved, as was the running emulator.
No device reset or broad cache deletion was used.

DOM image packet `73344FC7` passed fourteen root Debug cases (six image, one real fatal-detachment injection, seven Studio), and all six published image cases in Edge and Firefox.
WebKit passed four and failed two strict checks: offline-emulated Blob decoding and a one-channel, one-level difference after PNG canvas encoding and native image display.
A framework-free native probe reproduces both: raw online pixels are exact, the WebKit PNG-to-image roundtrip changes green's red channel from zero to one, and offline emulation rejects the Blob read.
Neither a decode-budget waiver nor a new color-tolerance policy was applied; these strict WebKit gates remain open.

DOM Reveal packet `D787D9C4` passed five root Debug and five nested published Release cases, including the actual Operations drawer and short-height editing, plus the public C# native-progress/RAF/listener-retirement case.
These qualify the recorded Edge cohort, not Android motion or an unrun cross-engine motion matrix.
Native Windows memory-image source `BBEF82BE` was integrated and rebuilt as root DLL `83ED88657406F2EE5A2248F4FFED889C73ED6B4FF1EA8C700AA0F307B4BD764F`; its image model fixture passed.
The separately qualified native/managed owner evidence must remain distinct from subsequent root adapter acceptance, including native GPU-failure injection versus managed protocol injection.

The Windows adapter import required follow-up `195E1199` after `0EE9651B`.
Review found that a throwing native cancellation could leave a managed pixel reservation without a cleanup owner after `ImageRequestLifetime` released its resource reference.
A peer-owned ledger now retains both native ownership and the reservation until native cancellation or containing-scope retirement is acknowledged; failures retain their charges and original errors.
The final root cohort passed 76 native image ABI assertions, actual hidden/deferred readiness and source-aspect pixels, target recovery/native fatal cleanup, 16 managed ABI assertions, 13 cleanup-failure assertions, 41 actual managed image assertions, and the 34-assertion Studio drawer regression.
The expected managed synthetic post-Ready error was reported after real Host detachment and the process exited successfully.
This closes the bounded Windows/DOM image slices, not Android Image or the two strict WebKit image checks.

Standalone font feasibility now has actual Windows, browser, and API 35 Android evidence, separately pinned by SDK evidence manifest `4201E3C3`.
The Android fixture's first metric failures were preserved: `Paint.MeasureText` rounds native advances upward, whereas `GetTextWidths` exposes fractional advances.
The corrected fixture retains its strict fractional comparison and separately checks the ceiling behavior; it passed 101 assertions with 26 owned Typeface cycles.
This does not implement production `Host` font assignment, prove API 26 execution, or replace physical input/accessibility checks.

The verified current published Studio is frozen separately from the earlier previews.
The old Order and Studio previews remain untouched; the newer preview opts into browser motion without changing those immutable assets.
All source work remains uncommitted on the user-owned integration branch, with no package publication or GitHub CI execution.
The full framework plan remains open, including Android Image/Reveal/font projection, production font assignment and native dialogs, performance/startup/memory gates, supported cache qualification, physical-device/IME/assistive-technology coverage, and release approval.

## Backend work

Use separate platform project directories under `bindings/dotnet/Experimental`.
Import `Xui.Portable.targets`.
Link `SharedDemo/Greeting.xui` as an `AdditionalFiles` item.
Construct `PortableDemo.Greeting` with a platform-thread `Host`.
Keep the generated class in the platform assembly.

Implement `IBackend`, `IElementPeer`, and `IUiDispatcher` against the public runtime types.
Keep one native input widget for each `TextInput` throughout an attachment.
Remove native listeners during peer disposal.
Report callback and dispatch errors at the platform boundary.
Do not change the shared runtime to access internal state.

The host owns one component tree and one attachment at a time.
Platform recreation can detach and attach while the host remains on the same UI thread.
A replacement UI thread requires a new host.
The platform must dispose the host before it abandons the final UI surface.

DOM execution uses local .NET WebAssembly and JavaScript interop.
An application bootstrap can use Blazor WebAssembly without authored Razor UI.
Android execution uses .NET for Android and native widgets.
Neither backend translates authored C# into another language.

## Foundation evidence, September 19, 2026

The SDK-independent runtime and generated demo passed headless event, lifetime, ownership, and error checks on Windows ARM64.
The Windows sample compiled against the real managed Windows bindings without native DLL deployment.
The tests exercise native text edits without writeback, silent setters, stable peers, stale events, and failed cleanup.
These results do not establish Android or browser execution.
The commit handoff records final assertion counts and command results.

## Windows integration evidence, September 19, 2026

The integration branch built `xui.dll` for Windows ARM64 in Release mode.
The shared Windows demo passed its `--smoke` run against that native library.
The run covered native button invocation, generated state updates, editor text, stable control identity, and window closure.
It did not establish physical keyboard input, IME behavior, screen-reader behavior, or pixel layout.

The first run exposed a missing STA entry-point attribute in the new Windows sample.
`Program.Main` now declares `[STAThread]` and reports startup errors with a nonzero exit code.
The run requires the generated executable with its Windows manifest, not direct execution of the managed DLL.
The contributor procedure contains the supported command.

## Receiving-machine baseline, September 23, 2026

The integration baseline is `c346ec5e0d3643cf765e52de67128db45c43535e`.
It merges local `main` at `8f9d7d9`, including Visual Studio discovery fixes, into the cross-platform branch.
The receiving machine runs Windows x64, .NET SDK 10.0.301, Visual Studio 2022 Preview with MSBuild 17.14.23, CMake 3.31.6-msvc6, and Windows SDK 10.0.26100.0.

All of these commands exited successfully:

```powershell
dotnet run --project bindings\dotnet\Experimental\Xui.Portable.Tests\Xui.Portable.Tests.csproj -c Release
dotnet run --project bindings\dotnet\GeneratorTests\GeneratorTests.csproj -c Release
dotnet build bindings\dotnet\Experimental\WindowsDemo\WindowsDemo.csproj -c Release -p:XuiCopyNativeRuntime=false
dotnet run --project bindings\dotnet\Experimental\Xui.Android.LayoutTests\Xui.Android.LayoutTests.csproj -c Release
dotnet build bindings\dotnet\Experimental\Xui.Android.ReferenceCheck\Xui.Android.ReferenceCheck.csproj -c Release
dotnet run --project bindings\dotnet\Experimental\Xui.Web.Tests\Xui.Web.Tests.csproj -c Release
. .\scripts\Release.Common.ps1
$cmake = Get-XuiCMake
& $cmake --build build\x64 --config Release --target xui --parallel 4
dotnet run --project bindings\dotnet\Experimental\WindowsDemo\WindowsDemo.csproj -c Release -- --smoke
```

The native command used the existing `build\x64` Visual Studio 2022 configuration with LSH enabled and required.
Portable runtime assertions: 77; generator assertions: 42,062; Android layout arithmetic assertions: 4,170; browser dispatcher assertions: 14.
Windows compilation and Android reference compilation each reported zero warnings and errors.
The final smoke command used the rebuilt native x64 library and reported `Windows shared .xui smoke passed.`
This closes the receiving-machine Windows execution gap, not physical IME, accessibility, or visual-layout acceptance.

### Integrated shared-app workflow

The new `scripts/Build-PortableDemo.ps1` ran with no arguments and exited zero after building Windows, Android, and web in Debug.
The same command with `-Configuration Release` also built all three hosts successfully, including a fresh isolated native Release build and Android trimming/AOT.
All three managed application builds reported zero warnings and errors.
.NET discovered the installed Android SDK and JDK without overrides.
The Windows build used `build\portable-demo\x64\Debug`, separate from the existing Release build.
This narrow demo disables LSH and WebView2 because its portable controls need neither feature.
The initial native Debug compilation reported the existing `src/images.cpp` C4702 unreachable-code warning; it completed successfully.

`tests/portable-build.ps1` passed after integration.
The shared Windows demo then passed `--smoke` in both Debug and Release with `--no-build --no-restore`.
SHA-256 comparisons confirmed each deployed `xui.dll` exactly matched the corresponding isolated native build.
The web `-Run` path served the integrated app at `http://127.0.0.1:5190/` with HTTP 200 and was stopped after the check.
The [DOM evidence](dom-web.md) describes the browser regressions.
After integration, all 12 Debug cases and all six published Release cases at each of `/` and `/nested/xui/` passed with clean runner exits.

This workflow builds one authored application for three hosts.
It does not deliver portable NuGet packages, an out-of-repository template, a new shared language feature, or production platform certification.

## Shared order builder, September 23, 2026

The follow-on sample keeps the greeting source and projects intact.
It adds a fixed catalog and customer form using only the existing portable controls.
`Build-PortableDemo.ps1 -Sample Order` selects the additional `WindowsOrderDemo`, `AndroidOrderDemo`, and `WebOrderDemo` projects.
The default remains `Greeting`; the native build directory is shared because both Windows samples use the same runtime.
There are no shared generator/runtime changes or dynamic collections.

`OrderState` is a sealed immutable record with three text fields, three quantity fields, and a review flag.
Quantity initializers reject values outside 0..9; text initializers reject null and NUL.
Expected form errors remain representable, with visible validation and a disabled review action.
The email rule is deliberately limited client-side syntax validation.
Money uses decimal arithmetic and explicit invariant USD formatting.
The fixed prices do not create half-cent discounts; the model's midpoint rule is explicit rather than claimed as a fixture-covered price case.
Input text is never canonicalized as a side effect of validation.

The seven JSON scenarios contain 161 literal text/enabled/visible expectations.
They cover empty and invalid orders, corrected details, raw Unicode and whitespace, coupons, quantity boundaries, review/edit/reset, and a complete order.
The final checkpoint has three coffees, two teas, and one cocoa, with $63.50 subtotal, $6.35 discount, and $57.15 total.
The corpus does not call the production model to calculate expected results.
All scenarios begin by activating Reset, retaining the same tree.
The C# runner rejects unknown/duplicate properties, unsupported versions/actions, duplicate scenario names, missing initial reset, empty expectations, and failures with scenario/step context.
JavaScript reads the same JSON rather than compiling a second business model.

The recording tests reuse the existing backend and dispatcher through a partial test program.
The integrated Release run passed 391 assertions, including the 161 scenario expectations.
Generator regressions passed 42,062 assertions.
Additional recorded-peer checks cover all three editors, exact silent reset updates, equal snapshots, disabled ancestry, detach/reattach, stale sinks, and terminal disposal.

The Windows host links the same UI/model and embeds the scenario JSON.
Its smoke runner uses a worker only to sequence bounded calls onto the real application dispatcher; the native message loop remains free to deliver Enter and Tab.
Changes go through real EDIT messages, clicks through native button messages, text through the existing control API, enabled state through the native HWND, and visibility through the existing public C ABI feature getter.
No application test handler or reflection writes the order state for scenario actions.
Separate retention checks intentionally change unrelated state to detect editor writeback.
The native run passed the 161 shared expectations plus 11 input/Tab/narrow-layout checks, then closed the window and verified all captured native handles were destroyed.
The build script's sample/platform selection, argument boundaries, failure propagation, and working-directory regressions also passed.

The device run motivated one authored-layout refinement: only the title stays fixed above the scroll viewport.
The explanatory text now scrolls with the form instead of consuming a second fixed line above the landscape keyboard.
IDs, model, and literal scenario expectations did not change.
The recording and native Windows suites were rerun against this final layout.
The integrated `-Sample Order` command built all three hosts in both Debug and Release.
Both Windows configurations passed 172 native checks against the final shared layout; Android Release included trimming/AOT, and the web Release publication passed root and non-root browser acceptance.

Android and browser host evidence belongs in their respective notes.
The sample demonstrates broader use of the existing contract, not completion of every Stage 2 fixture or the CI matrix.
Keyed dynamic children remain a later vertical slice, and physical IME/accessibility gates remain open.
