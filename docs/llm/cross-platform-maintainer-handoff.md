# Cross-platform maintainer handoff

**September 24, 2026, morning handoff (UTC-05:00). Experimental implementation, not a completed framework or release candidate.**

The user subsequently requested a local WIP preservation commit.
The commit containing this document stashes the accumulated work for the next maintainer; it is not a Git stash, release, or claim that the remaining gates pass.

One shared `.xui` UI and C# application can now run on native Windows, native Android, and a real DOM/.NET Wasm browser host.
The original shared-app goal has working examples; the full [approved completion plan](../specs/multi-platform-framework-plan.md) is still open.
The next phase is consolidation, platform parity, and hardening, not another collection of demos.

## Read this before touching the checkout

| Item | Handoff state |
| --- | --- |
| Repository | `zadjii-msft/xui` |
| Integration branch | `zadjii-msft-xui-cross-platform` |
| Local checkout | `D:\dev\private\xui` |
| Parent baseline | `c346ec5e0d3643cf765e52de67128db45c43535e` |
| Preservation checkpoint | The commit containing this handoff, titled `WIP: stash cross-platform progress for maintainer handoff` |
| Prior working tree | Before the handoff edits: 109 modified tracked files and 620 untracked files; these describe the pre-commit state |
| Delivery | Local WIP source checkpoint; no push, package publication, or GitHub CI run |

**Transfer this preservation commit, not just the parent baseline or an older remote branch tip.**
The parent includes the earlier `main` build fix, not the subsequent framework work.
This checkpoint captures the accumulated source, tests, assets, screenshots, tooling, and documentation; ignored binaries/caches and machine-local evidence are not part of it.
The branch has not been pushed, so cloning the remote must not be assumed to include the checkpoint.
Preserve any later tracked edits and untracked files separately; a Git diff or bundle alone misses untracked files.
Do not reset, clean, stash, switch branches, merge historical owner branches, or copy an owner's whole tree over the integration checkout.
Only this local checkpoint was authorized; push and publication still require separate permission.
See the [source-transfer procedure](../../CONTRIBUTING.md#preserve-an-uncommitted-integration) before moving machines.

This document supersedes the current-state and next-step interpretation of the older [cross-platform roadmap](cross-platform-roadmap.md).
The roadmap remains provenance; the [public portable contract](../specs/experimental-portable-xui.md), [Android contract](../specs/experimental-android.md), and [web contract](../specs/experimental-dom-web.md) define behavior.
The chronological [foundation evidence](portable-foundation.md) includes superseded failures and passes: later results qualify only their explicitly identified build and workload.

## What is available

| Area | Implemented and exercised | Boundary still open |
| --- | --- | --- |
| Common application surface | Portable generated components, managed retained host, native Windows adapter, Android widgets, DOM controls with local C# Wasm | Experimental opt-in surface; existing Windows APIs remain separate and supported by their own contracts |
| Composition and application lifetime | Keyed components, retained input identity, owned cancellation/resources, navigation/pages/tabs, async work, storage and service boundaries | Full device/browser lifecycle and physical-input matrix is not certified |
| Layout and controls | Axis constraints, Grid, adaptive layouts, forms, choices/ranges, typography and semantic themes | No universal control parity, complete bidi, or production packaged-font assignment |
| Resources and motion | Bounded image/Reveal contracts and qualified Windows/DOM slices; packaged font bytes and standalone native feasibility | Android rendered Image and Reveal absent; production font integration and native message-dialog projections unfinished |
| SDK/tooling | Six `Xui.Experimental.*` preview packages, `dotnet new xui-portable`, fresh external consumers, upgrade checks and CI definitions | Latest code is newer than the last preview feed; no clean remote CI or release qualification |
| Scale | Bounded realization of a 10,000-item catalog, focused/composing-row protection, viewport ownership and cleanup fixtures | Current Windows, Android, and settled-DOM latency gates fail; Android input-pressure ANR unresolved |

The four original applications are Order builder, Task board, Expense ledger, and Session planner.
All have real Windows, Android, and browser captures.
Workspace Studio/Operations adds genuine navigation, retained document tabs/editors, compact and desktop layouts, a virtual catalog, and a cancellable local scan.
The scan is sample-data computation, not remote telemetry; Studio drafts are not a promise of durable document storage.
Additional cart, profile, forms, localization, and resource workbenches are focused fixtures, not separate proof of framework completion.

Use the [screenshot gallery](portable-gallery.md) for the twelve baseline images and thirteen Studio images.
The [Studio capture manifest](images/portable-gallery/studio-capture-manifest.json) records hashes and differing source/runtime cohorts.
An older screenshot is not proof of a later feature or binary.

## Where the approved plan stands

| Milestone | Status at handoff |
| --- | --- |
| M0-M1: baseline, common host, conformance | Architecture and local integration implemented; clean-machine CI and supported-platform matrix remain open |
| M2: consumer SDK | Preview packaging/template and scoped external upgrades exercised; final integrated cohort must be repacked |
| M3: dynamic composition | Implemented and covered by shared/backend fixtures; preserve native input and failure semantics during follow-up |
| M4: controls/resources/presentation | Partially delivered; Android images/motion, production fonts, native dialogs, bidi and qualification remain |
| M5: application model/services | Substantial implementation and real app flows; broader recreation, permission, history and device acceptance remain |
| M6: scale/performance | Functional bounded virtualization; performance acceptance is not passing |
| M7: tooling/docs/samples | Real apps and gallery delivered; final author walkthrough and capability documentation depend on closure above |
| M8: release | Not complete; no support-label change, production signing, publication, or release approval |

These are progress descriptions, not milestone exit certificates.
Portable hot reload, a multi-platform Designer, portable C++/Rust, and iOS/macOS are separate scope; do not expand this handoff into those initiatives.

## Resume in this order

1. **Start from the preservation checkpoint.** Transfer this commit or take over its integration checkout, preserving any subsequent edits. Review the WIP source before treating it as supported. Rebuild affected managed/native outputs and record their identities. Use one integration writer and one interactive test owner per desktop/device.
2. **Close the Android reliability failure.** Reproduce scrolling while an Operations scan is pending, collect the native ANR/dispatcher evidence, and fix it without suppressing input. The final successful Studio runner waits for scan completion; it does not close this stress failure.
3. **Finish required vertical slices.** Implement Android Image and Reveal against existing shared contracts; connect production font registration/assignment across hosts; complete native message-dialog projections and bidi requirements. Keep unsupported capabilities explicit. Each change needs actual rendering/input/lifetime evidence, not only recording peers.
4. **Resolve scale and browser gates.** Remeasure the unchanged workload, retain failures, and investigate the WebKit image and browser-cache cases below. Do not loosen assertions or claim a passing older cohort as current.
5. **Repack the integrated SDK.** Build one matching native/managed/JS/Wasm cohort, generate a fresh consumer outside the checkout, and run unchanged-source upgrade checks. The existing preview.6 feed predates the final image, Reveal, dialog, and font-ownership changes.
6. **Qualify the declared release.** Execute the clean CI matrix and physical/device/accessibility checks, document capability gaps and compatibility policy, and seek separate release approval. A successful APK build, screenshot, or local test union is not the release gate.

### Failures and missing acceptance to retain

| Gate | Current evidence and next requirement |
| --- | --- |
| Virtualization latency | Target is p95 <= 50 ms for 100 samples after 10 warmups, from local offset request through native commit, row pruning, and explicit settlement. Current Windows/Android/settled-DOM workloads fail. An older DOM p95 of 44.2 ms belongs to a different cohort. Retain the full workload and samples; no hidden deferred work or cross-key editor reuse to manufacture a pass. |
| Android responsiveness | Repeated scroll input during pending analysis produced an ANR. The final 17-check functional workflow passed without that stress. Add a reproducible stress regression before claiming this fixed. |
| WebKit images | Two strict published cases fail: offline Blob decoding and a one-channel, one-level PNG round-trip mismatch. Framework-free probes reproduced both; this narrows the investigation, not the support boundary. No tolerance waiver or fallback codec was adopted. |
| Browser back/forward cache | Firefox/WebKit strict retention tests fail. Plain HTML also fails in the current Playwright harness, which does not support this qualification. Keep terminal cleanup distinct from retained-cache behavior and qualify with a suitable browser/harness rather than skipping the cases. |
| Broader resource budgets | Latest-cohort startup, download size, memory, frame latency, and repeated-use acceptance are not complete. Historical 100-cycle passes do not certify every later slice. |
| Devices and accessibility | Physical ARM64 execution, Android minimum API 26/current API, actual Safari, real IMEs, TalkBack/Narrator, permission/file-picker UX, and complete high-contrast/reduced-motion coverage remain open. |
| Documentation coverage | The handoff-time repository documentation check reports `Control catalog does not mention public RetainedPages from retained_pages.hpp`. Add an accurate catalog entry for that native surface; do not hide the missing coverage by excluding the type. This documentation-only handoff does not change that control or its catalog. |
| Delivery | CI definitions exist but GitHub CI was not executed. Production signing, API/version compatibility approval, and publication remain separate authorization gates. |

## Source and test map

Paths below are repository-relative. Start with the current source, not an old owner archive.

| Work | Implementation entry points | Focused evidence/fixtures |
| --- | --- | --- |
| Shared host/generator | [Xui.Portable](../../bindings/dotnet/Experimental/Xui.Portable), `Host.*`, `Contracts.cs`, `ComponentLifetime.cs`; [Xui.Generator](../../bindings/dotnet/Xui.Generator) | `Xui.Portable.Tests`, `GeneratorTests`, `Xui.Portable.ApplicationTests` |
| Studio/Operations | [WorkspaceStudio.xui](../../bindings/dotnet/Experimental/SharedDemo/WorkspaceStudio.xui), `WorkspaceStudioController.cs`, `StudioOperationsController.cs`, `StudioCatalogViewport.cs` in the same directory | [WorkspaceStudio.Tests](../../bindings/dotnet/Experimental/WorkspaceStudio.Tests); shared literal workflow corpus |
| Virtualization | `Xui.Portable\Virtualization*.cs`, `Host.Virtualization.cs`; `SharedDemo\VirtualList*.cs` | `Xui.Portable.VirtualizationTests`; Windows viewport selectors; Android `Run-VirtualListDeviceAcceptance.ps1`; DOM `virtual-*.spec.js` |
| Windows native and adapter | [Xui.Windows](../../bindings/dotnet/Experimental/Xui.Windows), [managed bindings](../../bindings/dotnet/Xui), [native source](../../src), [public headers](../../include/xui) | [WindowsPortableTests](../../bindings/dotnet/Experimental/WindowsPortableTests), native ABI/image/Reveal/viewport fixtures |
| Android native/layout | [Xui.Android](../../bindings/dotnet/Experimental/Xui.Android), `AndroidBackend*.cs`, `NativeLayout.cs`; [WorkspaceStudioActivity.cs](../../bindings/dotnet/Experimental/AndroidGalleryDemo/WorkspaceStudioActivity.cs) | `AndroidDeviceTests`, Android layout/reference projects; [Studio device runner](../../bindings/dotnet/Experimental/AndroidGalleryDemo/Run-StudioDeviceAcceptance.ps1) |
| DOM and browser lifetime | [Xui.Web](../../bindings/dotnet/Experimental/Xui.Web), `DomBackend*.cs`, `DomLayout.cs`, `wwwroot` | [WebDemo.Tests](../../bindings/dotnet/Experimental/WebDemo.Tests): `studio*`, `image*`, `reveal*`, navigation and virtual-list suites |
| Images/fonts/dialogs | `Xui.Portable\Image*.cs`, `Host.Images.cs`, `FontResource*.cs`, `MessageDialogs.cs`, `Host.MessageDialogs.cs` | `Xui.ImageResources.Tests`, `Xui.FontResources.Tests`, `Xui.FontOwnership.Tests`; backend acceptance remains separate |
| Packaging/CI | [Pack-Portable.ps1](../../scripts/Pack-Portable.ps1), [packaging](../../packaging/experimental), [template](../../templates/xui-portable), [portable workflow](../../.github/workflows/portable.yml) | `tests\portable-packages.ps1`, package-version, assets, localization, and font consumer scripts |

All experimental projects in this table are under `bindings\dotnet\Experimental`, except `GeneratorTests` under `bindings\dotnet`.
Build, run, and validation procedures live in [CONTRIBUTING](../../CONTRIBUTING.md), especially [Studio](../../CONTRIBUTING.md#workspace-studio), [application/resource checks](../../CONTRIBUTING.md#portable-application-and-resource-checks), [portable foundation](../../CONTRIBUTING.md#experimental-portable-foundation), and [preview consumers](../../CONTRIBUTING.md#experimental-preview-packages).

## Invariants that must survive follow-up

- **Keep native editors and identity.** Key plus component type determines retained identity. Unrelated refresh, reordering, navigation, theme changes, and virtualization must not replace an active editor or silently normalize its text/selection/composition.
- **Keep ownership and errors explicit.** Precommit rejection differs from a committed-model/native failure. Postcommit native failure detaches the host and reports the original error. Cancellation transport failure is not cleanup acknowledgement; native image/pixel and modal-request ownership must remain until acknowledged retirement.
- **Respect lifetime boundaries.** Ordinary detach is not permanent component retirement. Retire child scopes and owned work/resources without skipping later cleanup after an error. Attachment-owned passwords are cleared, not restored as ordinary state.
- **Retain the viewport protocol.** Source versions and request epochs guard publication. Materialize before exposing an offset; clear realized rows in detached preparation before reattachment. Windows uses viewport-sized rebasing because a giant child HWND hits native geometry limits. Focus success alone does not prove visible rows.
- **Do not overpromise native atomicity.** Intermediate HWND/accessibility observations can occur. The contract does not promise an atomic OS-wide accessibility snapshot.
- **Preserve resource accounting.** Encoded-source admission and decoded-pixel reservations are distinct; retain charges through uncancelable native work and failed cleanup. Hidden images can become Ready without first paint; contain uses original source aspect. No opaque engine/GPU memory cap is promised.
- **Keep capability limits visible.** The Operations drawer is opt-in and defaults off. Unsupported Reveal subtrees and missing platform services must reject explicitly, not silently degrade or change the shared UI tree.
- **Keep user data out of acceptance.** Use owned fake streams/data and explicit devices. Do not read/write the user's clipboard, open their files, accept SDK licenses, or remove applications to make a test pass.

## Last qualified integration evidence

These are prior execution results from the overnight integration, not a fresh full-suite run for this documentation handoff.
The [foundation record](portable-foundation.md#september-24-combined-checkout-continuation) and platform notes retain the detailed chronology and limits.

| Cohort | Qualified scope |
| --- | --- |
| Combined Android Studio Debug APK | 17 actual API 35 x86_64 checks: native navigation/tabs, retained edits and selection through background/rotation, positive short-landscape editor bounds, bounded catalog, Spinner/local scan and Back. Not the concurrent-scroll stress case. |
| Combined Windows | Studio drawer workflow and final native/managed image ABI, pixels, hidden/deferred readiness, target recovery, host-detach and cleanup-failure ownership fixtures passed. Native GPU-failure and managed synthetic-failure cases are separate. |
| Combined DOM | Studio/Reveal Debug and nested published Release plus clock/listener retirement passed on Edge. Published image cases passed on Edge/Firefox; WebKit remained 4/6. No full browser-matrix success claim. |
| Shared managed | Portable/generator and adjacent application/resource/ownership suites passed their recorded forced rebuilds. Recording backends are not native renderers. |
| Local preview.6 SDK | Six-package feed, font-byte consumer and unchanged-source preview.3-to-preview.6 upgrades passed their scoped builds/payload/browser checks. This older feed does not contain all final features. |

Final root native DLL SHA-256: `83ED88657406F2EE5A2248F4FFED889C73ED6B4FF1EA8C700AA0F307B4BD764F`.
Final root Android Studio APK SHA-256: `944676EEF3B2DEDF618585D7958270A1A0DB5B6E19FA2D35F1258C16E178BF64`.
These identify prior binaries built before the preservation commit; do not substitute them for rebuilding its source.
Imported ZIP timestamps previously caused stale C#/C++ outputs and false-looking incremental passes.
Force the affected managed rebuild and ensure native objects are rebuilt before using `--no-build`.

## Local machine and process handover

The recorded environment is Windows x64, .NET SDK 10.0.301, VS 2022 Preview, Android SDK at `C:\Program Files (x86)\Android\android-sdk`, and JDK at `C:\Program Files\Android\openjdk\jdk-21.0.8`.
The running AVD is `pixel_7_-_api_35_0`, API 35 x86_64, serial `emulator-5554`.
Do not wipe it or assume exclusive control without transferring device ownership.
GUI acceptance changes focus/foreground state and must be serialized; avoid simultaneous heavy builds when recording performance.

| Existing local preview | Purpose and owner |
| --- | --- |
| <http://127.0.0.1:5190/> | Older frozen Order; previous integration session |
| <http://127.0.0.1:5193/?app=studio> | Older frozen Studio; previous integration session |
| <http://127.0.0.1:5194/?app=studio&motion=1> | Latest frozen 249-file Studio publication with opt-in browser motion; hosting transferred to the previous integration session |

These are local process-owned conveniences, not deployed sites or portable handoff artifacts.
The latest server exited between turns in the final integration session, including after an initial successful restart.
Hosting was transferred to the previous integration session (`c352351e-2671-434d-bafe-93412f2db289`), which reported a loopback listener and HTTP 200 from the same frozen files.
Its attached shell is `studio-preview-immutable-5194`; the source remains in the final integration session's artifact directory.
This is not a promise of availability after CLI/session shutdown; rebuild and serve a fresh cohort when taking over.
Do not hot-replace assets beneath existing Wasm pages, reload the user's page, or archive owner sessions to silence notifications.
Use a free port for new work; preserve these processes until their owner and replacement are agreed.
Parent and final integration coordination automations were cleared at handoff.
All nine child worktrees remain preserved; this local checkpoint records the integration checkout without committing those separate worktrees or their artifacts.

### Optional local recovery evidence

The durable entry points are this document, the platform notes, source/tests, and the gallery.
Detailed logs, source packets, prior feeds, and frozen previews additionally live under `C:\Users\zadji\.copilot\session-state\<session-id>\files`.
Those directories are machine-local and are not transferred by Git.
Retain relevant failing logs/raw timing samples and package evidence before retiring the machine or its sessions; if unavailable, rerun rather than treating the historical result as reproduced.

| Historical owner | Session ID |
| --- | --- |
| Final root integration / latest preview files | `50e820cd-1bfc-4d7c-8148-d84409dc3bc4` |
| Previous integration / all three preview servers | `c352351e-2671-434d-bafe-93412f2db289` |
| Core/generator | `2e39867f-a5b4-4da0-8940-461b6e26d1e1` |
| Android/emulator | `90215318-7fd2-4b3f-af98-741f80cc08a0` |
| DOM | `6c5ccb92-73ab-42f8-b6bf-42831b6e6607` |
| Windows adapter | `1bcbbbb2-4c6f-4dab-a344-6d417bb28513` |
| Native C++/ABI | `55466391-9d85-4918-8530-03d558a120b2` |
| Shared applications | `3c3275e9-bd9f-463e-808a-ea55fb2ded35` |
| SDK/resources | `8cef353f-d60c-4475-a8d7-ed68ef8ff15d` |
| Virtualization | `b6735c3a-cda5-4c5d-9af2-afeda41d3816` |
| Presentation/review | `fc3922a7-70bd-4eef-855e-e0dbef31ceaa` |

In the final integration folder, start with the **LATEST UPDATE** in `integration-handoff.txt`.
Useful evidence includes `root-android-studio-final.log`, `root-native-image-final-actual.log`, `preserved-device-virtual-performance.json`, `preview-cohort-6\evidence.json`, and the frozen `studio-published-final-v2` directory.
Only confirmed disposable test APKs were removed during the earlier storage recovery; demo data and the emulator were preserved.
No cleanup or further feature work is implied by this handoff.
