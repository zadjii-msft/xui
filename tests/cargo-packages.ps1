# Build consumers from extracted release archives, not checkout crate paths.
# Run from a Visual Studio developer shell for Target, with its Rust target installed.
# The console smoke runs with xui.dll beside its executable, without changing PATH.
# The full sample is built and deployed but not launched (it opens an interactive UI).
# Example:
# .\tests\cargo-packages.ps1 -Version 1.2.3 -PackageDirectory build\release\assets `
#     -Target aarch64-pc-windows-msvc
# -Architecture ARM64 or x64 is equivalent to the corresponding -Target.
[CmdletBinding(DefaultParameterSetName = 'Target')]
param(
    [Parameter(Mandatory)][ValidatePattern('\A(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\z')][string]$Version,
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory, ParameterSetName = 'Target')][ValidateSet('x86_64-pc-windows-msvc', 'aarch64-pc-windows-msvc')][string]$Target,
    [Parameter(Mandatory, ParameterSetName = 'Architecture')][ValidateSet('x64', 'ARM64')][string]$Architecture,
    [string]$WorkDirectory
)

$ErrorActionPreference = 'Stop'
if ($PSCmdlet.ParameterSetName -eq 'Architecture') {
    $Target = if ($Architecture -eq 'ARM64') { 'aarch64-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
}
$root = Split-Path $PSScriptRoot -Parent
$packages = (Resolve-Path -LiteralPath $PackageDirectory).Path
if (!$WorkDirectory) {
    $WorkDirectory = Join-Path $root ("build\cargo-consumer-{0}-{1}" -f $Target, [guid]::NewGuid().ToString('N'))
}
$work = [IO.Path]::GetFullPath($WorkDirectory)
$buildPrefix = [IO.Path]::GetFullPath((Join-Path $root 'build')) + [IO.Path]::DirectorySeparatorChar
if (!$work.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "WorkDirectory must be an isolated directory under $buildPrefix"
}
if (Test-Path -LiteralPath $work) { throw "WorkDirectory must not exist: $work" }
foreach ($name in @('xui-sys', 'xui')) {
    if (!(Test-Path -LiteralPath "$packages\$name-$Version.crate" -PathType Leaf)) {
        throw "Missing archive: $packages\$name-$Version.crate"
    }
}
function Write-Utf8([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Invoke-Cargo([string[]]$CargoArguments) {
    & cargo +stable @CargoArguments
    if ($LASTEXITCODE -ne 0) { throw "cargo $($CargoArguments -join ' ') failed ($LASTEXITCODE)" }
}

$extracted = Join-Path $work 'extracted'
$consumer = Join-Path $work 'consumer'
$sampleRoot = Join-Path $work 'sample'
$sample = Join-Path $sampleRoot 'bindings\rust\sample'
$targetDirectory = Join-Path $work 'target'
New-Item -ItemType Directory -Path $extracted, "$consumer\src", $sample, "$sampleRoot\demo" -Force | Out-Null
foreach ($name in @('xui-sys', 'xui')) {
    & tar -xzf "$packages\$name-$Version.crate" -C $extracted
    if ($LASTEXITCODE -ne 0) { throw "Cannot extract $name-$Version.crate" }
    if ((Get-FileHash "$extracted\$name-$Version\LICENSE").Hash -ne (Get-FileHash "$root\LICENSE").Hash) {
        throw "The $name archive must contain the project license."
    }
    if ((Get-Content "$extracted\$name-$Version\Cargo.toml" -Raw) -notmatch '(?m)^license = "MIT"\r?$') {
        throw "The $name archive must declare the MIT license."
    }
}
$sysPath = Join-Path $extracted "xui-sys-$Version"
$xuiPath = Join-Path $extracted "xui-$Version"
$native = Join-Path $sysPath "native\$Target"
foreach ($name in @('xui.dll', 'xui.lib', 'xui_preview_host.exe')) {
    if (!(Test-Path -LiteralPath "$native\$name" -PathType Leaf)) {
        throw "The xui-sys archive is missing native\$Target\$name"
    }
}
$patch = @"
[patch.crates-io]
xui = { path = $(ConvertTo-Json -InputObject $xuiPath -Compress) }
xui-sys = { path = $(ConvertTo-Json -InputObject $sysPath -Compress) }
"@
Write-Utf8 "$consumer\Cargo.toml" @"
[package]
name = "xui-package-smoke"
version = "0.0.0"
edition = "2024"
publish = false
[workspace]
[dependencies]
xui = "=$Version"
xui-sys = "=$Version"
$patch
"@
Write-Utf8 "$consumer\src\main.rs" @'
fn main() {
    let result: xui::Result<()> = Ok(());
    result.unwrap();
    assert_eq!(unsafe { xui_sys::xui_abi_version() }, xui_sys::ABI_VERSION);
    println!("XUI extracted-package ABI smoke passed");
}
'@
Write-Utf8 "$consumer\build.rs" @'
fn main() {
    let directory = std::env::var("DEP_XUI_RUNTIME_DIR").expect("Missing runtime directory metadata");
    let dll = std::env::var("DEP_XUI_RUNTIME_DLL").expect("Missing runtime DLL metadata");
    assert_eq!(std::path::Path::new(&directory).join("xui.dll"), std::path::Path::new(&dll));
    assert!(std::path::Path::new(&dll).is_file());
}
'@
Copy-Item -LiteralPath "$root\bindings\rust\sample\src" -Destination "$sample\src" -Recurse
Copy-Item -LiteralPath "$root\bindings\rust\sample\build.rs" -Destination "$sample\build.rs"
Copy-Item -LiteralPath "$root\demo\xui.manifest" -Destination "$sampleRoot\demo\xui.manifest"
Write-Utf8 "$sample\Cargo.toml" @"
[package]
name = "xui-sample"
version = "0.0.0"
edition = "2024"
publish = false
[workspace]
[dependencies]
xui = "=$Version"
$patch
"@
$config = Join-Path $work 'config.toml'
Write-Utf8 $config @"
[target.$Target]
rustflags = ["-C", "target-feature=+crt-static"]
"@

$oldCargoHome = $env:CARGO_HOME
$oldLibDir = $env:XUI_LIB_DIR
try {
    $env:CARGO_HOME = Join-Path $work 'cargo-home'
    Remove-Item Env:XUI_LIB_DIR -ErrorAction SilentlyContinue
    Push-Location $work
    try {
        # Both dependency graphs must resolve offline using only the extracted archives.
        $common = @('--offline', '--release', '--target', $Target, '--target-dir', $targetDirectory, '--config', $config)
        Invoke-Cargo (@('build', '--manifest-path', "$consumer\Cargo.toml") + $common)
        Invoke-Cargo (@('build', '--manifest-path', "$sample\Cargo.toml") + $common)
        $binaryDirectory = Join-Path $targetDirectory "$Target\release"
        Copy-Item -LiteralPath "$native\xui.dll" -Destination "$binaryDirectory\xui.dll"
        Copy-Item -LiteralPath "$native\xui_preview_host.exe" -Destination "$binaryDirectory\xui_preview_host.exe"
        # Working elsewhere also prevents the current directory from supplying the DLL.
        & "$binaryDirectory\xui-package-smoke.exe"
        if ($LASTEXITCODE -ne 0) { throw "Extracted-package ABI smoke failed ($LASTEXITCODE)" }
        Write-Host "Runnable sample with adjacent DLL: $binaryDirectory\xui-sample.exe"
    } finally {
        Pop-Location
    }
} finally {
    $env:CARGO_HOME = $oldCargoHome
    $env:XUI_LIB_DIR = $oldLibDir
}
