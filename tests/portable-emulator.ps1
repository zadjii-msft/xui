#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Serial,
    [string]$AdbPath = 'adb',
    [string]$ArtifactDirectory = 'build\ci\android'
)
. "$PSScriptRoot\..\scripts\Release.Common.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts = [IO.Path]::GetFullPath($ArtifactDirectory)
New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
Start-Transcript (Join-Path $artifacts 'device.log')
try {
    Invoke-Checked { & $AdbPath -s $Serial get-state }
    foreach ($project in 'AndroidDemo', 'AndroidDeviceTests', 'AndroidOrderDemo', 'AndroidOrderDeviceTests') {
        $apks = @(Get-ChildItem "$repo\bindings\dotnet\Experimental\$project\bin\Debug" -Recurse -File -Filter '*-Signed.apk')
        if ($apks.Count -ne 1) { throw "Expected exactly one fresh signed Debug APK for $project, found $($apks.Count)." }
        Invoke-Checked { & $AdbPath -s $Serial install -r $apks[0].FullName }
    }
    & "$repo\bindings\dotnet\Experimental\AndroidDeviceTests\Run-DeviceAcceptance.ps1" -Serial $Serial -AdbPath $AdbPath
    & "$repo\bindings\dotnet\Experimental\AndroidOrderDeviceTests\Run-OrderDeviceAcceptance.ps1" -Serial $Serial -AdbPath $AdbPath
} finally {
    try { & $AdbPath -s $Serial logcat -d 2>&1 | Set-Content (Join-Path $artifacts 'logcat.txt') }
    finally { Stop-Transcript }
}
