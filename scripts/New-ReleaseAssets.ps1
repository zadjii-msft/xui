param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$StageDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
. "$PSScriptRoot\Release.Common.ps1"
Assert-ReleaseVersion $Version
$stage = (Resolve-Path $StageDirectory).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
foreach ($rid in 'win-x64', 'win-arm64') {
    foreach ($kind in 'Samples', 'Designer') {
        $root = Join-Path $stage "$kind\$rid"
        $manifest = Get-Content "$root\manifest.json" -Raw | ConvertFrom-Json
        if ($manifest.version -cne $Version -or $manifest.runtime -cne $rid) { throw "Wrong $kind version or architecture: $root" }
        foreach ($file in $manifest.files) {
            if ((Get-FileHash (Join-Path $root $file.path)).Hash -ne $file.sha256) { throw "$kind hash mismatch: $($file.path)" }
        }
    }
}
& "$PSScriptRoot\Pack-NuGet.ps1" -Version $Version -NativeRoot "$stage\native" -OutputDirectory $output
& "$PSScriptRoot\Pack-Templates.ps1" -Version $Version -OutputDirectory $output
& "$PSScriptRoot\Pack-Cargo.ps1" -Version $Version -NativeRoot "$stage\native" -OutputDirectory $output
foreach ($rid in 'win-x64', 'win-arm64') {
    foreach ($kind in 'Samples', 'Designer') {
        $root = Join-Path $stage "$kind\$rid"
        Copy-Item "$PSScriptRoot\..\packaging\$kind.md" "$root\README.md"
        Copy-Item "$PSScriptRoot\..\LICENSE" "$root\LICENSE"
        $zip = Join-Path $output "Xui.$kind.$Version.$rid.zip"
        if (Test-Path $zip) { throw "Release asset already exists: $zip" }
        [IO.Compression.ZipFile]::CreateFromDirectory($root, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
    }
}
$assets = Get-XuiReleaseAssetNames $Version
foreach ($asset in $assets) {
    if (!(Test-Path (Join-Path $output $asset))) { throw "Missing release asset: $asset" }
}
$assets | ForEach-Object {
    $hash = (Get-FileHash (Join-Path $output $_)).Hash.ToLowerInvariant()
    "$hash  $_"
} | Set-Content "$output\SHA256SUMS.txt" -Encoding ascii
