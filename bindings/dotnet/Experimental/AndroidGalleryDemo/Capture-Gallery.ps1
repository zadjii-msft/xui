#requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Serial,
    [string]$AdbPath = 'adb',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\..\..\..\build\portable-gallery\android'),
    [ValidateRange(30, 120)][int]$TimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$adb = (Get-Command $AdbPath -ErrorAction Stop).Source
$output = [IO.Path]::GetFullPath($OutputDirectory)
$null = New-Item -ItemType Directory -Path $output -Force
$remoteXml = "/sdcard/xui-gallery-$PID.xml"
$remotePng = "/sdcard/xui-gallery-$PID.png"
$script:package = ''
$captures = [Collections.Generic.List[object]]::new()
. (Join-Path $PSScriptRoot 'AndroidDeviceHarness.ps1')

function Visible-Ime {
    $windows = [regex]::Split((Adb shell dumpsys window), '(?m)(?=^\s*Window #\d+ Window\{)')
    return @($windows | Where-Object { $_ -match 'mIsImWindow=true' -and $_ -match '(?m)^\s*isVisible=true\s*$' }).Count -gt 0
}

function Assert-ImeHidden {
    if (Visible-Ime) { throw 'The native IME is still visible; refusing an obscured screenshot.' }
}

function Launch([string]$Component) {
    $script:package = $Component.Split('/')[0]
    $null = Adb shell am force-stop $script:package
    $result = Adb shell am start -W -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -f 0x10008000 -n $Component
    if ($result -notmatch 'Status: ok') { throw "Sample launch failed: $result" }
}

function Capture([string]$Name, [string]$Component, [string[]]$Markers) {
    Assert-ImeHidden
    $tree = Tree
    foreach ($marker in $Markers) {
        if ($null -eq $tree.SelectSingleNode("//node[@package='$script:package' and @text='$marker']")) {
            throw "Refusing to capture $Name without expected native content '$marker'."
        }
    }
    $window = Adb shell dumpsys window
    if ($window -notmatch "mCurrentFocus=Window\{[^\r\n]*$([regex]::Escape($Component))") {
        throw "Refusing to capture a window other than $Component."
    }
    $null = Adb shell screencap -p $remotePng
    $path = Join-Path $output "$Name.png"
    $null = Adb pull $remotePng $path
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 5000 -or [Convert]::ToHexString($bytes[0..7]) -ne '89504E470D0A1A0A') {
        throw "The native capture is not a complete PNG: $path"
    }
    $captures.Add([ordered]@{
        sample = $Name; component = $Component; serial = $Serial
        api = (Adb shell getprop ro.build.version.sdk); file = $path
        sha256 = (Get-FileHash -LiteralPath $path).Hash; markers = $Markers
    })
    Write-Host "CAPTURED: $Name $path"
}

if ((Adb get-state) -ne 'device') { throw "Device $Serial is not ready." }
$rotation = Adb shell wm user-rotation
if ($rotation -notmatch '^(free|lock [0-3])$') { throw "Unknown rotation policy: $rotation" }
$angle = Adb shell settings get system user_rotation
if ($angle -notmatch '^[0-3]$') { throw "Unknown display angle: $angle" }
$showIme = Adb shell settings get secure show_ime_with_hard_keyboard
if ($showIme -notmatch '^(null|0|1)$') { throw "Unknown IME preference: $showIme" }
try {
    $null = Adb shell settings put secure show_ime_with_hard_keyboard 0
    $null = Adb shell wm user-rotation lock 0
    $gallery = 'dev.xui.portable.gallery'
    $component = "$gallery/dev.xui.portable.gallery.TaskBoardActivity"
    Launch $component
    $null = Find "//node[@package='$gallery' and @text='1 of 3 complete']"
    Capture 'task-board' $component @('TASK BOARD', '1 of 3 complete')

    $component = "$gallery/dev.xui.portable.gallery.ExpenseLedgerActivity"
    Launch $component
    $null = Find "//node[@package='$gallery' and @text='Spent: `$75.25']"
    Capture 'expense-ledger' $component @('EXPENSE LEDGER', 'Spent: $75.25', 'Left: $74.75')

    $component = "$gallery/dev.xui.portable.gallery.SessionPlannerActivity"
    Launch $component
    $null = Find "//node[@package='$gallery' and @text='Finish: 10:30']"
    Capture 'session-planner' $component @('SESSION PLANNER', 'Finish: 10:30', '80 min focus')

    $order = 'dev.xui.portable.orders'
    $resolved = Adb shell cmd package resolve-activity --brief -a android.intent.action.MAIN -c android.intent.category.LAUNCHER $order
    $components = @($resolved -split '\r?\n' | Where-Object { $_.StartsWith("$order/") })
    if ($components.Count -ne 1) { throw "Install the shared order sample before capture: $resolved" }
    $component = $components[0]
    Launch $component
    $null = Find "//node[@package='$order' and @text='Total: `$0.00']"
    Capture 'order-builder' $component @('XUI order builder', 'Total: $0.00')
    $captures | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'captures.json') -Encoding utf8
    Write-Host 'PASS: four real Android sample Activity screenshots captured.'
}
finally {
    if ($rotation -eq 'free') {
        $null = Adb shell wm user-rotation lock $angle
        $null = Adb shell wm user-rotation free
    }
    else { $null = Adb shell wm user-rotation lock $rotation.Split(' ')[1] }
    if ($showIme -eq 'null') { $null = Adb shell settings delete secure show_ime_with_hard_keyboard }
    else { $null = Adb shell settings put secure show_ime_with_hard_keyboard $showIme }
    $null = Adb shell rm -f $remoteXml $remotePng
}
