param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$AssetDirectory
)
. "$PSScriptRoot\..\scripts\Release.Common.ps1"
Assert-ReleaseVersion $Version
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$assets = (Resolve-Path $AssetDirectory).Path
$work = Join-Path $repo ("build\sample-tests-" + [guid]::NewGuid().ToString('N'))
[IO.Compression.ZipFile]::ExtractToDirectory("$assets\Xui.Samples.$Version.zip", $work)
Assert-SameFile "$repo\LICENSE" "$work\LICENSE"

function Get-PeImports([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $pe = [BitConverter]::ToInt32($bytes, 0x3c)
    if ([BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550) { throw "Not a PE file: $Path" }
    $sectionCount = [BitConverter]::ToUInt16($bytes, $pe + 6)
    $optionalSize = [BitConverter]::ToUInt16($bytes, $pe + 20)
    $optional = $pe + 24
    $directories = if ([BitConverter]::ToUInt16($bytes, $optional) -eq 0x20b) { $optional + 112 } else { $optional + 96 }
    $sections = for ($i = 0; $i -lt $sectionCount; $i++) {
        $section = $optional + $optionalSize + 40 * $i
        [pscustomobject]@{
            rva = [BitConverter]::ToUInt32($bytes, $section + 12)
            size = [Math]::Max([BitConverter]::ToUInt32($bytes, $section + 8), [BitConverter]::ToUInt32($bytes, $section + 16))
            offset = [BitConverter]::ToUInt32($bytes, $section + 20)
        }
    }
    function Offset([uint32]$Rva) {
        foreach ($section in $sections) {
            if ($Rva -ge $section.rva -and $Rva -lt $section.rva + $section.size) {
                return [int]($Rva - $section.rva + $section.offset)
            }
        }
        throw "Unmapped PE address in $Path"
    }
    $imports = [BitConverter]::ToUInt32($bytes, $directories + 8)
    if ($imports -eq 0) { return }
    $descriptor = Offset $imports
    while (($name = [BitConverter]::ToUInt32($bytes, $descriptor + 12)) -ne 0) {
        $start = Offset $name
        $end = $start
        while ($bytes[$end] -ne 0) { $end++ }
        [Text.Encoding]::ASCII.GetString($bytes, $start, $end - $start)
        $descriptor += 20
    }
}

$nativeTargets = [regex]::Match((Get-Content "$repo\CMakeLists.txt" -Raw), '(?s)set\(XUI_SAMPLE_TARGETS\s+([^)]+)\)').Groups[1].Value.Trim() -split '\s+'
if (!$nativeTargets.Count) { throw 'No native sample inventory found.' }
foreach ($rid in 'win-x64', 'win-arm64') {
    $root = Join-Path $work $rid
    $manifest = Get-Content "$root\manifest.json" -Raw | ConvertFrom-Json
    if ($manifest.version -cne $Version -or $manifest.runtime -cne $rid) { throw "Incorrect archive manifest: $rid" }
    foreach ($file in $manifest.files) {
        if ((Get-FileHash (Join-Path $root $file.path)).Hash -ne $file.sha256) { throw "Archive hash mismatch: $($file.path)" }
    }
    foreach ($target in $nativeTargets) {
        if (!(Test-Path "$root\native\$target.exe")) { throw "Missing native sample: $rid\$target" }
    }
    foreach ($project in Get-XuiSamples) {
        $directory = "$root\dotnet\$($project.BaseName)"
        foreach ($name in @("$($project.BaseName).exe", "$($project.BaseName).deps.json", 'coreclr.dll', 'hostfxr.dll', 'hostpolicy.dll', 'xui.dll', 'Xui.Managed.dll')) {
            if (!(Test-Path "$directory\$name")) { throw "Incomplete .NET sample: $directory\$name" }
        }
        Assert-SameFile "$root\native\xui.dll" "$directory\xui.dll"
        if (Test-Path "$directory\Xui.Development.dll") { throw "Development host in release: $directory" }
    }
    if (!(Test-Path "$root\rust\xui-sample.exe")) { throw "Missing Rust sample: $rid" }
    Assert-SameFile "$root\native\xui.dll" "$root\rust\xui.dll"
    foreach ($binary in Get-ChildItem $root -File -Recurse | Where-Object Extension -In '.exe', '.dll') {
        foreach ($dependency in Get-PeImports $binary.FullName) {
            if (Test-Path (Join-Path $binary.DirectoryName $dependency)) { continue }
            if ($dependency -match '^(api-ms-|ext-ms-)') { continue }
            if ($dependency -notmatch '^(vcruntime|msvcp)\d' -and (Test-Path "$env:windir\System32\$dependency")) { continue }
            throw "Undeployed dependency '$dependency' required by $($binary.FullName)"
        }
    }
}
Write-Output "Sample archive hashes, inventory, and PE dependencies passed. Evidence: $work"
