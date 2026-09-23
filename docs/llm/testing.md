# Test coverage and measurement protocols

This reference describes regression scope and measurement methods.
Use [CONTRIBUTING](../../CONTRIBUTING.md#tests) for the build and test entry points.

## Release packaging and local deployment

The public contract is in [Packages and deployment](../specs/packages.md).
`scripts\Build-Release.ps1` builds each architecture and stages its sample inventory.
`scripts\New-ReleaseAssets.ps1` creates the packages, per-architecture sample and Designer ZIPs, and checksums.
Release samples use `Get-XuiSamples -ReleaseOnly`. TaskCard opts out through `IsXuiReleaseSample=false`.
Local native-copy checks retain the full sample inventory.
The release build publishes C# samples with NativeAOT, size optimization, and no debug symbols.
`scripts\Build-DesignerRelease.ps1` separately publishes self-contained Designer output for each architecture.
The Designer retains managed assemblies and Roslyn for runtime compilation, without trimming, single-file output, or NativeAOT.
Its SDK must match the target architecture because the Designer references the SDK's Roslyn assemblies.
`packaging\DESIGNER.md` supplies the [archive instructions](../../packaging/DESIGNER.md).
`packaging\Xui.nuspec` defines the combined NuGet layout.
`packaging\Xui.Templates.nuspec` defines the separate `dotnet new xui` package.
[`packaging\TEMPLATES.md`](../../packaging/TEMPLATES.md) supplies the template package instructions.
`scripts\Pack-Templates.ps1` sets the generated package reference version without a native build or tracked source edits.
The native and managed imports have separate framework directories.
`bindings\dotnet\Xui.Declarative.Common.targets` shares compiler input tracking between source and package consumers.

`tests\native-copy.ps1` checks each local sample in Debug and Release.
It compares DLL hashes, checks explicit missing-runtime errors, and selects an older-timestamp replacement.
The no-op check protects the native DLL from unnecessary writes during a managed development loop.
`Xui.Native.targets` uses `IfDifferent` instead of timestamp-only copy behavior.
Compiler-only fixtures explicitly disable native deployment.

`tests\packages.ps1` restores the actual NuGet archive into an isolated package directory.
Its managed fixture compiles `.xui`, loads the C ABI, and checks Debug, Release, and publish output.
Its CMake and Visual C++ fixtures cover static C++ and shared C ABI consumption.
The static executable runs without neighboring package files.
The Cargo checks build extracted archives offline through a checksum-backed local registry.
The consumer checks also build the Rust sample and run a window-free ABI probe with the DLL beside its executable.
`FrameworkSource` selects an alternate configured feed for Microsoft runtime packages.
The XUI package always comes from the local release assets.

`tests\templates.ps1` checks the template archive and uses an isolated template directory outside the checkout.
It checks installation, removal, default output, explicit names, namespace substitution, and preservation of hot-reload preprocessor branches.
With release assets, it builds generated projects in Debug and Release, checks the hot-reload opt-out, and checks published native files.
The release workflow runs these consumer checks on x64 and ARM64.
The standalone template workflow checks package generation without native inputs.
The test does not exercise interactive window behavior.

`tests\release-samples.ps1` extracts both sample ZIPs.
It checks the manifests, file hashes, exact C# inventory, native inventory, PE architectures, and imported PE dependencies.
It requires NativeAOT output without managed runtime files, managed assemblies, or .NET debug symbols.
These checks do not exercise each sample's interactive behavior.
The existing desktop regressions remain responsible for that behavior.

`tests\release-designer.ps1` extracts the Designer ZIP for the selected architecture.
It checks file hashes, complete manifest coverage, licenses, runtime and compiler files, native PE architectures, and the self-contained runtime configuration.
It runs the extracted executable with `--smoke`, a restricted `PATH`, and invalid external .NET root paths.
The existing smoke exercises runtime compilation, preview construction, compilation errors, preview recovery, and file operations.
The release workflow runs this check on each architecture.

`tests\release-packaging-unit.ps1` runs the asset script with inert samples and Designer files, plus substitute NuGet and Cargo packers.
It checks separate archive contents, licenses, checksums, existing-output refusal, and incorrect versions, architectures, or hashes.
The workflow runs these checks before the release builds.

`tests\release-workflow.ps1` replaces `gh` with a local fixture.
It checks numeric versions, draft creation, complete asset uploads, repeat runs, and refusal to change a published release.
It also checks per-asset upload retries, exponential delays, recovery on the last attempt, retry exhaustion, and a subsequent run.
Successful uploads must not repeat during retries, and retry exhaustion must stop further uploads.
It also requires every package and both sample and Designer archives before any GitHub request.
It makes no GitHub requests.
The release workflow checks package consumers on both architectures before the draft job receives write permission.
It also publishes and runs the FileExplorer model tests with NativeAOT on each architecture.
The explorer uses source-generated JSON metadata to preserve state persistence without runtime reflection.
The model tests disable reflection-based JSON serialization and cover the persisted schema, round trips, and corrupt-file protection.

## Tests and measurements

### Framework consolidation gate

The September 18, 2026 consolidation merged `origin/main` at `972d144` through `15a81e2`.
Commit `c83411b` added the Terminal branch's split-axis APIs while preserving upstream split animations.
The ARM64 Release build included the framework DLL, native regression fixtures, and the triangle sample.

The initial fixed CTest batch passed nine of twelve groups.
Split animation, split-axis input, controls, layout styles, foundation, explorer, swap-chain hosting, swap-chain ABI, and native key routing passed.
Three groups failed their existing focus or foreground assertions: live swap-chain overlays, popup input/lifetime, and native ContentHost replacement.
The overlay fixture reached visible two-producer occlusion before its foreground assertion failed during subsequent operations.
These failures remain unclassified. They do not establish whether the framework or external desktop activity changed focus.

One diagnostic ContentHost run passed after the fixture gained numeric HWND diagnostics.
That result is a non-reproduction, not a correction of the failed batch.
The overlay and popup fixtures also gained failure-only HWND diagnostics without weaker assertions.
The original failed batch remains in the parent session's `files/framework-integration-c83411b-ctest.log`.

The follow-up added `tests/native_focus_diagnostics.hpp`, a thread-local CBT observer.
It records focus and activation notifications synchronously, with bounded storage and operation labels.
An initial diagnostic batch passed all three previously failed groups, without observed focus requests, activation requests, or foreground changes.
The original test order then passed eleven groups and failed the panel fixture's existing foreground assertion.
The panel fixture did not yet have the CBT observer during that failure.
An instrumented panel run subsequently passed, without observed requests or foreground changes.

The final fixed batch added strict passive-focus checks to all four affected fixtures.
Those checks reject transient focus or activation requests, sampled foreground changes, and diagnostic overflow.
All twelve groups passed in 20.64 seconds on September 18, 2026.
All eight instrumented cases reported zero activation requests, focus requests, sampled foreground changes, and dropped records.
The final log is `files/focus-final-acceptance-20260918.log` in the parent session.
The diagnostic and ordered-batch logs remain beside it.

This final batch passes the current ARM64 acceptance checks with stronger assertions and unchanged production code.
The earlier foreground failures remain unclassified, not corrected or proven external.
The observer covers the fixture UI thread, not every application on the desktop.
Review must distinguish the current passing checks from a root-cause explanation of the earlier failures.

Separate checks passed for native split ABI, managed split visibility and animation, and the generator.
Non-activating label backgrounds, expander surfaces, and range focus checks passed.
Both triangle smoke paths passed: the default swap-chain pointer and the imported-handle WARP renderer.
Documentation checks passed. Foreground-dependent animation and inspection fixtures were not run.

The consolidation does not import Terminal application code, packages, or runtime evidence into XUI.
The separate Terminal output timeouts and earlier UIA event-cache failure remain unresolved.
The passing framework batch does not resolve those separate application failures.

### Hidden scroll views

The terminal palette exposed a hidden `ScrollView` that still reserved its preferred height.
`xui_control_tests` reproduced the failure before the visibility guard in `ScrollView::measure`.
The regression covers normal and passthrough scroll hosts, plus restored visible content.

### Retained scroll performance

`xui_scroll_tests --retained-only` exercises a large retained form without foreground activation.
The fixture checks native geometry, editor state, root measurements, and individual editor placements during scrolling.
It also covers a stationary popup and synchronous paint timing.

The FileExplorer `--settings-scroll-smoke` check uses the complete inline settings editor with both file panes open.
It requires real movement, stable editor HWNDs, native focus, unsaved drafts, search results, and a subsequent settings update.
It reports wheel dispatch separately from wheel dispatch plus pending native paint work.
These measurements exclude compositor presentation latency.
[CONTRIBUTING](../../CONTRIBUTING.md) supplies the command and acceptance budgets.

### Split axes and divider input

`xui_split_axis_window_tests` uses an owned, non-activating window.
It sends pointer messages to both divider orientations and checks nested pane geometry, arrow keys, Home, callbacks, and capture cancellation.
It also checks that layout changes and hidden panes release capture without another pointer movement.
The fixture never calls `SetFocus` or changes the foreground window.
`xui_explorer_tests` covers axis-specific minima and pane layout.
`xui_split_animation_tests` covers both axes with custom minima, animated surface geometry, drag takeover, and layout changes during animation.
`xui_abi_features_tests --split-first-visible` covers layout arguments, handle kinds, thread affinity, and closed handles.
The managed `--split-first-visible` mode covers layout round trips and deferred ratio events.

On September 18, 2026, these ARM64 checks and `xui_style_layouts_tests` passed.
The x64 native package counterpart compiled but was not executed.
These checks do not prove application terminal output, installed IME input, or screen-reader behavior.

### Swap chain host input

`tests\swap_chain_input_tests.cpp` uses a fixture-owned HWND and its normal XUI message loop.
It checks native Tab, Shift+Tab, and PageDown delivery after the explicit input opt-in.
Application F6 and Shift+F6 handlers move focus out of and back into the panel.
With the opt-in disabled, ordinary Tab traversal skips the panel.
Modifier changes affect only the fixture thread's keyboard state and are restored after the test.
The test does not inject desktop-wide input or prove terminal text composition.

`tests\swap_chain_abi_tests.cpp` checks graphics-host handles, metrics, callback errors, owner-thread guards, and teardown.
`tests\swap_chain_panel_tests.cpp` checks the native compositor with owned-window pixel capture.

`tests\swap_chain_overlay_tests.cpp` is the acceptance fixture for popup pixels above live swap chains.
One producer uses a DXGI pointer, and the other uses an imported composition handle.
Both direct HWND siblings and producers inside separate `ContentHost` ancestors use the same assertions.
With a narrow, left-aligned anchor, `below_viewport_center` must center the popup on the physical client area.
The fixture captures both producers before the popup opens.
It then requires opaque popup pixels over both surfaces while the visible producer colors change.
Repeated `ContentHost` replacement must show each new result color above both producers and remove the previous result pixels.
Empty results must leave an opaque popup, without stale result pixels or producer leakage.
Dismissal must reveal the current producer pixels, without stale popup pixels.

The fixture also requires unchanged HWNDs, physical bounds, metrics, buffer dimensions, and resize counts.
Every metrics notification must preserve visibility and dimensions, including transient notifications.
The fixture requires unchanged foreground ownership and releases the producers before COM shutdown.
These checks do not prove native keyboard routing, installed IME behavior, or screen-reader compatibility.

An independent Win32 probe on September 18, 2026 isolated the native clipping requirement.
A higher-Z-order Direct2D child surface did not cover a sibling DirectComposition producer without `WS_CLIPSIBLINGS`.
The producer remained live throughout the probe.
After both peers received the style, the overlay covered the same sampled region.
An unclipped native ancestor reproduced the failure with the same producer HWND.
The ancestor also required sibling clipping.

`tests\swap_chain_popup_input_tests.cpp` checks the independent popup input and drawing targets.
It covers native EDIT text, rounded bounds, nested popups, Escape, outside dismissal, and modal input ownership.
Modal presentation disables producer input without hiding its surface or changing its metrics.
The fixture also covers resize, synthetic DPI, target recreation, reentrant dismissal, and owner closure during presentation.
An injected rendering failure must remain explicit, and all drawing targets must release.
These ARM64 checks passed without foreground activation. They do not exercise an installed IME.
The fixture also covers popup-host replacement, empty results, native editor identity, and nested-popup retirement.
Closed and foreign hosts remain invalid replacement targets.

On September 18, 2026, the packaged terminal palette exposed a missing mounted-popup ownership path.
The parent compositor fixture reproduced the rejection after successful static-overlay captures.
The correction passed the compositor, popup-input, content-host, content-inspection, and ABI-feature fixtures on ARM64.
The compositor checks included repeated and empty results over both direct and nested live producers.
The content-host fixture only attempts editor focus and pointer capture when its owner is already active.
Background runs do not prove those active-input paths.

### PR 32 integration with main

The merge with `2a5f769` preserves the animation demos, tab tear-out, new controls, and the scroll-copy correction.
The shared transition timer remains 43, with diagnostics 33 through 36.
The incoming indeterminate progress timer uses 44, with diagnostics 37 and 38.
Indeterminate bars and rings retain the incoming Classic and WinUI behavior.

SplitView topology changes settle motion before the primary pane disappears.
The secondary-only layout occupies the full container without a divider, including widths below the two-pane breakpoint.
New model regressions cover this topology and determinate ProgressRing interpolation.
The ring fixture explicitly selects determinate mode because ProgressRing defaults to indeterminate.

The split, progress, control, and new-control model checks passed on Windows ARM64 Release.
The foundation, queue fairness, progress motion, Expander motion, tab motion, Reveal, and scroll-flicker desktop checks passed.
The merged native DLL also built successfully.

The gallery retains 60 pages, including Motion and the incoming control examples.
The catalog, Motion, feedback, progress, content, document, new-control, and both parity smoke modes passed.
The generator passed 41,997 assertions.
The VS Code tokenizer passed 30 tests, and the Microsoft Edit grammar passed 12 tests.
The Explorer model suite passed 78,120 assertions, including the incoming drag and hover-join cases.
The documentation checks passed for 52 pages, 650 local links, and 19 adapter tests.

The isolated Explorer preview built without warnings at `build\animation-merge-preview\FileExplorer.exe`.
Its copied native DLL matches the merged native build.
The full smoke passed, including stationary palettes, tab-drag handlers, reversible hover joins, tear-out rollback, and same-Application merges.
Separate hover-join, retained-view entry, and pane-animation smoke runs also passed.
The pane-reopening assertion uses native focus and `IsWindowEnabled` because the managed `Enabled` property is write-only.
The full smoke observed 28 intermediate pane paints, and the focused pane smoke observed 30.
Existing user preview outputs remained untouched.

The Rust library suite passed 32 of 33 tests.
The remaining style test expects `Wrong handle kind` in an error message.
Incoming main changed `xui_button_set_style` from an exact-kind check to a Button feature check.
The error status remains 3, but the existing message assertion fails.
This merge does not weaken that assertion or change the incoming error behavior.

The incoming tab-drag desktop test failed its `Background fixture does not steal foreground` assertion twice.
An isolated comparison used unchanged `origin/main` `application.cpp` with the current other libraries and the same test object.
That comparison failed the same assertion after the gesture checks passed.
This isolates the failure from the merged window-host changes, but does not establish a clean-main baseline.
The foreground failure remains unresolved.

### Reveal animation checks

`tests\reveal_tests.cpp` covers natural measurement, full-size child geometry, duration validation, reversal, delayed clocks, and terminal invalidation.
`tests\reveal_window_tests.cpp` covers live timer delivery and intermediate native editor positions.
It also covers first-character input, immediate focus return, disabled exit interaction, idle paints, hidden windows, and retained models after closure.
Its retirement fixture removes an active reveal through `Window::replace_content`.
Its concurrent fixture keeps the shared timer active until the last reveal stops.
It also covers hidden reveals and minimized windows.
Its owned-window capture checks moving content pixels and the clip above a stationary footer.
The capture selects only the fixture window.

The Windows fixture reads the system motion preference without changing it.
It requires intermediate timer frames when that preference permits motion.
It requires immediate settlement when the preference disables motion.
The zero-duration path also runs regardless of the system preference.
Physical IME candidate positioning and broad frame-time measurements remain manual acceptance work.

On 2026-09-17, the ARM64 Release model and window fixtures passed in the animation worktree.
The control, style-layout, and preferred-stack fixtures also passed.
The content-host fixture failed an initial background-focus assertion, then passed in isolation.
The compiler fixture passed 41,900 assertions, and the FileExplorer model fixture passed 416 assertions.
The Rust `reveal_contract` fixture and the complete managed FileExplorer `--smoke` also passed.
The managed `--reveal` fixture passed 27 ownership, validation, and state assertions.
The Explorer smoke covers the production `.xui` reveal, immediate input, reversal, and settled geometry.
The VS Code grammar suite passed 38 tests, and the Microsoft Edit grammar suite passed 11 tests.
The native syntax fixture used the syntax-disabled build configuration.
These results do not establish a CPU budget or smoothness on other devices.

#### Coordinated layout phase

On 2026-09-17, the ARM64 Release fixtures also passed with `RevealLayout::expand` and all four entry directions.
The model fixtures cover repeated grid measurement, sibling geometry, reversal, bounded natural sizes, nested clips, and preserved scroll offsets.
The native fixtures cover zero-height opening focus, first-character delivery, caret selection, undo history, and constant editor height.
Owned-window captures cover both layout modes for each entry edge.
Retirement, concurrent transitions, hidden windows, minimized windows, and timer shutdown run with both layout modes.
Nested native reveals retain input through a scroll viewport.
Resize and synthetic DPI messages preserve progress and align the native editor with retained bounds.
Synthetic DPI messages do not replace physical multi-monitor acceptance.

The frame-work fixture samples 20 intermediate frames in a small window with an editor and buttons.
Fixed layout performs zero root layouts during those samples.
Expanding layout performs exactly 20 root layouts.
Both modes stop the shared timer after settlement.
The fixture includes synchronous update and paint in its timing interval.
One isolated run recorded median intervals of 16.82 ms for fixed layout and 16.50 ms for expanding layout.
The respective 95th-percentile intervals were 41.42 ms and 18.51 ms.
Other runs varied substantially with desktop and graphics activity.
These samples demonstrate bounded layout work, not an expansion speed advantage or a frame-rate guarantee.
Representative Explorer workloads, CPU profiling, graphics allocations, and long-cycle retention remain phase 6 work.

One extended capture run exposed cached WinRT factories across fixture apartment shutdown.
The fixture now retains one apartment across its capture windows and clears factories before final shutdown.
Queued driver callbacks also reject a completed or absent fixture.
Some shared-desktop runs lost focus or observed an extra idle repaint.
The complete isolated Reveal run passed without weakening focus or idle assertions.
The adjacent core, control, and style-layout fixtures passed after the final native changes.
The unchanged content-host fixture again failed its background-focus assertion, including an isolated rerun.
That failure occurs before its replacement loop and has no Reveal in the fixture.

The coordinated-layout compiler fixture passed 41,913 assertions, and the FileExplorer model fixture passed 416 assertions.
The rebuilt native DLL passed 58 managed Reveal assertions and the Rust `reveal_contract` test.
The complete Release Explorer smoke passed with animated and zero-duration layout transitions.
Its geometry assertions first flush the posted layout update because a timer sample can precede that update in the UI queue.
Its reversal assertion bounds the expected cubic position by measured call times, rather than comparing against a stale rendered frame.
The smoke retains exact shared-edge, conserved-height, full-size-input, focus, and native-identity assertions.
The updated Explorer output is separate from the running demo, so its output lock does not interrupt the existing window.

#### Explorer animation demos and replay gallery

On 2026-09-17, the ARM64 Release Explorer smoke passed with opt-in navigation, split-pane, and tab insertion transitions.
The navigation check compares the shared pane edge, retained search peer, focus return, and titlebar minimum inset.
The pane check requires intermediate geometry, full-width secondary content, immediate input, retained text, and zero final extent after closure.
The tab check observes intermediate New tab button positions while logical selection and file focus change immediately.
The preview output is `build\animation-preview`, separate from the existing user process.

The split model fixture and native pane capture passed.
The managed split fixture passed 11 assertions, and the Rust `split_animation_contract` passed.
The tab model fixture passed stable-ID geometry, pointer geometry, interruption, and idle invalidation checks.
The managed tab fixture passed 22 assertions, and the Rust `tab_animation_duration` passed.
The tab-specific window fixture passed direct execution and five consecutive CTest runs.
It covers owned-window pixels, native New tab button identity, presented hit and UIA bounds, rounded clocks, retirement, and idle work.
The fixture uses natural tab height and a viewport that does not trigger overflow settlement.
Its native identity check reads the HWND-backed parent of the semantic button.

The gallery catalog fixture passed with the animation page and its handbook route.
The animation gallery smoke passed replay, reversal, immediate mode, fixed slots, retained native text, pane access, and timer shutdown.
The first gallery revision clipped the vertical native fields because duplicate captions exceeded the authored child height.
The sample now hides the duplicate captions and gives each native field an explicit field-sized slot.
The retention check uses UIA ValuePattern for cross-process text, not `GetWindowTextW`.

The Reveal reversal fixture now waits for a delivered exit sample before it reverses.
Its diagnostic exit duration is 1000 ms to permit sample observation under desktop load.
Focus, intermediate progress, endpoints, and idle assertions remain intact.
That longer fixture exposed a terminal invalidation defect in both Reveal and SplitView.
Cubic easing can round the progress to its endpoint before the clock reaches the configured duration.
The next clock sample previously cleared the active flag without requesting the final layout.
Both models now invalidate completion even when the progress value is unchanged.
Deterministic rounded-endpoint regressions failed before the fix and passed afterward.
The complete native Reveal window suite and animation gallery smoke also passed after the fix.
These results do not establish frame-rate, CPU, or allocation limits for representative applications.

#### Ratio presets and feedback

The split model fixture passed ten consecutive runs with ratio motion, retargeting, pointer takeover, and constrained endpoints.
The native pane fixture passed intermediate ratio pixel checks on both sides of the divider.
It also checked native editor geometry, text, focus, caret, and undo retention during width changes.
The gallery ratio check observed intermediate native editor positions and rapid preset changes.
The complete Explorer smoke passed with the rebuilt ratio implementation.

The gallery catalog now contains 50 entries.
The `feedback-motion` page passed its focused runtime check.
Each validation or notice state produced one live announcement, without repeated announcements during motion.
Typing and erasing retained native focus, identity, caret, and undo.
The immediate-mode path also retained announcements and stopped the animation clock.
Built-in InlineStatus dismissal remains unchanged. The demo animates its containing reveal instead.

#### Popup entry experiment

The popup gallery source now has an optional Animate popup content switch for parent and nested popups.
It keeps dismissal immediate and resets the retained reveal for the next generation.
The new native fixture covers first-character input, stationary popup bounds, reentrant dismissal, generation revocation, and clock cleanup.
The focused gallery fixture covers nested dismissal and native editor retention across replay.
MSVC syntax checks passed for the gallery and both fixtures.
The first runtime run exposed premature animation settlement before popup layout.
The scheduler treated the new popup's empty bounds as hidden ancestry.
Policy checks now ignore stale geometry while layout is pending, but retain logical visibility, enabled state, modality, and generation checks.
Normal input and visibility checks still require arranged geometry.
The native fixture passed intermediate content movement, stationary frame geometry, first-character input, reentrant reopening, and immediate cleanup.
The gallery popup and feedback modes also passed after this change.

The first gallery replay assertion incorrectly required native identity across complete popup dismissal.
The existing popup contract releases closed native peers after input dispatch.
The fixture now requires stable identity during entry, peer release after dismissal, and retained text in the newly created editor after reopening.
It does not change popup ownership or promise undo retention across dismissal.
The rebuilt `build\animation-preview` passed the complete Explorer smoke after the shared scheduler change.

The dialog gallery now provides opt-in form entry with stationary title and actions.
The native fixture passed intermediate form movement, immediate modal exclusion, first-character input, validation failure, and one result after logical closure.
The gallery fixture passed invalid and valid submissions, owner re-enabling, and timer cleanup.
Result delivery and dismissal remain immediate. Whole-dialog motion and exit presentation remain planned.
The public dialog text now describes the existing callback order accurately: generation revocation precedes results, and eligible focus restoration follows callbacks.
The native modal-entry fixture also passed with WinUI layout.

#### Expander acceptance

The native Expander model and window fixtures passed in ARM64 Release.
The window fixture covers native input, clipping pixels, UIA geometry, reversal, focus repair, and idle work.
The gallery `disclosure` example passed collapse/reopen checks with the original native field and retained text.
The managed fixture passed 21 assertions, and the Rust `expander_animation_contract` passed.

One desktop run missed an intermediate frame in the original 400 ms entry fixture.
The revised fixture records intermediate body arrangements from before expansion, rather than only after its driver timer starts.
It uses a 2000 ms diagnostic duration and reports setup, driver, completion, and progress details on failure.
The revised desktop fixture passed without injected entry clock samples.
The actual intermediate-frame requirement, focus, native identity, undo, and pixel assertions remain intact.

#### Tab removal and further timing checks

The tab model now supports gap-closing motion after logical removal.
Its regression passed retained stable-ID positions, removed-ID rejection, trailing New button movement, interruption by insertion, placement-only work, and immediate clearing.
The native window fixture passed surviving-tab pixels, background pixels in the removed gap, pointer geometry, and immediate UIA item removal.
The original strip and New button HWNDs remain unchanged, and removal frames perform no root layout.
The Explorer preview now includes removal motion.
Its complete application smoke passed with bounded intermediate New button positions during insertion and trailing removal.
The deleted tab leaves the logical model immediately, and file focus returns before gap-closing motion completes.
An earlier Explorer run failed the combined pane focus/text/intermediate-frame assertion.
That assertion now reports each value separately. The complete subsequent run passed without changing pane behavior or its diagnostic duration.
The earlier failure does not identify which condition failed, so its cause remains unconfirmed.

One loaded-desktop Reveal run completed its 240 ms entry before the driver observed an intermediate frame.
The fixture now uses the gallery's 1200 ms diagnostic duration for that entry.
It still requires an actual intermediate timer frame, unchanged input identity, correct geometry, and no idle work.
This diagnostic allowance does not change the production duration or establish a frame-rate guarantee.

Animation window tests now use the existing `XUI_DESKTOP_TESTS` registration gate.
Configuration checks passed with the gate both disabled and enabled.
The headless model tests remain registered in both modes.
Enabled animation window tests carry the `desktop` label and `RUN_SERIAL`.
This change prevents a default headless CTest run from opening the new fixture windows.

#### Determinate progress follow-up

Opt-in Progress interpolation passed native model and window fixtures on 2026-09-17.
The gallery includes normal, slow, and immediate durations, plus reset, retarget, and completion actions.
The focused `--progress-motion-only` fixture checks immediate logical UIA values, native identity, mode changes, and idle work.
It passed after the ComboBox commitment correction described below.
Managed `--progress-animation` passed 32 assertions. Rust `progress_animation_contract` also passed.
Cargo reported an incremental-directory access warning, but compilation and the contract test succeeded.
Expander model and window regressions passed with the same native build.
`ScalarTransition` remains float-valued. Progress uses its easing weight to interpolate double endpoints.

#### Retained content-state follow-up

The `content-motion` gallery source adds loading, empty, and results content in one fixed grid cell.
These are manual state transitions, not asynchronous requests or a new PageView API.
The example closes outgoing input immediately and preserves the native result editor for later states.
The focused fixture checks native movement, query focus and bounds, result text/undo/identity, rapid reversal, and immediate mode.
MSVC syntax checks passed for the gallery and smoke source.
The catalog syntax check passed after the command supplied its CMake-provided `XUI_SOURCE_DIRECTORY` definition.
Documentation checks passed with 52 pages, 601 local links, and 19 site tests.
The focused `--content-motion-only` run passed on 2026-09-17.
It observed intermediate native positions, retained HWND/text/undo, immediate input exclusion, reversal, query bounds, focus, and immediate mode.

The first focus assertion incorrectly assumed that an action button would preserve query focus.
The observed focus after Show results was the Show results button, not the incoming editor.
The sample now also accepts Enter in the query to show results without a button action.
Enter in the result note selects loading while the outgoing editor still owns focus.
These native submit paths exercise both focus preservation and explicit focus return.
The state buttons retain normal activation behavior.
Editor submit handlers hold a weak state callback to avoid cycles through the retained editors.

#### Gallery duration commitment correction

The previous gallery helper selected a ComboBox popup preview without committing it.
UIA SelectionItem selection alone does not call the ComboBox change callback.
The shared `choose_combo` helper now presses Enter and requires both popup closure and the committed selection.
Animation, Progress, and content fixtures use this helper.
Animation and Progress modes passed with actual slow and zero-duration settings.
Earlier duration-choice assertions did not establish those settings and must not count as immediate-mode evidence.
The new content run also passed with committed duration choices.

The separate `build\animation-preview` Explorer output was refreshed with the Progress native DLL.
Its complete smoke passed navigation, panes, tabs, menus, filtering, sorting, columns, commands, detached previews, lifetime, native copy, images, and transfers.
Documentation checks passed with 52 pages, 603 local links, and 19 site tests.
These results do not establish visual approval, physical-monitor DPI behavior, or a universal frame-rate guarantee.

#### Concurrent animation scale fixture

`animation_stress_window_tests.cpp` adds bounded workloads with 1, 16, and 64 retained Reveal/Progress pairs.
Each pair includes a native editor. The fixture compares fixed and expanding layout at the same window size.
It warms both endpoints before recording CPU time, paints, layouts, peer counts, native buffer bytes, and GDI objects.
The workload includes reversal, native identity checks, idle settlement, and host hiding.
The test reads the system motion preference without changing it.

CPU observations include the fixture observer, native composition, and rendering.
They do not isolate scheduler cost or represent an application benchmark.
The fixture does not enforce a machine-dependent frame-rate threshold.
It requires no intermediate root layouts for fixed placement and no animation timer, paints, or layouts after settlement.
Its own bounded observation timer remains active during the idle assertion.
The desktop registration is serial and uses `XUI_DESKTOP_TESTS`.
The native ARM64 Release fixture passed on 2026-09-17 with system motion enabled.

The first version incorrectly included logical target changes and terminal closure in its zero-layout assertion.
Fixed Reveal still releases its reserved slot at terminal closure.
The corrected fixture classifies root measurements as target changes, terminal closure, or intermediate frames.
Each run recorded five target layouts and two closure layouts.
An early idle assertion also called the explicit update message on every observation.
That message requests painting, so the observer itself caused the reported idle paints.
The corrected observer does not request per-tick updates or paints.
It permits one message-pump turn for final native visibility messages, then requires a quiet 250 ms interval.

One completed run produced these observations:

| Pairs | Layout | Elapsed ms | CPU ms | Paints | Intermediate layouts | Peak native buffer bytes |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Fixed | 2649.89 | 203.125 | 114 | 0 | 6144 |
| 16 | Fixed | 2775.47 | 984.375 | 100 | 0 | 52224 |
| 64 | Fixed | 3487.95 | 1390.62 | 40 | 0 | 199680 |
| 1 | Expand | 2637.09 | 281.25 | 112 | 100 | 6144 |
| 16 | Expand | 2667.74 | 906.25 | 111 | 99 | 52224 |
| 64 | Expand | 3045.75 | 1328.12 | 57 | 45 | 199680 |

All runs retained their native editor identities and 4, 49, or 193 peers.
GDI object counts remained at their warmed values: 20, 50, or 146.
Reversal, idle shutdown, and whole-window hiding passed at every size.
Paint counts are not displayed-frame counts. CPU values include the observer and the complete native rendering path.
This single shared-desktop run does not establish that expansion is faster than fixed placement.
The larger native-editor workload reduced paint delivery despite fewer intermediate root layouts.
It supports the existing warning that placement-only motion still incurs rendering and native composition cost.

The tab gallery source now provides Reverse document order and normal, slow, and immediate duration choices.
The new `--tab-motion-only` fixture checks intermediate accessible positions, stable selection/identity, interruption, insertion/removal, and immediate mode.
The native tab model/window fixtures, gallery catalog, tab gallery, and revised content gallery passed.
The window fixture includes crossing pixels, hit targets, UIA order/runtime identity, native-peer retention, and idle shutdown.
Crossing tabs narrow to avoid overlapping input rectangles. Visual preference for this effect remains unconfirmed.
Managed tab bindings passed 23 assertions. The Rust tab-duration contract passed with reorder calls.
Cargo again reported incremental-directory access warnings without a build or test failure.
The separate `build\animation-preview` output contains the new tab implementation and passed the complete Explorer smoke.

#### Navigation-group design boundary

A09 investigation found that a local `NavigationView` duration property cannot preserve the current collection contract.
`VirtualCollection::item_bounds()` uses consecutive logical row offsets, so an exit gap extends a surviving row's hit and UIA bounds.
`CollectionProvider::with()` rejects removed keys through the logical source.
Keeping outgoing keys there would retain interactive accessible identities.
The renderer also reads images through `source()->visual(row.index)`, which cannot safely use indices from a retired row list.
Repeated source replacement would trigger structure changes, focus repair, hover resets, and scroll clamping.
No partial API or substitute navigation demo was added.
The [animation proposal](../specs/animations.md#proposed-collection-presentation-foundation) records the required shared geometry and draw-only ownership phase.

#### Native document motion

The gallery now has 52 entries, including `document-motion`.
Its native `--document-motion-only` smoke passed on 2026-09-17.
The run covered actual native RichEdit movement, immediate input exclusion, reversal, text/selection/undo retention, editable policy, and zero-duration behavior.
The separate `--document-motion-winui` run also passed with the same assertions.
That mode checks the gallery's active WinUI style before it exercises motion.
It uses the full gallery rather than the separate WinUI experiment catalog.
Both desktop registrations run serially and remain behind `XUI_DESKTOP_TESTS`.
The gallery catalog also passed.
The first smoke lookup incorrectly expected native RichEdit to expose the model automation ID.
The corrected fixture uses the accessible name published by `NativeDocumentBridge::sync()` through `EM_SETUIANAME`, as existing document fixtures do.
No accessibility provider replacement was added.
The first native fixture run completed MultilineText fixed mode, then failed its intermediate-layout assertion in a later case.
A queued update after reversal repeated baseline progress and layout counts.
The fixture incorrectly required another layout despite unchanged geometry.
The corrected assertion requires additional layout only when expanding geometry changes.
Fixed-mode zero-layout checks and all native clipping, input, and identity assertions remain intact.
Two subsequent native runs passed all four MultilineText/RichText and fixed/expanding combinations.
The final run observed 86, 84, 85, and 86 intermediate native placements, respectively.
Coverage includes paint-DC clipping, native UIA bounds/read-only semantics, rich formatting, undo/redo, reversal, focus return, hiding, and idle shutdown.
No production fix was necessary.
The demo does not claim media, WebView, opacity, snapshot, or IME acceptance.

#### Detached preview client entry

`PreviewLayout.xui` now wraps the body in a fixed Reveal with an explicit 180 ms duration.
`PreviewSession` opens that Reveal once, after the first content result reaches the UI thread.
Loading, cancellation, image decoding, and window lifetime remain independent of entry.
The caption and status row remain stationary.

The separate `build\animation-preview` output built without warnings and passed the complete Explorer smoke on 2026-09-17.
The added fixture uses a 1200 ms duration and observes real intermediate native RichEdit positions.
It checks native focus, selection, read-only text, stationary caption/status geometry, and the stopped animation clock.
Zero-duration entry and closure during a 10,000 ms diagnostic entry also passed.
Queued closure completes in less than two seconds, rather than waiting for that entry duration.
The first closure assertion incorrectly required synchronous HWND destruction inside `Dismiss`.
`Window::close()` posts `WM_CLOSE`, so the corrected assertion checks completion after message delivery.
The existing metadata, image, cancellation, native copy, and opener-first lifetime cases also passed.
No running user output was overwritten.

The next acceptance pass kept a folder preview in a 10,000 ms entry while it closed and disposed Explorer.
The ownerless preview retained its own active animation clock after opener destruction.
The native text-copy, image-resize, captured-folder Open, and final-window retirement checks still passed.
A deleted-file error also left the content reveal closed and inactive.
The complete Explorer smoke passed again with these new assertions, and the separate preview output was refreshed.

#### Overflow tab reveal

The 2026-09-17 continuation adds A07 source through the existing opt-in tab duration.
Selection remains logical and immediate. A separate presented offset moves the overflow viewport.
The retained New tab button stays stationary.
Clipped tab rectangles drive drawing, hit targets, close targets, and accessible bounds.
Reversal starts at the current offset. Overflow topology changes still settle immediately.

The gallery adds Fill overflow, First document, and Last document.
Model coverage includes fractional geometry, disjoint targets, reversal, metadata refresh, resize, immediate mode, and idle invalidation.
The native fixture adds owned-window pixels, UIA geometry, close actions during reversal, New tab access, and retirement.
The gallery fixture also observes actual timer-driven accessible movement.
Initial `/Zs /W4 /WX` checks passed for the model, control, gallery, and gallery-smoke source.
Native compilation initially waited for the collection foundation handoff.
The existing preview retained the preceding delivery during that source-only stage.
Source inspection also found an existing narrow-viewport error in `reveal_selected()`.
Its 120-DIP minimum could exceed the actual tab width and discard visible neighbors while it searched for an impossible width.
The minimum now respects actual tab width, with the existing geometry tolerance for fractional edges.
New model assertions cover narrow immediate and animated endpoints.
Overflow now reserves the New button slot at the viewport edge, even when the final tab leaves unused space.
This placement also applies in immediate mode. A viewport that fits all tabs restores the leading tabs.
The updated control source and native fixture also passed strict syntax checks.
The final control and model source passed another strict syntax check after the narrow-viewport corrections.
The documentation check passed for 52 pages and 603 local links.

The next continuation adds Explorer-specific overflow acceptance to `ExplorerSmoke`.
It fills a bounded tab strip, selects an offscreen document through `FilePaneView`, and clicks the prior displayed viewport before the first frame.
The fixture checks immediate logical selection, native pointer targeting, file focus, New-button identity, reversal, and immediate mode.
It closes only the tabs that it created and restores the original selection and duration.
The managed-only build passed with no warnings or errors.
`XuiCopyNativeRuntime=false` kept that compilation separate from native runtime delivery.
The subsequent coordinated build passed the model, native tab window, and tab gallery fixtures.
This includes intermediate overflow pixels, UIA geometry, native close actions, and timer-driven gallery movement.

The full Explorer smoke then failed its explicit overflow-clock assertion.
Selection and file focus were correct, but the clock was inactive despite enabled system motion and a 1200 ms duration.
`TitleBar::arrange()` first assigned generic tab widths, then assigned pane-aligned widths.
Those temporary widths settled animations during otherwise unchanged root layouts.
The source correction now arranges each strip only at its final aligned or generic rectangle.
New model assertions cover both aligned strips, insertion, removal, reorder, overflow, and genuine viewport resize.
Strict syntax checks passed.
An isolated model run then reproduced the failure against the last accepted core archive.
That baseline stopped at `Unchanged pane-aligned titlebar layout preserves insertion`.
The same fixture passed after linking the corrected `titlebar.cpp` object before that archive.
This run covered both aligned strips and genuine resize settlement without rebuilding shared libraries.
The source and archive used the same ARM64 Release runtime configuration.
No production DLL or preview output changed during this check.
The subsequent standard CMake build passed the corrected model and native tab fixtures.
The tab gallery also passed.

The earlier Explorer insertion/removal fixture inferred motion policy from the animation clock.
It could therefore accept missing animation as reduced motion.
The corrected fixture reads the OS preference independently and requires an active clock when motion is enabled.
Earlier Explorer smoke passes do not prove visible tab motion through this host path.
The complete Explorer smoke then passed with the stricter checks and the title-bar correction.
The separate `build\animation-overflow-preview` output includes this runtime and the navigation-group opt-in.
One preceding full run failed a later palette-history assertion.
The next run passed after adding exact failure diagnostics, without a palette implementation change.
The cause of that isolated palette failure remains unconfirmed.

#### Collection presentation foundation integration

The source handoff adds immutable single-column presentation bands, explicit clips, and a separate draw-only outgoing cache.
`src\collection_presentation.hpp` defines the internal snapshot.
Collection drawing, input, accessibility, image synchronization, and scrolling consume that geometry without replacing the logical source per frame.
Outgoing image requests use frozen visual metadata rather than stale indexes in the current source.
Presentation retirement clears the frame and notifies the future transition producer.

Both new fixtures are registered in CMake.
The window fixture is desktop-gated and serial.
The first ARM64 Release run passed `xui_collection_presentation_tests` and the expanded `xui_tab_animation_tests`.
The presentation native fixture and existing collection/navigation window regressions also passed.
Existing collection, navigation, NavigationView, and collection/navigation style model fixtures passed.
This accepted foundation supports the subsequent NavigationView producer.

The next continuation stages `--navigation-motion-only` in the gallery fixture.
It uses the existing nested NavigationView example, not a replacement expander.
Its assertions cover intermediate surviving-row positions, immediate removal of outgoing UIA rows, ancestor focus repair, pinned sections, and native search identity.
It also covers reversal, preserved selection, filter interruption, and immediate mode.
The fixture reads system motion independently, so a missing animation clock cannot silently pass.
Strict ARM64 syntax checks passed.
The native handoff subsequently added the producer and C/C#/Rust duration APIs.
Explorer opts into 180 ms, and the gallery provides normal, slow, and immediate modes.
The navigation model, existing navigation-view/style models, and C# 15-assertion and Rust binding contracts passed.
The registered focused navigation gallery fixture also passed.

The first gallery assertion captured pinned geometry before the duration popup and focus changes.
The corrected baseline follows those actions.
A second assertion expected reversal to restore a pre-collapse scroll offset that extent clamping had changed.
The fixture now reveals the first logical row before geometric reversal checks.
It still requires actual intermediate movement, immediate logical retirement, focus repair, stable native search, and compatible final geometry.

The dedicated native navigation fixture failed `Settled navigation has no idle repaint or layout`.
Its synchronous `flush()` left previously posted updates in the message queue.
The corrected setup dispatches those updates with a bounded limit, then paints existing damage before the idle baseline.
It neither discards messages nor repeatedly waits for a quiet interval.
Two consecutive native runs passed Classic and WinUI.
Each style dispatched one queued update and one paint before the baseline.
The subsequent 180 ms observation recorded no update, paint, layout, or animation work.
The fixture also requires released presentation and unchanged anchor focus. No production change was necessary.
Additional Explorer coverage uses native Tab, Home, Left, and Right to exercise real sidebar groups.
Those checks passed collapse, expansion, reversal, immediate mode, and native search retention in the latest run.
That run later missed intermediate samples in the existing pane fixture despite an initially active transition.
The pane failure now includes elapsed-time diagnostics.
The next complete Explorer smoke passed, including the native group actions and strict tab checks.
The isolated pane sampling miss did not recur. Its cause remains unconfirmed.

The next compiler-only slice adds reactive NavigationView `duration` authoring in `.xui`.
Explorer now declares 180 ms in `SidebarLayout.xui` instead of setting it in the controller constructor.
The generator fixture checks initialization, omitted defaults, retained search identity/text, cached refreshes, invalid types, and range rejection.
The generator suite passed 41,933 assertions.
The managed-only Explorer build passed without warnings or errors.
No desktop run or shared native build occurred during this slice.
After the native fixture released the desktop slot, the full Explorer build and smoke passed with this declarative opt-in.
The refreshed gallery catalog and navigation-motion smoke also passed.
The current verified preview is `build\animation-overflow-preview\FileExplorer.exe`.

#### Retained PageView entry, September 18, 2026

The gallery `pages` example now composes a real PageView with a fixed Reveal.
The application selects the next page immediately and restarts directional entry at its edge.
It does not retain outgoing pixels or add a PageView duration API.
Normal, slow, and immediate choices expose the transition.

The Release gallery, smoke harness, and catalog builds passed.
The catalog and both `xui_page_motion_gallery_smoke` and `xui_page_motion_winui_gallery_smoke` passed.
The native checks recorded actual editor positions during right entry and left return.
They also required immediate outgoing input retirement, typing during entry, fixed neighboring geometry, and retained HWND, text, selection, and undo.
Rapid switches left only the final page active. Zero duration switched immediately without an animation clock.
Each animated endpoint released the shared clock.
These results establish native geometry and input behavior, not subjective visual approval.
Documentation checks passed for 52 pages and 606 local links.
The runnable gallery is `build\ARM64\Release\xui_gallery.exe --page pages`.
The verified Explorer preview remains unchanged while the separate palette-entry implementation awaits acceptance.

#### Explorer view entry, September 18, 2026

This section records the earlier animation experiment.
Later user feedback removed the view Reveal and replaced this check with `--view-switch-smoke`.
The current check requires stationary content and no animation timer.

Details and Columns now share a fixed Reveal in `FilePaneLayout.xui`.
The controller opts into 180 ms after initial construction.
A deliberate mode change requests entry when its filtered rows arrive.
Rapid mode changes retain only the latest request. Navigation, cancellation, and later filtering settle active motion.
Initial population and cold navigation do not request entry.

The separate `build\animation-views-preview` Release build passed without warnings or errors.
Its focused `--view-entry-smoke` passed directional intermediate geometry, fixed surrounding layout, immediate mode ownership, and outgoing native-peer retirement.
It also passed selection and 320-DIP scroll-offset retention, original Details HWND retention, rapid switching, navigation cancellation, and zero duration.
The first focused run inspected native focus before the queued visibility update and failed.
The fixture now dispatches one normal native update before that immediate-retirement assertion. It does not advance the animation clock.
The corrected focused run passed without a production change for that assertion.

Full acceptance stopped earlier in the new palette fixture at `Palette dismissal must immediately retire native peers and entry work.`
That failure precedes the view checks. The palette workstream owns its investigation.
The candidate is runnable, but `build\animation-overflow-preview` remains the latest fully accepted Explorer preview.
The A14 catalog row remains provisional until integrated acceptance passes.

#### Palette and view integration follow-up, September 18, 2026

The palette failure came from a fixture assumption about native cleanup.
Dismissal immediately hides and disables popup peers and restores focus.
The normal later update destroys the HWNDs. The fixture originally required destruction inside the dismissal callback.
The corrected fixture separates immediate visibility, cancellation, and focus checks from bounded deferred cleanup.
It requires destroyed peers and a stopped Reveal within two seconds, including ten-second diagnostic entries.
No production dismissal change was necessary.

Palette acceptance now covers actual result-peer movement, stationary query/frame geometry, native typing, selection, undo, and retained open-generation HWNDs.
It also covers cold-query retirement, repeated Escape, command execution during entry, and zero duration.
Cold queries now remove stale logical rows rather than retain disabled suggestions.
Each new popup generation resets entry. Subsequent query edits do not restart it.

The first integrated rerun passed the palette checks, then missed every intermediate pane sample with an 800 ms diagnostic duration.
The assertion ran after 1,022 ms and retained the expected native editor, text, and focus.
Additional diagnostics now record input readiness and the first observation before and after its explicit native update.
The next complete smoke passed, including A14, A23, existing tab/navigation/pane/Find checks, previews, and transfers.
That run recorded 15 intermediate pane samples, input readiness at 64 ms, and the first sample at 85 ms.
The first explicit update ended at 92 ms. The final assertion ran at 1,615 ms.
These timings include asynchronous observer scheduling. They are not animation frame-time measurements.
The intermittent missed-sample cause remains unresolved. No pane assertion or production duration changed.

The Release build passed without warnings or errors.
The latest fully accepted runnable Explorer preview is now `build\animation-views-preview\FileExplorer.exe`.
The older `build\animation-overflow-preview` output remains intact.
A14 and A23 now have integrated acceptance. Their scope remains incoming content only, with immediate outgoing ownership changes.

#### Animation queue fairness, September 18, 2026

The isolated `--pane-animation-smoke` exposed a native timer-delivery defect.
One failure recorded zero timer dispatches and zero progress from 235 ms through 4,930 ms.
The first timer arrived at 5,082 ms and immediately reached the endpoint.
Posted updates outranked the generated `WM_TIMER` and `WM_PAINT` messages.
This was not only a delayed managed observer.

`animation_queue_window_tests.cpp` reproduced the defect with a bounded stream of posted messages.
The baseline processed 603 messages over 1,500 ms, with no animation timer or intermediate paint.
The window message loop now services due animation timers between dispatches and worker deliveries.
The service uses the existing window timer, applies its queued geometry update, and paints the frame.
It adds no worker, extra timer, or idle wakeup. Inactive windows bypass clock sampling.
The normal update path still does not advance the animation clock.

The first service revision exposed an ordering defect in the native Reveal fixture.
It could retrieve another timer before applying the pending animation geometry.
The corrected service presents that frame before it retrieves another timer.
Native retirement guards protect both timer and update dispatch.
The queue fixture warms initial native rendering before its measured traffic. It does not include cold initialization in that workload.

Final native acceptance passed Reveal, TabStrip, NavigationView, Expander, Progress, native documents, concurrent stress, and queue fairness.
The expanded queue fixture also passed zero-duration traffic and retirement of one active window while another window continued.
Its idle checks required no animation timer, additional paint, or layout after settlement.
A separate tab UIA failure hit the conversation window of another process, not a tab.
The owned test window now stays above unrelated windows without activation. Its strict point-target assertion passed.

Private metrics now count actual timer delivery and successful intermediate SplitView and Reveal paints.
The Explorer fixture requires those actual paints instead of relying only on asynchronously sampled progress.
It also reads pane motion policy independently, rather than interpreting an already completed animation as reduced motion.
Observed cold-start runs consumed most or all of the diagnostic duration before the first managed sample.
Some had real intermediate paints despite zero managed samples. Others had no intermediate paint.
The scheduler cannot interrupt expensive callbacks or initial rendering, and this work does not establish a universal frame-rate guarantee.

The final full Explorer smoke passed with 14 managed pane samples and 29 successful intermediate pane paints.
The refreshed gallery passed popup, tabs, navigation, and retained-page entry in Classic and WinUI.
The final queue-lifetime fixture passed in 10.83 seconds.
The latest fully accepted preview is `build\animation-fairness-preview\FileExplorer.exe`.
The earlier previews remain intact. Physical DPI changes, IME sessions, and subjective visual approval remain outside this automated evidence.

Nonanimated compatibility checks passed multiwindow ownership, the application ABI, navigation input, WinUI lifecycle, and split-resource retirement.
The default `xui_window_tests` failed its retained-page buffer-growth assertion.
Peer count increased from 4 to 26, but composition buffer bytes remained 57,456. Animation timers and ticks remained zero.
An isolated executable replaced the current window host with `HEAD`'s unchanged `src\application.cpp`.
It reproduced the same assertion and values.
That comparison used current headers, core, drawing, and other libraries. It isolates host changes, not every animation change.
The buffer-growth failure remains unresolved. The assertion remains intact, with diagnostics for later investigation.

The queue fixture then exposed a shutdown defect in the new frame service.
Filtered `PeekMessageW` calls can retrieve `WM_QUIT` regardless of the requested message range.
The service consumed that message and lost the exit code.
The regression drained a real animation update, posted exit code 37, and failed its bounded shutdown guard before the correction.
Both filtered paths now repost the quit message and return to the outer message loop.
The single-window fixture removes its own normal teardown quit before it starts the next independent case.

A second queue regression exposed dropped native child timers.
The HWND filter includes child messages, but the initial service dispatched only messages whose HWND matched the top-level window.
The fixture received zero child-timer callbacks during posted traffic, despite intermediate animation painting.
The corrected service checks the retained owner lifetime, then dispatches the retrieved message to its actual target.
It does not add or accelerate native child timers.

#### Overnight checkpoint, September 18, 2026, after 03:00 UTC-05

The corrected queue fixture passed three consecutive runs, including native child timers and exit-code preservation through both application entry points.
The rebuilt Reveal and application ABI fixtures passed.
The rebuilt gallery passed popup, tab, navigation, and retained-page entry, including the WinUI page case.
The final full Explorer smoke passed with 19 managed pane samples and 40 successful intermediate pane paints.
Native input was ready at 50 ms. These observations describe this run, not a frame-rate guarantee.

The accepted preview is `build\animation-final-preview\FileExplorer.exe`.
Its native DLL hash matched `build\ARM64\Release\xui.dll` before the full smoke run.
Earlier previews and running user applications remain unchanged.
The shared disk became full during acceptance. Cleanup removed four exact, rebuildable linker intermediates from this session.
The build products and application data remained intact.

The nonanimated multiwindow fixture intermittently failed `No wrong-window shortcuts` during two acceptance batches.
An isolated baseline-host executable passed eight consecutive runs.
The current host also passed eight consecutive runs after the fixture gained failure-only shortcut-count diagnostics.
No production routing change followed that diagnostic addition. The intermittent failure remains unexplained, not fixed.
The retained-page resource assertion described earlier also remains unresolved.
This checkpoint does not claim a fully green repository suite or subjective visual approval.
Documentation source checks passed for 52 pages and 606 local links, plus 19 adapter tests.
The separate rendered-site check found no generated HTML. This run did not rebuild the documentation site.

The overnight implementation interval ended after its requested horizon.
The catalog still identifies compact/adaptive navigation, additional collection topology, indicator effects, overlay exit, and advanced rendering as future work.
Physical DPI changes, IME sessions, media/web hosts, and manual visual acceptance remain outside the delivered evidence.

#### Palette review and Motion discovery, September 18, 2026

User review accepted the other Explorer motion but rejected the palette result slide.
The shared command/location palette now renders results directly, without a Reveal or entry-animation state.
Cold queries still clear old logical rows. Cancellation and deferred native cleanup retain their existing contracts.
A whole-palette pop-in remains planned. This change does not provide scale or opacity animation.

The full Explorer smoke passed in `build\palette-static-preview\FileExplorer.exe`.
The revised palette checks cover immediate visible results, stationary geometry, and no animation timer after unrelated motion settles.
They retain native typing, editor identity, selection, undo, execution, Escape, focus return, and deferred cleanup checks.
The same run passed the other Explorer animation and application regressions.
The build reported no warnings or errors. Earlier preview outputs remain unchanged.

The existing gallery playground now appears as **Layout > Motion**, with its stable `animations` page ID.
Catalog checks passed search by both `motion` and `animations`.
The native motion-page smoke passed its heading, reversal, native-editor, pane, duration, and idle checks.
Documentation source checks and the adapter tests passed.

### Native input and retained content

Native file dialogs have core, native Shell, XUI window, C ABI, and managed fixtures.
`tests\file_dialog_test_probe.hpp` finds only current-thread dialogs owned by the exact fixture window.
It records callback errors without throwing through a native timer.
The native window fixture covers closure and public-owner deletion while `IFileDialog::Show` owns its modal loop.
The ABI and managed fixtures cover candidate and active content-scope rejection.
The [dialog procedure](../../CONTRIBUTING.md#native-file-dialogs) lists commands and manifest requirements.

Document range editing has separate core, native, ABI, and managed fixtures.
`tests\document_editing_tests.cpp` checks validation before adapter dispatch.
`tests\document_editing_window_tests.cpp` uses real RichEdit controls with an owned notification parent.
It checks exact text and selection, independent undo actions, prior typing history, redo, stale native text, callback lifetime, and maximum length.
Native notifications stay suppressed during the range transaction. The model publishes one change after native calls return.
The fixture includes owner deletion from that callback and composition-message rejection.
`tests\document_editing_abi_tests.cpp` checks invalid spans, status values, thread ownership, and output preservation.
`bindings\dotnet\Tests\DocumentEditingTests.cs` checks the managed edit path and native tree selection.
The [document procedure](../../CONTRIBUTING.md#document-range-editing) lists the commands.

`tests\content_host_window_tests.cpp` covers the native retained content boundary.
`bindings\dotnet\Designer.Preview.Tests` covers its C ABI and managed ownership through the embedded preview.
The managed harness measures 100 replacements, repeated rollback, live handles, source-version delivery, and managed callback recovery.
It keeps the same native editor and checks text, selection, undo availability, focus, and foreground activation.
These desktop checks need the matching native DLL.
Compiler-only `Designer.Tests` retains its rule that no native XUI library loads.

The explorer adds these regressions:

| Program | Coverage |
| --- | --- |
| `xui_explorer_tests` | History commits, failed navigation, tab selection and closure, cancellation, bounded state, UNC roots, long Unicode scans, activation, and injected file associations |
| `xui_explorer_smoke` | The real explorer, address input, history, keyboard shortcuts, context commands, independent panes, tab providers, divider input, clipping, and resource bounds |
| `xui_tab_window_tests` | Owned-window tab pixels in Classic and WinUI, light/dark/high contrast, 96/120/144/168/192 DPI, open bottom edges, titlebar gap borders, empty rows, custom colors, content activation, close targets, New tab placement and UIA invocation, focus, and overflow |
| `xui_tab_drag_window_tests` | Queued-paint capture retention, hidden/disabled cancellation, reorder, retained-HWND tear-out, first-show remainder Z-order, reversible hover joins, layered overlays, release-only fallback, cancellation, placement, full-width secondary panes, native focus, and retirement |
| `xui_tab_drag_indicator_tests` | Focused insertion-marker pixels through `xui_tab_window_tests --drag-indicator`, across both styles, all themes, and multiple DPI values |
| `xui_suggestion_tests` | Folder prefixes, real and synthetic enumeration limits, deterministic cancellation, native EDIT behavior, popup input, themes, and closure during a blocked request |
| `xui_split_window_tests` | Eight window cycles with tabs, two lists, native fields, capture cancellation, simulated DPI, target recreation, and final resource disposal |

The desktop tests require `-DXUI_DESKTOP_TESTS=ON`.
The keyboard test requires foreground ownership before it sends input. It never sends a shortcut to another application.
The file-association test records the exact selected path through an injected dispatcher. It does not start fixture files.
The smoke test uses scoped UIA focus-property events, selection events, and structure events.
Its optional `--global-focus-events` argument also subscribes to desktop-wide focus events.
That optional subscription can stall inside Windows before a test action. It depends on providers outside this process.

The drag fixtures use owned windows and are available with `BUILD_TESTING`.
Their scripted caption-down boundary permits deterministic native callback checks without a physical mouse press.
Full-window dragging uses `WM_WINDOWPOSCHANGING`, including native hide and show flags.
Outline-only dragging uses `WM_MOVING` and retains release-only merge.
The live-loop probe reports when user32 declines an operation without a held mouse button.
Target acceptance depends on actual desktop occlusion. A covered target exercises rejection instead.
The fixture reports that substitution instead of claiming an accepted merge.
Dedicated hover cases expose temporary topmost fixture windows without activation.
Those cases check hover transfer before release, departure, rejoin, rejected commit, cancellation, detached dimensions, and first-show remainder Z-order.
They also disable a joined target after the last motion and require release-time recovery instead of a stale commit.
An opaque layered fixture blocks merge.
Zero alpha, layered pass-through input, and a region hole each permit merge.
After tear-out and departure from a target, the pointer offset must remain within one physical pixel.

The 2026-09-17 ARM64 development run passed the focused drag, insertion-marker, placement, native ABI, managed binding, and model checks.
An unobstructed run exercised native `QueryDrop` and accepted `Drop`.
The full tab-pixel fixture completed its inner assertions but exceeded its 300-second duration requirement.
That run is not a passing result for the full fixture.
Physical pointer continuity, Escape delivery through user32, and mixed-monitor dragging still require manual coverage.

The 2026-09-17 follow-up to checkpoint `cbd5ebf` passed the full-window hover, immediate Z-order, native-focus, pointer-offset, and overlay regressions.
The C ABI, managed drag, and managed split-visibility checks also passed.
The captured full Explorer smoke passed with the isolated `HoverJoinValidation` output.
The final isolated build matched the latest native DLL and passed the full smoke after the no-op Join and release-time revalidation changes.
The pure Explorer suite passed 78,120 assertions.
An earlier full-smoke process returned exit code 1 without captured diagnostics. The passing rerun does not establish that the full smoke is flake-free.
The native loop probe still declined entry without a physically held mouse button.
These results cover native messages and application transfers, not a complete physical drag gesture.

The managed explorer `--smoke` also covers file transfers in its temporary fixture.
Its tab-drag probes cover same-window and cross-window transfers, limits, stale targets, QueryDrop without mutation, rollback, and continued window lifetime.
Hover probes cover repeated destinations, closed-target recovery, and window retention through `Completed`.
Repeated Join at equivalent insertion slots preserves pending navigation, selection, and content.
Secondary-tab tear-out also checks full-width content at normal and narrow sizes, with layout restoration after cancellation and completion.
Its preview checks cover native Space dispatch, held Space, focus restoration, text selection, and command isolation.
They also cover Details and Columns, bounded text, image readiness and errors, metadata, deletion, replacement, and cancellation.
`PreviewController.cs` owns the popup and request lifetime. `PreviewLayout.xui` defines its layout.
`PreviewMetadataLayout.xui` defines the centered icon-and-details view.
`Models\FilePreviewService.cs` owns format classification and bounded text reads.
The model tests cover encoding, binary rejection, byte and character limits, complete surrogate pairs, metadata, and canceled requests.
The preview smoke also reads the native document font and checks borderless styling, quiet status, and metadata icon geometry.
These checks do not prove screen-reader speech or image quality on physical monitors.
Navigation checks cover exact-directory Enter, trailing-slash child results, cached completion, Tab, parent queries, and query history.
Drive-root checks cover bare and slash-terminated drive queries without automatic child activation.
The native New tab buttons create tabs in their respective panes and restore file focus.
The model checks compare cached and scanned suggestions for relative, quoted, expanded, and slash-terminated paths.
Footer checks cover upward view selection, both choices, Escape, focus restoration, and independent pane feedback.
Feedback timing checks cover the three-second lifetime, replacement cancellation, singular counts, and persistent errors.
Native Miller capture checks cover row hover and pixel-aligned vertical separators across themes and DPI values.
Column Find checks cover independent queries, hidden ancestor selections, preserved descendants, target changes, and tab duplication.
Native header and empty-area focus checks distinguish column focus from row selection and navigation.
Command snapshot checks require eager availability evaluation, stable row content, and no action execution during source callbacks.
The smoke opens and filters Copy/Cut commands with selected entries in both Details and Columns views.
These checks cover the native callback guard that rejects selection queries from an immutable source callback.
The checks include multi-selection menus, folder-row copy, empty-area move, nested folder content, and refresh in both panes.
File rows and panes with obsolete rows must reject drops.
Native text fields must retain their text clipboard shortcuts.
The smoke does not replace the interactive desktop clipboard.
These controller checks do not establish interoperability with every external application or Shell extension.
`xui_file_transfer_tests` covers grid drag gestures, clipboard serialization, the OLE drop protocol, and Shell file operations.
`xui_file_clipboard_tests` runs clipboard round trips in a job-bounded child with a private window station and desktop.
The shared `tests\private_desktop.hpp` helper also isolates the existing native editing tests.
The parent process requires the interactive clipboard sequence to remain unchanged.

Run the explorer checks:

```powershell
ctest --test-dir build\arm64 -C Release -R "xui_(explorer|split_window)" --output-on-failure
.\tests\measure-browser.ps1 -Runs 5 -WorkspaceCycles -Screenshots -Output build\explorer\final-memory.json
```

`-WorkspaceCycles` adds a second pane, creates 15 inactive tabs, closes those tabs, and performs 40 folder transitions.
It then hides the second pane and records two idle samples.
The protocol retains the original 60-file workload, 924×641 client extent, and selection/filter workload.
Native child discovery now supports pane hosts. The theme workload uses the Theme command instead of F6.
The baseline path still supports the original F6 command.

The image regressions require `-DXUI_DESKTOP_TESTS=ON`:

| Program | Coverage |
| --- | --- |
| `xui_image_tests` | Exact budgets, complete row-image loading, deferred admission and wakes, reservations, sharing, alpha conversion, size keys, file versions, corrupt data, LRU eviction, cancellation, and device recreation |
| `xui_visual_window_tests` | Actual pixels in every visible row, grid source identity, clipped and hidden controls, and Miller images through navigation, scrolling, filtering, and idle |
| `xui_image_window_tests` | A 20,000-item recycled grid, folder replacements, real pixels, native geometry, clipping, scrolling, idle frames, unload, and blocked-decoder closure |
| `xui_images_smoke` | The actual thumbnail executable, external UIA, native folder input, source errors, image errors, unload, idle, and stale providers |
| `xui_gallery_image_smoke` | The gallery preview, real decoding, external image semantics, native path input, corrupt files, unload, idle, and provider disconnection |

`xui_image_tests` writes original PNG fixtures through the Windows WIC encoder.
Its malformed files cover zero dimensions, huge dimensions, truncated pixels, corrupt data, missing files, directories, and an oversized encoded file.
Deterministic gates stop the worker before decoding, during a reservation, and before completion delivery.
The tests replace a tile source at each boundary and reject the obsolete result.
The full-queue test holds one active job and fills all 64 queue slots.
Row-image gates cover WIC and Shell queues, deferred source replacement, weak window ownership, cancellation wakes, and queue headroom for explicit images.
Another assertion requires every row to draw when visible images outnumber bitmap cache entries.
The GPU test fills the controlled bitmap budget, rejects an extra upload, evicts a bitmap, and uploads again.

The Miller image regression keeps a thumbnail-enabled sidebar beside the columns.
It opens a path of eight columns while focus stays on the root column.
Owned-window pixel captures verify images in every visible row, including rows beyond the former retained-slot limits.
The regression also scrolls both axes, replaces an ancestor with an empty filtered source, and checks idle paints.

The window test holds a 4 MiB reservation while the decoder gate remains closed.
`Application::run` returns before the test opens that gate.
After the worker resumes, cancellation returns the reservation to zero without a stale control update.
Image assertions use owned pixel bytes and controlled bitmap estimates, not process-memory thresholds.

`xui_core_tests` covers layout, lifecycle, filtering, selection, navigation, and viewport calculations.
It uses explicit assertions that remain active in Release builds.
It also covers scrollbar geometry and text contrast for both built-in palettes.

`xui_navigation_window_tests` captures command palettes in Classic and WinUI at 96/144/192 DPI, with dark, light, and high-contrast themes.
Pixel assertions compare the list background with the padding on all four sides and the gap below the search field.
The same captures cover row hover colors and the shadow outside the frame.

`xui_generic_popup_window_tests` runs the same executable with `--generic-popup`.
It covers the FileExplorer layout: a generic Popup with 12-DIP padding, a native TextInput, an eight-DIP gap, and an ItemsView.
The captures cover Classic and WinUI, both popup backgrounds, dark/light/high-contrast themes, and 96/144/192 DPI.
A magenta vector fills the area behind the popup, so transparent padding cannot pass through a matching window background.
Pixel assertions cover all four padding edges, the search gap, unselected rows, rounded corners, the outer shadow, and dismissal.
The test uses owned-window capture and does not capture the desktop.

One WinRT apartment spans the capture matrix.
The fixture clears cached capture factories before this apartment closes.
This prevents stale factory references between separate `Application::run` calls.

`xui_control_tests` covers control state, disabled actions, pointer capture, cancellation, focus traversal, invalidation, Unicode limits, and retained controls.
It also covers injected text metrics, cached measurement, fixed and automatic sizes, size limits, unbounded flex measurement, scroll reveal, and content ownership.
`xui_scroll_tests` covers DirectWrite measurements, narrow windows, native viewport clipping, UIA bounds, offscreen state, native keyboard input, and focus reveal.
It also covers wheel input over EDIT, thumb drag, keyboard scrolling, disabled descendants, nested Stacks, and an embedded FileList.
Eight window cycles exercise dark, light, explicit high-contrast colors, and 96/120/144/192-DPI messages without changes to Windows settings.
Repeated paints and scrolling must reuse the cached text layouts and the single render target.
The tests compare native text-pattern support with an unmodified EDIT provider.
The tests also require bounded handle counts and rejected provider actions after closure.
The resource sample follows asynchronous snapshot disposal and a short message-pump interval for native provider cleanup.
The combined UIA client/server test reports process handles but does not attribute client thread pools to the framework.
The server-only lifecycle test retains its process-handle limit for eight ordinary windows and eight ScrollView trees.

`xui_scroll_frame_tests` and `xui_winui_scroll_frame_tests` capture viewport pixels between layout and root presentation.
The previous complete frame must remain unchanged during this interval.
The first new frame must match a subsequent full repaint, including native fields.
Cases cover custom-only content, native EDIT and RichEdit content, image placement, three themes, and injected 96/144/192 DPI.
Offsets include fractional movement, partial native clipping, forward movement, and reverse movement.
The fixtures also check native editing and undo after scrolling.

The scroll-frame fixtures use owned-window Graphics Capture with cursor capture disabled.
They defer root painting during the layout capture because Graphics Capture can dispatch messages through COM.
Child placement and native painting remain active, so intermediate pixel copies remain observable.
They exclude unrelated window borders and wait for initial window transitions.
They do not capture the desktop or require an unobscured window.

`xui_window_tests` covers public window ownership, callback closure, startup failure, callback failure, and later runs on the same thread.
It also covers a public list beside other controls, filter delivery, selection callbacks, reentrant closure, and off-thread snapshot disposal.
Two sets of eight repeated windows exercise cancelled refreshes, theme changes, resize, simulated DPI changes, and target recreation.
The DPI cases use 96, 120, 144, and 192 without global display changes.
These cases require one live target during display and no XUI targets after closure.
After warm-up, handle, GDI, and USER counts must remain within a small fixed allowance for Windows initialization.
A blocked-loader test requires window closure to return before the loader can finish.
An isolated public-control server checks list names, automation IDs, independent focus, selection, and retained-provider rejection after closure.
It requires the same manifest and desktop as the applications.
`xui_gallery_smoke` covers UIA roles, names, identity, Invoke, Toggle, disabled action rejection, label updates, and native text.
Its `--reference-only` mode checks every gallery page in each example language.
It compares native document text and clipboard text against the catalog, including deferred pages.
It checks the initial `.xui` tab and shared language selection across eager, deferred, and revisited pages.
It also checks read-only editing, handbook URLs, and the Other links navigation group without opening a browser.
`xui_gallery_catalog_tests` checks reference order, content coverage, and documentation paths against the source tree.
The source map and commands are in [Contributing: Gallery](../../CONTRIBUTING.md#gallery).
It also covers Tab, Shift+Tab, Space, Enter, pointer cancellation, theme changes, idle paint counts, and provider invalidation after shutdown.
The native UIA text proxy can complete focus changes after a method returns.
The browser and gallery probes require stable focus before keyboard sequences instead of accepting a transient focus notification.
The gallery resolves EDIT through its automation tree, by name and control type.
It calls external UIA `SetFocus` without a `WM_NEXTDLGCTL` workaround.
Twelve activation cycles cover a competing test window, minimized restoration, native focus events, and queued Unicode input with a surrogate pair.
An external event client covers Invoke, Toggle, name, enabled, native value, and custom keyboard-focus properties.
The isolated public-list client covers selection events, focus properties, structure changes, scroll properties, and quiet repeated scroll endpoints.
The lifecycle test also exercises `Window::focus` with native EDIT.

`xui_native_integration_tests` covers native selection, replacement, undo, surrogate input, rejected focus, and composition guards.
It checks exact native font heights at 96, 120, 144, and 192 DPI.
A private renderer seam injects `D2DERR_RECREATE_TARGET` after `EndDraw`.
The test requires a replacement frame, one live target, retained text layouts, and a return to idle.
The seam belongs to the internal renderer. It adds no public window action.
Clipboard checks run in a private window station with its own desktop.
They cover public copy, native copy/paste/cut/undo, and a paste limit that preserves surrogate pairs.
The interactive clipboard sequence number must remain unchanged.

`xui_performance_tests` covers immutable views, source generations, cancellation, errors, retirement, and repeated worker shutdown.
It also covers changed collection order, removed identities, hidden selection, and independent focus.
The list cases use 60, 100,000, and 1,000,000 synthetic rows.
Allocation counters require zero allocations during 20,000 selection, focus, scroll, and lookup sequences at each size.
Tests use synchronization and operation invariants, not performance thresholds.

The desktop smoke program creates a fixture folder under its working directory and opens its own demo window.
It exercises native text input, Unicode names, UIA actions, keyboard navigation, scrolling, and shutdown.
It also invokes Refresh through the native context menu.
It exercises theme changes without loss of text, selection, or item identity.
Pointer-message sequences exercise scrollbar paging and thumb dragging.
It also reports the number of submitted rows and custom paint calls during an idle interval.
Live `WM_CHAR` input overlaps a refresh. Another sequence combines 80 query replacements, ten refresh requests, and two theme changes.
The probe requires the final requested generation and the expected row.
It also exercises simulated IME boundaries, process memory counters, and shutdown after observed worker activity.
An interactive Windows desktop is necessary.

```powershell
& ".\$build\Release\xui_windows_smoke.exe" ".\$build\Release\xui_demo.exe"
& ".\$build\Release\xui_gallery_smoke.exe" ".\$build\Release\xui_gallery.exe"
& ".\$build\Release\xui_window_tests.exe"
& ".\$build\Release\xui_scroll_tests.exe"
& ".\$build\Release\xui_native_integration_tests.exe"
```

To include the desktop smoke program in CTest, configure `-DXUI_DESKTOP_TESTS=ON`.
The default Windows CTest configuration also registers some tests that create native controls.
`XUI_DESKTOP_TESTS` adds the remaining interactive smoke and presentation tests.

### Repeatable benchmark

Run the benchmark separately from builds and desktop tests:

```powershell
& ".\$build\Release\xui_performance_tests.exe" --benchmark |
    Tee-Object -FilePath ".\$build\performance-results.txt"
```

The benchmark reports synthetic data creation separately from snapshot construction and filtering.
It performs no directory I/O. Timed workloads exclude process startup.
Each nonempty query reports the median of five runs.
The empty-query result and the interaction sequence each report one run.
The interaction sequence includes selection, independent focus, reveal, and identity lookups.
Checksums and assertions keep that work observable in Release builds.

Windows memory counters report private bytes and the working set.
The memory sample includes source items, cached names, indices, views, and allocator retention.
It excludes the directory identity map, graphics resources, and external UIA clients.
Refreshes can temporarily retain both old and new sources.

### Framework performance pass, September 19, 2026

The baseline was `6ef78129de43555027c755f18d7c6043190b7125` with the new benchmark harness.
The working-tree build used Windows ARM64, MSVC 19.44.35228, and Release configuration in `build\performance`.
The [contributor procedure](../../CONTRIBUTING.md#performance-regression-checks) describes the focused checks and benchmark commands.
The [architecture notes](architecture.md#layout-and-frame-hot-paths) describe the implementation boundaries.

The layout fixture uses eight nested stacks and 64 leaf elements.
It changes the available bounds over 10,000 root measurement and arrangement pairs.
The baseline made 260,000 scratch allocations. The optimized path made zero after the initial pass.
Both paths still measured each leaf three times per pair.
Separate checks cover reentrant calls, measurement exceptions, child growth, geometry, and alignment on both axes.
Scratch storage remains proportional to child count.

The drawing fixture checks every typography field, equivalent font-family names, normalized wrapping, and mutable layout bounds.
It also checks ownership release, long-text exclusion, entry limits, and allocation-free cache hits.
On ARM64, a retained format entry decreased from 256 to 80 bytes.
A retained layout entry decreased from 312 to 136 bytes.
At the existing cache limits, these changes remove 33,792 bytes of live-entry payload per renderer.
This total excludes vector spare capacity, text buffers, and DirectWrite resources.

The selection fixture uses 128 ranges with intervening point terms over a million-row source.
For 20,000 membership queries, source lookups decreased from 2,560,000 to 20,000.
The projection fixture uses 4,096 groups, 12,289 spans, and 1,052,672 projected rows.
Its row benchmark performs 60,000 span resolutions across 20,000 iterations.
Separate final-item identity searches decreased from 1,638,600 source lookups to 200 for 200 queries.
The span index adds eight bytes per span on ARM64.

Initial timing runs overlapped other work on this shared machine.
Those runs do not establish an application speedup.
The deterministic allocation and operation counts are the regression gates.
No result in this pass establishes application frame rate, idle CPU, or total process-memory limits.

After all agent builds stopped, three sequential baseline/optimized pairs measured the text caches.
Each result used the median of seven samples, with 200,000 lookups per sample.
The table shows the median across the three runs, in nanoseconds per lookup.

| Lookup | Cache entries | Baseline | Optimized |
| --- | ---: | ---: | ---: |
| Format | 1 | 21.259 | 7.718 |
| Format | 64 | 177.565 | 161.361 |
| Layout | 1 | 73.810 | 53.026 |
| Layout | 128 | 236.727 | 216.032 |

These microbenchmarks measure cache hits, not text shaping or complete frames.
The final collection run reported 7.224 ms for membership queries and 5.216 ms for projection row access.
No sequential collection timing baseline remained, so these times do not establish a relative speedup.
The local output is `build\performance\sequential-benchmarks.txt`.

The first incremental integration run used a stale collection object and failed the projection lookup-count assertion.
A clean integrated rebuild removed that artifact.
All 14 selected CTest fixtures then passed, including native Stack geometry, focus, identity, and undo checks.
The basic and navigation rendering executables also passed their DirectWrite and software-pixel checks.
The native DLL built from the same final sources.
The local build log is `build\performance\clean-integrated-build.log`.

### Binding validation and measurement scope

The C test compiles the public header as C11 and checks layouts, imports, version discovery, and lifecycle.
The adversarial ABI test covers invalid/stale tokens, kinds, threads, structure sizes, malformed strings, overflow, ownership, callbacks, and batch rejection.
It also covers self-unsubscribe, busy destruction, thrown C++ callbacks, recursive dispatch, persistent callback codes, and repeated arena teardown.
Managed tests cover Unicode, disposal, wrong-thread access, ownership, batch rejection, subscription lifetime, callback exceptions, and list identity.
Rust tests add raw layout checks, RAII ownership, panic containment, callback recursion, and compile-time thread restrictions.

The same external UIA harness runs all five actual applications.
It checks Unicode labels, Invoke, Toggle, native EDIT ValuePattern, Unicode input, submit, key callbacks, retained scrolling, images, and virtual lists.
The keyboard check sends both surrogate units through native `WM_CHAR` and reads text inside foreign callbacks.
It requires one render target, a quiet idle interval, callback closure, a successful process exit, and rejection from a retained provider.
Separate GUI runs require callback exceptions or panics to produce a nonzero application result.
The harness also requests image replacement shortly before closure.

This Windows EDIT provider reports no TextPattern in these runs. ValuePattern and real native editing passed.
The earlier native comparison tests cover parity with an unmodified EDIT provider.
The binding reload/close sequence exercises a race, not a deterministic blocked-codec gate.
The native image-window test separately covers a blocked decoder and nonblocking UI closure.
The CPU and controlled GPU ledgers do not bound WIC transient memory, hardware residency, or total process memory.

All five measured applications use a 600-by-720-DIP window, the dark theme, the same native fonts, and the same controls.
The small workload contains eight preference labels, 60 files, and one generated PNG.
No UIA client runs during memory measurements.
Startup starts before `Process.Start` and ends after a nonzero native paint counter.
The probe disables console creation for every measured GUI process.
Memory follows two seconds of idle time. A separate half-second interval supplies the idle paint delta.
Processes are fresh, but file caches are warm. These results are not reboot-cold startup measurements.

Each throughput process applies 64,000 text mutations before the message loop starts.
The individual path makes 64,000 calls. The batch path makes 1,000 calls with 64 properties each.
The managed measurements include wrapper encoding, pinning, and boundary overhead.
The native static path has no foreign boundary and no separate batch operation.
These numbers measure property submission, not layout, paint throughput, or interactive frame latency.
Raw measurements and exact deployment file lists are in `build\phase4\measurements.json`.

Real IME sessions, Narrator speech, physical mixed-monitor transitions, and actual GPU removal remain manual coverage.
The automated tests do not claim those results.

## Declarative integration fixtures

## Run the integration checks

The native probe reads controls through UI Automation.
It targets the fixture's process ID.
It does not move the pointer or activate another application.

1. Build the native library and probe:

   ```powershell
   cmake --build build\xui-language --config Release --target xui xui_language_probe
   ```

2. Run the edit-loop checks:

   ```powershell
   .\tests\xui-language.ps1
   ```

The script creates an isolated fixture under `build\xui-language-check`.
It starts `dotnet watch` and changes the fixture, not the tracked sample.
It checks text, layout, input preservation, event behavior, invalid-edit recovery, and structural fallback.
It also checks input addition, renaming, deletion, no-op builds, and release references.
Logs and edit durations remain in the fixture directory.
The script stops the processes that it starts.

The compiler and MSBuild checks do not open a native window:

```powershell
dotnet run --project bindings\dotnet\GeneratorTests\GeneratorTests.csproj -c Release
.\bindings\dotnet\GeneratorTests\BuildTests.ps1
```
