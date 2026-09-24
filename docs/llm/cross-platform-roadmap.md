# Cross-platform roadmap and developer handoff

**For the September 24 continuation, use the [current maintainer handoff](cross-platform-maintainer-handoff.md).**
The overnight framework implementation is captured in a later local WIP preservation commit; cloning an older remote branch tip does not include that checkpoint.
The original published provenance and starting-state descriptions below remain historical.

Handoff date: September 23, 2026.
Repository: <https://github.com/zadjii-msft/xui>.
The starting branch is `zadjii-msft-xui-cross-platform`.
This document records the integrated experiment and proposes the remaining implementation stages.
It does not declare production Android or browser support.

The [multi-platform framework completion plan](../specs/multi-platform-framework-plan.md) expands the remaining work into a proposed supported release, including host unification, dynamic composition, core controls, application services, packaging, CI, and release gates.
Use that proposal for forward delivery planning; this document retains the experiment's provenance, original stages, and measured handoffs.

The [receiving-machine continuation](#receiving-machine-continuation-september-23-2026) records subsequent work on this branch.
Use the [single build/run workflow](../../CONTRIBUTING.md#build-one-app-for-windows-android-and-web) to build the shared application for all three hosts.
The original provenance and blocker sections below remain historical evidence.

## Start here

1. Use the [new-machine checkout procedure](../../CONTRIBUTING.md#continue-cross-platform-development).
2. Read the [portable contract](../specs/experimental-portable-xui.md).
3. Read the [Android contract](../specs/experimental-android.md) and [DOM contract](../specs/experimental-dom-web.md).
4. Run the SDK-only baseline in [Stage 0](#stage-0-reproduce-the-integrated-baseline).
5. Complete platform acceptance before expanding the supported language.

The integrated branch contains all source, tests, and documentation from the three experiment branches.
The receiving developer does not need the original machine, Copilot sessions, caches, or worktree paths.
Generated binaries and installed SDKs are not part of the handoff.
The previous loopback preview stopped. Its URL is not a hosted deployment.

## Branches and commit provenance

The handoff publishes these four branches to `origin`.
Only the integration branch contains the complete result.

- **Continue development:** `zadjii-msft-xui-cross-platform`.
  The implementation snapshot before this roadmap is `125b8c61e3e0706a93d80cc7f7aa7a9c0b52b7a0`.
  The branch also contains the later roadmap commit.
- **Foundation snapshot:** `zadjii-msft-portable-xui-foundation`.
  Tip: `db82425e77c745bfe24a6685d5f846146d30f435`.
  This commit is an ancestor of the integration branch and both platform branches.
- **Android snapshot:** `zadjii-msft-xui-android-experiment`.
  Tip: `7e309eacd3b21b0f4faffa2c61130791fbf7162b`.
  The integration branch contains its cherry-pick as `71cb92094312a4f3ab2c391885deb9350a7b6753`.
- **DOM snapshot:** `zadjii-msft-xui-dom-web-experiment`.
  Implementation: `54eebcdc2cc1ba5f0793e8958caad95e53db4c65`.
  Documentation tip: `862da127ba51cb9fc8f337fd3c7d42577b24e5f8`.
  Their integration commits are `9f1bdba2f1666891e2e80a421d1899db395ef736` and `125b8c61e3e0706a93d80cc7f7aa7a9c0b52b7a0`.

The common mainline baseline is `84b17c80b2504594b263e3ec49124005c7461a90`.
It includes the mainline merges through #45.
Mainline commits after that baseline are not part of this provenance claim.

Additional integration-only commits:

- `ff440d73eba0ab048fc4eee82451aae44f4d5623`: Native Windows smoke mode and the required STA entry point.
- `3ed161ad7affbea7cf905dacf06f8212d12b92df`: Android handbook navigation.
- `dfa883c509c3af4c0d89521730e6c0465efa6466`: Root README link to the experiment.
- `26d1109ddf07c9b42b1512e5571964f89583d819`: Browser-test lockfile without machine-specific registry URLs.

Do not merge or cherry-pick the historical platform branches into the integration branch again.
Preserve those branch tips as evidence.
Create new topic branches from the latest integration branch instead.
No pull request, release tag, package publication, or mainline merge forms part of this handoff.

## What exists today

The experiment shares `.xui` layout and ordinary compiled C# behavior.
It does not port the C++ runtime or its entire control library.
The exact application source is [SharedDemo/Greeting.xui](../../bindings/dotnet/Experimental/SharedDemo/Greeting.xui).
All three application projects link that file rather than copy its UI.

The supported subset contains `VStack`, `HStack`, `Text`, `Button`, `TextInput`, and vertical `ScrollView`.
It supports state-driven properties, click/change/submit handlers, spacing, padding, and limited size/flex rules.
Unsupported language features produce source-located diagnostics.
Dynamic children, styles, hot reload, and full control parity are absent.

Windows remains the default generator profile and uses the existing native bindings.
The opt-in `Portable` profile targets `Xui.Experimental.Portable`.
Android maps retained elements to native widgets through .NET for Android.
Web maps retained elements to real DOM controls through local .NET WebAssembly.
Blazor supplies browser startup, not a second authored Razor UI.
Authored C# does not become JavaScript or require server-side event handlers.

### Source ownership

- **Shared language and runtime:** [foundation source map](portable-foundation.md#source-map).
  Primary files are `Xui.Generator/XuiGenerator.cs`, `Experimental/Xui.Portable.targets`, and `Experimental/Xui.Portable/{Contracts,Elements,Host}.cs`.
- **Native Android:** [Android source map](android-experiment.md#source-map).
  Adapter files are in `Experimental/Xui.Android`, with startup in `Experimental/AndroidDemo/MainActivity.cs`.
- **DOM browser:** [DOM source map](dom-web.md#source-map).
  Adapter files are in `Experimental/Xui.Web`, with startup in `Experimental/WebDemo/Program.cs`.
- **Windows acceptance:** `bindings/dotnet/Experimental/WindowsDemo/Program.cs`.
  Its `--smoke` path uses real native controls and the executable's Windows manifest.

The paths beginning with `Experimental` or `Xui.Generator` are relative to `bindings/dotnet`.
The source maps identify the associated managed, native, and browser tests.

### Invariants for every stage

- Keep the Windows default and existing bindings compatible.
- Keep one shared `.xui` application source and target-specific startup.
- Preserve native input identity, selection, composition, accessible names, and keyboard behavior during unrelated updates.
- Keep programmatic text changes silent. Reject delayed native echoes.
- Keep stale event sinks inactive after detach, reattachment, and disposal.
- Unmount the backend first. Let the host dispose peers in reverse creation order.
- Remove listeners and interop handles during peer disposal.
- Surface callback, dispatch, update, and cleanup failures explicitly.
- Complete or fault every accepted `Host.DispatchAsync` task.
- Keep shared API changes synchronized across the generator, runtime, both adapters, tests, and public contracts.

The public contracts define the exact semantics and exceptions.
In particular, state refresh is synchronous but not transactional across several bindings.
An update failure detaches the backend and retains the current model state.

## Recorded baseline and open blockers

The original evidence dates from September 19, 2026.
The source and branch inventory were inspected again for this handoff.
The results below are historical acceptance evidence, not fresh runs on the receiving machine.

- Shared runtime: 77 assertions passed against the actual generated demo.
- Generator: 42,062 assertions passed, including Windows and portable compilation.
- Windows: ARM64 native build and the shared-source `--smoke` run passed.
- Android: official-reference compilation passed with zero warnings or errors. Layout arithmetic passed 4,170 assertions.
- Web: Debug and Release builds, Release publication, and 14 bounded dispatcher assertions passed.
- Browser: all 12 cases passed individually, including actual generated C# Wasm behavior.
- Integrated documentation: 24 unit tests passed. Link checks covered 57 pages and 732 local links.

**Android blocker:** The real application build stopped at `NETSDK1147` because the Android workload was absent.
The official `Microsoft.Android.Ref.36` package, version `36.1.69`, supplied compile-only evidence.
That evidence does not establish APK packaging, Java integration, deployment, or native execution.
The device assertion app exists but did not run.
IME, TalkBack, rotation, focus restoration, and native layout acceptance remain open.

**Browser blocker:** Playwright 1.63.0 completed the individual cases but stalled in final `browser.close()`.
An independent `about:blank` probe reproduced the timeout without XUI or Wasm.
Native Edge with `--disable-gpu` produced the most complete application evidence.
This is not a clean test-runner exit or proof of an application cleanup defect.
See the [shutdown evidence](dom-web.md#browser-shutdown-limitation).

**Manual acceptance:** Synthetic input and composition checks do not establish physical IME or screen-reader behavior on any platform.
Windows smoke also does not establish pixel layout or physical keyboard acceptance.

The original host used Windows ARM64, .NET SDK 10.0.401, and Node.js 25.6.1.
The browser package references are 10.0.12.
These versions record the environment, not a complete support matrix.
Use the checked-in manifests and an approved package source.
Do not copy machine-specific registry configuration or disable TLS.

## Receiving-machine continuation, September 23, 2026

Integration baseline `c346ec5e0d3643cf765e52de67128db45c43535e` merges local `main` at `8f9d7d9`.
This includes the receiving machine's Visual Studio discovery fixes as well as the latest fetched mainline changes.
The historical platform commits were not reapplied.
Follow-on source changes are kept on the current integration branch; no package publication or production-support decision is implied.

The machine runs Windows x64 with .NET SDK 10.0.301 and an installed Android workload.
The Android emulator uses API 35, x86_64.
Browser acceptance uses installed Microsoft Edge 153.0.4234.48 with Playwright 1.63.0.
Exact tool versions, commands, results, and remaining limits are in the linked platform notes.

| Area | Receiving-machine result | Still outside this evidence |
| --- | --- | --- |
| Stage 0 shared/Windows baseline | Portable runtime, generator, Android arithmetic/reference, browser dispatcher, native Windows build, and shared-source Windows smoke pass | Physical Windows IME, screen reader, and visual layout |
| Stage 1A Android automation | Actual APK build/deployment, 30 native assertions, and 24 device checks pass on the existing API 35 AVD, including keyboard-visible layout, rotation, and background restoration | API 26/current-API support matrix, physical-device IME and TalkBack |
| Stage 1B browser automation | Clean Debug runner and published Release runs at `/` and `/nested/xui/`, including offline C# callbacks, terminal navigation, and actual back-forward cache restoration | Physical IME, Narrator, browser zoom, and a broader browser/architecture matrix |
| Repository-local author workflow | `Build-PortableDemo.ps1` builds the same `.xui` app for Windows, Android, and web; selected-platform run commands and command/failure regressions are available | Versioned portable packages and an out-of-repository template |

The Android fix gives actual hardware key events precedence over `ImeAction.Done`, which Android 14 and later can also report for hardware Enter.
The input requests an in-place keyboard, and the demo applies current system-bar, cutout, and IME insets so native scrolling keeps controls reachable in landscape.
The browser fix changes the sample's HTML base URL to directory-relative, allowing one published site to work at the domain root or below a directory prefix.
Browser tests no longer require the old unconditional GPU-disable workaround.
The historical ARM64 teardown failure is not declared fixed by x64 success.

The build command was exercised against all three real toolchains in both Debug and Release.
The matching Windows native DLLs were checked and both resulting configurations passed the native smoke.
Android Release included trimming/AOT; browser publication was exercised without the Debug test bridge.
The integrated checkout's Android Debug APKs were then deployed through the selected-serial commands and passed the full native/device procedure again.
This demonstrates one authored application with three platform hosts, not one executable that runs unchanged everywhere.
The shared source and portable language subset remain unchanged.

See [foundation and integrated workflow evidence](portable-foundation.md#receiving-machine-baseline-september-23-2026),
[Android device evidence](android-experiment.md), and [browser evidence](dom-web.md).
The full Stage 1 acceptance gates remain open for the manual and platform-matrix work above.
Stage 2 conformance fixtures/CI and Stage 3 external consumer packaging remain follow-on work.
The build convenience command is not a claim that those stages are complete.

### Shared order-builder follow-on

The next application-level conformance slice adds `SharedDemo/OrderBuilder.xui` and `OrderModel.cs`.
The greeting remains the default smoke sample.
`Build-PortableDemo.ps1 -Sample Order` builds separate native Windows, native Android, and browser hosts from those same sources.
There is no server, purchase flow, second platform UI definition, or portable language expansion.

The order builder exercises three retained inputs, visible validation, immutable C# state, dependent decimal totals, coupons, quantity limits, disabled actions, review/edit/reset, and long scrolling content.
Seven shared scenarios supply 161 literal expectations to the recording backend and each real platform.
Platform-specific checks additionally cover native input state, narrow layouts, keyboard navigation, Android focused-field recreation/backgrounding, and browser offline behavior and real navigation lifetime.
The sample also exposed an overlapping browser error pane and an unnecessarily short landscape keyboard viewport; the host error layout and shared fixed-header allocation were corrected.

This is a concrete start on Stage 2's shared conformance work, not completion of its full matrix or CI jobs.
The [public sample guide](../specs/experimental-portable-xui.md#order-builder-sample), [contributor procedures](../../CONTRIBUTING.md#order-builder-conformance), and [shared implementation evidence](portable-foundation.md#shared-order-builder-september-23-2026) describe the delivered scope.
Real keyed insertion/removal/reordering remains a later vertical slice with explicit identity, focus, event lifetime, and disposal semantics.
The fixed catalog must not be represented as dynamic collection support.

## Roadmap

These stages are proposed work, not additional delivered features.
The suggested topic branches do not exist yet.
Every topic starts from the latest integrated baseline unless it explicitly depends on another unmerged topic.
The Android and browser acceptance stages can proceed in parallel after Stage 0.

### Stage 0: Reproduce the integrated baseline

Suggested topic: `xui-portable-baseline`.

1. Complete the [new-machine procedure](../../CONTRIBUTING.md#continue-cross-platform-development).
2. Run the [portable baseline](../../CONTRIBUTING.md#experimental-portable-foundation), including generator regressions and Windows compilation.
3. Run the Android arithmetic and reference checks from the [Android procedure](../../CONTRIBUTING.md#experimental-android-backend).
4. Run the managed browser checks and Release publication from the [web procedure](../../CONTRIBUTING.md#experimental-dom-web).
5. On Windows, build the matching native architecture and run the Windows smoke executable.
6. Record commands, exit codes, tool versions, architecture, and failures in the relevant maintainer notes.

Exit gate: Each reproducible baseline command passes, or a specific environment blocker has a recorded reproduction.
A Windows source compilation alone does not satisfy the native smoke gate.
No SDK or browser cache from the original machine is necessary.

### Stage 1A: Establish real Android acceptance

Suggested topic: `xui-android-device-acceptance`. Dependency: Stage 0.

1. Provision .NET for Android, a compatible JDK, the Android SDK, and a device or emulator through approved installation procedures.
2. Build and deploy both `AndroidDemo` and `AndroidDeviceTests`.
3. Run the native assertion app and the complete [device smoke procedure](../../CONTRIBUTING.md#android-device-smoke-procedure).
4. Fix production failures in the adapter or Activity, with regression coverage for each failure.
5. Record API level, device architecture, density, font scale, keyboard, and TalkBack version.

Exit gate: APK construction, deployment, native assertions, and manual input/accessibility/lifecycle acceptance pass.
Cover at least the minimum supported API 26 and a current Android API in the eventual support matrix.
Use a physical device for at least one IME and TalkBack acceptance run.
Current recreation support restores authored state, focus, and selection, but not composition, keyboard visibility, or scroll position.
Do not broaden that contract without an explicit design and device evidence.

### Stage 1B: Establish clean browser acceptance

Suggested topic: `xui-dom-browser-acceptance`. Dependency: Stage 0.

1. Install the locked test dependencies with `npm ci` through the [web procedure](../../CONTRIBUTING.md#experimental-dom-web).
2. Run the existing suite on the receiving machine with a supported browser.
3. If teardown stalls, isolate browser startup and shutdown with a blank page before changing XUI lifetime code.
4. Record a complete runner exit, including teardown, rather than individual test counts alone.
5. Run the published Release site through a static host with the correct Wasm MIME type.
6. Exercise physical IME input, keyboard navigation, zoom, narrow viewports, and a screen reader.
7. Propose a browser support matrix and add coverage for each declared browser.

Exit gate: The full automated runner exits successfully without blanket retries, ignored errors, or abandoned browser processes.
Release acceptance covers local C# callbacks after network disconnection, input identity, error surfaces, and navigation/disposal.
Back-forward cache acceptance must include real browser navigation, not only synthetic `pagehide` events.
Do not turn the environment-specific `--disable-gpu` workaround into a product requirement without evidence.

### Stage 2: Make the portable contract repeatable

Suggested topics: `xui-portable-conformance`, then `xui-portable-ci`.
Dependencies: Both Stage 1 acceptance results.

1. Add small shared fixtures for each supported property, event, layout rule, and lifecycle boundary.
2. Run those fixtures against the recording backend and both real platform backends.
3. Cover duplicate IDs, disabled ancestry, malformed values, partial construction, delayed echoes, and cleanup failures.
4. Cover disposal during queued dispatch and repeated attach/detach without retained native resources.
5. Add scoped CI jobs for managed regressions, Windows smoke, browser build/tests, and Android build/device assertions.
6. Retain failure logs and relevant artifacts with bounded job timeouts.

Exit gate: Clean checkouts reproduce the declared contract on the selected platform matrix.
Reference-only Android compilation cannot substitute for the device job.
Physical IME and accessibility procedures remain explicit release gates where automation cannot establish behavior.
The current workflows cover documentation and Windows releases, not this portable acceptance matrix.
CI changes must not publish packages or accept new deployment obligations implicitly.

### Stage 3: Make application authoring usable outside this repository

Suggested topic: `xui-portable-projects`. Dependency: Stage 2.

1. Define the supported project structure for shared `.xui`/C# source and thin platform entry points.
2. Design versioned packages for the generator, portable runtime, and adapters.
3. Replace repository-relative consumer imports with package build assets.
4. Provide a project template with a shared application and Windows, Android, and browser hosts.
5. Document platform-dependent C# APIs, resources, startup errors, debugging, and deployment.
6. Exercise template restore, build, and run from a fresh directory outside the XUI checkout.

Exit gate: An application author creates the shared demo without copying runtime code or editing generator internals.
The Windows project retains its existing profile and native deployment requirements.
Browser assets resolve correctly under a non-root deployment path.
Package versions and target compatibility are explicit.
The `Experimental` namespace and profile remain until a separate compatibility decision.

### Stage 4: Expand the language in vertical slices

Suggested topic family: `xui-portable-feature-<name>`. Dependency: Stage 3.

1. Choose the next feature from an actual shared application requirement.
2. Write its public behavior and platform limits before changing the runtime contract.
3. Implement the generator, runtime, Android peer, and DOM peer in the same integration sequence.
4. Add a shared fixture and platform acceptance before exposing the feature.
5. Preserve source-located rejection for everything outside the declared subset.

Candidate slices include conditional content, keyed collections, shared resources, typography, images, additional form controls, and navigation.
Dynamic children require explicit identity, event lifetime, focus, accessibility, and disposal semantics.
Asynchronous resources require explicit ownership, cancellation, and stale-result handling.
Hot reload requires a separate lifecycle design.
These are candidates, not a commitment to full Windows control parity.
C++ runtime portability and additional platforms remain separate proposals.

Exit gate for each slice: One authored example works on all declared targets without a second platform-specific UI definition.
Native input and accessibility remain intact.
Platform differences are documented rather than hidden behind silent fallbacks.

### Stage 5: Define production readiness and release policy

Suggested topic: `xui-portable-release-readiness`.
Dependencies: Stage 3 and the Stage 4 slices selected for the initial release.

1. Measure cold startup, download or package size, steady memory, update latency, and native-resource retention.
2. Record the device/browser matrix and set acceptance budgets from those measurements.
3. Exercise trimming, optional AOT, static hosting, Android packaging/signing, and failure recovery.
4. Review untrusted text, asset loading, interop boundaries, accessibility, localization, and reduced-motion behavior.
5. Define versioning, compatibility, support policy, and the process for platform-specific defects.
6. Require explicit release approval before package publication or removal of experimental labels.

Exit gate: The selected feature set meets documented correctness, accessibility, performance, packaging, and support requirements.
Optional optimization must not replace correctness acceptance.
The initial release can remain a deliberately small portable subset.

## Coordination and integration

Keep one owner for shared generator/runtime changes.
Give Android and DOM owners separate adapter paths after the shared contract is agreed.
When the contract changes, integrate it first and give both adapter owners the exact commit.
Do not let either adapter depend on internal runtime fields.

Use one reviewable topic per acceptance fix or feature slice.
Record its base commit, source changes, acceptance commands, actual outcomes, and remaining limits.
Integrate both adapters before declaring a shared feature complete.
Rerun affected regressions after integration rather than relying only on isolated branch results.
Keep implementation history here, public contracts in `docs/specs`, and reusable procedures in `CONTRIBUTING.md`.

## Prompt for the receiving developer or coding agent

> Continue the XUI portable experiment from `origin/zadjii-msft-xui-cross-platform`.
> Read `docs/llm/cross-platform-roadmap.md` and the three linked public contracts.
> Do not repeat the historical platform cherry-picks.
> Start with Stage 0, then establish real Android and clean browser acceptance.
> Preserve the exact shared `.xui` source, native text editing, accessibility, ownership, and explicit errors.
> Keep platform execution gaps explicit.
> Record each completed gate with reproducible commands and environment details.
