# Packages and deployment

XUI release assets support Windows x64 and ARM64.
The tag `release/1.2.3` produces package version `1.2.3`.
No extra build number is necessary.
GitHub releases remain drafts until a maintainer publishes them.
The workflow does not publish packages to a package registry.

XUI uses the [MIT license](../../LICENSE).
The NuGet package and both Cargo crates declare MIT in their package metadata.
Each package and each sample ZIP contain the root `LICENSE` file.
Third-party components retain their own license terms.

Each release includes one `Xui` NuGet package, `xui-sys` and `xui` Cargo crates, two sample ZIPs, and SHA-256 checksums.
The NuGet download contains both native architectures, headers, static libraries, the C ABI runtime, .NET bindings, and the `.xui` compiler.
Native C++ deployment does not include managed assemblies.
The compiler is a build-time dependency, not part of release application output.

## .NET applications

The package requires the .NET 10 SDK.
After download, place the `.nupkg` in a local NuGet source:

```powershell
dotnet nuget add source D:\packages\xui --name XuiLocal
dotnet add package Xui --version 1.2.3
```

A minimal application project uses:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <RuntimeIdentifier>win-x64</RuntimeIdentifier>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Xui" Version="1.2.3" />
  </ItemGroup>
</Project>
```

`win-arm64` selects ARM64 instead.
The SDK copies the matching native DLL into build and publish output.
No repository checkout or XUI entry in `PATH` is necessary.
The package processes `.xui` files and supplies the native-control manifest.
An explicit `ApplicationManifest` overrides that manifest.

Managed Debug builds include `Xui.Development` and define `XUI_HOT_RELOAD`.
`XuiHotReload=false` disables that integration.
Release and NativeAOT builds exclude the development host.
The [language guide](xui-language.md) describes component authoring and reload behavior.

## Native C++ applications

The native NuGet targets support Visual C++ projects with platform `x64` or `ARM64`.
The default `XuiNativeLinkage=Static` links `xui_windows.lib` and `xui_core.lib`.
It selects C++20, Unicode definitions, the application manifest, and the static MSVC runtime (`/MT`).
Only the application and its own dependencies belong in deployment.
Neither .NET nor `xui.dll` is necessary for this mode.

The package supplies Release static libraries only.
Debug applications can build XUI from source or use `XuiNativeLinkage=CAbi`.
The C ABI mode links `xui.lib` and copies `xui.dll` beside the executable.
It does not expose the C++ class API through that DLL.
The [binding reference](bindings.md) describes the C ABI contract.

For CMake, extract the NuGet archive and use its package configuration:

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyApp LANGUAGES C CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
find_package(Xui CONFIG REQUIRED)
add_executable(my_app WIN32 main.cpp)
target_link_libraries(my_app PRIVATE Xui::Windows)
target_compile_options(my_app PRIVATE /utf-8 /EHsc)
target_link_options(my_app PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${Xui_MANIFEST}")
```

`Xui_DIR` must identify the extracted `build\native` directory.
`Xui::Core`, `Xui::Windows`, and `Xui::CAbi` are imported targets.
The C++ static targets require Release-compatible MSVC runtime and iterator-debug settings.
For the C ABI target, copy `$<TARGET_FILE:Xui::CAbi>` beside the executable.

## Rust applications

`xui` supplies the safe wrapper. `xui-sys` supplies the C ABI declarations.
The crates use the same version, and the wrapper requires that exact `xui-sys` version.
Release `xui-sys` archives contain the native DLL and import library for both Windows MSVC targets.
An extracted crate selects its native files from Cargo's target triple.
`XUI_LIB_DIR` explicitly overrides that selection.
No native download occurs during a consumer build.

The release attaches crates but does not register them on crates.io.
For local consumption, extract both archives into a vendor directory:

```toml
[dependencies]
xui = { path = "vendor/xui-1.2.3" }

[patch.crates-io]
xui-sys = { path = "vendor/xui-sys-1.2.3" }
```

Both extracted crates retain the release version.
The application must deploy the selected `xui.dll` beside its executable.
Cargo does not provide a general transitive DLL-copy hook for arbitrary application outputs.
The release script copies the DLL for the repository's Rust sample.
The [binding guide](bindings.md#rust-ownership-and-use) describes the API and ownership rules.

## Source development

Repository .NET samples copy the native DLL from this checkout's `build\<architecture>\Release` directory.
The sample architecture defaults to the host Windows architecture.
`RuntimeIdentifier` selects `win-x64` or `win-arm64`.
`XuiNativeConfiguration` selects another native configuration, and `XuiNativeDir` overrides the complete directory.
A missing DLL causes a build error instead of a runtime loader failure.
Build and publish use the same native source.

The DLL remains a separate native build.
A managed rebuild copies its latest output but does not compile native sources.
The [contributor guide](../../CONTRIBUTING.md#c-and-declarative-samples) contains the commands.

## Sample archives

The release supplies `Xui.Samples.<version>.win-x64.zip` and `Xui.Samples.<version>.win-arm64.zip`.
Each ZIP contains only its named architecture, with `native`, `dotnet`, and `rust` directories at the archive root.
The .NET samples are `Sample`, `DeclarativeSample`, `FileExplorer`, and `Minesweeper`.
TaskCard remains a source tutorial but does not ship in the archives.

The .NET samples use size-optimized NativeAOT executables, so a separate .NET installation is unnecessary.
Each executable contains its required runtime code instead of a separate copy of the full .NET runtime.
The archives omit .NET debug symbols and development tools.
Each sample retains its own directory and native DLL, so it can run independently.
Each .NET sample also includes the .NET license and third-party notices from its resolved runtime and compiler packages.

The native and Rust builds use the static C runtime.
The package excludes optional WebView2 support.

Each application runs from its extracted directory without an XUI entry in `PATH`.
Windows system components remain prerequisites.
Each archive includes a versioned file manifest with SHA-256 hashes.
Release procedures are in [CONTRIBUTING](../../CONTRIBUTING.md#release-packages).
