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
    $sampleRoot = Join-Path $stage "samples\$rid"
    $manifest = Get-Content "$sampleRoot\manifest.json" -Raw | ConvertFrom-Json
    if ($manifest.version -cne $Version -or $manifest.runtime -cne $rid) { throw "Wrong sample version or architecture: $sampleRoot" }
    foreach ($file in $manifest.files) {
        if ((Get-FileHash (Join-Path $sampleRoot $file.path)).Hash -ne $file.sha256) { throw "Sample hash mismatch: $($file.path)" }
    }
}
& "$PSScriptRoot\Pack-NuGet.ps1" -Version $Version -NativeRoot "$stage\native" -OutputDirectory $output
& "$PSScriptRoot\Pack-Cargo.ps1" -Version $Version -NativeRoot "$stage\native" -OutputDirectory $output
Copy-Item "$PSScriptRoot\..\packaging\SAMPLES.md" "$stage\samples\README.md"
Copy-Item "$PSScriptRoot\..\LICENSE" "$stage\samples\LICENSE"
$zip = Join-Path $output "Xui.Samples.$Version.zip"
if (Test-Path $zip) { throw "Release asset already exists: $zip" }
[IO.Compression.ZipFile]::CreateFromDirectory("$stage\samples", $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
$assets = Get-XuiReleaseAssetNames $Version
foreach ($asset in $assets) {
    if (!(Test-Path (Join-Path $output $asset))) { throw "Missing release asset: $asset" }
}
$assets | ForEach-Object {
    $hash = (Get-FileHash (Join-Path $output $_)).Hash.ToLowerInvariant()
    "$hash  $_"
} | Set-Content "$output\SHA256SUMS.txt" -Encoding ascii
