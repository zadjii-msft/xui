param(
    [string]$BuildDirectory = "build\framework-pr",
    [ValidateSet('x64', 'ARM64')][string]$Architecture = 'x64',
    [ValidateSet('All', 'Native', 'Bindings')][string]$Suite = 'All',
    [switch]$OnPrivateDesktop
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (![OperatingSystem]::IsWindows()) { throw 'The framework PR gate requires Windows and MSVC; Linux is not validated.' }
. "$PSScriptRoot\..\scripts\Release.Common.ps1"
$root = Split-Path $PSScriptRoot
$build = [IO.Path]::GetFullPath($BuildDirectory, $root)
$native = Join-Path $build 'Release'
$cmake = Get-XuiCMake
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
foreach ($tool in @('python', 'dotnet', 'cargo', 'rustfmt')) {
    Get-Command $tool -ErrorAction Stop | Out-Null
}
Push-Location $root
try {
    if ($Suite -in @('All', 'Native')) {
        Invoke-Checked { & $cmake -S . -B $build -G (Get-XuiGenerator) -A $Architecture `
            -DBUILD_TESTING=ON -DXUI_DESKTOP_TESTS=OFF -DXUI_REQUIRE_LSH=ON }
        Invoke-Checked { & $cmake --build $build --config Release --target xui_headless_tests --parallel 4 }
        if (Select-String -LiteralPath "$build\CMakeCache.txt" -Pattern '^XUI_ENABLE_IPO:BOOL=ON$' -Quiet) {
            foreach ($target in @('xui', 'xui_content_host_window_tests')) {
                $logs = @(Get-ChildItem -LiteralPath "$build\$target.dir\Release" -Recurse -Filter link.command.1.tlog)
                if ($logs.Count -ne 1) { throw "Missing unique actual link command for $target" }
                $options = Get-Content -LiteralPath $logs[0].FullName -Raw
                if ($options -match '/LTCG:incremental' -or $options -notmatch '(?:^|\s)/LTCG(?:\s|$)') {
                    throw "Release IPO must use full LTCG for $target, not incremental code-generation caches"
                }
            }
            $logs = @(Get-ChildItem -LiteralPath "$build\xui_core.dir\Release" -Recurse -Filter CL.command.1.tlog)
            if ($logs.Count -ne 1 -or (Get-Content -LiteralPath $logs[0].FullName -Raw) -notmatch '(?:^|\s)/GL(?:\s|$)') {
                throw 'Release IPO lost /GL compilation'
            }
            Write-Host 'MSVC Release IPO retains /GL and full /LTCG for DLL and native fixture links.'
        }
        $inventory = & $ctest --test-dir $build -C Release --show-only=json-v1
        if ($LASTEXITCODE -ne 0) { throw 'CTest inventory failed.' }
        $inventory | Set-Content (Join-Path $build 'headless-inventory.json') -Encoding utf8
        $tests = ($inventory | ConvertFrom-Json).tests
        $names = @($tests | ForEach-Object { $_.name })
        foreach ($required in @('xui_core_tests', 'xui_control_tests', 'xui_owned_children_tests', 'xui_content_host_model_tests', 'xui_layout_performance_tests',
            'xui_performance_tests', 'xui_collections_tests', 'xui_document_editing_tests',
            'xui_abi_tests', 'xui_abi_c_test', 'xui_abi_features_headless_tests', 'xui_control_style_catalog_parity',
            'xui_row_image_performance_tests', 'xui_syntax_highlighting_tests')) {
            if ($required -notin $names) { throw "Required CTest is missing: $required" }
        }
        foreach ($test in $tests) {
            $labels = @($test.properties | Where-Object name -eq 'LABELS' | ForEach-Object { $_.value })
            if ('desktop' -in $labels -or !('headless' -in $labels -or 'system' -in $labels)) {
                throw "Unexpected test registration with desktop tests OFF: $($test.name)"
            }
        }
        foreach ($label in @{ headless = 47; core = 37; abi = 4; syntax = 2; schema = 1; allocation = 1; model = 2 }.GetEnumerator()) {
            $selected = @($tests | Where-Object {
                $label.Key -in @($_.properties | Where-Object name -eq 'LABELS' | ForEach-Object { $_.value })
            })
            if ($selected.Count -lt $label.Value) {
                throw "Native coverage disappeared: expected at least $($label.Value) tests labelled $($label.Key)"
            }
        }
        Invoke-Checked { & "$native\xui_headless_runner.exe" $ctest --test-dir $build -C Release `
            -L '^headless$' --no-tests=error --output-on-failure -j 1 --output-junit "$build\headless.xml" }
    }
    if ($Suite -in @('All', 'Bindings')) {
        if (!$OnPrivateDesktop) {
            Invoke-Checked { & "$native\xui_headless_runner.exe" (Get-Command pwsh).Source -NoProfile -File $PSCommandPath `
                -Suite Bindings -BuildDirectory $build -Architecture $Architecture -OnPrivateDesktop }
            return
        }
        foreach ($file in @('xui.dll', 'xui.lib', 'xui_style_catalog.exe', 'lsh_lib.dll')) {
            if (!(Test-Path -LiteralPath (Join-Path $native $file))) { throw "Missing required native asset: $native\$file" }
        }
        Invoke-Checked { python bindings\generate_features.py --check }
        Invoke-Checked { python bindings\generate_control_styles.py --native-executable "$native\xui_style_catalog.exe" --check }
        Invoke-Checked { python tests\framework-gates.py --native-executable "$native\xui_style_catalog.exe" }
        Invoke-Checked { dotnet run --project bindings\dotnet\GeneratorTests -c Release }
        Invoke-Checked { & .\bindings\dotnet\GeneratorTests\BuildTests.ps1 }
        Invoke-Checked { dotnet run --project bindings\dotnet\Tests -c Release -p:XuiNativeDir="$native" -- --headless }
        $target = if ($Architecture -eq 'ARM64') { 'aarch64-pc-windows-msvc' } else { 'x86_64-pc-windows-msvc' }
        $oldLibrary = $env:XUI_LIB_DIR
        $env:XUI_LIB_DIR = $native
        $oldPath = $env:PATH
        try {
            $env:PATH = "$native;$oldPath"
            $cargo = 'cargo test --locked --manifest-path bindings\rust\Cargo.toml --target ' + $target
            foreach ($rustSuite in @(
                @{ Arguments = '-p xui-sys --lib -- --test-threads=1'; Minimum = 1 },
                @{ Arguments = '-p xui --lib -- --test-threads=1'; Minimum = 39 },
                @{ Arguments = '-p xui --doc'; Minimum = 2 }
            )) {
                Invoke-XuiVcVarsCommand $Architecture "$cargo $($rustSuite.Arguments)" 2>&1 |
                    Tee-Object -Variable output | Write-Output
                $summary = [regex]::Match(($output -join "`n"), 'test result: ok\. (\d+) passed')
                if (!$summary.Success -or [int]$summary.Groups[1].Value -lt $rustSuite.Minimum) {
                    throw "Rust coverage disappeared: expected at least $($rustSuite.Minimum) passing tests for $($rustSuite.Arguments)"
                }
            }
        } finally {
            $env:PATH = $oldPath
            $env:XUI_LIB_DIR = $oldLibrary
        }
    }
} finally {
    Pop-Location
}
