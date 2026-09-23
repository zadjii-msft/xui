param([string]$BuildDirectory = "build\controls", [switch]$SkipBuild)
$ErrorActionPreference = "Stop"
$root = (Get-Location).Path
$build = Join-Path $root $BuildDirectory
$output = Join-Path $build "bindings-validation"
New-Item -ItemType Directory -Force $output | Out-Null
. "$PSScriptRoot\..\scripts\Release.Common.ps1"
$cmake = Get-XuiCMake
$env:XUI_LIB_DIR = Join-Path $build "Release"
function Run([string]$name, [scriptblock]$body) {
    & $body 2>&1 | Tee-Object -FilePath (Join-Path $output "$name.log")
    if ($LASTEXITCODE -ne 0) { throw "$name failed: $LASTEXITCODE" }
}
if (!$SkipBuild) {
    Run "native-build" { & $cmake --build $build --config Release --target xui_abi_features_tests xui_abi_tests xui_abi_c_test xui_feature_sample xui_bindings_smoke xui_abi_sample xui_direct_sample --parallel 4 }
    Run "native-abi" { & (Join-Path (Split-Path $cmake) "ctest.exe") --test-dir $build -C Release -R "xui_abi" --output-on-failure }
    foreach ($project in @("Tests","Sample")) {
        foreach ($aot in @($false,$true)) {
            $flavor = if ($aot) {"aot"} else {"fdd"}
            $destination = Join-Path $output "$project-$flavor"
            $aotFlag = if ($aot) {"true"} else {"false"}
            Run "$project-$flavor-build" {
                if ($aot) {
                    Invoke-XuiVcVarsCommand ARM64 "dotnet publish `"bindings\dotnet\$project\$project.csproj`" -c Release -r win-arm64 --no-restore -p:PublishAot=true -p:SelfContained=true -p:XuiNativeDir=`"$env:XUI_LIB_DIR`" -p:PublishDir=`"$destination\`" --nologo"
                } else {
                    dotnet publish "bindings\dotnet\$project\$project.csproj" -c Release -r win-arm64 --no-restore "-p:PublishAot=$aotFlag" "-p:SelfContained=$aotFlag" "-p:XuiNativeDir=$env:XUI_LIB_DIR" "-p:PublishDir=$destination\" --nologo
                }
            }
        }
    }
    $rustCommand = 'cd /d "' + $root + '\bindings\rust" && '
    Run "rust-build" { Invoke-XuiVcVarsCommand ARM64 ($rustCommand + 'cargo build --workspace --release') }
    Copy-Item (Join-Path $env:XUI_LIB_DIR "xui.dll") bindings\rust\target\aarch64-pc-windows-msvc\release\deps\
    Run "rust-tests" { Invoke-XuiVcVarsCommand ARM64 ($rustCommand + 'cargo test --workspace --release') }
    Run "rust-clippy" { Invoke-XuiVcVarsCommand ARM64 ($rustCommand + 'cargo clippy --workspace --all-targets --release -- -D warnings') }
    Run "rust-format" { Invoke-XuiVcVarsCommand ARM64 ($rustCommand + 'cargo fmt --all --check') }
    $rustOutput = Join-Path $output "rust"
    New-Item -ItemType Directory -Force $rustOutput | Out-Null
    Copy-Item bindings\rust\target\aarch64-pc-windows-msvc\release\xui-sample.exe $rustOutput
    Copy-Item (Join-Path $env:XUI_LIB_DIR "xui.dll") $rustOutput
}
Run "dotnet-fdd-tests" { & (Join-Path $output "Tests-fdd\Tests.exe") }
Run "dotnet-aot-tests" { & (Join-Path $output "Tests-aot\Tests.exe") }

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class XuiFeatureSmoke {
    public delegate bool EnumProc(nint window, nint context);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, nint context);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(nint window, out uint process);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(nint window);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(nint window, StringBuilder value, int count);
    [DllImport("user32.dll")] public static extern bool PostMessage(nint window, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll")] public static extern nint SendMessageTimeout(nint window, uint message, nuint wparam, nint lparam, uint flags, uint timeout, out nuint result);
    public static nint Find(int process) {
        nint found=0;
        EnumWindows((w,c)=> { GetWindowThreadProcessId(w,out uint pid); var name=new StringBuilder(80); GetClassName(w,name,80); if(pid==process && IsWindowVisible(w) && name.ToString()=="Xui.Window.1"){found=w;return false;}return true;},0);
        return found;
    }
    public static void Key(nint window, uint key) {
        if(!PostMessage(window,0x100,key,0) || !PostMessage(window,0x101,key,0)) throw new Exception("Owned window did not accept key input.");
    }
}
'@
$clients = [ordered]@{
    native = (Join-Path $build "Release\xui_feature_sample.exe")
    fdd = (Join-Path $output "Sample-fdd\Sample.exe")
    aot = (Join-Path $output "Sample-aot\Sample.exe")
    rust = (Join-Path $output "rust\xui-sample.exe")
}
$results = @()
foreach ($client in $clients.GetEnumerator()) {
    foreach ($fail in @($false,$true)) {
        $arguments = if ($fail) { @("--features","--callback-fail") } else { @("--features") }
        $mode = if ($fail) { "failure" } else { "normal" }
        $process = Start-Process $client.Value -ArgumentList $arguments -PassThru -RedirectStandardOutput (Join-Path $output "$($client.Key)-$mode.out.log") -RedirectStandardError (Join-Path $output "$($client.Key)-$mode.err.log")
        try {
            $deadline = [DateTime]::UtcNow.AddSeconds(30)
            $window = [IntPtr]::Zero
            while (!$process.HasExited -and [DateTime]::UtcNow -lt $deadline -and $window -eq 0) {
                $window = [XuiFeatureSmoke]::Find($process.Id); Start-Sleep -Milliseconds 100
            }
            if (!$window) { throw "$($client.Key) did not open its owned window." }
            Start-Sleep -Milliseconds 700
            [XuiFeatureSmoke]::Key($window,0x75)
            Start-Sleep -Milliseconds 300
            [XuiFeatureSmoke]::Key($window,0x1b)
            Start-Sleep -Milliseconds 300
            [XuiFeatureSmoke]::Key($window,0x77)
            Start-Sleep -Milliseconds 300
            if (!$fail) { [XuiFeatureSmoke]::Key($window,0x7b) }
            if (!$process.WaitForExit(15000)) { throw "$($client.Key) $mode did not close." }
            $expected = if ($fail) {1} else {0}
            if ($process.ExitCode -ne $expected) { throw "$($client.Key) $mode returned $($process.ExitCode), expected $expected." }
            $results += [ordered]@{ client=$client.Key; mode=$mode; exit=$process.ExitCode; passed=$true }
        } finally {
            if (!$process.HasExited) { Stop-Process -Id $process.Id -Force }
            $process.Dispose()
        }
    }
}
$results | ConvertTo-Json | Set-Content (Join-Path $output "feature-smoke.json")
$results | Format-Table | Out-String | Write-Output
