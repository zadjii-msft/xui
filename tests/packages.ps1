param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$AssetDirectory,
    [ValidateSet('x64', 'ARM64')][string]$Architecture = 'x64',
    [string]$FrameworkSource = 'https://api.nuget.org/v3/index.json'
)
. "$PSScriptRoot\..\scripts\Release.Common.ps1"
Assert-ReleaseVersion $Version
$assets = (Resolve-Path $AssetDirectory).Path
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work = Join-Path $repo ("build\package-tests-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$rid = if ($Architecture -eq 'ARM64') { 'win-arm64' } else { 'win-x64' }
$package = Join-Path $assets "Xui.$Version.nupkg"
$expanded = Join-Path $work 'nuget'
[IO.Compression.ZipFile]::ExtractToDirectory($package, $expanded)
Assert-SameFile "$repo\LICENSE" "$expanded\LICENSE"
[xml]$nuspec = Get-Content "$expanded\Xui.nuspec" -Raw
$license = $nuspec.SelectSingleNode("/*[local-name()='package']/*[local-name()='metadata']/*[local-name()='license']")
if ($null -eq $license -or $license.type -ne 'expression' -or $license.InnerText -ne 'MIT') {
    throw 'The NuGet package must declare the MIT license expression.'
}
foreach ($path in @(
    'lib\net10.0\Xui.Managed.dll', 'analyzers\dotnet\cs\Xui.Generator.dll',
    'build\native\lib\x64\xui_core.lib', 'build\native\lib\ARM64\xui_core.lib',
    'runtimes\win-x64\native\xui.dll', 'runtimes\win-arm64\native\xui.dll'
)) {
    if (!(Test-Path (Join-Path $expanded $path))) { throw "Missing NuGet asset: $path" }
}
Copy-Item "$PSScriptRoot\packaging\managed" "$work\managed" -Recurse
$project = "$work\managed\Consumer.csproj"
$source = [Security.SecurityElement]::Escape($assets)
$frameworkFeed = [Security.SecurityElement]::Escape($FrameworkSource)
@"
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources>
    <clear />
    <add key="XuiLocal" value="$source" />
    <add key="nuget.org" value="$frameworkFeed" />
  </packageSources>
  <packageSourceMapping>
    <clear />
    <packageSource key="XuiLocal"><package pattern="Xui" /></packageSource>
    <packageSource key="nuget.org"><package pattern="Microsoft.*" /></packageSource>
  </packageSourceMapping>
</configuration>
"@ | Set-Content "$work\NuGet.Config" -Encoding utf8
foreach ($configuration in 'Debug', 'Release') {
    Invoke-Checked {
        dotnet restore $project -r $rid "-p:XuiTestVersion=$Version" "-p:Configuration=$configuration" `
            --configfile "$work\NuGet.Config" --packages "$work\packages" --nologo
    }
    Invoke-Checked { dotnet build $project -c $configuration -r $rid "-p:XuiTestVersion=$Version" --no-restore --nologo }
    $bin = "$work\managed\bin\$configuration\net10.0\$rid"
    Assert-SameFile "$expanded\runtimes\$rid\native\xui.dll" "$bin\xui.dll"
    if ((Test-Path "$bin\Xui.Development.dll") -ne ($configuration -eq 'Debug')) { throw "Incorrect development-host deployment in $bin" }
    if (Test-Path "$bin\Xui.Generator.dll") { throw "Compiler leaked into runtime output: $bin" }
    Invoke-Checked { & "$bin\Consumer.exe" }
}
Invoke-Checked { dotnet publish $project -c Release -r $rid --self-contained true "-p:XuiTestVersion=$Version" --no-restore -o "$work\published" --nologo }
Assert-SameFile "$expanded\runtimes\$rid\native\xui.dll" "$work\published\xui.dll"
Invoke-Checked { & "$work\published\Consumer.exe" }

Copy-Item "$PSScriptRoot\packaging\native" "$work\native" -Recurse
$cmake = Get-XuiCMake
Invoke-Checked { & $cmake -S "$work\native" -B "$work\native-build" -G 'Visual Studio 17 2022' -A $Architecture "-DXui_DIR=$expanded\build\native" }
Invoke-Checked { & $cmake --build "$work\native-build" --config Release --parallel 2 }
New-Item -ItemType Directory "$work\static-only" | Out-Null
Copy-Item "$work\native-build\Release\static_consumer.exe" "$work\static-only"
Invoke-Checked { & "$work\static-only\static_consumer.exe" }
Invoke-Checked { & "$work\native-build\Release\abi_consumer.exe" }

$vcvars = Get-XuiVcVars $Architecture
foreach ($linkage in 'Static', 'CAbi') {
    $destination = "$work\vc-$linkage"
    Invoke-Checked {
        & $env:ComSpec /d /c "call `"$vcvars`" >nul && msbuild `"$work\native\Consumer.vcxproj`" /nologo /v:minimal /p:Configuration=Release /p:Platform=$Architecture /p:XuiNativeLinkage=$linkage /p:XuiPackageRoot=`"$expanded`" /p:XuiTestOutput=`"$destination`""
    }
    if ((Test-Path "$destination\xui.dll") -ne ($linkage -eq 'CAbi')) { throw "Incorrect native package deployment: $linkage" }
    if (Get-ChildItem $destination -Filter '*Managed*') { throw "Managed files leaked into native output: $linkage" }
    Invoke-Checked { & "$destination\Consumer.exe" }
}

& "$PSScriptRoot\cargo-packages.ps1" -Version $Version -PackageDirectory $assets -Architecture $Architecture
Write-Output "Package consumers passed. Evidence: $work"
