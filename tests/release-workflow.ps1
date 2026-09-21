. "$PSScriptRoot\..\scripts\Release.Common.ps1"
$PSNativeCommandUseErrorActionPreference = $true
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work = Join-Path $repo ("build\release-workflow-tests-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $work | Out-Null
foreach ($name in (Get-XuiReleaseAssetNames '1.2.3') + 'SHA256SUMS.txt') {
    Set-Content (Join-Path $work $name) 'release fixture' -Encoding ascii
}
$calls = [Collections.Generic.List[string]]::new()
$releaseTestState = @{
    calls = $calls
    existing = $null
    failList = $false
    uploadFailures = @{}
    delays = [Collections.Generic.List[int]]::new()
}
function gh {
    $releaseTestState.calls.Add(($args -join ' '))
    $global:LASTEXITCODE = 0
    if ($args[0] -eq 'api') {
        if ($releaseTestState.failList) { $global:LASTEXITCODE = 1; return }
        if ($null -ne $releaseTestState.existing) {
            @{ tagName = 'release/1.2.3'; isDraft = $releaseTestState.existing } | ConvertTo-Json -Compress
        }
    } elseif ($args[1] -eq 'upload') {
        Assert ($args.Count -eq 7 -and $args[2] -eq 'release/1.2.3' -and $args[3] -eq '--repo' -and
            $args[4] -eq 'fixture/xui' -and $args[6] -eq '--clobber') 'Incorrect per-asset upload arguments.'
        $name = Split-Path -Leaf $args[5]
        if ($releaseTestState.uploadFailures.ContainsKey($name) -and $releaseTestState.uploadFailures[$name] -gt 0) {
            $releaseTestState.uploadFailures[$name]--
            & $env:ComSpec /d /c 'exit 1'
            $global:LASTEXITCODE = $LASTEXITCODE
        }
    } elseif ($args[1] -eq 'view') {
        '{"isDraft":true}'
    }
}
function Start-Sleep([int]$Seconds) {
    $releaseTestState.delays.Add($Seconds)
}
function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}
try {
    $expectedAssets = @('Xui.1.2.3.nupkg', 'Xui.Templates.1.2.3.nupkg', 'xui-sys-1.2.3.crate', 'xui-1.2.3.crate',
        'Xui.Samples.1.2.3.win-x64.zip', 'Xui.Samples.1.2.3.win-arm64.zip',
        'Xui.Designer.1.2.3.win-x64.zip', 'Xui.Designer.1.2.3.win-arm64.zip')
    Assert (!(Compare-Object $expectedAssets (Get-XuiReleaseAssetNames '1.2.3'))) 'Incorrect release asset inventory.'
    Assert (@(Get-XuiSamples).BaseName.Contains('TaskCard')) 'TaskCard must remain in local sample checks.'
    Assert (!(Compare-Object @('DeclarativeSample', 'FileExplorer', 'Minesweeper', 'Sample') @((Get-XuiSamples -ReleaseOnly).BaseName))) 'Incorrect release sample inventory.'
    foreach ($version in '0.0.0', '1.2.3', '100.20.3') { Assert-ReleaseVersion $version }
    foreach ($version in '01.2.3', '1.2', '1.2.3.4', '1.2.3-beta', '1.2.3/extra', '1.2.3;bad') {
        $rejected = $false
        try { Assert-ReleaseVersion $version } catch { $rejected = $true }
        Assert $rejected "Accepted invalid version: $version"
    }
    & "$repo\scripts\New-DraftRelease.ps1" -Tag 'release/1.2.3' -Repository 'fixture/xui' -AssetDirectory $work
    Assert (@($calls | Where-Object { $_ -like 'release create *' -and $_ -match '--draft' -and $_ -match '--verify-tag' }).Count -eq 1) 'New release was not a verified-tag draft.'
    $uploads = @($calls | Where-Object { $_ -like 'release upload *' })
    Assert ($uploads.Count -eq 9) 'Expected one upload per asset.'
    foreach ($name in (Get-XuiReleaseAssetNames '1.2.3') + 'SHA256SUMS.txt') {
        $upload = @($uploads | Where-Object { $_.Contains($name) })
        Assert ($upload.Count -eq 1) "Asset must upload exactly once: $name"
        Assert ($upload[0].EndsWith('--clobber')) "Asset upload must replace partial or existing assets: $name"
    }
    Assert ($releaseTestState.delays.Count -eq 0) 'Successful uploads must not wait.'
    $calls.Clear()
    $releaseTestState.existing = $true
    & "$repo\scripts\New-DraftRelease.ps1" -Tag 'release/1.2.3' -Repository 'fixture/xui' -AssetDirectory $work
    Assert (@($calls | Where-Object { $_ -like 'release create *' }).Count -eq 0) 'Rerun created a second release.'
    Assert (@($calls | Where-Object { $_ -like 'release upload *' }).Count -eq 9) 'Rerun did not update draft assets.'

    $failedAsset = 'Xui.Samples.1.2.3.win-arm64.zip'
    foreach ($failureCount in 1, 4, 5) {
        $calls.Clear()
        $releaseTestState.delays.Clear()
        $releaseTestState.uploadFailures[$failedAsset] = $failureCount
        $failureMessage = ''
        try {
            & "$repo\scripts\New-DraftRelease.ps1" -Tag 'release/1.2.3' -Repository 'fixture/xui' -AssetDirectory $work
        } catch {
            $failureMessage = $_.Exception.Message
        }
        $exhausted = $failureCount -eq 5
        $attempts = [Math]::Min($failureCount + 1, 5)
        $uploads = @($calls | Where-Object { $_ -like 'release upload *' })
        Assert (@($uploads | Where-Object { $_.Contains($failedAsset) }).Count -eq $attempts) 'Incorrect upload retry count.'
        Assert ($releaseTestState.delays.Count -eq ($attempts - 1)) 'Incorrect retry delay count.'
        for ($i = 0; $i -lt $releaseTestState.delays.Count; $i++) {
            Assert ($releaseTestState.delays[$i] -eq (5 * [Math]::Pow(2, $i))) 'Incorrect upload retry backoff.'
        }
        foreach ($name in 'Xui.1.2.3.nupkg', 'Xui.Templates.1.2.3.nupkg', 'xui-sys-1.2.3.crate', 'xui-1.2.3.crate', 'Xui.Samples.1.2.3.win-x64.zip') {
            Assert (@($uploads | Where-Object { $_.Contains($name) }).Count -eq 1) "Retry repeated a successful upload: $name"
        }
        Assert (@($calls | Where-Object { $_ -like 'release create *' }).Count -eq 0) 'Upload retry created another release.'
        if ($exhausted) {
            Assert ($failureMessage.Contains($failedAsset) -and $failureMessage.Contains('5 attempts') -and $failureMessage.Contains('exit code 1')) 'Upload exhaustion must report the asset, attempts, and exit code.'
            Assert ($uploads.Count -eq 10) 'Upload continued after retry exhaustion.'
            Assert (@($calls | Where-Object { $_ -like 'release view *' }).Count -eq 0) 'Failed uploads reached the success check.'
        } else {
            Assert ($failureMessage -eq '') "Transient upload failure was not recovered: $failureMessage"
            Assert ($uploads.Count -eq (9 + $failureCount)) 'Recovery did not upload all assets.'
            Assert (@($calls | Where-Object { $_ -like 'release view *' }).Count -eq 1) 'Recovered uploads skipped the draft check.'
        }
    }
    $calls.Clear()
    $releaseTestState.delays.Clear()
    & "$repo\scripts\New-DraftRelease.ps1" -Tag 'release/1.2.3' -Repository 'fixture/xui' -AssetDirectory $work
    Assert (@($calls | Where-Object { $_ -like 'release create *' }).Count -eq 0) 'Rerun after upload exhaustion created another release.'
    Assert (@($calls | Where-Object { $_ -like 'release upload *' }).Count -eq 9) 'Rerun after upload exhaustion did not update all assets.'
    Assert ($releaseTestState.delays.Count -eq 0) 'Rerun retained an exhausted retry budget.'
    Assert $PSNativeCommandUseErrorActionPreference 'Release script changed the caller native error preference.'
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
    foreach ($name in $expectedAssets) {
        $calls.Clear()
        $path = Join-Path $work $name
        Remove-Item $path
        $rejected = $false
        try {
            & "$repo\scripts\New-DraftRelease.ps1" -Tag 'release/1.2.3' -Repository 'fixture/xui' -AssetDirectory $work
        } catch {
            $rejected = $true
        }
        Assert $rejected "Accepted a release without $name."
        Assert ($calls.Count -eq 0) 'Contacted GitHub before checking all assets.'
        Set-Content $path 'release fixture' -Encoding ascii
    }
    Write-Output 'Release versions, draft creation, upload retries, reruns, and publication guards passed without network calls.'
    $global:LASTEXITCODE = 0
} finally {
    Remove-Item Function:\gh
    Remove-Item Function:\Start-Sleep
    Remove-Item -LiteralPath $work -Recurse -Force
}
