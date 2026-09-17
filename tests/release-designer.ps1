param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$AssetDirectory,
    [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string]$Architecture
)
. "$PSScriptRoot\..\scripts\Release.Common.ps1"
Assert-ReleaseVersion $Version
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$assets = (Resolve-Path $AssetDirectory).Path
$work = Join-Path $repo ("build\designer-tests-" + [guid]::NewGuid().ToString('N'))
$rid = if ($Architecture -eq 'ARM64') { 'win-arm64' } else { 'win-x64' }
$root = Join-Path $work $rid
[IO.Compression.ZipFile]::ExtractToDirectory("$assets\Xui.Designer.$Version.$rid.zip", $root)
Assert-SameFile "$repo\LICENSE" "$root\LICENSE"
Assert-SameFile "$repo\packaging\DESIGNER.md" "$root\README.md"
$manifest = Get-Content "$root\manifest.json" -Raw | ConvertFrom-Json
if ($manifest.version -cne $Version -or $manifest.runtime -cne $rid) { throw "Incorrect Designer archive manifest: $rid" }
foreach ($file in $manifest.files) {
    if ((Get-FileHash (Join-Path $root $file.path)).Hash -ne $file.sha256) { throw "Designer archive hash mismatch: $($file.path)" }
}
$expectedFiles = @($manifest.files.path) + @('manifest.json', 'LICENSE', 'README.md')
$actualFiles = @(Get-ChildItem $root -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($root, $_.FullName) })
if (Compare-Object $expectedFiles $actualFiles) { throw "Unexpected or untracked files in Designer archive: $rid" }
foreach ($name in 'Designer.exe', 'Designer.dll', 'Designer.deps.json', 'Designer.runtimeconfig.json',
    'Xui.Managed.dll', 'Xui.Generator.dll', 'Microsoft.CodeAnalysis.dll', 'Microsoft.CodeAnalysis.CSharp.dll',
    'xui.dll', 'coreclr.dll', 'hostfxr.dll', 'hostpolicy.dll', 'DOTNET-LICENSE.txt', 'THIRD-PARTY-NOTICES.TXT') {
    if (!(Test-Path "$root\$name")) { throw "Incomplete Designer archive: $rid\$name" }
}
if (Get-ChildItem $root -Filter '*.pdb' -Recurse) { throw "Debug symbols in Designer archive: $rid" }
if (Test-Path "$root\Xui.Development.dll") { throw "Development host in Designer archive: $rid" }
$runtime = (Get-Content "$root\Designer.runtimeconfig.json" -Raw | ConvertFrom-Json).runtimeOptions
if ($runtime.tfm -cne 'net10.0' -or !$runtime.PSObject.Properties['includedFrameworks'] -or
    $runtime.PSObject.Properties['framework'] -or $runtime.PSObject.Properties['frameworks']) {
    throw "Designer must contain its own .NET runtime: $rid"
}
foreach ($name in 'Designer.exe', 'xui.dll', 'coreclr.dll', 'hostfxr.dll', 'hostpolicy.dll') {
    $bytes = [IO.File]::ReadAllBytes("$root\$name")
    $pe = [BitConverter]::ToInt32($bytes, 0x3c)
    $expectedMachine = if ($rid -eq 'win-arm64') { 0xaa64 } else { 0x8664 }
    if ([BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes, $pe + 4) -ne $expectedMachine) {
        throw "Incorrect Designer PE architecture: $rid\$name"
    }
}

$start = [Diagnostics.ProcessStartInfo]::new("$root\Designer.exe")
$start.ArgumentList.Add('--smoke')
$start.WorkingDirectory = $root
$start.UseShellExecute = $false
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.Environment['PATH'] = "$env:windir\System32;$env:windir"
$start.Environment['DOTNET_ROOT'] = "$work\no-installed-dotnet"
$start.Environment['DOTNET_ROOT_X64'] = "$work\no-installed-dotnet"
$start.Environment['DOTNET_ROOT_ARM64'] = "$work\no-installed-dotnet"
$process = [Diagnostics.Process]::Start($start)
try {
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (!$process.WaitForExit(90000)) {
        $process.Kill($true)
        $process.WaitForExit()
        throw "Published Designer smoke test timed out: $rid"
    }
    $stdout.GetAwaiter().GetResult() | Set-Content "$work\stdout.txt"
    $errorText = $stderr.GetAwaiter().GetResult()
    $errorText | Set-Content "$work\stderr.txt"
    if ($process.ExitCode -ne 0) { throw "Published Designer smoke test failed ($($process.ExitCode)): $errorText" }
} finally {
    $process.Dispose()
}
Write-Output "Designer archive, runtime compilation, preview recovery, and file smoke checks passed. Evidence: $work"
