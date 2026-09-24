#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$Version = '0.1.0-preview.1',
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$WorkDirectory,
    [string]$NativeDirectory,
    [ValidateSet('win-x64', 'win-arm64')][string]$NativeRuntimeIdentifier = 'win-x64',
    [string]$AndroidSdkDirectory,
    [string]$JavaSdkDirectory,
    [switch]$SkipWindows
)
. "$PSScriptRoot\Release.Common.ps1"
if ($Version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-preview\.(0|[1-9][0-9]*)$') {
    throw 'Experimental packages require Major.minor.patch-preview.number without leading zeroes.'
}
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (!$WorkDirectory) { $WorkDirectory = Join-Path $repo ("build\portable-pack-" + [guid]::NewGuid().ToString('N')) }
$work = [IO.Path]::GetFullPath($WorkDirectory)
if (Test-Path -LiteralPath $work) { throw "Use a new WorkDirectory to avoid stale package inputs: $work" }
if (!$SkipWindows) {
    if (!$NativeDirectory) { throw 'Supply -NativeDirectory from an actual native Release build, or use -SkipWindows.' }
    $native = (Resolve-Path -LiteralPath $NativeDirectory).Path
    if (!(Test-Path -LiteralPath "$native\xui.dll" -PathType Leaf)) { throw "Native runtime missing: $native\xui.dll" }
    $bytes = [IO.File]::ReadAllBytes("$native\xui.dll")
    $offset = if ($bytes.Length -ge 64) { [BitConverter]::ToInt32($bytes, 60) } else { -1 }
    if ($offset -lt 0 -or $offset + 6 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes, $offset) -ne 0x4550) {
        throw 'Native input is not a PE image.'
    }
    $machine = [BitConverter]::ToUInt16($bytes, $offset + 4)
    $expected = if ($NativeRuntimeIdentifier -eq 'win-x64') { 0x8664 } else { 0xaa64 }
    if ($machine -ne $expected) { throw "Native image architecture does not match $NativeRuntimeIdentifier." }
}
$androidArguments = @()
foreach ($name in 'AndroidSdkDirectory', 'JavaSdkDirectory') {
    $path = Get-Variable -Name $name -ValueOnly
    if ($path) {
        if (!(Test-Path -LiteralPath $path -PathType Container)) { throw "$name does not exist: $path" }
        $androidArguments += "-p:${name}=$([IO.Path]::GetFullPath($path))"
    }
}
New-Item -ItemType Directory -Path $work, $output -Force | Out-Null
Invoke-Checked { dotnet build "$repo\bindings\dotnet\Xui.Generator\Xui.Generator.csproj" -c Release "-p:Version=$Version" -o "$work\compiler" --nologo }
Invoke-Checked {
    dotnet pack "$repo\packaging\experimental\Xui.Experimental.Compiler.csproj" -c Release "-p:Version=$Version" `
        "-p:XuiCompilerDirectory=$work\compiler" "-p:BaseIntermediateOutputPath=$work\compiler-obj\" `
        "-p:OutputPath=$work\compiler-bin\" -o $output --nologo
}
foreach ($name in 'Portable', 'Android', 'Web') {
    $arguments = @('pack', "$repo\bindings\dotnet\Experimental\Xui.$name\Xui.$name.csproj",
        '-c', 'Release', "-p:Version=$Version", '-o', $output, '--nologo')
    if ($name -eq 'Android') { $arguments += $androidArguments }
    Invoke-Checked { dotnet @arguments }
}
if (!$SkipWindows) {
    Invoke-Checked {
        dotnet build "$repo\bindings\dotnet\Experimental\Xui.Windows\Xui.Windows.csproj" -c Release `
            "-p:Version=$Version" -o "$work\windows" --nologo
    }
    Invoke-Checked {
        dotnet pack "$repo\packaging\experimental\Xui.Experimental.Windows.csproj" -c Release "-p:PackageVersion=$Version" `
            "-p:XuiWindowsDirectory=$work\windows" "-p:XuiNativeDirectory=$native" "-p:XuiNativeRuntimeIdentifier=$NativeRuntimeIdentifier" `
            "-p:BaseIntermediateOutputPath=$work\windows-obj\" "-p:OutputPath=$work\windows-bin\" -o $output --nologo
    }
}
$template = "$repo\templates\xui-portable"
$staged = "$work\template"
foreach ($file in Get-ChildItem -LiteralPath $template -File -Recurse -Force) {
    $relative = [IO.Path]::GetRelativePath($template, $file.FullName)
    if ($relative -match '(^|[\\/])(bin|obj)([\\/]|$)') { continue }
    $destination = Join-Path $staged $relative
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    $content = Get-Content -LiteralPath $file.FullName -Raw
    $content.Replace('__XUI_PORTABLE_VERSION__', $Version) | Set-Content -LiteralPath $destination -Encoding utf8
}
Invoke-Checked {
    dotnet pack "$repo\packaging\experimental\Xui.Experimental.Templates.csproj" -c Release "-p:Version=$Version" `
        "-p:XuiTemplateDirectory=$staged" "-p:BaseIntermediateOutputPath=$work\template-obj\" `
        "-p:OutputPath=$work\template-bin\" -o $output --nologo
}
$packages = @('Compiler', 'Portable', 'Android', 'Web', 'Templates')
if (!$SkipWindows) { $packages += 'Windows' }
foreach ($name in $packages) {
    if (!(Test-Path -LiteralPath "$output\Xui.Experimental.$name.$Version.nupkg")) { throw "Package was not created: $name" }
}
Write-Output "Local experimental packages created in $output. Nothing was published; workloads and SDK licenses were not installed."
