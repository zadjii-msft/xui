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
The local commands can cross-compile both architectures:

```powershell
.\scripts\Build-Release.ps1 -Version 0.1.0 -Architecture x64 -StageDirectory build\release-stage
.\scripts\Build-Release.ps1 -Version 0.1.0 -Architecture ARM64 -StageDirectory build\release-stage
.\scripts\New-ReleaseAssets.ps1 -Version 0.1.0 -StageDirectory build\release-stage -OutputDirectory build\release-assets
.\tests\packages.ps1 -Version 0.1.0 -AssetDirectory build\release-assets -Architecture $arch
.\tests\release-samples.ps1 -Version 0.1.0 -AssetDirectory build\release-assets
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
It builds both architectures, the release samples, the NuGet package, and both Cargo crates.
The sample assets are `Xui.Samples.<version>.win-x64.zip` and `Xui.Samples.<version>.win-arm64.zip`.
Each archive contains native dependencies and size-optimized NativeAOT deployments without .NET debug symbols.
No separate .NET installation is necessary.
TaskCard remains available as tutorial source but does not ship in these archives.
`IsXuiReleaseSample=false` excludes a project from releases without excluding it from local native-copy checks.
The workflow creates a draft release and attaches the assets and SHA-256 checksums.
It does not publish to NuGet.org or crates.io.
It refuses to replace assets on an already published GitHub release.

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
