. "$PSScriptRoot\..\scripts\Release.Common.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work = Join-Path $repo ("build\release-workflow-tests-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $work | Out-Null
foreach ($name in (Get-XuiReleaseAssetNames '1.2.3') + 'SHA256SUMS.txt') {
    Set-Content (Join-Path $work $name) 'release fixture' -Encoding ascii
}
$calls = [Collections.Generic.List[string]]::new()
$releaseTestState = @{ calls = $calls; existing = $null; failList = $false }
function gh {
    $releaseTestState.calls.Add(($args -join ' '))
    $global:LASTEXITCODE = 0
    if ($args[0] -eq 'api') {
        if ($releaseTestState.failList) { $global:LASTEXITCODE = 1; return }
        if ($null -ne $releaseTestState.existing) {
            @{ tagName = 'release/1.2.3'; isDraft = $releaseTestState.existing } | ConvertTo-Json -Compress
        }
    } elseif ($args[1] -eq 'view') {
        '{"isDraft":true}'
    }
}
function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}
try {
    foreach ($version in '0.0.0', '1.2.3', '100.20.3') { Assert-ReleaseVersion $version }
    foreach ($version in '01.2.3', '1.2', '1.2.3.4', '1.2.3-beta', '1.2.3/extra', '1.2.3;bad') {
        $rejected = $false
        try { Assert-ReleaseVersion $version } catch { $rejected = $true }
        Assert $rejected "Accepted invalid version: $version"
    }
    & "$repo\scripts\New-DraftRelease.ps1" -Tag 'release/1.2.3' -Repository 'fixture/xui' -AssetDirectory $work
    Assert (@($calls | Where-Object { $_ -like 'release create *' -and $_ -match '--draft' -and $_ -match '--verify-tag' }).Count -eq 1) 'New release was not a verified-tag draft.'
    $uploads = @($calls | Where-Object { $_ -like 'release upload *' })
    Assert ($uploads.Count -eq 1) 'Expected one asset upload.'
    foreach ($name in (Get-XuiReleaseAssetNames '1.2.3') + 'SHA256SUMS.txt') {
        Assert ($uploads[0].Contains($name)) "Asset omitted from upload: $name"
    }
    $calls.Clear()
    $releaseTestState.existing = $true
    & "$repo\scripts\New-DraftRelease.ps1" -Tag 'release/1.2.3' -Repository 'fixture/xui' -AssetDirectory $work
    Assert (@($calls | Where-Object { $_ -like 'release create *' }).Count -eq 0) 'Rerun created a second release.'
    Assert (@($calls | Where-Object { $_ -like 'release upload *' }).Count -eq 1) 'Rerun did not update draft assets.'
    foreach ($failure in 'published', 'list') {
        $calls.Clear()
        $releaseTestState.existing = $false
        $releaseTestState.failList = $failure -eq 'list'
        $rejected = $false
        try {
            & "$repo\scripts\New-DraftRelease.ps1" -Tag 'release/1.2.3' -Repository 'fixture/xui' -AssetDirectory $work
        } catch {
            $rejected = $true
        }
        Assert $rejected "Release operation did not reject $failure."
        Assert (@($calls | Where-Object { $_ -like 'release *' }).Count -eq 0) "Release mutation after $failure."
    }
    Write-Output 'Release versions, draft creation, reruns, and publication guards passed without network calls.'
    $global:LASTEXITCODE = 0
} finally {
    Remove-Item Function:\gh
}
