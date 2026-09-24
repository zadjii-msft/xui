#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work = Join-Path ([IO.Path]::GetTempPath()) ("xui-font-targets-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$font = [Security.SecurityElement]::Escape("$repo\assets\fonts\abel\Abel-Regular.ttf")
$license = [Security.SecurityElement]::Escape("$repo\assets\fonts\abel\OFL.txt")
$targets = [Security.SecurityElement]::Escape("$repo\packaging\experimental\Xui.Experimental.Fonts.targets")
$hash = '8809dcad25318225052f88333e208c5aad4adcb7b2c934c135735ec19aa410b4'
$licenseHash = '4f4bc3806a1e55789c6ef75ca5fc628297b05292f74966474dc0d40324abc609'
$item = "<XuiFont Include=`"$font`" AssetId=`"fonts/abel.ttf`" Sha256=`"$hash`" LicenseFile=`"$license`" LicenseAssetId=`"licenses/abel.txt`" LicenseSha256=`"$licenseHash`" LicenseExpression=`"OFL-1.1`" SourceUrl=`"https://example.org/pinned-font`" />"
function Check([string]$Items, [string]$Code, [string]$Platform = '', [string]$Additional = '') {
    $project = "<Project><PropertyGroup><TargetPlatformIdentifier>$Platform</TargetPlatformIdentifier></PropertyGroup><ItemGroup>$Items$Additional</ItemGroup><Import Project=`"$targets`"/></Project>"
    Set-Content "$work\fixture.proj" $project -Encoding utf8
    $result = & dotnet msbuild "$work\fixture.proj" -t:PrepareExperimentalXuiFonts --nologo 2>&1 | Out-String
    if (!$Code) {
        if ($LASTEXITCODE -ne 0) { throw "Valid font target fixture failed: $result" }
    } elseif ($LASTEXITCODE -eq 0 -or !$result.Contains($Code)) { throw "Expected $Code diagnostic: $result" }
}
try {
    Check $item ''
    Check $item '' android
    Check ($item.Replace('AssetId="fonts/abel.ttf"', 'AssetId="../abel.ttf"')) XUIF002
    Check ($item.Replace($hash, ('0' * 64))) XUIF006
    Check ($item.Replace($licenseHash, ('0' * 64))) XUIF006
    Check ($item.Replace($hash, $hash.ToUpperInvariant())) XUIF004
    Check ($item.Replace('https://example.org/pinned-font', 'https://user@example.org/font')) XUIF002
    Check ($item.Replace('LicenseExpression="OFL-1.1"', 'LicenseExpression=""')) XUIF002
    Check ($item + $item) XUIF005
    Check ($item + $item.Replace('fonts/abel.ttf', 'fonts/other.ttf').Replace('licenses/abel.txt', 'licenses/other.txt')) XUIF005
    Check ($item * 5) XUIF001
    Check $item XUIF007 android '<AndroidAsset Include="other.ttf" LogicalName="xui-fonts/foreign.ttf" />'
    Check $item ''
    Write-Output 'Font target metadata, source hashes, canonical ids, duplicate projection, face count and Android namespace guards passed.'
} finally { Remove-Item -LiteralPath $work -Recurse -Force }
