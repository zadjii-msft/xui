# Contributing to XUI

## Requirements

Native builds require Windows 10 version 1703 or later, CMake 3.24 or later, and Visual Studio 2022.
Install the C++ desktop workload and a Windows SDK.
For ARM64 builds, include the ARM64 C++ tools.
The Windows backend requires a 64-bit build.

C# samples require the .NET 10 SDK. Rust samples require the Rust MSVC toolchain.
The VS Code syntax package requires Node.js and npm.
Binding generation uses Python.

Run the commands from the repository root in Visual Studio Developer PowerShell.
The commands use a separate build directory, so they do not replace another build's executables.

## Build the native code

```powershell
$arch = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq "Arm64") { "ARM64" } else { "x64" }
$rid = if ($arch -eq "ARM64") { "win-arm64" } else { "win-x64" }
$build = "build\$arch"
cmake -S . -B $build -G "Visual Studio 17 2022" -A $arch -DBUILD_TESTING=ON
cmake --build $build --config Release --parallel 4
```

The architecture selection avoids x64 emulation on ARM64 Windows.
Use the same architecture for `xui.dll` and each application that loads it.
Close executables from this build directory before relinking them.

If CMake is absent from `PATH`, find the Visual Studio copy:

```powershell
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
$tools = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$env:PATH = "$tools;$env:PATH"
```

Then run the native build commands.

## Native samples

After the native build, run a sample:

```powershell
& ".\$build\Release\xui_demo.exe" "C:\Windows"
& ".\$build\Release\xui_gallery.exe"
& ".\$build\Release\xui_task_manager.exe"
& ".\$build\Release\xui_images.exe" "C:\Pictures"
```

The explorer opens the current directory without a folder argument.
The thumbnail sample shows a folder field without an argument.
The [sample references](docs/specs/README.md#samples) describe shortcuts, behavior, and limits.

### Gallery

To build only the galleries, use:

```powershell
cmake --build $build --config Release --target xui_gallery xui_winui_gallery
& ".\$build\Release\xui_gallery.exe" --page combo
& ".\$build\Release\xui_gallery.exe" --winui-catalog
& ".\$build\Release\xui_winui_gallery.exe"
```

The last command opens the compact WinUI-style experiment.
Both galleries accept `--light`, `--high-contrast`, and `--system-titlebar`.
The ordinary gallery also accepts `--winui` for the compact experiment.
The complete catalog has a live Classic/WinUI switch.

The gallery separates the live controls from their reference content:

- `demo\gallery.cpp` composes the pages and shares the selected language across their code tabs.
- `demo\gallery_catalog.hpp` supplies navigation metadata and C++ excerpts.
- `demo\gallery_reference.hpp` supplies usage guidance, exercises, limits, documentation paths, and the other language excerpts.
- `demo\gallery_urls.hpp` maps documentation paths to handbook URLs. `gallery_links.hpp` opens links.

Keep the reference entries in catalog order.
Use current public APIs in each excerpt.
For unsupported operations, describe the binding limit instead of inventing a wrapper.
Documentation paths must name existing pages under `docs\specs`.

To check gallery content and the language tabs, run:

```powershell
cmake --build $build --config Release --target xui_gallery xui_gallery_catalog_tests xui_gallery_smoke
ctest --test-dir $build -C Release -R "^xui_gallery_catalog_tests$" --output-on-failure
& ".\$build\Release\xui_gallery_smoke.exe" ".\$build\Release\xui_gallery.exe" --reference-only
```

The last command opens a desktop window.
It checks shared language selection, native code text, copy actions, handbook URLs, and navigation links without opening a browser.

### Use XUI in a C++ application

Link the executable to `xui_windows`.
Include `demo\xui.rc`, or supply an equivalent common-controls v6 and per-monitor-DPI manifest.
For MSVC builds that use this resource, set `/MANIFEST:NO`.
The following CMake example assumes `xui` is a subdirectory beside your `main.cpp`:

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyApp LANGUAGES C CXX RC)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_subdirectory(xui)
add_executable(my_app WIN32 main.cpp xui/demo/xui.rc)
target_link_libraries(my_app PRIVATE xui_windows)
target_compile_options(my_app PRIVATE /utf-8 /EHsc)
target_link_options(my_app PRIVATE /MANIFEST:NO)
```

The [application reference](docs/specs/application.md) describes thread ownership, callbacks, layout, and error handling.

### C ABI application setup

The [C guide](docs/specs/languages/c.md) contains a complete `main.c`.
Link it to the `xui` DLL target, not the C++ `xui_windows` target.
This CMake example assumes the XUI checkout is a subdirectory beside `main.c`:

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyCApp LANGUAGES C CXX RC)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_subdirectory(xui)
add_executable(my_c_app main.c xui/demo/xui.rc)
target_link_libraries(my_c_app PRIVATE xui)
target_compile_options(my_c_app PRIVATE /utf-8 /W4)
target_link_options(my_c_app PRIVATE /MANIFEST:NO)
add_custom_command(TARGET my_c_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:xui>" "$<TARGET_FILE_DIR:my_c_app>"
    VERBATIM)
```

CMake uses the C compiler for `main.c` and the C++ compiler for the native XUI implementation.
The target supplies the include directory and import library.
The resource embeds the common-controls v6 and per-monitor-DPI manifest.
The post-build command places the matching DLL beside the console executable.
For a separately built DLL, supply its `xui.lib`, the `include` directory, and the same manifest instead.
Keep the C executable and native library on the same architecture.

## C# and declarative samples

After the native build, run a sample:

```powershell
dotnet run --project bindings\dotnet\DeclarativeSample -r $rid
```

Sample builds copy this checkout's `build\<architecture>\Release\xui.dll` into their output.
Publish copies the same DLL into its output.
The default sample architecture matches the host Windows architecture.
`-r win-x64` or `-r win-arm64` selects another architecture.
`XuiNativeConfiguration=Debug` selects a Debug native build.
`XuiNativeDir` selects an explicit native output directory.
A missing DLL stops the build with a diagnostic and native build commands.
No XUI entry in `PATH` is necessary.

For the development loop, use:

```powershell
dotnet watch --project bindings\dotnet\DeclarativeSample --non-interactive "-p:RuntimeIdentifier=$rid"
```

Edit `bindings\dotnet\DeclarativeSample\Counter.xui`.
Supported property and handler edits preserve state.
Structural edits require window replacement or process restart and reset transient state.
The [language guide](docs/specs/xui-language.md) describes project integration and reload behavior.

For restart-on-save without in-place hot reload, add `--no-hot-reload` to the watch command.
If you change the compiler, stop the watcher before rebuilding it.
Windows can keep the analyzer assembly open while the watcher runs.

Use `bindings\dotnet\Minesweeper` instead of `DeclarativeSample` to run the game.
Use `bindings\dotnet\Sample` for the handwritten C# sample.
Run the generated executable or use `dotnet run`.
Direct `dotnet Sample.dll` execution does not apply the apphost's native-control manifest.

### XUI Designer

After the native build, run the designer:

```powershell
dotnet run --project bindings\dotnet\Designer -c Release -r $rid
```

To open an existing trusted component, append its path:

```powershell
dotnet run --project bindings\dotnet\Designer -c Release -r $rid -- "C:\Projects\Demo\Counter.xui"
```

The designer uses the same native DLL selection as the other C# samples.
Its runtime includes the XUI generator and the SDK Roslyn assemblies.
It does not support NativeAOT, trimming, or single-file publishing.
The [designer guide](docs/specs/designer.md) describes preview limits, shortcuts, file behavior, and recovery drafts.

Run the compiler tests without a native DLL:

```powershell
dotnet run --project bindings\dotnet\Designer.Tests -c Release
dotnet run --project bindings\dotnet\Designer.TemplateTests -c Release
dotnet run --project bindings\dotnet\Designer.DocumentTests -c Release
dotnet run --project bindings\dotnet\Designer.DiagnosticsTests -c Release
```

The template tests compile every built-in example and reject compiler warnings.
The template sources are under `bindings\dotnet\Designer\Templates`, with the counter example in `Starter.xui`.
The document tests cover atomic saves, disk conflicts, UTF-8 input, and recovery snapshots without a native DLL.
The diagnostics tests cover native paragraph offsets, Unicode selection, stale revisions, and locations from the real compiler.

Run the source hierarchy and visual edit tests without a native DLL:

```powershell
dotnet run --project bindings\dotnet\Designer.SourceTests -c Release
```

This suite checks exact UTF-16 ranges, source preservation, stale revisions, container rules, and generated compilation.
Dimension codec checks cover numeric syntax, culture independence, exact tuple preservation, input limits, and expression and comment refusals.
It also checks opt-in element mapping with the existing managed generator fakes, without native DLL calls.
Diagnostic checks cover exact expression columns and the actual preview compiler output.
The [source API contract](docs/specs/designer.md#source-editing-api) describes edit proposals and editor integration.

After the native build, run the generated RangeInput and Progress checks:

```powershell
dotnet run --project bindings\dotnet\ValueControls.Tests -c Release -r $rid
```

The fixture uses the actual `.xui` generator and this checkout's native DLL.
It checks construction order, numeric updates, callbacks, defaults, invalid values, and opt-in element mapping.
It also opens a bounded native window and closes it through the window dispatcher.

After the native build, run the desktop smoke test:

```powershell
dotnet run --project bindings\dotnet\Designer.Preview.Tests -c Release -r $rid
dotnet run --project bindings\dotnet\Designer -c Release -r $rid -- --smoke
dotnet run --project bindings\dotnet\Designer.RecoveryTests -c Release -r $rid
dotnet run --project bindings\dotnet\Designer.GroupingTests -c Release -r $rid
dotnet run --project bindings\dotnet\Designer.TextModeTests -c Release -r $rid
dotnet run --project bindings\dotnet\Designer.NavigationTests -c Release -r $rid
dotnet run --project bindings\dotnet\Designer.SearchTests -c Release -r $rid
dotnet run --project bindings\dotnet\Designer.ViewportTests -c Release -r $rid
dotnet run --project bindings\dotnet\Designer.CommandTests -c Release -r $rid
dotnet run --project bindings\dotnet\Designer.IndentationTests -c Release -r $rid
dotnet run --project bindings\dotnet\Designer -c Release -r $rid -- --builder-smoke
dotnet run --project bindings\dotnet\Designer -c Release -r $rid -- --file-smoke
dotnet run --project bindings\dotnet\Designer -c Release -r $rid -- --selection-smoke
dotnet run --project bindings\dotnet\Designer.LayoutTests -c Release -r $rid
dotnet run --project bindings\dotnet\Designer.WorkspaceTests -c Release -r $rid
dotnet run --project bindings\dotnet\Designer.DiscardTests -c Release -r $rid
cmake --build $build --config Release --target xui_content_host_window_tests
& ".\$build\Release\xui_content_host_window_tests.exe"
cmake --build $build --config Release --target xui_abi_features_tests
& ".\$build\Release\xui_abi_features_tests.exe" --activation
```

The preview regression opens one window with native editor and preview content.
It covers repeated replacement, bounded handles, candidate rollback, source versions, scoped callback errors, and editor identity, selection, and undo.
It also checks that replacement does not change foreground activation.
The smoke test opens the designer with its embedded preview.
It covers native layout, compiler diagnostics, preview construction errors, recovery after those errors, and file operations.
The recovery UI test uses isolated drafts and a real native `ContentDialog`.
It covers draft selection, recovery copies, dirty-source protection, confirmed deletion, corrupt metadata, and file races.
The grouping UI test uses the production hierarchy and inspector with native source editing.
It covers wrap buttons, root replacement, unwrap refusals, hierarchy shortcuts, and native undo.
The navigation UI test covers diagnostic buttons, F8 routing, exact native selections, stale source, and replaced diagnostic text.
It also covers the native Go to dialog in Classic and WinUI, Unicode boundaries, Enter/Escape, revision guards, cancellation, and undo.
The source-search UI test covers literal matching, native selection, current-source offsets, keyboard routing, and undo preservation.
It also covers Unicode word boundaries, single and bulk replacement, no-op edits, native undo and redo, and atomic length-limit errors.
The viewport UI test covers preset and custom dimensions, invalid input, vertical scrolling, retained preview state, and controls inside a narrow pane.
The command UI test uses the native CommandSurface in Classic and WinUI.
It covers native query input, Enter, Escape, focus, disabled commands, changed availability, duplicate invocation, cancellation, and source undo.
The source-indentation UI test covers Enter, leading-whitespace Tab and Shift+Tab, native undo, caret positions, focus, and length-limit errors.
It also covers line comments in both visual styles, selected-line boundaries, Unicode, blank lines, mixed prefixes, read-only source, and atomic length refusals.
Source-line duplication coverage includes both styles, exact text and selections, trailing empty lines, Unicode, one-action undo/redo, focus routing, and length limits.
The selection smoke uses actual native preview clicks in the full application.
It covers Find, authored-handler suppression, version guards, source and hierarchy selection, native undo, and explicit stale-preview refusal.
It also covers outline feedback for the selected control and immediate invalidation after a source revision.
Source replacement also exercises hierarchy updates, preview compilation, and one-action undo through the complete application.
Selected-text search checks cover native query contents, source-only shortcuts, current selections, matching options, Unicode, line boundaries, query limits, and undo in both styles.
The application selection smoke checks palette actions, disabled empty selections, hierarchy synchronization, and retained live preview state.
Viewport checks cover exact Compact dimensions, retained preview versions and state, native picking, and a return to Fit.
Command checks cover shortcut routing, pointer-mode exit, native dismissal, discard protection, and focus inside the scrollable inspector.
Structural command checks cover duplicate, delete, movement, each wrapper, and unwrap through the native palette.
They check live preview text, arranged order, selected nodes, disabled root actions, and exact source undo.
Relative selection checks cover parent, first-child, sibling, and root commands through the native palette without replacing the live preview.
Go to checks cover the header action, command palette, source-only shortcut, exact caret, hierarchy synchronization, and retained preview state.
The text-mode UI test covers decoded string editing, exact no-op preservation, mode conversion, native undo, encoded-length errors, and named property resets.
It also covers native width/height fields, dimension drafts, invalid conversion, size resets, focus, and stale-source rejection.
Boolean checks cover native toggles in both visual styles, raw drafts, exact no-ops, compilation, undo/redo, reset, and stale-source rejection.
The selection smoke checks exact native preview sizes after a dimension edit and its source undo.
It also checks the actual native enabled state after a boolean edit, and restoration through source undo.
Inset checks cover uniform and four-sided values, draft conversion, numeric limits, native fields, focus, reset, stale source, and undo in both styles.
Draft reversion checks cover raw and structured modes, invalid fields, retained filters, unset properties, expression refusals, busy/stale guards, and unchanged source undo.
The application selection smoke checks its command-palette entry, disabled expression actions, and unchanged preview dimensions.
The source suite checks exact formatting, cultures, rejected syntax, source limits, and compiled inset changes across control kinds.
The application selection smoke checks the actual native label height after a padding edit and source undo.
Color codec checks cover RGB24 literals, exact no-ops, whitespace, rejected expressions, source limits, and compilation across color properties.
Native color checks cover channels, invalid-input recovery, opaque alpha, Escape/cancel, draft updates, stale source/selection/drafts, disposal, and source undo in both styles.
The application selection smoke checks palette dispatch, unchanged preview ownership before Apply, and native foreground values after Apply and undo.
Comment checks cover palette and shortcut routing, native preview removal/restoration, and separate undo operations through the complete source pipeline.

The builder smoke covers hierarchy selection, literal edits, palette insertion, structure commands, native undo and redo, and stale-edit rejection.
Palette checks cover name and description search, empty results, retained selection, filtered insertion, and source undo.
It also covers read-only expressions and recovery from invalid source without replacing the native document.
The file smoke uses an isolated recovery directory and the complete application.
It covers automatic drafts, the native recovery picker, disk conflicts, persistent file errors, and native edits after an invalid file opens.
It also covers real native Open and Save As results, cancellation, file shortcuts, explicit discard approval, and stale chooser results.
The layout smoke uses the production `.xui` layouts without the runtime compiler or preview host.
It covers pane bounds, pane order, native selection, and source preservation across theme changes.
The workspace suite runs the same builder smoke against production controllers without the preview host.
It also covers hierarchy search in both visual styles, native queries, match navigation, collapsed ancestors, revision guards, invalid source, and undo preservation.
Property search checks cover native queries, authored-only results, stable choice keys, retained raw and structured drafts, expressions, source revisions, and undo.
Property source checks cover exact UTF-16 and multiline selections, literals, expressions, references, event-handler names, retained drafts, refusals, and native undo.
They also edit a revealed expression and compile a subsequent property change in both styles.
The application selection smoke checks property-search focus, native query bounds, and unchanged preview geometry while a draft remains active.
It also checks property-source command availability, exact source selection, and retained native preview identity.
Sibling insertion checks cover before/after buttons, filtered templates, nested parents, Grid cells, structural refusals, native undo/redo, and cancellation by source typing.
Structural availability checks compare shared capabilities with native buttons for variable-child parents, fixed-child parents, roots, and movement boundaries.
They also cover busy/stale states, Grid duplication, and native undo in both styles.
Relative selection workspace checks cover nested parents, collapsed ancestors, boundaries, UTF-16 ranges, focus, filters, and one notification per selection.
They check exact-snapshot mismatches, pending edits, invalid source, disposal, recovery, and retained source undo in both styles.
Hierarchy expansion checks cover native descendant rows, retained nested expansion, partial cancellation, selection/source invalidation, disposal, property drafts, and undo in both styles.
The application selection smoke checks expansion/collapse commands, disabled leaf actions, and retained source selection and preview state.
Empty-cell checks cover native Grid coordinates, nested target selection, read-only discovery, explicit refusals, busy/stale guards, and insertion undo in both styles.
The source suite covers occupancy combinations, spans, large track arrays, unknown tracks, invalid placement, revisions, cancellation, and compiled insertion.
It compiles source transformations but does not execute authored preview code.
The application selection smoke checks sibling insertion against native preview text, arranged order, and restoration through source undo.
It also inserts a native preview control into a discovered Grid cell and checks separate undo operations.
The discard UI test covers native cancel and undo preservation, deferred approval, and rejection of stale source or revision snapshots.

The following opt-in diagnostic currently fails for programmatic owner closure during a native chooser in the complete designer:

```powershell
dotnet run --project bindings\dotnet\Designer -c Release -r $rid -- --file-close-smoke
```

It retains separate startup and cancellation deadlines, native HWND teardown checks, and rejection of late UI callbacks.
Its fallback Cancel action ends the failed fixture. That action does not count as successful programmatic cancellation.
The activation test checks the separate public window contract for foreground activation and initial keyboard focus.
`XUI_DESKTOP_TESTS=ON` also registers the activation test with CTest.

### C# file explorer

The explorer uses the same automatic DLL copy as the other samples:

```powershell
dotnet build bindings\dotnet\FileExplorer -c Release -r $rid
& ".\bindings\dotnet\FileExplorer\bin\Release\net10.0\$rid\FileExplorer.exe" "D:\Documents"
```

If the explorer closes unexpectedly, capture its error output and process exit code:

```powershell
$exe = (Resolve-Path "bindings\dotnet\FileExplorer\bin\Release\net10.0\$rid\FileExplorer.exe").Path
$errorLog = Join-Path $env:TEMP "xui-file-explorer-error.log"
$process = Start-Process -FilePath $exe -ArgumentList "`"$PWD`"" -PassThru -Wait -RedirectStandardError $errorLog
Get-Content $errorLog
$process.ExitCode
```

PowerShell can return immediately after it starts a GUI executable.
`$LASTEXITCODE` alone does not prove that the explorer finished successfully.
The command above waits until the explorer closes and preserves managed error details.

For markup changes, use restart-on-save:

```powershell
dotnet watch --project bindings\dotnet\FileExplorer --no-hot-reload --non-interactive "-p:RuntimeIdentifier=$rid"
```

The explorer disables in-place reload because its controllers own asynchronous work and native event subscriptions.
A restart resets transient pane state. Bookmarks and recents retain their normal persistence behavior.
The DLL must include the visual-style API in `xui_layout.h`.
File clipboard and drag-and-drop commands also require the file-transfer APIs from this checkout.
The New tab buttons require the tab-action APIs from this checkout.
An older `xui.dll` does not provide these APIs.

### NativeAOT and deployment

```powershell
dotnet publish bindings\dotnet\DeclarativeSample -c Release -r $rid -p:PublishAot=true
```

NativeAOT requires the matching Visual Studio native tools.
Publish restores the matching compiler packages.
Release output excludes the compiler and development reload host.

Static C++ applications need only their executable and Windows system components.
C ABI, Rust, and NativeAOT applications also need `xui.dll` beside the executable.
Framework-dependent C# applications need their managed files and the matching .NET runtime.
PDB files are optional diagnostics.

## Rust

The repository's Cargo configuration defaults to ARM64 and the static CRT.
For x64, explicitly select `x86_64-pc-windows-msvc`.
Use a Visual Studio developer shell for the target architecture.

```powershell
$target = if ($arch -eq "ARM64") { "aarch64-pc-windows-msvc" } else { "x86_64-pc-windows-msvc" }
$env:XUI_LIB_DIR = (Resolve-Path "$build\Release").Path
Push-Location bindings\rust
cargo build --workspace --release --target $target
Copy-Item "$env:XUI_LIB_DIR\xui.dll" ".\target\$target\release\deps\"
cargo test --workspace --release --target $target
cargo clippy --workspace --all-targets --release --target $target -- -D warnings
cargo fmt --all --check
Copy-Item "$env:XUI_LIB_DIR\xui.dll" ".\target\$target\release\"
& ".\target\$target\release\xui-sample.exe"
Pop-Location
```

`XUI_LIB_DIR` selects the directory with `xui.lib`.
The [binding reference](docs/specs/bindings.md) describes ownership and callback errors.

## Release packages

The [package guide](docs/specs/packages.md) describes consumption and deployment.
Release builds require both x64 and ARM64 C++ tools and Rust targets.
Each GitHub runner builds its own architecture.
The Designer requires a .NET SDK that matches its target architecture because it bundles the SDK's Roslyn assemblies.
For local builds, select the matching SDK through `PATH` before each architecture command:

```powershell
.\scripts\Build-Release.ps1 -Version 0.1.0 -Architecture x64 -StageDirectory build\release-stage
.\scripts\Build-Release.ps1 -Version 0.1.0 -Architecture ARM64 -StageDirectory build\release-stage
.\scripts\New-ReleaseAssets.ps1 -Version 0.1.0 -StageDirectory build\release-stage -OutputDirectory build\release-assets
.\tests\packages.ps1 -Version 0.1.0 -AssetDirectory build\release-assets -Architecture $arch
.\tests\release-samples.ps1 -Version 0.1.0 -AssetDirectory build\release-assets
.\tests\release-designer.ps1 -Version 0.1.0 -AssetDirectory build\release-assets -Architecture $arch
.\tests\release-workflow.ps1
.\tests\release-packaging-unit.ps1
.\tests\native-copy.ps1 -Architecture $arch
```

Use a fresh staging directory for each release build.
The scripts preserve existing staging directories and stop instead of mixing old and new outputs.
The scripts find CMake and Visual Studio tools automatically.
Static package libraries disable link-time optimization to avoid compiler-version coupling from `/GL` objects.
NuGet and Cargo versions come from the supplied version. Package scripts do not edit tracked version files.
Compiler-only fixtures can set `XuiCopyNativeRuntime=false`.
Application builds must not use that escape hatch.

The workflow runs for tag pushes under `release/`.
It accepts only `release/Major.minor.rev`, with three numeric components and no leading zeroes.
It builds both architectures, the release samples, the Designer, the NuGet package, and both Cargo crates.
The sample assets are `Xui.Samples.<version>.win-x64.zip` and `Xui.Samples.<version>.win-arm64.zip`.
Each archive contains native dependencies and size-optimized NativeAOT deployments without .NET debug symbols.
No separate .NET installation is necessary.
TaskCard remains available as tutorial source but does not ship in these archives.
`IsXuiReleaseSample=false` excludes a project from releases without excluding it from local native-copy checks.

The Designer assets are `Xui.Designer.<version>.win-x64.zip` and `Xui.Designer.<version>.win-arm64.zip`.
The Designer stays outside the NativeAOT sample inventory.
`scripts\Build-DesignerRelease.ps1` publishes self-contained, untrimmed, multi-file output with the runtime compiler and no debug symbols.
`Build-Release.ps1` stages that output under `designer\<rid>`, separately from `samples\<rid>`.
The Designer archives include the .NET runtime, licenses, notices, and file manifests.
The release workflow runs the extracted `Designer.exe --smoke` on each architecture before it creates the draft.
That check uses the bundled runtime and compiler without an SDK or XUI entry in `PATH`.

The workflow creates a draft release and attaches the assets and SHA-256 checksums.
It does not publish to NuGet.org or crates.io.
It refuses to replace assets on an already published GitHub release.

The script uploads one asset at a time.
It retries a failed upload up to four times, with delays of 5, 10, 20, and 40 seconds.
Each retry replaces any partial asset with the same name.
Successful uploads do not repeat during these retries.
If all five attempts fail, the script stops with the asset path and exit code.
A later run can replace the assets on the existing draft.

To request a release, push a tag from the intended commit:

```powershell
git tag release/0.1.0
git push origin release/0.1.0
```

Before publication, review the draft assets and generated notes.
XUI uses the root MIT license.
The NuGet package, both Cargo crates, and both sample ZIPs include that license.

## Tests

Run the registered native tests after a build:

```powershell
ctest --test-dir $build -C Release --output-on-failure
```

Some registered tests open Windows controls even without the desktop option.
`XUI_DESKTOP_TESTS` adds the interactive smoke, presentation, image, and lifecycle regressions.
For those additional tests, configure and rebuild:

```powershell
cmake -S . -B $build -DXUI_DESKTOP_TESTS=ON
cmake --build $build --config Release --parallel 4
ctest --test-dir $build -C Release --output-on-failure
```

Run desktop tests sequentially on an interactive desktop.
Focus-dependent tests require foreground ownership of their own windows.
Do not remove those safeguards to force a passing result.
Do not run builds, desktop tests, and measurements concurrently.

For a focused native change, select the relevant tests:

```powershell
ctest --test-dir $build -C Release -R "xui_(explorer|split_window)" --output-on-failure
ctest --test-dir $build -C Release -R "xui_abi" --output-on-failure
ctest --test-dir $build -C Release -R "xui_winui" --output-on-failure
ctest --test-dir $build -C Release -R "xui_miller" --output-on-failure
```

Compiler and model checks do not need a native window:

```powershell
dotnet run --project bindings\dotnet\GeneratorTests -c Release
.\bindings\dotnet\GeneratorTests\BuildTests.ps1
dotnet run --project bindings\dotnet\FileExplorer.Tests -c Release
dotnet run --project bindings\dotnet\Minesweeper.Tests -c Release
```

For declarative UI integration, build the probe and use matching architecture arguments:

```powershell
cmake --build $build --config Release --target xui xui_language_probe
.\tests\xui-language.ps1 -NativeDirectory "$build\Release" -RuntimeIdentifier $rid
.\tests\minesweeper.ps1 -NativeDirectory "$build\Release" -RuntimeIdentifier $rid
$exe = (Resolve-Path "bindings\dotnet\FileExplorer\bin\Release\net10.0\$rid\FileExplorer.exe").Path
$process = Start-Process -FilePath $exe -ArgumentList "--smoke" -PassThru -Wait
if ($process.ExitCode -ne 0) { throw "Explorer smoke failed with exit code $($process.ExitCode)." }
```

Build the C# explorer before its smoke run.
These scripts use isolated fixtures. The explorer smoke does not write the normal state file.
The [test reference](docs/llm/testing.md) describes coverage and measurement protocols.
Physical IME, mixed-monitor transitions, and screen-reader speech still require manual coverage.

### Tab tear-out and merge

Run the native gesture fixture and the Explorer model checks:

```powershell
cmake --build $build --config Release --target xui xui_tab_drag_window_tests xui_tab_window_tests
& ".\$build\Release\xui_tab_drag_window_tests.exe"
& ".\$build\Release\xui_tab_window_tests.exe" --drag-indicator
dotnet run --project bindings\dotnet\FileExplorer.Tests -c Release
```

The native fixture uses a deterministic driver at the caption-down boundary.
It does not synthesize pointer input or move the real cursor.
If another window covers the target, the fixture checks occlusion rejection instead of target acceptance.
Its output reports that condition.
Dedicated hover cases use temporary topmost fixture windows without activation.
They check native hide and show transitions, reversible transfer, transparent overlays, and remainder Z-order.
The Explorer smoke checks model transfer through the managed drag handler.

For physical drag coverage, press Ctrl+N in FileExplorer to create another window in the same application.
Drag tabs within a strip, outside the window, and onto the other window.
Before release, check that the target contains the dragged tab and the detached window is hidden.
Without release, drag away from the target and then onto it again.
Check that the detached window appears above the previous target.
Check that the remainder never appears above the detached window.
Repeat with the secondary pane, a single tab, a full target pane, and a maximized source.
During a detached drag, press Escape.
Check the folder history, Find text, selection, scroll position, and Columns state after each transfer.
Repeat across monitors with different DPI values.
The target marker must disappear after release, cancellation, or target closure.

### Toggles and progress

Run the model, native animation, pixel, and gallery checks sequentially:

```powershell
cmake --build $build --config Release --target xui xui_foundation_tests xui_foundation_window_tests xui_styling_window_tests xui_abi_features_tests xui_gallery xui_gallery_smoke
ctest --test-dir $build -C Release -R '^xui_(foundation_tests|foundation_window_tests|switch_ring_pixels)$' --output-on-failure
& ".\$build\Release\xui_gallery_smoke.exe" ".\$build\Release\xui_gallery.exe" --controls-only
& ".\$build\Release\xui_abi_features_tests.exe" --toggle-controls
dotnet run --project bindings\dotnet\Tests -c Release -r $rid -- --toggle-controls
dotnet run --project bindings\dotnet\GeneratorTests -c Release
dotnet run --project bindings\dotnet\Designer.SourceTests -c Release
```

The pixel check covers switch geometry and circular progress in Classic, WinUI, and high contrast.
The window check covers keyboard input, UIA, animation, hidden controls, disabled ancestors, detached content, and minimized windows.
It respects the Windows animation preference without changing that preference.
The gallery check covers the `toggle-switch`, `toggle-button`, `progress-ring`, and `progress` pages.
With `XUI_DESKTOP_TESTS=ON`, CTest also registers this gallery check as `xui_winui_controls_gallery_smoke`.

### Choices, links, badges, and menus

Run the focused native and gallery checks sequentially:

```powershell
cmake --build $build --config Release --target xui xui_next_controls_tests xui_menu_bar_tests xui_next_controls_window_tests xui_menu_bar_window_tests xui_styling_window_tests xui_abi_features_tests xui_gallery xui_gallery_smoke xui_gallery_catalog_tests
ctest --test-dir $build -C Release -R '^xui_(next_controls_tests|menu_bar_tests|next_controls_window_tests|menu_bar_window_tests|next_controls_pixels)$' --output-on-failure
& ".\$build\Release\xui_gallery_catalog_tests.exe"
& ".\$build\Release\xui_gallery_smoke.exe" ".\$build\Release\xui_gallery.exe" --parity-only
& ".\$build\Release\xui_gallery_smoke.exe" ".\$build\Release\xui_gallery.exe" --parity-classic
& ".\$build\Release\xui_abi_features_tests.exe" --parity-controls
dotnet run --project bindings\dotnet\Tests -c Release -r $rid -- --parity-controls
$env:PATH = (Resolve-Path "$build\Release").Path + ";" + $env:PATH
cargo test --manifest-path bindings\rust\Cargo.toml -p xui parity_controls -- --test-threads=1
```

The gallery checks cover CheckBox, HyperlinkButton, SelectorBar, InfoBadge, and MenuBar in WinUI and Classic styles.
They cover mixed checkbox state, disabled input, link callbacks, exclusive selection, badge updates, and menu commands.
The hyperlink example does not open a browser.
The menu example does not write files or change the clipboard.

### Independent windows

Run these desktop checks sequentially:

```powershell
cmake --build $build --config Release --target xui xui_multiwindow_tests xui_application_abi_tests
& ".\$build\Release\xui_multiwindow_tests.exe"
& ".\$build\Release\xui_application_abi_tests.exe"
dotnet run --project bindings\dotnet\Tests -c Release -r $rid -- --multiwindow
dotnet run --project bindings\dotnet\Tests -c Release -r $rid -- --window-icons
$env:PATH = (Resolve-Path "$build\Release").Path + ";" + $env:PATH
cargo test --manifest-path bindings\rust\Cargo.toml -p xui --test application -- --test-threads=1
```

The Rust integration executable embeds a common-controls v6 and per-monitor-DPI manifest.
The ABI check covers close callbacks, batched cancellation, retained errors, and worker posts concurrent with dispatcher destruction.
The managed check covers strong roots, deferred disposal, canceled posts, callback errors, and legacy sequential runs.
The Explorer smoke also closes Explorer before its text, image, and folder previews.
It checks real ownerless HWNDs, native text copying, image reuse, resize, and captured-target Open after opener disposal.
Smoke mode records Open targets instead of starting associated applications.
Manual coverage still includes cross-monitor DPI changes, taskbar grouping, physical IME, and screen-reader output.

### Content pointer inspection

Build and run the native inspection fixture and managed preview lifecycle suite:

```powershell
cmake --build $build --config Release --target xui xui_content_inspection_window_tests
ctest --test-dir $build -C Release -R '^xui_content_inspection_window_tests$' --output-on-failure
dotnet run --project bindings\dotnet\Designer.Preview.Tests -c Release -r $rid
```

The native fixture uses real HWND routes and registered retained nodes.
It covers clipping, editor state, explicit refusals, deferred delivery, and repeated replacement.
The managed suite adds ABI validation, source-version checks, observer retirement, and collectible assembly checks.
The [inspection contract](docs/specs/bindings.md#content-pointer-picking) lists unsupported surfaces.
Physical IME, touch/pen hardware, and screen-reader speech require manual checks.

### Non-occluding selection outlines

Build the outline fixture:

```powershell
cmake --build $build --config Release --target xui xui_content_highlight_window_tests
```

Run geometry checks without desktop access:

```powershell
ctest --test-dir $build -C Release -R '^xui_content_highlight_geometry_tests$' --output-on-failure
```

With an available desktop, run the renderer and managed lifecycle checks:

```powershell
ctest --test-dir $build -C Release -R '^xui_content_highlight_window_tests$' --output-on-failure
dotnet run --project bindings\dotnet\Designer.Preview.Tests -c Release -r $rid
```

Do not run concurrent focus-sensitive desktop fixtures.
The native fixture checks original-perimeter pixels, native occlusion refusal, unchanged regions and input, and repeated retirement.
The [outline contract](docs/specs/bindings.md#non-occluding-content-outlines) defines the conservative supported subset.

### Native file dialogs

Build and run the focused dialog checks:

```powershell
cmake --build $build --config Release --target xui_file_dialog_tests xui_file_dialog_native_tests xui_file_dialog_window_tests xui_file_dialog_abi_tests
ctest --test-dir $build -C Release -R "^xui_file_dialog_.*tests$" --output-on-failure
dotnet run --project bindings\dotnet\Tests -c Release -r $rid -- --file-dialogs
```

These desktop fixtures open real owned Windows Shell dialogs and cancel them through bounded, owner-specific probes.
They cover Open and Save paths, Unicode, default extensions, invalid options, thread access, callback failures, and content-scope guards.
The native window fixture covers focus return, composition rejection, owner closure, and owner deletion during the modal loop.
The Save fixtures select unique absent destinations and assert that no file was created.
Each native window fixture uses `demo\xui.rc` and `/MANIFEST:NO`. The managed fixture uses the matching apphost manifest.
The probes use a local temporary directory to avoid unrelated Shell startup delays from remembered locations.

### Stack preferred sizing

Run the core and native Stack layout regressions:

```powershell
cmake --build $build --config Release --target xui_core_tests xui_stack_layout_window_tests
ctest --test-dir $build -C Release -R "^xui_(core_tests|stack_layout_window_tests)$" --output-on-failure
```

The core fixture covers both axes, nested flex allocation, natural sizing, explicit preferences, automatic overrides, padding, and size limits.
The native fixture uses the matching manifest and real RichEdit peers.
It checks nonzero source geometry, retained focus and HWND identity, preference changes, and native undo and redo after layout.

### Transparent label backgrounds

With `XUI_DESKTOP_TESTS=ON`, run the label background regression:

```powershell
cmake --build $build --config Release --target xui_style_layouts_window_tests
ctest --test-dir $build -C Release -R "^xui_label_background_window_tests$" --output-on-failure
```

The fixture captures only its owned window.
It checks transparent labels and TextInput captions in both themes and visual styles.
It also checks explicit fills, border-only styles, style removal, caption typography, native selection, and the editor's accessible name.

### Document range editing

Build and run the focused document checks:

```powershell
cmake --build $build --config Release --target xui xui_document_editing_tests xui_document_editing_window_tests xui_document_editing_abi_tests xui_documents_tests
ctest --test-dir $build -C Release -R '^xui_(document_editing.*|documents_tests)$' --output-on-failure
dotnet run --project bindings\dotnet\Tests -c Release -r $rid -- --document-editing
```

The native fixture uses real RichEdit controls and the common-controls v6 manifest.
It covers exact native text, selection, undo, redo, callback counts, rejected edits, owner deletion, and the maximum document length.
Its composition checks use native composition messages. Physical IME interaction remains a manual check.
The ABI fixture covers strict UTF-8 spans, explicit errors, thread affinity, and unchanged failure outputs.
The C# fixture also covers the native `TreeView.Select` action.

### Control styling

Build and run the focused style checks:

```powershell
cmake --build $build --config Release --target xui xui_styling_tests xui_styling_window_tests xui_abi_features_tests
ctest --test-dir $build -C Release -R '^xui_(styling_tests|styling_window_tests|abi_features_tests)$' --output-on-failure
dotnet run --project bindings\dotnet\Tests -c Release -r $rid -- --styling
dotnet run --project bindings\dotnet\GeneratorTests -c Release
.\bindings\dotnet\GeneratorTests\BuildTests.ps1
```

The presentation test requires `XUI_DESKTOP_TESTS=ON` for CTest registration.
It checks actual Direct2D pixels, native editor identity, resource retention, and idle paints.

For the Toggle pilot, run these additional focused checks:

```powershell
cmake --build $build --config Release --target xui xui_control_tests xui_control_styling_tests xui_styling_window_tests xui_abi_c_test xui_abi_features_tests
ctest --test-dir $build -C Release -R '^xui_(control_tests|control_styling_tests|abi_c_test|abi_features_tests)$' --output-on-failure
& ".\$build\Release\xui_styling_window_tests.exe" --toggle-only
cargo test --manifest-path bindings\rust\Cargo.toml -p xui control_styling::tests -- --test-threads=1
```

The Rust test binary requires the matching native library and import library.
The Toggle presentation checks use native input, UIA, and owned-window rendering.
The `--toggle-only` option excludes the historical Button benchmarks and lifetime cycles.
Managed compiler and definition checks do not require an updated native library:

```powershell
dotnet run --project bindings\dotnet\GeneratorTests -c Release
dotnet run --project bindings\dotnet\Tests -c Release -- --styling-definitions
cargo check --manifest-path bindings\rust\Cargo.toml --workspace --tests
```

Use `xui_styling_window_tests.exe --trace-resources` to investigate transient USER-object failures.
Do not increase resource limits to hide an unexplained failure.

After all builds and other desktop tests finish, run the separate styled workload:

```powershell
& ".\$build\Release\xui_styling_window_tests.exe" --benchmark
```

The benchmark reports styled and unstyled CPU time, latency, and resource counts.
It does not replace a pristine baseline comparison.
Record repeated samples, medians, absolute deltas, and interference.
Do not report noisy results as performance acceptance.

`tests\button_style_probe.cpp` also compiles against the pre-style headers.
Use the same ARM64 or x64 Release library and compiler flags for both comparison executables.
For an ARM64 developer shell:

```powershell
cl /nologo /std:c++20 /O2 /EHsc /MT /Iinclude tests\button_style_probe.cpp `
    /Fo"$build\button-probe.obj" /Fe"$build\button-probe.exe" "$build\Release\xui_core.lib" psapi.lib /link /LTCG
& ".\$build\button-probe.exe"
```

Preserve the pristine executable before rebuilding.
Alternate pristine and changed runs without simultaneous builds or desktop tests.
The [styling evidence](docs/llm/control-styling.md) describes the initial samples and their limitations.

For a complete paired comparison, prepare both versions before reserving a quiet interval:

```powershell
.\tests\measure-button-styling.ps1 `
    -BaselineDirectory "$build\baseline-build\Release" `
    -ChangedDirectory "$build\Release" `
    -BaselineProbe "$build\button-baseline.exe" `
    -ChangedProbe "$build\button-final.exe" `
    -BaselineCollections "$build\baseline\xui_performance_tests.exe" `
    -OutputDirectory "$build\counterbalanced-comparison" -WindowPairs 6
```

The script performs no native builds or dependency restoration.
It alternates execution order, requires foreground ownership, and records detected interference.
It reports incomplete batches explicitly.
Its eight-minute deadline prevents another phase from starting after the agreed interval.
Individual test executables retain their own timeout guards.

### Binding generation and compatibility

After a feature-header or manifest change, regenerate the declarations:

```powershell
python bindings\generate_features.py
```

Normal `.xui` builds and managed style construction use checked-in catalogs.
They do not load the native DLL to discover schemas.
After a native schema change, build the exporter in the selected native build directory:

```powershell
cmake --build $build --config Release --target xui_style_catalog
python bindings\generate_control_styles.py --native-executable "$build\Release\xui_style_catalog.exe"
python bindings\generate_control_styles.py --native-executable "$build\Release\xui_style_catalog.exe" --check
dotnet run --project bindings\dotnet\GeneratorTests -c Release
```

The executable exports exact schemas and limits through the public C ABI.
Python does not load the target DLL into its own process.
Thus, an ARM64 exporter does not require an ARM64 Python installation.
The exporter also compiles exhaustive C/C++ identifier assertions.

The portable generator tests compare the snapshot against managed and compiler catalogs.
They exercise real managed definition validation with a native-load guard.
If Python is available, CTest registers the read-only `xui_control_style_catalog_parity` check.
That check compares the current DLL snapshot and all generated catalog files.

To regenerate from the checked-in snapshot without native execution, run:

```powershell
python bindings\generate_control_styles.py
python bindings\generate_control_styles.py --check
```

The ARM64 integration scripts currently assume Visual Studio 2022 Preview at its standard installation path.
`binding-features.ps1` also publishes with `--no-restore`.
Before its first run, restore both managed projects for the required publish modes:

```powershell
foreach ($project in @("Tests", "Sample")) {
    dotnet restore "bindings\dotnet\$project" -r win-arm64 -p:PublishAot=true
}
.\tests\binding-features.ps1 -BuildDirectory $build
```

Use this script with an ARM64 native build and the installed ARM64 Rust toolchain.
It stores logs and deployment copies in the selected build's `bindings-validation` directory.
The `Sample-fdd`, `Sample-aot`, and `rust` subdirectories contain runnable clients with `--features` support.

The historical baseline script uses `build\arm64`:

```powershell
.\tests\phase4.ps1 -SkipMeasurements
```

That script requires a build configured with `XUI_DESKTOP_TESTS=ON`.
It covers native tests, C# tests, Rust tests, and the original UIA clients.
Use the individual build commands for other toolchain locations or architectures.

### Measurements and captures

```powershell
& ".\$build\Release\xui_performance_tests.exe" --benchmark
& ".\$build\Release\xui_menu_tests.exe" "$build\menu-captures"
& ".\$build\Release\xui_image_tests.exe" "$build\image-fixtures" --fixtures
```

The fixture command creates original images, including deliberately malformed files.
Use a dedicated output directory.
The repository also includes `measure-browser.ps1`, `measure-task-manager.ps1`, `measure-window-memory.ps1`, and `measure-resize-memory.ps1` under `tests`.
Their defaults refer to historical build locations.
Inspect their parameters before selecting another executable or output directory.
The [memory report](docs/specs/windows-gui-memory.md) defines the counters and reproduction methods.
Historical results are not performance guarantees.

## Optional WebView2

The default build has no WebView2 dependency.
The optional host requires the pinned SDK and an installed Microsoft Edge WebView2 runtime.
XUI does not install that runtime.

Restore the SDK version from `integrations\webview2\packages.config`:

```powershell
$version = '1.0.2903.40'
New-Item -ItemType Directory -Force "$build\packages" | Out-Null
Invoke-WebRequest "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$version" -OutFile "$build\packages\webview2.zip"
Expand-Archive "$build\packages\webview2.zip" "$build\packages\Microsoft.Web.WebView2.$version" -Force
$sdk = (Resolve-Path "$build\packages\Microsoft.Web.WebView2.$version").Path
cmake -S . -B $build -DXUI_ENABLE_WEBVIEW2=ON "-DXUI_WEBVIEW2_SDK_DIR=$sdk"
cmake --build $build --config Release --target xui_gallery xui_hosts_window_tests
ctest --test-dir $build -C Release -R '^xui_hosts_window_tests$' --output-on-failure
```

The [native-host reference](docs/specs/scenes-and-hosts.md) describes explicit loads, allowed origins, and cleanup.

## VS Code syntax package

Use the [extension README](integrations/vscode-xui/README.md) for packaging, installation, and tokenizer commands.
The package supplies syntax support, not a language server or visual designer.

## Microsoft Edit LSH grammar

The standalone grammar is `integrations\edit-lsh\xui.lsh`.
The [language guide](docs/specs/xui-language.md#microsoft-edit-syntax-support) describes its highlighting limits.
The regression suite requires Rust 1.93 or later.
Cargo downloads the LSH compiler and runtime from a pinned Microsoft Edit commit.
No native XUI build is required.

From the XUI repository root, run:

```powershell
cargo test --locked --manifest-path integrations\edit-lsh\Cargo.toml
```

The suite checks token colors, filename detection, multiline state, UTF-8 span boundaries, repository samples, and compatibility with the built-in Edit definitions.
The pinned revision is `826b4c097b6f14ba0a846dc56f2f0223a3aaf73a`.
Both dependencies in `Cargo.toml` must use the same revision.

To use the grammar in Edit, start with a separate [Edit source checkout](https://github.com/microsoft/edit).
From the XUI repository root, set `$edit` to that checkout:

```powershell
$edit = "C:\src\edit"
$sample = (Resolve-Path bindings\dotnet\DeclarativeSample\Counter.xui).Path
Copy-Item integrations\edit-lsh\xui.lsh "$edit\crates\lsh\definitions\xui.lsh"
```

In the Edit checkout, run:

```powershell
Set-Location $edit
cargo run -p lsh-bin -- assembly crates\lsh\definitions
cargo run -p lsh-bin -- render --input $sample crates\lsh\definitions
cargo build --release -p edit
```

Edit discovers the copied definition during its build.
An installed Edit binary does not load this source file at runtime.
The [Edit build documentation](https://github.com/microsoft/edit#building-from-source) lists platform requirements.

### LSH highlighting in XUI applications

The gallery, Designer, and FileExplorer use LSH when the native XUI build enables it.
The default build has no LSH dependency and keeps plain text.
Use the `Lsh` NuGet package, version `0.3.0`, for Windows x64 or ARM64.
This package supplies custom-grammar compilation through its native C API.
The samples do not need the package's managed wrapper.

Restore from a local package feed.
Set `$feed` to the directory containing `Lsh.0.3.0.nupkg`:

```powershell
$feed = "C:\packages"
dotnet restore integrations\lsh\Lsh.Package.csproj --source $feed --packages build\packages
$lsh = (Resolve-Path build\packages\lsh\0.3.0).Path
cmake -S . -B $build "-DXUI_LSH_PACKAGE_DIR=$lsh"
cmake --build $build --config Release --target xui xui_gallery xui_winui_gallery --parallel 4
dotnet build bindings\dotnet\Designer -c Release -r $rid
dotnet build bindings\dotnet\FileExplorer -c Release -r $rid
```

Use the `$build` and `$rid` values from [Build the native code](#build-the-native-code).
The default managed sample paths use `build\<architecture>\Release`.
For another build directory, pass `-p:XuiNativeDir=<native-output-directory>` to each managed command.
Set `XUI_LSH_PACKAGE_DIR` to an empty string to disable LSH.

CMake embeds the trusted XUI, C, C++, C#, and Rust grammars at build time.
It copies the matching `lsh_lib.dll` and `LSH-LICENSE.txt` beside the native binaries.
Managed sample builds and publishes copy those files with `xui.dll`.
Keep all three files together when deploying an LSH-enabled managed sample.
For a statically linked C++ application, keep the LSH DLL and license beside the executable.
An unknown file extension uses plain text.
Missing DLLs, incompatible APIs, grammar errors, and highlighting failures report errors.
No sample loads a grammar from the file being previewed.

Run the focused checks with LSH enabled:

```powershell
cmake --build $build --config Release --target xui_syntax_highlighting_tests xui_document_syntax_tests xui_document_syntax_window_tests xui_document_editing_window_tests xui_document_editing_abi_tests --parallel 4
ctest --test-dir $build -C Release -R '^xui_(syntax_highlighting|document_syntax|document_syntax_window|document_editing_window|document_editing_abi)_tests$' --output-on-failure
dotnet run --project bindings\dotnet\Syntax.Tests -c Release -r $rid
```

The native syntax test also supports an LSH-disabled build.
The managed syntax fixture requires LSH.
Native window checks need an interactive Windows desktop.

## Documentation and changes

### Retype preview and GitHub Pages

The handbook uses [Retype](https://retype.com/guides/getting-started/) for local preview and static HTML.
The Markdown also remains compatible with GitBook.
The documentation tools require Node.js 22 and Python 3.10 or later.
They do not require the native XUI build.

From the repository root, install the pinned documentation dependency:

```powershell
npm ci
```

On Windows ARM64, use this command instead:

```powershell
npm ci --cpu=x64
```

Retype 4.6.0 has no native Windows ARM64 package.
The helper runs its x64 executable through Windows 11 emulation.
Other platforms use the matching Retype package.

Start the local preview:

```powershell
npm run docs:dev
```

The server listens only on `127.0.0.1:5000` and opens the browser.
The helper watches the source pages and updates the generated input.
Retype refreshes the site after each change.
Edit the original Markdown, not the generated files under `build`.
Press Ctrl+C to stop the preview.

To select another port without opening a browser, run:

```powershell
npm run docs:dev -- --port 5001 --no-open
```

Run the source checks and create the static site:

```powershell
npm run docs:check
npm run docs:build
```

The build writes `build\retype-site`.
It checks rendered page coverage, sidebar order, local links, anchors, assets, and the `/xui/` URL prefix.
It also compares every rendered code block with its Markdown source.
Control examples must use the tab order `.xui`, `C#`, `Rust`, `C++`.
The source check rejects missing languages, malformed groups, and C++ examples outside their language tab.
Unexpected output files stop the build before artifact upload.
It does not publish the output.
The build uses Retype's public [GitHub Pages community key](https://retype.com/community/).
This key permits Pro features on the default `github.io` domain.
It is a public license key, not a repository credential.
An explicit `RETYPE_KEY` environment variable takes precedence.
A custom domain requires a separate license review.

#### Content and navigation

`docs/specs/SUMMARY.md` remains the single page list and ordering source.
`tools/docs_site.py` creates Retype input under `build\retype-input`.
It converts summary sections into folders and adds navigation metadata to generated copies.
The book contents page remains accessible but does not appear in the sidebar.
`retype.yml` defines the site URL, output, and branding.

Only pages in the summary, plus the contents page itself, enter the site.
The helper does not copy maintainer notes, application sources, native binaries, or sample build output.
Links to other repository files point to GitHub at the source checkout's exact commit.
These source links still require repository access while the repository is private.
Fenced examples remain unchanged.
Template processing is disabled so C++ initializer braces remain literal text.
The adapter tests are in `tests/test_docs_site.py`.
Page frontmatter requires an explicit adapter update rather than a silent metadata override.

Control guides use GitBook tab directives in their Markdown source:

````markdown
{% tabs %}
{% tab title=".xui" %}
Declarative example and any required C# construction.
{% endtab %}
{% tab title="C#" %}
Handwritten C# example.
{% endtab %}
{% tab title="Rust" %}
Rust example.
{% endtab %}
{% tab title="C++" %}
C++ example.
{% endtab %}
{% endtabs %}
````

The helper converts these directives to Retype tabs in generated input.
It leaves fenced code unchanged.
GitBook retains its native tab syntax, and both renderers show `.xui` first.
Put shared behavior notes outside the group.
For an unbound API, state the limitation in its tab instead of inventing a call.
For a bound control without a markup constructor, show C# creation and a `.xui` `Content(...)` component.

#### Enable public deployment

**The repository is private. GitHub Pages publication makes the selected handbook pages and their code examples public.**
The workflow does not publish until an administrator explicitly enables deployment.
A private repository also requires a GitHub plan that supports Pages.

After approval for public publication:

1. Merge the documentation branch into `main`.
2. Open the repository's **Settings > Pages**.
3. Select **GitHub Actions** as the build and deployment source.
4. Review the `github-pages` environment and restrict deployment to `main`.
5. Create the Actions repository variable `XUI_PAGES_PUBLIC` with the value `true`.
6. Run the **Documentation** workflow on `main`.

The site URL is `https://zadjii-msft.github.io/xui/`.
Later pushes to `main` build and deploy the site automatically.
Pull requests build the site but never deploy it.
The workflow uploads only `build\retype-site`, not the repository.
The build job has read-only repository access.
Only the deployment job receives Pages and identity-token permissions.
The workflow uses commit-pinned actions and a locked Retype version.

To stop future deployments, remove the `XUI_PAGES_PUBLIC` variable.
This action does not remove an already published site.
To remove the public site, unpublish it in **Settings > Pages**.

### GitBook documentation

The [XUI handbook](docs/specs/README.md) is the GitBook entry point.
`.gitbook.yaml` uses the repository root as its content root.
It selects `docs/specs/README.md` and `docs/specs/SUMMARY.md` as the first page and navigation file.
The wider content root keeps links to contributor procedures and sample sources within the repository.
The summary selects the public pages; maintainer notes are not sidebar chapters.

To host the book, connect this repository and the desired branch through GitBook Git Sync.
Keep the configuration and Markdown in Git.
GitBook hosting is separate from the Retype workflow.
The configuration follows GitBook's [content configuration reference](https://gitbook.com/docs/docs-as-code/git-sync/content-configuration).
Do not install the obsolete `gitbook-cli` package to process these Git Sync files.

When adding a public page, link it from its section index and `docs/specs/SUMMARY.md`.
Each page can appear only once in the summary.
Use relative Markdown links for repository pages.
Keep control coverage tied to current public headers and binding factories, not the names of style-target enum members.
Some enum members describe unsupported facade targets; a style schema does not imply a markup constructor.

Run the dependency-free documentation check from the repository root:

```powershell
python tests\check-docs.py
```

It checks navigation uniqueness, local links and heading anchors, section coverage, and control-catalog names against current source.
It does not publish the site or verify external URLs.
Compile the tutorial sample separately after changing its examples.

### GitBook tutorial sample

The [tutorials](docs/specs/tutorials/README.md) include a complete task-card application.
After the native build, use the same `$build` and `$rid` values from the earlier procedures:

```powershell
dotnet run --project docs\specs\tutorials\sample\TaskCard.csproj -r $rid
```

For development reload:

```powershell
dotnet watch --project docs\specs\tutorials\sample\TaskCard.csproj --non-interactive "-p:RuntimeIdentifier=$rid"
```

For a compile-only check, no native DLL is needed:

```powershell
dotnet build docs\specs\tutorials\sample\TaskCard.csproj -c Release -r $rid -p:XuiCopyNativeRuntime=false
dotnet build docs\specs\tutorials\sample\TaskCard.csproj -c Debug -r $rid -p:XuiCopyNativeRuntime=false
```

For NativeAOT, use the same project path in the [publish procedure](#nativeaot-and-deployment).
Build and publish copy this checkout's `xui.dll` into the output directory automatically.
The tutorial sample does not ship in the release sample ZIPs.
The sample stores task state only in memory.
Do not describe Apply as persistent storage or reload replacement as state preservation.

### Document scope

Keep each document focused on its reader:

- `README.md`: A brief introduction, small examples, and links for application authors.
- `CONTRIBUTING.md`: Build requirements, commands, tests, and contribution procedures.
- `docs/specs`: Public API contracts, language guides, sample behavior, and human-facing design proposals.
- `docs/llm`: Source maps, implementation plans, handoffs, historical results, and unresolved investigations.

When behavior changes, update the relevant public reference.
Add maintainer evidence separately, with its date, scope, and limitations.
Do not append test transcripts or implementation diaries to the README.
Preserve native editing, accessibility, ownership, and cancellation contracts in code changes.
The [maintainer index](docs/llm/README.md) identifies the relevant source maps and previous investigations.
