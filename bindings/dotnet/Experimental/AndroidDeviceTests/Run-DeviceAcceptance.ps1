#requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Serial,
    [string]$AdbPath = 'adb',
    [ValidateRange(10, 120)]
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$adb = (Get-Command $AdbPath -ErrorAction Stop).Source
$demoPackage = 'dev.xui.portable.greeting'
$testsPackage = 'dev.xui.portable.tests'
$remoteXml = "/sdcard/xui-device-acceptance-$PID.xml"
$script:assertions = 0

function Invoke-Adb {
    $arguments = @($args)
    $start = [Diagnostics.ProcessStartInfo]::new($adb)
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in (@('-s', $Serial) + $arguments)) {
        $start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $process.Id -Force
            throw "adb command timed out: $($arguments -join ' ')"
        }
        $output = $stdout.GetAwaiter().GetResult()
        $errorOutput = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) {
            throw "adb failed ($($process.ExitCode)): $($arguments -join ' ')`n$output`n$errorOutput"
        }
        if ($errorOutput.Trim()) { Write-Verbose $errorOutput.Trim() }
        return $output.Trim()
    }
    finally { $process.Dispose() }
}

function Assert-Device([bool]$Condition, [string]$Description) {
    if (-not $Condition) { throw "FAIL: $Description" }
    $script:assertions++
    Write-Host "PASS: $Description"
}

function Get-Launcher([string]$Package) {
    $component = Invoke-Adb shell cmd package resolve-activity --brief -a android.intent.action.MAIN -c android.intent.category.LAUNCHER $Package
    $line = @($component -split '\r?\n' | Where-Object { $_.StartsWith("$Package/") })
    if ($line.Count -ne 1) { throw "Install $Package before running acceptance. Launcher result: $component" }
    return $line[0]
}

function Start-Launcher([string]$Component) {
    # MAIN/LAUNCHER resumes the existing task; a bare component can create another Activity.
    $result = Invoke-Adb shell am start -W -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n $Component
    if ($result -notmatch 'Status: ok') { throw "Activity launch failed: $result" }
}

function Get-Ui {
    $null = Invoke-Adb shell uiautomator dump $remoteXml
    return [xml](Invoke-Adb shell cat $remoteXml)
}

function Wait-Node([string]$XPath, [string]$ScrollDirection = '') {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $previousTree = ''
    do {
        $tree = Get-Ui
        $node = $tree.SelectSingleNode($XPath)
        if ($null -ne $node) { return $node }
        if ($ScrollDirection) {
            if ($tree.OuterXml -eq $previousTree) {
                $ScrollDirection = if ($ScrollDirection -eq 'down') { 'up' } else { 'down' }
            }
            $previousTree = $tree.OuterXml
            $scroll = $tree.SelectSingleNode("//node[@package='$demoPackage' and @class='android.widget.ScrollView']")
            if ($null -ne $scroll -and $scroll.bounds -match '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') {
                $x = [int]$Matches[3] - 20
                $top = [int]$Matches[2] + 10
                $bottom = [int]$Matches[4] - 10
                if ($ScrollDirection -eq 'up') {
                    $null = Invoke-Adb shell input swipe $x $top $x $bottom 250
                }
                else { $null = Invoke-Adb shell input swipe $x $bottom $x $top 250 }
            }
        }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out waiting for UI node: $XPath`n$($tree.OuterXml)"
}

function Tap-Node([System.Xml.XmlElement]$Node) {
    if ($Node.bounds -notmatch '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') {
        throw "Invalid native bounds: $($Node.bounds)"
    }
    $x = [int](([int]$Matches[1] + [int]$Matches[3]) / 2)
    $y = [int](([int]$Matches[2] + [int]$Matches[4]) / 2)
    $null = Invoke-Adb shell input tap $x $y
}

function Tap-Button([string]$Text) {
    Tap-Node (Wait-Node "//node[@package='$demoPackage' and @class='android.widget.Button' and @text='$Text' and @enabled='true']" 'down')
}

function Assert-Text([string]$Text, [string]$Description) {
    $null = Wait-Node "//node[@package='$demoPackage' and @class='android.widget.TextView' and @text='$Text']" 'down'
    Assert-Device $true $Description
}

function Assert-Entry([string]$Text, [string]$Description) {
    $null = Wait-Node "//node[@package='$demoPackage' and @class='android.widget.EditText' and @text='$Text' and @focused='true']" 'up'
    Assert-Device $true $Description
}

function Select-AllText {
    $null = Invoke-Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
}

function Assert-ImeViewport([string]$Description) {
    $window = Invoke-Adb shell dumpsys window
    $frames = @([regex]::Matches($window, 'type=ime frame=\[\d+,(\d+)\]\[\d+,(\d+)\][^\r\n]*visible=true') |
        Where-Object { [int]$_.Groups[2].Value -gt [int]$_.Groups[1].Value })
    if ($frames.Count -eq 0) { throw 'Expected a visible docked IME with a nonzero native inset.' }
    $imeTop = [int]$frames[0].Groups[1].Value
    $tree = Get-Ui
    $scroll = $tree.SelectSingleNode("//node[@package='$demoPackage' and @class='android.widget.ScrollView']")
    if ($null -eq $scroll -or $scroll.bounds -notmatch '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') {
        throw 'The demo scroll viewport is not visible above the keyboard.'
    }
    Assert-Device ([int]$Matches[4] -le $imeTop) $Description
    Write-Host "Native viewport $($scroll.bounds); IME top $imeTop."
}

Assert-Device ((Invoke-Adb get-state) -eq 'device') "Target $Serial is connected."
$testsLauncher = Get-Launcher $testsPackage
$demoLauncher = Get-Launcher $demoPackage
$rotationPolicy = Invoke-Adb shell wm user-rotation
if ($rotationPolicy -notmatch '^(free|lock [0-3])$') {
    throw "Unrecognized rotation policy: $rotationPolicy"
}
$originalRotation = Invoke-Adb shell settings get system user_rotation
if ($originalRotation -notmatch '^[0-3]$') { throw "Unrecognized display rotation: $originalRotation" }
$originalShowIme = Invoke-Adb shell settings get secure show_ime_with_hard_keyboard
if ($originalShowIme -notmatch '^(null|0|1)$') { throw "Unrecognized hardware-keyboard IME preference: $originalShowIme" }
Write-Host "API $(Invoke-Adb shell getprop ro.build.version.sdk); ABI $(Invoke-Adb shell getprop ro.product.cpu.abi)"
Write-Host "$(Invoke-Adb shell wm density); font scale $(Invoke-Adb shell settings get system font_scale)"
Write-Host "IME $(Invoke-Adb shell settings get secure default_input_method)"
Write-Host "Accessibility services $(Invoke-Adb shell settings get secure enabled_accessibility_services)"

try {
    $null = Invoke-Adb shell settings put secure show_ime_with_hard_keyboard 1
    $null = Invoke-Adb shell wm user-rotation lock 0
    $null = Invoke-Adb shell am force-stop $testsPackage
    Start-Launcher $testsLauncher
    $testPid = Invoke-Adb shell pidof $testsPackage
    if ($testPid -notmatch '^\d+$') { throw "Native test process did not start: $testPid" }
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $log = Invoke-Adb logcat -d "--pid=$testPid" -s Xui.Android.Tests:I Xui.Android:E AndroidRuntime:E
        if ($log -match 'FAIL:|FATAL EXCEPTION|E Xui.Android\s*:') { throw $log }
        if ($log -match 'PASS: \d+ Android native assertions\.') { break }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    Assert-Device ($log -match 'PASS: \d+ Android native assertions\.') "Native assertions completed in the new process."
    Write-Host $Matches[0]
    $null = Wait-Node "//node[@package='$testsPackage' and starts-with(@text, 'PASS: ')]"
    Assert-Device $true 'The native assertion Activity displays PASS.'

    $null = Invoke-Adb shell am force-stop $demoPackage
    Start-Launcher $demoLauncher
    Assert-Text 'Count: 0' 'The demo starts from authored initial state.'
    $increment = Wait-Node "//node[@package='$demoPackage' and @class='android.widget.Button' and @text='INCREMENT']"
    for ($i = 0; $i -lt 10; $i++) { Tap-Node $increment }
    Assert-Text 'Count: 10' 'Ten taps produce ten authored increments.'
    $increment = Wait-Node "//node[@package='$demoPackage' and @class='android.widget.Button' and @text='INCREMENT' and @enabled='false']"
    Tap-Node $increment
    Assert-Text 'Count: 10' 'The disabled increment does not change the count.'
    Tap-Button 'RESET'
    Assert-Text 'Count: 0' 'Reset restores the count.'

    Tap-Node (Wait-Node "//node[@package='$demoPackage' and @class='android.widget.EditText']")
    $null = Invoke-Adb shell input text Ada
    Assert-ImeViewport 'The portrait scroll viewport ends above the visible docked keyboard.'
    $null = Invoke-Adb shell input keyevent KEYCODE_ENTER
    Assert-Entry 'Ada' 'Injected native keyboard input reaches the retained editor.'
    Assert-Text 'Hello, Ada!' 'Hardware Enter submits the current text.'
    Select-AllText
    Tap-Button 'INCREMENT'
    $null = Invoke-Adb shell input text Grace
    Assert-Entry 'Grace' 'An unrelated update preserves focus and the selected range.'
    Tap-Button 'SUBMIT'
    Assert-Text 'Hello, Grace!' 'The Submit button uses the edited value.'

    Select-AllText
    $null = Invoke-Adb shell wm user-rotation lock 1
    $null = Wait-Node "/hierarchy[@rotation='1']//node[@package='$demoPackage' and @class='android.widget.EditText' and @text='Grace' and @focused='true']"
    Assert-Text 'Hello, Grace!' 'Rotation restores the submitted message.'
    Assert-Text 'Count: 1' 'Rotation restores the counter.'
    $null = Invoke-Adb shell input text Ada
    Assert-Entry 'Ada' 'Activity recreation restores entry, focus and the selected range.'
    Tap-Node (Wait-Node "//node[@package='$demoPackage' and @class='android.widget.EditText']")
    Assert-ImeViewport 'The landscape viewport respects the current docked keyboard inset.'

    Select-AllText
    $null = Invoke-Adb shell input keyevent KEYCODE_HOME
    $null = Wait-Node "//node[@package!='$demoPackage' and @package!='com.android.systemui']"
    Start-Launcher $demoLauncher
    Assert-Entry 'Ada' 'Home and launcher resume preserve entry and focus.'
    Assert-Text 'Count: 1' 'Background reattachment retains the counter.'
    $null = Invoke-Adb shell input text Grace
    Assert-Entry 'Grace' 'Background reattachment preserves the selected range.'
    Assert-ImeViewport 'The landscape viewport remains above the keyboard after background restore.'
    Tap-Button 'INCREMENT'
    Assert-Text 'Count: 2' 'One tap after reattachment produces exactly one increment.'
    Tap-Button 'RESET'
    Assert-Text 'Count: 0' 'Reset works after recreation and reattachment.'
    $null = Invoke-Adb shell input text Grace
    Assert-Entry 'Grace' 'Reset cleared the focused editor without losing focus.'

    $demoPid = Invoke-Adb shell pidof $demoPackage
    $log = Invoke-Adb logcat -d "--pid=$demoPid" -s Xui.Android:E AndroidRuntime:E
    Assert-Device ($log -notmatch 'FATAL EXCEPTION|E Xui.Android\s*:') "The demo completed without adapter or Android runtime errors."
    Write-Host "PASS: $script:assertions Android device acceptance checks."
    Write-Host 'ADB input and native composition fixtures do not establish physical IME or TalkBack acceptance.'
}
finally {
    if ($rotationPolicy -eq 'free') {
        $null = Invoke-Adb shell wm user-rotation lock $originalRotation
        $null = Invoke-Adb shell wm user-rotation free
    }
    else { $null = Invoke-Adb shell wm user-rotation lock $rotationPolicy.Split(' ')[1] }
    if ($originalShowIme -eq 'null') { $null = Invoke-Adb shell settings delete secure show_ime_with_hard_keyboard }
    else { $null = Invoke-Adb shell settings put secure show_ime_with_hard_keyboard $originalShowIme }
    $null = Invoke-Adb shell rm -f $remoteXml
}
