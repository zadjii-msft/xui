#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'portable-packages.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$names = @('Assert', 'Assert-ResolvedVersion', 'Assert-RestoredCohort')
$functions = @($ast.EndBlock.Statements | Where-Object {
    $_ -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -in $names
})
if ($functions.Count -ne $names.Count) { throw 'The package cohort guards could not be loaded.' }
foreach ($definition in $functions) { Invoke-Expression $definition.Extent.Text }

$work = Join-Path ([IO.Path]::GetTempPath()) ('xui-package-versions-' + [guid]::NewGuid().ToString('N'))
$shared = Join-Path $work 'Shared'
$hostProject = Join-Path $work 'Web\PortableConsumer.Web.csproj'
function Write-Lock([string]$Directory, [string[]]$Packages) {
    $libraries = [ordered]@{}
    foreach ($package in $Packages) { $libraries[$package] = @{ type = 'package' } }
    New-Item -ItemType Directory (Join-Path $Directory 'obj') -Force | Out-Null
    @{ libraries = $libraries } | ConvertTo-Json -Depth 4 |
        Set-Content (Join-Path $Directory 'obj\project.assets.json') -Encoding utf8
}
function Expect-Rejection([string]$Pattern) {
    try { Assert-RestoredCohort $work $hostProject '0.1.0-preview.3' }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        return
    }
    throw "Expected cohort rejection: $Pattern"
}
try {
    Write-Lock $shared @('Xui.Experimental.Compiler/0.1.0-preview.3', 'Xui.Experimental.Portable/0.1.0-preview.3')
    Write-Lock (Split-Path $hostProject) @('Xui.Experimental.Web/0.1.0-preview.3', 'Xui.Experimental.Portable/0.1.0-preview.3')
    Assert-RestoredCohort $work $hostProject '0.1.0-preview.3'
    Write-Lock $shared @('Xui.Experimental.Compiler/0.1.0-preview.1', 'Xui.Experimental.Portable/0.1.0-preview.3')
    Expect-Rejection 'Shared.*Compiler/0\.1\.0-preview\.1'
    Write-Lock $shared @('xui.experimental.compiler/0.1.0-preview.1', 'Xui.Experimental.Portable/0.1.0-preview.3')
    Expect-Rejection 'Shared.*compiler/0\.1\.0-preview\.1'
    Write-Lock $shared @('Xui.Experimental.Compiler/0.1.0-preview.3', 'Xui.Experimental.Portable/0.1.0-preview.1')
    Expect-Rejection 'Shared.*Portable/0\.1\.0-preview\.1'
    Write-Lock $shared @('Xui.Experimental.Compiler/0.1.0-preview.3', 'Xui.Experimental.Portable/0.1.0-preview.3')
    Write-Lock (Split-Path $hostProject) @('Xui.Experimental.Web/0.1.0-preview.1', 'Xui.Experimental.Portable/0.1.0-preview.3')
    Expect-Rejection 'Web.*Web/0\.1\.0-preview\.1'
    Write-Lock $shared @()
    Expect-Rejection 'No portable packages.*Shared'
    Write-Lock $shared @('Xui.Experimental.Compiler/0.1.0-preview.3', 'Xui.Experimental.Portable/0.1.0-preview.3')
    Write-Lock (Split-Path $hostProject) @()
    Expect-Rejection 'No portable packages.*Web'
} finally {
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
}
Write-Output 'Portable package cohort guards reject private stale compilers and mixed shared/host versions.'
