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
