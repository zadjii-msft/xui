#requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Serial,
    [string]$AdbPath = 'adb',
    [ValidateRange(30, 180)][int]$TimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$adb = (Get-Command $AdbPath -ErrorAction Stop).Source
$package = 'dev.xui.portable.orders'
$testsPackage = 'dev.xui.portable.orders.tests'
$remoteXml = "/sdcard/xui-order-acceptance-$PID.xml"
$script:assertions = 0

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
        $output = $stdout.GetAwaiter().GetResult()
        $errors = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "adb failed: $($arguments -join ' ')`n$output`n$errors" }
        if ($errors.Trim()) { Write-Verbose $errors.Trim() }
        return $output.Trim()
    }
    finally { $process.Dispose() }
}

function Check([bool]$Condition, [string]$Description) {
    if (-not $Condition) { throw "FAIL: $Description" }
    $script:assertions++
    Write-Host "PASS: $Description"
}

function Launcher([string]$PackageName) {
    $result = Adb shell cmd package resolve-activity --brief -a android.intent.action.MAIN -c android.intent.category.LAUNCHER $PackageName
    $lines = @($result -split '\r?\n' | Where-Object { $_.StartsWith("$PackageName/") })
    if ($lines.Count -ne 1) { throw "Install $PackageName before acceptance: $result" }
    return $lines[0]
}

function Launch([string]$Component) {
    $result = Adb shell am start -W -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n $Component
    if ($result -notmatch 'Status: ok') { throw "Activity launch failed: $result" }
}

function Tree {
    $null = Adb shell uiautomator dump $remoteXml
    return [xml](Adb shell cat $remoteXml)
}

function Find([string]$XPath, [string]$Direction = 'down') {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $tree = Tree
        $node = $tree.SelectSingleNode($XPath)
        if ($null -ne $node) { return $node }
        if ($Direction) {
            $scroll = $tree.SelectSingleNode("//node[@package='$package' and @class='android.widget.ScrollView']")
            if ($null -ne $scroll -and $scroll.bounds -match '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') {
                $x = [int]$Matches[3] - 20
                $top = [int]$Matches[2] + 10
                $bottom = [int]$Matches[4] - 10
                if ($Direction -eq 'up') { $null = Adb shell input swipe $x $top $x $bottom 200 }
                else { $null = Adb shell input swipe $x $bottom $x $top 200 }
            }
        }
        Start-Sleep -Milliseconds 150
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out finding $XPath`n$($tree.OuterXml)"
}

function Tap([System.Xml.XmlElement]$Node) {
    if ($Node.bounds -notmatch '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') { throw "Invalid bounds: $($Node.bounds)" }
    $x = [int](([int]$Matches[1] + [int]$Matches[3]) / 2)
    $y = [int](([int]$Matches[2] + [int]$Matches[4]) / 2)
    $null = Adb shell input tap $x $y
}

function Button([string]$Text, [string]$Direction = 'down') {
    Tap (Find "//node[@package='$package' and @class='android.widget.Button' and @text='$Text' and @enabled='true']" $Direction)
}

function Input([string]$Caption, [string]$Direction = 'up') {
    return Find "//node[@package='$package' and @class='android.widget.EditText' and ../node[@text='$Caption']]" $Direction
}

function Fill([string]$Caption, [string]$Text, [string]$Direction = 'down') {
    Tap (Input $Caption $Direction)
    $null = Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
    $null = Adb shell input text $Text
    Entry $Caption $Text
}

function Entry([string]$Caption, [string]$Text) {
    $node = Find "//node[@package='$package' and @class='android.widget.EditText' and @text='$Text' and @focused='true']" ''
    Check ($node.text -eq $Text -and $node.focused -eq 'true') "$Caption has '$Text' and native focus."
}

function Text([string]$Value, [string]$Direction = 'down') {
    $null = Find "//node[@package='$package' and @class='android.widget.TextView' and @text='$Value']" $Direction
    Check $true "Native display: $Value"
}

function ImeViewport {
    $frames = @([regex]::Matches((Adb shell dumpsys window), 'type=ime frame=\[\d+,(\d+)\]\[\d+,(\d+)\][^\r\n]*visible=true') |
        Where-Object { [int]$_.Groups[2].Value -gt [int]$_.Groups[1].Value })
    if ($frames.Count -eq 0) { throw 'Expected a visible docked IME with a nonzero native inset.' }
    $top = [int]$frames[0].Groups[1].Value
    $scroll = (Tree).SelectSingleNode("//node[@package='$package' and @class='android.widget.ScrollView']")
    if ($null -eq $scroll -or $scroll.bounds -notmatch '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') {
        throw 'The order scroll viewport is not visible.'
    }
    Check ([int]$Matches[4] -le $top) "Order viewport $($scroll.bounds) ends above IME top $top."
}

Check ((Adb get-state) -eq 'device') "Target $Serial is connected."
$demo = Launcher $package
$tests = Launcher $testsPackage
$rotation = Adb shell wm user-rotation
if ($rotation -notmatch '^(free|lock [0-3])$') { throw "Unknown rotation policy: $rotation" }
$angle = Adb shell settings get system user_rotation
if ($angle -notmatch '^[0-3]$') { throw "Unknown display angle: $angle" }
$showIme = Adb shell settings get secure show_ime_with_hard_keyboard
if ($showIme -notmatch '^(null|0|1)$') { throw "Unknown IME preference: $showIme" }

try {
    $null = Adb shell settings put secure show_ime_with_hard_keyboard 1
    $null = Adb shell wm user-rotation lock 0
    $null = Adb shell am force-stop $testsPackage
    Launch $tests
    $testPid = Adb shell pidof $testsPackage
    if ($testPid -notmatch '^\d+$') { throw "The native scenario process did not start: $testPid" }
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $log = Adb logcat -d "--pid=$testPid" -s Xui.Android.Orders:I Xui.Android:E AndroidRuntime:E
        if ($log -match 'FAIL:|FATAL EXCEPTION|E Xui.Android\s*:') { throw $log }
        if ($log -match 'PASS: \d+ shared order expectations; \d+ Android order assertions\.') { break }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    Check ($log -match 'PASS: \d+ shared order expectations; \d+ Android order assertions\.') 'Shared scenarios passed on native widgets.'
    Write-Host $Matches[0]
    $null = Find "//node[@package='$testsPackage' and starts-with(@text, 'PASS: ')]" ''
    Check $true 'The native order test Activity displays PASS.'

    $null = Adb shell am force-stop $package
    Launch $demo
    $null = Find "//node[@package='$package' and @class='android.widget.Button' and @text='REVIEW ORDER' and @enabled='false']"
    Check $true 'The empty order disables Review.'
    Fill 'Customer name' 'Ada' 'up'
    Fill 'Email' 'ada@example.com'
    Fill 'Discount code' 'SAVE10'
    ImeViewport
    Button 'MORE COFFEE' 'up'
    Button 'MORE COFFEE'
    Button 'MORE TEA'
    Button 'MORE COCOA'
    Text 'Total: $38.70'

    $fields = @(
        @{ Caption = 'Customer name'; Before = 'Ada'; After = 'Grace'; Angle = 1; Direction = 'up' },
        @{ Caption = 'Email'; Before = 'ada@example.com'; After = 'grace@example.com'; Angle = 0; Direction = 'down' },
        @{ Caption = 'Discount code'; Before = 'SAVE10'; After = 'SAVE10'; Angle = 1; Direction = 'down' }
    )
    foreach ($field in $fields) {
        Tap (Input $field.Caption $field.Direction)
        $null = Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
        $null = Adb shell wm user-rotation lock $field.Angle
        $null = Find "/hierarchy[@rotation='$($field.Angle)']" ''
        Entry $field.Caption $field.Before
        $null = Adb shell input text $field.After
        Entry $field.Caption $field.After
        $null = Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
        $null = Adb shell input keyevent KEYCODE_HOME
        $null = Find "//node[@package!='$package' and @package!='com.android.systemui']" ''
        Launch $demo
        Entry $field.Caption $field.After
        $null = Adb shell input text $field.After
        Entry $field.Caption $field.After
        Check $true "$($field.Caption) restores its selected range after rotation and Home/launcher resume."
    }

    Tap (Input 'Discount code')
    ImeViewport
    Button 'MORE TEA' 'up'
    Text 'Tea: 2' 'up'
    Text 'Total: $45.90'
    Button 'REVIEW ORDER'
    $null = Find "//node[@package='$package' and @class='android.widget.Button' and @text='EDIT' and @enabled='true']"
    Check $true 'Review displays the local review state.'
    $null = Adb shell wm user-rotation lock 0
    $null = Find "/hierarchy[@rotation='0']" ''
    $null = Find "//node[@package='$package' and @class='android.widget.Button' and @text='EDIT' and @enabled='true']"
    Check $true 'Rotation restores Reviewing as well as order fields.'
    Text 'Total: $45.90' 'up'
    $null = Adb shell input keyevent KEYCODE_HOME
    $null = Find "//node[@package!='$package' and @package!='com.android.systemui']" ''
    Launch $demo
    Button 'EDIT'
    Button 'RESET'
    Text 'Coffee: 0' 'up'
    Text 'Tea: 0'
    Text 'Cocoa: 0'
    Text 'Total: $0.00'
    $null = Find "//node[@package='$package' and @class='android.widget.Button' and @text='REVIEW ORDER' and @enabled='false']"
    Check $true 'Reset clears the order and disables Review after lifecycle transitions.'

    $demoPid = Adb shell pidof $package
    $log = Adb logcat -d "--pid=$demoPid" -s Xui.Android:E AndroidRuntime:E
    Check ($log -notmatch 'FATAL EXCEPTION|E Xui.Android\s*:') 'No adapter or runtime errors occurred.'
    Write-Host "PASS: $script:assertions Android order device checks."
    Write-Host 'Native fixtures and adb input are not physical IME or TalkBack acceptance.'
}
finally {
    if ($rotation -eq 'free') {
        $null = Adb shell wm user-rotation lock $angle
        $null = Adb shell wm user-rotation free
    }
    else { $null = Adb shell wm user-rotation lock $rotation.Split(' ')[1] }
    if ($showIme -eq 'null') { $null = Adb shell settings delete secure show_ime_with_hard_keyboard }
    else { $null = Adb shell settings put secure show_ime_with_hard_keyboard $showIme }
    $null = Adb shell rm -f $remoteXml
}
