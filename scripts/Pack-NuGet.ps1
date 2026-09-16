param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$NativeRoot,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$WorkDirectory
)
. "$PSScriptRoot\Release.Common.ps1"
Assert-ReleaseVersion $Version
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$native = (Resolve-Path $NativeRoot).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (!$WorkDirectory) { $WorkDirectory = Join-Path $repo ("build\nuget-" + [guid]::NewGuid().ToString('N')) }
$work = [IO.Path]::GetFullPath($WorkDirectory)
foreach ($rid in 'win-x64', 'win-arm64') {
    foreach ($file in 'xui.dll', 'xui.lib', 'xui_core.lib', 'xui_windows.lib') {
        if (!(Test-Path "$native\$rid\$file")) { throw "Missing native package input: $native\$rid\$file" }
    }
}
New-Item -ItemType Directory -Path $work, $output -Force | Out-Null
foreach ($project in 'Xui', 'Xui.Generator', 'Xui.Development') {
    Invoke-Checked { dotnet build "$repo\bindings\dotnet\$project\$project.csproj" -c Release "-p:Version=$Version" -o "$work\$project" --nologo }
}
Invoke-Checked {
    dotnet pack "$repo\packaging\Xui.Package.csproj" -c Release "-p:PackageVersion=$Version" `
        "-p:XuiPackageNativeRoot=$native" "-p:XuiPackageManagedRoot=$work" `
        "-p:BaseIntermediateOutputPath=$work\obj\" "-p:OutputPath=$work\bin\" -o $output --nologo
}
if (!(Test-Path "$output\Xui.$Version.nupkg")) { throw 'NuGet did not create the expected package.' }
