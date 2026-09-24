#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$Version = '0.1.0-preview.1',
    [Parameter(Mandatory)][string]$AssetDirectory,
    [Parameter(Mandatory)][string]$LocalizationDirectory,
    [string]$WorkDirectory,
    [ValidateSet('Windows', 'Android', 'Web')][string[]]$Platform = @('Windows', 'Web', 'Android'),
    [string]$FrameworkSource = 'https://api.nuget.org/v3/index.json',
    [string]$AndroidSdkDirectory,
    [string]$JavaSdkDirectory,
    [switch]$Browser
)
. "$PSScriptRoot\..\scripts\Release.Common.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$feed = (Resolve-Path -LiteralPath $AssetDirectory).Path
$localization = (Resolve-Path -LiteralPath $LocalizationDirectory).Path
$packageNames = @('Compiler', 'Portable')
if ($Platform -contains 'Web') { $packageNames += 'Web' }
$packageFiles = $packageNames | ForEach-Object { Join-Path $feed "Xui.Experimental.$_.$Version.nupkg" }
foreach ($file in $packageFiles) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing localization package input: $file" }
}
$sourceFiles = @('LocalizationCatalog.cs', 'LocalizationWorkbench.xui', 'LocalizationWorkbench.items.props',
    'WorkbenchStrings.resx', 'WorkbenchStrings.de.resx', 'WorkbenchStrings.ar.resx')
foreach ($file in $sourceFiles) {
    if (!(Test-Path -LiteralPath (Join-Path $localization $file) -PathType Leaf)) { throw "Missing localization input: $file" }
}
$keep = !!$WorkDirectory
if (!$WorkDirectory) { $WorkDirectory = Join-Path ([IO.Path]::GetTempPath()) ("xui-localization-" + [guid]::NewGuid().ToString('N')) }
$work = [IO.Path]::GetFullPath($WorkDirectory)
if ($work -eq $repo -or $work.StartsWith("$repo$([IO.Path]::DirectorySeparatorChar)", [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Localization consumers must be generated outside the repository.'
}
if (Test-Path -LiteralPath $work) { throw 'Use a fresh WorkDirectory for isolated restoration and language-bundle checks.' }
if (!$Platform -or ($Browser -and $Platform -notcontains 'Web')) { throw 'Select platforms; Browser requires Web.' }
function Assert([bool]$Value, [string]$Message) { if (!$Value) { throw $Message } }
$oldPackages = $env:NUGET_PACKAGES
$oldHttp = $env:NUGET_HTTP_CACHE_PATH
New-Item -ItemType Directory -Path $work | Out-Null
$env:NUGET_PACKAGES = "$work\packages"
$env:NUGET_HTTP_CACHE_PATH = "$work\http"
try {
    $inputs = @($packageFiles) + @($sourceFiles | ForEach-Object { Join-Path $localization $_ })
    @($inputs | ForEach-Object {
        [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
    }) | ConvertTo-Json | Set-Content "$work\verified-inputs.json" -Encoding utf8
    $escapedFeed = [Security.SecurityElement]::Escape($feed)
    $escapedFramework = [Security.SecurityElement]::Escape($FrameworkSource)
    @"
<configuration>
  <packageSources><clear/><add key="Local" value="$escapedFeed"/><add key="Framework" value="$escapedFramework"/></packageSources>
  <packageSourceMapping><clear/><packageSource key="Local"><package pattern="Xui.Experimental.*"/></packageSource><packageSource key="Framework"><package pattern="*"/></packageSource></packageSourceMapping>
</configuration>
"@ | Set-Content "$work\NuGet.Config" -Encoding utf8
    $fixture = "$PSScriptRoot\fixtures\portable-localization"
    $application = "$work\application with spaces"
    foreach ($file in Get-ChildItem -LiteralPath $fixture -File -Recurse) {
        $relative = [IO.Path]::GetRelativePath($fixture, $file.FullName)
        if ($relative -match '(^|[\\/])(bin|obj)([\\/]|$)') { continue }
        $destination = Join-Path $application $relative
        New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
        (Get-Content -LiteralPath $file.FullName -Raw).Replace('__LOCALIZATION_PACKAGE_VERSION__', $Version) |
            Set-Content -LiteralPath $destination -Encoding utf8
    }
    New-Item -ItemType Directory "$application\Localization" -Force | Out-Null
    foreach ($file in $sourceFiles) {
        Copy-Item -LiteralPath (Join-Path $localization $file) -Destination "$application\Localization\$file"
        Assert-SameFile (Join-Path $localization $file) "$application\Localization\$file"
    }
    foreach ($platformName in $Platform | Select-Object -Unique) {
        $project = "$application\$platformName\LocalizationProof.$platformName.csproj"
        $arguments = @()
        if ($platformName -eq 'Android') {
            $arguments += '-p:RuntimeIdentifier=android-x64'
            foreach ($name in 'AndroidSdkDirectory','JavaSdkDirectory') {
                $path = Get-Variable -Name $name -ValueOnly
                if ($path) { $arguments += "-p:${name}=$path" }
            }
        }
        if ($platformName -eq 'Windows') { $arguments += '-p:PublishTrimmed=true' }
        Invoke-Checked { dotnet restore $project --configfile "$work\NuGet.Config" @arguments --nologo }
        Invoke-Checked { dotnet build $project -c Release --no-restore @arguments --nologo }
        $published = "$work\published\$platformName"
        if ($platformName -eq 'Windows') {
            Invoke-Checked { dotnet publish $project -c Release --no-restore --self-contained true @arguments -o $published --nologo }
            $executable = "$published\LocalizationProof.Windows.exe"
            Invoke-Checked { & $executable }
        } elseif ($platformName -eq 'Web') {
            Invoke-Checked { dotnet publish $project -c Release --no-restore -o $published --nologo }
            if ($Browser) {
                Invoke-Checked { node "$PSScriptRoot\portable-localization.browser.mjs" "$published\wwwroot" "$repo\bindings\dotnet\Experimental\WebDemo.Tests\package.json" "$work\browser-result.json" }
            }
        } else {
            $apks = @(Get-ChildItem "$application\Android\bin\Release" -Filter '*-Signed.apk' -Recurse -File)
            Assert ($apks.Count -gt 0) 'The Android localization consumer produced no APK.'
            Write-Output 'Android localization APK construction passed; no Android device execution is implied.'
        }
    }
    if ($Platform -contains 'Windows') {
        $shared = "$application\Shared\LocalizationProof.Shared.csproj"
        $original = Get-Content $shared -Raw
        $project = "$application\Windows\LocalizationProof.Windows.csproj"
        foreach ($culture in 'en', 'de', 'ar') {
            try {
                $filename = if ($culture -eq 'en') { 'WorkbenchStrings.resx' } else { "WorkbenchStrings.$culture.resx" }
                $source = [Security.SecurityElement]::Escape("$application\Localization\$filename")
                $remove = "<ItemGroup><EmbeddedResource Remove=`"$source`" /></ItemGroup>"
                $original.Replace('</Project>', "$remove</Project>") | Set-Content $shared -Encoding utf8
                $negative = "$work\missing-$culture"
                Invoke-Checked { dotnet publish $project -c Release --no-restore --self-contained true -p:PublishTrimmed=true -o $negative --nologo }
                Invoke-Checked { & "$negative\LocalizationProof.Windows.exe" --expect-missing-bundle $culture }
            } finally { Set-Content $shared $original -Encoding utf8 }
        }
    }
    Write-Output "Isolated localized package consumer and required embedded language-bundle checks passed: $($Platform -join ', ')."
} finally {
    $env:NUGET_PACKAGES = $oldPackages
    $env:NUGET_HTTP_CACHE_PATH = $oldHttp
    if (!$keep) { Remove-Item -LiteralPath $work -Recurse -Force }
}
