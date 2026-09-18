[CmdletBinding(DefaultParameterSetName = 'Feed')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Package')][string]$LshPackageDirectory,
    [Parameter(ParameterSetName = 'Feed')][string]$LshPackageFeed = "$PSScriptRoot\..\dep",
    [ValidateSet('ARM64', 'x64')][string]$Architecture = $(if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'ARM64' } else { 'x64' }),
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$CMakePath
)
. "$PSScriptRoot\Release.Common.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!$BuildDirectory) { $BuildDirectory = Join-Path $repo "build\$Architecture" }
$build = [IO.Path]::GetFullPath($BuildDirectory)
if (!$CMakePath) { $CMakePath = Get-XuiCMake }
$rid = if ($Architecture -eq 'ARM64') { 'win-arm64' } else { 'win-x64' }

if ($PSCmdlet.ParameterSetName -eq 'Feed') {
    $feed = (Resolve-Path -LiteralPath $LshPackageFeed).Path
    if (!(Test-Path -LiteralPath "$feed\Lsh.0.3.0.nupkg" -PathType Leaf)) {
        throw "The local feed must contain Lsh.0.3.0.nupkg: $feed"
    }
    $packages = Join-Path $repo 'build\packages'
    Invoke-Checked {
        dotnet restore "$repo\integrations\lsh\Lsh.Package.csproj" --source $feed --packages $packages --nologo
    }
    $LshPackageDirectory = Join-Path $packages 'lsh\0.3.0'
}
$lsh = (Resolve-Path -LiteralPath $LshPackageDirectory).Path
Invoke-Checked {
    & $CMakePath -S $repo -B $build -G 'Visual Studio 17 2022' -A $Architecture `
        '-DXUI_ENABLE_LSH=ON' '-DXUI_REQUIRE_LSH=ON' "-DXUI_LSH_PACKAGE_DIR=$lsh"
}
Invoke-Checked { & $CMakePath --build $build --config Release --target xui --parallel 4 }
$native = Join-Path $build 'Release'
$project = "$repo\bindings\dotnet\Designer\Designer.csproj"
$outputArguments = @()
if ($OutputDirectory) { $outputArguments = @("-p:OutputPath=$([IO.Path]::GetFullPath($OutputDirectory))") }
Invoke-Checked { dotnet build $project -c Release -r $rid "-p:XuiNativeDir=$native" @outputArguments --nologo }
$output = (Invoke-Checked {
    dotnet msbuild $project -p:Configuration=Release "-p:RuntimeIdentifier=$rid" "-p:XuiNativeDir=$native" @outputArguments -getProperty:TargetDir
}).Trim()
foreach ($name in 'xui.dll', 'lsh_lib.dll', 'LSH-LICENSE.txt') {
    Assert-SameFile "$native\$name" "$output\$name"
}
Write-Output "Designer with LSH syntax highlighting: ${output}Designer.exe"
