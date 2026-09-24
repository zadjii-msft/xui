#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$Version = '0.1.0-preview.1',
    [Parameter(Mandatory)][string]$AssetDirectory,
    [string]$UpgradeFromVersion,
    [string]$UpgradeAssetDirectory,
    [ValidateSet('Windows', 'Android', 'Web')][string[]]$Platform = @('Windows', 'Android', 'Web'),
    [ValidateSet('win-x64', 'win-arm64')][string]$WindowsRuntimeIdentifier = 'win-x64',
    [string]$WorkDirectory,
    [string]$FrameworkSource = 'https://api.nuget.org/v3/index.json',
    [string]$AndroidSdkDirectory,
    [string]$JavaSdkDirectory,
    [switch]$Browser
)
. "$PSScriptRoot\..\scripts\Release.Common.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$assets = (Resolve-Path -LiteralPath $AssetDirectory).Path
if (!!$UpgradeFromVersion -ne !!$UpgradeAssetDirectory) {
    throw 'Supply both -UpgradeFromVersion and -UpgradeAssetDirectory, or neither.'
}
foreach ($candidate in @($Version, $UpgradeFromVersion) | Where-Object { $_ }) {
    if ($candidate -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-preview\.(0|[1-9][0-9]*)$') {
        throw 'Package versions must be Major.minor.patch-preview.number without leading zeroes.'
    }
}
if ($UpgradeFromVersion -and
    [version]($UpgradeFromVersion -replace '-preview\.', '.') -ge [version]($Version -replace '-preview\.', '.')) {
    throw 'An upgrade must select a newer package version.'
}
$previousAssets = if ($UpgradeAssetDirectory) { (Resolve-Path -LiteralPath $UpgradeAssetDirectory).Path } else { $null }
$templateVersion = if ($UpgradeFromVersion) { $UpgradeFromVersion } else { $Version }
$templateAssets = if ($previousAssets) { $previousAssets } else { $assets }
$keep = !!$WorkDirectory
if (!$WorkDirectory) { $WorkDirectory = Join-Path ([IO.Path]::GetTempPath()) ("xui-portable-consumer-" + [guid]::NewGuid().ToString('N')) }
$work = [IO.Path]::GetFullPath($WorkDirectory)
if ($work -eq $repo -or $work.StartsWith("$repo$([IO.Path]::DirectorySeparatorChar)", [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package consumers must be generated outside the repository checkout.'
}
if (Test-Path -LiteralPath $work) { throw "Use a fresh WorkDirectory with an empty template hive and package cache: $work" }
if (!$Platform -or ($Browser -and $Platform -notcontains 'Web')) { throw 'Select at least one platform; -Browser also requires Web.' }
function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}
function Assert-ReleaseFiles([string]$Directory) {
    $bad = @(Get-ChildItem -LiteralPath $Directory -File -Recurse | Where-Object {
        $_.Name -match 'Xui\.(Generator|Development)|Microsoft\.CodeAnalysis|BrowserTestDriver|browser-test|PortableConsumer\.Tests'
    })
    Assert ($bad.Count -eq 0) "Compiler, development tools, or test bridges leaked into $Directory : $(@($bad | Select-Object -ExpandProperty Name) -join ', ')"
}
function Get-HostProperties([string]$HostName) {
    if ($HostName -eq 'Windows') { "-p:RuntimeIdentifier=$WindowsRuntimeIdentifier" }
    if ($HostName -eq 'Android') {
        '-p:RuntimeIdentifier=android-x64'
        foreach ($name in 'AndroidSdkDirectory', 'JavaSdkDirectory') {
            $path = Get-Variable -Name $name -ValueOnly
            if ($path) { "-p:${name}=$path" }
        }
    }
}
function Assert-ResolvedVersion([string]$Project, [string]$ExpectedVersion) {
    $lock = Get-Content -LiteralPath (Join-Path (Split-Path $Project) 'obj\project.assets.json') -Raw |
        ConvertFrom-Json -AsHashtable
    $packages = @($lock.libraries.Keys | Where-Object { $_.StartsWith('Xui.Experimental.', [StringComparison]::OrdinalIgnoreCase) })
    Assert ($packages.Count -gt 0) "No portable packages were restored for $Project."
    foreach ($package in $packages) {
        Assert ($package.EndsWith("/$ExpectedVersion", [StringComparison]::Ordinal)) "Mixed preview cohort in $Project : $package"
    }
}
function Assert-RestoredCohort([string]$Application, [string]$Project, [string]$ExpectedVersion) {
    Assert-ResolvedVersion (Join-Path $Application 'Shared\PortableConsumer.Shared.csproj') $ExpectedVersion
    Assert-ResolvedVersion $Project $ExpectedVersion
}
$oldPackages = $env:NUGET_PACKAGES
$oldHttpCache = $env:NUGET_HTTP_CACHE_PATH
New-Item -ItemType Directory -Path $work | Out-Null
$env:NUGET_PACKAGES = "$work\packages"
$env:NUGET_HTTP_CACHE_PATH = "$work\http-cache"
try {
    $names = @('Compiler', 'Portable', 'Android', 'Web', 'Templates')
    if ($Platform -contains 'Windows') { $names += 'Windows' }
    foreach ($name in $names) {
        $package = "$assets\Xui.Experimental.$name.$Version.nupkg"
        Assert (Test-Path -LiteralPath $package) "Missing local package: $package"
        $expanded = "$work\expanded\$name"
        [IO.Compression.ZipFile]::ExtractToDirectory($package, $expanded)
        [xml]$nuspec = Get-Content -LiteralPath "$expanded\Xui.Experimental.$name.nuspec" -Raw
        $metadata = $nuspec.SelectSingleNode("/*[local-name()='package']/*[local-name()='metadata']")
        Assert ($metadata.id -ceq "Xui.Experimental.$name" -and $metadata.version -ceq $Version) "Unexpected package identity: $name"
        Assert ($metadata.license.InnerText -ceq 'MIT') "Missing MIT license: $name"
        Assert-SameFile "$repo\LICENSE" "$expanded\LICENSE"
        Assert ($metadata.readme -ceq 'README.md') "Package readme metadata missing: $name"
        Assert-SameFile "$repo\packaging\experimental\README.md" "$expanded\README.md"
        Assert (!(Get-ChildItem -LiteralPath $expanded -Filter '*.cs' -Recurse -File | Where-Object { $_.FullName -notlike '*\templates\*' })) "Runtime source leaked into package: $name"
        if ($previousAssets) {
            Assert (Test-Path -LiteralPath "$previousAssets\Xui.Experimental.$name.$UpgradeFromVersion.nupkg") "Missing previous preview package: $name"
        }
    }
    Assert (!(Test-Path "$work\expanded\Compiler\lib")) 'Compiler must only be a build-time analyzer.'
    Assert (!(Test-Path "$work\expanded\Templates\lib")) 'Template packer assembly leaked.'
    Assert (@(Get-ChildItem "$work\expanded\Templates\templates" -Recurse -Force -Filter template.json).Count -eq 1) 'Template configuration is duplicated.'
    foreach ($hostName in 'Shared', 'Tests', 'Windows', 'Android', 'Web') {
        Assert (Test-Path "$work\expanded\Templates\templates\xui-portable\$hostName\PortableApp.$hostName.csproj") "Incorrect template payload path: $hostName"
    }
    Assert (Test-Path "$work\expanded\Compiler\build\Xui.Experimental.Compiler.targets") 'Compiler build assets missing.'
    Assert (Test-Path "$work\expanded\Web\staticwebassets\xui-dom.js") 'Packaged DOM module missing.'
    Assert (Test-Path "$work\expanded\Web\staticwebassets\xui-dom.css") 'Packaged DOM styles missing.'

    $source = [Security.SecurityElement]::Escape($assets)
    $framework = [Security.SecurityElement]::Escape($FrameworkSource)
    $previousSource = if ($previousAssets) {
        '<add key="XuiPrevious" value="' + [Security.SecurityElement]::Escape($previousAssets) + '" />'
    } else { '' }
    $previousMapping = if ($previousAssets) {
        '<packageSource key="XuiPrevious"><package pattern="Xui.Experimental.*" /></packageSource>'
    } else { '' }
    @"
<configuration>
  <packageSources><clear /><add key="XuiLocal" value="$source" />$previousSource<add key="Framework" value="$framework" /></packageSources>
  <packageSourceMapping>
    <clear />
    <packageSource key="XuiLocal"><package pattern="Xui.Experimental.*" /></packageSource>
    $previousMapping
    <packageSource key="Framework"><package pattern="*" /></packageSource>
  </packageSourceMapping>
</configuration>
"@ | Set-Content -LiteralPath "$work\NuGet.Config" -Encoding utf8
    $hive = "$work\hive"
    Invoke-Checked { dotnet new install "$templateAssets\Xui.Experimental.Templates.$templateVersion.nupkg" --debug:custom-hive $hive }
    $app = "$work\applications with spaces\PortableConsumer"
    Invoke-Checked { dotnet new xui-portable -n PortableConsumer -o $app --no-update-check --debug:custom-hive $hive }
    foreach ($project in Get-ChildItem -LiteralPath $app -Filter '*.csproj' -Recurse) {
        [xml]$xml = Get-Content -LiteralPath $project.FullName -Raw
        Assert ($null -eq $xml.SelectSingleNode('//Import')) 'Generated consumer has a checkout-relative MSBuild import.'
        foreach ($reference in $xml.SelectNodes('//ProjectReference')) {
            $target = [IO.Path]::GetFullPath($reference.Include, $project.DirectoryName)
            Assert ($target.StartsWith("$app$([IO.Path]::DirectorySeparatorChar)", [StringComparison]::OrdinalIgnoreCase)) 'Generated project references outside the application.'
        }
        foreach ($reference in $xml.SelectNodes('//PackageReference[starts-with(@Include, "Xui.")]')) {
            Assert ($reference.Version -ceq $templateVersion) 'Generated project does not use the selected template preview version.'
        }
    }
    Assert (!(Get-ChildItem -LiteralPath $app -Directory -Recurse | Where-Object Name -In 'bin', 'obj')) 'Template unexpectedly restored during generation.'
    Assert (!(Get-ChildItem -LiteralPath $app -File -Recurse | Select-String -SimpleMatch '__XUI_PORTABLE_VERSION__', 'bindings\dotnet', 'Xui.Portable.targets')) 'Source tokens or checkout paths leaked.'
    $testProject = "$app\Tests\PortableConsumer.Tests.csproj"
    if ($UpgradeFromVersion) {
        Invoke-Checked { dotnet restore $testProject --configfile "$work\NuGet.Config" --nologo }
        Assert-RestoredCohort $app $testProject $UpgradeFromVersion
        Invoke-Checked { dotnet run --project $testProject -c Release --no-restore }
        foreach ($hostName in @($Platform | Select-Object -Unique)) {
            $project = "$app\$hostName\PortableConsumer.$hostName.csproj"
            $properties = @(Get-HostProperties $hostName)
            Invoke-Checked { dotnet restore $project -p:Configuration=Release @properties --configfile "$work\NuGet.Config" --nologo }
            Assert-RestoredCohort $app $project $UpgradeFromVersion
            Invoke-Checked { dotnet build $project -c Release @properties --no-restore --nologo }
        }
        $sources = @{}
        foreach ($file in Get-ChildItem -LiteralPath $app -File -Recurse | Where-Object {
            $_.Extension -in '.cs', '.xui' -and $_.FullName -notmatch '[\\/](bin|obj)[\\/]'
        }) { $sources[$file.FullName] = (Get-FileHash -LiteralPath $file.FullName).Hash }
        foreach ($project in Get-ChildItem -LiteralPath $app -Filter '*.csproj' -Recurse | Where-Object {
            $_.FullName -notmatch '[\\/](bin|obj)[\\/]'
        }) {
            [xml]$xml = Get-Content -LiteralPath $project.FullName -Raw
            foreach ($reference in $xml.SelectNodes('//PackageReference[starts-with(@Include, "Xui.Experimental.")]')) {
                $reference.SetAttribute('Version', $Version)
            }
            $xml.Save($project.FullName)
        }
        foreach ($file in $sources.Keys) {
            Assert ((Get-FileHash -LiteralPath $file).Hash -ceq $sources[$file]) "The upgrade changed authored application source: $file"
        }
        Write-Output "Upgrading the same authored application from $UpgradeFromVersion to $Version."
    }
    Invoke-Checked { dotnet restore $testProject --configfile "$work\NuGet.Config" --nologo }
    Assert-RestoredCohort $app $testProject $Version
    Invoke-Checked { dotnet run --project $testProject -c Release --no-restore }
    foreach ($hostName in @($Platform | Select-Object -Unique)) {
        $project = "$app\$hostName\PortableConsumer.$hostName.csproj"
        $properties = @(Get-HostProperties $hostName)
        foreach ($configuration in 'Debug', 'Release') {
            Invoke-Checked { dotnet restore $project "-p:Configuration=$configuration" @properties --configfile "$work\NuGet.Config" --nologo }
            Assert-RestoredCohort $app $project $Version
            Invoke-Checked { dotnet build $project -c $configuration @properties --no-restore --nologo }
        }
        $published = "$work\published\$hostName"
        if ($hostName -eq 'Android') {
            $apks = @(Get-ChildItem "$app\Android\bin\Release" -Recurse -File -Filter '*-Signed.apk')
            Assert ($apks.Count -ge 1) 'Release Android build did not produce an installable development-signed APK.'
            New-Item -ItemType Directory -Path $published -Force | Out-Null
            Copy-Item -LiteralPath $apks[0].FullName -Destination $published
            $apk = [IO.Compression.ZipFile]::OpenRead($apks[0].FullName)
            try {
                Assert (@($apk.Entries | Where-Object FullName -Match 'Xui\.(Generator|Development)|BrowserTestDriver').Count -eq 0) 'Build tools leaked into the APK.'
            } finally { $apk.Dispose() }
        } else {
            Invoke-Checked { dotnet publish $project -c Release @properties --no-restore -o $published --nologo }
        }
        Assert-ReleaseFiles "$app\$hostName\bin\Release"
        Assert-ReleaseFiles $published
        if ($hostName -eq 'Windows') {
            Assert-SameFile "$work\expanded\Windows\runtimes\$WindowsRuntimeIdentifier\native\xui.dll" "$published\xui.dll"
            $unsupportedRid = if ($WindowsRuntimeIdentifier -eq 'win-x64') { 'win-arm64' } else { 'win-x64' }
            $failure = & dotnet msbuild $project -t:ValidateExperimentalWindowsRuntime "-p:RuntimeIdentifier=$unsupportedRid" 2>&1 | Out-String
            Assert ($LASTEXITCODE -ne 0 -and $failure.Contains("does not contain $unsupportedRid")) 'Missing Windows architecture must fail explicitly.'
        }
        if ($hostName -eq 'Web') {
            Assert-SameFile "$work\expanded\Web\staticwebassets\xui-dom.js" "$published\wwwroot\_content\Xui.Web\xui-dom.js"
            Assert-SameFile "$work\expanded\Web\staticwebassets\xui-dom.css" "$published\wwwroot\_content\Xui.Web\xui-dom.css"
            $html = Get-Content -LiteralPath "$published\wwwroot\index.html" -Raw
            foreach ($url in '_content/Xui.Web/xui-dom.css', '_framework/blazor.webassembly.js') {
                Assert ($html.Contains($url)) "Published entry point does not resolve $url."
            }
            if ($Browser) {
                Invoke-Checked { node "$PSScriptRoot\portable-packages.browser.mjs" "$published\wwwroot" "$repo\bindings\dotnet\Experimental\WebDemo.Tests\package.json" }
            }
        }
    }
    Invoke-Checked { dotnet new uninstall Xui.Experimental.Templates --debug:custom-hive $hive }
    Write-Output "Isolated package install, shared scenarios, Debug/Release hosts, and Release payload checks passed: $($Platform -join ', ')."
} finally {
    $env:NUGET_PACKAGES = $oldPackages
    $env:NUGET_HTTP_CACHE_PATH = $oldHttpCache
    if (!$keep) { Remove-Item -LiteralPath $work -Recurse -Force }
}
