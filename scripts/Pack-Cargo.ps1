# Package from release-tag version X.Y.Z without changing the checkout manifests.
# NativeRoot contains win-x64 and/or win-arm64 directories. Each needs xui.dll
# and xui.lib; the static xui_core.lib and xui_windows.lib are not Rust dependencies.
# Requires stable Cargo and the installed Rust standard library for each supplied target.
# Verification is enabled: xui-sys is packaged first, then a checksum-backed local
# registry lets Cargo package and verify xui offline before xui-sys is published.
# Consumers can extract both crates and use [patch.crates-io] paths, or use a registry.
# A build.rs with a direct xui-sys dependency receives DEP_XUI_RUNTIME_DIR and
# DEP_XUI_RUNTIME_DLL. Copy that DLL next to the final executable before running it.
# Cargo does not deploy runtime DLLs transitively. See tests\cargo-packages.ps1.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('\A(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\z')][string]$Version,
    [Parameter(Mandatory)][string]$NativeRoot,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$WorkDirectory
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$native = (Resolve-Path -LiteralPath $NativeRoot).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (!$WorkDirectory) {
    $WorkDirectory = Join-Path $root ("build\cargo-package-{0}-{1}" -f $Version, [guid]::NewGuid().ToString('N'))
}
$work = [IO.Path]::GetFullPath($WorkDirectory)
$buildPrefix = [IO.Path]::GetFullPath((Join-Path $root 'build')) + [IO.Path]::DirectorySeparatorChar
if (!$work.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "WorkDirectory must be an isolated directory under $buildPrefix"
}
if (Test-Path -LiteralPath $work) {
    throw "WorkDirectory must not exist: $work"
}
$targets = [ordered]@{
    'win-x64' = 'x86_64-pc-windows-msvc'
    'win-arm64' = 'aarch64-pc-windows-msvc'
}
$present = @($targets.Keys | Where-Object { Test-Path -LiteralPath (Join-Path $native $_) -PathType Container })
if (!$present.Count) { throw "NativeRoot must contain win-x64 or win-arm64: $native" }
foreach ($rid in $present) {
    foreach ($name in @('xui.dll', 'xui.lib')) {
        $file = Join-Path $native "$rid\$name"
        if (!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -eq 0) {
            throw "Missing or empty native asset: $file"
        }
    }
}
foreach ($name in @('xui-sys', 'xui')) {
    if (Test-Path -LiteralPath (Join-Path $output "$name-$Version.crate")) {
        throw "Refusing to overwrite an existing release asset: $name-$Version.crate"
    }
}

function Write-Utf8([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Invoke-Cargo([string[]]$CargoArguments) {
    & cargo +stable @CargoArguments
    if ($LASTEXITCODE -ne 0) { throw "cargo $($CargoArguments -join ' ') failed ($LASTEXITCODE)" }
}

New-Item -ItemType Directory -Path $work | Out-Null
$source = Join-Path $work 'source'
$registry = Join-Path $work 'registry'
$targetDirectory = Join-Path $work 'target'
New-Item -ItemType Directory -Path $source, "$source\.cargo", "$registry\index\xu\i-" -Force | Out-Null
Copy-Item -LiteralPath "$root\bindings\rust\.cargo\config.toml" -Destination "$source\.cargo\config.toml"
Write-Utf8 "$source\Cargo.toml" "[workspace]`nmembers = [`"xui-sys`", `"xui`"]`nresolver = `"3`"`n"
foreach ($name in @('xui-sys', 'xui')) {
    $destination = Join-Path $source $name
    New-Item -ItemType Directory -Path $destination | Out-Null
    Copy-Item -LiteralPath "$root\bindings\rust\$name\src" -Destination "$destination\src" -Recurse
    $manifest = Get-Content -LiteralPath "$root\bindings\rust\$name\Cargo.toml" -Raw
    $manifest = $manifest -replace '(?m)^version = "[^"]+"', "version = `"$Version`""
    if ($name -eq 'xui') {
        $manifest = $manifest -replace 'version = "=[^"]+"', "version = `"=$Version`""
    }
    Write-Utf8 "$destination\Cargo.toml" $manifest
}
Copy-Item -LiteralPath "$root\bindings\rust\xui-sys\build.rs" -Destination "$source\xui-sys\build.rs"
foreach ($rid in $present) {
    $destination = Join-Path $source "xui-sys\native\$($targets[$rid])"
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -LiteralPath "$native\$rid\xui.dll", "$native\$rid\xui.lib" -Destination $destination
}

# Source replacement keeps the published dependency on crates.io in the archive.
# A private Cargo cache also proves verification does not use previously cached crates.
$config = Join-Path $work 'registry.toml'
$registryToml = ConvertTo-Json -InputObject $registry -Compress
Write-Utf8 $config "[source.crates-io]`nreplace-with = `"release-local`"`n[source.release-local]`nlocal-registry = $registryToml`n"
$oldCargoHome = $env:CARGO_HOME
$oldLibDir = $env:XUI_LIB_DIR
$oldCargoIncremental = $env:CARGO_INCREMENTAL
try {
    $env:CARGO_HOME = Join-Path $work 'cargo-home'
    $env:CARGO_INCREMENTAL = '0'
    Remove-Item Env:XUI_LIB_DIR -ErrorAction SilentlyContinue
    Push-Location $source
    try {
        $common = @('--offline', '--allow-dirty', '--target-dir', $targetDirectory, '--config', $config)
        foreach ($rid in $present) {
            Invoke-Cargo (@('package', '-p', 'xui-sys', '--target', $targets[$rid]) + $common)
        }
        $sysArchive = Join-Path $targetDirectory "package\xui-sys-$Version.crate"
        Copy-Item -LiteralPath $sysArchive -Destination "$registry\xui-sys-$Version.crate"
        $entry = @{
            name = 'xui-sys'; vers = $Version; deps = @()
            cksum = (Get-FileHash -LiteralPath $sysArchive -Algorithm SHA256).Hash.ToLowerInvariant()
            features = @{}; yanked = $false; links = 'xui'
        } | ConvertTo-Json -Compress -Depth 10
        Write-Utf8 "$registry\index\xu\i-\xui-sys" "$entry`n"
        foreach ($rid in $present) {
            Invoke-Cargo (@('package', '-p', 'xui', '--target', $targets[$rid]) + $common)
        }
    } finally {
        Pop-Location
    }
} finally {
    $env:CARGO_HOME = $oldCargoHome
    $env:XUI_LIB_DIR = $oldLibDir
    $env:CARGO_INCREMENTAL = $oldCargoIncremental
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
foreach ($name in @('xui-sys', 'xui')) {
    Copy-Item -LiteralPath "$targetDirectory\package\$name-$Version.crate" -Destination $output
}
Write-Host "Verified Cargo assets: $output"
Write-Host "Cargo staging and local registry: $work"
