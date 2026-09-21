param(
    [string]$Version = '1.2.3',
    [string]$AssetDirectory,
    [ValidateSet('x64', 'ARM64')][string]$Architecture = 'x64',
    [string]$FrameworkSource = 'https://api.nuget.org/v3/index.json'
)
. "$PSScriptRoot\..\scripts\Release.Common.ps1"
Assert-ReleaseVersion $Version
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work = Join-Path ([IO.Path]::GetTempPath()) ("xui-template-tests-" + [guid]::NewGuid().ToString('N'))
$buildConsumers = !!$AssetDirectory
$rid = if ($Architecture -eq 'ARM64') { 'win-arm64' } else { 'win-x64' }
$hive = Join-Path $work 'hive'
function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}
New-Item -ItemType Directory -Path $work | Out-Null
try {
    if ($buildConsumers) {
        $assets = (Resolve-Path $AssetDirectory).Path
    } else {
        $assets = Join-Path $work 'assets'
        & "$repo\scripts\Pack-Templates.ps1" -Version $Version -OutputDirectory $assets -WorkDirectory "$work\pack"
    }
    $package = Join-Path $assets "Xui.Templates.$Version.nupkg"
    $expanded = Join-Path $work 'expanded'
    [IO.Compression.ZipFile]::ExtractToDirectory($package, $expanded)
    Assert-SameFile "$repo\LICENSE" "$expanded\LICENSE"
    [xml]$nuspec = Get-Content "$expanded\Xui.Templates.nuspec" -Raw
    $metadata = $nuspec.SelectSingleNode("/*[local-name()='package']/*[local-name()='metadata']")
    Assert ($metadata.id -ceq 'Xui.Templates' -and $metadata.version -ceq $Version) 'Wrong template package identity or version.'
    Assert ($metadata.license.type -eq 'expression' -and $metadata.license.InnerText -eq 'MIT') 'Missing template MIT license.'
    Assert ($metadata.packageTypes.packageType.name -ceq 'Template') 'Missing NuGet Template package type.'
    Assert ($null -eq $metadata.SelectSingleNode("*[local-name()='dependencies']")) 'Template packages must not have runtime dependencies.'
    $files = @(Get-ChildItem "$expanded\templates" -Recurse -File -Force |
        ForEach-Object { [IO.Path]::GetRelativePath("$expanded\templates", $_.FullName) })
    Assert (!(Compare-Object @('xui\.template.config\template.json', 'xui\XuiApp.csproj', 'xui\Program.cs', 'xui\Counter.xui') $files)) 'Unexpected template payload.'
    Assert (!(Test-Path "$expanded\lib")) 'Template packer leaked its build assembly.'
    Assert ((Get-Content "$repo\templates\xui\XuiApp.csproj" -Raw).Contains('__XUI_PACKAGE_VERSION__')) 'Packing edited the source version token.'

    Invoke-Checked { dotnet new install $package --debug:custom-hive $hive }
    Invoke-Checked { dotnet new xui --help --debug:custom-hive $hive }
    $generated = @()
    foreach ($name in 'MyApp', 'Contoso.My-App') {
        $destination = Join-Path $work "applications with spaces\$name"
        Invoke-Checked { dotnet new xui -n $name -o $destination --no-update-check --debug:custom-hive $hive }
        $project = Join-Path $destination "$name.csproj"
        [xml]$xml = Get-Content $project -Raw
        Assert ($xml.Project.ItemGroup.PackageReference.Include -ceq 'Xui') 'Generated project must reference Xui.'
        Assert ($xml.Project.ItemGroup.PackageReference.Version -ceq $Version) 'Generated project does not use the template release version.'
        Assert ($xml.Project.PropertyGroup.OutputType -ceq 'WinExe' -and $xml.Project.PropertyGroup.TargetFramework -ceq 'net10.0') 'Wrong application output type or framework.'
        Assert ($null -eq $xml.SelectSingleNode('//ProjectReference | //Import')) 'Generated project depends on a checkout.'
        $namespace = $name.Replace('-', '_')
        Assert ((Get-Content "$destination\Counter.xui" -Raw).Contains("namespace $namespace;")) 'Incorrect .xui namespace substitution.'
        $program = Get-Content "$destination\Program.cs" -Raw
        Assert ($program.Contains("using $namespace;")) 'Incorrect C# namespace substitution.'
        Assert ($program.Contains('#if XUI_HOT_RELOAD') -and $program.Contains('ReloadHost.Run') -and $program.Contains('#else')) 'Template engine removed the hot-reload conditional.'
        Assert (!$program.Contains(':cnd:')) 'Template processing directives leaked into Program.cs.'
        $files = @(Get-ChildItem $destination -Recurse -File -Force | ForEach-Object { [IO.Path]::GetRelativePath($destination, $_.FullName) })
        Assert (!(Compare-Object @("$name.csproj", 'Program.cs', 'Counter.xui') $files)) 'Unexpected generated files or implicit restore.'
        $generated += $project
    }

    # No -n or -o: use the current directory without adding another directory.
    $inPlace = Join-Path $work 'InPlaceApp'
    New-Item -ItemType Directory $inPlace | Out-Null
    Push-Location $inPlace
    try {
        Invoke-Checked { dotnet new xui --no-update-check --debug:custom-hive $hive }
        Assert (Test-Path "$inPlace\InPlaceApp.csproj") 'Bare dotnet new xui did not use the current directory.'
        Invoke-Checked { dotnet new xui -n NamedApp --no-update-check --debug:custom-hive $hive }
        Assert (Test-Path "$inPlace\NamedApp\NamedApp.csproj") 'Named generation did not create a project directory.'
    } finally { Pop-Location }

    if ($buildConsumers) {
        $source = [Security.SecurityElement]::Escape($assets)
        $frameworkFeed = [Security.SecurityElement]::Escape($FrameworkSource)
        @"
<configuration>
  <packageSources>
    <clear />
    <add key="XuiLocal" value="$source" />
    <add key="Framework" value="$frameworkFeed" />
  </packageSources>
  <packageSourceMapping>
    <clear />
    <packageSource key="XuiLocal"><package pattern="Xui" /></packageSource>
    <packageSource key="Framework"><package pattern="Microsoft.*" /></packageSource>
  </packageSourceMapping>
</configuration>
"@ | Set-Content "$work\NuGet.Config" -Encoding utf8
        $runtime = Join-Path $work 'runtime'
        [IO.Compression.ZipFile]::ExtractToDirectory("$assets\Xui.$Version.nupkg", $runtime)
        foreach ($project in $generated) {
            $directory = Split-Path $project
            $hostRid = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') { 'win-arm64' } else { 'win-x64' }
            $defaultRid = Invoke-Checked { dotnet msbuild $project -getProperty:RuntimeIdentifier }
            Assert ($defaultRid -ceq $hostRid) 'Generated project did not select the host architecture.'
            foreach ($configuration in 'Debug', 'Release') {
                Invoke-Checked {
                    dotnet restore $project -r $rid "-p:Configuration=$configuration" `
                        --configfile "$work\NuGet.Config" --packages "$work\packages" --nologo
                }
                Invoke-Checked { dotnet build $project -c $configuration -r $rid --no-restore --nologo }
                $bin = "$directory\bin\$configuration\net10.0\$rid"
                Assert-SameFile "$runtime\runtimes\$rid\native\xui.dll" "$bin\xui.dll"
                Assert ((Test-Path "$bin\Xui.Development.dll") -eq ($configuration -eq 'Debug')) 'Incorrect template hot-reload deployment.'
                Assert (!(Test-Path "$bin\Xui.Generator.dll")) 'Template compiler leaked into runtime output.'
            }
            Invoke-Checked { dotnet publish $project -c Release -r $rid --self-contained false --no-restore -o "$directory\published" --nologo }
            Assert-SameFile "$runtime\runtimes\$rid\native\xui.dll" "$directory\published\xui.dll"
            Assert (!(Test-Path "$directory\published\Xui.Development.dll")) 'Template publish contains the development host.'
            Invoke-Checked { dotnet build $project -c Debug -r $rid -p:XuiHotReload=false --no-restore --nologo }
            Assert (!(Test-Path "$directory\bin\Debug\net10.0\$rid\Xui.Development.dll")) 'Hot-reload opt-out retained the development host.'
        }
    }
    Invoke-Checked { dotnet new uninstall Xui.Templates --debug:custom-hive $hive }
    Write-Output "Template package, installation, generation, naming, and removal passed. Consumer builds: $buildConsumers."
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force
}
