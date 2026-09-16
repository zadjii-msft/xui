Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-ReleaseVersion([string]$Version) {
    if ($Version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw "Invalid release version '$Version'. Use Major.minor.rev without leading zeroes."
    }
}

function Get-XuiReleaseAssetNames([string]$Version) {
    @("Xui.$Version.nupkg", "xui-sys-$Version.crate", "xui-$Version.crate",
        "Xui.Samples.$Version.win-x64.zip", "Xui.Samples.$Version.win-arm64.zip")
}

function Invoke-Checked([scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $Command" }
}

function Get-XuiSamples([switch]$ReleaseOnly) {
    $roots = @(
        (Join-Path $PSScriptRoot '..\bindings\dotnet'),
        (Join-Path $PSScriptRoot '..\docs\specs\tutorials\sample')
    )
    Get-ChildItem -Path $roots -Filter '*.csproj' -Recurse |
        Where-Object {
            [xml]$project = Get-Content -LiteralPath $_.FullName -Raw
            $null -ne $project.SelectSingleNode('/Project/PropertyGroup/IsXuiSample[text()="true"]') -and
                (!$ReleaseOnly -or $null -eq $project.SelectSingleNode('/Project/PropertyGroup/IsXuiReleaseSample[text()="false"]'))
        }
}

function Assert-SameFile([string]$Expected, [string]$Actual) {
    if ((Get-FileHash -LiteralPath $Expected).Hash -ne (Get-FileHash -LiteralPath $Actual).Hash) {
        throw "Native runtime differs from the selected build: $Actual"
    }
}

function Get-XuiCMake {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
    if (!$vs) { throw 'CMake was not found. Install the Visual Studio C++ CMake tools or add CMake to PATH.' }
    return Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}

function Get-XuiVcVars([string]$Architecture) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath
    if (!$vs) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (!$vs) { throw 'Visual Studio C++ tools were not found.' }
    $hostArm = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64'
    $name = if ($Architecture -eq 'ARM64') {
        if ($hostArm) { 'vcvarsarm64.bat' } else { 'vcvarsamd64_arm64.bat' }
    } else {
        if ($hostArm) { 'vcvarsarm64_amd64.bat' } else { 'vcvars64.bat' }
    }
    $path = Join-Path $vs "VC\Auxiliary\Build\$name"
    if (!(Test-Path -LiteralPath $path)) { throw "Required compiler environment not found: $path" }
    return $path
}
