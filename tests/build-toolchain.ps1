. "$PSScriptRoot\..\scripts\Release.Common.ps1"

function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}

$vs = Get-XuiVisualStudio 'Microsoft.VisualStudio.Component.VC.CMake.Project'
$generator = Get-XuiGenerator
Assert ($generator -eq 'Visual Studio 17 2022' -or $generator -eq 'Visual Studio 18 2026') "Unexpected generator: $generator"
Assert (![string]::IsNullOrWhiteSpace($vs.installationPath)) 'Visual Studio discovery returned no installation path.'
Assert (Test-Path -LiteralPath (Get-XuiCMake) -PathType Leaf) 'CMake was not found.'
$originalPath = $env:PATH
foreach ($architecture in 'x64', 'ARM64') {
    Assert (Test-Path -LiteralPath (Get-XuiVcVars $architecture) -PathType Leaf) "Missing $architecture compiler environment."
    Invoke-XuiVcVarsCommand $architecture 'where vswhere.exe >nul && where link.exe >nul && where dotnet.exe >nul'
}
Assert ($env:PATH -ceq $originalPath) 'Developer command changed the caller PATH.'

function Get-XuiVisualStudio([string]$Component) {
    [pscustomobject]@{ installationVersion = '18.0.0.0' }
}
Assert ((Get-XuiGenerator) -eq 'Visual Studio 18 2026') 'Visual Studio 2026 generator was not selected.'
function Get-XuiVisualStudio([string]$Component) {
    [pscustomobject]@{ installationVersion = '19.0.0.0' }
}
$rejected = $false
try { Get-XuiGenerator } catch { $rejected = $_.Exception.Message -like 'Unsupported Visual Studio version:*' }
Assert $rejected 'Unsupported Visual Studio versions must fail explicitly.'
Write-Output "Visual Studio toolchain discovery and generator selection passed ($generator at $($vs.installationPath))."
