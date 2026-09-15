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
$sample = Join-Path $root "bindings\dotnet\Minesweeper"
$run = Join-Path $root ("build\minesweeper-check\" + [Guid]::NewGuid().ToString("N"))
$oldPath = $env:PATH
$env:PATH = "$native;$oldPath"
$watch = $null
$published = $null
$application = 0
$assertions = 0
$lastProbeFailure = ""
New-Item -ItemType Directory -Path $run -Force | Out-Null
$stdout = Join-Path $run "watch.stdout.log"
$stderr = Join-Path $run "watch.stderr.log"
$inputFile = Join-Path $run "Minefield.xui"

function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    $script:assertions++
}
function Read-Log {
    $text = ""
    foreach ($path in @($script:stdout, $script:stderr)) {
        if (Test-Path $path) {
            $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
            $reader = [IO.StreamReader]::new($stream)
            try { $text += $reader.ReadToEnd() }
            finally { $reader.Dispose() }
        }
    }
    return $text
}
function Probe([string]$Command, [string]$Id = "", [switch]$Retry) {
    $arguments = @("$script:application", $Command)
    if ($Id) { $arguments += $Id }
    $output = & $script:probe @arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        $script:lastProbeFailure = "$Command $Id : $output"
        if ($Retry) { return $null }
        throw $script:lastProbeFailure
    }
    return ($output -join "`n")
}
function Wait-For([string]$Description, [scriptblock]$Condition) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    do {
        if ($script:watch) {
            if ($script:watch.HasExited) { throw "dotnet watch exited during $Description" }
            $matches = [regex]::Matches((Read-Log), "Minesweeper fixture process (\d+)")
            if ($matches.Count) { $script:application = [int]$matches[-1].Groups[1].Value }
        }
        if ($script:published -and $script:published.HasExited) { throw "The published sample exited during $Description" }
        if ($script:application -and (& $Condition)) { $script:assertions++; return }
        Start-Sleep -Milliseconds 100
    } while ($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds)
    throw "Timed out during $Description. Last probe failure: $script:lastProbeFailure"
}
function Cell-Id([int]$Index) {
    return "cell-$([int][Math]::Floor($Index / 9) + 1)-$($Index % 9 + 1)"
}
function Save-Source([string]$Source) {
    [IO.File]::WriteAllText($script:inputFile, $Source, [Text.UTF8Encoding]::new($false))
}

try {
    Assert ((Test-Path $probe) -and (Test-Path (Join-Path $native "xui.dll"))) "Build xui and xui_language_probe first."
    $layoutOutput = & dotnet run --project (Join-Path $root "bindings\dotnet\Minesweeper.Tests") -v:q -- --layout 17 40
    Assert ($LASTEXITCODE -eq 0) "Cannot compute the seeded game."
    $layout = ($layoutOutput -join "`n") | ConvertFrom-Json
    Copy-Item (Join-Path $sample "GameState.cs") $run
    $program = [IO.File]::ReadAllText((Join-Path $sample "Program.cs"))
    $program = $program.Replace("int? seed = args switch",
        'Console.WriteLine($"Minesweeper fixture process {Environment.ProcessId}");' + "`n            int? seed = args switch")
    [IO.File]::WriteAllText((Join-Path $run "Program.cs"), $program)
    $targets = [Security.SecurityElement]::Escape((Join-Path $root "bindings\dotnet\Xui.Declarative.targets"))
    $projectText = [IO.File]::ReadAllText((Join-Path $sample "Minesweeper.csproj")).
        Replace('..\Xui.Declarative.targets', $targets).
        Replace("win-arm64", $RuntimeIdentifier)
    $project = Join-Path $run "Minesweeper.csproj"
    [IO.File]::WriteAllText($project, $projectText)
    $source = [IO.File]::ReadAllText((Join-Path $sample "Minefield.xui"))
    Save-Source $source
    $watch = Start-Process -FilePath (Get-Command dotnet).Source `
        -ArgumentList @("watch", "--project", "`"$project`"", "--non-interactive", "--", "--seed", "17") `
        -WorkingDirectory $run -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    Wait-For "startup" { (Probe "name" "heading" -Retry) -eq "MINESWEEPER" }
    $initialProcess = $application
    $initialWindow = Probe "window"
    $ready = "Flags: 0/10    Safe squares: 0/71    Moves: 0"
    Assert ((Probe "name" "summary") -eq $ready) "Initial counters are incorrect."
    $bounds = @{}
    foreach ($i in 0..80) {
        $id = Cell-Id $i
        Assert ((Probe "name" $id) -eq "?") "The board must start covered: $id"
        Assert ((Probe "help" $id) -eq "Row $([int][Math]::Floor($i / 9) + 1), column $($i % 9 + 1): covered.") "Wrong cell binding: $id"
        $rect = (Probe "bounds" $id) | ConvertFrom-Json
        $bounds[$i] = $rect
        Assert ($rect.width -gt 0 -and $rect.width -eq $rect.height) "A cell is not square: $id"
        if ($i -gt 0) { Assert ($rect.width -eq $bounds[0].width) "Unequal cell widths: $id" }
        if ($i -ge 9) { Assert ($rect.left -eq $bounds[$i % 9].left) "A column is misaligned: $id" }
        if ($i % 9) { Assert ($rect.top -eq $bounds[$i - 1].top -and $rect.left -gt ($bounds[$i - 1].left + $rect.width)) "Cells overlap: $id" }
    }
    $null = Probe "key" "70"
    Wait-For "flag shortcut" { (Probe "name" "instructions" -Retry) -like "*place or clear a flag*" }
    $null = Probe "invoke" "cell-1-1"
    Wait-For "flag placement" { (Probe "name" "cell-1-1" -Retry) -eq "F" }
    $null = Probe "toggle" "flag-mode"
    Wait-For "reveal mode" { (Probe "enabled" "cell-1-1" -Retry) -eq "false" }
    $null = Probe "invoke" "cell-5-5"
    $playing = "Flags: 1/10    Safe squares: $($layout.revealed)/71    Moves: 2"
    Wait-For "first safe reveal" { (Probe "name" "summary" -Retry) -eq $playing }
    Assert ((Probe "help" "cell-5-5") -eq "Row 5, column 5: empty.") "The first reveal did not open an empty square."

    $source = $source.Replace('Text("MINESWEEPER"', 'Text("MINESWEEPER LIVE"')
    Save-Source $source
    Wait-For "live heading edit" { (Probe "name" "heading" -Retry) -eq "MINESWEEPER LIVE" }
    Assert ($application -eq $initialProcess -and (Probe "window") -eq $initialWindow) "A text edit replaced the window."
    Assert ((Probe "name" "summary") -eq $playing) "A text edit reset the game."
    $source = $source.Replace("size: (36, 36)", "size: (34, 34)").
        Replace("help: Game.CellDescription(0)", 'help: Game.CellDescription(0) + " (live)"')
    Save-Source $source
    Wait-For "live size and help edits" { (Probe "help" "cell-1-1" -Retry) -like "* (live)" }
    $smaller = (Probe "bounds" "cell-1-1") | ConvertFrom-Json
    Assert ($smaller.width -lt $bounds[0].width -and $smaller.width -eq $smaller.height) "A size edit did not resize the retained cell."
    Assert ($application -eq $initialProcess -and (Probe "window") -eq $initialWindow -and (Probe "name" "summary") -eq $playing) "A property edit reset the game."
    foreach ($i in $layout.safe) {
        $id = Cell-Id $i
        if ((Probe "enabled" $id) -eq "true") { $null = Probe "invoke" $id }
    }
    Wait-For "win" { (Probe "name" "status" -Retry) -eq "You won! All 71 safe squares are clear." }
    Assert ((Probe "name" "summary") -like "Flags: 10/10    Safe squares: 71/71*") "Win counters are incorrect."
    foreach ($i in $layout.mines) {
        Assert ((Probe "name" (Cell-Id $i)) -eq "F") "Winning did not flag a mine."
    }

    $source = $source.Replace('help: Game.CellDescription(0) + " (live)", ', "")
    Save-Source $source
    Wait-For "optional binding removal" { (Probe "name" "summary" -Retry) -eq $ready }
    Assert ((Probe "help" "cell-1-1") -eq "") "Removing a help binding retained stale help."
    $null = Probe "invoke" "cell-5-5"
    Wait-For "replacement board" { (Probe "name" "summary" -Retry) -like "*Safe squares: $($layout.revealed)/71*" }
    $null = Probe "invoke" "cell-1-1"
    Wait-For "loss" { (Probe "name" "status" -Retry) -eq "Mine hit! Start a new game to try again." }
    Assert ((Probe "name" "cell-1-1") -eq "!" -and (Probe "name" (Cell-Id $layout.mines[1])) -eq "*") "Loss did not expose mines."
    Assert ((Probe "enabled" "cell-1-2") -eq "false") "Loss left the board active."
    $null = Probe "key" "113"
    Wait-For "F2 restart" { (Probe "name" "summary" -Retry) -eq $ready }
    $null = Probe "toggle" "flag-mode"
    $null = Probe "invoke" "cell-9-9"
    Wait-For "new-board flag" { (Probe "name" "cell-9-9" -Retry) -eq "F" }
    $null = Probe "invoke" "new-game"
    Wait-For "button restart" { (Probe "name" "summary" -Retry) -eq $ready }
    Assert ((Probe "name" "instructions") -like "*Click to reveal*") "Restart did not clear flag mode."
    $null = Probe "close"
    Wait-For "shutdown" { !(Get-Process -Id $script:application -ErrorAction SilentlyContinue) }
    $watch.Kill($true)
    $watch.WaitForExit()
    $watch.Dispose()
    $watch = $null

    $publishDirectory = Join-Path $run "publish"
    $output = & dotnet publish (Join-Path $sample "Minesweeper.csproj") -c Release -r $RuntimeIdentifier `
        -p:PublishAot=true -o $publishDirectory --nologo -v:q 2>&1
    $output | Set-Content (Join-Path $run "publish.log")
    Assert ($LASTEXITCODE -eq 0) "NativeAOT publish failed: $output"
    Copy-Item (Join-Path $native "xui.dll") $publishDirectory
    $published = Start-Process -FilePath (Join-Path $publishDirectory "Minesweeper.exe") `
        -ArgumentList @("--seed", "17") -RedirectStandardOutput (Join-Path $run "published.stdout.log") `
        -RedirectStandardError (Join-Path $run "published.stderr.log") -PassThru
    $application = $published.Id
    Wait-For "NativeAOT startup" { (Probe "name" "summary" -Retry) -eq $ready }
    $null = Probe "invoke" "cell-5-5"
    Wait-For "NativeAOT reveal" { (Probe "name" "summary" -Retry) -eq "Flags: 0/10    Safe squares: $($layout.revealed)/71    Moves: 1" }
    $null = Probe "close"
    Assert ($published.WaitForExit(10000) -and $published.ExitCode -eq 0) "NativeAOT shutdown failed."
    Write-Host "$assertions assertions passed. Results: $run"
} catch {
    Write-Host (Read-Log)
    Write-Host "Failure artifacts: $run"
    throw
} finally {
    foreach ($process in @($watch, $published)) {
        if ($process) {
            if (!$process.HasExited) { $process.Kill($true); $process.WaitForExit() }
            $process.Dispose()
        }
    }
    $env:PATH = $oldPath
}
