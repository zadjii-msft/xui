param(
    [ValidateRange(1,100)][int]$Runs = 5,
    [string]$Output = "build\phase4\measurements.json",
    [string]$Image = "build\phase3\sample-fixtures\image-0.png"
)
$ErrorActionPreference = "Stop"
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class XuiProbe {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd,uint message,IntPtr w,IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd,IntPtr after,int x,int y,int cx,int cy,uint flags);
    [DllImport("kernel32.dll")] public static extern bool IsWow64Process2(IntPtr process,out ushort machine,out ushort native);
    [DllImport("psapi.dll",SetLastError=true)] static extern bool QueryWorkingSet(IntPtr process,IntPtr buffer,int size);
    public static IntPtr Window(int pid) {
        IntPtr result=IntPtr.Zero;
        EnumWindows((h,d)=>{uint p; GetWindowThreadProcessId(h,out p);
            if (p==pid && IsWindowVisible(h) && SendMessage(h,0x803c,(IntPtr)11,IntPtr.Zero).ToInt64()==1) {
                result=h;return false;}return true;},IntPtr.Zero);
        return result;
    }
    public static long PrivateWorkingSet(IntPtr process) {
        for(int bytes=65536;bytes<=33554432;bytes*=2) {
            IntPtr buffer=Marshal.AllocHGlobal(bytes);
            try {
                if(QueryWorkingSet(process,buffer,bytes)) {
                    long pages=0,count=Marshal.ReadInt64(buffer);
                    for(int i=0;i<count;++i) if((Marshal.ReadInt64(buffer,8+i*8)&0x100)==0) ++pages;
                    return pages*Environment.SystemPageSize;
                }
                int error=Marshal.GetLastWin32Error();
                if(error!=24 && error!=122) throw new System.ComponentModel.Win32Exception(error);
            } finally {Marshal.FreeHGlobal(buffer);}
        }
        throw new InvalidOperationException("Working set probe capacity exceeded.");
    }
}
'@
$clients = [ordered]@{
    cpp_static = "build\phase4\cpp-static\xui_direct_sample.exe"
    cpp_abi = "build\phase4\cpp-abi\xui_abi_sample.exe"
    dotnet_fdd = "build\phase4\dotnet\Sample.exe"
    dotnet_aot = "build\phase4\aot\Sample.exe"
    rust = "build\phase4\rust\xui-sample.exe"
}
$imagePath = (Resolve-Path $Image).Path
$samples = [Collections.Generic.List[object]]::new()
$throughput = [Collections.Generic.List[object]]::new()
$sizes = [Collections.Generic.List[object]]::new()
foreach ($client in $clients.GetEnumerator()) {
    $exe = (Resolve-Path $client.Value).Path
    $files = @(Get-ChildItem (Split-Path $exe) -File | Where-Object Extension -ne ".pdb")
    $sizes.Add([pscustomobject]@{client=$client.Key; executable_bytes=(Get-Item $exe).Length
        native_dll_bytes=if ($client.Key -eq "cpp_static") {0} else {(Get-Item (Join-Path (Split-Path $exe) "xui.dll")).Length}
        deployment_bytes=($files | Measure-Object Length -Sum).Sum; files=@($files | Select-Object Name,Length)})
}
for ($run=1; $run -le $Runs; ++$run) {
    foreach ($client in $clients.GetEnumerator()) {
        $exe = (Resolve-Path $client.Value).Path
        $startInfo = [Diagnostics.ProcessStartInfo]::new($exe)
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $true
        $startInfo.ArgumentList.Add($imagePath)
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $process = [Diagnostics.Process]::Start($startInfo)
        try {
            do {
                if ($process.HasExited -or $clock.Elapsed.TotalSeconds -gt 20) { throw "$($client.Key) failed to paint." }
                $window = [XuiProbe]::Window($process.Id)
                if ($window -ne [IntPtr]::Zero -and [XuiProbe]::SendMessage($window,0x803c,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -gt 0) { break }
                Start-Sleep -Milliseconds 5
            } while ($true)
            $startup = $clock.Elapsed.TotalMilliseconds
            [ushort]$machine=0; [ushort]$native=0
            if (![XuiProbe]::IsWow64Process2($process.Handle,[ref]$machine,[ref]$native) -or $machine -ne 0 -or $native -ne 0xaa64) {
                throw "The client is not native ARM64."
            }
            [XuiProbe]::SetWindowPos($window,[IntPtr](-1),40,40,0,0,0x11) | Out-Null
            Start-Sleep -Seconds 2
            $process.Refresh()
            $paints = [XuiProbe]::SendMessage($window,0x803c,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
            $samples.Add([pscustomobject]@{run=$run; client=$client.Key; startup_ms=$startup
                private_commit=$process.PrivateMemorySize64; private_working_set=[XuiProbe]::PrivateWorkingSet($process.Handle)
                working_set=$process.WorkingSet64; handles=$process.HandleCount; threads=$process.Threads.Count
                targets=[XuiProbe]::SendMessage($window,0x803c,[IntPtr]11,[IntPtr]::Zero).ToInt64()
                paints=$paints; architecture="ARM64"})
            Start-Sleep -Milliseconds 500
            $samples[$samples.Count-1] | Add-Member -NotePropertyName idle_paints -NotePropertyValue (
                [XuiProbe]::SendMessage($window,0x803c,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()-$paints)
        } finally {
            if (!$process.HasExited) {
                $process.CloseMainWindow() | Out-Null
                if (!$process.WaitForExit(10000)) { Stop-Process -Id $process.Id; throw "The client did not close." }
            }
            $process.Dispose()
        }
        $raw = & $exe --throughput
        if ($LASTEXITCODE -ne 0) { throw "Throughput run failed." }
        $measurement = $raw | ConvertFrom-Json
        $measurement | Add-Member -NotePropertyName client -NotePropertyValue $client.Key
        $measurement | Add-Member -NotePropertyName run -NotePropertyValue $run
        $throughput.Add($measurement)
    }
}
[pscustomobject]@{timestamp=[DateTimeOffset]::Now.ToString("o"); runs=$Runs
    definition="Startup starts before Process.Start (no console) and ends after a nonzero paint counter. Memory follows 2 seconds without UIA."
    samples=$samples; throughput=$throughput; sizes=$sizes} | ConvertTo-Json -Depth 6 | Set-Content $Output
$samples | Format-Table client,run,startup_ms,private_commit,private_working_set,working_set,handles,threads,idle_paints | Out-String
$throughput | Format-Table | Out-String
$sizes | Format-Table client,executable_bytes,native_dll_bytes,deployment_bytes | Out-String
