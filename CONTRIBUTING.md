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

## C# and declarative samples

After the native build, add its DLL directory to this shell's search path:

```powershell
$env:PATH = (Resolve-Path "$build\Release").Path + ";" + $env:PATH
dotnet run --project bindings\dotnet\DeclarativeSample -r $rid
```

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

The explorer copies the native DLL from `XuiNativeDir`:

```powershell
dotnet build bindings\dotnet\FileExplorer -c Release -r $rid "-p:XuiNativeDir=$PWD\$build\Release"
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
dotnet watch --project bindings\dotnet\FileExplorer --no-hot-reload --non-interactive "-p:RuntimeIdentifier=$rid" "-p:XuiNativeDir=$PWD\$build\Release"
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
Copy-Item "$build\Release\xui.dll" "bindings\dotnet\DeclarativeSample\bin\Release\net10.0\$rid\publish\"
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
$env:PATH = "$env:XUI_LIB_DIR;$env:PATH"
Push-Location bindings\rust
cargo build --workspace --release --target $target
cargo test --workspace --release --target $target
cargo clippy --workspace --all-targets --release --target $target -- -D warnings
cargo fmt --all --check
& ".\target\$target\release\xui-sample.exe"
Pop-Location
```

`XUI_LIB_DIR` selects the directory with `xui.lib`.
The [binding reference](docs/specs/bindings.md) describes ownership and callback errors.

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

### Binding generation and compatibility

After a feature-header or manifest change, regenerate the declarations:

```powershell
python bindings\generate_features.py
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
