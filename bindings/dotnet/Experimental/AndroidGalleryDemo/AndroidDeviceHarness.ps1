function Adb {
    $arguments = @($args)
    $start = [Diagnostics.ProcessStartInfo]::new($adb)
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in (@('-s', $Serial) + $arguments)) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $process.Id -Force
            throw "adb timed out: $($arguments -join ' ')"
        }
        $text = $stdout.GetAwaiter().GetResult()
        $errors = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "adb failed: $($arguments -join ' ')`n$text`n$errors" }
        if ($errors.Trim()) { Write-Verbose $errors.Trim() }
        if ($arguments.Count -ge 3 -and $arguments[0] -eq 'shell' -and $arguments[1] -eq 'uiautomator' -and $arguments[2] -eq 'dump') {
            return ($text + "`n" + $errors).Trim()
        }
        return $text.Trim()
    }
    finally { $process.Dispose() }
}

function Tree {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $output = Adb shell uiautomator dump $remoteXml
        if ($output -match 'UI hierchary dumped to:|UI hierarchy dumped to:') {
            return [xml](Adb shell cat $remoteXml)
        }
        if ($output -notmatch 'null root node returned') { throw "Native UI dump failed: $output" }
        Write-Verbose 'The launching Activity has no accessibility root yet.'
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'The native Activity did not publish an accessibility root before the timeout.'
}

function Find([string]$XPath, [string]$Direction = 'down') {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $tree = Tree
        $node = $tree.SelectSingleNode($XPath)
        if ($null -ne $node) { return $node }
        $scroll = $tree.SelectSingleNode("//node[@package='$script:package' and @class='android.widget.ScrollView']")
        if ($Direction -and $null -ne $scroll -and $scroll.bounds -match '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') {
            $x = [int]$Matches[3] - 20
            $top = [int]$Matches[2] + 10
            $bottom = [int]$Matches[4] - 10
            if ($Direction -eq 'up') { $null = Adb shell input swipe $x $top $x $bottom 250 }
            else { $null = Adb shell input swipe $x $bottom $x $top 250 }
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Expected sample content was not visible: $XPath`n$($tree.OuterXml)"
}

function Tap([System.Xml.XmlElement]$Node) {
    if ($Node.bounds -notmatch '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') { throw "Invalid bounds: $($Node.bounds)" }
    $x = [int](([int]$Matches[1] + [int]$Matches[3]) / 2)
    $y = [int](([int]$Matches[2] + [int]$Matches[4]) / 2)
    $null = Adb shell input tap $x $y
}
