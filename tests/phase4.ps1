param([switch]$SkipNativeTests, [switch]$SkipMeasurements, [int]$Runs = 5)
$ErrorActionPreference = "Stop"
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Preview\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = Join-Path (Split-Path $cmake) "ctest.exe"
$root = (Get-Location).Path
New-Item -ItemType Directory -Force build\phase4 | Out-Null
function Run([string]$Log, [scriptblock]$Body) {
    $path = Join-Path $root "build\phase4\$Log"
    Set-Content $path "Command output:"
    & $Body 2>&1 | Tee-Object -FilePath $path -Append
    $exitCode = $LASTEXITCODE
    Add-Content $path "Exit code: $exitCode"
    if ($exitCode -ne 0) { throw "$Log failed with exit code $exitCode." }
}
Run "native-build.log" { & $cmake --build build\arm64 --config Release --parallel 4 }
if (!$SkipNativeTests) {
    Run "native-tests.log" { & $ctest --test-dir build\arm64 -C Release --output-on-failure }
}
$env:PATH = "$root\build\arm64\Release;$env:PATH"
Run "dotnet-fdd-build.log" {
    dotnet publish bindings\dotnet\Sample\Sample.csproj -c Release -r win-arm64 -p:SelfContained=false "-p:PublishDir=$root\build\phase4\dotnet\" --nologo
}
Run "dotnet-aot-build.log" {
    dotnet publish bindings\dotnet\Sample\Sample.csproj -c Release -r win-arm64 -p:PublishAot=true "-p:PublishDir=$root\build\phase4\aot\" --nologo
}
Run "dotnet-fdd-test-build.log" {
    dotnet publish bindings\dotnet\Tests\Tests.csproj -c Release -r win-arm64 -p:SelfContained=false "-p:PublishDir=$root\build\phase4\dotnet-tests\" --nologo
}
Run "dotnet-aot-test-build.log" {
    dotnet publish bindings\dotnet\Tests\Tests.csproj -c Release -r win-arm64 -p:PublishAot=true "-p:PublishDir=$root\build\phase4\aot-tests\" --nologo
}
Push-Location bindings\rust
try {
    Run "rust-build.log" { cargo build --workspace --release }
    Run "rust-tests.log" { cargo test --workspace --release }
    Run "rust-clippy.log" { cargo clippy --workspace --all-targets --release -- -D warnings }
    Run "rust-format.log" { cargo fmt --all --check }
} finally { Pop-Location }
foreach ($directory in @("cpp-static","cpp-abi","rust")) {
    New-Item -ItemType Directory -Force "build\phase4\$directory" | Out-Null
}
Copy-Item build\arm64\Release\xui_direct_sample.exe build\phase4\cpp-static\
Copy-Item build\arm64\Release\xui_abi_sample.exe build\phase4\cpp-abi\
Copy-Item bindings\rust\target\aarch64-pc-windows-msvc\release\xui-sample.exe build\phase4\rust\
foreach ($directory in @("cpp-abi","dotnet","aot","rust","dotnet-tests","aot-tests")) { Copy-Item build\arm64\Release\xui.dll "build\phase4\$directory\" }
Run "dotnet-fdd-tests.log" { & .\build\phase4\dotnet-tests\Tests.exe }
Run "dotnet-aot-tests.log" { & .\build\phase4\aot-tests\Tests.exe }
$fixture = "$root\build\phase3\sample-fixtures\image-0.png"
if (!(Test-Path $fixture)) {
    Run "image-fixtures.log" { & .\build\arm64\Release\xui_image_smoke.exe "$root\build\arm64\Release\xui_images.exe" "$root\build\phase3\sample-fixtures" }
}
$clients = [ordered]@{
    cpp_static = "build\phase4\cpp-static\xui_direct_sample.exe"
    cpp_abi = "build\phase4\cpp-abi\xui_abi_sample.exe"
    dotnet_fdd = "build\phase4\dotnet\Sample.exe"
    dotnet_aot = "build\phase4\aot\Sample.exe"
    rust = "build\phase4\rust\xui-sample.exe"
}
foreach ($client in $clients.GetEnumerator()) {
    Run "$($client.Key)-uia.log" { & .\build\arm64\Release\xui_bindings_smoke.exe "$root\$($client.Value)" $fixture }
    Run "$($client.Key)-callback-failure.log" { & .\build\arm64\Release\xui_bindings_smoke.exe "$root\$($client.Value)" $fixture --failure }
}
if (!$SkipMeasurements) {
    & .\tests\measure-bindings.ps1 -Runs $Runs
    & .\tests\measure-browser.ps1 -Runs $Runs -Output build\phase4\browser-after.json | Out-String
}
