param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string]$Architecture,
    [Parameter(Mandatory)][string]$StageDirectory,
    [string]$BuildDirectory,
    [string]$Generator = 'Visual Studio 17 2022'
)
. "$PSScriptRoot\Release.Common.ps1"
Assert-ReleaseVersion $Version
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stage = [IO.Path]::GetFullPath($StageDirectory)
if (!$BuildDirectory) { $BuildDirectory = Join-Path $repo "build\release\$Architecture" }
$build = [IO.Path]::GetFullPath($BuildDirectory)
$rid = if ($Architecture -eq 'ARM64') { 'win-arm64' } else { 'win-x64' }
$target = if ($Architecture -eq 'ARM64') { 'aarch64-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
$native = Join-Path $stage "native\$rid"
$samples = Join-Path $stage "samples\$rid"
if ((Test-Path $native) -or (Test-Path $samples)) { throw "Use a fresh staging directory: $stage" }
$cmake = Get-XuiCMake
Invoke-Checked { & $cmake -S $repo -B $build -G $Generator -A $Architecture -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF -DXUI_ENABLE_IPO=OFF -DXUI_ENABLE_WEBVIEW2=OFF }
Invoke-Checked { & $cmake --build $build --config Release --parallel 4 }
Invoke-Checked { & $cmake --install $build --config Release --component Native --prefix $native }
Invoke-Checked { & $cmake --install $build --config Release --component Samples --prefix "$samples\native" }

foreach ($project in Get-XuiSamples) {
    $destination = Join-Path $samples "dotnet\$($project.BaseName)"
    Invoke-Checked { dotnet publish $project.FullName -c Release -r $rid --self-contained true "-p:Version=$Version" "-p:XuiNativeDir=$native" -o $destination --nologo }
    Assert-SameFile "$native\xui.dll" "$destination\xui.dll"
    if (!(Test-Path "$destination\$($project.BaseName).exe") -or !(Test-Path "$destination\coreclr.dll")) {
        throw "Incomplete self-contained sample: $destination"
    }
    if (Test-Path "$destination\Xui.Development.dll") { throw "Development host leaked into $destination" }
}

$vcvars = Get-XuiVcVars $Architecture
$oldLibDir = $env:XUI_LIB_DIR
try {
    $env:XUI_LIB_DIR = $native
    $cargoOutput = Join-Path $build 'cargo'
    Invoke-Checked {
        & $env:ComSpec /d /c "call `"$vcvars`" >nul && cd /d `"$repo\bindings\rust`" && cargo build --locked --release --target $target --target-dir `"$cargoOutput`" -p xui-sample"
    }
    New-Item -ItemType Directory -Path "$samples\rust" -Force | Out-Null
    Copy-Item "$cargoOutput\$target\release\xui-sample.exe" "$samples\rust"
    Copy-Item "$native\xui.dll" "$samples\rust"
} finally {
    $env:XUI_LIB_DIR = $oldLibDir
}

$files = Get-ChildItem $samples -File -Recurse | ForEach-Object {
    [ordered]@{ path = [IO.Path]::GetRelativePath($samples, $_.FullName); sha256 = (Get-FileHash $_.FullName).Hash }
}
[ordered]@{ version = $Version; runtime = $rid; files = @($files) } |
    ConvertTo-Json -Depth 5 | Set-Content "$samples\manifest.json" -Encoding utf8
Write-Output "Release inputs: $stage"
