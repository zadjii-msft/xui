param(
    [ValidateSet('x64', 'ARM64')][string]$Architecture = $(if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'ARM64' } else { 'x64' })
)
. "$PSScriptRoot\..\scripts\Release.Common.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$rid = if ($Architecture -eq 'ARM64') { 'win-arm64' } else { 'win-x64' }
$native = "$repo\build\$Architecture\Release\xui.dll"
foreach ($project in Get-XuiSamples) {
    foreach ($configuration in 'Debug', 'Release') {
        Invoke-Checked { dotnet build $project.FullName -c $configuration -r $rid --nologo -v:q }
        Assert-SameFile $native "$($project.DirectoryName)\bin\$configuration\net10.0\$rid\xui.dll"
    }
}
$override = Join-Path $repo ("build\native-copy-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $override | Out-Null
$variant = "$override\xui.dll"
[IO.File]::WriteAllBytes($variant, ([IO.File]::ReadAllBytes($native) + [byte[]](0, 1, 2, 3)))
(Get-Item $variant).LastWriteTimeUtc = [datetime]'2000-01-01'
$sample = "$repo\bindings\dotnet\Sample"
$sampleDll = "$sample\bin\Release\net10.0\$rid\xui.dll"
try {
    Invoke-Checked { dotnet build $sample -c Release -r $rid "-p:XuiNativeDir=$override" --nologo -v:q }
    Assert-SameFile $variant $sampleDll
    $written = (Get-Item $sampleDll).LastWriteTimeUtc
    Invoke-Checked { dotnet build $sample -c Release -r $rid "-p:XuiNativeDir=$override" --nologo -v:q }
    if ((Get-Item $sampleDll).LastWriteTimeUtc -ne $written) { throw 'A no-op build rewrote the native runtime.' }
} finally {
    Invoke-Checked { dotnet build $sample -c Release -r $rid --nologo -v:q }
    Assert-SameFile $native $sampleDll
}
$missing = Join-Path $repo ("build\missing-" + [guid]::NewGuid().ToString('N'))
$message = & dotnet build "$repo\bindings\dotnet\Sample" -r $rid "-p:XuiNativeDir=$missing" --nologo 2>&1 | Out-String
if ($LASTEXITCODE -eq 0 -or $message -notmatch 'XUI native runtime not found') { throw 'A missing native runtime must fail explicitly.' }
Write-Output 'Local sample DLL deployment and missing-runtime diagnostic passed.'
$global:LASTEXITCODE = 0
