param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][ValidateSet('demo','raw','xui')][string]$Kind,
    [Parameter(Mandatory)][string]$Output,
    [string]$Fixture,
    [ValidateRange(1,10)][int]$Runs = 3,
    [ValidateRange(20,50)][int]$Cycles = 24,
    [ValidateRange(1,10)][int]$Lifetimes = 5
)
$ErrorActionPreference = 'Stop'
if (-not ('WindowMemoryProbe' -as [type])) {
    & "$PSScriptRoot\measure-window-memory.ps1" -LibraryOnly
}
if (-not ('ResizeMemoryNative' -as [type])) {
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ResizeMemoryNative {
    delegate bool EnumProc(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder text, int length);
    [DllImport("user32.dll", SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp, uint flags, uint timeout, out IntPtr result);
    public static IntPtr Root(int pid, string expectedClass) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((hwnd, data) => {
            uint actual;
            GetWindowThreadProcessId(hwnd, out actual);
            if(actual == pid && IsWindowVisible(hwnd)) {
                var name = new StringBuilder(128);
                GetClassName(hwnd, name, name.Capacity);
                if(name.ToString() == expectedClass) { result = hwnd; return false; }
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static long Send(IntPtr hwnd, int pid, uint message, int key) {
        uint actual;
        if(GetWindowThreadProcessId(hwnd,out actual)==0 || actual!=pid)
            throw new InvalidOperationException("The window does not belong to the launched process.");
        IntPtr result;
        if(SendMessageTimeout(hwnd,message,(IntPtr)key,IntPtr.Zero,2,10000,out result)==IntPtr.Zero)
            throw new System.ComponentModel.Win32Exception();
        return result.ToInt64();
    }
}
'@
}
$exe = (Resolve-Path $Executable).Path
$exeHash = (Get-FileHash $exe -Algorithm SHA256).Hash
$rootClass = if ($Kind -eq 'raw') {'Xui.MemoryProbe'} else {'Xui.Window.1'}
$outPath = [IO.Path]::GetFullPath($Output)
if (Test-Path $outPath) { throw 'The output already exists. Use a new output path.' }
New-Item -ItemType Directory -Force (Split-Path $outPath) | Out-Null
if ($Kind -eq 'demo') {
    if (!$Fixture) { throw 'The demo requires an owned fixture directory with 60 text files.' }
    $Fixture = (Resolve-Path $Fixture).Path
    $files = @(Get-ChildItem -LiteralPath $Fixture -File)
    if ($files.Count -ne 60 -or @($files | Where-Object Extension -ne '.txt').Count) {
        throw 'The demo fixture must contain exactly 60 text files.'
    }
}
$samples = [Collections.Generic.List[object]]::new()
$timings = [Collections.Generic.List[object]]::new()
$identities = [Collections.Generic.List[object]]::new()
$dpiContext = [WindowMemoryProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))
$requestedWidth = $null; $requestedHeight = $null
function Assert-Owned($p, $h) {
    if ($p.HasExited) { throw "Owned process $($p.Id) exited unexpectedly." }
    [uint32]$owner = 0
    if (![WindowMemoryProbe]::GetWindowThreadProcessId($h,[ref]$owner) -or $owner -ne $p.Id) {
        throw 'The HWND no longer belongs to the launched process.'
    }
    $name = [Text.StringBuilder]::new(128)
    if (![ResizeMemoryNative]::GetClassName($h,$name,128)) { throw 'Cannot read the owned window class.' }
    $expected = if ($Kind -eq 'raw') {'Xui.MemoryProbe'} else {'Xui.Window.1'}
    if ($name.ToString() -ne $expected) { throw "Unexpected root class: $name" }
}
function Metric($p, $h, [int]$key) {
    [ResizeMemoryNative]::Send($h,$p.Id,0x803c,$key)
}
function Wait-Window($p) {
    $wait = [Diagnostics.Stopwatch]::StartNew()
    do {
        if ($p.HasExited -or $wait.Elapsed.TotalSeconds -gt 20) { throw 'Owned root window did not appear.' }
        $h = [ResizeMemoryNative]::Root($p.Id,$rootClass)
        if ($h -ne [IntPtr]::Zero) { Assert-Owned $p $h; return $h }
        Start-Sleep -Milliseconds 1
    } while ($true)
}
function Snapshot($p, $h, [string]$phase, [int]$cycle = 0, [string]$batch = '', [int]$life = 1) {
    $at = $clock.Elapsed.TotalMilliseconds
    $p.Refresh()
    $commit = $p.PrivateMemorySize64; $ws = $p.WorkingSet64
    $regions = @([WindowMemoryProbe]::Regions($p.Handle))
    if (!$regions.Count) { throw 'VirtualQueryEx returned no process regions.' }
    $private = @($regions | Where-Object type -eq 'private' | ForEach-Object {
        [pscustomobject]@{allocation=$_.allocation; reserved_bytes=$_.reserved_bytes; committed_bytes=$_.committed_bytes}
    })
    $metrics = $null; $width = $null; $height = $null; $dpi = $null
    if ($h -ne [IntPtr]::Zero) {
        Assert-Owned $p $h
        $client = [WindowMemoryProbe+Rect]::new()
        if (![WindowMemoryProbe]::GetClientRect($h,[ref]$client)) { throw 'GetClientRect failed.' }
        $width = $client.right; $height = $client.bottom; $dpi = [WindowMemoryProbe]::GetDpiForWindow($h)
        $metrics = [ordered]@{paints=(Metric $p $h 0); targets=(Metric $p $h 11)}
        if ($Kind -eq 'raw') {
            $metrics['size_notifications'] = Metric $p $h 2
            $metrics['target_pixel_width'] = Metric $p $h 40
            $metrics['target_pixel_height'] = Metric $p $h 41
        }
        if ($Kind -ne 'raw') {
            foreach ($pair in @(@('layouts',2),@('peers',14),@('text_layouts',13),@('created_text_layouts',12),
                @('native_buffer_bytes',30),@('native_bitmap_bytes',31),@('scene_paths',32),
                @('thumbnail_slots',22),@('thumbnail_ready',23),@('file_rows',9))) {
                $metrics[$pair[0]] = Metric $p $h $pair[1]
            }
        }
    }
    $samples.Add([pscustomobject]@{
        run=$run; pid=$p.Id; hwnd=$h.ToInt64(); phase=$phase; batch=$batch; cycle=$cycle; lifetime=$life
        elapsed_ms=$at; snapshot_ms=$clock.Elapsed.TotalMilliseconds-$at
        requested_client_width=$requestedWidth; requested_client_height=$requestedHeight
        actual_client_width=$width; actual_client_height=$height; dpi=$dpi
        private_commit_bytes=$commit; total_working_set_bytes=$ws
        private_working_set_bytes=($regions | Measure-Object private_resident_bytes -Sum).Sum
        virtual_private_commit_bytes=($private | Measure-Object committed_bytes -Sum).Sum
        handles=$p.HandleCount; gdi=[WindowMemoryProbe]::GetGuiResources($p.Handle,0)
        user=[WindowMemoryProbe]::GetGuiResources($p.Handle,1); cpu_ms=$p.TotalProcessorTime.TotalMilliseconds
        metrics=$metrics; private_allocations=$private
        private_region_histogram=@($private | Where-Object committed_bytes -gt 0 |
            Group-Object committed_bytes | ForEach-Object {[pscustomobject]@{bytes=[long]$_.Name; count=$_.Count}})
    })
}
function Paint($p, $h) {
    Assert-Owned $p $h
    $before = Metric $p $h 0
    # Update layout before the synchronous paint. This message is XUI's existing update message.
    if ($Kind -ne 'raw') { [ResizeMemoryNative]::Send($h,$p.Id,0x800c,0) | Out-Null }
    if (![WindowMemoryProbe]::RedrawWindow($h,[IntPtr]::Zero,[IntPtr]::Zero,0x181)) { throw 'Owned redraw failed.' }
    if ((Metric $p $h 0) -le $before) { throw 'The owned redraw did not complete a paint.' }
}
function Resize($p, $h, [int]$width, [int]$height, [string]$batch, [int]$cycle) {
    Assert-Owned $p $h
    $outer = [WindowMemoryProbe+Rect]::new(); $client = [WindowMemoryProbe+Rect]::new()
    if (![WindowMemoryProbe]::GetWindowRect($h,[ref]$outer) -or
        ![WindowMemoryProbe]::GetClientRect($h,[ref]$client)) { throw 'Cannot read owned window bounds.' }
    $changed = $client.right -ne $width -or $client.bottom -ne $height
    $script:requestedWidth = $width; $script:requestedHeight = $height
    $cpu = [WindowMemoryProbe]::Cpu($p.Handle); $timer = [Diagnostics.Stopwatch]::StartNew()
    $layouts = Metric $p $h 2
    if (![WindowMemoryProbe]::SetWindowPos($h,[IntPtr]::Zero,0,0,
        $width+$outer.right-$outer.left-$client.right,
        $height+$outer.bottom-$outer.top-$client.bottom,0x16)) { throw 'Owned resize failed.' }
    Paint $p $h
    $timer.Stop()
    if (![WindowMemoryProbe]::GetClientRect($h,[ref]$client)) { throw 'Cannot read resized client.' }
    if ($client.right -ne $width -or $client.bottom -ne $height) {
        throw "Client constraint changed requested ${width}x$height to $($client.right)x$($client.bottom)."
    }
    if ($changed -and (Metric $p $h 2) -le $layouts) {
        throw 'Resize did not complete a size notification or XUI layout.'
    }
    if ($Kind -eq 'raw' -and ((Metric $p $h 40) -ne $width -or (Metric $p $h 41) -ne $height)) {
        throw 'The raw target pixel size does not match the client.'
    }
    $timings.Add([pscustomobject]@{run=$run; batch=$batch; cycle=$cycle; width=$width; height=$height
        wall_ms=$timer.Elapsed.TotalMilliseconds; cpu_ms=[WindowMemoryProbe]::Cpu($p.Handle)-$cpu})
}
function Heap-Snapshot($p, $h) {
    if ($Kind -ne 'demo') { [ResizeMemoryNative]::Send($h,$p.Id,0x803e,0) | Out-Null }
}
function Settle($p, $h, [string]$phase) {
    $settle = [Diagnostics.Stopwatch]::StartNew()
    foreach ($seconds in @(0.5,2,10,30)) {
        $remaining = $seconds*1000-$settle.Elapsed.TotalMilliseconds
        if ($remaining -gt 0) { Start-Sleep -Milliseconds ([int]$remaining) }
        Snapshot $p $h "$phase-$seconds"
    }
    Heap-Snapshot $p $h
}
$failure = $null
try {
    for ($run=1; $run -le $Runs; ++$run) {
        $requestedWidth=$null; $requestedHeight=$null
        $arguments = if ($Kind -eq 'demo') {'"' + $Fixture + '"'} else {
            "$(if ($Kind -eq 'raw') {'rounded'} else {'xui'}) 800 600 0 0 0 0 $Lifetimes"
        }
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $p = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru `
            -RedirectStandardOutput "$outPath-run$run.stdout.txt" -RedirectStandardError "$outPath-run$run.stderr.txt"
        $h = [IntPtr]::Zero
        try {
            Snapshot $p $h 'launch-first-accessible-process'
            $h = Wait-Window $p
            $identities.Add([pscustomobject]@{run=$run; pid=$p.Id; hwnd=$h.ToInt64(); executable=$p.Path})
            Snapshot $p $h 'first-observed-window'
            $wait = [Diagnostics.Stopwatch]::StartNew()
            while ((Metric $p $h 0) -lt 1) {
                if ($wait.Elapsed.TotalSeconds -gt 20) { throw 'No first paint.' }
                Start-Sleep -Milliseconds 1
            }
            Snapshot $p $h 'first-completed-paint'
            Start-Sleep -Seconds 1
            Snapshot $p $h 'initial-1-second'
            Start-Sleep -Seconds 9
            if ($Kind -eq 'demo' -and (Metric $p $h 9) -ne 60) { throw 'Demo did not load the 60-file fixture.' }
            Snapshot $p $h 'initial-10-seconds'
            Resize $p $h 800 600 'warm-control' 0
            for ($paint=0; $paint -lt 30; ++$paint) { Paint $p $h }
            Snapshot $p $h 'small-after-30-same-size-paints'
            Heap-Snapshot $p $h
            Resize $p $h 1100 850 'first-enlarge' 0
            Snapshot $p $h 'first-large'
            Heap-Snapshot $p $h
            Resize $p $h 800 600 'first-shrink' 0
            Snapshot $p $h 'first-shrink'
            foreach ($batch in @('fixed','varied')) {
                for ($cycle=1; $cycle -le $Cycles; ++$cycle) {
                    if ($batch -eq 'fixed') { Resize $p $h 1100 850 $batch $cycle }
                    else {
                        Resize $p $h 1400 500 $batch $cycle
                        Resize $p $h 800 1000 $batch $cycle
                        Resize $p $h (900+($cycle%8)*23) (660+($cycle%7)*19) $batch $cycle
                    }
                    Resize $p $h 800 600 $batch $cycle
                    Start-Sleep -Milliseconds 40
                    Snapshot $p $h 'post-shrink' $cycle $batch
                }
                Heap-Snapshot $p $h
                Snapshot $p $h "$batch-complete"
            }
            Settle $p $h 'final-small-idle'
            if ($Kind -eq 'raw') {
                [ResizeMemoryNative]::Send($h,$p.Id,0x803f,0) | Out-Null
                Settle $p $h 'target-released-factory-retained'
                [ResizeMemoryNative]::Send($h,$p.Id,0x8040,0) | Out-Null
                Snapshot $p $h 'small-target-recreated'
                Resize $p $h 1100 850 'release-experiment' 0
                Resize $p $h 800 600 'release-experiment' 0
                [ResizeMemoryNative]::Send($h,$p.Id,0x803d,0) | Out-Null
                Settle $p $h 'target-and-factory-released'
                [ResizeMemoryNative]::Send($h,$p.Id,0x8040,0) | Out-Null
                Snapshot $p $h 'factory-and-small-target-recreated'
            }
            if ($Kind -ne 'demo') {
                for ($life=2; $life -le $Lifetimes; ++$life) {
                    Assert-Owned $p $h
                    if (![WindowMemoryProbe]::PostMessage($h,0x10,[IntPtr]::Zero,[IntPtr]::Zero)) { throw 'Close failed.' }
                    $wait = [Diagnostics.Stopwatch]::StartNew()
                    while ([ResizeMemoryNative]::Root($p.Id,$rootClass) -ne [IntPtr]::Zero) {
                        if ($wait.Elapsed.TotalSeconds -gt 10) { throw 'Root was not destroyed.' }
                        Start-Sleep -Milliseconds 10
                    }
                    Start-Sleep -Milliseconds 300
                    Snapshot $p ([IntPtr]::Zero) 'between-root-lifetimes' 0 '' $life
                    $h = Wait-Window $p
                    $identities.Add([pscustomobject]@{run=$run; lifetime=$life; pid=$p.Id; hwnd=$h.ToInt64(); executable=$p.Path})
                    Paint $p $h
                    Snapshot $p $h 'reopened-small' 0 '' $life
                    Resize $p $h 1100 850 'lifetime' $life
                    Resize $p $h 800 600 'lifetime' $life
                    Start-Sleep -Milliseconds 500
                    Snapshot $p $h 'reopened-post-shrink' 0 '' $life
                }
            }
        } finally {
            if (!$p.HasExited) {
                $h = [ResizeMemoryNative]::Root($p.Id,$rootClass)
                if ($h -ne [IntPtr]::Zero) {
                    Assert-Owned $p $h
                    [WindowMemoryProbe]::PostMessage($h,0x10,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
                }
                if (!$p.WaitForExit(10000)) { Stop-Process -Id $p.Id; $p.WaitForExit(); if (!$failure) {$failure='Owned process required forced cleanup.'} }
            }
            if ($p.ExitCode -ne 0 -and !$failure) { $failure="Owned process exit code $($p.ExitCode)." }
            $p.Dispose()
        }
        if ($failure) { throw $failure }
        if ((Get-FileHash $exe -Algorithm SHA256).Hash -ne $exeHash) { throw 'Executable changed during measurement.' }
    }
} catch {
    $failure = $_.ToString()
    throw
} finally {
    [WindowMemoryProbe]::SetThreadDpiAwarenessContext($dpiContext) | Out-Null
    [ordered]@{
        schema=1; captured_utc=[DateTime]::UtcNow.ToString('o'); status=if($failure){'failed'}else{'complete'}
        error=$failure; executable=$exe; sha256=$exeHash
        kind=$Kind; runs=$Runs; cycles_per_batch=$Cycles; lifetimes=$Lifetimes
        protocol='Physical client pixels; synchronous layout and completed redraw; no foreground input, trimming, compaction, or forced collection.'
        startup_limit='First accessible process sample is not guaranteed to precede paint; raw probe stdout separately records entry and prepaint heaps.'
        identities=$identities.ToArray(); samples=$samples.ToArray(); resize_timings=$timings.ToArray()
    } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $outPath -Encoding utf8
}
