param(
    [string]$BaselineDirectory = "build\styling-arm64\baseline-build\Release",
    [string]$ChangedDirectory = "build\styling-arm64\Release",
    [string]$BaselineProbe = "build\styling-arm64\button-baseline.exe",
    [string]$ChangedProbe = "build\styling-arm64\button-final.exe",
    [string]$BaselineCollections = "build\styling-arm64\baseline\xui_performance_tests.exe",
    [string]$OutputDirectory = "build\styling-arm64\reserved-comparison",
    [ValidateRange(1,10)][int]$WindowPairs = 3,
    [ValidateRange(2,8)][int]$StyledRuns = 4,
    [switch]$StyledOnly
)
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\measure-window-memory.ps1" -LibraryOnly
if (!$StyledOnly) {
    $baselineGallery = (Resolve-Path "$BaselineDirectory\xui_gallery.exe").Path
    $changedGallery = (Resolve-Path "$ChangedDirectory\xui_gallery.exe").Path
    $baselineProbePath = (Resolve-Path $BaselineProbe).Path
    $changedProbePath = (Resolve-Path $ChangedProbe).Path
    $baselineCollectionsPath = (Resolve-Path $BaselineCollections).Path
    $changedCollectionsPath = (Resolve-Path "$ChangedDirectory\xui_performance_tests.exe").Path
}
$styledWindowPath = (Resolve-Path "$ChangedDirectory\xui_styling_window_tests.exe").Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$output = (Resolve-Path $OutputDirectory).Path
$deadline = [DateTime]::UtcNow.AddMinutes(8)
$records = [System.Collections.Generic.List[object]]::new()
$probes = [System.Collections.Generic.List[object]]::new()
$interference = [System.Collections.Generic.List[object]]::new()

function Assert-Quiet {
    if ([DateTime]::UtcNow -ge $deadline) { throw "The eight-minute measurement batch limit expired." }
    $active = @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -in @("cmake.exe", "cl.exe", "link.exe") -or
        $_.Name -like "xui*tests.exe" -or
        ($_.Name -eq "MSBuild.exe" -and $_.CommandLine -notlike "*/nodemode:*")
    })
    if ($active.Count) {
        foreach ($item in $active) {
            $interference.Add([pscustomobject]@{
                timestamp = [DateTime]::UtcNow.ToString("o")
                process_id = $item.ProcessId
                name = $item.Name
                command = $item.CommandLine
            })
        }
        throw "Another build or desktop test is active. The measurement batch stopped."
    }
}
function Run-Probe([string]$executable, [string[]]$arguments, [string]$file) {
    Assert-Quiet
    Write-Output "Run $file : $($arguments -join ' ')"
    $started = [DateTime]::UtcNow.ToString("o")
    $hash = (Get-FileHash -LiteralPath $executable).Hash
    & $executable @arguments 2> "$output\$file.stderr" | Set-Content -LiteralPath "$output\$file"
    $exitCode = $LASTEXITCODE
    $probes.Add([pscustomobject]@{
        executable = $executable; sha256 = $hash; arguments = $arguments
        output = $file; started = $started; completed = [DateTime]::UtcNow.ToString("o"); exit_code = $exitCode
    })
    if ($exitCode) { throw "$executable failed with exit code $exitCode. See $output\$file.stderr." }
    Assert-Quiet
}
function Measure-Gallery($executable, $phase, $run) {
    Assert-Quiet
    $process = Start-Process -FilePath $executable -ArgumentList "--page buttons" -PassThru
    $window = [IntPtr]::Zero
    try {
        $ready = [Diagnostics.Stopwatch]::StartNew()
        do {
            if ($process.HasExited -or $ready.Elapsed.TotalSeconds -gt 15) { throw "The gallery did not become ready." }
            Start-Sleep -Milliseconds 10
            $window = [WindowMemoryProbe]::Window($process.Id)
        } while ($window -eq [IntPtr]::Zero -or [WindowMemoryProbe]::Metric($window,0) -lt 1)
        $outer = [WindowMemoryProbe+Rect]::new()
        $client = [WindowMemoryProbe+Rect]::new()
        if (![WindowMemoryProbe]::GetWindowRect($window,[ref]$outer) -or
            ![WindowMemoryProbe]::GetClientRect($window,[ref]$client)) { throw "Cannot read the owned gallery bounds." }
        $width = 800 + $outer.right - $outer.left - $client.right
        $height = 780 + $outer.bottom - $outer.top - $client.bottom
        if (![WindowMemoryProbe]::SetWindowPos($window,[IntPtr](-1),20,20,$width,$height,0x10)) {
            throw "Cannot resize the owned gallery."
        }
        [WindowMemoryProbe]::Foreground($window,$process.Id)
        Start-Sleep -Milliseconds 500
        for ($i = 0; $i -lt 10; ++$i) {
            [WindowMemoryProbe]::Measure($process.Handle,$window,$window,0,[IntPtr]::Zero,$false) | Out-Null
        }
        $paints = [System.Collections.Generic.List[object]]::new()
        for ($i = 0; $i -lt 200; ++$i) {
            if ([WindowMemoryProbe]::GetForegroundWindow() -ne $window) { throw "The owned gallery lost foreground during measurement." }
            $paints.Add([WindowMemoryProbe]::Measure($process.Handle,$window,$window,0,[IntPtr]::Zero,$false))
        }
        $idlePaints = [WindowMemoryProbe]::Metric($window,0)
        Start-Sleep -Milliseconds 1000
        if ([WindowMemoryProbe]::GetForegroundWindow() -ne $window) { throw "The owned gallery lost foreground during idle measurement." }
        $process.Refresh()
        $records.Add([pscustomobject]@{
            phase = $phase; run = $run; executable = $executable
            sha256 = (Get-FileHash -LiteralPath $executable).Hash
            dpi = [WindowMemoryProbe]::GetDpiForWindow($window)
            private_bytes = $process.PrivateMemorySize64
            process_handles = $process.HandleCount
            user = [WindowMemoryProbe]::GetGuiResources($process.Handle,1)
            gdi = [WindowMemoryProbe]::GetGuiResources($process.Handle,0)
            targets = [WindowMemoryProbe]::Metric($window,11)
            peers = [WindowMemoryProbe]::Metric($window,14)
            idle_paints = [WindowMemoryProbe]::Metric($window,0) - $idlePaints
            native_buffer_bytes = [WindowMemoryProbe]::Metric($window,30)
            native_bitmap_bytes = [WindowMemoryProbe]::Metric($window,31)
            paints = $paints.ToArray()
        })
    } finally {
        if (!$process.HasExited) {
            if ($window -ne [IntPtr]::Zero) { [WindowMemoryProbe]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null }
            if (!$process.WaitForExit(5000)) { Stop-Process -Id $process.Id }
        }
        $process.Dispose()
    }
    Assert-Quiet
}

$previousDpi = [WindowMemoryProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))
$completed = $false
$failure = $null
try {
    if (!$StyledOnly) {
        for ($i = 1; $i -le 7; ++$i) {
            if ($i % 2) {
                Run-Probe $baselineProbePath @() "baseline-button-$i.txt"
                Run-Probe $changedProbePath @() "changed-button-$i.txt"
            } else {
                Run-Probe $changedProbePath @() "changed-button-$i.txt"
                Run-Probe $baselineProbePath @() "baseline-button-$i.txt"
            }
        }
        for ($i = 1; $i -le 5; ++$i) {
            if ($i % 2) {
                Run-Probe $baselineCollectionsPath @("--benchmark") "baseline-collection-$i.txt"
                Run-Probe $changedCollectionsPath @("--benchmark") "changed-collection-$i.txt"
            } else {
                Run-Probe $changedCollectionsPath @("--benchmark") "changed-collection-$i.txt"
                Run-Probe $baselineCollectionsPath @("--benchmark") "baseline-collection-$i.txt"
            }
        }
        for ($i = 1; $i -le $WindowPairs; ++$i) {
            if ($i % 2) {
                Measure-Gallery $baselineGallery "baseline" $i
                Measure-Gallery $changedGallery "changed" $i
            } else {
                Measure-Gallery $changedGallery "changed" $i
                Measure-Gallery $baselineGallery "baseline" $i
            }
        }
    }
    Run-Probe $styledWindowPath @("--lower-level") "styled-lower-level.txt"
    for ($i = 1; $i -le $StyledRuns; ++$i) {
        $order = if ($i % 2) { @("--benchmark") } else { @("--benchmark", "--styled-first") }
        Run-Probe $styledWindowPath $order "styled-window-$i.txt"
    }
    $completed = $true
} catch {
    $failure = $_.Exception.Message
    throw
} finally {
    [WindowMemoryProbe]::SetThreadDpiAwarenessContext($previousDpi) | Out-Null
    [pscustomobject]@{
        timestamp = [DateTime]::UtcNow.ToString("o")
        completed = $completed
        failure = $failure
        samples = $records.ToArray()
        probes = $probes.ToArray()
        interference = $interference.ToArray()
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$output\gallery.json"
}
Write-Output "Measurement-only batch completed. Results: $output"
