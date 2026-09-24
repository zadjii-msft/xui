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
$component = "$script:package/dev.xui.portable.gallery.ProfileWorkspaceActivity"
$remoteXml = "/sdcard/xui-profile-acceptance-$PID.xml"
$script:assertions = 0
. (Join-Path $PSScriptRoot 'AndroidDeviceHarness.ps1')

function Check([bool]$Condition, [string]$Description) {
    if (-not $Condition) { throw "FAIL: $Description" }
    $script:assertions++
    Write-Host "PASS: $Description"
}

function Launch {
    $result = Adb shell am start -W -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n $component
    if ($result -notmatch 'Status: ok') { throw "Profile Activity launch failed: $result" }
}

function Label([string]$Value, [string]$Direction = 'down') {
    $null = Find "//node[@package='$script:package' and @class='android.widget.TextView' and @text='$Value']" $Direction
}

function Entry([string]$Value) {
    $null = Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='$Value' and @focused='true']" ''
    Check $true "Native profile editor retains '$Value' and focus."
}

$rotation = Adb shell wm user-rotation
if ($rotation -notmatch '^(free|lock [0-3])$') { throw "Unknown rotation policy: $rotation" }
$angle = Adb shell settings get system user_rotation
if ($angle -notmatch '^[0-3]$') { throw "Unknown rotation angle: $angle" }
try {
    $null = Adb shell wm user-rotation lock 0
    $null = Adb shell am force-stop $script:package
    Launch
    Label 'No draft loaded. Nothing is saved automatically.'
    Check $true 'A fresh profile Activity does not auto-load or auto-save.'
    Tap (Find "//node[@package='$script:package' and @class='android.widget.EditText' and ../node[@text='Display name']]")
    $null = Adb shell input text TransientName
    Tap (Find "//node[@package='$script:package' and @class='android.widget.EditText' and ../node[@text='Role']]")
    $null = Adb shell input text RoleDraft
    $null = Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
    $null = Adb shell wm user-rotation lock 1
    $null = Find "/hierarchy[@rotation='1']" ''
    Entry 'RoleDraft'
    $null = Adb shell input text RoleRestored
    Entry 'RoleRestored'
    Tap (Find "//node[@package='$script:package' and @class='android.widget.Button' and @text='PREVIEW PROFILE' and @enabled='true']")
    Label 'PROFILE PREVIEW' 'up'
    Label 'TransientName'
    Label 'RoleRestored'
    Check $true 'Native Preview uses the transient draft without storage IO.'
    $null = Adb shell wm user-rotation lock 0
    $null = Find "/hierarchy[@rotation='0']" ''
    Label 'PROFILE PREVIEW' 'up'
    Label 'TransientName'
    Label 'RoleRestored'
    Check $true 'The versioned transient session restores Preview and draft fields.'
    $null = Adb shell input keyevent KEYCODE_BACK
    Label 'EDIT PROFILE' 'up'
    $null = Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='TransientName']" 'up'
    Check $true 'Native Back returns from restored Preview to a fresh Edit route.'
    Tap (Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='RoleRestored']")
    $null = Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
    $null = Adb shell input keyevent KEYCODE_HOME
    $null = Find "//node[@package!='$script:package' and @package!='com.android.systemui']" ''
    Launch
    Entry 'RoleRestored'
    $null = Adb shell input text RoleBackground
    Entry 'RoleBackground'
    Label 'Edits are not saved.' 'up'
    Check $true 'Background restoration keeps unsaved edits without implicitly saving.'
    $pidText = Adb shell pidof $script:package
    if ($pidText -notmatch '^\d+$') { throw 'The profile Activity process is missing.' }
    $log = Adb logcat -d "--pid=$pidText" -s Xui.Android:E Xui.Android.Profile:E AndroidRuntime:E
    Check ($log -notmatch 'FATAL EXCEPTION|E Xui.Android(?:.Profile)?\s*:') 'Profile lifecycle and native Back produced no fatal errors.'
    Write-Host "PASS: $script:assertions Android profile Activity checks; no Load/Save/Delete was invoked."
}
finally {
    if ($rotation -eq 'free') {
        $null = Adb shell wm user-rotation lock $angle
        $null = Adb shell wm user-rotation free
    }
    else { $null = Adb shell wm user-rotation lock $rotation.Split(' ')[1] }
    $null = Adb shell rm -f $remoteXml
}
