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
Invoke-Checked { gh release upload $Tag --repo $Repository @assets --clobber }
$release = gh release view $Tag --repo $Repository --json isDraft | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or !$release.isDraft) { throw 'Release must remain a draft.' }
