param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$fixture = [System.IO.Path]::GetFullPath((Join-Path $repo ("build\generator-tests\" + [guid]::NewGuid().ToString('N'))))
[void][System.IO.Directory]::CreateDirectory($fixture)
$project = Join-Path $fixture 'Fixture.csproj'
$assembly = Join-Path $fixture 'bin\Debug\net10.0\Fixture.dll'
$targets = [System.Security.SecurityElement]::Escape((Join-Path $repo 'bindings\dotnet\Xui.Declarative.targets'))
$encoding = [System.Text.UTF8Encoding]::new($false)
$passed = $false
$checks = 0

function Write-Fixture([string]$name, [string]$text) {
    [System.IO.File]::WriteAllText((Join-Path $fixture $name), $text, $encoding)
}
function Build-Fixture {
    & dotnet build $project --nologo -v:q
    if ($LASTEXITCODE -ne 0) { throw "Fixture build failed: $fixture" }
}
function Has-Type([string]$name) {
    $reader = [System.Reflection.PortableExecutable.PEReader]::new([System.IO.File]::OpenRead($assembly))
    try {
        $metadata = [System.Reflection.Metadata.PEReaderExtensions]::GetMetadataReader($reader)
        foreach ($handle in $metadata.TypeDefinitions) {
            $type = $metadata.GetTypeDefinition($handle)
            $fullName = $metadata.GetString($type.Namespace) + '.' + $metadata.GetString($type.Name)
            if ($fullName -eq $name) { return $true }
        }
        return $false
    }
    finally { $reader.Dispose() }
}
function Assert([bool]$condition, [string]$message) {
    if (!$condition) { throw $message }
    $script:checks++
}

try {
    Write-Fixture 'Fixture.csproj' @"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>
  </PropertyGroup>
  <Import Project="$targets" />
</Project>
"@
    Write-Fixture 'Program.cs' 'internal static class Program { private static void Main() {} }'
    Write-Fixture 'Baseline.xui' 'namespace Demo; component Baseline { view { VStack() { Text("Baseline"); } } }'
    Write-Fixture 'Composition.xui' ([System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Fixtures\Composition.xui')))
    Write-Fixture 'Styling.xui' ([System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Fixtures\Styling.xui')))
    Build-Fixture
    Assert (Has-Type 'Demo.Baseline') 'The initial state-free component was not compiled.'
    Assert (Has-Type 'Demo.Composition') 'Extended composition did not compile against the real bindings.'
    Assert (Has-Type 'Demo.Styling') 'Named styles and resources did not compile against the real bindings.'
    Write-Fixture 'ConstructorChecks.cs' @'
internal static class ConstructorChecks {
    internal static void Compile(Xui.Window window, Xui.Element body) {
        _ = new Demo.Baseline(window);
        var component = new Demo.Composition(window, body, "Browser", 0, attach: false);
        Xui.Stack root = component.Root;
        Xui.Grid grid = component.Layout;
        Xui.DataGrid details = component.Details;
        Xui.Element content = component.Embedded;
        window.SetContent(root);
    }
}
'@
    Assert (!(Has-Type 'Demo.Extra')) 'Extra unexpectedly exists before adding its source.'

    Write-Fixture 'Extra.xui' 'namespace Demo; component Extra { view { VStack() { Text("Extra"); } } }'
    Build-Fixture
    Assert (Has-Type 'Demo.Extra') 'Adding .xui did not add its compiled type.'

    [System.IO.File]::Move((Join-Path $fixture 'Extra.xui'), (Join-Path $fixture 'Renamed.xui'))
    Build-Fixture
    Assert (Has-Type 'Demo.Extra') 'Renaming .xui lost its compiled type.'

    [System.IO.File]::Delete((Join-Path $fixture 'Renamed.xui'))
    Build-Fixture
    Assert (!(Has-Type 'Demo.Extra')) 'Deleting .xui left its stale type in the compiled assembly.'
    Assert (Has-Type 'Demo.Baseline') 'Deleting a sibling removed the unchanged component.'

    $before = (Get-Item -LiteralPath $assembly).LastWriteTimeUtc
    $manifest = Join-Path $fixture 'obj\Debug\net10.0\Fixture.xui.inputs'
    $manifestBefore = (Get-Item -LiteralPath $manifest).LastWriteTimeUtc
    Build-Fixture
    Assert ((Get-Item -LiteralPath $assembly).LastWriteTimeUtc -eq $before) 'A no-op build rewrote the assembly.'
    Assert ((Get-Item -LiteralPath $manifest).LastWriteTimeUtc -eq $manifestBefore) 'A no-op build rewrote the input manifest.'

    [System.IO.File]::Delete((Join-Path $fixture 'Baseline.xui'))
    [System.IO.File]::Delete((Join-Path $fixture 'Composition.xui'))
    [System.IO.File]::Delete((Join-Path $fixture 'Styling.xui'))
    [System.IO.File]::Delete((Join-Path $fixture 'ConstructorChecks.cs'))
    Build-Fixture
    Assert (!(Has-Type 'Demo.Baseline')) 'Deleting the last .xui left its compiled type behind.'
    $passed = $true
    Write-Output "XUI MSBuild assertions: $checks passed."
}
finally {
    if ($passed) { [System.IO.Directory]::Delete($fixture, $true) }
    else { Write-Warning "Failed build fixture retained at $fixture" }
}
