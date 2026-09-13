param(
    [string]$Executable = "build\arm64\Release\xui_task_manager.exe",
    [string]$Output = "build\task-manager\memory-investigation\size-sweep.json",
    [string]$Arguments = "",
    [string[]]$Sizes = @("800x780", "1000x780", "1024x780", "1040x780", "1080x780", "1200x780", "800x780"),
    [ValidateRange(1,20)][int]$Runs = 3,
    [switch]$TaskManager,
    [switch]$Gallery,
    [switch]$ResourceMetrics,
    [switch]$Regions,
    [switch]$CaptureOutput,
    [switch]$ReleaseProbeTarget,
    [switch]$Performance,
    [switch]$ForceInputPaint,
    [switch]$LibraryOnly
)
$ErrorActionPreference = "Stop"
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class WindowMemoryProbe {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
    [StructLayout(LayoutKind.Sequential)] struct Memory {
        public ulong BaseAddress, AllocationBase;
        public uint AllocationProtect, Alignment;
        public ulong RegionSize;
        public uint State, Protect, Type, Padding;
    }
    public class Region {
        public string allocation, type, mapped_file;
        public long reserved_bytes, committed_bytes, resident_bytes, private_resident_bytes;
    }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc fn, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc fn, IntPtr data);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder value, int count);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint from, uint to, bool attach);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flag);
    [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr hwnd, IntPtr rect, IntPtr region, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("kernel32.dll", SetLastError=true)] static extern UIntPtr VirtualQueryEx(IntPtr process, IntPtr address, out Memory memory, UIntPtr bytes);
    [DllImport("psapi.dll", CharSet=CharSet.Unicode)] static extern uint GetMappedFileName(IntPtr process, IntPtr address, StringBuilder name, int size);
    [DllImport("psapi.dll", SetLastError=true)] static extern bool QueryWorkingSet(IntPtr process, IntPtr buffer, int bytes);
    [DllImport("kernel32.dll")] static extern bool GetProcessTimes(IntPtr process, out long created, out long exited, out long kernel, out long user);
    [DllImport("kernel32.dll")] static extern bool QueryProcessCycleTime(IntPtr process, out ulong cycles);
    public class Timing {
        public double wall_ms, cpu_ms;
        public ulong cycles;
    }
    public static double Cpu(IntPtr process) {
        long created, exited, kernel, user;
        if (!GetProcessTimes(process, out created, out exited, out kernel, out user))
            throw new System.ComponentModel.Win32Exception();
        return (kernel+user)/10000.0;
    }
    public static ulong Cycles(IntPtr process) {
        ulong result;
        if (!QueryProcessCycleTime(process, out result)) throw new System.ComponentModel.Win32Exception();
        return result;
    }
    public static void Foreground(IntPtr window, int process) {
        uint owner;
        uint thread=GetWindowThreadProcessId(window,out owner);
        if(owner!=process) throw new InvalidOperationException("Foreground target is not owned by the probe.");
        uint current=GetCurrentThreadId();
        uint foreground=GetWindowThreadProcessId(GetForegroundWindow(),out owner);
        bool joined=foreground!=0 && foreground!=current && AttachThreadInput(current,foreground,true);
        bool joinedOwner=thread!=current && thread!=foreground && AttachThreadInput(current,thread,true);
        try { SetForegroundWindow(window); }
        finally {
            if(joinedOwner) AttachThreadInput(current,thread,false);
            if(joined) AttachThreadInput(current,foreground,false);
        }
        if(GetForegroundWindow()!=window) throw new InvalidOperationException("Owned performance window is not foreground.");
    }
    public static Timing Measure(IntPtr process, IntPtr root, IntPtr input, uint message, IntPtr wp, bool forceUpdate) {
        var paints=Metric(root,0);
        var cpu=Cpu(process); var cycles=Cycles(process);
        var timer=System.Diagnostics.Stopwatch.StartNew();
        if (forceUpdate) {
            if(message!=0) SendMessage(input,message,wp,IntPtr.Zero);
            SendMessage(root,0x800c,IntPtr.Zero,IntPtr.Zero);
            if(!RedrawWindow(root,IntPtr.Zero,IntPtr.Zero,0x181)) throw new System.ComponentModel.Win32Exception();
        } else if (message!=0) {
            SendMessage(input,message,wp,IntPtr.Zero);
            while(Metric(root,0)==paints) {
                if(timer.Elapsed.TotalSeconds>5) throw new InvalidOperationException("Input did not produce a completed paint.");
                System.Threading.Thread.Sleep(1);
            }
        } else if (!RedrawWindow(root,IntPtr.Zero,IntPtr.Zero,0x181)) throw new System.ComponentModel.Win32Exception();
        timer.Stop();
        return new Timing {wall_ms=timer.Elapsed.TotalMilliseconds,cpu_ms=Cpu(process)-cpu,cycles=Cycles(process)-cycles};
    }
    public static IntPtr Window(int process) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((h,d)=> { uint pid; GetWindowThreadProcessId(h,out pid);
            if (pid==process && IsWindowVisible(h)) { found=h; return false; } return true;
        },IntPtr.Zero);
        return found;
    }
    public static long Metric(IntPtr root, int key) { return SendMessage(root,0x803c,(IntPtr)key,IntPtr.Zero).ToInt64(); }
    public static IntPtr Child(IntPtr root, string name) {
        IntPtr found=IntPtr.Zero;
        EnumChildWindows(root,(h,d)=> {
            var text=new StringBuilder(256); GetWindowText(h,text,256);
            if(text.ToString()==name) { found=h; return false; } return true;
        },IntPtr.Zero);
        if(found==IntPtr.Zero) throw new InvalidOperationException("Missing child: "+name);
        return found;
    }
    public static Region[] Regions(IntPtr process) {
        var result=new List<Region>();
        var bases=new List<ulong>();
        var ends=new List<ulong>();
        var owners=new List<Region>();
        Region current=null;
        for(ulong address=0;;) {
            Memory m;
            if(VirtualQueryEx(process,(IntPtr)address,out m,(UIntPtr)Marshal.SizeOf<Memory>()).ToUInt64()==0) break;
            if(m.State!=0x10000) {
                string key=m.AllocationBase.ToString("x");
                if(current==null || current.allocation!=key) {
                    current=new Region {allocation=key,type=m.Type==0x20000 ? "private" : m.Type==0x40000 ? "mapped" : "image",mapped_file=""};
                    if(m.Type!=0x20000) {
                        var name=new StringBuilder(1024);
                        GetMappedFileName(process,(IntPtr)m.BaseAddress,name,name.Capacity);
                        current.mapped_file=name.ToString();
                    }
                    result.Add(current);
                }
                current.reserved_bytes+=(long)m.RegionSize;
                if(m.State==0x1000) current.committed_bytes+=(long)m.RegionSize;
                bases.Add(m.BaseAddress); ends.Add(m.BaseAddress+m.RegionSize); owners.Add(current);
            }
            ulong next=m.BaseAddress+m.RegionSize;
            if(next<=address) break;
            address=next;
        }
        for(int bytes=65536;bytes<=67108864;bytes*=2) {
            IntPtr buffer=Marshal.AllocHGlobal(bytes);
            try {
                if(!QueryWorkingSet(process,buffer,bytes)) {
                    int error=Marshal.GetLastWin32Error();
                    if(error!=24 && error!=122) throw new System.ComponentModel.Win32Exception(error);
                    continue;
                }
                long count=Marshal.ReadInt64(buffer);
                for(int i=0;i<count;++i) {
                    ulong entry=(ulong)Marshal.ReadInt64(buffer,8+i*8);
                    ulong address=entry & ~4095UL;
                    int low=0,high=bases.Count-1;
                    while(low<=high) {
                        int mid=(low+high)/2;
                        if(address<bases[mid]) high=mid-1;
                        else if(address>=ends[mid]) low=mid+1;
                        else { owners[mid].resident_bytes+=4096; if((entry&0x100)==0) owners[mid].private_resident_bytes+=4096; break; }
                    }
                }
                return result.ToArray();
            } finally { Marshal.FreeHGlobal(buffer); }
        }
        throw new InvalidOperationException("Working set probe limit");
    }
}
'@
if ($LibraryOnly) { return }
$exe = (Resolve-Path $Executable).Path
$folder = Split-Path $Output
if ($folder) { New-Item -ItemType Directory -Force $folder | Out-Null }
$records = [System.Collections.Generic.List[object]]::new()
$previousDpi = [WindowMemoryProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))
function Snapshot($process, $window, $run, $phase, $latency, $navigation = @(), $navigationCpu = 0) {
    if ($Performance -and [WindowMemoryProbe]::GetForegroundWindow() -ne $window) {
        throw "The owned window lost foreground during measurement."
    }
    if ($latency -eq 0) {
        $paintTimer=[Diagnostics.Stopwatch]::StartNew()
        for($paint=0;$paint -lt 10;++$paint) {
            [WindowMemoryProbe]::RedrawWindow($window,[IntPtr]::Zero,[IntPtr]::Zero,0x181) | Out-Null
        }
        $latency=$paintTimer.Elapsed.TotalMilliseconds/10
    }
    $process.Refresh()
    $cpuBefore=$process.TotalProcessorTime.TotalMilliseconds
    $paintsBefore=[WindowMemoryProbe]::Metric($window,0)
    $idleCycles=[WindowMemoryProbe]::Cycles($process.Handle)
    Start-Sleep -Milliseconds 1000
    $idleCycles=[WindowMemoryProbe]::Cycles($process.Handle)-$idleCycles
    if ($Performance -and [WindowMemoryProbe]::GetForegroundWindow() -ne $window) {
        throw "The owned window lost foreground during idle measurement."
    }
    $process.Refresh()
    $regions = [WindowMemoryProbe]::Regions($process.Handle)
    $outer = [WindowMemoryProbe+Rect]::new()
    $client = [WindowMemoryProbe+Rect]::new()
    [WindowMemoryProbe]::GetWindowRect($window,[ref]$outer) | Out-Null
    [WindowMemoryProbe]::GetClientRect($window,[ref]$client) | Out-Null
    $records.Add([pscustomobject]@{
        run=$run; phase=$phase; pid=$process.Id; dpi=[WindowMemoryProbe]::GetDpiForWindow($window)
        outer_width=$outer.right-$outer.left; outer_height=$outer.bottom-$outer.top
        client_width=$client.right; client_height=$client.bottom
        private_commit_bytes=$process.PrivateMemorySize64; total_working_set_bytes=$process.WorkingSet64
        private_working_set_bytes=($regions | Measure-Object private_resident_bytes -Sum).Sum
        virtual_private_commit_bytes=($regions | Where-Object type -eq private | Measure-Object committed_bytes -Sum).Sum
        virtual_mapped_commit_bytes=($regions | Where-Object type -eq mapped | Measure-Object committed_bytes -Sum).Sum
        virtual_image_commit_bytes=($regions | Where-Object type -eq image | Measure-Object committed_bytes -Sum).Sum
        handles=$process.HandleCount; threads=$process.Threads.Count
        gdi=[WindowMemoryProbe]::GetGuiResources($process.Handle,0); user=[WindowMemoryProbe]::GetGuiResources($process.Handle,1)
        targets=[WindowMemoryProbe]::Metric($window,11); paints=[WindowMemoryProbe]::Metric($window,0)
        peers=[WindowMemoryProbe]::Metric($window,14); text_layouts=[WindowMemoryProbe]::Metric($window,13)
        native_buffer_bytes=if($ResourceMetrics) {[WindowMemoryProbe]::Metric($window,30)} else {$null}
        native_bitmap_bytes=if($ResourceMetrics) {[WindowMemoryProbe]::Metric($window,31)} else {$null}
        scene_paths=if($ResourceMetrics) {[WindowMemoryProbe]::Metric($window,32)} else {$null}
        thumbnail_slots=[WindowMemoryProbe]::Metric($window,22)
        thumbnail_ready=[WindowMemoryProbe]::Metric($window,23)
        cpu_total_ms=$process.TotalProcessorTime.TotalMilliseconds
        idle_cpu_ms=$process.TotalProcessorTime.TotalMilliseconds-$cpuBefore
        idle_paints=[WindowMemoryProbe]::Metric($window,0)-$paintsBefore
        process_rows=[WindowMemoryProbe]::Metric($window,19); visible_rows=[WindowMemoryProbe]::Metric($window,18)
        file_rows=[WindowMemoryProbe]::Metric($window,9); theme=[WindowMemoryProbe]::Metric($window,6)
        paint_latency_ms=$latency
        navigation_latency_ms=@($navigation)
        navigation_cpu_ms=$navigationCpu
        startup=$startup
        navigation_timings=@($navigationTimings | ForEach-Object {$_})
        paint_timings=@($paintTimings)
        scroll_timings=@($scrollTimings)
        input_timings=@($inputTimings)
        idle_cycles=$idleCycles
        regions=if($Regions) {$regions} else {$null}
    })
}
try {
    for ($run=1; $run -le $Runs; ++$run) {
        $startup=$null
        $navigationTimings=@(); $paintTimings=@(); $scrollTimings=@(); $inputTimings=@()
        $start = @{FilePath=$exe; PassThru=$true}
        if ($Arguments) { $start.ArgumentList=$Arguments }
        if ($CaptureOutput) {
            $start.RedirectStandardOutput="$Output-run$run.stdout.txt"
            $start.RedirectStandardError="$Output-run$run.stderr.txt"
        }
        $launchTimer=[Diagnostics.Stopwatch]::StartNew()
        $process = Start-Process @start
        $window = [IntPtr]::Zero
        try {
            $timer=[Diagnostics.Stopwatch]::StartNew()
            do {
                Start-Sleep -Milliseconds $(if($Performance) {1} else {10})
                if ($process.HasExited -or $timer.Elapsed.TotalSeconds -gt 15) { throw "Window did not become ready." }
                $window=[WindowMemoryProbe]::Window($process.Id)
            } while ($window -eq [IntPtr]::Zero -or (!$Performance -and $TaskManager -and [WindowMemoryProbe]::Metric($window,17) -lt 1))
            if ($Performance) {
                while ([WindowMemoryProbe]::Metric($window,0) -lt 1) {
                    if ($process.HasExited -or $launchTimer.Elapsed.TotalSeconds -gt 15) { throw "First paint did not complete." }
                    Start-Sleep -Milliseconds 1
                }
                $firstPaint=$launchTimer.Elapsed.TotalMilliseconds
                $firstPaintCpu=[WindowMemoryProbe]::Cpu($process.Handle)
                $firstPaintCycles=[WindowMemoryProbe]::Cycles($process.Handle)
                while ($TaskManager -and [WindowMemoryProbe]::Metric($window,17) -lt 1) {
                    if ($process.HasExited -or $launchTimer.Elapsed.TotalSeconds -gt 15) { throw "The first process sample did not become ready." }
                    Start-Sleep -Milliseconds 1
                }
                if (!$Gallery -and !$TaskManager) {
                    while ([WindowMemoryProbe]::Metric($window,9) -ne 60 -or
                        [WindowMemoryProbe]::Metric($window,23) -lt [WindowMemoryProbe]::Metric($window,22)) {
                        if ($process.HasExited -or $launchTimer.Elapsed.TotalSeconds -gt 15) { throw "The 60-file fixture did not become ready." }
                        Start-Sleep -Milliseconds 1
                    }
                }
                $startup=[pscustomobject]@{
                    first_paint_observed_ms=$firstPaint; first_paint_cpu_ms=$firstPaintCpu; first_paint_cycles=$firstPaintCycles
                    ready_ms=$launchTimer.Elapsed.TotalMilliseconds
                    ready_cpu_ms=[WindowMemoryProbe]::Cpu($process.Handle); ready_cycles=[WindowMemoryProbe]::Cycles($process.Handle)
                }
                [WindowMemoryProbe]::Foreground($window,$process.Id)
            }
            [WindowMemoryProbe]::SetWindowPos($window,[IntPtr](-1),20,20,0,0,0x11) | Out-Null
            Start-Sleep -Milliseconds 500
            Snapshot $process $window $run "initial" 0
            foreach ($size in $Sizes) {
                if ($process.HasExited) { throw "The owned process exited during the size sweep." }
                if ($size -notmatch '^(\d+)x(\d+)$') { throw "Invalid client size: $size" }
                $outer=[WindowMemoryProbe+Rect]::new(); $client=[WindowMemoryProbe+Rect]::new()
                [WindowMemoryProbe]::GetWindowRect($window,[ref]$outer) | Out-Null
                [WindowMemoryProbe]::GetClientRect($window,[ref]$client) | Out-Null
                $width=[int]$Matches[1]+$outer.right-$outer.left-$client.right
                $height=[int]$Matches[2]+$outer.bottom-$outer.top-$client.bottom
                if (![WindowMemoryProbe]::SetWindowPos($window,[IntPtr](-1),20,20,$width,$height,0x10)) {
                    throw "Cannot resize the owned window."
                }
                Start-Sleep -Milliseconds 350
                $timer.Restart()
                for($i=0;$i -lt 10;++$i) { [WindowMemoryProbe]::RedrawWindow($window,[IntPtr]::Zero,[IntPtr]::Zero,0x181) | Out-Null }
                $latency=$timer.Elapsed.TotalMilliseconds/10
                Snapshot $process $window $run $size $latency
            }
            if ($Gallery) {
                $catalog=[WindowMemoryProbe]::Child($window,"Control catalog")
                if ([WindowMemoryProbe]::Metric($window,19) -ne 46) { throw "The gallery workload requires exactly 46 catalog pages." }
                for($round=0;$round -lt 2;++$round) {
                    [WindowMemoryProbe]::SendMessage($catalog,0x100,[IntPtr]0x24,[IntPtr]::Zero) | Out-Null
                    $navigation=[System.Collections.Generic.List[double]]::new()
                    $navigationTimings=[System.Collections.Generic.List[object]]::new()
                    $process.Refresh(); $navigationCpuBefore=$process.TotalProcessorTime.TotalMilliseconds
                    for($page=1;$page -lt 46;++$page) {
                        $timer.Restart()
                        if ($Performance) {
                            $navigationTimings.Add([WindowMemoryProbe]::Measure($process.Handle,$window,$catalog,0x100,[IntPtr]0x28,$ForceInputPaint.IsPresent))
                        } else {
                            [WindowMemoryProbe]::SendMessage($catalog,0x100,[IntPtr]0x28,[IntPtr]::Zero) | Out-Null
                            [WindowMemoryProbe]::SendMessage($window,0x800c,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
                            [WindowMemoryProbe]::RedrawWindow($window,[IntPtr]::Zero,[IntPtr]::Zero,0x181) | Out-Null
                        }
                        if ($page -eq 19) {
                            $anchor=[WindowMemoryProbe]::Child($window,"Open retained popup")
                            [WindowMemoryProbe]::SendMessage($anchor,0x201,[IntPtr]1,[IntPtr]0x000a000a) | Out-Null
                            [WindowMemoryProbe]::SendMessage($anchor,0x202,[IntPtr]::Zero,[IntPtr]0x000a000a) | Out-Null
                            if ([WindowMemoryProbe]::Metric($window,24) -ne 1) { throw "The owned gallery popup did not open." }
                            [WindowMemoryProbe]::SendMessage($anchor,0x100,[IntPtr]0x1b,[IntPtr]::Zero) | Out-Null
                            [WindowMemoryProbe]::SendMessage($window,0x800c,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
                            if ([WindowMemoryProbe]::Metric($window,24) -ne 0) { throw "The owned gallery popup did not close." }
                        }
                        $navigation.Add($timer.Elapsed.TotalMilliseconds)
                        Start-Sleep -Milliseconds 100
                    }
                    [WindowMemoryProbe]::SendMessage($catalog,0x100,[IntPtr]0x24,[IntPtr]::Zero) | Out-Null
                    Start-Sleep -Milliseconds 350
                    $process.Refresh(); $navigationCpu=$process.TotalProcessorTime.TotalMilliseconds-$navigationCpuBefore
                    $phase=if($round -eq 0) {"all-46-pages-returned"} else {"all-46-pages-warm-returned"}
                    Snapshot $process $window $run $phase 0 $navigation.ToArray() $navigationCpu
                }
            }
            if ($Performance) {
                $navigationTimings=@()
                $paintTimings=@(for($i=0;$i -lt 60;++$i) {
                    [WindowMemoryProbe]::Measure($process.Handle,$window,$window,0,[IntPtr]::Zero,$ForceInputPaint.IsPresent)
                })
                $inputName=if($Gallery) {"Control catalog"} elseif($TaskManager) {"Processes"} else {"Files"}
                $input=[WindowMemoryProbe]::Child($window,$inputName)
                $scrollTimings=@(for($i=0;$i -lt 60;++$i) {
                    $delta=if([int][Math]::Floor($i/10)%2 -eq 0) {-120} else {120}
                    [WindowMemoryProbe]::Measure($process.Handle,$window,$input,0x20a,[IntPtr]($delta*65536),$ForceInputPaint.IsPresent)
                })
                if (!$Gallery) {
                    if (!$ForceInputPaint) {
                        [WindowMemoryProbe]::SendMessage($input,0x100,[IntPtr]0x24,[IntPtr]::Zero) | Out-Null
                        Start-Sleep -Milliseconds 100
                    }
                    $inputTimings=@(for($i=0;$i -lt 60;++$i) {
                        $key=if($i%2 -eq 0) {0x28} else {0x26}
                        [WindowMemoryProbe]::Measure($process.Handle,$window,$input,0x100,[IntPtr]$key,$ForceInputPaint.IsPresent)
                    })
                }
                Snapshot $process $window $run "warm-interactions" 0
            }
            if ($ReleaseProbeTarget) {
                [WindowMemoryProbe]::SendMessage($window,0x803d,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
                Start-Sleep -Milliseconds 500
                Snapshot $process $window $run "released-probe-target" 0
            }
        } finally {
            try {
                if (!$process.HasExited -and $window -ne [IntPtr]::Zero) {
                    [uint32]$owner=0
                    [WindowMemoryProbe]::GetWindowThreadProcessId($window,[ref]$owner) | Out-Null
                    if ($owner -eq $process.Id) { [WindowMemoryProbe]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null }
                }
                if (!$process.WaitForExit(5000)) { Stop-Process -Id $process.Id; throw "Owned probe did not close in five seconds." }
                if ($process.ExitCode -ne 0) { throw "Owned probe exited with code $($process.ExitCode)." }
            } finally { $process.Dispose() }
        }
    }
} finally { [WindowMemoryProbe]::SetThreadDpiAwarenessContext($previousDpi) | Out-Null }
[pscustomobject]@{
    timestamp=(Get-Date -Format o); executable=$exe; executable_bytes=(Get-Item $exe).Length
    sha256=(Get-FileHash $exe -Algorithm SHA256).Hash; arguments=$Arguments
    note="Sizes are physical client pixels. Initial size is unchanged. Later sizes share the initial process and renderer history. No working-set trimming."
    logical_processors=[Environment]::ProcessorCount; samples=$records
    gallery_page_count=if($Gallery) {46} else {0}
    gallery_navigation_rounds=if($Gallery) {2} else {0}
    gallery_popup_cycles=if($Gallery) {2} else {0}
    initializes_browser=$false
    forced_update_and_paint=$ForceInputPaint.IsPresent
    performance_note="Opt-in performance samples use process CPU time and cycle counts. Input follows the normal event-driven update path and waits for the completed-paint counter; pure paint uses RedrawWindow only. CPU time can be quantized; cycles are not milliseconds. Startup observes the first completed root paint, not the compositor's first displayed pixel. Explorer readiness requires 60 rows and all requested visible thumbnails. Each warm phase has 60 paints, 60 wheel events, and (outside Gallery) 60 navigation keys. No size change occurs when Sizes is empty."
    resource_metrics_note="Native bitmap bytes count four bytes per uploaded pixel, excluding driver allocations. Null resource counters mean that the executable does not expose the new metrics."
} | ConvertTo-Json -Depth 7 | Set-Content -Encoding utf8 $Output
$records | Select-Object run,phase,client_width,client_height,
    @{n="commit_MiB";e={[Math]::Round($_.private_commit_bytes/1MB,2)}},
    @{n="private_WS_MiB";e={[Math]::Round($_.private_working_set_bytes/1MB,2)}},
    paint_latency_ms | Format-Table
