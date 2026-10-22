param(
    [Parameter(Mandatory)][ValidateSet('Build', 'Measure')][string]$Mode,
    [Parameter(Mandatory)][string]$BaselineSource,
    [Parameter(Mandatory)][string]$BaselineBuild,
    [Parameter(Mandatory)][string]$CandidateBuild,
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    [ValidateRange(2, 100)][int]$Warmups = 2,
    [ValidateRange(7, 100)][int]$Samples = 7,
    [switch]$CandidateFirst
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\scripts\Release.Common.ps1"
$root = Split-Path $PSScriptRoot
$baseline = [IO.Path]::GetFullPath($BaselineBuild, $root)
$candidate = [IO.Path]::GetFullPath($CandidateBuild, $root)
$evidence = [IO.Path]::GetFullPath($EvidenceDirectory, $root)
foreach ($build in @($baseline, $candidate)) {
    if ($evidence.Equals($build, [StringComparison]::OrdinalIgnoreCase) -or
        $evidence.StartsWith("$build\", [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Evidence must not modify either revision build directory.'
    }
}
$revisions = @(
    @{ Name = 'baseline'; Build = $baseline; Source = [IO.Path]::GetFullPath($BaselineSource, $root) },
    @{ Name = 'candidate'; Build = $candidate; Source = $root }
)
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
if ($Mode -eq 'Build') {
    $hashes = @()
    foreach ($revision in $revisions) {
        $output = Join-Path $evidence $revision.Name
        New-Item -ItemType Directory -Force -Path $output | Out-Null
        $object = Join-Path $output 'collection_host_probe.obj'
        $binary = Join-Path $output 'collection_host_probe.exe'
        $compile = Join-Path $output 'compile.rsp'
        $link = Join-Path $output 'link.rsp'
        $source = Join-Path $PSScriptRoot 'collection_host_probe.cpp'
        $include = Join-Path $revision.Source 'include'
        $library = Join-Path $revision.Build 'Release'
        $resource = Join-Path $baseline 'xui_collections_window_tests.dir\Release\xui.res'
        foreach ($file in @($source, $resource, "$library\xui_core.lib", "$library\xui_windows.lib")) {
            if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing frozen input: $file" }
            $hashes += Get-FileHash -LiteralPath $file -Algorithm SHA256 |
                Select-Object @{ n = 'Revision'; e = { $revision.Name } }, Path, Hash
        }
        @"
/c /I"$include" /nologo /W4 /WX- /diagnostics:column /O2 /Ob2 /GL /D _UNICODE /D UNICODE /D WIN32 /D _WINDOWS /D NDEBUG /D NOMINMAX /D WIN32_LEAN_AND_MEAN /D _WIN32_WINNT=0x0A00 /EHsc /MT /GS /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /std:c++20 /permissive- /Fo"$object" /external:W4 /Gd /TP /utf-8 "$source"
"@ | Set-Content -LiteralPath $compile -Encoding utf8
        @"
/OUT:"$binary" /NOLOGO "$library\xui_windows.lib" "$library\xui_core.lib" PSAPI.LIB D2D1.LIB DXGUID.LIB DWRITE.LIB DWMAPI.LIB DCOMP.LIB UIAUTOMATIONCORE.LIB OLE32.LIB OLEAUT32.LIB COMCTL32.LIB SHELL32.LIB USER32.LIB GDI32.LIB WINDOWSCODECS.LIB UXTHEME.LIB MFUUID.LIB RUNTIMEOBJECT.LIB KERNEL32.LIB WINSPOOL.LIB UUID.LIB COMDLG32.LIB ADVAPI32.LIB /MANIFEST:NO /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF /LTCG /DYNAMICBASE /NXCOMPAT /MACHINE:X64 "$resource" "$object"
"@ | Set-Content -LiteralPath $link -Encoding utf8
        Invoke-XuiVcVarsCommand x64 "cl /Bv @`"$compile`" && link @`"$link`"" 2>&1 |
            Tee-Object -FilePath (Join-Path $output 'build.log') | Write-Output
        $hashes += Get-FileHash -LiteralPath $binary -Algorithm SHA256 |
            Select-Object @{ n = 'Revision'; e = { $revision.Name } }, Path, Hash
    }
    $hashes | Export-Csv -LiteralPath (Join-Path $evidence 'inputs-and-binaries.csv') -NoTypeInformation
    return
}
# Reserve a quiet block with other users/coordinators before invoking Measure.
$records = @()
for ($index = 0; $index -lt $Warmups + $Samples; ++$index) {
    $order = if ([bool]($index % 2) -xor $CandidateFirst.IsPresent) {
        @($revisions[1], $revisions[0])
    } else { $revisions }
    foreach ($revision in $order) {
        $kind = if ($index -lt $Warmups) { 'warmup' } else { 'recorded' }
        $name = '{0}-{1:D2}-{2}' -f $kind, $index, $revision.Name
        $stdout = Join-Path $evidence "$name.stdout.log"
        $stderr = Join-Path $evidence "$name.stderr.log"
        $binary = Join-Path $evidence "$($revision.Name)\collection_host_probe.exe"
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $startedUtc = [DateTime]::UtcNow.ToString('o')
        $process = Start-Process -FilePath $binary -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $exited = $process.WaitForExit(90000)
        $terminationErrors = @()
        if (!$exited) {
            Stop-Process -Id $process.Id -ErrorAction Continue -ErrorVariable terminationErrors
            if (!$process.WaitForExit(10000)) {
                [pscustomobject]@{
                    Revision = $revision.Name; Kind = $kind; Round = $index
                    StartedUtc = $startedUtc; CompletedUtc = [DateTime]::UtcNow.ToString('o')
                    TimedOut = $true; TerminationUnconfirmed = $true; ProcessId = $process.Id
                    TerminationErrors = @($terminationErrors | ForEach-Object { $_.ToString() })
                    Stdout = $stdout; Stderr = $stderr
                } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence "$name.termination-failure.json")
                throw "Timed-out probe termination unconfirmed: $name (failure record and logs retained)"
            }
        }
        $clock.Stop()
        $process.Refresh()
        $record = [ordered]@{
            Revision = $revision.Name; Kind = $kind; Round = $index
            StartedUtc = $startedUtc; CompletedUtc = [DateTime]::UtcNow.ToString('o')
            ExitCode = $process.ExitCode; TimedOut = !$exited
            TerminationErrors = @($terminationErrors | ForEach-Object { $_.ToString() })
            ProcessWallMilliseconds = $clock.Elapsed.TotalMilliseconds
            ProcessCpuMilliseconds = $process.TotalProcessorTime.TotalMilliseconds
            BinarySha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
            Stdout = $stdout; Stderr = $stderr
            Phases = @()
        }
        $records += [pscustomobject]$record
        $records | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidence 'samples.json') -Encoding utf8
        if (!$exited -or $process.ExitCode -ne 0) { throw "Probe failed: $name (raw logs retained)" }
        $record.Phases = @(Get-Content -LiteralPath $stdout | ConvertFrom-Json)
        $records[-1].Phases = $record.Phases
        $records | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidence 'samples.json') -Encoding utf8
        $expected = @{
            'scroll-sync' = 32768
            'selection-publication' = 8192
            'source-publication' = 8192
            'paint-visible-rows' = 256
        }
        if ($record.Phases.Count -ne 5 -or @($record.Phases | Where-Object phase -eq 'retained').Count -ne 1) {
            throw "Incomplete probe output: $name"
        }
        foreach ($phaseName in $expected.Keys) {
            $phase = @($record.Phases | Where-Object phase -eq $phaseName)
            if ($phase.Count -ne 1) { throw "Missing/duplicate phase $phaseName in $name" }
            $phase = $phase[0]
            if ($phase.iterations -ne $expected[$phaseName] -or $phase.updates -ne $phase.iterations -or
                $phase.item_reads -lt $phase.iterations -or $phase.grid_keys -lt $phase.iterations -or
                $phase.checksum -le 0 -or $phase.cpp_allocations -lt 0 -or $phase.wall_ms -le 0 -or
                $phase.kernel_after100ns -lt $phase.kernel_before100ns -or
                $phase.user_after100ns -lt $phase.user_before100ns) {
                throw "Invalid actual phase work/counters: $phaseName in $name"
            }
            $cpu = (($phase.kernel_after100ns - $phase.kernel_before100ns) +
                ($phase.user_after100ns - $phase.user_before100ns)) / 10000.0
            if ($cpu -le 0 -or [Math]::Abs($cpu - $phase.ui_thread_cpu_ms) -gt 0.000001) {
                throw "Raw/derived owning-thread CPU mismatch: $phaseName in $name"
            }
            if ($phaseName -eq 'selection-publication' -and
                ($phase.item_callbacks -ne 8192 -or $phase.grid_callbacks -ne 8192 -or
                 $phase.selected_item -ne 106484 -or $phase.selected_grid -ne 39248)) {
                throw "Incomplete selection effects in $name"
            }
            if ($phaseName -eq 'source-publication' -and $phase.source_transitions -ne 16384) {
                throw "Incomplete source identity transitions in $name"
            }
            if ($phaseName -eq 'paint-visible-rows' -and $phase.paints -ne 256) {
                throw "Incomplete painted-frame work in $name"
            }
        }
        $retained = @($record.Phases | Where-Object phase -eq 'retained')[0]
        if ($retained.items -ne 1000000 -or $retained.grid_rows -ne 100000 -or
            $retained.legacy_rows -ne 10000 -or $retained.peers -ne 4 -or $retained.child_hwnds -ne 5 -or
            $retained.targets -ne 1 -or $retained.selection_terms -ne 2 -or $retained.editor_scroll_writes -ne 0) {
            throw "Invalid retained resource/work bounds in $name"
        }
        foreach ($phaseName in $expected.Keys) {
            $reference = @($records[0].Phases | Where-Object phase -eq $phaseName)[0]
            $actual = @($record.Phases | Where-Object phase -eq $phaseName)[0]
            foreach ($counter in @('checksum', 'item_reads', 'item_keys', 'item_lookups', 'grid_reads',
                'grid_keys', 'grid_lookups', 'item_callbacks', 'grid_callbacks', 'updates',
                'selected_item', 'selected_grid', 'selected_file', 'source_transitions', 'paints')) {
                if ($actual.$counter -ne $reference.$counter) {
                    throw "Actual $phaseName $counter work changed in $name (raw record retained)"
                }
            }
        }
        $records | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidence 'samples.json') -Encoding utf8
        Write-Host "$name completed"
    }
}
