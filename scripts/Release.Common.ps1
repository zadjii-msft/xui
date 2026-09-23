Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-ReleaseVersion([string]$Version) {
    if ($Version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw "Invalid release version '$Version'. Use Major.minor.rev without leading zeroes."
    }
}

function Get-XuiReleaseAssetNames([string]$Version) {
    @("Xui.$Version.nupkg", "Xui.Templates.$Version.nupkg", "xui-sys-$Version.crate", "xui-$Version.crate",
        "Xui.Samples.$Version.win-x64.zip", "Xui.Samples.$Version.win-arm64.zip",
        "Xui.Designer.$Version.win-x64.zip", "Xui.Designer.$Version.win-arm64.zip")
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

function Write-XuiArchiveManifest([string]$Directory, [string]$Version, [string]$RuntimeIdentifier) {
    $files = Get-ChildItem $Directory -File -Recurse | ForEach-Object {
        [ordered]@{ path = [IO.Path]::GetRelativePath($Directory, $_.FullName); sha256 = (Get-FileHash $_.FullName).Hash }
    }
    [ordered]@{ version = $Version; runtime = $RuntimeIdentifier; files = @($files) } |
        ConvertTo-Json -Depth 5 | Set-Content "$Directory\manifest.json" -Encoding utf8
}

function Get-XuiVisualStudio([string]$Component) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path -LiteralPath $vswhere)) { throw "Visual Studio installer not found: $vswhere" }
    $arguments = @('-latest', '-prerelease', '-products', '*', '-requires', $Component, '-format', 'json')
    $installations = & $vswhere @arguments -version '[17.0,18.0)' | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw "Visual Studio discovery failed: $vswhere" }
    if (!$installations) {
        $installations = & $vswhere @arguments | ConvertFrom-Json
        if ($LASTEXITCODE -ne 0) { throw "Visual Studio discovery failed: $vswhere" }
    }
    if (!$installations) { throw "Visual Studio component not found: $Component" }
    return $installations
}

function Get-XuiGenerator {
    $vs = Get-XuiVisualStudio 'Microsoft.VisualStudio.Component.VC.CMake.Project'
    switch ([int]$vs.installationVersion.Split('.')[0]) {
        17 { return 'Visual Studio 17 2022' }
        18 { return 'Visual Studio 18 2026' }
        default { throw "Unsupported Visual Studio version: $($vs.installationVersion)" }
    }
}

function Get-XuiCMake {
    $vs = Get-XuiVisualStudio 'Microsoft.VisualStudio.Component.VC.CMake.Project'
    $path = Join-Path $vs.installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (!(Test-Path -LiteralPath $path)) { throw "CMake was not found: $path" }
    return $path
}

function Get-XuiVcVars([string]$Architecture) {
    $component = if ($Architecture -eq 'ARM64') {
        'Microsoft.VisualStudio.Component.VC.Tools.ARM64'
    } else {
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
    }
    $vs = Get-XuiVisualStudio $component
    $hostArm = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64'
    $name = if ($Architecture -eq 'ARM64') {
        if ($hostArm) { 'vcvarsarm64.bat' } else { 'vcvarsamd64_arm64.bat' }
    } else {
        if ($hostArm) { 'vcvarsarm64_amd64.bat' } else { 'vcvars64.bat' }
    }
    $path = Join-Path $vs.installationPath "VC\Auxiliary\Build\$name"
    if (!(Test-Path -LiteralPath $path)) { throw "Required compiler environment not found: $path" }
    return $path
}

function Invoke-XuiVcVarsCommand([string]$Architecture, [string]$Command) {
    $vcvars = Get-XuiVcVars $Architecture
    $installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
    $oldPath = $env:PATH
    try {
        $env:PATH = "$installer;$oldPath"
        & $env:ComSpec /d /c "call `"$vcvars`" >nul && $Command"
        if ($LASTEXITCODE -ne 0) { throw "Developer command failed ($LASTEXITCODE): $Command" }
    } finally {
        $env:PATH = $oldPath
    }
}
