param(
    [string]$Prototype = "build\x64\Release\xui_composition_probe.exe",
    [string]$Baseline = "",
    [string]$Output = "build\composition-measurements",
    [ValidateRange(1, 50)][int]$Runs = 8,
    [string[]]$Sizes = @("800x600", "1600x1000"),
    [ValidateRange(0, 200)][int]$ResizeCycles = 40,
    [ValidateRange(0, 60)][int]$HoldSeconds = 8
)
# Measures the experimental composition presentation against classic presentation, and the
# classic path against an optional baseline build without composition support.
# Each probe process measures one configuration; configurations alternate in ABBA order.
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $Output | Out-Null
Remove-Item "$Output\samples.jsonl", "$Output\holds.jsonl" -ErrorAction SilentlyContinue
$prototype = (Resolve-Path $Prototype).Path
$baselinePath = if ($Baseline) { (Resolve-Path $Baseline).Path } else { $null }

function Invoke-Probe([string]$exe, [string[]]$arguments) {
    $stdout = & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe $($arguments -join ' ') exited with $LASTEXITCODE" }
    $line = @($stdout | Where-Object { $_ -match '^\{"kind"' })[-1]
    if (!$line) { throw "No result from $exe $($arguments -join ' ')" }
    return $line | ConvertFrom-Json
}

$records = [System.Collections.Generic.List[object]]::new()
$configurations = @(
    @{ name = "classic"; exe = $prototype; kind = "classic" },
    @{ name = "classic-acrylic"; exe = $prototype; kind = "classic-acrylic" },
    @{ name = "composition"; exe = $prototype; kind = "composition" },
    @{ name = "composition-card"; exe = $prototype; kind = "composition-card" }
)
if ($baselinePath) {
    $configurations += @{ name = "baseline-classic"; exe = $baselinePath; kind = "classic" }
    $configurations += @{ name = "baseline-classic-acrylic"; exe = $baselinePath; kind = "classic-acrylic" }
}
foreach ($size in $Sizes) {
    if ($size -notmatch '^(\d+)x(\d+)$') { throw "Invalid size $size" }
    $width = $Matches[1]; $height = $Matches[2]
    for ($run = 0; $run -lt $Runs; ++$run) {
        $order = if ($run % 2 -eq 0) { $configurations } else { [array]::Reverse(($copy = @($configurations))); $copy }
        foreach ($configuration in $order) {
            $result = Invoke-Probe $configuration.exe @($configuration.kind, $width, $height, $ResizeCycles)
            $result | Add-Member configuration $configuration.name
            $result | Add-Member run $run
            $records.Add($result)
            # Keep partial results if a later phase fails.
            ($result | ConvertTo-Json -Compress) | Add-Content -Encoding utf8 "$Output\samples.jsonl"
            Write-Host ("{0,-26} {1,9} run {2}: paint {3,6:N2} mcycles, resize p95 {4,6:N1} ms, private {5,6:N1} MiB" -f
                $configuration.name, $size, $run, $result.paint_mcycles, $result.resize_p95_ms, $result.shown_private_mib)
        }
    }
}

# DWM cost: an animated window sits behind each configuration while counters sample GPU use.
$dwm = Get-Process dwm | Select-Object -First 1
$holds = [System.Collections.Generic.List[object]]::new()
if ($HoldSeconds -gt 0) {
    $dpi = 96
    Add-Type -Name Dpi -Namespace XuiMeasure -MemberDefinition '[DllImport("user32.dll")] public static extern uint GetDpiForSystem(); [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
    [XuiMeasure.Dpi]::SetProcessDPIAware() | Out-Null
    $dpi = [XuiMeasure.Dpi]::GetDpiForSystem()
    $scale = $dpi / 96.0
    foreach ($size in $Sizes) {
        $null = $size -match '^(\d+)x(\d+)$'
        $width = [int]$Matches[1]; $height = [int]$Matches[2]
        $holdConfigurations = @("none") + @($configurations | Where-Object { $_.exe -eq $prototype } | ForEach-Object { $_.kind })
        for ($run = 0; $run -lt [Math]::Max(2, [int]($Runs / 2)); ++$run) {
            $order = if ($run % 2 -eq 0) { $holdConfigurations } else { [array]::Reverse(($copy = @($holdConfigurations))); $copy }
            foreach ($kind in $order) {
                $animator = Start-Process $prototype -ArgumentList "animate", "0", "0", ([int](($width + 80) * $scale)), ([int](($height + 80) * $scale)), ($HoldSeconds + 6) -PassThru
                Start-Sleep -Milliseconds 800
                $probe = $null; $stdout = "$Output\hold-$kind-$run.txt"
                if ($kind -ne "none") {
                    $probe = Start-Process $prototype -ArgumentList $kind, $width, $height, 0, $HoldSeconds -PassThru -RedirectStandardOutput $stdout
                    $deadline = [DateTime]::Now.AddSeconds(15)
                    while (!(Test-Path $stdout) -or !(Select-String -Path $stdout -Pattern '"ready"' -Quiet)) {
                        if ([DateTime]::Now -gt $deadline -or $probe.HasExited) { throw "Hold probe $kind did not become ready." }
                        Start-Sleep -Milliseconds 50
                    }
                }
                Start-Sleep -Milliseconds 500
                $samples = Get-Counter "\GPU Engine(*engtype_3D)\Utilization Percentage" -SampleInterval 1 -MaxSamples ([Math]::Max(1, $HoldSeconds - 2)) |
                    ForEach-Object { ($_.CounterSamples | Measure-Object CookedValue -Sum).Sum }
                $mean = ($samples | Measure-Object -Average).Average
                $result = [pscustomobject]@{ kind = $kind; size = $size; run = $run; system_gpu_3d_percent = $mean; on_top = $null; backdrop_visuals = $null }
                if ($probe) {
                    $probe.WaitForExit(($HoldSeconds + 10) * 1000) | Out-Null
                    $line = @(Get-Content $stdout | Where-Object { $_ -match '^\{"kind"' })[-1]
                    if ($line) { $line | ConvertFrom-Json | ForEach-Object { $_.PSObject.Properties | ForEach-Object { $result | Add-Member $_.Name $_.Value -Force } } }
                }
                $animator.WaitForExit(($HoldSeconds + 10) * 1000) | Out-Null
                if (!$animator.HasExited) { $animator.Kill() }
                $holds.Add($result)
                ($result | ConvertTo-Json -Compress) | Add-Content -Encoding utf8 "$Output\holds.jsonl"
                Write-Host ("hold {0,-18} {1,9} run {2}: GPU 3D {3,6:N2}%  on_top={4} visuals={5}" -f $kind, $size, $run, $mean, $result.on_top, $result.backdrop_visuals)
            }
        }
    }
}

$prototypeDll = Join-Path (Split-Path $prototype) "xui.dll"
$baselineDll = if ($baselinePath) { Join-Path (Split-Path $baselinePath) "xui.dll" } else { $null }
[pscustomobject]@{
    timestamp = (Get-Date -Format o)
    prototype = $prototype; prototype_sha256 = (Get-FileHash $prototype -Algorithm SHA256).Hash
    baseline = $baselinePath
    prototype_dll_bytes = if (Test-Path $prototypeDll) { (Get-Item $prototypeDll).Length } else { $null }
    baseline_dll_bytes = if ($baselineDll -and (Test-Path $baselineDll)) { (Get-Item $baselineDll).Length } else { $null }
    os_build = [Environment]::OSVersion.Version.ToString()
    gpu = (Get-CimInstance Win32_VideoController | Select-Object -First 1).Name
    logical_processors = [Environment]::ProcessorCount
    runs = $Runs; resize_cycles = $ResizeCycles; hold_seconds = $HoldSeconds
    note = "Sizes are client DIPs. Each probe process measures one configuration. ABBA ordering. paint_mcycles is process cycles per synchronous full repaint. resize_* times one SetWindowPos plus synchronous repaint. system_gpu_3d_percent sums 3D engine utilization across all processes while a 60 Hz GDI animation sits behind the window; compare with kind none."
    samples = $records
    holds = $holds
} | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 "$Output\measurements.json"
"Wrote $Output\measurements.json"
