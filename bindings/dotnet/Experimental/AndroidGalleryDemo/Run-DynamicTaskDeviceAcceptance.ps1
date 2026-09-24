#requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Serial,
    [string]$AdbPath = 'adb',
    [ValidateRange(30, 180)][int]$TimeoutSeconds = 60,
    [switch]$LifecycleOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$adb = (Get-Command $AdbPath -ErrorAction Stop).Source
$script:package = 'dev.xui.portable.gallery'
$component = "$script:package/dev.xui.portable.gallery.DynamicTaskBoardActivity"
$remoteXml = "/sdcard/xui-dynamic-acceptance-$PID.xml"
$script:assertions = 0
. (Join-Path $PSScriptRoot 'AndroidDeviceHarness.ps1')

function Check([bool]$Condition, [string]$Description) {
    if (-not $Condition) { throw "FAIL: $Description" }
    $script:assertions++
    Write-Host "PASS: $Description"
}

function Launch {
    $result = Adb shell am start -W -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n $component
    if ($result -notmatch 'Status: ok') { throw "Dynamic Activity launch failed: $result" }
}

function Entry([string]$Value) {
    $null = Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='$Value' and @focused='true']" ''
    Check $true "The keyed row editor has '$Value' and native focus."
}

function ImeViewport {
    if ($LifecycleOnly) {
        $scroll = (Tree).SelectSingleNode("//node[@package='$script:package' and @class='android.widget.ScrollView']")
        if ($null -eq $scroll -or $scroll.bounds -notmatch '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') {
            throw 'The dynamic task scroll viewport is not visible.'
        }
        $height = [int]$Matches[4] - [int]$Matches[2]
        $densities = [regex]::Matches((Adb shell wm density), 'density: (\d+)')
        if ($densities.Count -eq 0) { throw 'The device density was not reported.' }
        $minimum = [int][Math]::Round(48 * [int]$densities[$densities.Count - 1].Groups[1].Value / 160)
        Check ($height -ge $minimum) "Landscape viewport $($scroll.bounds) fits a 48dp native button."
        return
    }
    $frames = @([regex]::Matches((Adb shell dumpsys window), 'type=ime frame=\[\d+,(\d+)\]\[\d+,(\d+)\][^\r\n]*visible=true') |
        Where-Object { [int]$_.Groups[2].Value -gt [int]$_.Groups[1].Value })
    if ($frames.Count -eq 0) {
        $ime = (Adb shell settings get secure default_input_method).Split('/')[0]
        $null = Adb shell uiautomator dump --windows $remoteXml
        [xml]$windows = Adb shell cat $remoteXml
        $keyboard = $windows.SelectSingleNode("//node[@package='$ime' and @content-desc='Show on-screen keyboard']")
        if ($null -eq $keyboard) {
            $menu = $windows.SelectSingleNode("//node[@package='$ime' and @content-desc='More stylus options']")
            if ($null -eq $menu) { throw 'Open a docked keyboard before the landscape acceptance case.' }
            Tap $menu
            $null = Adb shell uiautomator dump --windows $remoteXml
            [xml]$windows = Adb shell cat $remoteXml
            $keyboard = $windows.SelectSingleNode("//node[@package='$ime' and @content-desc='Show on-screen keyboard']")
        }
        if ($null -eq $keyboard) { throw 'The stylus toolbar has no accessible Show on-screen keyboard action.' }
        Tap $keyboard
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        do {
            $frames = @([regex]::Matches((Adb shell dumpsys window), 'type=ime frame=\[\d+,(\d+)\]\[\d+,(\d+)\][^\r\n]*visible=true') |
                Where-Object { [int]$_.Groups[2].Value -gt [int]$_.Groups[1].Value })
            if ($frames.Count -ne 0) { break }
            Start-Sleep -Milliseconds 150
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($frames.Count -eq 0) { throw 'The requested docked keyboard did not become visible.' }
    }
    $top = [int]$frames[0].Groups[1].Value
    $scroll = (Tree).SelectSingleNode("//node[@package='$script:package' and @class='android.widget.ScrollView']")
    if ($null -eq $scroll -or $scroll.bounds -notmatch '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') {
        throw 'The dynamic task scroll viewport is not visible.'
    }
    $height = [int]$Matches[4] - [int]$Matches[2]
    $bottom = [int]$Matches[4]
    $densities = [regex]::Matches((Adb shell wm density), 'density: (\d+)')
    if ($densities.Count -eq 0) { throw 'The device density was not reported.' }
    $minimum = [int][Math]::Round(48 * [int]$densities[$densities.Count - 1].Groups[1].Value / 160)
    Check ($height -ge $minimum -and $bottom -le $top) "Landscape viewport $($scroll.bounds) fits a 48dp native button above IME top $top."
}

$rotation = Adb shell wm user-rotation
if ($rotation -notmatch '^(free|lock [0-3])$') { throw "Unknown rotation policy: $rotation" }
$angle = Adb shell settings get system user_rotation
if ($angle -notmatch '^[0-3]$') { throw "Unknown rotation angle: $angle" }
$showIme = Adb shell settings get secure show_ime_with_hard_keyboard
if ($showIme -notmatch '^(null|0|1)$') { throw "Unknown IME preference: $showIme" }
try {
    if ($LifecycleOnly) { Write-Warning 'Lifecycle-only mode does not establish docked-keyboard acceptance.' }
    $null = Adb shell settings put secure show_ime_with_hard_keyboard 1
    $null = Adb shell wm user-rotation lock 0
    $null = Adb shell am force-stop $script:package
    Launch
    $null = Find "//node[@package='$script:package' and @text='3 tasks / 1 done']"
    Tap (Find "//node[@package='$script:package' and @class='android.widget.EditText' and ../node[@text='New task']]")
    $null = Adb shell input text LifecycleItem
    Tap (Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='Build shared UI']")
    $null = Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
    $null = Adb shell input text RowDraft
    $null = Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
    $null = Adb shell wm user-rotation lock 1
    $null = Find "/hierarchy[@rotation='1']" ''
    Entry 'RowDraft'
    $null = Adb shell input text AfterRotation
    Entry 'AfterRotation'
    $null = Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
    $null = Adb shell input keyevent KEYCODE_HOME
    $null = Find "//node[@package!='$script:package' and @package!='com.android.systemui']" ''
    Launch
    Entry 'AfterRotation'
    $null = Adb shell input text AfterBackground
    Entry 'AfterBackground'
    Tap (Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='AfterBackground']" '')
    ImeViewport
    Tap (Find "//node[@package='$script:package' and @class='android.widget.Button' and @text='ADD TASK' and @enabled='true']" 'up')
    $null = Find "//node[@package='$script:package' and @text='4 tasks / 1 done']" 'up'
    Check $true 'The restored draft adds exactly one new task through a real landscape button.'
    ImeViewport
    $null = Adb shell wm user-rotation lock 0
    $null = Find "/hierarchy[@rotation='0']" ''
    $null = Find "//node[@package='$script:package' and @text='4 tasks / 1 done']" 'up'
    Check $true 'The added task count survives another Activity recreation.'
    $pidText = Adb shell pidof $script:package
    if ($pidText -notmatch '^\d+$') { throw 'The dynamic Activity process is missing.' }
    $log = Adb logcat -d "--pid=$pidText" -s Xui.Android:E AndroidRuntime:E
    Check ($log -notmatch 'FATAL EXCEPTION|E Xui.Android\s*:') 'The dynamic Activity completed without native runtime or adapter errors.'
    Write-Host "PASS: $script:assertions Android dynamic Activity checks."
    if ($LifecycleOnly) { Write-Warning 'Docked-keyboard acceptance remains unverified in this run.' }
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
