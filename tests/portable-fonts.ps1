#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$AssetDirectory,
    [string]$Version = '0.1.0-preview.4',
    [string]$WorkDirectory,
    [string]$FrameworkSource = 'https://api.nuget.org/v3/index.json',
    [string]$AndroidSdkDirectory,
    [string]$JavaSdkDirectory
)
. "$PSScriptRoot\..\scripts\Release.Common.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$feed = (Resolve-Path -LiteralPath $AssetDirectory).Path
$packageNames = @('Compiler', 'Portable', 'Android')
foreach ($name in $packageNames) {
    if (!(Test-Path "$feed\Xui.Experimental.$name.$Version.nupkg")) { throw "Missing font-enabled package: Xui.Experimental.$name.$Version" }
}
$keep = !!$WorkDirectory
if (!$WorkDirectory) { $WorkDirectory = Join-Path ([IO.Path]::GetTempPath()) ("xui-font-prototype-" + [guid]::NewGuid().ToString('N')) }
$work = [IO.Path]::GetFullPath($WorkDirectory)
if ($work -eq $repo -or $work.StartsWith("$repo$([IO.Path]::DirectorySeparatorChar)", [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use an external consumer directory.'
}
if (Test-Path -LiteralPath $work) { throw 'Use a fresh consumer directory.' }
function Assert([bool]$Value, [string]$Message) { if (!$Value) { throw $Message } }
function Read-FontItems([string]$Project) {
    $output = & dotnet msbuild $Project '-t:PrepareExperimentalXuiFonts;PrepareExperimentalXuiAssets' `
        '-getItem:XuiAsset,EmbeddedResource,AndroidAsset' -warnaserror:MSB4011 --nologo 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "Font target evaluation failed: $output" }
    return ($output | ConvertFrom-Json -AsHashtable).Items
}
$oldPackages = $env:NUGET_PACKAGES
$oldHttp = $env:NUGET_HTTP_CACHE_PATH
New-Item -ItemType Directory -Path $work | Out-Null
$env:NUGET_PACKAGES = "$work\packages"
$env:NUGET_HTTP_CACHE_PATH = "$work\http"
try {
    foreach ($name in $packageNames) {
        [IO.Compression.ZipFile]::ExtractToDirectory("$feed\Xui.Experimental.$name.$Version.nupkg", "$work\expanded\$name")
        if ($name -ne 'Portable') {
            foreach ($target in "Xui.Experimental.$name.targets", 'Xui.Experimental.Fonts.targets', 'Xui.Experimental.Assets.targets') {
                Assert (Test-Path "$work\expanded\$name\build\$target") "Installed package lacks build asset: $name/$target"
            }
        }
    }
    Assert-SameFile "$work\expanded\Compiler\build\Xui.Experimental.Fonts.targets" "$work\expanded\Android\build\Xui.Experimental.Fonts.targets"
    Assert-SameFile "$work\expanded\Compiler\build\Xui.Experimental.Assets.targets" "$work\expanded\Android\build\Xui.Experimental.Assets.targets"
    $escapedFeed = [Security.SecurityElement]::Escape($feed)
    $escapedFramework = [Security.SecurityElement]::Escape($FrameworkSource)
    @"
<configuration>
  <packageSources><clear/><add key="Local" value="$escapedFeed"/><add key="Framework" value="$escapedFramework"/></packageSources>
  <packageSourceMapping><clear/><packageSource key="Local"><package pattern="Xui.Experimental.*"/></packageSource><packageSource key="Framework"><package pattern="*"/></packageSource></packageSourceMapping>
</configuration>
"@ | Set-Content "$work\NuGet.Config" -Encoding utf8
    $app = "$work\consumer with spaces"
    $fixture = "$PSScriptRoot\fixtures\portable-fonts"
    foreach ($file in Get-ChildItem -LiteralPath $fixture -Recurse -File) {
        $relative = [IO.Path]::GetRelativePath($fixture, $file.FullName)
        if ($relative -match '(^|[\\/])(bin|obj)([\\/]|$)' -or $relative -eq 'Abel.items.props') { continue }
        $destination = Join-Path $app $relative
        New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
        (Get-Content $file.FullName -Raw).Replace('__FONT_PACKAGE_VERSION__', $Version) | Set-Content $destination -Encoding utf8
    }
    New-Item -ItemType Directory "$app\Fonts" -Force | Out-Null
    Copy-Item -LiteralPath "$fixture\Abel.items.props" -Destination "$app\Fonts"
    foreach ($file in 'Abel-Regular.ttf', 'OFL.txt', 'provenance.json') {
        Copy-Item -LiteralPath "$repo\assets\fonts\abel\$file" -Destination "$app\Fonts"
    }
    foreach ($project in Get-ChildItem -LiteralPath $app -Recurse -File -Filter '*.csproj') {
        [xml]$xml = Get-Content -LiteralPath $project.FullName -Raw
        $imports = @($xml.SelectNodes('//Import'))
        Assert ($imports.Count -le 1) 'Consumer must not copy or manually import framework build targets.'
        foreach ($import in $imports) {
            Assert ($import.Project -ceq '..\Fonts\Abel.items.props') 'Only the app-owned font declaration may be imported.'
        }
    }
    Assert (@(Get-ChildItem -LiteralPath $app -Recurse -File -Filter '*.targets').Count -eq 0) 'Consumer contains copied framework targets.'
    [ordered]@{
        contract = 'Explicit shared and Android app-owned font declarations; build targets supplied automatically by NuGet packages.'
        packages = @($packageNames | ForEach-Object {
            [ordered]@{ id = "Xui.Experimental.$_"; sha256 = (Get-FileHash "$feed\Xui.Experimental.$_.$Version.nupkg").Hash }
        })
        font = (Get-FileHash "$app\Fonts\Abel-Regular.ttf").Hash
        license = (Get-FileHash "$app\Fonts\OFL.txt").Hash
    } | ConvertTo-Json | Set-Content "$work\inputs.json" -Encoding utf8
    $windows = "$app\Windows\FontProof.Windows.csproj"
    Invoke-Checked { dotnet restore $windows --configfile "$work\NuGet.Config" --nologo }
    Invoke-Checked { dotnet run --project $windows -c Release --no-restore }
    $shared = "$app\Shared\FontProof.Shared.csproj"
    $sharedItems = Read-FontItems $shared
    Assert ($sharedItems.XuiAsset.Count -eq 2 -and $sharedItems.EmbeddedResource.Count -eq 2 -and $sharedItems.AndroidAsset.Count -eq 0) `
        'Compiler package must embed exactly one font and one license.'
    $android = "$app\Android\FontProof.Android.csproj"
    $properties = @('-p:RuntimeIdentifier=android-x64')
    foreach ($name in 'AndroidSdkDirectory', 'JavaSdkDirectory') {
        $value = Get-Variable -Name $name -ValueOnly
        if ($value) { $properties += "-p:${name}=$value" }
    }
    Invoke-Checked { dotnet restore $android @properties --configfile "$work\NuGet.Config" --nologo }
    $androidItems = Read-FontItems $android
    Assert ($androidItems.AndroidAsset.Count -eq 2 -and $androidItems.XuiAsset.Count -eq 0) `
        'Android package must project the explicit host declaration exactly once.'
    Invoke-Checked { dotnet build $android -c Release --no-restore @properties --nologo }
    $apks = @(Get-ChildItem "$app\Android\bin\Release" -File -Recurse -Filter '*-Signed.apk')
    Assert ($apks.Count -eq 1) 'Expected one fresh signed font projection APK.'
    $hash = (Get-FileHash "$app\Fonts\Abel-Regular.ttf").Hash.ToLowerInvariant()
    $archive = [IO.Compression.ZipFile]::OpenRead($apks[0].FullName)
    try {
        foreach ($file in @(
            @{ Name = "assets/xui-fonts/$hash.ttf"; Source = "$app\Fonts\Abel-Regular.ttf" },
            @{ Name = "assets/xui-fonts/$hash.license.txt"; Source = "$app\Fonts\OFL.txt" }
        )) {
            $entry = $archive.GetEntry($file.Name)
            Assert ($null -ne $entry) "APK is missing the declared projected file $($file.Name)."
            $stream = $entry.Open()
            try {
                $actual = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))
                Assert ($actual -ceq (Get-FileHash $file.Source).Hash) 'APK projection changed the font or license bytes.'
            } finally { $stream.Dispose() }
        }
    } finally { $archive.Dispose() }
    $androidSource = Get-Content $android -Raw
    try {
        $reference = "<PackageReference Include=`"Xui.Experimental.Compiler`" Version=`"$Version`" PrivateAssets=`"all`" />"
        $androidSource.Replace('</ItemGroup>', "$reference</ItemGroup>") | Set-Content $android -Encoding utf8
        Invoke-Checked { dotnet restore $android @properties --configfile "$work\NuGet.Config" --nologo }
        $both = Read-FontItems $android
        Assert ($both.AndroidAsset.Count -eq 2 -and $both.XuiAsset.Count -eq 0) 'A host referencing both packages duplicated its font projection.'
        Invoke-Checked { dotnet build $android -c Debug --no-restore @properties -warnaserror:MSB4011 --nologo }
    } finally { Set-Content $android $androidSource -Encoding utf8 }
    Invoke-Checked { dotnet restore $android @properties --configfile "$work\NuGet.Config" --nologo }

    foreach ($order in @(@('Compiler', 'Android'), @('Android', 'Compiler'))) {
        $imports = @($order | ForEach-Object {
            $package = "xui.experimental.$_".ToLowerInvariant()
            $path = [Security.SecurityElement]::Escape("$work\packages\$package\$Version\build\Xui.Experimental.$_.targets")
            "<Import Project=`"$path`" />"
        }) -join ''
        $fontItems = [Security.SecurityElement]::Escape("$app\Fonts\Abel.items.props")
        $prefix = '<Project><PropertyGroup><TargetPlatformIdentifier>android</TargetPlatformIdentifier></PropertyGroup>'
        $probe = "$work\import-order.proj"
        "$prefix<Import Project=`"$fontItems`"/>$imports</Project>" | Set-Content $probe -Encoding utf8
        $items = Read-FontItems $probe
        Assert ($items.AndroidAsset.Count -eq 2 -and $items.XuiAsset.Count -eq 0) 'Package import order changed font projection.'
        "$prefix$imports</Project>" | Set-Content $probe -Encoding utf8
        $items = Read-FontItems $probe
        Assert ($items.AndroidAsset.Count -eq 0 -and $items.XuiAsset.Count -eq 0 -and $items.EmbeddedResource.Count -eq 0) `
            'Font imports must be a no-op when no XuiFont items are declared.'
    }
    foreach ($project in @($shared, $android)) {
        $original = Get-Content $project -Raw
        try {
            $original.Replace('<Import Project="..\Fonts\Abel.items.props" />', '') | Set-Content $project -Encoding utf8
            $items = Read-FontItems $project
            Assert ($items.AndroidAsset.Count -eq 0 -and $items.XuiAsset.Count -eq 0 -and $items.EmbeddedResource.Count -eq 0) `
                'A real package consumer without font items must remain unchanged.'
        } finally { Set-Content $project $original -Encoding utf8 }
    }
    $items = "$app\Fonts\Abel.items.props"
    $source = Get-Content $items -Raw
    foreach ($case in @(
        @{ From = $hash; To = ('0' * 64); Code = 'XUIF006' },
        @{ From = '4f4bc3806a1e55789c6ef75ca5fc628297b05292f74966474dc0d40324abc609'; To = ('0' * 64); Code = 'XUIF006' },
        @{ From = 'fonts/abel-regular.ttf'; To = '../abel.ttf'; Code = 'XUIF002' },
        @{ From = 'Abel-Regular.ttf'; To = 'Missing.ttf'; Code = 'XUIF003' }
    )) {
        try {
            $source.Replace($case.From, $case.To) | Set-Content $items -Encoding utf8
            $output = & dotnet build $shared -c Release --no-restore --nologo 2>&1 | Out-String
            Assert ($LASTEXITCODE -ne 0 -and $output.Contains($case.Code)) "Expected font build diagnostic $($case.Code): $output"
        } finally { Set-Content $items $source -Encoding utf8 }
    }
    Invoke-Checked { dotnet build $shared -c Release --no-restore --nologo }
    Write-Output 'PASS: NuGet-installed font targets, empty-item no-op, duplicate-import guards, shared embedded bytes and byte-identical Android APK assets. No font was registered or installed; API26 execution remains untested.'
} finally {
    $env:NUGET_PACKAGES = $oldPackages
    $env:NUGET_HTTP_CACHE_PATH = $oldHttp
    if (!$keep) { Remove-Item -LiteralPath $work -Recurse -Force }
}
