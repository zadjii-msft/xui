. "$PSScriptRoot\..\scripts\Release.Common.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work = Join-Path $repo ("build\release-packaging-tests-" + [guid]::NewGuid().ToString('N'))
$fixture = Join-Path $work 'fixture'
New-Item -ItemType Directory -Path "$fixture\scripts", "$fixture\packaging" -Force | Out-Null
Copy-Item "$repo\scripts\Release.Common.ps1", "$repo\scripts\New-ReleaseAssets.ps1" "$fixture\scripts"
Copy-Item "$repo\packaging\SAMPLES.md" "$fixture\packaging"
Copy-Item "$repo\LICENSE" $fixture
Set-Content "$fixture\scripts\Pack-NuGet.ps1" @'
param($Version, $NativeRoot, $OutputDirectory)
Set-Content "$OutputDirectory\Xui.$Version.nupkg" 'NuGet fixture'
'@
Set-Content "$fixture\scripts\Pack-Cargo.ps1" @'
param($Version, $NativeRoot, $OutputDirectory)
Set-Content "$OutputDirectory\xui-sys-$Version.crate" 'Cargo sys fixture'
Set-Content "$OutputDirectory\xui-$Version.crate" 'Cargo wrapper fixture'
'@
function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}
function Expect-Failure([scriptblock]$Action, [string]$Pattern) {
    try { & $Action } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        return
    }
    throw "Expected failure matching $Pattern"
}
foreach ($rid in 'win-x64', 'win-arm64') {
    $root = "$work\stage\samples\$rid"
    New-Item -ItemType Directory "$root\native" -Force | Out-Null
    Set-Content "$root\native\sample.exe" "$rid inert fixture"
    [ordered]@{
        version = '1.2.3'
        runtime = $rid
        files = @(@{ path = 'native\sample.exe'; sha256 = (Get-FileHash "$root\native\sample.exe").Hash })
    } | ConvertTo-Json -Depth 5 | Set-Content "$root\manifest.json"
}
$script = "$fixture\scripts\New-ReleaseAssets.ps1"
$arguments = @{ Version = '1.2.3'; StageDirectory = "$work\stage"; OutputDirectory = "$work\assets" }
& $script @arguments
$expectedAssets = @((Get-XuiReleaseAssetNames '1.2.3') + 'SHA256SUMS.txt')
Assert (!(Compare-Object $expectedAssets @(Get-ChildItem "$work\assets" -File | Select-Object -ExpandProperty Name))) 'Incorrect asset inventory.'
foreach ($rid in 'win-x64', 'win-arm64') {
    $root = "$work\extracted\$rid"
    [IO.Compression.ZipFile]::ExtractToDirectory("$work\assets\Xui.Samples.1.2.3.$rid.zip", $root)
    Assert-SameFile "$repo\LICENSE" "$root\LICENSE"
    Assert-SameFile "$repo\packaging\SAMPLES.md" "$root\README.md"
    Assert-SameFile "$work\stage\samples\$rid\native\sample.exe" "$root\native\sample.exe"
    $manifest = Get-Content "$root\manifest.json" -Raw | ConvertFrom-Json
    Assert ($manifest.runtime -ceq $rid -and $manifest.version -ceq '1.2.3') 'Wrong archive manifest.'
    $files = @(Get-ChildItem $root -Recurse -File | ForEach-Object { [IO.Path]::GetRelativePath($root, $_.FullName) })
    Assert (!(Compare-Object @('LICENSE', 'README.md', 'manifest.json', 'native\sample.exe') $files)) 'Archive contains unexpected files or another architecture.'
}
$checksums = @(Get-Content "$work\assets\SHA256SUMS.txt")
Assert ($checksums.Count -eq 5) 'Expected a checksum for every asset.'
foreach ($asset in Get-XuiReleaseAssetNames '1.2.3') {
    $expected = "$((Get-FileHash "$work\assets\$asset").Hash.ToLowerInvariant())  $asset"
    Assert ($checksums -ccontains $expected) "Missing or incorrect checksum: $asset"
}
Expect-Failure { & $script @arguments } 'Release asset already exists'
$arguments.OutputDirectory = "$work\invalid-assets"
$arguments.Version = '1.2.4'
Expect-Failure { & $script @arguments } 'Wrong sample version or architecture'
$arguments.Version = '1.2.3'
$manifestPath = "$work\stage\samples\win-arm64\manifest.json"
$original = Get-Content $manifestPath -Raw
Set-Content $manifestPath ($original.Replace('win-arm64', 'win-x64'))
Expect-Failure { & $script @arguments } 'Wrong sample version or architecture'
Set-Content $manifestPath $original
Add-Content "$work\stage\samples\win-arm64\native\sample.exe" 'corrupted'
Expect-Failure { & $script @arguments } 'Sample hash mismatch'
Assert (@(Get-ChildItem "$work\invalid-assets" -File).Count -eq 0) 'Invalid inputs produced release assets.'
Write-Output 'Per-architecture archives, licenses, checksums, and invalid-input guards passed.'
