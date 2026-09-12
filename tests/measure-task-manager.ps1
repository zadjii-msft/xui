param(
    [string]$Executable = "build\arm64\Release\xui_task_manager.exe",
    [string]$Output = "build\task-manager\measurements.json",
    [ValidateRange(1,20)][int]$Runs = 3,
    [switch]$Screenshots
)
$ErrorActionPreference = "Stop"
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class TaskProbe {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc fn, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc fn, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flag);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr hwnd, IntPtr rect, IntPtr region, uint flags);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr hwnd, string text);
    [DllImport("psapi.dll", SetLastError=true)] static extern bool QueryWorkingSet(IntPtr process, IntPtr buffer, int bytes);
    public static long PrivateWorkingSet(IntPtr process) {
        for (int bytes=65536; bytes<=16777216; bytes*=2) {
            IntPtr memory=Marshal.AllocHGlobal(bytes);
            try {
                if (QueryWorkingSet(process,memory,bytes)) {
                    long count=Marshal.ReadInt64(memory), pages=0;
                    for (int i=0;i<count;++i) if ((Marshal.ReadInt64(memory,8+i*8)&0x100)==0) ++pages;
                    return pages*Environment.SystemPageSize;
                }
                int error=Marshal.GetLastWin32Error();
                if (error!=24 && error!=122) throw new System.ComponentModel.Win32Exception(error);
            } finally { Marshal.FreeHGlobal(memory); }
        }
        throw new InvalidOperationException("Working set probe limit");
    }
    public static IntPtr Window(int process) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((h,d)=> { uint pid; GetWindowThreadProcessId(h,out pid);
            var text=new StringBuilder(128); GetClassName(h,text,128);
            if (pid==process && text.ToString()=="Xui.Window.1") { found=h; return false; } return true;
        },IntPtr.Zero);
        return found;
    }
    public static IntPtr Child(IntPtr root, string name) {
        IntPtr found=IntPtr.Zero;
        EnumChildWindows(root,(h,d)=> { var text=new StringBuilder(256); GetWindowText(h,text,256);
            if(text.ToString()==name) { found=h; return false; } return true;
        },IntPtr.Zero);
        if(found==IntPtr.Zero) throw new InvalidOperationException("Missing child: "+name);
        return found;
    }
    public static IntPtr Edit(IntPtr root) {
        IntPtr found=IntPtr.Zero;
        EnumChildWindows(root,(h,d)=> { var text=new StringBuilder(256); GetClassName(h,text,256);
            if(text.ToString()=="Edit" || text.ToString()=="EDIT") { found=h; return false; } return true;
        },IntPtr.Zero);
        if(found==IntPtr.Zero) throw new InvalidOperationException("Missing native EDIT");
        return found;
    }
    public static long Metric(IntPtr root, int key) { return SendMessage(root,0x803c,(IntPtr)key,IntPtr.Zero).ToInt64(); }
    public static void SetText(IntPtr edit, string value) {
        IntPtr text=Marshal.StringToHGlobalUni(value);
        try { if(SendMessage(edit,0x000c,IntPtr.Zero,text)==IntPtr.Zero) throw new InvalidOperationException("Cannot set EDIT text"); }
        finally { Marshal.FreeHGlobal(text); }
    }
    public static void Click(IntPtr control, int x, int y) {
        IntPtr point=(IntPtr)((y<<16)|(x&65535));
        SendMessage(control,0x201,(IntPtr)1,point); SendMessage(control,0x202,IntPtr.Zero,point);
    }
    public static void Dpi(IntPtr root, int dpi, int width, int height) {
        var rect=new Rect {left=20,top=20,right=20+width,bottom=20+height};
        IntPtr memory=Marshal.AllocHGlobal(Marshal.SizeOf<Rect>());
        try { Marshal.StructureToPtr(rect,memory,false); SendMessage(root,0x02e0,(IntPtr)((dpi<<16)|dpi),memory); }
        finally { Marshal.FreeHGlobal(memory); }
    }
}
'@
$exe = (Resolve-Path $Executable).Path
$folder = Split-Path $Output
New-Item -ItemType Directory -Force $folder | Out-Null
$records = [System.Collections.Generic.List[object]]::new()
function Capture($window, $name) {
    Add-Type -AssemblyName System.Drawing
    $rect = [TaskProbe+Rect]::new()
    [TaskProbe]::GetWindowRect($window,[ref]$rect) | Out-Null
    $bitmap = [Drawing.Bitmap]::new($rect.right-$rect.left,$rect.bottom-$rect.top)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.left,$rect.top,0,0,$bitmap.Size)
        $bitmap.Save((Join-Path (Get-Location) "$folder\$name.png"))
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}
function Record($process, $window, $run, $phase, $duration = 0, $cpuSeconds = 0, $paintDelta = 0, $latency = 0) {
    $process.Refresh()
    $outer = [TaskProbe+Rect]::new()
    $client = [TaskProbe+Rect]::new()
    [TaskProbe]::GetWindowRect($window,[ref]$outer) | Out-Null
    [TaskProbe]::GetClientRect($window,[ref]$client) | Out-Null
    $records.Add([pscustomobject]@{
        run=$run; phase=$phase; seconds=$duration
        process_cpu_seconds=$cpuSeconds
        machine_cpu_percent=if ($duration) {100*$cpuSeconds/$duration/[Environment]::ProcessorCount} else {0}
        private_commit_bytes=$process.PrivateMemorySize64
        private_working_set_bytes=[TaskProbe]::PrivateWorkingSet($process.Handle)
        total_working_set_bytes=$process.WorkingSet64
        threads=$process.Threads.Count; handles=$process.HandleCount
        gdi=[TaskProbe]::GetGuiResources($process.Handle,0); user=[TaskProbe]::GetGuiResources($process.Handle,1)
        paints=[TaskProbe]::Metric($window,0); paint_delta=$paintDelta
        samples=[TaskProbe]::Metric($window,17); process_rows=[TaskProbe]::Metric($window,19)
        visible_rows=[TaskProbe]::Metric($window,18); render_targets=[TaskProbe]::Metric($window,11)
        native_dpi=[TaskProbe]::GetDpiForWindow($window); input_latency_ms=$latency
        outer_width=$outer.right-$outer.left; outer_height=$outer.bottom-$outer.top
        client_width=$client.right; client_height=$client.bottom
    })
}
for ($run=1; $run -le $Runs; ++$run) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $exe -PassThru
    $window = [IntPtr]::Zero
    try {
        do {
            Start-Sleep -Milliseconds 10
            if ($process.HasExited -or $timer.Elapsed.TotalSeconds -gt 15) { throw "Task Manager did not become ready." }
            $window = [TaskProbe]::Window($process.Id)
        } while ($window -eq [IntPtr]::Zero -or [TaskProbe]::Metric($window,17) -lt 1 -or [TaskProbe]::Metric($window,0) -lt 1)
        $startup = $timer.Elapsed.TotalMilliseconds
        [TaskProbe]::SetWindowPos($window,[IntPtr](-1),20,20,0,0,0x11) | Out-Null
        Record $process $window $run "first-snapshot" 0 0 0 $startup
        Start-Sleep -Seconds 2
        $process.Refresh(); $cpuBefore=$process.TotalProcessorTime.TotalSeconds; $paintsBefore=[TaskProbe]::Metric($window,0)
        $timer.Restart(); Start-Sleep -Seconds 5
        $elapsed=$timer.Elapsed.TotalSeconds; $process.Refresh()
        Record $process $window $run "live-1s" $elapsed ($process.TotalProcessorTime.TotalSeconds-$cpuBefore) ([TaskProbe]::Metric($window,0)-$paintsBefore)
        $grid=[TaskProbe]::Child($window,"Processes")
        $timer.Restart()
        for ($i=0; $i -lt 30; ++$i) {
            [TaskProbe]::SendMessage($grid,0x100,[IntPtr]0x28,[IntPtr]::Zero) | Out-Null
            [TaskProbe]::SendMessage($grid,0x20a,[IntPtr](-120 -shl 16),[IntPtr]::Zero) | Out-Null
            [TaskProbe]::RedrawWindow($window,[IntPtr]::Zero,[IntPtr]::Zero,0x181) | Out-Null
        }
        Record $process $window $run "input-stress" 0 0 0 ($timer.Elapsed.TotalMilliseconds/30)
        $pause=[TaskProbe]::Child($window,"Pause")
        [TaskProbe]::Click($pause,10,10); Start-Sleep -Milliseconds 350
        $process.Refresh(); $cpuBefore=$process.TotalProcessorTime.TotalSeconds; $paintsBefore=[TaskProbe]::Metric($window,0)
        $timer.Restart(); Start-Sleep -Seconds 3
        $elapsed=$timer.Elapsed.TotalSeconds; $process.Refresh()
        Record $process $window $run "paused" $elapsed ($process.TotalProcessorTime.TotalSeconds-$cpuBefore) ([TaskProbe]::Metric($window,0)-$paintsBefore)
        if ($Screenshots -and $run -eq 1) {
            $tabs=[TaskProbe]::Child($window,"Task Manager pages")
            $theme=[TaskProbe]::Child($window,"Theme")
            $native=[TaskProbe]::GetDpiForWindow($window)
            foreach ($dpi in @(96,144,192)) {
                foreach ($width in @(1080,580)) {
                    $physicalWidth=[int]($width*$dpi/96)
                    $logicalHeight=[Math]::Min(780,[Math]::Floor(([TaskProbe]::GetSystemMetrics(1)-60)*96/$dpi))
                    $physicalHeight=[int]($logicalHeight*$dpi/96)
                    if ($physicalWidth+40 -gt [TaskProbe]::GetSystemMetrics(0) -or $physicalHeight+40 -gt [TaskProbe]::GetSystemMetrics(1)) {
                        throw "The screen is too small for an uncut $dpi-DPI screenshot."
                    }
                    # Test only this HWND's DPI layout. Do not change global display settings.
                    [TaskProbe]::Dpi($window,$dpi,$physicalWidth,$physicalHeight)
                    Start-Sleep -Milliseconds 200
                    [TaskProbe]::SendMessage($grid,0x100,[IntPtr]0x24,[IntPtr]::Zero) | Out-Null
                    [TaskProbe]::Click($tabs,[int](50*$dpi/96),[int](18*$dpi/96))
                    Start-Sleep -Milliseconds 250; Capture $window "processes-$width-$dpi"
                    [TaskProbe]::Click($tabs,[int](220*$dpi/96),[int](18*$dpi/96))
                    Start-Sleep -Milliseconds 250; Capture $window "performance-$width-$dpi"
                }
            }
            [TaskProbe]::Dpi($window,$native,[int](1080*$native/96),[int](780*$native/96))
            [TaskProbe]::Click($tabs,[int](50*$native/96),[int](18*$native/96))
            Start-Sleep -Milliseconds 200
            $edit=[TaskProbe]::Edit($window)
            [TaskProbe]::SetText($edit,$process.Id.ToString())
            Start-Sleep -Milliseconds 200
            if([TaskProbe]::Metric($window,19) -ne 1) { throw "Screenshot search did not isolate the owned sample process." }
            [TaskProbe]::SendMessage($grid,0x100,[IntPtr]0x24,[IntPtr]::Zero) | Out-Null
            [TaskProbe]::Click([TaskProbe]::Child($window,"Refresh"),10,10)
            Start-Sleep -Milliseconds 500
            [TaskProbe]::Click($tabs,[int](390*$native/96),[int](18*$native/96))
            Start-Sleep -Milliseconds 250; Capture $window "details-dark"
            [TaskProbe]::Click($theme,10,10); Start-Sleep -Milliseconds 250; Capture $window "details-light"
            [TaskProbe]::Click($tabs,[int](50*$native/96),[int](18*$native/96))
            [TaskProbe]::SetText([TaskProbe]::Edit($window),"")
            Start-Sleep -Milliseconds 250; Capture $window "processes-light"
            [TaskProbe]::Click($theme,10,10); Start-Sleep -Milliseconds 250; Capture $window "processes-high-contrast"
            [TaskProbe]::Click($tabs,[int](220*$native/96),[int](18*$native/96))
            Start-Sleep -Milliseconds 250; Capture $window "performance-high-contrast"
        }
        [TaskProbe]::Click($pause,10,10); Start-Sleep -Milliseconds 350
        [TaskProbe]::ShowWindow($window,6) | Out-Null; Start-Sleep -Milliseconds 350
        $process.Refresh(); $cpuBefore=$process.TotalProcessorTime.TotalSeconds; $paintsBefore=[TaskProbe]::Metric($window,0)
        $timer.Restart(); Start-Sleep -Seconds 2
        $elapsed=$timer.Elapsed.TotalSeconds; $process.Refresh()
        Record $process $window $run "minimized" $elapsed ($process.TotalProcessorTime.TotalSeconds-$cpuBefore) ([TaskProbe]::Metric($window,0)-$paintsBefore)
    } finally {
        if ($window -ne [IntPtr]::Zero) { [TaskProbe]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null }
        if (!$process.WaitForExit(5000)) { Stop-Process -Id $process.Id; throw "Owned sample did not close in five seconds." }
        $process.Dispose()
    }
}
[pscustomobject]@{
    timestamp=(Get-Date -Format o); executable=$exe; executable_bytes=(Get-Item $exe).Length
    sha256=(Get-FileHash $exe -Algorithm SHA256).Hash
    logical_processors=[Environment]::ProcessorCount
    screenshot_dpi_note="WM_DPICHANGED injected per owned window; native monitor DPI is recorded separately. No global display settings changed."
    samples=$records
} | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 $Output
$records | Format-Table run,phase,machine_cpu_percent,private_commit_bytes,private_working_set_bytes,total_working_set_bytes,paint_delta,process_rows,input_latency_ms
