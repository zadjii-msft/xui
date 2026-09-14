param(
    [string]$NativeDirectory = "build\xui-language\Release",
    [string]$RuntimeIdentifier = "win-arm64",
    [int]$TimeoutSeconds = 120
)
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$root = Split-Path $PSScriptRoot
$native = (Resolve-Path (Join-Path $root $NativeDirectory)).Path
$probe = Join-Path $native "xui_language_probe.exe"
$library = Join-Path $native "xui.dll"
$targets = Join-Path $root "bindings\dotnet\Xui.Declarative.targets"
foreach ($file in @($probe, $library, $targets)) {
    if (!(Test-Path $file)) { throw "The required file does not exist: $file" }
}
$run = Join-Path $root ("build\xui-language-check\" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$project = Join-Path $run "WatchFixture.csproj"
$inputFile = Join-Path $run "Counter.xui"
$stdout = Join-Path $run "watch.stdout.log"
$stderr = Join-Path $run "watch.stderr.log"
$results = [System.Collections.Generic.List[object]]::new()
$watch = $null
$application = 0
$assertions = 0

function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    $script:assertions++
}
function Probe([string]$Command, [string]$Id = "", [string]$Value = "") {
    $arguments = @("$script:application", $Command)
    if ($Id) { $arguments += $Id }
    if ($Command -eq "set-value") { $arguments += $Value }
    $output = & $script:probe @arguments 2>$null
    if ($LASTEXITCODE -eq 0) { return ($output -join "`n") }
    return $null
}
function Wait-For([string]$Description, [scriptblock]$Condition) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    do {
        if (& $Condition) {
            $script:assertions++
            return
        }
        if ($script:watch -and $script:watch.HasExited) {
            throw "dotnet watch exited during: $Description"
        }
        Start-Sleep -Milliseconds 100
    } while ($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds)
    throw "Timed out during: $Description"
}
function Write-Source([string]$Source) {
    [IO.File]::WriteAllText($script:inputFile, $Source, [Text.UTF8Encoding]::new($false))
}
function Read-WatchLog {
    $text = ""
    foreach ($path in @($script:stdout, $script:stderr)) {
        if (Test-Path $path) { $text += [IO.File]::ReadAllText($path) }
    }
    return $text
}
function Record-Edit([string]$Name, [string]$Source, [scriptblock]$Condition) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    Write-Source $Source
    Wait-For $Name $Condition
    $script:results.Add([pscustomobject]@{ edit = $Name; milliseconds = $clock.Elapsed.TotalMilliseconds })
}

$escapedTargets = [Security.SecurityElement]::Escape($targets)
$escapedLibrary = [Security.SecurityElement]::Escape($library)
@"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <RuntimeIdentifier>$RuntimeIdentifier</RuntimeIdentifier>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>
  </PropertyGroup>
  <Import Project="$escapedTargets" />
  <ItemGroup>
    <None Include="$escapedLibrary" Link="xui.dll" CopyToOutputDirectory="PreserveNewest" />
  </ItemGroup>
</Project>
"@ | Set-Content -LiteralPath $project -Encoding utf8
@'
using Xui;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        Console.WriteLine($"XUI integration process {Environment.ProcessId}");
        try
        {
#if XUI_HOT_RELOAD
            Xui.Development.ReloadHost.Run(
                () => new Window("XUI Declarative Integration", 480, 360),
                window => _ = new Demo.Counter(window));
#else
            using var window = new Window("XUI Declarative Integration", 480, 360);
            _ = new Demo.Counter(window);
            window.Run();
#endif
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
'@ | Set-Content -LiteralPath (Join-Path $run "Program.cs") -Encoding utf8
$source = @'
namespace Demo;
component Counter {
    state int Count = 0;
    state string Entry = "Initial";
    view {
        VStack(spacing: 8, padding: 16) {
            Text($"Count: {Count}", id: "count");
            Button("Increment", click: Increment, id: "increment");
            TextInput("Entry", text: Entry, change: SetEntry, id: "entry");
            Text($"Echo: {Entry}", id: "echo");
        }
    }
    code csharp {
        void Increment() => Count++;
        void SetEntry(string value) => Entry = value;
    }
}
'@
Write-Source $source

try {
    $listed = & dotnet watch --project $project --list 2>&1
    Assert ($LASTEXITCODE -eq 0) "dotnet watch --list failed: $listed"
    Assert (($listed -join "`n") -match "Counter\.xui") "The watcher does not include Counter.xui."
    $watch = Start-Process -FilePath (Get-Command dotnet).Source `
        -ArgumentList @("watch", "--project", "`"$project`"", "--non-interactive") `
        -WorkingDirectory $run -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    Wait-For "application startup" {
        $matches = [regex]::Matches((Read-WatchLog), "XUI integration process (\d+)")
        if ($matches.Count -eq 0) { return $false }
        $script:application = [int]$matches[$matches.Count - 1].Groups[1].Value
        return (Probe "name" "count") -eq "Count: 0"
    }
    $initialProcess = $application
    $initialWindow = Probe "window"
    $null = Probe "invoke" "increment"
    Wait-For "generated state update" { (Probe "name" "count") -eq "Count: 1" }
    $null = Probe "set-value" "entry" "User text"
    Wait-For "two-way input event" { (Probe "name" "echo") -eq "Echo: User text" }

    $source = $source.Replace('Text($"Count: {Count}"', 'Text($"Total: {Count}"')
    Record-Edit "text live reload" $source { (Probe "name" "count") -eq "Total: 1" }
    Assert ($application -eq $initialProcess) "A text edit restarted the application."
    Assert ((Probe "window") -eq $initialWindow) "A text edit replaced the native window."
    Assert ((Probe "value" "entry") -eq "User text") "A text edit reset the native input."

    $before = (Probe "bounds" "increment") | ConvertFrom-Json
    $source = $source.Replace("spacing: 8", "spacing: 24")
    Record-Edit "spacing live reload" $source {
        $bounds = Probe "bounds" "increment"
        return $bounds -and (($bounds | ConvertFrom-Json).top -gt $before.top)
    }
    Assert ((Probe "window") -eq $initialWindow) "A spacing edit replaced the native window."

    $source = $source.Replace("Count++;", "Count += 2;")
    $logLength = (Read-WatchLog).Length
    Write-Source $source
    Wait-For "handler code delta" {
        $log = Read-WatchLog
        return $log.Length -gt $logLength -and $log.Substring($logLength) -match "XUI hot reload applied"
    }
    $null = Probe "invoke" "increment"
    Wait-For "updated event-handler behavior" { (Probe "name" "count") -eq "Total: 3" }
    Assert ((Probe "window") -eq $initialWindow) "A handler-body edit replaced the native window."

    foreach ($prefix in @("Value", "Current", "Total")) {
        $source = $source -replace 'Text\(\$"(Total|Value|Current): \{Count\}"', ('Text($"' + $prefix + ': {Count}"')
        $expected = "${prefix}: 3"
        Record-Edit "repeated text reload ($prefix)" $source { (Probe "name" "count") -eq $expected }
    }
    $null = Probe "invoke" "increment"
    Wait-For "single event subscription after repeated reload" { (Probe "name" "count") -eq "Total: 5" }

    $logLength = (Read-WatchLog).Length
    Write-Source ($source.Replace("spacing: 24", "unknownArgument: 24"))
    Wait-For "invalid input diagnostic" {
        $log = Read-WatchLog
        return $log.Length -gt $logLength -and $log.Substring($logLength) -match 'Counter\.xui\(\d+,\d+\).*error'
    }
    Assert ((Probe "name" "count") -eq "Total: 5") "An invalid edit changed the live UI."
    $source = $source.Replace('Text($"Total: {Count}"', 'Text($"Recovered: {Count}"')
    Record-Edit "recovery after invalid input" $source { (Probe "name" "count") -eq "Recovered: 5" }
    Assert ((Probe "window") -eq $initialWindow) "Recovery replaced the native window."

    $source = $source.Replace('Text($"Echo: {Entry}", id: "echo");',
        'Text($"Echo: {Entry}", id: "echo");' + "`n            " + 'Text("Added", id: "added");')
    Record-Edit "structural fallback" $source { (Probe "name" "added") -eq "Added" }
    Assert ((Read-WatchLog) -match "recreating the window") "The structural fallback did not report window recreation."
    Assert ((Probe "name" "count") -eq "Recovered: 0") "The documented structural fallback did not reset component state."
    Assert ((Probe "value" "entry") -eq "Initial") "The structural fallback did not recreate native input."

    $null = Probe "close"
    Wait-For "application shutdown" { !(Get-Process -Id $application -ErrorAction SilentlyContinue) }
    $watch.Kill($true)
    $watch.WaitForExit()
    $watch.Dispose()
    $watch = $null
    $results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $run "edit-times.json") -Encoding utf8
    Write-Host "$assertions assertions passed. Results: $run"
} catch {
    Write-Host (Read-WatchLog)
    Write-Host "Failure artifacts: $run"
    throw
} finally {
    if ($watch) {
        if (!$watch.HasExited) { $watch.Kill($true); $watch.WaitForExit() }
        $watch.Dispose()
    }
}
