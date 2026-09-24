# Android experiment handoff

The [public Android contract](../specs/experimental-android.md) describes native mapping and lifetime.
The [contributor procedures](../../CONTRIBUTING.md#experimental-android-backend) contain build and device commands.
The implementation uses the shared foundation without runtime or generator changes.

## Source map

- `bindings/dotnet/Experimental/Xui.Android/AndroidBackend.cs`: Attachment, native peers, event suppression, accessible properties, and cleanup.
- `bindings/dotnet/Experimental/Xui.Android/AndroidDispatcher.cs`: Android main-looper dispatch.
- `bindings/dotnet/Experimental/Xui.Android/NativeLayout.cs`: Native stack measurement, element constraints, and disabled scroll input.
- `bindings/dotnet/Experimental/Xui.Android/LayoutMath.cs`: Density conversion, measured-dimension saturation, and legacy integer-allocation arithmetic checks.
- `bindings/dotnet/Experimental/Xui.Android/NativeMeasure.cs`: Pixel offers and semantic unbounded-context propagation through native measurement caches.
- `bindings/dotnet/Experimental/Xui.Android/NativeGridLayout.cs`: Native Grid view group consuming the shared column/row/span solver.
- `bindings/dotnet/Experimental/AndroidDemo/MainActivity.cs`: Shared component startup, attachment lifetime, and saved state.
- `bindings/dotnet/Experimental/AndroidDeviceTests/TestActivity.cs`: Real-widget checks for authored events, text spans, scrolling, layout, dispatch, and teardown.
- `bindings/dotnet/Experimental/AndroidDeviceTests/Run-DeviceAcceptance.ps1`: Bounded selected-device checks against the installed native assertion app and actual demo, including keyboard-visible layout and Activity recreation.
- `bindings/dotnet/Experimental/AndroidOrderDemo`: Separate shared-order Activity, complete authored-state/focused-field restoration, and keyboard-aware surface.
- `bindings/dotnet/Experimental/AndroidOrderDeviceTests`: Shared literal scenario driver over native widgets, per-editor/lifetime checks, and `Run-OrderDeviceAcceptance.ps1`.
- `bindings/dotnet/Experimental/AndroidOrderDeviceTests/TestActivity.Dynamic.cs`: Exact shared mutation corpus plus native order, composition, removal, failure, and recovery probes.
- `bindings/dotnet/Experimental/AndroidOrderDeviceTests/TestActivity.Gallery.cs`: Shared gallery corpora over native widgets.
- `bindings/dotnet/Experimental/AndroidGalleryDemo`: Three typed Activity hosts and `Capture-Gallery.ps1` for real sample-window PNGs.
- `bindings/dotnet/Experimental/Xui.Android/NativeValues.cs`: Native binary/mixed choices and authored-unit progress accessibility.
- `bindings/dotnet/Experimental/Xui.Android/AndroidPlatformServices.cs`: Activity-scoped clipboard/URI bridge with explicit permission, cancellation, and lifetime handling.
- `bindings/dotnet/Experimental/AndroidOrderDeviceTests/TestActivity.Values.cs` and `TestActivity.Services.cs`: Shared primitive corpus, drawable/accessibility probes, and isolated service tests.
- `bindings/dotnet/Experimental/AndroidGalleryDemo/ProfileWorkspaceActivity.cs`: Private-store host, transient-session recreation, weak error reporting, and native Back.
- `bindings/dotnet/Experimental/AndroidOrderDeviceTests/TestActivity.Profile.cs`: Off-UI shared corpus over real widgets and a uniquely owned test directory.
- `bindings/dotnet/Experimental/Xui.Android.LayoutTests`: SDK-only tests of the production arithmetic.
- `bindings/dotnet/Experimental/Xui.Android.ReferenceCheck`: Compile-only checks of the exact adapter, demo, and native test sources.

The native test app links the same `SharedDemo/Greeting.xui` as the demo.
Its additional layout fixture constructs runtime elements to exercise constraints.
It does not replace Android classes with mocks.
Its native text assignments exercise Android listeners, not physical keyboard or IME input.
The manual device procedure remains necessary.

## Emulator acceptance, September 23, 2026

Baseline: `c346ec5e0d3643cf765e52de67128db45c43535e`, the integration merge of local main build fixes.
The results below include the uncommitted Android acceptance changes on top of that baseline.
Both actual applications built and deployed successfully; this is no longer reference-only evidence.
No tools were installed, licenses accepted, elevation used, AVD data wiped, or unrelated processes terminated.

### Environment and commands

- Host: Windows x64, Windows build `10.0.26200`, .NET SDK `10.0.301`.
- Android workload: `36.1.43/10.0.100`, installed by Visual Studio `18.8.11925.187`.
- SDK: `C:\Program Files (x86)\Android\android-sdk`, platforms 35 and 36.
- JDK: Microsoft OpenJDK `21.0.8+9-LTS`, `C:\Program Files\Android\openjdk\jdk-21.0.8`.
- Emulator: `35.5.10.0`, build `13402964`; adb `1.0.41`, package `36.0.0-13206524`.
- Existing AVD: `pixel_7_-_api_35_0`, Android 15 / API 35, Google Play x86_64 image, serial `emulator-5554`.
- Display: 1080 x 2400 portrait / 2400 x 1080 landscape; density 420 dpi; font scale 1.0.
- IME: `com.google.android.inputmethod.latin/.LatinIME`, Gboard `18.1.3.962075747-release-x86_64`.
- Accessibility package: `com.google.android.marvin.talkback`, installed version `15.0.0.639625893` (system preload `14.2.09.629370537-preload-x86_64`).
  No accessibility service was enabled; TalkBack was not exercised.

Reproduction from the repository root with the already-provisioned tools:

```powershell
$sdk = 'C:\Program Files (x86)\Android\android-sdk'
$jdk = 'C:\Program Files\Android\openjdk\jdk-21.0.8'
$adb = Join-Path $sdk 'platform-tools\adb.exe'

# Run in a separate, session-attached terminal; do not start a duplicate AVD.
& "$sdk\emulator\emulator.exe" -avd pixel_7_-_api_35_0 -port 5554 -no-snapshot-save -no-boot-anim
```

The emulator was started attached to the acceptance session, not as a detached service.
It was left running for the parent integration's selected-serial rerun.
`-no-snapshot-save` does not wipe userdata.
Wait for `adb -s emulator-5554 shell getprop sys.boot_completed` to report `1`.
Keep the emulator unlocked and do not interact with it concurrently with acceptance.

```powershell
$android = @("-p:AndroidSdkDirectory=$sdk", "-p:JavaSdkDirectory=$jdk")
dotnet build bindings\dotnet\Experimental\AndroidDemo\AndroidDemo.csproj -c Debug @android
dotnet build bindings\dotnet\Experimental\AndroidDeviceTests\AndroidDeviceTests.csproj -c Debug @android
dotnet build bindings\dotnet\Experimental\AndroidDemo\AndroidDemo.csproj -c Debug -t:Run '-p:AdbTarget=-s emulator-5554' @android
dotnet build bindings\dotnet\Experimental\AndroidDeviceTests\AndroidDeviceTests.csproj -c Debug -t:Run '-p:AdbTarget=-s emulator-5554' @android
pwsh -NoProfile -File bindings\dotnet\Experimental\AndroidDeviceTests\Run-DeviceAcceptance.ps1 -Serial emulator-5554 -AdbPath $adb
dotnet run --project bindings\dotnet\Experimental\Xui.Android.LayoutTests\Xui.Android.LayoutTests.csproj -c Release
dotnet build bindings\dotnet\Experimental\Xui.Android.ReferenceCheck\Xui.Android.ReferenceCheck.csproj -c Release
```

These paths record this machine, not project defaults.
The contributor procedure remains the general build entry point.
The reusable script requires PowerShell 7.2+, adb and both already-installed applications, a rotatable unlocked device, `input keycombination` support, and a docked IME.
It does not build, install, or clear application data.
It restarts only the two XUI packages, changes demo state, uses native Ctrl+A selection and adb text/key/touch injection, and temporarily enables the soft keyboard with a hardware keyboard.
Its `finally` block restores the original rotation policy and hardware-keyboard IME preference and removes its unique device-side XML dump.
It resolves launcher components instead of depending on generated Java names.
Every adb command and UI wait has a timeout; the script exits nonzero on failed assertions, native errors, or missing prerequisites.
The native `PASS` must come from the newly launched test PID and also appear in its UI, so stale logcat success cannot satisfy acceptance.

### Results and defects fixed

Both Debug APK builds/deployments and the reference compilation completed with zero warnings or errors.
The arithmetic suite passed 4,170 assertions.
The final native application reported `PASS: 30 Android native assertions.`
The reusable script exited 0 with `PASS: 24 Android device acceptance checks.`

The parent integration copied the exact six-file acceptance snapshot, rebuilt the demo in Debug and trimmed/AOT Release, and rebuilt the native test APK.
Its `Build-PortableDemo.ps1 -Platform Android -Run -AndroidSerial emulator-5554` command deployed the integrated demo using workload-discovered SDK/JDK paths.
After deploying the integrated native test APK, the same device procedure again passed all 30 native assertions and 24 device checks with the viewport measurements below.

The original native fixture passed 26 assertions, but an added hardware-key regression failed before the adapter change:
`Hardware Enter does not submit on key-down.`
On API 35 a hardware event carries `ActionId.Done` as well as its `KeyEvent`.
The old handler treated that as a soft-IME action and submitted on key-down.
The adapter now distinguishes soft actions with no key event from hardware transitions.
The regression requires one soft `Done`, no hardware key-down submission, exactly one key-up submission, and no canceled-key submission.

The longer demo smoke exposed fullscreen Gboard extraction in landscape.
UIAutomator still reported underlying Activity buttons; tapping their coordinates typed a keyboard character instead.
Native window inspection showed a visible fullscreen IME with a zero-height bottom inset, and the screenshot showed the extracted editor covering the application.
The adapter now requests `ImeFlags.NoFullscreen`, not merely `NoExtractUi`.
The native input-connection assertion verifies that the flag reaches `EditorInfo`.
The Activity also applies current system-bar/cutout/IME insets explicitly instead of relying on `SetFitsSystemWindows` under edge-to-edge behavior.
No keyboard dismissal is used to make the smoke pass.

With the docked Gboard visible, the final measured portrait scroll viewport was `[42,261][1038,1475]`, above IME top `1517`.
After rotation and again after Home/launcher restoration, the landscape viewport was `[178,199][2358,352]`, above IME top `394`.
Native swipes reached all three buttons at y=226..352 while the IME occupied y=394..1080.
The post-fix screenshot showed the actual application buttons above the keyboard.
The script compares the viewport's native bounds with the visible docked `InsetsSource`, then scrolls and taps the real widgets.

The smoke checks ten increments and the disabled cap, reset, injected text, hardware submission, and the Submit button.
Replacing a selected range after an unrelated increment, rotation, and background/restore verifies retained focus and selection rather than just comparing model text.
Count, entry and submitted message survive recreation.
One tap after reattachment produces one increment.
Native checks additionally cover silent programmatic text, composition spans, input identity, disabled ancestry, duplicate IDs, layout, queued dispatch, stale disposed widgets, and teardown.

An early automation attempt used a bare `am start -n` after Home and created another Activity instead of resuming the launcher task.
The script uses `MAIN`/`LAUNCHER` intents so that this fixture mistake cannot masquerade as state loss.
Shift+Left selection was also unsuitable while Gboard's extracted editor was active; the final fixture uses Ctrl+A against the retained editor and verifies replacement after each lifetime boundary.

### Remaining Stage 1A gates

This completes APK packaging, deployment, native assertions, and the automation-accessible demo checks on the available API 35 emulator only.
It does not complete the full roadmap exit gate.
There was no API 26 image or physical device run, and no API 36 emulator run.
Actual physical IME composition, language-specific editing, soft-keyboard interaction by a person, TalkBack navigation/announcements, and Activity-retention profiling remain open.
Injected adb input, a synthetic native `Done`, native composition spans, and accessibility property checks do not establish those behaviors.
Composition sessions, keyboard visibility, and scroll position still do not survive recreation by contract.
The fixes do not add state persistence for those features.

## Shared order-builder acceptance, September 23, 2026

The order host and test application use the same installed toolchain and API 35 emulator recorded above.
Their packages are `dev.xui.portable.orders` and `dev.xui.portable.orders.tests`.
Neither replaces the greeting package or its native regression application.
No additional platform packages, SDK images, system tools, or licenses were needed.

`AndroidOrderDemo` links the exact `SharedDemo/OrderBuilder.xui` and `OrderModel.cs`.
Only the Activity and native surface are Android-specific.
The Activity restores all seven immutable `OrderState` fields before attaching a new host tree.
It separately saves the focused automation ID and selection, covering customer name, email, and discount code.
It does not serialize an Activity, native editor, composition, keyboard visibility, or scroll position.
The new surface follows the proven system-bar/cutout/IME inset pattern; the adapter's existing `NoFullscreen` request remains unchanged.

`AndroidOrderDeviceTests` embeds the exact shared `OrderScenarios.json` and compiles the shared `OrderScenarioRunner.cs`.
The driver invokes real `Button.PerformClick`, `EditText.Text`, and `OnEditorAction` callbacks.
It compares native `TextView.Text`, enabled state, and ancestor visibility against fixture literals.
It does not call model actions directly or recompute expected totals in the Android driver.
Walking native ancestor visibility is intentional: the fixture can inspect the mounted tree before its first display traversal, and hidden state belongs to the element wrapper rather than just its inner widget.

The native run passed **161 shared expectations across seven scenarios and 23 Android-specific assertions**.
Those extras check all three editors' native identity, focus, selection and composition spans during unrelated quantity updates; silent reset; actual callback rejection for inactive controls; background dispatch; and stale widgets after detach/reattach.
The corpus additionally covers Unicode and whitespace preservation, malformed/corrected inputs, discount cents, quantity bounds, review/edit/reset, and the complete order summary.
Debug builds and deployment succeeded with zero warnings/errors.
The actual Release order APK also built successfully with trimming/AOT and zero warnings/errors.
The reference-only project now compiles both greeting and order hosts/tests.

The final external run exited successfully with **36 Android order device checks** on shared-layout snapshot 2.
`OrderBuilder.xui` SHA-256 was `01E4778BB587CDD61EB9534D4EA3A9E2CDBEAED3B0E6FD4DF98B914C305E5F20`.
Every input was focused and fully selected in turn, then rotated, replaced, backgrounded with Home, resumed through its launcher, and replaced again.
Exact native text and focus checks verify that restoration selected the correct field and preserved its range.
The script also verifies dependent totals, one quantity change after reattachment, persisted review mode, and reset after the lifetime transitions.

With Gboard visible, the final portrait viewport was `[42,250][1038,1475]`, above IME top `1517`.
The final landscape viewport was `[178,188][2358,352]`, above IME top `394`.
The first authored layout kept a second description line outside the scroll body and left only 92 physical pixels for landscape scrolling.
Shared-layout snapshot 2 moved that line inside the scroll body without changing IDs, state, or scenarios.
The resulting 164-pixel viewport can accommodate a complete preferred 44dp button row at the recorded density.
The device smoke reaches and clicks catalog/review controls through actual scrolling with the keyboard visible.
No Android-specific authored layout fork was introduced.
After the final order run, the unchanged greeting regression also exited 0 with 30 native assertions and 24 device checks.
The original greeting Activity, adapter fixes, and greeting acceptance script were not changed by the order-host work.

The parent integration subsequently rebuilt and deployed its own two Debug order APKs using `Build-PortableDemo.ps1 -Sample Order -Platform Android -Run -AndroidSerial emulator-5554` and the native test project's `Run` target.
The complete device procedure again passed 161 shared expectations, 23 Android assertions, and 36 external checks with the same final viewport measurements.
The integrated all-platform Release build also succeeded, including Android trimming/AOT.

From the repository root, using `$sdk`, `$jdk`, `$adb`, and `$android` from the environment reproduction above:

```powershell
dotnet build bindings\dotnet\Experimental\AndroidOrderDemo\AndroidOrderDemo.csproj -c Debug -t:Run '-p:AdbTarget=-s emulator-5554' @android
dotnet build bindings\dotnet\Experimental\AndroidOrderDeviceTests\AndroidOrderDeviceTests.csproj -c Debug -t:Run '-p:AdbTarget=-s emulator-5554' @android
pwsh -NoProfile -File bindings\dotnet\Experimental\AndroidOrderDeviceTests\Run-OrderDeviceAcceptance.ps1 -Serial emulator-5554 -AdbPath $adb
dotnet build bindings\dotnet\Experimental\AndroidOrderDemo\AndroidOrderDemo.csproj -c Release @android
dotnet build bindings\dotnet\Experimental\Xui.Android.ReferenceCheck\Xui.Android.ReferenceCheck.csproj -c Release
```

The order device script requires the same unlocked, exclusively controlled device and docked-IME prerequisites as greeting acceptance.
Its native test success must come from the fresh test PID and appear in the Activity.
It restarts only the two order packages and restores the prior rotation policy and hardware-keyboard IME preference.
Every adb command and UI search is bounded by `-TimeoutSeconds` (default 60).
Searches use the known authored scroll direction; identical accessibility XML is not a reliable end-of-scroll signal when a long text node fills a small viewport.
The script does not dismiss the keyboard to reach controls.
Physical IME composition, TalkBack, API 26/API 36 coverage, and memory-retention profiling remain separate acceptance gates.

### Captioned input sizing correction

A subsequent visual report exposed clipping inside the native editors, despite the earlier state/lifecycle checks passing.
Both shared samples assigned `preferredSize: (280, 56)` to captioned inputs.
At density 420 and font scale 1, that 147-pixel outer height left a 96-pixel editor after its 51-pixel caption.
The editor's actual text layout and compound padding required 118 pixels.
A native geometry regression reproduced the failure before changing the samples.

The inputs now use natural platform sizing; neither adapter sizing semantics nor native editor padding changed.
All four inputs across the two samples now measure a 118-pixel editor within a 169-pixel outer element at font scale 1.
At font scale 2, those measurements grow to 161 and 252 pixels respectively.
The test uses themed configuration contexts without changing the device's font-scale preference.
It measures the actual generated forms and asserts that text layout plus native compound padding fits, with the caption and editor contained in their allocated element.
The order native test project also links the original greeting source to cover its input.

The corrected native run passed 161 shared expectations and 41 Android assertions.
The order APK was rebuilt and redeployed, and its foreground screenshot confirmed complete editor text and underlines.
Shared recording tests now guard against reintroducing a fixed or preferred input size.
Windows native smoke and both Debug browser suites remain successful with native content sizing.
The Windows smoke also now waits for the arranged root width after resize: `WM_SIZE` schedules a separate layout update, so an immediate bounds read could observe the previous width.

## Mutable peers, lifetime stress, and gallery evidence

The September 23 continuation ran on the same API 35 x86_64 emulator.
Only the API 35 system image and existing `pixel_7_-_api_35_0` AVD were installed.
The API 36 SDK platform is a compilation target, not an additional executed device.
No system image, workload, license, or privileged tool installation was performed.

Android implements the core-owned `IMutableElementPeer` protocol.
`StackLayout` keeps native child order separately from the final model snapshot.
Its same-parent move uses `DetachViewFromParent`/`AttachViewToParent`, not window-detaching `RemoveView`/`AddView`.
The actual generated `MutationBoard` fixture retained the same focused editor and composing range with zero attach/detach notifications.
Additional probes cover insertion before future-index moves, focused removal, a key reappearing, component-type replacement, conditional content, rejected factories/duplicate keys, and committed-failure recovery.
The literal `MutationScenarios.json` is consumed by the shared `MutationScenarioRunner`.
The native order driver traverses the actual Android child hierarchy; it does not return model order as a substitute.

The final first-slice native result was **161 shared order expectations and 923 Android assertions**.
The latter includes the prior geometry/input checks, 696 lifetime-stress assertions, gallery corpora (53 Task Board, 43 Expense Ledger, 44 Session Planner), three gallery teardown checks, 25 shared mutation expectations, and 18 native mutation probes.
The stress run created 12 hosts and 96 attachments, disposed 3,456 peers in reverse ownership order, released 7,488 captured native **view** handles, delivered exactly 96 clicks and 96 changes, and faulted 12 accepted queued tasks after terminal disposal.
This measures explicit peer/view-handle cleanup and callback counts, not Java heap collection or Activity-retention profiling.
All APK/reference compilations completed with zero warnings/errors; the gallery Release APK also completed trimming/AOT.

The checked SDK `36.1.43` target `Xamarin.Android.Common.Debugging.targets` uses `RunActivity` for `-t:Run`.
`AndroidLaunchActivity` belongs to the separate `StartAndroidActivity` target and is not the selector for this command.
An actual run with `-p:RunActivity=dev.xui.portable.gallery.ExpenseLedgerActivity` was verified through the focused native window.

```powershell
dotnet build bindings\dotnet\Experimental\AndroidGalleryDemo\AndroidGalleryDemo.csproj -c Debug -t:Run '-p:RunActivity=dev.xui.portable.gallery.ExpenseLedgerActivity' '-p:AdbTarget=-s emulator-5554' @android
dotnet build bindings\dotnet\Experimental\AndroidOrderDeviceTests\AndroidOrderDeviceTests.csproj -c Debug -t:Run '-p:AdbTarget=-s emulator-5554' @android
pwsh -NoProfile -File bindings\dotnet\Experimental\AndroidGalleryDemo\Capture-Gallery.ps1 -Serial emulator-5554 -AdbPath $adb
```

The capture run exited 0 and produced four real PNGs under `build\portable-gallery\android`: `task-board.png`, `expense-ledger.png`, `session-planner.png`, and `order-builder.png`, with `captures.json` recording components, seed markers, API, serial, and SHA-256.
The first three use shared `new()` example data; Order uses its deterministic empty-cart initial state (`Total: $0.00`), not a claimed filled review state.
All four were visually inspected as native application output with complete natural-size editors and no keyboard overlay.
The helper restarts only the named sample packages, uses explicit Activity components, checks the focused window and content markers, and verifies PNG signatures.
It temporarily locks portrait and disables software-IME display with a hardware keyboard, then restores both preferences.
It does not clear package data or modify the images.

An attempted touch-seeded rich order capture exposed Gboard's physical-keyboard toolbar: it can remain visible while the bottom IME inset is zero.
Back can dismiss the Activity rather than that toolbar, so the final helper does not press Back or accept a zero inset as proof that no IME is visible.
It checks actual input-method window visibility and captures the authored initial state without entering an editor.
Physical IME/TalkBack, API 26/API 36 execution, and retention profiling remain open; these new tests and screenshots do not certify them.

## Native primitives, services, and dynamic application

The next bounded September 23 slice passed **161 order expectations and 1,085 Android assertions** on API 35.
Beyond the previous 923 assertions, it added 75 shared Settings expectations and 11 native value-control probes, 52 shared dynamic-task expectations and six native application probes, and 18 service assertions.
The initial native result was read from PID 9689 and the final rerun from PID 13050; all associated Debug, gallery Release trimming/AOT, and reference builds completed with zero warnings/errors.

The value-control probes inspect real checkbox accessibility nodes and render the native mixed-state drawable into a bitmap to verify its minus mark and unfilled interior.
They verify silent programmatic changes, exact user callback counts, disabled/hidden ancestry, and retained unrelated input composition.
Progress checks read actual native bar positions and accessibility nodes, including authored fractional units, indeterminate omission, huge double bounds, and bounds that collapse when converted to float.
API 36's typed accessibility checked-state path is compiled but was not executed; API 26-29's mixed-label fallback remains unverified on a device.

Service tests never read or write the real emulator clipboard and never launch an external URI.
The emulator may bridge clipboard state to the host.
Actual native capability and denied-operation checks run before the Activity has window focus, with an assertion enforcing that precondition.
Positive text/empty-text roundtrips, URI forwarding, cancellation, disposal, and expired-owner behavior use the internal isolated bridge.
A real clipboard roundtrip and external-handler acceptance therefore remain manual gates, not claimed successes.

`DynamicTaskBoardActivity` links the exact shared application and codec.
Its shared corpus verifies add/rename/reorder/remove, filtering, invalid and Unicode drafts, and initial state.
Native probes verify actual child order, composition during reordering, codec restoration, removed callback rejection, and complete teardown.
The Activity preserves the focused row's stable key and selection separately from serialized task data.

Actual landscape inspection found the first authored dynamic layout left only a 12px list viewport (`[178,963][2358,975]`) because all editing commands were fixed above it.
The shared author moved all non-title content into the existing scroll body; no Android-only UI fork was added.
The updated Activity's landscape viewport is `[178,188][2358,975]` with no docked bottom inset.
`Run-DynamicTaskDeviceAcceptance.ps1 -LifecycleOnly` exited 0 with nine actual Activity checks: a non-first keyed editor's focus/range survived rotation and Home/launcher restoration, the restored draft added exactly one task through a real button, the count survived another recreation, and native error logs remained clear.

The strict docked-keyboard variant did **not** pass in this later run.
Gboard was in stylus/floating mode with a zero bottom inset; the explicitly labelled `Show on-screen keyboard` action did not produce a docked inset.
The script never selects clipboard or unrelated IME actions.
Lifecycle-only mode emits a warning and does not claim docked-keyboard acceptance.
Earlier recorded greeting/order docked-keyboard evidence remains historical evidence, not a substitute for this new dynamic case.

```powershell
dotnet build bindings\dotnet\Experimental\AndroidGalleryDemo\AndroidGalleryDemo.csproj -c Debug -t:Run '-p:RunActivity=dev.xui.portable.gallery.DynamicTaskBoardActivity' '-p:AdbTarget=-s emulator-5554' @android
pwsh -NoProfile -File bindings\dotnet\Experimental\AndroidGalleryDemo\Run-DynamicTaskDeviceAcceptance.ps1 -Serial emulator-5554 -AdbPath $adb -LifecycleOnly
```

`AndroidDeviceHarness.ps1` now shares bounded adb and native-tree helpers between the gallery capture and dynamic Activity scripts.
Keep that helper with either script when copying or packaging them.
Both scripts restore device preferences and remove their own temporary dumps.
Physical IME/TalkBack, the missing API 26/API 36 device matrix, clipboard interoperability, and retained-Activity profiling remain open.

## Native focus and selection follow-up

The separate September 23 input-interface follow-up implements `IFocusableElementPeer` and `ITextSelectionPeer` through the retained Android views.
`TestActivity.Input.cs` adds 18 native assertions: empty initial selection, actual focus, UTF-16 surrogate-pair caret/range clamping, reversed endpoints without read-side mutation, no text-change echoes, keyed identity after insertion/reordering, inactive/nonfocusable rejection, invalid offsets, detach/reattach, and removed-key rejection.
The actual API 35 run (PID 13446) passed **161 shared order expectations and 1,103 Android assertions**.
The APK and official-reference builds completed with zero warnings/errors.
The earlier M4/services/dynamic ZIP remains unchanged; this is a separate source handoff.

Android's `-1/-1` absent native selection maps to a collapsed `0/0` range.
Other incomplete negative native ranges are errors rather than silent fallbacks.
The setter delegates Unicode boundary handling to the common `TextSelection.ClampTo`, and the getter orders native endpoints without writing them back.
Neither operation assigns editor text or replaces the peer.
The tests establish native API behavior, not physical IME or screen-reader acceptance.

## Independent axes and static Grid

The September 23 layout packet consumes the pinned shared `LayoutMath` and `GridLayoutMath`; it does not copy their solvers.
`ElementFrame` applies independent constraints to native content, resolves width before natural height, and retains legacy dimensions only where the corresponding axis override is null.
Authored dp lengths retain midpoint-away rounding and the Android 24-bit cap.
Fractional solver budgets are floored consistently during native measurement and arrangement to avoid a pixel of overflow or measuring text at a wider allocation than it receives.

`NativeMeasureContext` carries `UnboundedContext` outside Android's integer spec bits and forces a remeasure when that semantic flag changes even if mode and size are identical.
Stacks use shared allocation slots.
Grid uses shared clipped span offers and per-cell contexts, preserving natural star/flex behavior under scroll and Auto maximums while allowing an all-fixed span to establish a finite flex budget.
Native parent caches release their managed child/arrangement references during disposal.

The actual API 35 run, PID 15249, passed **161 shared order expectations and 1,142 Android assertions**.
Its 39 new geometry assertions cover per-axis inheritance, natural caption/editor height, retained input/focus/selection/composition through sizing changes, minimum-pressure and capped-flex placement, Grid spans and track updates, keyed content inside cells, natural unbounded rows, fixed versus mixed-span context, same-spec cache invalidation in both directions, and zero-sized allocations.
Caption/editor geometry and clipped span bounds were checked at font scales 1 and 2 across widths of 148, 180, 240, and 360dp.
The legacy Android arithmetic suite still passed 4,170 assertions; actual native geometry is the acceptance evidence for the new shared allocation path.
The gallery Release trimming/AOT build and official-reference build completed with zero warnings/errors.
This remains API 35 emulator evidence, not physical IME/TalkBack or API 26/API 36 certification.

## Private profile workspace

The September 23 profile packet passed **161 order expectations and 1,196 Android assertions** on API 35, PID 15995.
The increment is the shared 45-expectation profile corpus plus nine native storage/lifetime checks.
The corpus runner executes on a worker, marshals actions and native-property reads to the UI thread, captures `Controller.LastOperation` there, and waits off-UI while Android's dispatcher pumps.
Completion is not treated as implicit success: fixture literals verify the rendered status and error state.

Actual `DirectoryApplicationStorage` ran only under the test APK's `FilesDir\xui-profile-tests\<new GUID>`.
The fixture observed exactly two document writes, three reads, and one deletion; creating/mounting and transient restoration performed none.
It verified Preview restoration with fresh navigation IDs, root cancellation, native teardown, and Back to a fresh Edit route.
The owned test directory was deleted only after its operations quiesced.
No corpus action accessed `FilesDir\xui-profile` in the real gallery application.

The production host uses `ProfileWorkspace.Create` with a private directory and the shared transient-session codec.
The controller is owned only by the root lifetime.
The fatal reporter logs worker errors and captures a weak Activity for optional UI notification; retired controllers' tasks are observed without retaining a destroyed Activity.
API 33+ uses the native Back dispatcher, with the legacy callback retained for API 26-32.
The platform default Back fallback remains intact when the controller cannot navigate.

`Run-ProfileDeviceAcceptance.ps1` exited 0 with **10 actual Activity checks**.
It verified a non-first editor's selection/focus across rotation and Home, transient Preview recreation, actual native Back to Edit, and unsaved draft preservation.
This script never invokes Load, Save, or Delete.
An initial automation search looked downward for the new Preview heading even though the retained scroll position put it above the viewport; the fixed script searches upward rather than mistaking that fixture error for failed navigation.
The gallery Debug/Release trimming/AOT builds and official-reference build completed with zero warnings/errors.

```powershell
dotnet build bindings\dotnet\Experimental\AndroidGalleryDemo\AndroidGalleryDemo.csproj -c Debug -t:Run '-p:RunActivity=dev.xui.portable.gallery.ProfileWorkspaceActivity' '-p:AdbTarget=-s emulator-5554' @android
pwsh -NoProfile -File bindings\dotnet\Experimental\AndroidGalleryDemo\Run-ProfileDeviceAcceptance.ps1 -Serial emulator-5554 -AdbPath $adb
```

The transient session's interruption flag is not proof that an old noncooperative provider did not commit.
The shared controller and host observe retirement and retain a warning advising an explicit later Load, after provider quiescence, to reconcile stored data.
Physical Back gestures, API 26/API 36 execution, TalkBack, and retained-Activity profiling remain manual/device-matrix gaps.

## Leased virtualization investigation (acceptance still open)

The Android lease implementation holds the actual native viewport at committed bounds, starting at zero height, while its parent reserves the requested layout slot.
Native `ScrollTo`, over-scroll, and wheel input create requests before changing the native offset.
Declared source extent is independent of staged child geometry.
A reserved commit measures the staged native tree and checks actual registered row roots, source version, logical index, pixel position, pitch, and complete visible-index coverage before publishing offset or growth.
The implementation uses explicit `VirtualItemInfo`, not automation-ID parsing, to validate row identity and native collection metadata.
This is not a promise of atomic intermediate operating-system accessibility trees.

The first actual API 35 native fixture passed 62 assertions over 10,000 logical rows, including first/last navigation, fewer than 30 realized rows, composition-blocked requests, distant focus pins, recycled draft/selection, source-version changes, and pre-publication growth hold.
The detached-bootstrap follow-up completed 100 actual native attachment cycles with at most nine realized rows, 8,106 released native view handles, and 348 delivered viewport requests.
Each cycle cleared old rows and large gaps while detached before creating a new native tree.
An initial range assertion ran before the posted initial interaction snapshot restored an unfocused editor's range.
A UI-queue barrier established that ordering explicitly; the external draft/range was intact, and all 100 cycles then passed.
These are native lifetime/geometry results, not a completed M6 performance gate.

The first measured performance run **failed** the agreed warm p95 budget of 50ms.
Using ten warmups and 100 shared far/neighbor requests, UI-thread timing from request acceptance through native commit and shared pruning recorded p50 **142.013ms**, p95 **400.046ms**, maximum **500.769ms**.
The raw samples are preserved in the session artifact `android-virtual-diagnostic.log` (PID 21863).
This was a Debug emulator run on the shared development host amid coordinated platform validation.
No threshold was relaxed, no rows were pooled across keys, and no benchmark samples were discarded beyond the specified ten warmups.
Phase instrumentation and an exclusive-lane rerun are pending to separate queue wait, preparation, native measurement, coverage checks, publication, and pruning.
M6 remains unqualified until the performance and remaining native acceptance gates pass.

The exclusive W4 follow-up also failed: ten warmups and 100 samples recorded p50 **99.480ms**, p95 **382.880ms**, maximum **865.570ms**.
Its complete samples and six-phase data are preserved as `android-w4-debug.json` and `android-w4-debug.log` in the session artifacts (PID 23023).
Mean phase times were 11.559ms queue wait, 64.224ms preparation, 47.720ms native measure, 1.139ms coverage validation, 0.380ms publication, and 6.883ms pruning.
The corresponding phase p95 values were 19.089, 206.189, 131.175, 2.744, 1.012, and 21.789ms.
Preparation and measurement, not just host contention or coverage checks, require further work.

Evaluated Debug properties were `UseInterpreter=true`, `AndroidUseInterpreter=true`, `RunAOTCompilation=false`, `Optimize=false`, `AndroidLinkMode=None`, and `EmbedAssembliesIntoApk=false`.
The runtime configuration identified Mono on .NET 10.0.9.
W4 installed the already-built APK and its exact prebuilt managed fast-deployment files in the test package's private override directory; it did not rebuild during measurement.
The host was Windows x64, the emulator x86_64/API 35 at 420dpi and font scale 1.0; WHPX 10.0.26200 was reported installed and usable.
The acceleration probe also printed argument-parsing warnings from its helper, so this records availability rather than a separately verified active acceleration mode.
A later Release/AOT measurement must be named separately and cannot erase either Debug failure.

Subsequent attribution counted an average 1,511 `CheckAccess` calls per viewport transaction, each previously performing two native Looper queries.
The dispatcher already required main-looper construction; it now captures that validated managed thread ID and rejects real worker-thread mutation without repeating JNI queries.
Native tests cover the constructor rejection, worker rejection, and posted main-thread execution.
Measuring exact cross-axis width on the first pass also reduced redundant native text measurement.
An attempted relaxation of measurement-cache invalidation was rejected by the mixed Grid regression (84/126px became 126/378px) and was reverted.
Full semantic measure-constraint invalidation remains required.

A separately labelled Release/AOT pre-settle run still failed (p50 50.641ms, p95 168.490ms, maximum 289.878ms); Debug failures remain preserved.
Lease retry attribution found 220 delivered epochs for initialization plus 110 explicit requests.
Retry deduplication based on actual focused-editor identity and global composing state reduced that to 113 while preserving all 613 model interaction notices.
Identical metadata updates are skipped only for the same active lease after reserved-source validation; actual coverage is still checked.
Initially hidden captions are created lazily if later shown, without replacing or reparenting the retained editor.
Programmatic initialization avoids redundant enabled-state assignments.
These changes are not evidence that the 50ms gate has passed.

The optional settled-lease interface was then adopted.
The current completion event follows shared pruning, feedback-state changes, and a synchronous `FlushCommitted` of the actual backend surface and committed native content.
A flush validates the current epoch, native row geometry, and visible coverage without consuming pending future intent.
Earlier timing records are explicitly pre-flush measurements and cannot stand in for this stronger completion definition.

Actual `VirtualListActivity` testing caught an additional geometry defect: an intermediate intrinsic measurement inflated the available viewport and clamped Last's requested offset even though the final parent allocation was smaller.
Available geometry now comes only from the final `ElementFrame` layout allocation.
The actual Activity then passed six checks: first/last native editors, edited draft and saved selection through `OnStop`/`OnStart`, and return to First with no adapter errors.
It does not claim Activity/process recreation persistence.
Accessibility startup waits are bounded and recognize Android's explicit transient null-root response instead of reading a missing or stale dump.

The final settled smoke (PID 30164) passed 96 native assertions; the count depends on how many actual mounted rows its coverage loop inspects.
It includes native source deferral and real focus release, owned native fling cancellation by a newer request, exact blocked epochs, authored viewport shrink/growth, post-prune coverage, and fail-closed lease retirement.
Retiring a lease collapses its native viewport instead of enabling ordinary scrolling through sparse gaps.
The earlier 100-cycle result is preserved; the final settled revision still needs its complete 100-cycle and performance rerun.
M6 remains explicitly performance-blocked while the broader framework work continues.

## Native Forms and Presentation

The September 24 Forms gate passed **45 native assertions** (PID 2091): 21 literal shared expectations plus 24 native editor checks.
It used real input connections for advisory-purpose text and the synthetic password seed, not a model-only driver.
Number input retained its native text key listener and accepted nonnumeric/Unicode text.
Multiline checks covered canonical LF, native Enter, silent programmatic updates, read-only native keyboard/input-connection/accessibility behavior, and retained selection/composition.
Opaque password checks used only lengths/booleans in logs, validated bounded scoped reads, native masking and password accessibility, copy/cut rejection without clipboard access, pre-unmount clearing, and empty/stale-safe reattachment.
No real secret or user clipboard value was accessed.

Two genuine adapter defects were caught rather than waived: removing the key listener alone left the EditText accessibility node editable, and calling `SetSingleLine` after password input type replaced its password transformation.
The adapter now explicitly applies readonly semantics to native input/AX paths and sets `PasswordTransformationMethod` after single-line configuration.
Native filters reject invalid edits visibly and keep the original selection text; callback payloads never contain a password value.

The final Presentation gate passed **40 native assertions** (PID 2452).
All five text-bearing targets used actual 28sp normal/bold behavior with font scales 1 and 2 at 240/360dp widths, with complete native caption/editor geometry.
Tests verified native light/dark custom resources, checked accent and disabled-state colors, retained editor identity/text/focus/selection/composition, and isolation from another host.
Null typography restored each captured native family, size, and weight rather than assuming a 14/400 default.
Null theme restored exact original color-state lists and the caller-owned background.
Additional ownership regressions verified that a never-themed backend does not overwrite external styles on disposal and that clear/reacquire captures a newly changed caller background.
The earlier 38-check run is pre-ownership-fix history, not the final gate.

The work used the integration lead's coherent `consumer-core-baseline-2322-78.zip` (6AD7E26BCA76CF402D0DA45CE40A6095A6C30DFF90B6D2059854DC60E223ADE0), not a stale overwrite of the M6 host.
Debug and Release trimming/AOT builds and official-reference compilation succeeded.
The final Release/AOT APK also executed both native suites: Forms 45 (PID 2862) and Presentation 40 (PID 2971).
API 35 execution does not establish API 26 runtime behavior, physical IME acceptance, native TalkBack output, or complete contrast certification.
Android native high-contrast text is explicitly not a universal forced-color override.

## Evidence, September 19, 2026

The host is Windows ARM64 with .NET SDK 10.0.401.
The Android workload is absent.
The actual `AndroidDemo` build stops at `NETSDK1147`.
No APK, Java build, deployment, emulator, or device execution is established.
No workload installation, elevation, system configuration change, or license acceptance occurred in this session.

The configured NuGet mirror supplied `Microsoft.Android.Ref.36` version `36.1.69`.
The separate reference project compiles the production sources against `Mono.Android.dll` and `Java.Interop.dll`.
It does not import Android packaging targets or run Android code.
This check caught binding errors before delivery.
The final reference compilation completed with no warnings or errors.
The regular production projects still target `net10.0-android`.

The layout arithmetic tests passed 4,170 assertions.
The unchanged shared runtime and generated demo passed 77 assertions.
The generator regression suite passed 42,062 assertions.
The documentation check passed with 55 pages and 717 local links.
These results do not establish layout, focus, accessibility, rotation, or IME behavior on Android.
The native test application and manual smoke procedure remain the acceptance gates.

Existing repository release automation builds Windows artifacts.
No workflow or PR was published to claim Android CI execution.
The parent integration must keep the native execution gap explicit.

## Design notes

The backend uses native controls inside measurement wrappers.
The wrappers apply preferred and fixed sizes even for a root or scroll child.
The stack allocates finite main-axis space without Android's negative-weight shrink behavior.
The scroll viewport does not apply finite-height flex rules to its unbounded content.

Backend disposal removes only the mounted root.
Peer disposal unhooks listeners, removes native parent links, and releases only that peer's resources.
Partial construction failures also release already-created resources.
Cleanup failures remain visible as aggregate exceptions.

The Activity saves authored state rather than an Activity-bound host.
Ordinary state updates retain the existing EditText.
Explicit replacement clamps selection and can end composition.
Activity recreation restores text and selection, but not composition spans or scroll position.

## Studio native checkpoint (2026-09-24)

The API 35 x86_64 emulator `emulator-5554` remains the only device execution matrix.
It uses 420dpi and font scale 1; 2x geometry cases use an isolated configured native Context.
The host is Windows x64, SDK 10.0.301, Android workload 36.1.43, SDK
`C:\Program Files (x86)\Android\android-sdk`, JDK
`C:\Program Files\Android\openjdk\jdk-21.0.8`.
This checkpoint is Debug/interpreter, not physical ARM64 or Release performance evidence.

Actual `dev.xui.portable.gallery/dev.xui.portable.gallery.WorkspaceStudioActivity`
build/deployment succeeded with no warnings or errors using the unchanged D55 Studio authored cohort.
The Gallery project removes the SDK's implicit `Android.Widget` global using rather than
forking shared portable `ScrollView` source.
The phone captures are real screenshots of the Activity, not the test completion screen.

Native suites independently exercised retained PageView/TabWidget/ListView (41 shared plus
9 native expectations), SingleChoice (11), owned viewport/label layout (21), and existing
axis/Grid geometry (39).
An initial viewport rerun installed a synthetic composing span before the Activity gained
window focus and failed; the fixture now waits for the real focused window before starting
its composition probe. The same 21 assertions passed afterward, with no weakened assertion.

The first phone capture exposed a clipped second line in `ANALYZE DOCUMENT`.
Horizontal StackLayout had calculated cross-axis height before allocating flex widths.
It now remeasures at each allocated width before choosing natural cross-axis height.
The exact shared Studio editor at 280/320/360dp and 1x/2x fonts adds 12 native glyph-layout
checks to the 32 exact catalog-row checks: 44 passed. At 320dp/1x, Analyze needs and receives
144px; 2x cases require 221/301/381px depending on wrapping. No caption or fixed-height
workaround was used. The 128dp shared catalog row still passes its exact native geometry;
96dp remains insufficient at 2x.

Reproduce the targeted APK suites with `TestActivity` extras `pages-only`,
`single-choice-only`, `viewport-label-only`, `studio-row-only`, or `layout-only`.
Build/install with:

```powershell
dotnet build bindings\dotnet\Experimental\AndroidOrderDeviceTests\AndroidOrderDeviceTests.csproj -c Debug -t:Install '-p:AdbTarget=-s emulator-5554' '-p:AndroidSdkDirectory=C:\Program Files (x86)\Android\android-sdk' '-p:JavaSdkDirectory=C:\Program Files\Android\openjdk\jdk-21.0.8' -v:quiet
```

`AndroidGalleryDemo\Run-StudioDeviceAcceptance.ps1` drives actual Library/Insights/Operations,
draft editing, background selection restoration, rotation and native Back, and saves PNGs.
The initial bounded run proved portrait draft/focus restoration after rotating through
landscape but exposed two further issues: short landscape height starved the editor, and
Library activation with a focused editor rejected pane hiding. Those runs are not complete
Studio acceptance. Shared short-height/deferred-navigation corrections are coordinated with
the authored application owner; the adapter never forces composition to end.

The separate SDK-owned embedded Release/AOT font registration fixture eventually passed
101 assertions (PID 13183), including 26 owned Typeface release cycles and actual widget
identity/range/pixels. Two earlier failures were retained: Android `Paint.MeasureText`
rounds up, so the corrected fixture uses fractional `GetTextWidths` at the unchanged
0.1px hmtx tolerance and separately checks the documented ceiling behavior.
This proves the fixture's API35 constructor path, not production packaged-font integration,
API26 execution, physical IME behavior, or a native font-engine memory ceiling.

The settled M6 50ms p95 performance gate remains open/failed. These native correctness
and screenshot results do not waive it, certify TalkBack, or expand the API/device matrix.

### Final bounded Studio follow-up

The shared D55-compatible short-height v2 cohort
`9AD6680508814306885E575AFE010D805945B78F31B9CD2BEFCC06177155FBFF`
then passed 12 actual `Run-StudioDeviceAcceptance.ps1 -LifecycleOnly` checks.
It is a shared height policy, not an Android orientation fork.
The native landscape accessibility tree exposes the title at `[809,474][2378,592]`
and multiline body at `[809,603][2378,984]`: 118px and 381px tall.
The selected title range survives recreation, accepts exact replacement in landscape,
and retains its updated draft/focus when returning to portrait and through Library.
The screenshot named `android-studio-landscape-editing` intentionally includes Gboard's
floating/stylus input overlay. It does not prove a docked keyboard's resize behavior.
The corrected portrait PNG visibly includes both full Analyze Document caption lines.

The Page suite now passes 52 (41 shared plus 11 native), adding deliberate navigation
activation with and without composition. Composition retains input focus/spans;
noncomposing activation takes real ListView focus before the authored pane change.
The owned viewport fixture passes 21 after waiting for initial window focus.
Layout arithmetic also remains 4,170.

The initial full Studio script was **not green**: the earlier all-10,000-document
Operations scan produced an actual Android ANR dialog after nine preceding interaction
checks. No native/managed cause is established by that observation. The bounded
lifecycle run explicitly excludes Operations and is not a substitute for that open gate.
No physical IME, TalkBack, API26/API36 runtime, production font binding, or final M6
performance completion is claimed.

The final full script then passed **17 actual Activity checks** (PID 17721),
including the short-height round trip, native Spinner scope selection, a complete
10,000-document Operations snapshot, and two native Back steps through Insights
to Library. Completion is awaited without injecting scroll gestures while work is
pending. The visible native totals are Documents 10000, Drafts 1, Open documents 2,
Words 470000, Characters 3033336 and Checklist items 30000.
The first scan's ANR while automated scrolling was pending is preserved as an
unresolved responsiveness/stress observation; the later functional pass does not
establish its cause or prove that input-pressure problem fixed.

The full reproducible Activity command, after building/installing AndroidGalleryDemo,
is:

```powershell
.\bindings\dotnet\Experimental\AndroidGalleryDemo\Run-StudioDeviceAcceptance.ps1 -Serial emulator-5554 -AdbPath 'C:\Program Files (x86)\Android\android-sdk\platform-tools\adb.exe' -TimeoutSeconds 120
```

PNG/XML captures and raw logs were handed off under the Android session's
`files\studio-full-final` and `files\android-studio-full-final.log`.
Do not interpret the earlier estimated 314dp height as a measured Host viewport:
the actual landscape screenshot is 2400x1080px and the inset content root is
`[136,74][2400,1017]`; the exact measured evidence is the native editor rectangles above.
