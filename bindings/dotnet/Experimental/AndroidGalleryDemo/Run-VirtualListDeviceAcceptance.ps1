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
$script:package = 'dev.xui.portable.gallery'
$component = "$script:package/dev.xui.portable.gallery.VirtualListActivity"
$remoteXml = "/sdcard/xui-virtual-activity-$PID.xml"
$script:checks = 0
. (Join-Path $PSScriptRoot 'AndroidDeviceHarness.ps1')

function Check([string]$Description) {
    $script:checks++
    Write-Host "PASS: $Description"
}

function Launch {
    $result = Adb shell am start -W -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n $component
    if ($result -notmatch 'Status: ok') { throw "Virtual Activity launch failed: $result" }
}

function Button([string]$Text) {
    Tap (Find "//node[@package='$script:package' and @class='android.widget.Button' and @text='$Text' and @enabled='true']" '')
}

try {
    $null = Adb shell am force-stop $script:package
    Launch
    $null = Find "//node[@package='$script:package' and @text='VIRTUAL TASK LIST']" ''
    $null = Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='Task 1']" ''
    Check 'The actual Activity realizes the first native editor in the 10,000-row source.'
    Button 'LAST'
    $null = Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='Task 10,000' and @focused='true']" ''
    Check 'Logical Last exposes and focuses the actual final native editor.'
    $null = Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
    $null = Adb shell input text Kept%sDraft
    $null = Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='Kept Draft' and @focused='true']" ''
    $null = Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
    $null = Adb shell input keyevent KEYCODE_HOME
    $null = Find "//node[@package!='$script:package' and @package!='com.android.systemui']" ''
    Launch
    $null = Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='Kept Draft']" ''
    Check 'OnStop/OnStart reattaches at the saved offset with the edited task draft.'
    Button 'LAST'
    $null = Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='Kept Draft' and @focused='true']" ''
    $null = Adb shell input text Replaced
    $null = Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='Replaced' and @focused='true']" ''
    Check 'Logical focus restoration applies the saved native selection after reattachment.'
    Button 'FIRST'
    $null = Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='Task 1' and @focused='true']" ''
    Check 'Logical First returns to a real focused editor.'
    $pidText = Adb shell pidof $script:package
    if ($pidText -notmatch '^\d+$') { throw 'The virtual Activity process is missing.' }
    $log = Adb logcat -d "--pid=$pidText" -s Xui.Android:E AndroidRuntime:E
    if ($log -match 'FATAL EXCEPTION|E Xui.Android\s*:') { throw $log }
    Check 'The actual virtual Activity completed without adapter or runtime errors.'
    Write-Host "PASS: $script:checks Android virtual Activity checks."
    Write-Host 'This checks one Activity instance stopping and restarting, not process-death persistence or physical IME/TalkBack.'
}
finally { $null = Adb shell rm -f $remoteXml }
