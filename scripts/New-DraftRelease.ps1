param(
    [Parameter(Mandatory)][string]$Tag,
    [Parameter(Mandatory)][string]$Repository,
    [Parameter(Mandatory)][string]$AssetDirectory
)
. "$PSScriptRoot\Release.Common.ps1"
if (!$Tag.StartsWith('release/')) { throw 'Expected a release/ tag.' }
$version = $Tag.Substring('release/'.Length)
Assert-ReleaseVersion $version
$directory = (Resolve-Path $AssetDirectory).Path
$assets = @((Get-XuiReleaseAssetNames $version) + 'SHA256SUMS.txt' | ForEach-Object {
    $file = Get-Item -LiteralPath (Join-Path $directory $_)
    if ($file.Length -eq 0) { throw "Empty release asset: $($file.FullName)" }
    $file.FullName
})
$releases = gh api --paginate "repos/$Repository/releases?per_page=100" --jq '.[] | {tagName: .tag_name, isDraft: .draft}' |
    ForEach-Object { $_ | ConvertFrom-Json }
if ($LASTEXITCODE -ne 0) { throw 'Cannot list releases.' }
$existing = $releases | Where-Object tagName -CEQ $Tag
if ($existing -and !$existing.isDraft) { throw 'Refusing to change a published release.' }
if (!$existing) {
    Invoke-Checked { gh release create $Tag --repo $Repository --verify-tag --draft --title "XUI $version" --generate-notes }
}
# Handle native exit codes explicitly so PowerShell cannot terminate before a retry.
$PSNativeCommandUseErrorActionPreference = $false
$maximumAttempts = 5
foreach ($asset in $assets) {
    for ($attempt = 1; $attempt -le $maximumAttempts; $attempt++) {
        Write-Host "Uploading $asset (attempt $attempt/$maximumAttempts)."
        gh release upload $Tag --repo $Repository $asset --clobber
        $exitCode = $LASTEXITCODE
        if ($exitCode -eq 0) { break }
        if ($attempt -eq $maximumAttempts) {
            throw "Release asset upload failed for '$asset' after $maximumAttempts attempts (exit code $exitCode)."
        }
        $delay = 5 * [Math]::Pow(2, $attempt - 1)
        Write-Warning "Upload failed for '$asset' (exit code $exitCode). Retry in $delay seconds."
        Start-Sleep -Seconds $delay
    }
}
$release = gh release view $Tag --repo $Repository --json isDraft | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or !$release.isDraft) { throw 'Release must remain a draft.' }
