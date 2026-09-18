param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][ValidateSet('win-x64', 'win-arm64')][string]$RuntimeIdentifier,
    [Parameter(Mandatory)][string]$NativeDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
. "$PSScriptRoot\Release.Common.ps1"
Assert-ReleaseVersion $Version
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$native = (Resolve-Path $NativeDirectory).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
$project = "$repo\bindings\dotnet\Designer\Designer.csproj"
if (Test-Path $output) { throw "Use a fresh Designer output directory: $output" }
Invoke-Checked {
    dotnet publish $project -c Release -r $RuntimeIdentifier --self-contained true `
        -p:PublishAot=false -p:PublishTrimmed=false -p:PublishSingleFile=false `
        -p:DebugType=None -p:DebugSymbols=false "-p:Version=$Version" "-p:XuiNativeDir=$native" -o $output --nologo
}
Assert-SameFile "$native\xui.dll" "$output\xui.dll"
foreach ($name in 'Designer.exe', 'Designer.dll', 'Designer.deps.json', 'Designer.runtimeconfig.json',
    'Xui.Managed.dll', 'Xui.Generator.dll', 'Microsoft.CodeAnalysis.dll', 'Microsoft.CodeAnalysis.CSharp.dll',
    'coreclr.dll', 'hostfxr.dll', 'hostpolicy.dll') {
    if (!(Test-Path "$output\$name")) { throw "Incomplete Designer deployment: $output\$name" }
}
if (Test-Path "$output\Xui.Development.dll") { throw "Development host leaked into $output" }
$resolved = Invoke-Checked {
    dotnet msbuild $project -p:Configuration=Release "-p:RuntimeIdentifier=$RuntimeIdentifier" -p:SelfContained=true `
        -target:ResolveFrameworkReferences -getItem:ResolvedRuntimePack
} | ConvertFrom-Json
$runtimePack = @($resolved.Items.ResolvedRuntimePack | Where-Object FrameworkName -EQ 'Microsoft.NETCore.App')
if ($runtimePack.Count -ne 1) { throw 'Expected one .NET runtime pack for Designer.' }
Copy-Item "$($runtimePack[0].PackageDirectory)\LICENSE.txt" "$output\DOTNET-LICENSE.txt"
Copy-Item "$($runtimePack[0].PackageDirectory)\THIRD-PARTY-NOTICES.TXT" $output
Write-XuiArchiveManifest $output $Version $RuntimeIdentifier
Write-Output "Designer release inputs: $output"
