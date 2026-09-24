#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work = Join-Path $repo ("build\portable-build-tests-" + [guid]::NewGuid().ToString('N'))
$fixture = Join-Path $work 'repo with spaces'
$tools = Join-Path $fixture 'scripts'
$demo = Join-Path $tools 'Build-PortableDemo.ps1'
$log = Join-Path $tools 'calls.jsonl'
$failure = Join-Path $tools 'failure.json'
$pwsh = (Get-Process -Id $PID).Path
$originalLocation = (Get-Location).Path

function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}
function Assert-Arguments($Call, [string]$Tool, [string[]]$Arguments) {
    Assert ($Call.tool -ceq $Tool) "Expected $Tool, got $($Call.tool)."
    Assert ($Call.arguments.Count -eq $Arguments.Count) "Wrong $Tool argument count: $($Call.arguments | ConvertTo-Json -Compress)"
    for ($i = 0; $i -lt $Arguments.Count; $i++) {
        Assert ($Call.arguments[$i] -ceq $Arguments[$i]) "Wrong $Tool argument ${i}: expected '$($Arguments[$i])', got '$($Call.arguments[$i])'."
    }
}
function Invoke-Demo([hashtable]$Options = @{}) {
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
    & $demo @Options | Out-Null
}
function Read-Calls {
    if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json } }
}
function Expect-Failure([scriptblock]$Action, [string]$Pattern) {
    try { & $Action } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        Assert ((Get-Location).Path -ceq $originalLocation) 'Failure changed the caller directory.'
        return
    }
    throw "Expected failure matching $Pattern."
}

New-Item -ItemType Directory -Path $tools -Force | Out-Null
try {
    Copy-Item -LiteralPath "$repo\scripts\Build-PortableDemo.ps1", "$repo\scripts\Release.Common.ps1" -Destination $tools
    foreach ($hostName in 'Windows', 'Android', 'Web') {
        $directory = Join-Path $fixture "bindings\dotnet\Experimental\${hostName}Demo"
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        Copy-Item -LiteralPath "$repo\bindings\dotnet\Experimental\${hostName}Demo\${hostName}Demo.csproj" -Destination $directory
        $orderDirectory = Join-Path $fixture "bindings\dotnet\Experimental\${hostName}OrderDemo"
        New-Item -ItemType Directory -Path $orderDirectory -Force | Out-Null
        Copy-Item -LiteralPath "$repo\bindings\dotnet\Experimental\${hostName}Demo\${hostName}Demo.csproj" -Destination "$orderDirectory\${hostName}OrderDemo.csproj"
        $galleryDirectory = Join-Path $fixture "bindings\dotnet\Experimental\${hostName}GalleryDemo"
        New-Item -ItemType Directory -Path $galleryDirectory -Force | Out-Null
        Copy-Item -LiteralPath "$repo\bindings\dotnet\Experimental\${hostName}Demo\${hostName}Demo.csproj" -Destination "$galleryDirectory\${hostName}GalleryDemo.csproj"
    }
    Add-Content -LiteralPath "$tools\Release.Common.ps1" -Value @'

function Get-XuiCMake { Join-Path $PSScriptRoot 'cmake.ps1' }
function Get-XuiGenerator { 'Visual Studio 18 2026' }
function Invoke-FixtureTool([string]$Tool, [string[]]$Arguments) {
    [ordered]@{ tool = $Tool; arguments = $Arguments } |
        ConvertTo-Json | Set-Content -LiteralPath "$PSScriptRoot\next-call.json"
    & (Get-Process -Id $PID).Path -NoProfile -File "$PSScriptRoot\record-tool.ps1"
}
function dotnet { Invoke-FixtureTool dotnet $args }
'@
    Set-Content -LiteralPath "$tools\cmake.ps1" -Value @'
Invoke-FixtureTool cmake $args
exit $LASTEXITCODE
'@
    Set-Content -LiteralPath "$tools\record-tool.ps1" -Value @'
$ErrorActionPreference = 'Stop'
$call = Get-Content -LiteralPath "$PSScriptRoot\next-call.json" -Raw | ConvertFrom-Json
$tool = $call.tool
$arguments = @($call.arguments)
[ordered]@{ tool = $tool; arguments = $arguments; cwd = (Get-Location).Path } |
    ConvertTo-Json -Compress | Add-Content -LiteralPath "$PSScriptRoot\calls.jsonl"
if (Test-Path -LiteralPath "$PSScriptRoot\failure.json") {
    $failure = Get-Content -LiteralPath "$PSScriptRoot\failure.json" -Raw | ConvertFrom-Json
    if ($failure.tool -eq $tool -and $failure.command -eq $arguments[0]) { exit 37 }
}
if ($tool -eq 'dotnet' -and $arguments[0] -eq '--version') {
    if (Test-Path -LiteralPath "$PSScriptRoot\sdk-version.txt") { Get-Content -LiteralPath "$PSScriptRoot\sdk-version.txt" }
    else { '10.0.301' }
}
if ($tool -eq 'cmake' -and $arguments[0] -eq '--build' -and !(Test-Path -LiteralPath "$PSScriptRoot\skip-native")) {
    $native = Join-Path $arguments[1] $arguments[3]
    New-Item -ItemType Directory -Path $native -Force | Out-Null
    Set-Content -LiteralPath "$native\xui.dll" -Value 'Inert native fixture'
}
exit 0
'@
    Push-Location $work
    $originalLocation = (Get-Location).Path
    try {
        $windows = Join-Path $fixture 'bindings\dotnet\Experimental\WindowsDemo\WindowsDemo.csproj'
        $android = Join-Path $fixture 'bindings\dotnet\Experimental\AndroidDemo\AndroidDemo.csproj'
        $web = Join-Path $fixture 'bindings\dotnet\Experimental\WebDemo\WebDemo.csproj'
        $architecture = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'ARM64' } else { 'x64' }
        $rid = if ($architecture -eq 'ARM64') { 'win-arm64' } else { 'win-x64' }
        $build = Join-Path $fixture "build\portable-demo\$architecture"
        Invoke-Demo
        $calls = @(Read-Calls)
        Assert ($calls.Count -eq 6) 'Default must build exactly three hosts after SDK and native checks.'
        Assert-Arguments $calls[0] dotnet @('--version')
        Assert-Arguments $calls[1] cmake @('-S', $fixture, '-B', $build, '-G', 'Visual Studio 18 2026', '-A', $architecture,
            '-DBUILD_TESTING=OFF', '-DXUI_ENABLE_LSH=OFF', '-DXUI_ENABLE_WEBVIEW2=OFF')
        Assert-Arguments $calls[2] cmake @('--build', $build, '--config', 'Debug', '--target', 'xui', '--parallel', '4')
        Assert-Arguments $calls[3] dotnet @('build', $windows, '-c', 'Debug', '-r', $rid, "-p:XuiNativeDir=$build\Debug", '--nologo')
        Assert-Arguments $calls[4] dotnet @('build', $android, '-c', 'Debug', '--nologo')
        Assert-Arguments $calls[5] dotnet @('build', $web, '-c', 'Debug', '--nologo')
        foreach ($call in $calls) { Assert ($call.cwd -ceq $fixture) 'Tools must run from the repository, independent of caller cwd.' }
        Assert ((Get-Location).Path -ceq $originalLocation) 'Build changed the caller directory.'

        Invoke-Demo @{ Sample = 'Order'; Configuration = 'Release' }
        $calls = @(Read-Calls)
        Assert ($calls.Count -eq 6) 'Order must build exactly three hosts after SDK and native checks.'
        Assert-Arguments $calls[2] cmake @('--build', $build, '--config', 'Release', '--target', 'xui', '--parallel', '4')
        $orderWindows = Join-Path $fixture 'bindings\dotnet\Experimental\WindowsOrderDemo\WindowsOrderDemo.csproj'
        $orderAndroid = Join-Path $fixture 'bindings\dotnet\Experimental\AndroidOrderDemo\AndroidOrderDemo.csproj'
        $orderWeb = Join-Path $fixture 'bindings\dotnet\Experimental\WebOrderDemo\WebOrderDemo.csproj'
        Assert-Arguments $calls[3] dotnet @('build', $orderWindows, '-c', 'Release', '-r', $rid, "-p:XuiNativeDir=$build\Release", '--nologo')
        Assert-Arguments $calls[4] dotnet @('build', $orderAndroid, '-c', 'Release', '--nologo')
        Assert-Arguments $calls[5] dotnet @('build', $orderWeb, '-c', 'Release', '--nologo')
        foreach ($platformName in 'Windows', 'Android', 'Web') {
            $options = @{ Sample = 'Order'; Platform = $platformName; Run = $true }
            if ($platformName -eq 'Android') { $options.AndroidSerial = 'emulator-5566' }
            Invoke-Demo $options
            $calls = @(Read-Calls)
            $last = $calls[-1]
            $project = Join-Path $fixture "bindings\dotnet\Experimental\${platformName}OrderDemo\${platformName}OrderDemo.csproj"
            Assert ($last.arguments -ccontains $project) 'Order run selected a greeting project.'
            if ($platformName -eq 'Android') {
                Assert-Arguments $last dotnet @('build', $project, '-c', 'Debug', '--nologo', '-t:Run', '-p:AdbTarget=-s emulator-5566')
            } elseif ($platformName -eq 'Windows') {
                Assert-Arguments $last dotnet @('run', '--project', $project, '-c', 'Debug', '-r', $rid, "-p:XuiNativeDir=$build\Debug", '--no-build', '--no-restore')
            } else {
                Assert-Arguments $last dotnet @('run', '--project', $project, '-c', 'Debug', '--no-build', '--no-restore', '--no-launch-profile', '--', '--urls', 'http://127.0.0.1:5190')
            }
        }

        foreach ($architecture in 'x64', 'ARM64') {
            $rid = if ($architecture -eq 'ARM64') { 'win-arm64' } else { 'win-x64' }
            $build = Join-Path $fixture "custom native $architecture"
            Invoke-Demo @{ Platform = 'Windows'; Configuration = 'Release'; Architecture = $architecture; NativeBuildDirectory = "custom native $architecture"; Run = $true }
            $calls = @(Read-Calls)
            Assert ($calls.Count -eq 5) 'Windows run must build native and managed code before one launch.'
            Assert-Arguments $calls[1] cmake @('-S', $fixture, '-B', $build, '-G', 'Visual Studio 18 2026', '-A', $architecture,
                '-DBUILD_TESTING=OFF', '-DXUI_ENABLE_LSH=OFF', '-DXUI_ENABLE_WEBVIEW2=OFF')
            Assert-Arguments $calls[2] cmake @('--build', $build, '--config', 'Release', '--target', 'xui', '--parallel', '4')
            Assert-Arguments $calls[3] dotnet @('build', $windows, '-c', 'Release', '-r', $rid, "-p:XuiNativeDir=$build\Release", '--nologo')
            Assert-Arguments $calls[4] dotnet @('run', '--project', $windows, '-c', 'Release', '-r', $rid, "-p:XuiNativeDir=$build\Release", '--no-build', '--no-restore')
        }

        foreach ($case in @(
            @{ Input = 'Tasks'; Name = 'Tasks'; Activity = 'TaskBoardActivity' },
            @{ Input = 'expenses'; Name = 'Expenses'; Activity = 'ExpenseLedgerActivity' },
            @{ Input = 'Planner'; Name = 'Planner'; Activity = 'SessionPlannerActivity' },
            @{ Input = 'dynamictasks'; Name = 'DynamicTasks'; Activity = 'DynamicTaskBoardActivity' },
            @{ Input = 'profile'; Name = 'Profile'; Activity = 'ProfileWorkspaceActivity' },
            @{ Input = 'studio'; Name = 'Studio'; Activity = 'WorkspaceStudioActivity' }
        )) {
            Invoke-Demo @{ Sample = 'Gallery'; GalleryApp = $case.Input; Platform = 'Android'; Run = $true; AndroidSerial = 'emulator-5566' }
            $calls = @(Read-Calls)
            $project = Join-Path $fixture 'bindings\dotnet\Experimental\AndroidGalleryDemo\AndroidGalleryDemo.csproj'
            Assert-Arguments $calls[-1] dotnet @('build', $project, '-c', 'Debug', '--nologo', '-t:Run',
                '-p:AdbTarget=-s emulator-5566', "-p:RunActivity=dev.xui.portable.gallery.$($case.Activity)")
            Invoke-Demo @{ Sample = 'Gallery'; GalleryApp = $case.Input; Platform = 'Windows'; Run = $true }
            $calls = @(Read-Calls)
            Assert ($calls[-1].arguments[-2] -ceq '--sample' -and $calls[-1].arguments[-1] -ceq $case.Name) 'Windows gallery selection was not canonicalized.'
        }
        Invoke-Demo @{ Sample = 'Gallery'; Platform = 'Web'; GalleryApp = 'Expenses'; Run = $true }
        $calls = @(Read-Calls)
        $project = Join-Path $fixture 'bindings\dotnet\Experimental\WebGalleryDemo\WebGalleryDemo.csproj'
        Assert-Arguments $calls[-1] dotnet @('run', '--project', $project, '-c', 'Debug', '--no-build', '--no-restore',
            '--no-launch-profile', '--', '--urls', 'http://127.0.0.1:5190')
        foreach ($case in @(
            @{ Input = 'DynamicTasks'; Query = 'dynamic-tasks' },
            @{ Input = 'Profile'; Query = 'profile-workspace' },
            @{ Input = 'Studio'; Query = 'studio' }
        )) {
            $output = & $demo -Sample Gallery -Platform Web -GalleryApp $case.Input -Run 6>&1 | Out-String
            Assert ($output.Contains("Open http://127.0.0.1:5190/?app=$($case.Query)")) 'Web gallery run printed the wrong application URL.'
        }

        $androidSdk = Join-Path $fixture 'Android SDK'
        $javaSdk = Join-Path $fixture 'Java JDK'
        New-Item -ItemType Directory -Path $androidSdk, $javaSdk -Force | Out-Null
        Invoke-Demo @{ Platform = 'Android'; Run = $true; AndroidSerial = 'emulator-5566'; AndroidSdkDirectory = $androidSdk; JavaSdkDirectory = 'Java JDK' }
        $calls = @(Read-Calls)
        Assert ($calls.Count -eq 2) 'Android run must not build or start other hosts.'
        Assert-Arguments $calls[1] dotnet @('build', $android, '-c', 'Debug', '--nologo',
            "-p:AndroidSdkDirectory=$androidSdk", "-p:JavaSdkDirectory=$javaSdk", '-t:Run', '-p:AdbTarget=-s emulator-5566')
        Invoke-Demo @{ Platform = @('Android', 'Web'); Configuration = 'Release' }
        $calls = @(Read-Calls)
        Assert ($calls.Count -eq 3) 'Subset build selected an unexpected host.'
        Assert-Arguments $calls[1] dotnet @('build', $android, '-c', 'Release', '--nologo')
        Assert-Arguments $calls[2] dotnet @('build', $web, '-c', 'Release', '--nologo')

        foreach ($configuration in 'Debug', 'Release') {
            Invoke-Demo @{ Platform = 'Web'; Configuration = $configuration; Run = $true; WebUrl = 'http://localhost:5199' }
            $calls = @(Read-Calls)
            Assert ($calls.Count -eq 3) 'Web run must build once and launch once.'
            Assert-Arguments $calls[1] dotnet @('build', $web, '-c', $configuration, '--nologo')
            Assert-Arguments $calls[2] dotnet @('run', '--project', $web, '-c', $configuration, '--no-build', '--no-restore', '--no-launch-profile', '--', '--urls', 'http://localhost:5199')
        }

        foreach ($options in @(
            @{ Platform = 'Unknown' },
            @{ Sample = 'Unknown' },
            @{ Sample = 'Gallery'; GalleryApp = 'Unknown' },
            @{ Sample = 'Order'; GalleryApp = 'Tasks' },
            @{ Platform = @('All', 'Web') },
            @{ Platform = @() },
            @{ Run = $true },
            @{ Platform = @('Android', 'Web'); Run = $true },
            @{ Platform = 'Android'; Run = $true },
            @{ Platform = 'Android'; Run = $true; AndroidSerial = 'emulator-5566 -e' },
            @{ Platform = 'Android'; AndroidSerial = 'emulator-5566' },
            @{ Platform = 'Web'; Run = $true; AndroidSerial = 'emulator-5566' },
            @{ Platform = 'Android'; AndroidSdkDirectory = 'missing SDK' },
            @{ Platform = 'Android'; JavaSdkDirectory = 'missing JDK' },
            @{ Platform = 'Web'; Configuration = 'Fast' }
        )) {
            Expect-Failure { Invoke-Demo $options } '.'
            Assert (@(Read-Calls).Count -eq 0) 'Invalid selection must fail before executing tools.'
        }
        foreach ($url in 'http://0.0.0.0:5190', 'http://example.com:5190', 'ftp://localhost:5190', 'http://localhost:0',
            'http://user@localhost:5190', 'http://localhost:5190/path', 'http://localhost:5190/?query', 'http://localhost:5190/#fragment', 'not a URL') {
            Expect-Failure { Invoke-Demo @{ Platform = 'Web'; Run = $true; WebUrl = $url } } 'HTTP loopback origin'
            Assert (@(Read-Calls).Count -eq 0) 'Invalid URL must fail before executing tools.'
        }

        foreach ($case in @(
            @{ Tool = 'dotnet'; Command = '--version'; Platform = 'Web'; Count = 1 },
            @{ Tool = 'cmake'; Command = '-S'; Platform = 'Windows'; Count = 2 },
            @{ Tool = 'cmake'; Command = '--build'; Platform = 'Windows'; Count = 3 },
            @{ Tool = 'dotnet'; Command = 'build'; Platform = 'Windows'; Count = 4 },
            @{ Tool = 'dotnet'; Command = 'build'; Platform = 'Android'; Count = 2 },
            @{ Tool = 'dotnet'; Command = 'build'; Platform = 'Web'; Count = 2 },
            @{ Tool = 'dotnet'; Command = 'run'; Platform = 'Web'; Count = 3 }
        )) {
            @{ tool = $case.Tool; command = $case.Command } | ConvertTo-Json | Set-Content -LiteralPath $failure
            $options = @{ Platform = $case.Platform; Run = $true }
            if ($case.Platform -eq 'Android') { $options.AndroidSerial = 'emulator-5566' }
            Expect-Failure { Invoke-Demo $options } 'Command failed \(37\)'
            Assert (@(Read-Calls).Count -eq $case.Count) 'A failed tool must stop subsequent builds and launches.'
        }
        $output = & $pwsh -NoProfile -File $demo -Platform Web -Run 2>&1 | Out-String
        Assert ($LASTEXITCODE -ne 0 -and $output -match 'Command failed \(37\)') 'External script invocation must return nonzero and report the tool failure.'
        Remove-Item -LiteralPath $failure

        Set-Content -LiteralPath "$tools\sdk-version.txt" -Value '9.0.100'
        Expect-Failure { Invoke-Demo @{ Platform = 'Web' } } 'selected SDK: 9.0.100'
        Assert (@(Read-Calls).Count -eq 1) 'An old SDK must fail before build.'
        Remove-Item -LiteralPath "$tools\sdk-version.txt"
        Set-Content -LiteralPath "$tools\skip-native" -Value ''
        Expect-Failure { Invoke-Demo @{ Platform = 'Windows'; NativeBuildDirectory = 'missing native output' } } 'Native build did not produce'
        Assert (@(Read-Calls).Count -eq 3) 'Missing native output must stop the managed build.'
    } finally {
        Pop-Location
    }
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force
}
Write-Output 'Portable demo sample/platform selection, argument boundaries, run guards, tool failures, and cwd isolation passed.'
