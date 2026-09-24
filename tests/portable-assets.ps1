#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$Version = '0.1.0-preview.1',
    [Parameter(Mandatory)][string]$AssetDirectory,
    [string]$WorkDirectory,
    [ValidateSet('Windows', 'Android', 'Web')][string[]]$Platform = @('Windows', 'Android', 'Web'),
    [string]$FrameworkSource = 'https://api.nuget.org/v3/index.json',
    [string]$AndroidSdkDirectory,
    [string]$JavaSdkDirectory,
    [switch]$Browser
)
. "$PSScriptRoot\..\scripts\Release.Common.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$feed = (Resolve-Path -LiteralPath $AssetDirectory).Path
$keep = !!$WorkDirectory
if (!$WorkDirectory) { $WorkDirectory = Join-Path ([IO.Path]::GetTempPath()) ("xui-assets-" + [guid]::NewGuid().ToString('N')) }
$work = [IO.Path]::GetFullPath($WorkDirectory)
if ($work -eq $repo -or $work.StartsWith("$repo$([IO.Path]::DirectorySeparatorChar)", [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Asset consumers must be generated outside the repository.'
}
if (Test-Path -LiteralPath $work) { throw 'Use a fresh WorkDirectory for isolated asset package restoration.' }
if (!$Platform -or ($Browser -and $Platform -notcontains 'Web')) { throw 'Select platforms; Browser requires Web.' }
function Assert([bool]$Value, [string]$Message) { if (!$Value) { throw $Message } }
$oldPackages = $env:NUGET_PACKAGES
$oldHttp = $env:NUGET_HTTP_CACHE_PATH
New-Item -ItemType Directory -Path $work | Out-Null
$env:NUGET_PACKAGES = "$work\packages"
$env:NUGET_HTTP_CACHE_PATH = "$work\http"
try {
    foreach ($name in 'Compiler', 'Portable') {
        [IO.Compression.ZipFile]::ExtractToDirectory("$feed\Xui.Experimental.$name.$Version.nupkg", "$work\expanded\$name")
    }
    Assert (Test-Path "$work\expanded\Compiler\build\Xui.Experimental.Assets.targets") 'Asset build targets are missing from the compiler package.'
    $escapedFeed = [Security.SecurityElement]::Escape($feed)
    $escapedFramework = [Security.SecurityElement]::Escape($FrameworkSource)
    @"
<configuration>
  <packageSources><clear/><add key="Local" value="$escapedFeed"/><add key="Framework" value="$escapedFramework"/></packageSources>
  <packageSourceMapping><clear/><packageSource key="Local"><package pattern="Xui.Experimental.*"/></packageSource><packageSource key="Framework"><package pattern="*"/></packageSource></packageSourceMapping>
</configuration>
"@ | Set-Content "$work\NuGet.Config" -Encoding utf8
    $fixture = "$PSScriptRoot\fixtures\portable-assets"
    $application = "$work\application with spaces"
    [byte[]]$bytes = @(0..4096 | ForEach-Object { [byte]($_ % 251) })
    $encoded = [Convert]::ToBase64String($bytes)
    foreach ($file in Get-ChildItem -LiteralPath $fixture -File -Recurse) {
        $relative = [IO.Path]::GetRelativePath($fixture, $file.FullName)
        if ($relative -match '(^|[\\/])(bin|obj)([\\/]|$)') { continue }
        $destination = Join-Path $application $relative
        New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
        (Get-Content -LiteralPath $file.FullName -Raw).Replace('__ASSET_PACKAGE_VERSION__', $Version).Replace('__ASSET_BYTES_BASE64__', $encoded) |
            Set-Content -LiteralPath $destination -Encoding utf8
    }
    New-Item -ItemType Directory "$application\Shared\Fixtures" -Force | Out-Null
    [IO.File]::WriteAllBytes("$application\Shared\Fixtures\owned.bin", $bytes)
    foreach ($platformName in $Platform | Select-Object -Unique) {
        $project = "$application\$platformName\AssetProof.$platformName.csproj"
        $arguments = @()
        if ($platformName -eq 'Android') {
            $arguments += '-p:RuntimeIdentifier=android-x64'
            foreach ($name in 'AndroidSdkDirectory','JavaSdkDirectory') {
                $path = Get-Variable -Name $name -ValueOnly
                if ($path) { $arguments += "-p:${name}=$path" }
            }
        }
        Invoke-Checked { dotnet restore $project --configfile "$work\NuGet.Config" @arguments --nologo }
        Invoke-Checked { dotnet build $project -c Release --no-restore @arguments --nologo }
        if ($platformName -eq 'Windows') {
            Invoke-Checked { dotnet run --project $project -c Release --no-build --no-restore }
        } elseif ($platformName -eq 'Web') {
            Invoke-Checked { dotnet publish $project -c Release --no-restore -o "$work\published\Web" --nologo }
            if ($Browser) {
                Invoke-Checked { node "$PSScriptRoot\portable-assets.browser.mjs" "$work\published\Web\wwwroot" "$repo\bindings\dotnet\Experimental\WebDemo.Tests\package.json" }
            }
        } else {
            $apks = @(Get-ChildItem "$application\Android\bin\Release" -Filter '*-Signed.apk' -Recurse -File)
            Assert ($apks.Count -gt 0) 'The Android packaged-asset consumer produced no APK.'
            Write-Output 'Android APK construction passed; no Android device execution is implied.'
        }
    }
    $shared = "$application\Shared\AssetProof.Shared.csproj"
    $source = Get-Content $shared -Raw
    foreach ($case in @(
        @{ Token = 'images/logo.png'; Value = '../logo.png'; Code = 'XUIA002' },
        @{ Token = 'images/logo.png'; Value = 'Images/logo.png'; Code = 'XUIA002' },
        @{ Token = 'images/logo.png'; Value = 'images/photo.jpg'; Code = 'XUIA004' },
        @{ Token = 'images/logo.png'; Value = ''; Code = 'XUIA001' },
        @{ Token = 'Fixtures\owned.bin'; Value = 'Fixtures\missing.bin'; Code = 'XUIA003' }
    )) {
        try {
            $source.Replace($case.Token, $case.Value) | Set-Content $shared -Encoding utf8
            $output = & dotnet build $shared -c Release --no-restore --nologo 2>&1 | Out-String
            Assert ($LASTEXITCODE -ne 0 -and $output.Contains($case.Code)) "Expected asset diagnostic $($case.Code): $output"
        } finally { Set-Content $shared $source -Encoding utf8 }
    }
    foreach ($metadata in 'LogicalName', 'ManifestResourceName') {
        try {
            $collision = "<EmbeddedResource Include=`"Fixtures\owned.bin`" $metadata=`"Xui.Asset.images/logo.png`" />"
            $source.Replace('</ItemGroup>', "$collision</ItemGroup>") | Set-Content $shared -Encoding utf8
            $output = & dotnet build $shared -c Release --no-restore --nologo 2>&1 | Out-String
            Assert ($LASTEXITCODE -ne 0 -and $output.Contains('XUIA006')) "Reserved embedded-resource collision must fail: $output"
        } finally { Set-Content $shared $source -Encoding utf8 }
    }
    try {
        $items = 1..4093 | ForEach-Object { "<XuiAsset Include=`"Fixtures\owned.bin`" AssetId=`"budget/$_`" />" }
        $source.Replace('</ItemGroup>', (($items -join "`n") + '</ItemGroup>')) | Set-Content $shared -Encoding utf8
        $output = & dotnet build $shared -c Release --no-restore --nologo 2>&1 | Out-String
        Assert ($LASTEXITCODE -ne 0 -and $output.Contains('XUIA005')) "The asset manifest count must be bounded: $output"
    } finally { Set-Content $shared $source -Encoding utf8 }
    Invoke-Checked { dotnet build $shared -c Release --no-restore --nologo }
    Write-Output "Isolated packaged-asset consumer and build diagnostics passed: $($Platform -join ', ')."
} finally {
    $env:NUGET_PACKAGES = $oldPackages
    $env:NUGET_HTTP_CACHE_PATH = $oldHttp
    if (!$keep) { Remove-Item -LiteralPath $work -Recurse -Force }
}
