param(
    [string]$Executable = "build\arm64\Release\xui_task_manager.exe",
    [string]$Output = "build\task-manager\memory-investigation\size-sweep.json",
    [string]$Arguments = "",
    [string[]]$Sizes = @("800x780", "1000x780", "1024x780", "1040x780", "1080x780", "1200x780", "800x780"),
    [ValidateRange(1,20)][int]$Runs = 3,
    [switch]$TaskManager,
    [switch]$Regions,
    [switch]$CaptureOutput,
    [switch]$ReleaseProbeTarget
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
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
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
    public static IntPtr Window(int process) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((h,d)=> { uint pid; GetWindowThreadProcessId(h,out pid);
            if (pid==process && IsWindowVisible(h)) { found=h; return false; } return true;
        },IntPtr.Zero);
        return found;
    }
    public static long Metric(IntPtr root, int key) { return SendMessage(root,0x803c,(IntPtr)key,IntPtr.Zero).ToInt64(); }
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
$exe = (Resolve-Path $Executable).Path
$folder = Split-Path $Output
if ($folder) { New-Item -ItemType Directory -Force $folder | Out-Null }
$records = [System.Collections.Generic.List[object]]::new()
$previousDpi = [WindowMemoryProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))
function Snapshot($process, $window, $run, $phase, $latency) {
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
        process_rows=[WindowMemoryProbe]::Metric($window,19); visible_rows=[WindowMemoryProbe]::Metric($window,18)
        paint_latency_ms=$latency
        regions=if($Regions) {$regions} else {$null}
    })
}
try {
    for ($run=1; $run -le $Runs; ++$run) {
        $start = @{FilePath=$exe; PassThru=$true}
        if ($Arguments) { $start.ArgumentList=$Arguments }
        if ($CaptureOutput) {
            $start.RedirectStandardOutput="$Output-run$run.stdout.txt"
            $start.RedirectStandardError="$Output-run$run.stderr.txt"
        }
        $process = Start-Process @start
        $window = [IntPtr]::Zero
        try {
            $timer=[Diagnostics.Stopwatch]::StartNew()
            do {
                Start-Sleep -Milliseconds 10
                if ($process.HasExited -or $timer.Elapsed.TotalSeconds -gt 15) { throw "Window did not become ready." }
                $window=[WindowMemoryProbe]::Window($process.Id)
            } while ($window -eq [IntPtr]::Zero -or ($TaskManager -and [WindowMemoryProbe]::Metric($window,17) -lt 1))
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
} | ConvertTo-Json -Depth 7 | Set-Content -Encoding utf8 $Output
$records | Select-Object run,phase,client_width,client_height,
    @{n="commit_MiB";e={[Math]::Round($_.private_commit_bytes/1MB,2)}},
    @{n="private_WS_MiB";e={[Math]::Round($_.private_working_set_bytes/1MB,2)}},
    paint_latency_ms | Format-Table
