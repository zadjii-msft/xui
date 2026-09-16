# Exercise the real packaging script with small Rust fixtures and inert native files.
# This checks Cargo registry resolution and verification, not native linking or ABI.
# Run in a Visual Studio developer shell. No native XUI build is required.
[CmdletBinding()]
param(
    [ValidateSet('x86_64-pc-windows-msvc', 'aarch64-pc-windows-msvc')]
    [string]$Target = 'aarch64-pc-windows-msvc'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$work = Join-Path $root ("build\cargo-packaging-unit-" + [guid]::NewGuid().ToString('N'))
$fixture = Join-Path $work 'fixture'
$rid = if ($Target -eq 'x86_64-pc-windows-msvc') { 'win-x64' } else { 'win-arm64' }
New-Item -ItemType Directory -Path "$fixture\scripts", "$fixture\bindings\rust\.cargo",
    "$fixture\bindings\rust\xui-sys\src", "$fixture\bindings\rust\xui\src", "$fixture\native\$rid" -Force | Out-Null
function Write-Utf8([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Expect-Failure([scriptblock]$Action, [string]$MessagePattern) {
    try { & $Action } catch {
        if ($_.Exception.Message -notmatch $MessagePattern) { throw }
        return
    }
    throw "Expected failure matching $MessagePattern"
}
$script = Join-Path $fixture 'scripts\Pack-Cargo.ps1'
Copy-Item -LiteralPath "$root\scripts\Pack-Cargo.ps1" -Destination $script
Write-Utf8 "$fixture\bindings\rust\.cargo\config.toml" @"
[target.$Target]
rustflags = ["-C", "target-feature=+crt-static"]
"@
Write-Utf8 "$fixture\bindings\rust\xui-sys\Cargo.toml" @'
[package]
name = "xui-sys"
version = "0.1.0"
edition = "2024"
description = "Packaging test fixture"
repository = "https://github.com/zadjii-msft/xui"
links = "xui"
build = "build.rs"
include = ["Cargo.toml", "build.rs", "src/**", "native/**"]
'@
Write-Utf8 "$fixture\bindings\rust\xui\Cargo.toml" @'
[package]
name = "xui"
version = "0.1.0"
edition = "2024"
description = "Packaging test fixture"
repository = "https://github.com/zadjii-msft/xui"
[dependencies]
xui-sys = { version = "=0.1.0", path = "../xui-sys" }
'@
Copy-Item -LiteralPath "$root\bindings\rust\xui-sys\build.rs" -Destination "$fixture\bindings\rust\xui-sys\build.rs"
Write-Utf8 "$fixture\bindings\rust\xui-sys\src\lib.rs" 'pub fn value() -> u32 { 42 }'
Write-Utf8 "$fixture\bindings\rust\xui\src\lib.rs" 'pub fn value() -> u32 { xui_sys::value() }'
Write-Utf8 "$fixture\native\$rid\xui.dll" 'Inert packaging fixture, not a native runtime.'
Write-Utf8 "$fixture\native\$rid\xui.lib" 'Inert packaging fixture, not a native import library.'
$before = (Get-FileHash "$fixture\bindings\rust\xui\Cargo.toml").Hash
$arguments = @{
    Version = '12.34.56'
    NativeRoot = "$fixture\native"
    OutputDirectory = "$fixture\assets"
    WorkDirectory = "$fixture\build\package"
}

foreach ($invalid in @('1.2', '01.2.3', '1.2.3-rc.1', 'release/1.2.3', "1.2.3`n")) {
    Expect-Failure { & $script -Version $invalid -NativeRoot "$fixture\native" -OutputDirectory "$fixture\unused" } 'Version'
}
& $script @arguments
if ((Get-FileHash "$fixture\bindings\rust\xui\Cargo.toml").Hash -ne $before) {
    throw 'Packaging changed the source manifest'
}
$archive = "$fixture\assets\xui-12.34.56.crate"
$extract = Join-Path $work 'extracted'
New-Item -ItemType Directory -Path $extract | Out-Null
& tar -xzf $archive -C $extract
if ($LASTEXITCODE -ne 0) { throw 'Cannot extract fixture archive' }
$normalized = Get-Content "$extract\xui-12.34.56\Cargo.toml" -Raw
$dependency = [regex]::Match($normalized, '(?ms)^\[dependencies\.xui-sys\]\r?\n(?<fields>.*?)(?=^\[|\z)').Groups['fields'].Value
if ($dependency -notmatch 'version = "=12\.34\.56"' -or $dependency -match '(?m)^path = ') {
    throw 'The packaged dependency must have an exact registry version and no checkout path'
}
$sysFiles = & tar -tzf "$fixture\assets\xui-sys-12.34.56.crate"
if ($LASTEXITCODE -ne 0) { throw 'Cannot list fixture archive' }
foreach ($asset in @('xui.dll', 'xui.lib')) {
    if ($sysFiles -notcontains "xui-sys-12.34.56/native/$Target/$asset") {
        throw "Missing bundled fixture asset: $asset"
    }
}
# Invalid library code only fails if cargo package actually verifies its archive.
Write-Utf8 "$fixture\bindings\rust\xui\src\lib.rs" 'pub fn broken() -> u32 { "not a number" }'
$arguments.Version = '12.34.57'
$arguments.WorkDirectory = "$fixture\build\invalid-rust"
Expect-Failure { & $script @arguments } 'cargo .* failed'
if (Test-Path "$fixture\assets\xui-12.34.57.crate") { throw 'Failed verification produced a release asset' }
Write-Host "Cargo packaging unit checks passed. Fixture evidence retained at $work"
