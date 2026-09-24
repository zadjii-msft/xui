# DOM web implementation and evidence

The [public DOM contract](../specs/experimental-dom-web.md) describes author-visible behavior and limits.
The [contributor procedures](../../CONTRIBUTING.md#experimental-dom-web) contain reproducible commands.
This implementation uses the shared portable interfaces without changes.

## Source map

- `bindings/dotnet/Experimental/Xui.Web/DomBackend.cs`: Typed state projection, synchronous interop, callbacks, and peer ownership.
- `bindings/dotnet/Experimental/Xui.Web/BrowserDispatcher.cs`: UI-thread identity, queued dispatch, and explicit callback errors.
- `bindings/dotnet/Experimental/Xui.Web/BrowserLifetime.cs`: Terminal page cleanup and interop reference ownership.
- `bindings/dotnet/Experimental/Xui.Web/wwwroot/xui-dom.js`: DOM nodes, labels, validation, ordered callbacks, text revisions, and listeners.
- `bindings/dotnet/Experimental/Xui.Web/wwwroot/xui-dom.css`: Browser flex and scroll layout.
- `bindings/dotnet/Experimental/WebDemo/Program.cs`: Wasm startup and the generated shared component.
- `bindings/dotnet/Experimental/WebDemo/BrowserTestDriver.cs`: Debug-only state and lifetime probes.
- `bindings/dotnet/Experimental/Xui.Web.Tests/Program.cs`: Bounded dispatcher acceptance and error checks.
- `bindings/dotnet/Experimental/WebDemo.Tests`: Playwright checks against real DOM nodes and local .NET WebAssembly.
- `bindings/dotnet/Experimental/WebDemo.Tests/playwright.release.config.js`: Published-site acceptance and separate terminal-navigation/back-forward-cache browser projects.
- `bindings/dotnet/Experimental/WebDemo.Tests/static-server.js`: Loopback-only test host for unchanged Release publication, with Wasm MIME and optional subdirectory mounting.
- `bindings/dotnet/Experimental/WebDemo.Tests/release.spec.js`: Release callbacks, input retention, explicit errors, accessible keyboard input, and real navigation without the Debug bridge.
- `bindings/dotnet/Experimental/WebOrderDemo`: Thin Wasm host linking the shared order-builder UI and C# model, without a test bridge.
- `bindings/dotnet/Experimental/WebDemo.Tests/browser-config.js`: Shared Debug/published runner settings for greeting and order hosts.
- `bindings/dotnet/Experimental/WebDemo.Tests/order-fixtures.js`: Direct shared JSON fixture loading and native DOM action/expectation translation.
- `bindings/dotnet/Experimental/WebDemo.Tests/order.spec.js`: Shared order scenarios, local/offline C#, three retained native inputs, errors, keyboard, and narrow layout.
- `bindings/dotnet/Experimental/WebDemo.Tests/order.navigation.spec.js`: Published full-order terminal navigation and actual bfcache retention.
- `bindings/dotnet/Experimental/WebDemo.Tests/navigation.js`: Shared lifecycle observation used by both published application suites.
- `bindings/dotnet/Experimental/WebGalleryDemo`: One thin host selecting the three shared gallery applications by explicit query.
- `bindings/dotnet/Experimental/WebDemo.Tests/gallery.spec.js`: The shared gallery corpus against generated C# and native controls, including explicit unknown-app errors.
- `bindings/dotnet/Experimental/WebDemo.Tests/capture.spec.js`: Real published-app PNG capture after deterministic offline C# interactions, with source and image hashes.
- `bindings/dotnet/Experimental/WebDemo/BrowserMutationFixture.cs`: Debug-only managed keyed failure and replacement probes.
- `bindings/dotnet/Experimental/WebDemo.Tests/generated-mutation.spec.js`: Shared authored mutation corpus and retained composing-editor checks.
- `bindings/dotnet/Experimental/WebDemo.Tests/mutation.spec.js`: Real C# keyed failure boundaries, retained native identity, detach, and recovery.
- `bindings/dotnet/Experimental/WebDemo.Tests/dynamic-tasks.spec.js`: Actual dynamic gallery app retention, synthetic composition, and natural row geometry.
- `bindings/dotnet/Experimental/WebDemo.Tests/dynamic-tasks.navigation.spec.js`: Published dynamic row disposal and actual bfcache preservation.
- `bindings/dotnet/Experimental/Xui.Web/BrowserPlatformServices.cs` and `wwwroot/xui-services.js`: Shared-policy managed capability adapter and native browser clipboard/URI boundary.
- `bindings/dotnet/Experimental/WebDemo.Tests/services.spec.js`: Real Wasm service calls with isolated clipboard doubles, real no-activation denial, and controlled loopback popups.
- `bindings/dotnet/Experimental/Xui.Web.Tests/BrowserServiceChecks.cs`: Bounded service response, interop failure, and cancellation regressions without a browser.
- `bindings/dotnet/Experimental/Xui.Web/IndexedDbApplicationStorage.cs` and `wwwroot/xui-storage.js`: Bounded byte documents, transaction completion/abort, and connection ownership.
- `bindings/dotnet/Experimental/WebDemo.Tests/storage.spec.js`: Real isolated IndexedDB transactions through Wasm and native API boundary cases.
- `bindings/dotnet/Experimental/WebDemo.Tests/settings.spec.js`: Shared value-control corpus, native accessibility state, progress ratios, keyboard, and geometry.
- `bindings/dotnet/Experimental/Xui.Web.Tests/BrowserStorageChecks.cs`: Missing/empty protocol checks, failed responses, cancellation, and disposal guards.
- `bindings/dotnet/Experimental/WebDemo.Tests/native-input.spec.js`: Public C# Host focus/selection calls against actual DOM focus and UTF-16 native input state.
- `bindings/dotnet/Experimental/Xui.Web/DomLayout.cs`: Native measurement orchestration using the actual shared C# axis, stack, and two-pass Grid solvers.
- `bindings/dotnet/Experimental/Xui.Web/wwwroot/xui-layout.js`: Sanitized native measurement, coalesced invalidation, frame application, and observer ownership.
- `bindings/dotnet/Experimental/WebDemo.Tests/layout.spec.js`: Real authored axis/Grid geometry, intrinsic invalidation, measurement safety, and bounded failures.
- `bindings/dotnet/Experimental/WebDemo/BrowserLayoutMathFixture.cs` and `WebDemo.Tests/layout-math.spec.js`: Debug-only execution of the literal shared numeric fixtures through actual C# Wasm.
- `bindings/dotnet/Experimental/Xui.Web/BrowserErrorReporter.cs`: Thread-safe, nonthrowing fatal reporting with console/DOM fallback and retained reporting failures.
- `bindings/dotnet/Experimental/WebDemo.Tests/profile-fixtures.js`, `profile.spec.js`, and `profile.navigation.spec.js`: Shared Profile scenarios with isolated real IndexedDB, explicit-operation barriers, and actual navigation lifecycle.
- `bindings/dotnet/Experimental/Xui.Web.Tests/BrowserErrorChecks.cs`: Reporter UI/worker delivery, failed sinks, and disposed-reporter regressions.
- `bindings/dotnet/Experimental/Xui.Web/DomVirtualViewport.cs` and `wwwroot/xui-virtual.js`: Exact-Int64 lease transport, native intent scroller, committed layer, and viewport ownership.
- `bindings/dotnet/Experimental/WebDemo.Tests/virtual-list.spec.js`: Actual 10,000-row native geometry, sparse bounds, retained pins, IME/source gating, logical metadata, and frame exposure audit.
- `bindings/dotnet/Experimental/WebDemo.Tests/virtual-lease.spec.js` and `viewport-probe.spec.js`: Native epoch/source protocol and actual C# Host lease boundary checks.
- `bindings/dotnet/Experimental/WebDemo/BrowserVirtualListFixture.cs`: Debug-only shared-app attachment and local performance observations.
- `bindings/dotnet/Experimental/WebDemo.Tests/virtual-stress.spec.js` and `virtual-performance.spec.js`: Real 100-attachment resource checks and local native commit/prune timings.
- `bindings/dotnet/Experimental/Xui.Web/wwwroot/xui-presentation.js`: Typed typography and mount-scoped theme projection with owned media-query listeners.
- `bindings/dotnet/Experimental/WebDemo.Tests/forms.spec.js`, `presentation.spec.js`, and `feature-lifetime.spec.js`: Native Forms corpus, secret retirement boundaries, actual font/palette geometry, and override cleanup.
- `bindings/dotnet/Experimental/WebDemo/BrowserFeatureFixture.cs`: Debug-only public C# lifetime/setter probes; never a Release application bridge.
- `bindings/dotnet/Experimental/Xui.Web/wwwroot/xui-pages.js`: Typed retained panels, native tab/navigation headers, and precommit focus/composition policy.
- `bindings/dotnet/Experimental/Xui.Web/DomHostViewport.cs`: Owned mount-allocation observation through the shared Host coalescing contract.
- `bindings/dotnet/Experimental/Xui.Web/wwwroot/xui-text-layout.js`: Native label clipping/ellipsis and measured line caps without text rewriting.
- `bindings/dotnet/Experimental/WebGalleryDemo/BrowserStudioHost.cs` and `wwwroot/studio-host.js`: Mount-scoped application Back policy, not a fabricated browser history router.
- `bindings/dotnet/Experimental/WebDemo.Tests/pages.spec.js`, `studio.spec.js`, `studio.navigation.spec.js`, and `studio-catalog-geometry.spec.js`: Actual retained Studio workflows, native roles, responsive layout, lifecycle, and catalog content measurements.
- `bindings/dotnet/Experimental/Xui.Web/DomImage.cs` and `wwwroot/xui-image.js`: Bounded packaged bytes, native image codec/resampling, source/output ownership, and canceled-result retirement.
- `bindings/dotnet/Experimental/WebDemo.Tests/image.spec.js`: Actual codec pixels, original-aspect containment, hidden readiness, quality-independent layout, and bounded uncancelable decode admission.
- `bindings/dotnet/Experimental/Xui.Web/wwwroot/xui-reveal.js`: Native retained Reveal clock, input/accessibility gate, motion policy, and lifetime cleanup.
- `bindings/dotnet/Experimental/WebDemo.Tests/reveal.spec.js` and `reveal-lifetime.spec.js`: Actual native motion geometry, retained input, policy overrides, public C# presentation queries, and opt-in Studio drawer behavior.

## Lifetime details

`DomBackend.Dispose` unmounts the surface but does not dispose its peers.
The last peer releases the JavaScript surface reference.
The module remains owned by the application until host disposal finishes.
Each peer releases its `DotNetObjectReference` after listener cleanup.

The JavaScript event queue serializes callbacks across the whole surface.
Text revisions suppress queued native edits after an explicit programmatic edit.
The .NET callback checks the same revision before delivery.
Native changes record their value without a DOM writeback.

The dispatcher invokes each accepted host action even if a context delivers it on the wrong thread.
This lets `Host.DispatchAsync` complete its task with the host's thread error.
A dispatcher guard that skips the action can leave that task incomplete forever.
The managed tests cover this failure path with a five-second bound.

A terminal `pagehide` uses synchronous Wasm interop to dispose the sample.
Callback errors remain visible through the retained error element.
A persisted `pagehide` preserves the host and its listeners for a back-forward cache restore.
Explicit disposal removes the page listener.

## Evidence, September 19, 2026

The implementation milestone is commit `54eebcdc2cc1ba5f0793e8958caad95e53db4c65`.
The machine uses Windows ARM64, .NET SDK 10.0.401, and Node.js 25.6.1.
The projects reference the .NET 10.0.12 browser packages.

Observed build results:

- `dotnet build ...\WebDemo.csproj -c Debug`: success, zero warnings and errors.
- `dotnet build ...\WebDemo.csproj -c Release`: success, zero warnings and errors.
- `dotnet publish ...\WebDemo.csproj -c Release --no-restore`: success without an additional browser workload.
- `dotnet run --project ...\Xui.Web.Tests.csproj -c Release`: all 14 assertions passed.
- `python -m unittest discover -s tests -p test_docs_site.py`: all 24 tests passed.
- `python tools\docs_site.py prepare`: prepared all 55 handbook pages, including the DOM contract.

The publish command reported the optional `wasm-tools` optimization recommendation.
It did not require that workload for the interpreted Wasm application.

Playwright 1.63.0 ran against isolated, headless Microsoft Edge with `--disable-gpu` and one worker.
All 12 browser tests reported individual passes.
The suite includes five adapter tests and seven actual Wasm application tests.
The development server answered HTTP 200 on `127.0.0.1:5187`.

The browser checks cover the exact generated demo, including offline C# callbacks after startup.
They cover input binding, submit, reset, enabled state, ancestor disabling, visibility, keyboard activation, and accessible names.
They also cover stable input and tree identity, selection, caret position, composition-aware submit, layout, and literal text safety.
Lifetime checks cover detach, remount, stale nodes, final disposal, terminal pagehide, and persisted pagehide.
Boundary checks cover malformed state, child ownership, callback order, delayed text echoes, and visible callback errors.
Two error cases expect a specific console error and an alert.
All other console and page errors fail the tests.

### Browser shutdown limitation

The final Playwright `browser.close()` did not finish on this machine.
The test command therefore did not produce a successful process exit, despite all 12 individual passes.
This is not recorded as a fully passing runner.

An independent probe reproduced the shutdown timeout with only `about:blank` and the same Edge flags.
The probe printed `BLANK PAGE OPEN`, `BLANK CONTEXT CLOSED`, then `BROWSER CLOSE TIMEOUT 15s`.
It loaded no XUI code, Wasm, or pagehide handler.
The probe browser PID was 87476.
The owned process tree was stopped after the timeout.

Earlier downloaded Chromium runs also stalled during browser startup or shutdown.
An earlier Chromium run passed the first eight checks before a new page stalled.
Native Edge completed every application assertion.
The test configuration has no unconditional retries or ignored teardown errors.
The recorded test and probe process trees were stopped.
No development server remains intentionally active.

The browser shutdown limitation needs a separate machine or browser-tooling check.
It does not establish production-page shutdown failure.
Synthetic composition checks do not replace manual IME or screen-reader acceptance.

## Clean browser acceptance, September 23, 2026

The integration baseline is `c346ec5`, which merges local main `8f9d7d9`.
The acceptance changes were uncommitted when these results were recorded.
This is a Windows x64 run, not a reproduction or resolution of the historical ARM64 shutdown issue.
The operating system reports `Microsoft Windows 10.0.26200`, architecture `X64`.
Tools: .NET SDK `10.0.301`, Node.js `v21.6.1`, npm `10.4.0`, Playwright `1.63.0`.
The installed browser is Microsoft Edge `153.0.4234.48`, launched headlessly through channel `msedge`.
The browser package references remain `10.0.12`.
No package or browser download version was changed.

### Changes and reproduced defect

The normal browser configuration no longer hardcodes `--disable-gpu`.
The existing 12 tests completed successfully without it, including final runner teardown.
No blank-page probe was necessary on this machine because startup and shutdown did not stall.
Both test configurations use one worker, no retries, bounded test/startup waits, and a five-minute whole-run timeout.
They reject an already occupied port rather than reusing another session's server.

The new Release runner serves only the actual `WebDemo/bin/Release/net10.0/publish/wwwroot` files.
It does not rewrite the HTML, replace generated application code, or supply a JavaScript version of the authored handlers.
Its small navigation-target page is a test fixture outside the application.
The test host serves Wasm as `application/wasm`.
With `XUI_WEB_BASE_PATH=/nested/xui/`, application files outside that prefix return 404.

Before the fix, the non-root asset test exited 1: the increment button never appeared and two asset requests returned 404.
The sample's `<base href="/">` sent its stylesheet and bootstrap requests to the site root.
Changing that base to `./` preserves root hosting and resolves assets beneath the current application directory.
The same unmodified Release publication subsequently passed the complete suite under both `/` and `/nested/xui/`.
The shared `Greeting.xui`, generator, portable runtime, DOM adapter, and application C# behavior were not changed.

### Commands and outcomes

All commands below ran from the repository root unless the command itself selects another directory.
These record the actual acceptance invocation; reusable contributor procedures belong in [CONTRIBUTING](../../CONTRIBUTING.md#experimental-dom-web).

```powershell
dotnet --version
node --version
npm --version
dotnet build bindings\dotnet\Experimental\WebDemo\WebDemo.csproj -c Debug
dotnet build bindings\dotnet\Experimental\WebDemo\WebDemo.csproj -c Release
dotnet run --project bindings\dotnet\Experimental\Xui.Web.Tests\Xui.Web.Tests.csproj -c Release
dotnet publish bindings\dotnet\Experimental\WebDemo\WebDemo.csproj -c Release --no-restore
```

Both final builds exited 0 with zero warnings and errors.
The bounded managed suite exited 0 with all 14 dispatcher assertions passed.
Publication exited 0 and reported only the optional `wasm-tools` optimization recommendation.
No additional workload was installed.

The first browser invocation failed because `playwright` was missing, before starting a server or browser.
Only then were the scoped locked dependencies installed:

```powershell
$env:XUI_BROWSER_CHANNEL = "msedge"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests test
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests ci
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests ls @playwright/test --depth=0
```

The initial test command exited 1.
`npm ci` exited 0, installed three packages, and reported zero audit vulnerabilities.
The dependency listing confirmed `@playwright/test@1.63.0`.
The existing suite then passed all 12 cases and exited 0 in 42.8 seconds, without the GPU workaround.
After advancing to the merged integration baseline, adding Release coverage, and fixing the HTML base, the final browser commands were:

```powershell
$env:XUI_BROWSER_CHANNEL = "msedge"
$env:XUI_WEB_BASE_PATH = "/nested/xui/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:release
$env:XUI_WEB_BASE_PATH = "/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:release
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests test
```

| Run | Result including teardown | Runner duration |
| --- | --- | --- |
| Published Release, `/nested/xui/` | 6 passed, exit 0 | 16.9 seconds |
| Published Release, `/` | 6 passed, exit 0 | 15.8 seconds |
| Debug adapter and actual Wasm application | 12 passed, exit 0 | 26.8 seconds |

The Release checks establish:

- All application requests stay beneath the configured deployment URL; every loaded Wasm response has the correct MIME type.
- The generated tree has 11 retained nodes and no canvas; `?test` does not expose a Release test bridge.
- Increment, change, submit, and reset execute after network disconnection, with no new requests.
- Unrelated C# changes preserve the exact input and tree, focus, selection, and zero programmatic value writes; synthetic composition suppresses Enter submission.
- A malformed native text event reaches the real compiled C# validation boundary, produces one expected console error and visible alert, and permits subsequent valid callbacks.
- Accessible labels, native Tab/Space/Enter behavior, and bounded scrolling work at a 240 by 200 CSS-pixel viewport.
- Real terminal navigation observes `pagehide.persisted=false` and zero application nodes after synchronous disposal; Back loads fresh C# state.
- Real back-forward-cache navigation observes `pagehide.persisted=true` with 11 nodes and `pageshow.persisted=true` on return. The same DOM objects, input value and selection, and C# counter survive; subsequent C# callbacks work.

The bfcache project removes only Playwright's default `--disable-back-forward-cache` launch argument.
It does not synthesize lifecycle events to claim a cache restore.
The terminal-navigation project retains that default so it exercises deterministic non-cached disposal.
The bfcache assertion requires an actual restore and attaches lifecycle observations plus browser not-restored reasons for diagnosis; it does not silently skip a miss.
The older Debug synthetic pagehide check remains a separate callback-boundary test.

After the final runs, the process checks were:

```powershell
Get-NetTCPConnection -State Listen | Where-Object LocalPort -EQ 5187
Get-CimInstance Win32_Process -Filter "Name = 'msedge.exe'" |
    Where-Object CommandLine -Match "playwright" |
    Select-Object ProcessId, CommandLine
```

Both returned no matching processes or listeners.
The runner owned and stopped its development/static servers and isolated browser processes.
No unrelated processes were stopped; no server remains intentionally running.
`git diff --check` exited 0.
`python -m unittest discover -s tests -p test_docs_site.py` exited 0 with all 24 documentation tests passed.

### Acceptance matrix and remaining gates

The provisional automated target is Microsoft Edge on Windows x64.
The Debug and published root/non-root checks cover that target, including actual bfcache behavior.
The historical Windows ARM64 result is still a tooling limitation, not a newly verified target.
Chrome, Playwright's bundled Chromium, Firefox, Safari, Android browsers, and iOS browsers were not run and are not declared supported by this evidence.

Physical IME candidate selection and composition, touch keyboards, screen-reader output (including Narrator), and real browser zoom were not exercised.
The automated key and composition events and native accessible-name assertions cannot establish those manual outcomes.
No physical keyboard or screen-reader version is recorded because no such acceptance session occurred.
These remain Stage 1B manual gates; the clean automated runner does not declare the entire experimental platform production-ready.

## Shared order-builder acceptance, September 23, 2026

The second application is `WebOrderDemo`, a thin .NET 10 Wasm bootstrap.
It links the exact shared `OrderBuilder.xui` and `OrderModel.cs`; no UI is copied into HTML, Razor, or JavaScript.
The HTML contains only the mount, error surface, and bootstrap assets.
There is no application test bridge in either configuration.
The portable runtime, generator, DOM adapter, and shared source were not changed by this web work.
The fixed catalog does not introduce dynamic or keyed collections.

The worktree baseline remains `c346ec5`, with the earlier browser acceptance changes preserved.
The shared owner supplied snapshot 1 before the order builds.
The three consumed source files had these SHA-256 hashes:

| Shared file | SHA-256 |
| --- | --- |
| `OrderBuilder.xui` | `4A5BEC44E8331456D0B402C0AEE1AB298469980D5F2F80504DCFA25658FBA8A7` |
| `OrderModel.cs` | `D18F9C5CB246E8097C89AC9259A6B32E742C15B341E85859EFC825B6D5975ADC` |
| `OrderScenarios.json` | `91C4C3B886245538229E37BB02E420B895FDA10E9002C58BF24C006F11E80345` |

The browser runner reads that JSON directly from `SharedDemo`.
Its seven scenarios contain 161 literal property checks, including exact input values, whitespace, Unicode, multiline review text, visibility, and enabled state.
It translates `change`, `click`, and `submit` into native DOM interactions.
A click on an already disabled native button invokes its native `click()` method, which the browser ignores; it is not skipped by the runner or forced past the disabled state.
No expected totals are calculated in JavaScript.
The `full-order-review` fixture is also the offline and rich-state navigation checkpoint.

`browser-config.js` shares the existing one-worker, bounded, no-retry runner settings.
The static host now allowlists `WebDemo` and `WebOrderDemo`; it does not accept arbitrary project paths.
The greeting commands remain unchanged.
Order commands use `test:order` and `test:order:release` in the same npm package and use the same scoped locked dependencies.
No new npm dependency, package manifest, or additional browser installation was needed.

### Verified host error layout correction

The first Debug order run passed 11 of 12 cases.
The deliberate malformed-native-text test revealed that the initially copied fixed-bottom error overlay intercepted pointer clicks on Reset.
The new host now lays out the application and alert vertically.
The alert has a 40-viewport-percent maximum height, scrolling, wrapping, and keyboard focus; the application occupies the remaining height.
The regression verifies separate nonoverlapping bounds and actual pointer-based Reset recovery while the error stays visible.
The greeting host's earlier overlay was not changed by this order task.

One intermediate test assumed that Reset could repair a synthetic NUL value rejected before it entered the model.
Because the retained model was still its initial value, same-value Reset correctly did not rewrite the arbitrary external DOM value.
The test was corrected to enter valid native text after rejection before asserting shared Reset behavior.
No model, runtime, or adapter change was made for that test assumption.
The error remains explicit: exactly one expected console error and a visible alert are required; unexpected errors still fail the run.

### Commands and observed results

The environment and installed Edge version are the same as the clean browser acceptance above.
The actual commands ran from the repository root:

```powershell
dotnet build bindings\dotnet\Experimental\WebOrderDemo\WebOrderDemo.csproj -c Debug
dotnet build bindings\dotnet\Experimental\WebOrderDemo\WebOrderDemo.csproj -c Release
dotnet publish bindings\dotnet\Experimental\WebOrderDemo\WebOrderDemo.csproj -c Release --no-restore
dotnet run --project bindings\dotnet\Experimental\Xui.Web.Tests\Xui.Web.Tests.csproj -c Release
$env:XUI_BROWSER_CHANNEL = "msedge"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:order
$env:XUI_WEB_BASE_PATH = "/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:order:release
$env:XUI_WEB_BASE_PATH = "/nested/order/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:order:release
```

Debug and Release builds exited 0 with zero warnings and errors.
Publication exited 0 with only the optional `wasm-tools` recommendation.
The existing managed dispatcher suite again passed all 14 assertions.

| Final complete order run | Outcome including teardown | Duration |
| --- | --- | --- |
| Debug | 12 passed, exit 0 | 36.1 seconds |
| Published Release, `/` | 14 passed, exit 0 | 38.4 seconds |
| Published Release, `/nested/order/` | 14 passed, exit 0 | 31.2 seconds |

The common cases cover all seven shared scenarios plus actual local Wasm assets/MIME, no test bridge, the full shared review scenario after network disconnection with no further requests, all three input identities and selections, zero programmatic input writes during unrelated updates, synthetic composition-aware submit, explicit callback errors and recovery, native keyboard order, unique accessible control names, and 320 by 240 CSS-pixel scrolling.
Native tab order follows the authored tree: customer name, email, the enabled catalog buttons, discount code, and Reset when review/edit are disabled.

Published-only cases perform real navigation.
Terminal navigation observes nonpersisted pagehide with no remaining application nodes; Back reconstructs the initial empty controls and the full shared scenario works again.
The bfcache project removes only Playwright's default cache-disable argument.
It requires persisted pagehide and pageshow, identical retained DOM objects, all three input values and selections, quantities, totals, enabled states, and visible review.
After restoration, native Edit followed by Email Enter reproduces the same literal shared review checkpoint through C#.
There are no synthetic lifecycle events, cache-miss skips, GPU-disable flags, or blanket retries in these acceptance cases.

Greeting regressions after the shared configuration extraction also passed: Debug 12/12 in 18.7 seconds, published root 6/6 in 11.7 seconds, and published `/nested/xui/` 6/6 in 11.5 seconds.
The latter includes both real-navigation cases after extracting the shared navigation observer.
All runner processes exited 0.
The same post-run checks as above found zero port-5187 listeners and zero Playwright Edge processes; no unrelated processes were stopped.

Physical IME candidate handling, touch keyboards, screen-reader output, browser zoom, and other browser/platform combinations remain unverified.
Synthetic composition and native accessibility assertions do not replace those manual gates.
The review remains local sample state: no purchase, payment, order transmission, or reload persistence is implemented.

## Mutable DOM, gallery, and capture checkpoint, September 23, 2026

This checkpoint uses the parent-synchronized portable mutation contract and generated shared mutation fixtures.
No shared runtime or generator edits were made in the DOM worktree.
`DomPeer` implements `IMutableElementPeer`; the JavaScript surface tracks its own native child order instead of reading the already-committed managed tree.
Move preflight accepts future nonnegative indices, checks current parent ownership, and rejects an unsupported active composition before any native mutation.
Insert/remove and move operations validate their actual current bounds.
Events require connection through the mounted root, so newly staged and removed subtrees cannot deliver callbacks.
Peer destruction remains host-owned and removes listeners individually.

State-preserving `moveBefore` keeps a moved editor alive and focused.
The fallback uses native insertion and restores focus/selection without a value assignment, only outside composition.
Composition rejection is explicit and has no automatic retry.
Tests cover 30 native moves, 12 authored reversals, future-index preflight, removals with queued callbacks, duplicate keys, failed factories, type replacement under the same key, and native move failure followed by detach/reattach.
The generated `MutationBoard`/`MutationRow`/`MutationBanner` fixture and shared `MutationScenarios.json` run on `?mutation=true` in Debug.
That fixture and the separate managed failure probe are excluded from Release.
The Debug test bridge remains the greeting test facility, not application UI.

The lifecycle stress performs 25 actual C# attachment cycles.
It instruments native listener registration/removal, proves retired input/button listener counts are zero, sends events to old nodes, and verifies only the current attachment reacts.
The order geometry test uses 16, 24, and 32 CSS-pixel body fonts at a 320 by 360 viewport.
It checks caption/editor separation, bounds, readable native editor height, retained labels, and subsequent real C# actions.
This is text scaling, not a claim of physical browser-zoom or OS IME acceptance.

`WebGalleryDemo` links the three exact shared applications and their model/helper files.
`?app=task-board`, `?app=expense-ledger`, and `?app=session-planner` select the component in C#.
An unknown app remains a visible startup failure.
All shared gallery scenarios execute with the context offline after startup.
Greeting, order, and gallery literal step execution now share one DOM driver.
No expected application data is calculated in JavaScript.

### Real screenshot artifacts

The capture harness runs only published Release output, drives the native controls through local C# callbacks, waits for fonts, and captures actual browser PNG pixels.
Each PNG has adjacent JSON containing its application/project/source, source and PNG SHA-256, browser version/channel, viewport, URL, native input count, retained node count, and offline-callback flag.
It rejects unexpected browser errors, a visible error alert, a canvas, a test bridge, or clipped scroll content at the capture viewport.
No screenshot is a hand-authored mockup or replacement HTML tree.

The default output is the ignored `build/browser-captures` directory.
`XUI_CAPTURE_DIR` can select another artifact directory.
The checked-in capture registry deliberately allowlists apps rather than accepting arbitrary project paths.
The viewport is 1100 by 1300 at device scale 1, light color scheme, and reduced motion.
All five PNGs were opened and visually inspected:

- `greeting-web.png`: Ada Lovelace greeting and count 3.
- `order-web.png`: Shared full-order review checkpoint.
- `task-board-web.png`: Shared task-board screenshot seed.
- `expense-ledger-web.png`: Shared expense-ledger screenshot seed.
- `session-planner-web.png`: Shared session-planner screenshot seed.

The latter four are distinct application examples; the greeting remains an additional smoke sample.
The capture commands were:

```powershell
$env:XUI_BROWSER_CHANNEL = "msedge"
foreach ($app in @("greeting", "order", "task-board", "expense-ledger", "session-planner")) {
    $env:XUI_CAPTURE_APP = $app
    npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run capture
    if ($LASTEXITCODE -ne 0) { throw "Capture failed: $app" }
}
```

The browser and tool versions remain Edge `153.0.4234.48`, Playwright `1.63.0`, SDK `10.0.301`, Node `21.6.1`, and npm `10.4.0` on Windows x64.
Each capture command completed with a clean runner exit.
The browser PNGs are local evidence artifacts, not committed generated binaries or a hosted gallery.

### Browser and regression results

The scoped builds used `dotnet build ...\WebDemo.csproj -c Debug`, `dotnet build ...\WebOrderDemo.csproj -c Debug`, and `dotnet build ...\WebGalleryDemo.csproj -c Debug`.
Each application also used `dotnet publish ... -c Release`.
All completed with zero build warnings/errors; publication retained the optional `wasm-tools` recommendation.
The bounded dispatcher suite again passed all 14 assertions.

| Complete suite | Observed result including teardown |
| --- | --- |
| Debug greeting, adapter, lifetime stress, generated and managed mutation | 21 passed, 49.5 seconds |
| Debug order with scaled-font geometry | 13 passed, 43.0 seconds |
| Published greeting | 6 passed, 23.7 seconds |
| Published order with scaled-font geometry and real navigation | 15 passed, 53.3 seconds |
| Debug three-app gallery | 11 passed, 52.6 seconds |
| Published gallery at root | 11 passed, 36.8 seconds |
| Published gallery at `/nested/gallery/` | 11 passed, 28.4 seconds |
| Final-core published greeting at `/nested/xui/` | 6 passed, 18.7 seconds |
| Final-core published order at `/nested/order/` | 15 passed, 45.5 seconds |

Commands remain `npm ... test`, `test:order`, `test:release`, and `test:order:release`.
The gallery adds `test:gallery` and `test:gallery:release`; its published suite uses the same `XUI_WEB_BASE_PATH` convention and static host.
No dependencies or browser packages were installed during this checkpoint.
Port 5187 and Playwright-owned Edge processes were absent after completed runs.
The parent's separate preview on port 5190 was not touched.

Installed-browser discovery also found stock Firefox `156.0`.
A bounded independent `about:blank` Playwright Firefox launch against the installed executable failed before opening a page because it does not provide Playwright's Firefox automation protocol.
The launched PID 45320 exited and its temporary profile was cleaned up.
No Firefox application acceptance is claimed; Chrome was absent at the standard machine installation paths, and WebKit was not run.
The automated support evidence therefore remains Edge on Windows x64, not a broadened browser matrix.
Physical IME, screen readers, browser zoom, and touch-keyboard acceptance remain explicit manual gaps.

## Dynamic application and browser services, September 23, 2026

This follow-up is incremental to the mutable-DOM/gallery checkpoint above.
It adds the exact shared `DynamicTaskBoard.xui`/`TaskRow.xui`, model, and codec to `WebGalleryDemo`.
`?app=dynamic-tasks` constructs the component through `DynamicTaskBoard.Create(host)`.
The fixed task-board application and its screenshots are unchanged.
The shared dynamic corpus supplies 52 literal property expectations.
The common DOM scenario driver accepts a missing node only for a visibility-only `false` expectation; missing text/enabled targets still fail.
No browser-specific task model or JavaScript total/state implementation was added.

The additional native test edits a retained row, inserts and removes another row, reverses 12 times during synthetic composition, verifies selection direction/caret and zero native value writes, then filters the row out and back in.
A surviving key keeps its exact input; a removed and recreated key gets a new node but restores its shared model text.
Natural row captions/editors remain separated and scroll-accessible at 16/24 CSS-pixel body text in a 320 by 640 viewport.
Published tests also prove real terminal disposal and actual bfcache restoration of edited dynamic state, input objects, selections, row order, and subsequent C# callbacks.
`dynamic-tasks-web.png` and adjacent metadata were captured from published output and visually inspected.
The extra capture uses `XUI_CAPTURE_APP=dynamic-tasks`; it does not replace `task-board-web.png`.

`BrowserPlatformServices` uses the parent-supplied `PlatformServicePolicy` and strict `OperationResult<T>` contract.
Its separate local ES module maps actual Clipboard API and window-opening outcomes.
It reports capability availability conservatively, validates again at the JavaScript boundary, and never returns an empty value to disguise failure.
A genuinely successful empty clipboard string remains `Completed("")`.
External token cancellation cancels the task; native `AbortError` becomes `OperationStatus.Cancelled`.
The caller owns the imported module.
File selection, file save, and application storage remain `Unsupported` in this slice.

### Clipboard and URI acceptance boundaries

**The user's host clipboard was neither read nor written.**
The successful clipboard tests install an isolated in-page Clipboard API double before the Wasm application loads.
They exercise the real managed adapter and interop serialization with Unicode, multiline, and empty strings.
The doubles also produce NotAllowed/Security/Abort/NotSupported errors and an explicit unexpected failure.
Tests require distinct Denied/Cancelled/Unsupported/Failed results without reading `Value` for an unsuccessful result.
Pre-cancelled and 50-millisecond in-flight token tests prove the managed task is canceled.
The latter uses a pending double, not an actual clipboard read.

Real browser no-activation calls were run from a fresh page with an explicit inactive-gesture assertion.
They deny before touching the native clipboard or creating a popup.
The only real URI navigation was to an owned loopback fixture, with its opener detached and its popup explicitly closed.
Popup blocking was additionally exercised through a null-returning window-open double.
All four allowed schemes were tested through a window-open double, so no external mail/telephone handler or remote website was launched.
Both managed and JS policy reject relative/unsafe schemes, HTTP credentials, and invalid text.
Actual clipboard permission prompts, real clipboard round trips, and external protocol-handler behavior remain unverified manual gates.

An initial cancellation assertion propagated a canceled Task directly out of the Debug JS-invokable test method; the JavaScript invocation did not settle.
The fixture now explicitly observes `OperationCanceledException` only when that returned task has `IsCanceled`, then returns a test observation.
This changes only the test bridge; the service API continues to cancel its task.
No cancellation is relabeled as a successful service result.
SDK-only checks also reject missing, malformed, wrong-type, or unknown-status interop results and preserve explicit interop exceptions.

### Commands and results

The environment remains Windows x64, Edge `153.0.4234.48`, Playwright `1.63.0`, .NET SDK `10.0.301`, Node `21.6.1`, and npm `10.4.0`.
No additional dependencies or browsers were installed.
The shared runtime, generator, dynamic source, and policy files were copied from the parent; no independent changes to those files were made here.
The parent's M4 source checkpoint was present during final builds, but Toggle/CheckBox/Progress DOM implementation is a separate slice.

```powershell
dotnet build bindings\dotnet\Experimental\WebDemo\WebDemo.csproj -c Debug
dotnet build bindings\dotnet\Experimental\WebGalleryDemo\WebGalleryDemo.csproj -c Debug
dotnet build bindings\dotnet\Experimental\Xui.Web\Xui.Web.csproj -c Release
dotnet publish bindings\dotnet\Experimental\WebDemo\WebDemo.csproj -c Release
dotnet publish bindings\dotnet\Experimental\WebGalleryDemo\WebGalleryDemo.csproj -c Release
dotnet run --project bindings\dotnet\Experimental\Xui.Web.Tests\Xui.Web.Tests.csproj -c Release
$env:XUI_BROWSER_CHANNEL = "msedge"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests test
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests test -- services.spec.js
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery
$env:XUI_WEB_BASE_PATH = "/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery:release
$env:XUI_WEB_BASE_PATH = "/nested/gallery/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery:release
$env:XUI_WEB_BASE_PATH = "/"
$env:XUI_CAPTURE_APP = "dynamic-tasks"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run capture
```

Builds succeeded with zero warnings/errors; publication reported only the optional Wasm optimization recommendation.
The dispatcher checks passed 14 assertions.
The separate managed service suite covers protocol validation and cancellation with five-second bounds.

| Complete runner | Result including teardown |
| --- | --- |
| Debug greeting/adapter/mutation/stress/services | 26 passed, 39.8 seconds |
| Debug fixed and dynamic gallery | 17 passed, 39.7 seconds |
| Published gallery, root | 19 passed, 41.9 seconds |
| Published gallery, `/nested/gallery/` | 19 passed, 44.8 seconds |
| Dynamic task screenshot | 1 passed, 21.3 seconds |

The focused services suite passed all five cases after the cancellation-observation correction.
All runs use the existing one-worker, no-retry/no-skip configurations and clean runner teardown.
No GPU-disable flag was added.
The independent preview on port 5190 remains untouched.

## Native value controls and IndexedDB checkpoint, September 23, 2026

This checkpoint implements the parent-validated Toggle, CheckBox, and Progress API in the DOM adapter.
Toggle is a native checkbox with switch semantics; CheckBox uses native checked/indeterminate properties.
Native activation delivers final typed values through `IValueControlEvents`.
Programmatic Checked/CheckState/ThreeState updates advance the peer revision without raising authored events.
Queued stale value callbacks are rejected on both sides of interop.
Labels remain associated with their native inputs, and the shared `SettingsShowcase.xui` runs at `?app=settings`.

Progress uses a real native `progress` element.
Its normalized native position preserves the visual ratio for ranges such as -10 through 30, while ARIA exposes authored minimum/maximum/current units.
An atomic range update includes the runtime-clamped current value.
Indeterminate mode removes native/current-value attributes, retains the logical value, and uses a CSS animation gated by effective visibility/enabled state and attachment.
Reduced motion removes that animation.
Unmounting or disposing the peer stops it even if a test retains the old node.

The settings browser runner reads all 75 literal shared property checks.
Additional checks exercise native Space activation, mixed-state accessibility, labels, determinate ratios 0.3125 and 0.31875, retained editor selection, narrow 24-pixel text geometry, disabled indeterminate state, and reduced motion.
An adapter-level case proves silent programmatic changes and stale callbacks, range clamping, hidden progress, and detached animation/listener cleanup.
The first development run exposed a misplaced new-control construction block; it was corrected before the shared cases passed.
No shared runtime or authored-model workaround was required.

### IndexedDB ownership and failure evidence

`IndexedDbApplicationStorage` imports `xui-storage.js` and owns one JavaScript storage object.
The application owns the imported module and chooses its database name.
Names and keys use the shared `StorageKeys.Validate` policy, with the same validation at the JS boundary.
The native database name is `xui-app-` plus that name, with schema version 1 and a `documents` store.
Each operation opens a native connection, runs one transaction, and closes the connection on completion/failure.
Writes clone bytes before asynchronous work.
Responses distinguish absent from present-empty records and do not substitute empty data for malformed or oversized records.
The default 1 MiB limit is enforced on writes and reads.

An external cancellation token calls the JavaScript operation's cancel hook and aborts its unfinished transaction.
Close aborts unfinished operations and makes future calls fail explicitly.
Already-completed transactions retain their actual completion outcome; cancellation cannot roll back a committed write.
A pending browser open request cannot be physically canceled by IndexedDB, so its later connection is closed or upgrade aborted without starting document work.
Schema creation exceptions abort the upgrade and become explicit failures, not unhandled callback errors.
There is no retry, localStorage fallback, database enumeration, or broad database cleanup.
`BrowserPlatformServices` now reports advisory `ApplicationStorage=Available` only when IndexedDB is exposed.
Open/save file remain unsupported.

The actual Wasm adapter is tested against real IndexedDB with unique UUID database names.
Tests cover missing/empty documents, Unicode UTF-8 data, persisted reload, delete/missing-delete, strict keys, a canceled replacement preserving the old record, and a 1024-byte configured bound.
Native transaction tests additionally prove byte snapshotting, close/cancel abort before commit, failed replacement preserving previous bytes, malformed records, and exactly 1,048,576 bytes accepted versus 1,048,577 rejected.
Quota and security failures are injected at the native API boundary rather than exhausting the machine's disk or changing browser privacy settings.
Schema-quota failure is followed by a separate successful operation after the injection is removed.
Every test deletes only its generated database name; blocked cleanup fails the test.
No user database, host clipboard, or unrelated process is read, overwritten, or removed.

### Reproduction and outcomes

The same Windows x64 tool/browser versions apply.
Debug WebDemo/WebGalleryDemo builds and Release publication completed with zero warnings/errors, apart from the existing optional Wasm optimization recommendation.
The commands were:

```powershell
dotnet build bindings\dotnet\Experimental\WebDemo\WebDemo.csproj -c Debug
dotnet build bindings\dotnet\Experimental\WebGalleryDemo\WebGalleryDemo.csproj -c Debug
dotnet publish bindings\dotnet\Experimental\WebDemo\WebDemo.csproj -c Release
dotnet publish bindings\dotnet\Experimental\WebGalleryDemo\WebGalleryDemo.csproj -c Release
dotnet run --project bindings\dotnet\Experimental\Xui.Web.Tests\Xui.Web.Tests.csproj -c Release
$env:XUI_BROWSER_CHANNEL = "msedge"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests test
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests test -- storage.spec.js services.spec.js
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery
$env:XUI_WEB_BASE_PATH = "/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery:release
$env:XUI_WEB_BASE_PATH = "/nested/gallery/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery:release
$env:XUI_WEB_BASE_PATH = "/"
$env:XUI_CAPTURE_APP = "settings"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run capture
```

| Runner | Observed result including teardown |
| --- | --- |
| Full Debug greeting/adapter/mutation/services/storage (before adding schema-failure case) | 32 passed, 1.1 minutes |
| Debug gallery, dynamic tasks, and settings | 22 passed, 1.0 minutes |
| Published gallery root | 24 passed, 46.2 seconds |
| Published gallery `/nested/gallery/` | 24 passed, 57.3 seconds |
| Focused services and final six storage cases | 11 passed, 28.5 seconds |
| Settings screenshot | 1 passed, 5.2 seconds |

Managed regressions pass 14 dispatcher, 13 service, and 15 storage assertions.
Storage tests bound their managed waits to five seconds.
`settings-web.png` and its adjacent metadata were captured from the actual published native-control UI and visually inspected.
The screenshot is an additional settings example, not a replacement for the four application captures.
No browser packages or dependencies were installed.
Physical assistive-technology/IME acceptance and real privacy/quota exhaustion remain unclaimed.
Axis constraints, Grid layout, and Profile Workspace integration remain separate follow-up scopes.

## Public native input API checkpoint, September 23, 2026

The DOM peer now implements the parent's optional `IFocusableElementPeer` and `ITextSelectionPeer` contract.
No Host/runtime changes were made by this adapter slice.
Focus targets native text/choice inputs, buttons, and scroll regions.
Label/progress requests return false without assigning `tabIndex` or stealing another editor's focus.
`HasFocus` reads the actual active element.
Selection is read from `selectionStart`/`selectionEnd` and written with `setSelectionRange`.
The JS boundary applies the exact shared ordered UTF-16 clamp semantics against `input.value`.
It never assigns `input.value`, stores model-side selection, or focuses an input as a side effect of selecting its text.

Four actual Wasm C# Host tests cover input/button/scroll focus, native Toggle/CheckBox input targets, no focus stealing for label/progress, hidden/disabled ancestry, browser-denied focus through an inert mount, and detached/disposed errors.
They verify reattachment uses new peers.
Selection checks deliberately make the DOM value differ from the retained model, then cover reversed/oversized/negative ranges and surrogate-pair start/end/caret boundaries.
Native backward selections are observed through the ordered API.
An unfocused selection change leaves the current button focused.
Instrumentation records zero value writes and zero input events from selection operations.
The temporary C# focus probes live only in the existing Debug test driver and are disposed before returning.

`dotnet build bindings\dotnet\Experimental\WebDemo\WebDemo.csproj -c Debug`, the Release Xui.Web build, and WebDemo Release publication succeeded with zero warnings/errors apart from the optional Wasm optimization recommendation.
The managed suites remained 14 dispatcher, 13 service, and 15 storage assertions.
With `XUI_BROWSER_CHANNEL=msedge`, `npm --prefix bindings\dotnet\Experimental\WebDemo.Tests test -- native-input.spec.js` passed all four cases and exited cleanly in 17.4 seconds.
The environment and browser versions are unchanged from the preceding checkpoint.
Physical IME behavior for an explicit selection change and assistive-technology announcements remain manual acceptance work.

## Shared axis and static Grid layout checkpoint, September 23, 2026

`DomLayout` is a native-measurement adapter around the actual shared C# `LayoutMath` and `GridLayoutMath` implementations.
It does not copy either solver into JavaScript or treat CSS `fr` as equivalent.
An attachment opts into this path only when it contains an explicit axis constraint or Grid.
Ordinary legacy trees continue using their original CSS layout.
Live removal of all constraints from a Grid-free tree restores that path without recreating native controls.

The adapter retains its own native child lists for layout while a keyed mutation is applying.
This is necessary because the managed model already contains the final committed tree during insert/remove/move callbacks.
Frames use the existing native nodes and preserve their document/accessibility/keyboard order.
Constrained native width is measured before wrapped height.
Scroll content carries the shared semantic unbounded flags even with a finite clip or maximum.
Grid consumes the final shared `CellContexts`: all-fixed spans create a definite nested flex budget, while mixed intrinsic spans retain natural child sizing.
The parent distributed the final context-aware math and literal fixtures; no shared math or generator changes were authored in this worktree.

### Measurement and invalidation

Only supported native leaf tags are reconstructed for measurement.
The helper copies resolved typography and box metrics from the actual elements, including nested theme font settings.
It creates no copies of HTML IDs, names, form/label associations, input values, event listeners, or resource URLs.
The offscreen container is inert and `aria-hidden`, and a `finally` removes it.
Input values are never read or copied for measurement, including any future password-type element.
The real input is not reparented, focused, or assigned a value.
The safety regression observes the temporary containers and verifies empty input values, no unsafe associations/resources, removal, and zero extra network requests.

Property changes, track changes, and keyed child mutations invalidate layout directly.
Resize observation covers the allocated mount, while separate ancestor style/class, stylesheet DOM, font-load, and window-resize notifications cover intrinsic changes that fixed allocated boxes cannot reveal.
Invalidations coalesce into one animation-frame callback.
Viewport stabilization has a four-pass bound and throws explicitly if it cannot stabilize.
An automatic observer error is reported, disconnects its listeners, and stops automatic scheduling.
Live opt-out and terminal disposal disconnect every observer and cancel queued frames.
The usual synchronous native-update failure path detaches the host, which is covered by an unstable-viewport fault injection.

### Exact acceptance

The Debug-only math bridge executes the shared fixture inputs in C# Wasm and returns actual solver results.
The browser reads the unchanged literal JSON corpus: 23 axis cases, 10 stack allocations, 10 track cases, and 5 arrangements, including nested slot contexts.
A source-snapshot update arrived during one intermediate full run; the bridge then needed to pass the new semantic-context field.
That failure was fixed by consuming the final contract, not by changing expected results or forking the solver.
An initial interop frame-array argument-shape error was also corrected before successful geometry runs.

The authored gallery routes `?app=axes` and `?app=grid` link the exact shared fixtures.
Native assertions cover:

- Fixed width with natural caption/editor height, legacy inheritance restoration, and a width-only binding preserving an independent height.
- Parent-pressure minimum allocations of 120/60, fixed/capped/flex widths of 60/40/60, and natural unbounded heights of 32/48.
- Width-before-height Grid measurement under a narrow viewport with caption/editor containment.
- Scroll-contained Star rows retaining 40/80 heights and a 40-pixel second-row offset rather than expanding to 50/150.
- A fixed 200-pixel row giving nested flex children 48/144, versus a mixed intrinsic span keeping 32/48.
- Text growth and a new keyed note increasing Grid height and moving the following button even though allocated boxes alone would not reveal the change.
- A nested Grid font of 28 pixels with 1.5 line height triggering remeasurement while preserving native input identity and selection.
- Safe measurement, explicit bounded failures, and observer cleanup across live opt-in/opt-out/disposal.

Final commands used the existing Edge channel and test entry points:

```powershell
dotnet build bindings\dotnet\Experimental\WebDemo\WebDemo.csproj -c Debug
dotnet build bindings\dotnet\Experimental\WebGalleryDemo\WebGalleryDemo.csproj -c Debug
dotnet publish bindings\dotnet\Experimental\WebGalleryDemo\WebGalleryDemo.csproj -c Release
dotnet run --project bindings\dotnet\Experimental\Xui.Web.Tests\Xui.Web.Tests.csproj -c Release
$env:XUI_BROWSER_CHANNEL = "msedge"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests test
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery
$env:XUI_WEB_BASE_PATH = "/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery:release
$env:XUI_WEB_BASE_PATH = "/nested/gallery/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery:release
```

Both Debug builds succeeded with zero warnings/errors.
Release publication succeeded with the optional Wasm optimization recommendation.
The managed dispatcher/service/storage suites passed 14/13/15 assertions.
Full Debug acceptance passed 39 cases in 1.1 minutes; full Debug gallery acceptance passed 27 in 1.4 minutes.
Published root and nested gallery runs each passed 29 cases, in 58.8 and 59.2 seconds, including native layout and actual dynamic-app bfcache.
All runner exits were clean and no retry, GPU workaround, or skipped layout case was added.
The browser/tool versions and manual accessibility/IME limitations remain unchanged.

## Profile Workspace browser consumer, September 23, 2026

The `profile-workspace` gallery route is the first real application consumer of the browser storage adapter.
It links the exact shared ProfileWorkspace/ProfileEditPage/ProfilePreviewPage UI, controller, model, and session-v2 source checkpoint supplied by the parent (the session DTO remains version 1).
The application starts with no storage I/O and adds no automatic session persistence.
Its default application namespace is `profile-workspace`; the explicit `profile-store` query supports another validated `profile-` namespace.
The browser tests generate a fresh UUID namespace for every case, select it before startup, and delete only that exact database afterward.
Native IndexedDB open/read/write/delete instrumentation confirms zero startup calls and never records document contents.

The root component owns the controller.
The gallery's terminal cleanup order is host/controller, storage, reporter, storage module, then DOM module.
Cleanup continues after failures and reports an aggregate.
The imported modules outlive cancellation/disposal of root-owned work.
The bfcache path retains the live app rather than invoking cleanup or serializing incomplete drafts through the persistent profile document codec.
The shared session types are compiled without adding automatic browser recreation or storage behavior.

### Error boundary

`BrowserErrorReporter` implements the shared nonthrowing fatal callback requirement.
It writes a console record before UI dispatch, uses the captured UI thread/context for DOM reporting, and retains the original plus reporting errors through a thread-safe `LastError`.
The fallback does not report success when a sink fails.
After disposal, the reporter avoids the released JS module while preserving console/retained error reporting.
SDK tests cover UI reporting, worker-thread delivery, DOM failure, a rejected dispatcher, console failure, and a late report after disposal.
An intermediate unknown-app regression found that the Wasm console splits multiline exception text into separate events.
The reporter now escapes CR/LF in the console record while retaining the full exception and multiline DOM display.
The existing explicit unknown-gallery startup test passes with its expected error count.

### Actual storage and lifetime acceptance

All 45 literal shared Profile expectations run through real native controls and compiled C# with the page offline after startup.
The browser waits for native busy/status transitions to settle asynchronous storage actions; it does not invoke application commands through a Release test bridge.
The cases cover edit/preview/back, explicit Save/Load/Delete, and Clear editor without deleting storage.
Additional cases establish:

- A saved document survives a real reload, but the editor stays blank and storage remains untouched until Load is clicked.
- Malformed saved bytes produce `Load failed`, retain an incomplete current draft, and leave the app idle and responsive.
- A controlled delayed native-open notification keeps the UI busy with editing/reset disabled; Cancel settles the operation, and a released stale notification cannot overwrite the confirmed document.
- A later explicit Save succeeds after cancellation without racing the canceled request.
- Editing another field preserves a native editor's identity, selection, and zero programmatic value writes; Tab order, labels, and 24-pixel text geometry work at a 320-pixel width.
- Real terminal navigation during pending work disposes the tree before leaving; late native delivery cannot replace stored data, and a fresh page still waits for explicit Load.
- Real bfcache navigation retains an incomplete draft, selected native input, all DOM objects, and the live controller with zero storage I/O; preview works after restoration.

The delayed-notification test wraps an actual IndexedDB open request in the test realm; it does not replace application storage with a fake provider.
The wrapper is released during cleanup and all owned database connections are closed.
No test reads, writes, or deletes a user's normal Profile database.
Cancellation messages retain the shared conservative warning that storage may already have changed; no general rollback guarantee is inferred from a deliberately pre-commit test.

`profile-workspace-web.png` and adjacent metadata were captured from the actual published unsaved-preview scenario and visually inspected.
The capture uses a unique namespace and explicitly requires zero IndexedDB opens.
It is enabled by `XUI_CAPTURE_APP=profile-workspace` through the existing capture command.

### Commands and results

```powershell
dotnet run --project bindings\dotnet\Experimental\Xui.Web.Tests\Xui.Web.Tests.csproj -c Release
dotnet build bindings\dotnet\Experimental\WebGalleryDemo\WebGalleryDemo.csproj -c Debug
dotnet publish bindings\dotnet\Experimental\WebGalleryDemo\WebGalleryDemo.csproj -c Release
$env:XUI_BROWSER_CHANNEL = "msedge"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery
$env:XUI_WEB_BASE_PATH = "/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery:release
$env:XUI_WEB_BASE_PATH = "/nested/gallery/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery:release
$env:XUI_WEB_BASE_PATH = "/"
$env:XUI_CAPTURE_APP = "profile-workspace"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run capture
```

Build/publication succeeded with zero warnings/errors, apart from the optional Wasm optimization recommendation.
Managed tests passed 14 dispatcher, 13 service, 15 storage, and 11 fatal-reporting assertions.
The full Debug gallery passed 34 cases in 1.9 minutes.
Published root and nested gallery runs each passed 38 cases in 1.0 and 1.3 minutes.
The Profile screenshot command passed in 4.2 seconds.
Every runner exited cleanly; no retries, skipped navigation checks, dependency installs, or GPU flags were added.
Tool/browser versions and the remaining physical IME/screen-reader acceptance gaps are unchanged.

## Leased native viewport implementation

The browser implementation uses the shared v3 lease and metadata-v2 contracts.
`DomVirtualViewport` transports epochs and source versions as invariant decimal strings; JavaScript compares and increments BigInts, never lossy Numbers.
Count and geometry retain the shared Int32/finite-float boundaries.
The native helper rejects browser-unrepresentable scroll extents and unwinds its temporary structure.
Its native intent scroller contains only a spacer; the real input rows live in a sibling committed clip layer.
Native scroll intent cannot directly translate that row layer.
Only successful native `TryCommit` changes the committed transform and viewport size.

Begin rejects an already focused/composing content subtree before adopting it.
Callbacks are posted and serialized behind the same surface event queue as native input and interaction events.
They never run inline inside lease methods.
Immutable request snapshots carry exact epoch, requested/committed source versions, geometry, and blocking state.
Reservation holds the selected snapshot through synchronous controller staging; new intent is retained for another epoch.
Native validation checks every visible index against actual row-root rectangles, pitch, opaque key uniqueness, declared source version/count, and logical order before publication.
No automation identifier is parsed to manufacture list metadata.

Native row roots have `role=listitem`, `aria-posinset`, and `aria-setsize`; the committed clip is the list container.
Tab follows realized native source order, including pins.
The shared First/Previous/Next/Last and Enter-next actions provide logical-key navigation.
No full offscreen accessibility realization or continuous 10,000-row Tab promise is made.
An ordinary focused viewport move does not reorder a surviving row or its gap roots; native tests remove `moveBefore` capability for this case and still require zero row moves and value writes.
Composition blocks offset/growth publication.
Source changes also block while an actual row input remains focused.
Blur/end/removal supplies a fresh asynchronous request rather than a polling timer or offset rollback.

The shared application is constructed with `CreateForViewport`, then attached, then connected through `Host.BeginVirtualViewport` and `AttachViewport(lease, host.SetVirtualItemInfo)`.
The detached-bootstrap checkpoint is mandatory for repeated attachment:
capture editing state, detach, call `PrepareForViewportAttachment`, then attach and begin a new lease.
The test explicitly counts an empty row/gap model before every native attachment.
Lease disposal hides/inerts the committed layer, disconnects observers/listeners, invalidates callbacks, and releases interop references.
It does not reveal the remaining sparse model as an ordinary scroll surface.
The shared terminal-cleanup checkpoint also clears app callback/lease references.

### Native measurement and bounded work

Viewport callbacks batch native layout during synchronous staging/pruning.
The actual shared C# layout solver runs immediately before native commit validation, then after the callback if pruning requires updated frames.
This removes the previous repeated whole-tree measurement after every inserted row.
Native leaf measurements are cached only within a single layout pass for one peer/width/mode, never across frames or input changes.
A rapid-scroll/resize frame audit checks actual visible row geometry on animation frames and rejects a committed gap.

The 10,000-item sample uses pitch 128 and overscan 2.
Steady native input bounds derive from actual committed viewport height plus overscan and focused pins.
Insertion-time transient observations bound native rows by both old and requested viewport heights plus overscan/pins, rather than a fixed count that fails on taller windows.
The existing tests cover native wheel movement, distant and adjacent viewports, offline drafts, selection preservation through recycle, source reverse/filter/remove, IME plus viewport growth, and native logical-list metadata.

Evidence files are written under ignored `build/browser-virtualization`:
`native-realization-peak.json`, `virtual-attachment-resources.json`, and `native-viewport-performance.json`.
The performance case times locally from a C# lease request through native commit and controller pruning, using ten warmups and one hundred measured samples.
It retains every sample plus p50/p95/max; the initial recorded-host p95 gate is 50 milliseconds.
Playwright polling/network time is not the measured interval, and failed outliers are not rerun away.
Performance observers unsubscribe in `finally` and are not retained by the 100-attachment fixture.

### Interim recorded evidence and remaining gate

The original four native application cases passed before adding the frame audit.
An initial unbatched rapid-scroll run exceeded its 60-second test bound; batching layout at the real commit boundary corrected the repeated work without raising that timeout.
The five published native application cases then passed in 59.9 seconds, including the frame audit.
An initial 100-cycle assertion incorrectly expected every framework observer to be zero rather than return to its captured baseline; the corrected test compares the actual pre-lease observer count.
After the detached-bootstrap update and a forced nonincremental rebuild, all 100 actual DOM cycles passed in 52.0 seconds.
The measured loop was 46.1947588 seconds, with 101 empty-before-native-attachment checks.
Document/window/font listeners returned to `[1,15,0]` from active `[5,16,1]`; observer count returned to its baseline of 1.
Every retired input had zero listeners.
An earlier loaded-machine bootstrap run reached the existing 180-second bound; no timeout or budget was relaxed.
The coordinated exclusive native/browser timing windows distinguish machine contention from a performance claim.
The exclusive W3 browser timing run did not meet the 50-millisecond gate: p50 was 78.4 ms, p95 172 ms, and maximum 213.2 ms.
All 100 measured samples were saved before the assertion failed, and the original JSON was preserved separately rather than overwritten by a retry.
The runner exited 1 and its browser/server stopped before the next platform benchmark window.
This is a performance blocker, not a claimed passing release gate.
Debug-only phase profiling is used to attribute native interop/layout work before optimization; its instrumented results are not substituted for the uninstrumented gate.
The later W5 run below records the corrected native latency result; W3 remains preserved as a failed result.

### Profile-driven correction and W5 result

Debug-only profiling counted actual cross-language/native operations rather than attributing costs to the recording backend.
Initially, mean native measurement cost was 59.86 ms across 55 calls per sample, with two frame applications costing 14.54 ms.
The final prune/layout work was visible in separate callback-end and await-completed timestamps, so the timing did not stop at an early model event.
Corrections retained the same shared solver and native coverage validation:

- Native measurement descriptions cache bounded results for resolved typography/box metrics, offered width/mode, language/direction, and visible label text. The cache is attachment-owned, has at most 256 entries, never stores live input/password values or DOM nodes, and clears on retirement and font/style/resize changes.
- Per-live-peer measurement/context caches hold at most eight offers, invalidate changed peers/ancestors (and affected container descendants), and are removed when peers retire. Browser font, stylesheet-load, ancestor language/direction, resize, and resolution changes clear the owned layout cache.
- Known-width vertical leaf measurements cross interop in batches; other width-dependent layouts still use the exact two-pass measurement path.
- Typed packed frame arrays avoid repeated anonymous-object serialization. Unchanged native frames and unchanged logical metadata avoid redundant writes.
- Each layout pass reads one coherent element-state snapshot instead of repeatedly walking guarded model getters.

Existing pressure, Grid, font-change, measurement-safety, and retained-editor tests passed after these changes.
A new delayed stylesheet-load case verifies that CSS completing after its link insertion invalidates the cache.
Observer tests include resolution media-query listener removal.
No row controls are pooled, no native dimensions are guessed from the model, and every native commit still checks actual visible row rectangles.

The exclusive W5 run used the already-built uninstrumented Debug Wasm application once: 10 warmups followed by all 100 measured requests.
It passed the unchanged recorded-host p95 gate: **p50 32.8 ms, p95 44.2 ms, maximum 56.3 ms**.
The maximum is retained, not hidden by the percentile.
Timing begins locally before `RequestOffset` and ends after the asynchronous continuation resumes outside the callback, including final native prune geometry.
The run used Edge `153.0.4234.48`, Windows x64, a 600 by 700 CSS-pixel page, device scale 1, and the sample's ordinary 16-pixel font.
This is one recorded-host budget result, not a guarantee for all browsers, machines, or physical IMEs.

`native-viewport-performance.json` contains all 100 W5 samples.
The original W3 JSON and an immutable W5 copy are preserved in the session artifacts.
Profiling is disabled by default and excluded from Release.
The dedicated `test:virtual-performance` command selects the same unchanged 50 ms assertion separately from ordinary correctness runs; it requires an appropriately quiet measurement host.
It is not silently retried in a concurrently loaded full correctness run.

The final full Debug correctness runner passed 46 tests, including 100 actual attachment cycles.
The optimized 100-cycle case completed in 18.2 seconds (14.0670576 seconds in its measured loop), requiring an empty presentation before all 101 native attachments.
Document/window/font listeners returned to `[1,15,0]` from active `[6,16,1]`.
Observers returned to baseline 1; resolution-query listeners returned to baseline 0; all retired inputs had zero listeners.
Insertion-time measurements observed a peak of 16 native inputs versus a derived 21-row transient bound for the actual old/requested heights of 548 pixels.
These resource/peak measurements are stored in adjacent JSON evidence rather than inferred from model collection size.
The final published root and `/nested/gallery/` correctness suites each passed 45 cases, in 3.0 and 3.1 minutes.
They include actual virtual-list bfcache restoration of the same sparse native editor and committed viewport.
The final Debug suite passed 46 cases in 1.9 minutes; its 100-cycle case took 18.2 seconds.
The bounded performance assertion is now run explicitly with:

```powershell
$env:XUI_BROWSER_CHANNEL = "msedge"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:virtual-performance
```

The ordinary complete correctness commands remain `npm ... test` and `test:gallery:release`.
The dedicated diagnostic config `playwright.virtual-profile.config.js` records phase costs but never reports a budget pass.
All these runs kept zero retries and no ignored teardown errors.
The W5 result applies to this v3 callback-end implementation; a subsequent optional explicit-settle protocol is a separate checkpoint.

## Explicit post-prune settlement follow-up

The separate settled cohort uses the optional `ISettledVirtualViewportLease` contract from the shared owner and the conditional Host facade.
Base v3 methods are unchanged.
The DOM implementation validates the current committed epoch and absence of a reservation before flushing layout.
It then validates actual native row rectangles and metadata again, without consuming queued future viewport intent.
The shared sample invokes this operation after `Controller.CommitPreparedViewport` and pruning, before its completion event.
The adapter clears the pending prune-layout flag; later independent status/focus changes may still require the normal callback-finally layout pass.
No async frame or no-op capability stands in for the explicit flush.

The previous W5 data and source patch remain frozen as the v3 result.
The follow-up imports were hash verified: shared settled sample `29529576DE7BF63D982D5B7234492688C19AF086AC7C0CEC48DE235140E158D9` and Host settling facade `4B85063A35B429FE7A1B10D1C436B0120A36D57193DEFCD9976F34AC5B59EADE`.
Forced nonincremental builds avoid reusing binaries with newer timestamps than imported source archives.

The public C# Host probe verifies the returned optional capability and flushes actual native geometry.
Native tests verify that queued future offsets survive a flush, no callbacks run inline, a reserved/stale phase is rejected, and invalid actual row geometry cannot report successful settlement.
A JavaScript-only probe was corrected to reserve a snapshot in its fresh posted callback rather than assuming it remained current across a Playwright round trip; ResizeObserver is allowed to supersede an old request.
No retry or weaker protocol assertion was added.

The updated 100-cycle artifact now contains actual final listener counts, 101 empty-before-attachment checks, and zero peak retired-input listeners, not only assertions comparing to the baseline.
The settled-cohort 100-cycle case passed in 44.9 seconds, with a measured loop of 39.9857539 seconds.
Seven native/API cases passed in 15.6 seconds; seven published virtual-list cases passed in 27.6 seconds, including 24-pixel root-font geometry and real bfcache.
The explicit-settlement performance result must be recorded separately; the earlier W5 percentile is not silently relabeled as a new-cohort result.

The final shared completion-order correction `A8A8582F570D6A13C5B726F05A6722CD134D10F01F3FC65EBE72345FE1AC8010` moves Ready/status/focus work before the explicit flush and completion event.
After a forced rebuild, native/API checks passed 7 cases in 18.3 seconds, the 100-cycle test passed in 35.2 seconds, and published native virtual-list checks passed 7 cases in 28.2 seconds.
The corresponding resource JSON records 101 empty-before-attachment checks, zero retired-input listeners, and exact final listener/observer/media-query baselines.
The exclusive W6 timing run for this A8 settled cohort **failed** the unchanged gate: p50 63.0999 ms, p95 109.3 ms, maximum 130 ms.
All 100 raw samples were saved before the failure and preserved separately as `native-viewport-performance-w6-settled-failed.json`.
The runner exited 1 and cleaned up; the coordinator released the CPU window immediately.
The v3 W5 pass remains valid only for its frozen cohort and is not used to waive this failure.
Further settled-cohort performance attribution remains open; independent Forms/Presentation work does not declare this budget resolved.

## Native Forms and Presentation projection

This follow-up imports the frozen `3B914004` presentation source, `A30C6B1C` Forms/core hooks, and `FC980420` native password-length validation fix.
The shared owner verified compatibility with the existing metadata/settled Host files.
No shared controller, generator, or public-contract implementation was independently edited here.
Release initially reused an older generator binary because imported archive timestamps were older; a forced `--no-incremental` Release build corrected the cohort rather than changing the authored workbenches.

`MultilineText` projects to a real textarea with LF text, read-only behavior, UTF-16 bounds, native Enter, and retained identity/selection.
Input-purpose hints retain `type=text`, so intentionally invalid number/email drafts are not coerced by HTML.
`PasswordInput` projects to a native password input.
Its value is absent from serialized peer state and change-event payloads.
Programmatic values are silent, native length/scoped read observe actual autofilled state, and both Core and the DOM cleanup path clear the native value before retirement.
The tests use generated nonsensitive code-unit seeds; no user clipboard, stored credentials, or real password data is read.
They inspect only lengths, absence of serialized values, and copy/cut/context-menu cancellation.
The explicit-read boundary necessarily creates temporary interop strings and is not described as secure memory erasure.

The password lifecycle cases verify zero native length at the instant the mount is cleared and at the instant a keyed secret subtree is removed.
Retired controls do not regain a secret when readded or reattached.
Invalid UTF-16 and oversized values fail before accepted mutation; an exact maximum remains valid.
Multiline programmatic CR/CRLF becomes LF through the shared setter without raising the authored change handler.

Typography maps the typed resolved size to rem at a 16-CSS-pixel reference.
Native tests exercise 14/28-pixel text, weight 400/700, a 20-pixel document root producing 35-pixel editor text, and narrow caption/editor geometry.
The same actual input retains focus, selection, composition state, and zero value writes while appearance changes.
Direct null resets on all five supported target kinds restore inherited family/size/weight; the shared Restore inherited button resets theme only, as authored.
A palette test checks actual scoped computed colors, native accent, contrast of the provided foreground/background pair, System changes, and forced-color precedence.
Document body styling is unchanged.
System/forced-color media-query listeners return from two to zero on reset and disposal.
No periodic animation or observer loop was added for an idle theme.

### Commands and outcomes

```powershell
dotnet build bindings\dotnet\Experimental\WebDemo\WebDemo.csproj -c Debug
dotnet build bindings\dotnet\Experimental\WebGalleryDemo\WebGalleryDemo.csproj -c Debug --no-incremental
dotnet build bindings\dotnet\Experimental\WebGalleryDemo\WebGalleryDemo.csproj -c Release --no-incremental
dotnet publish bindings\dotnet\Experimental\WebGalleryDemo\WebGalleryDemo.csproj -c Release --no-build
$env:XUI_BROWSER_CHANNEL = "msedge"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests test
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery -- forms.spec.js presentation.spec.js
$env:XUI_WEB_BASE_PATH = "/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery:release -- forms.spec.js presentation.spec.js
$env:XUI_WEB_BASE_PATH = "/nested/gallery/"
npm --prefix bindings\dotnet\Experimental\WebDemo.Tests run test:gallery:release -- forms.spec.js presentation.spec.js
```

Debug and forced Release builds succeeded with zero warnings/errors; publication retained only the optional Wasm optimization recommendation.
The complete Debug runner passed 51 cases in 3.7 minutes, including all 100 virtual attachment cycles.
The shared Forms corpus and native Forms/Presentation cases passed 6/6 in Debug (33.2 seconds), published root (33.6 seconds), and published nested hosting (28.1 seconds).
Targeted adapter/lifetime checks passed 12/12 in 48.3 seconds before the final additional password-payload assertion was included in the 51-case run.
The Forms corpus contributes the same 21 literal shared checks rather than browser-specific expectations.
These observations use Edge 153.0.4234.48 and the prior tool versions; Firefox/WebKit are separate integration-owner work, not inferred passes here.
Physical screen-reader/IME acceptance remains manual, and the separate settled-M6 performance failure remains open.

### Presentation review correction

Bounded review of the first Forms/Presentation handoff found two real theme-boundary issues.
Theme disposal restored constructor-time mount styles even when no override had ever been acquired.
Theme foreground rules also overrode native disabled control colors.
The follow-up captures a baseline lazily on the first nonnull theme, restores only an acquired override, and captures a fresh baseline after clear/reacquire.
It preserves original CSS priorities and a preexisting theme class.
Null/dispose without an acquired override leaves external mount styling untouched.
Foreground/accent rules now target enabled controls, including under forced colors.

The native regression compares themed disabled buttons/editors with an otherwise identical unthemed native adapter surface (same markup, accessibility, inert, and font state).
It covers default inheritance, explicit System without foreground overrides, custom colors, a reenabled control, and forced colors.
The baseline/priority/disabled checks plus existing secret/typography lifetime cases passed 5/5 in 23.5 seconds.
The original B1C48 handoff is historical; this correction is a separate required patch rather than a rewritten artifact.

## Real retained Workspace Studio

The DOM backend now projects the typed PageView, TabStrip, NavigationView, host viewport, and explicit Label layout contracts.
The gallery links the exact shared Studio root, document editor, section, catalog, Operations dashboard, and workload row sources.
All business actions and computed reports remain compiled C#.
The Operations scope is an actual native select; no button substitute or JavaScript business implementation is used.

The imported model/compiler order was Choice/Range `2FC10C60`, retained navigation `78FE16AD`, Label owned `3FFDD597` plus hooks `94D987BC`, and responsive `42C39487`.
Choice model dependencies do not imply a RangeInput projection: that kind still fails explicitly if encountered.
The shared Studio app overlays were `D77B22D9`, `198B7F42`, Operations `BEAFEA57`, and Back `82C1A643`.
The later shared catalog row/pitch work is described separately below.
Core files were imported from owner-validated snapshots, not independently changed here.

### Native page and responsive behavior

Page roots stay alive when inactive, retaining native text/value/selection while hidden and inert.
Native input eligibility and indeterminate progress gating account for inactive panels.
Readonly page selection/visibility validation rejects affected focused editors and active composition before model mutation.
An action button inside a page can safely transfer focus to its own typed linked selector when navigation succeeds; this does not steal focus from another host or editor.
This was required by the real Operations-result Open action, rather than bypassed with an application ID special case.
Tab headers are retained by exact ID, use roving focus, and keep keyboard order equal to current source order after reordering.
The native tab/tabpanel `aria-controls`/`aria-labelledby` relationship is scoped and typed.
Close requests are separate native buttons and honor application veto.

The native mount observer reports the same client allocation used by layout, including zero-before-layout state.
Later callback coalescing and retirement are owned by the shared Host.
Actual Studio tests exercise widths 719/720/1119/1120 and 320, keeping the same document textarea across compact Catalog/Document/Details transitions.
The initial same-active-document compact-open bug was reported to the shared owner and fixed by the frozen V4 explicit-open intent, not a DOM click-ID workaround.
Hidden native scroll/label measurement probes must live outside the hidden subtree; otherwise their zero measurement was a real failure.
The corrected probes retain resolved font metrics and are always removed.

The mount-owned Alt+Left handler consumes shared handled/blocked outcomes, reports exceptions, and delegates an unhandled request exactly once.
It does not rewrite popstate history after traversal.
Shared corpus tests intercept only the outer history delegate to assert its call count; separate actual browser navigation verifies bfcache and full-reload behavior.
The explicit Debug test query installs a read-only idle observer that first drains native event delivery, then awaits the root aggregate `LastOperation`.
The probe asset and C# method are absent from Release.
Release scenarios assert real native results, not private producer quiescence.

### Native catalog content, not only viewport coverage

The original Studio pitch of 96 passed root/lease coverage but clipped real content at doubled logical text size.
At both 1400- and 320-pixel viewports, the original title-bearing button's text extended 56 pixels past its allocation.
The shared owner supplied a candidate row with a full-title native single-line ellipsis label plus a separate Open button.
The candidate retained full accessible title text and still measured 101 pixels naturally at 2x, so its 96-pixel allocation clipped five pixels of caption.

After actual DOM, Windows, and Android evidence, the shared Studio row alone moved to pitch 128.
The M6 benchmark's independent pitch/contract was not changed by the backend.
The owner-provided two-file candidate hash was `0AC511A9487557C15228CD7BBFBEDD14C31EAB315B3F7614FE1CC257BE53603A`, later frozen by the owner as equivalent row/viewport source.
All eight actual DOM cases passed at 1400/320 widths, 1x/2x logical font sizes, and sample/160-character dirty titles.
Both `html` and `body` font size were changed from 16 to 32 pixels; this was real logical text scaling, not a browser zoom proxy.
Native Open button/caption computed sizes were 32/24 pixels at 2x.
Natural row heights were 63 pixels at 1x and 101 at 2x; the assigned 128-pixel row contained the actual button and caption text ranges.
The 2x Open button was 49 pixels high, the full caption 32, with the authored padding 8 and spacing 4.
Long title text and accessible name both retained all 160 UTF-16 units while native ellipsis controlled its visual width.
Independent JSON evidence is under `build/browser-virtualization/studio-catalog-*.json`.

### Verification and captures

The actual retained-page corpus contributes 41 literal checks and the V4 Studio corpus 80.
Native arrows/Home/Space, close veto/removal, editor identity, exact IDs, current page visibility, and typed ARIA relationships are exercised in the browser.
The combined Page/Studio/Label Debug runner passed 12 cases in 1.1 minutes.
Published root acceptance passed 14 cases in 53.8 seconds; `/nested/gallery/` passed the same 14 in 57.7 seconds.
These include actual Studio bfcache retaining the same edited textarea/selection, scoped native Back composition protection, and a full reload that deliberately does not persist drafts.
The full legacy/Forms/M6 Debug regression runner passed 53 cases in 2.8 minutes, including 100 attachment cycles.
An additional native-select boundary case subsequently passed with the adapter group (11 cases, 16.0 seconds), verifying a decimal identity above 2^53, disabled options, absent selection, retained select identity, and silent updates.

Actual published desktop and phone screenshots were captured from the shared deterministic workspace scenario and visually inspected:
`build/browser-captures/studio-web.png` (1440x1000) and `studio-phone-web.png` (390x844), each with adjacent browser/source/image-hash metadata.
Capture uses `XUI_CAPTURE_APP=studio` or `studio-phone` through the existing command.
The workspace intentionally has native scrollable panes and a virtual catalog, so its capture registry explicitly permits scrolling instead of claiming the entire dataset is visible.
There is no canvas or HTML reconstruction of the shared UI.
All observations here remain the recorded Edge browser gate; integration-owned Firefox/WebKit coverage and the separate settled-M6 latency failure remain distinct.

## Actual packaged browser image checkpoint

The image implementation imports only owner-pinned SDK/runtime/compiler dependencies: file-selection stream types, packaged assets `87D361F7`, image helpers `39A09F80` plus `656D2D3E`, core image `C7F4E5AA` plus structural guard `49D920F6`, and separate source-pixel reservation `69FD7D40`.
No Dialog, Reveal, Font-control, or unrelated later Host snapshot was pulled into this checkpoint.

The shared ImageWorkbench executes real C# callbacks and embedded assembly resource reads.
Native `createImageBitmap` decodes the bounded source, its actual dimensions are checked against the shared preflight, and an off-DOM canvas resamples into a bounded output.
The visible object is an actual native `img`; no canvas substitutes for application UI.
The source bitmap closes in native `finally` before the source reservation/admission is released.
Canceled-but-still-running decodes occupy one of the two native slots until actual settlement.
The source budget is a separate 64 MiB estimate and each simultaneously owned output pixel copy uses the separate 8 MiB output budget.
The generated output PNG blob is independently size-bounded.
These are explicit owned-resource policies, not a total browser/codec/GPU resident-memory guarantee.

The browser limitation is recorded honestly: shared header/format/CRC/pixel-limit validation happens before native decoding, but native coded dimensions cannot be queried independently until after raw allocation.
`ValidateCodecSource` therefore runs before output resampling/publication, not before the browser's first internal pixel allocation.
Exact output/options validation still gates Ready.
Generations are decimal Int64 strings, and stale native leases close/revoke rather than publishing into a replacement attachment.

The unsized desired logical image viewport is 192x144 in every load state.
Authored preferred/fixed/per-axis/parent sizing still applies, and quality 64-to-32 changes neither allocated bounds nor native editor/image identity.
Containment uses source aspect ratio: the original 3x2 fixture resized to 2x1 still displays at 3:2 inside the box.
The test obtains actual native decoded RGBA pixels through a temporary test-only canvas; original fixture pixels were independently read from the verified PNG bytes.
Its six pixels are red, green, blue, white, black, and brown `(128,64,32)`, all opaque.
The original 84-byte fixture hash is `9CB62AD562FBB01637E27FE10DC0FF4C7078FDA012BF70E3E2B761B025B24870`.

Five real-browser cases passed in Debug (28.5 seconds) and published Release (16.8 seconds):
offline packaged loading/quality and retained editing, exact native pixels plus containment/parent cap, late canceled bitmap closure, initially hidden Ready-before-show, and a source-replacement storm retaining at most two uncancelable decodes.
The storm test holds actual browser-decoded bitmaps, observes explicit admission failure rather than starting a third codec, releases both, and verifies that a later explicit load succeeds.
The invalid resource is intentionally non-image JSON and establishes preflight Error, not a claim that a codec decoded invalid bytes.
The first pixel-test expectation used the wrong final RGB triplet; reading the actual fixture bytes corrected the expectation rather than changing decoded output.
Release was forced nonincremental after imported source timestamps to avoid a stale analyzer/runtime cohort.
No user file, remote image URL, or user clipboard was touched.
The later optional `IImagePresentationEvents` contract is supplied by owner pin `769E5F55`.
Only its exact Image type plus Events interface/method were merged into the current image Host cohort, avoiding an unrelated Dialog guard from the owner's newer full Host file.
A queued native image error after decoding calls that generation-aware fatal sink outside native guards.
The real Host detaches, unbinds the image, and releases encoded/source/output ownership before the original error is reported at the JavaScript boundary.
The injected post-Ready error test passed (11.4 seconds): `Host.IsAttached=false`, zero native children, no retained image src, and zero decode/source/output/encoded counters; a later stale error leaves that detached state unchanged.
This is explicitly an injected failure-path test, not a claim that immutable browser blobs naturally fail after Ready.
Twelve repeated real loads/clears also returned all owned output URLs to zero.

## Native retained Reveal and Operations drawer

The separate Reveal checkpoint imports owner geometry/types `8B837257`, core hooks `F31E7AA9`, and outer-layout guards `B64F257D`.
The existing optional image-fatal method/interface from `769E5F55` was preserved when the older pinned Host hooks were overlaid.
No unrelated Dialog or Font-control Host features were imported.
The common outer Reveal sizing/flex restrictions and the shared native measure/arrange functions remain authoritative.

The backend observes actual progress from a browser animation-frame clock.
It uses `RevealLayoutMath.Measure` without allowing a generic Exact offer to reset the animated demand to full size.
Arrangement separately retains full child size and clamps the visible clip to the allocated parent slot.
The child is not reparented, shrunk, translated, or recreated during animation.
Closing disables native input/accessibility immediately; at a settled zero extent it is also visually hidden while remaining a layout participant, preserving Stack spacing.
Progress descendants use the same effective-input/visibility gate, so logical closed content cannot keep its indeterminate animation active.

Motion changes are atomic Open/duration/direction snapshots.
The unchanged-motion path retargets from actual progress.
Changing motion first settles the old logical endpoint, then applies the new configuration and target.
No private managed timer or application animation copy is used.
Reduced-motion, forced-color, ancestor availability, and disposal stop the owned clock.
The native module owns and releases its media-query listeners.

Actual shared workbench checks passed three cases initially (20.3 seconds).
The final four-case workbench plus real Operations drawer set passed in Debug (23.8 seconds) and published Release (16.4 seconds).
They observe intermediate native progress with clip-height equal to full-child height times progress, unchanged child geometry, closed sibling spacing, retained native editor identity/value, focus/composition close veto, reduced/forced policy settlement, and direction-change settlement.
The public C# `Host.GetRevealPresentation` fixture passed in 10.2 seconds and verified settled zero/one native states, no idle animation frame after completion, and zero additional animation/listener resources after retirement.
These are bounded recorded-browser observations, not a frame-rate or all-engine motion guarantee.

The real Studio Operations drawer uses shared `EBE3AEA8` compact metrics, opt-in `A41238BF`, and focus correction `17841F5A`.
The host supplies the existing `enableOperationsDrawer` flag only for `?app=studio&motion=1`.
Default Studio stays unanimated.
Actual native tests close/open that drawer, retain the same select control, switch document/Operations tabs, activate the closed page into its external toggle rather than a hidden field, and perform a local scan after reopening.
No separate platform UI was authored for the drawer.
The newer short-height shared application packet and root-owned browser engine/configuration changes are not overwritten by this delta.

The parent integrated the host and tests, then consumed shared layout snapshot 2.
Only `OrderBuilder.xui` changed: the explanatory line moved inside the scroll region, leaving one fixed title to preserve more keyboard-visible space on Android.
Its SHA-256 is `01E4778BB587CDD61EB9534D4EA3A9E2CDBEAED3B0E6FD4DF98B914C305E5F20`; the model and corpus hashes above remain unchanged.
The parent rebuilt Debug and republished Release, then reran the order suites: 12 Debug cases, 14 published root cases, and 14 published `/nested/order/` cases all exited zero, including teardown.
The integrated greeting Debug 12 and published root 6 cases also exited zero.
