#Requires -Version 7.0

<#
.SYNOPSIS
Builds the experimental shared greeting, order, or gallery apps for Windows, Android, and web.
.DESCRIPTION
Requires .NET 10 or later. Windows also requires Visual Studio C++ desktop tools,
CMake, and a Windows SDK; Android requires the matching .NET Android workload,
Android SDK, and JDK. Missing prerequisites are never installed automatically.

Defaults to the greeting sample, Debug, and all three hosts. Windows native output is isolated under
build\portable-demo\<architecture>. Relative directory options are repository-relative.
Android SDK/JDK paths use .NET for Android discovery when not supplied.

-Run requires one platform. Android additionally requires -AndroidSerial and uses
the workload's development deployment. Web runs a foreground loopback server;
Ctrl+C stops it. This is not a production-support, release-signing, or publishing tool.
.EXAMPLE
.\scripts\Build-PortableDemo.ps1
.EXAMPLE
.\scripts\Build-PortableDemo.ps1 -Platform Windows,Web -Configuration Release
.EXAMPLE
.\scripts\Build-PortableDemo.ps1 -Sample Order
.EXAMPLE
.\scripts\Build-PortableDemo.ps1 -Sample Gallery -GalleryApp Expenses -Platform Android -Run -AndroidSerial emulator-5554
.EXAMPLE
.\scripts\Build-PortableDemo.ps1 -Sample Order -Platform Android -Run -AndroidSerial emulator-5554
.EXAMPLE
.\scripts\Build-PortableDemo.ps1 -Platform Windows -Run
.EXAMPLE
.\scripts\Build-PortableDemo.ps1 -Platform Android -Run -AndroidSerial emulator-5554 -AndroidSdkDirectory 'C:\Program Files (x86)\Android\android-sdk' -JavaSdkDirectory 'C:\Program Files\Android\openjdk\jdk-21.0.8'
.EXAMPLE
.\scripts\Build-PortableDemo.ps1 -Platform Web -Run -WebUrl http://127.0.0.1:5190
#>
[CmdletBinding()]
param(
    [ValidateSet('All', 'Windows', 'Android', 'Web')][string[]]$Platform = @('All'),
    [ValidateSet('Greeting', 'Order', 'Gallery')][string]$Sample = 'Greeting',
    [ValidateSet('Tasks', 'Expenses', 'Planner', 'DynamicTasks', 'Profile', 'Studio')][string]$GalleryApp = 'Tasks',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('x64', 'ARM64')][string]$Architecture = $(if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'ARM64' } else { 'x64' }),
    [string]$NativeBuildDirectory,
    [string]$AndroidSdkDirectory,
    [string]$JavaSdkDirectory,
    [string]$AndroidSerial,
    [string]$WebUrl = 'http://127.0.0.1:5190',
    [switch]$Run
)
. "$PSScriptRoot\Release.Common.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$projectSuffix = switch ($Sample) { Order { 'OrderDemo' } Gallery { 'GalleryDemo' } default { 'Demo' } }
$gallery = @{
    Tasks = @{ Name = 'Tasks'; Query = 'task-board'; Activity = 'TaskBoardActivity' }
    Expenses = @{ Name = 'Expenses'; Query = 'expense-ledger'; Activity = 'ExpenseLedgerActivity' }
    Planner = @{ Name = 'Planner'; Query = 'session-planner'; Activity = 'SessionPlannerActivity' }
    DynamicTasks = @{ Name = 'DynamicTasks'; Query = 'dynamic-tasks'; Activity = 'DynamicTaskBoardActivity' }
    Profile = @{ Name = 'Profile'; Query = 'profile-workspace'; Activity = 'ProfileWorkspaceActivity' }
    Studio = @{ Name = 'Studio'; Query = 'studio'; Activity = 'WorkspaceStudioActivity' }
}[$GalleryApp]
if ($PSBoundParameters.ContainsKey('GalleryApp') -and $Sample -ne 'Gallery') {
    throw '-GalleryApp is only valid with -Sample Gallery.'
}

if (!$Platform -or ($Platform -contains 'All' -and $Platform.Count -ne 1)) {
    throw 'Select All alone, or one or more of Windows, Android, Web.'
}
[string[]]$platforms = if ($Platform -contains 'All') { @('Windows', 'Android', 'Web') } else { @($Platform | Select-Object -Unique) }
if ($Run -and $platforms.Count -ne 1) { throw '-Run requires exactly one platform: Windows, Android, or Web.' }
if ($Run -and $platforms -contains 'Android' -and [string]::IsNullOrWhiteSpace($AndroidSerial)) {
    throw 'Android -Run requires -AndroidSerial from adb devices; a device is never selected implicitly.'
}
if ($AndroidSerial -and (!$Run -or $platforms -notcontains 'Android')) {
    throw '-AndroidSerial is only valid with -Platform Android -Run.'
}
if ($AndroidSerial -and $AndroidSerial -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._:-]*$') {
    throw 'Invalid Android serial. Supply one adb device serial, without options or whitespace.'
}
if ($platforms -contains 'Web') {
    $url = $null
    if (![Uri]::TryCreate($WebUrl, [UriKind]::Absolute, [ref]$url) -or
        $url.Scheme -ne 'http' -or !$url.IsLoopback -or $url.Port -lt 1 -or
        $url.UserInfo -or $url.AbsolutePath -ne '/' -or $url.Query -or $url.Fragment) {
        throw '-WebUrl must be an HTTP loopback origin, such as http://127.0.0.1:5190.'
    }
}
if ($platforms -contains 'Windows' -and ![System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows)) {
    throw 'The Windows demo requires Windows and the Visual Studio C++ desktop tools.'
}

$androidArguments = @()
if ($platforms -contains 'Android') {
    foreach ($name in 'AndroidSdkDirectory', 'JavaSdkDirectory') {
        $directory = Get-Variable -Name $name -ValueOnly
        if ($directory) {
            $directory = [IO.Path]::GetFullPath($directory, $repo)
            if (!(Test-Path -LiteralPath $directory -PathType Container)) {
                throw "$name does not exist: $directory. Install prerequisites separately or supply an existing directory."
            }
            $androidArguments += "-p:${name}=$directory"
        }
    }
}
foreach ($hostName in $platforms) {
    $project = Join-Path $repo "bindings\dotnet\Experimental\$hostName$projectSuffix\$hostName$projectSuffix.csproj"
    if (!(Test-Path -LiteralPath $project -PathType Leaf)) { throw "Demo project not found: $project" }
}
if (!(Get-Command dotnet -ErrorAction SilentlyContinue)) { throw 'The .NET 10 SDK or later is required. Install it separately and put dotnet on PATH.' }

Push-Location $repo
try {
    $sdk = (Invoke-Checked { dotnet --version } | Out-String).Trim()
    if ($sdk -notmatch '^(\d+)\.' -or [int]$Matches[1] -lt 10) {
        throw "The .NET 10 SDK or later is required; selected SDK: $sdk."
    }
    foreach ($hostName in $platforms) {
        $project = Join-Path $repo "bindings\dotnet\Experimental\$hostName$projectSuffix\$hostName$projectSuffix.csproj"
        Write-Host "Building experimental $Sample demo for $hostName ($Configuration)."
        switch ($hostName) {
            'Windows' {
                if (!$NativeBuildDirectory) { $NativeBuildDirectory = "build\portable-demo\$Architecture" }
                $build = [IO.Path]::GetFullPath($NativeBuildDirectory, $repo)
                $native = Join-Path $build $Configuration
                $rid = if ($Architecture -eq 'ARM64') { 'win-arm64' } else { 'win-x64' }
                $cmake = Get-XuiCMake
                $generator = Get-XuiGenerator
                Invoke-Checked {
                    & $cmake -S $repo -B $build -G $generator -A $Architecture `
                        -DBUILD_TESTING=OFF -DXUI_ENABLE_LSH=OFF -DXUI_ENABLE_WEBVIEW2=OFF
                }
                Invoke-Checked { & $cmake --build $build --config $Configuration --target xui --parallel 4 }
                if (!(Test-Path -LiteralPath "$native\xui.dll" -PathType Leaf)) { throw "Native build did not produce $native\xui.dll." }
                Invoke-Checked { dotnet build $project -c $Configuration -r $rid "-p:XuiNativeDir=$native" --nologo }
                if ($Run) {
                    $arguments = @('run', '--project', $project, '-c', $Configuration, '-r', $rid,
                        "-p:XuiNativeDir=$native", '--no-build', '--no-restore')
                    if ($Sample -eq 'Gallery') { $arguments += @('--', '--sample', $gallery.Name) }
                    Invoke-Checked { dotnet @arguments }
                }
            }
            'Android' {
                $arguments = @('build', $project, '-c', $Configuration, '--nologo') + $androidArguments
                if ($Run) { $arguments += @('-t:Run', "-p:AdbTarget=-s $AndroidSerial") }
                if ($Run -and $Sample -eq 'Gallery') {
                    $arguments += "-p:RunActivity=dev.xui.portable.gallery.$($gallery.Activity)"
                }
                Invoke-Checked { dotnet @arguments }
            }
            'Web' {
                Invoke-Checked { dotnet build $project -c $Configuration --nologo }
                if ($Run) {
                    Write-Host "Serving $WebUrl in the foreground; press Ctrl+C to stop."
                    if ($Sample -eq 'Gallery') { Write-Host "Open $($WebUrl.TrimEnd('/'))/?app=$($gallery.Query)" }
                    $arguments = @('run', '--project', $project, '-c', $Configuration,
                        '--no-build', '--no-restore', '--no-launch-profile', '--', '--urls', $WebUrl)
                    Invoke-Checked { dotnet @arguments }
                }
            }
        }
    }
} finally {
    Pop-Location
}
