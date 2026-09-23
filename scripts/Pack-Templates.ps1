param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$WorkDirectory
)
. "$PSScriptRoot\Release.Common.ps1"
Assert-ReleaseVersion $Version
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (!$WorkDirectory) { $WorkDirectory = Join-Path $repo ("build\templates-" + [guid]::NewGuid().ToString('N')) }
$work = [IO.Path]::GetFullPath($WorkDirectory)
New-Item -ItemType Directory -Path "$work\content", $output -Force | Out-Null
$project = Get-Content "$repo\templates\xui\XuiApp.csproj" -Raw
if ([regex]::Matches($project, '__XUI_PACKAGE_VERSION__').Count -ne 1) {
    throw 'Expected exactly one XUI package version token in the template project.'
}
$project.Replace('__XUI_PACKAGE_VERSION__', $Version) |
    Set-Content "$work\content\XuiApp.csproj" -Encoding utf8
Invoke-Checked {
    dotnet pack "$repo\packaging\Xui.Templates.csproj" -c Release "-p:PackageVersion=$Version" `
        "-p:XuiTemplateRoot=$work\content" "-p:BaseIntermediateOutputPath=$work\obj\" `
        "-p:OutputPath=$work\bin\" -o $output --nologo
}
if (!(Test-Path "$output\Xui.Templates.$Version.nupkg")) { throw 'NuGet did not create the expected template package.' }
