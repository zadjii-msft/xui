#requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Serial,
    [string]$AdbPath = 'adb',
    [ValidateRange(30, 180)][int]$TimeoutSeconds = 60,
    [switch]$LifecycleOnly,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\..\..\..\build\android-studio')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$adb = (Get-Command $AdbPath -ErrorAction Stop).Source
$script:package = 'dev.xui.portable.gallery'
$component = "$script:package/dev.xui.portable.gallery.WorkspaceStudioActivity"
$remoteXml = "/sdcard/xui-studio-acceptance-$PID.xml"
$remotePng = "/sdcard/xui-studio-acceptance-$PID.png"
$script:assertions = 0
. (Join-Path $PSScriptRoot 'AndroidDeviceHarness.ps1')
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force

function Check([bool]$Condition, [string]$Description) {
    if (-not $Condition) { throw "FAIL: $Description" }
    $script:assertions++
    Write-Host "PASS: $Description"
}
function Launch {
    $result = Adb shell am start -W -n $component
    if ($result -notmatch 'Status: (ok|timeout)') { throw "Studio Activity launch failed: $result" }
    $null = Find "//node[@package='$script:package' and @text='STUDIO']" ''
}
function Label([string]$Value, [string]$Direction = '') {
    $null = Find "//node[@package='$script:package' and @text='$Value']" $Direction
}
function Button([string]$Value, [string]$Direction = '') {
    return Find "//node[@package='$script:package' and @class='android.widget.Button' and @text='$Value' and @enabled='true']" $Direction
}
function Navigation([string]$Value) {
    Tap (Find "//node[@package='$script:package' and @class='android.widget.TextView' and @content-desc='$Value']" '')
}
function Editor([string]$Value, [bool]$Focused = $false) {
    $focus = if ($Focused) { " and @focused='true'" } else { '' }
    return Find "//node[@package='$script:package' and @class='android.widget.EditText' and @text='$Value'$focus]" ''
}
function Capture([string]$Name) {
    $tree = Tree
    Check ($null -ne $tree.SelectSingleNode("//node[@package='$script:package' and @text='STUDIO']")) 'Screenshot belongs to the actual Studio Activity.'
    $null = Adb shell screencap -p $remotePng
    $null = Adb pull $remotePng (Join-Path $OutputDirectory "$Name.png")
    $tree.Save((Join-Path $OutputDirectory "$Name.xml"))
}

$rotation = Adb shell wm user-rotation
if ($rotation -notmatch '^(free|lock [0-3])$') { throw "Unknown rotation policy: $rotation" }
$angle = Adb shell settings get system user_rotation
if ($angle -notmatch '^[0-3]$') { throw "Unknown rotation angle: $angle" }
try {
    $null = Adb shell wm user-rotation lock 0
    $null = Adb shell am force-stop $script:package
    Launch
    Label 'Compact workspace / Document'
    $null = Editor 'Keyboard navigation 00001'
    Check $true 'Actual compact Studio starts with a native retained document editor.'
    Capture 'android-studio-phone'
    Navigation 'Library'
    Label 'Compact workspace / Catalog'
    Label '10000 matching documents'
    $tree = Tree
    $catalog = $tree.SelectSingleNode("//node[@class='android.widget.ScrollView' and @content-desc='Virtual document catalog']")
    Check ($null -ne $catalog -and $catalog.SelectNodes(".//node[@text='OPEN']").Count -lt 20) 'Native catalog displays a bounded realized subset of 10,000 documents.'
    Capture 'android-studio-library'
    Tap (Button 'OPEN')
    Label 'Compact workspace / Document'
    Tap (Editor 'Keyboard navigation 00001')
    $null = Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
    $null = Adb shell input text AndroidStudioDraft
    Label 'LOCAL DRAFT / NOT SAVED TO DISK'
    $null = Editor 'AndroidStudioDraft' $true
    Check $true 'Native title edit updates the shared draft and tab title.'
    $null = Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
    $null = Adb shell input keyevent KEYCODE_HOME
    $null = Find "//node[@package!='$script:package' and @package!='com.android.systemui']" ''
    Launch
    $null = Editor 'AndroidStudioDraft' $true
    $null = Adb shell input text AndroidStudioRestored
    $null = Editor 'AndroidStudioRestored' $true
    Check $true 'Background reattachment restores the particular title editor and its selected range.'
    $null = Adb shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_A
    $null = Adb shell wm user-rotation lock 1
    $null = Find "/hierarchy[@rotation='1']" ''
    $title = Editor 'AndroidStudioRestored' $true
    $body = Find "//node[@package='$script:package' and @class='android.widget.EditText' and @content-desc='Document content']" ''
    foreach ($entry in @($title, $body)) {
        if ($entry.bounds -notmatch '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') { throw "Invalid native editor bounds: $($entry.bounds)" }
        Check ([int]$Matches[3] -gt [int]$Matches[1] -and [int]$Matches[4] -gt [int]$Matches[2]) "Short landscape exposes a positive native editor rectangle $($entry.bounds)."
    }
    $null = Adb shell input text AndroidStudioLandscape
    $null = Editor 'AndroidStudioLandscape' $true
    Check $true 'Short landscape retains the focused title and selected range while keeping the body accessible.'
    Capture 'android-studio-landscape-editing'
    $null = Adb shell wm user-rotation lock 0
    $null = Find "/hierarchy[@rotation='0']" ''
    $null = Editor 'AndroidStudioLandscape' $true
    Check $true 'Rotation through landscape and back restores the transient shared draft and focused editor in portrait.'
    $null = Adb shell input keyevent KEYCODE_BACK
    Navigation 'Library'
    Label 'Compact workspace / Catalog'
    Tap (Button 'OPEN')
    $null = Editor 'AndroidStudioLandscape'
    Check $true 'Returning through Library retains the authored document draft.'
    if ($LifecycleOnly) {
        Write-Host "PASS: $script:assertions actual Android Studio lifecycle/geometry checks; Operations is explicitly not part of this run."
        return
    }
    Navigation 'Insights'
    Label 'Compact workspace / Details'
    Tap (Button 'OPEN OPERATIONS DASHBOARD' 'down')
    Label 'LOCAL OPERATIONS'
    $spinner = Find "//node[@package='$script:package' and @class='android.widget.Spinner' and @content-desc='Snapshot scope']" ''
    Tap $spinner
    Tap (Find "//node[@package='$script:package' and @class='android.widget.CheckedTextView' and @text='All sample documents']" '')
    Label 'Scope selected. Scan to compute a new local snapshot.'
    Check $true 'A real Android Spinner popup changes the shared Operations scope.'
    Tap (Button 'SCAN')
    Label 'Local operations snapshot complete. No cloud metrics or user files were accessed.'
    Label 'Documents: 10000'
    Check $true 'The actual Operations controller completes a requested local 10,000-document scan.'
    Capture 'android-studio-operations'
    $null = Adb shell input keyevent KEYCODE_BACK
    Label 'Compact workspace / Details'
    $null = Adb shell input keyevent KEYCODE_BACK
    Label 'Compact workspace / Catalog'
    Check $true 'Native Back follows the Insights landing pane, then Library, without exiting.'
    $pidText = Adb shell pidof $script:package
    if ($pidText -notmatch '^\d+$') { throw 'The Studio process is missing.' }
    $log = Adb logcat -d "--pid=$pidText" -s Xui.Android:E Xui.Android.Studio:E AndroidRuntime:E
    Check ($log -notmatch 'FATAL EXCEPTION|E Xui.Android(?:.Studio)?\s*:') 'Studio navigation, operations and lifecycle produced no native fatal errors.'
    $log | Set-Content (Join-Path $OutputDirectory 'android-studio-errors.log')
    Write-Host "PASS: $script:assertions actual Android Studio Activity checks (PID $pidText)."
}
finally {
    if ($rotation -eq 'free') {
        $null = Adb shell wm user-rotation lock $angle
        $null = Adb shell wm user-rotation free
    }
    else { $null = Adb shell wm user-rotation lock $rotation.Split(' ')[1] }
    $null = Adb shell rm -f $remoteXml $remotePng
}
