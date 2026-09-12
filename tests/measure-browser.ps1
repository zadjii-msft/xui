param(
    [string]$Executable = "build\arm64\Release\xui_demo.exe",
    [string]$Output = "build\memory\sample.json",
    [ValidateRange(1,1000)][int]$Runs = 5,
    [switch]$Screenshots,
    [switch]$WorkspaceCycles
)
$ErrorActionPreference = "Stop"
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class BrowserProbe {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr root, EnumProc callback, IntPtr data);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flag);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr hwnd, string text);
    [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr hwnd, IntPtr rect, IntPtr region, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("psapi.dll", SetLastError=true)] static extern bool QueryWorkingSet(IntPtr process, IntPtr buffer, int bytes);
    public static long PrivateWorkingSet(IntPtr process) {
        for (int bytes=65536; bytes<=16777216; bytes*=2) {
            IntPtr buffer=Marshal.AllocHGlobal(bytes);
            try {
                if (QueryWorkingSet(process,buffer,bytes)) {
                    long count=Marshal.ReadInt64(buffer), pages=0;
                    for (int i=0; i<count; ++i)
                        if ((Marshal.ReadInt64(buffer,8+i*8)&0x100)==0) ++pages;
                    return pages*Environment.SystemPageSize;
                }
                int error=Marshal.GetLastWin32Error();
                if (error!=24 && error!=122) throw new System.ComponentModel.Win32Exception(error);
            } finally { Marshal.FreeHGlobal(buffer); }
        }
        throw new InvalidOperationException("The working set exceeds the probe limit.");
    }
    public static IntPtr Window(int pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h,d) => { uint p; GetWindowThreadProcessId(h,out p);
            if (p == pid && IsWindowVisible(h)) { found=h; return false; } return true; },IntPtr.Zero);
        return found;
    }
    public static long Metric(IntPtr hwnd, int key) { return SendMessage(hwnd,0x803c,(IntPtr)key,IntPtr.Zero).ToInt64(); }
    public static void SetEditText(IntPtr edit, string value) {
        IntPtr text=Marshal.StringToHGlobalUni(value);
        try {
            if(SendMessage(edit,0x000c,IntPtr.Zero,text)==IntPtr.Zero)
                throw new InvalidOperationException("Cannot set native EDIT text");
        } finally { Marshal.FreeHGlobal(text); }
    }
    public static IntPtr Child(IntPtr parent, string cls, int ordinal=0) {
        IntPtr result=IntPtr.Zero;
        EnumChildWindows(parent,(h,d) => {
            var text=new StringBuilder(128); GetClassName(h,text,128);
            if (String.Equals(text.ToString(),cls,StringComparison.OrdinalIgnoreCase) && ordinal--==0) {
                result=h; return false;
            }
            return true;
        },IntPtr.Zero);
        return result;
    }
    public static bool Command(IntPtr root, string title) {
        IntPtr result=IntPtr.Zero;
        EnumChildWindows(root,(h,d) => {
            var cls=new StringBuilder(128); GetClassName(h,cls,128);
            var text=new StringBuilder(128); GetWindowText(h,text,128);
            if (cls.ToString()=="Xui.Control.1" && text.ToString()==title) { result=h; return false; }
            return true;
        },IntPtr.Zero);
        if(result==IntPtr.Zero) return false;
        SendMessage(result,0x201,(IntPtr)1,(IntPtr)0x000a000a);
        SendMessage(result,0x202,IntPtr.Zero,(IntPtr)0x000a000a);
        return true;
    }
    public static void CloseTab(IntPtr root) {
        IntPtr target=IntPtr.Zero;
        EnumChildWindows(root,(h,d) => {
            var text=new StringBuilder(128); GetWindowText(h,text,128);
            if (text.ToString()=="Left pane tabs") { target=h; return false; }
            return true;
        },IntPtr.Zero);
        if(target==IntPtr.Zero) throw new InvalidOperationException("Missing tab strip");
        SendMessage(target,0x100,(IntPtr)0x2e,IntPtr.Zero);
    }
}
'@
$exe = (Resolve-Path $Executable).Path
$directory = Join-Path (Get-Location) "build\memory\fixture-60"
if (Test-Path $directory) { throw "The measurement fixture already exists: $directory" }
$outputDirectory = Split-Path $Output
if ($outputDirectory) { New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null }
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$samples = [System.Collections.Generic.List[object]]::new()
function Snapshot($process, $window, $run, $phase, $latency = 0) {
    $process.Refresh()
    $samples.Add([pscustomobject]@{
        run=$run; phase=$phase; private_bytes=$process.PrivateMemorySize64
        private_working_set=[BrowserProbe]::PrivateWorkingSet($process.Handle)
        working_set=$process.WorkingSet64; peak_working_set=$process.PeakWorkingSet64
        peak_commit=$process.PeakPagedMemorySize64
        threads=$process.Threads.Count; handles=$process.HandleCount
        gdi=[BrowserProbe]::GetGuiResources($process.Handle,0)
        user=[BrowserProbe]::GetGuiResources($process.Handle,1)
        paints=if ($window -ne [IntPtr]::Zero) {[BrowserProbe]::Metric($window,0)} else {0}
        dpi=if ($window -ne [IntPtr]::Zero) {[BrowserProbe]::GetDpiForWindow($window)} else {0}
        latency_ms=$latency
        targets=if ($window -ne [IntPtr]::Zero) {[BrowserProbe]::Metric($window,11)} else {0}
    })
}
function Screenshot($window, $suffix) {
    Add-Type -AssemblyName System.Drawing
    $rect = [BrowserProbe+Rect]::new()
    [BrowserProbe]::GetWindowRect($window,[ref]$rect) | Out-Null
    $bitmap = [Drawing.Bitmap]::new($rect.right-$rect.left,$rect.bottom-$rect.top)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.left,$rect.top,0,0,$bitmap.Size)
        $bitmap.Save("$((Join-Path (Get-Location) $Output))-$suffix.png")
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}
try {
    0..59 | ForEach-Object { [IO.File]::WriteAllText((Join-Path $directory ("Entry-{0:d3}.txt" -f $_)), "") }
    for ($run=1; $run -le $Runs; ++$run) {
        $process = Start-Process -FilePath $exe -ArgumentList "`"$directory`"" -PassThru
        try {
            Snapshot $process ([IntPtr]::Zero) $run "launch"
            $clock = [Diagnostics.Stopwatch]::StartNew()
            do {
                Start-Sleep -Milliseconds 10
                if ($process.HasExited -or $clock.Elapsed.TotalSeconds -gt 15) { throw "Browser did not become ready." }
                $window = [BrowserProbe]::Window($process.Id)
            } while ($window -eq [IntPtr]::Zero -or [BrowserProbe]::Metric($window,9) -ne 60 -or
                [BrowserProbe]::Metric($window,4) -ne 0 -or [BrowserProbe]::Metric($window,0) -eq 0)
            # Keep the initial client extent. Protect the owned window from unrelated desktop occlusion.
            [BrowserProbe]::SetWindowPos($window,[IntPtr](-1),40,40,0,0,0x11) | Out-Null
            [BrowserProbe]::RedrawWindow($window,[IntPtr]::Zero,[IntPtr]::Zero,0x181) | Out-Null
            Snapshot $process $window $run "painted" $clock.Elapsed.TotalMilliseconds
            $list = [BrowserProbe]::Child($window,"Xui.FileList.1")
            # Explorer keeps address before search; the previous one-field sample has only search.
            $edit = [BrowserProbe]::Child($window,"EDIT",1)
            if ($edit -eq [IntPtr]::Zero) { $edit = [BrowserProbe]::Child($window,"EDIT",0) }
            if ($list -eq [IntPtr]::Zero -or $edit -eq [IntPtr]::Zero) { throw "Browser controls were not found." }
            Start-Sleep -Milliseconds 750
            $clock.Restart()
            for ($i=0; $i -lt 30; ++$i) {
                [BrowserProbe]::SendMessage($list,0x100,[IntPtr]0x28,[IntPtr]::Zero) | Out-Null
                [BrowserProbe]::SendMessage($list,0x20a,[IntPtr](-120 -shl 16),[IntPtr]::Zero) | Out-Null
                [BrowserProbe]::RedrawWindow($window,[IntPtr]::Zero,[IntPtr]::Zero,0x181) | Out-Null
            }
            $latency = $clock.Elapsed.TotalMilliseconds/30
            Start-Sleep -Milliseconds 250
            Snapshot $process $window $run "warm" $latency
            Start-Sleep -Seconds 2
            Snapshot $process $window $run "idle"
            if ($Screenshots) {
                Screenshot $window $run
            }
            $clock.Restart()
            for ($i=0; $i -lt 20; ++$i) {
                [BrowserProbe]::SetEditText($edit,"Entry-00")
                [BrowserProbe]::SetEditText($edit,"no-match")
                [BrowserProbe]::SetEditText($edit,"")
                [BrowserProbe]::PostMessage($window,0x100,[IntPtr]0x74,[IntPtr]::Zero) | Out-Null
                if (![BrowserProbe]::Command($window,"Theme")) {
                    [BrowserProbe]::PostMessage($window,0x100,[IntPtr]0x75,[IntPtr]::Zero) | Out-Null
                }
                Start-Sleep -Milliseconds 30
            }
            do {
                Start-Sleep -Milliseconds 10
                if ($clock.Elapsed.TotalSeconds -gt 15) { throw "Refresh did not settle." }
            } while ([BrowserProbe]::Metric($window,4) -ne 0 -or [BrowserProbe]::Metric($window,8) -ne 60)
            Start-Sleep -Seconds 2
            Snapshot $process $window $run "stress"
            if ($WorkspaceCycles) {
                if (![BrowserProbe]::Command($window,"Split panes")) { throw "Split command not found." }
                Start-Sleep -Seconds 2
                Snapshot $process $window $run "two-panes"
                if ($Screenshots -and $run -eq 1) { Screenshot $window "two-panes" }
                for ($i=0; $i -lt 15; ++$i) {
                    if (![BrowserProbe]::Command($window,"+")) { throw "New tab command not found." }
                    Start-Sleep -Milliseconds 60
                }
                Start-Sleep -Seconds 2
                Snapshot $process $window $run "sixteen-tabs"
                if ($Screenshots -and $run -eq 1) { Screenshot $window "sixteen-tabs" }
                for ($i=0; $i -lt 15; ++$i) { [BrowserProbe]::CloseTab($window); Start-Sleep -Milliseconds 60 }
                $address = [BrowserProbe]::Child($window,"EDIT",0)
                $cycle = Join-Path (Get-Location) "build\memory\cycle-$run-$($process.Id)"
                if (Test-Path $cycle) { throw "Cycle fixture already exists." }
                New-Item -ItemType Directory -Path $cycle | Out-Null
                try {
                    for ($i=0; $i -lt 40; ++$i) {
                        $generation = [BrowserProbe]::Metric($window,3)
                        $path = if ($i % 2) { $directory } else { $cycle }
                        [BrowserProbe]::SetEditText($address,$path)
                        [BrowserProbe]::PostMessage($address,0x100,[IntPtr]0x0d,[IntPtr]::Zero) | Out-Null
                        $wait = [Diagnostics.Stopwatch]::StartNew()
                        do {
                            Start-Sleep -Milliseconds 10
                            if ($wait.Elapsed.TotalSeconds -gt 15) { throw "Navigation did not settle." }
                        } while ([BrowserProbe]::Metric($window,3) -le $generation -or [BrowserProbe]::Metric($window,4) -ne 0)
                    }
                    [BrowserProbe]::Command($window,"Split panes") | Out-Null
                    Start-Sleep -Seconds 2
                    Snapshot $process $window $run "workspace-cycles"
                    Start-Sleep -Seconds 2
                    Snapshot $process $window $run "workspace-idle"
                } finally { Remove-Item -LiteralPath $cycle -Force }
            }
        } finally {
            if (!$process.HasExited) {
                $process.CloseMainWindow() | Out-Null
                if (!$process.WaitForExit(10000)) { Stop-Process -Id $process.Id }
            }
            $process.Dispose()
        }
    }
    $samples | ConvertTo-Json -Depth 4 | Set-Content $Output
    $samples | Format-Table run,phase,private_bytes,private_working_set,working_set,peak_commit,threads,handles,gdi,user,paints,latency_ms
    $summary = foreach ($phase in "painted","warm","idle","stress") {
        foreach ($counter in "private_bytes","private_working_set","working_set","peak_commit","latency_ms") {
            $values = @($samples | Where-Object phase -eq $phase | ForEach-Object { $_.$counter } | Sort-Object)
            $middle = [int][Math]::Floor($values.Count/2)
            $median = if ($values.Count % 2) { $values[$middle] } else { ($values[$middle-1]+$values[$middle])/2 }
            $scale = if ($counter -eq "latency_ms") { 1 } else { 1MB }
            [pscustomobject]@{
                phase=$phase; counter=$counter; unit=if ($scale -eq 1) {"ms"} else {"MiB"}
                median=[Math]::Round($median/$scale,3)
                min=[Math]::Round($values[0]/$scale,3)
                max=[Math]::Round($values[-1]/$scale,3)
            }
        }
    }
    $summary | Format-Table
} finally {
    Remove-Item -LiteralPath $directory -Recurse -Force
}
