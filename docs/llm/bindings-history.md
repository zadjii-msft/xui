# Binding validation history

This archive preserves earlier implementation reports and measurements from the former root README.
Results, limitations, tool paths, and artifact paths describe those runs, not the current checkout.
Local `build` artifacts are not part of the repository and can be absent.
Use [CONTRIBUTING](../../CONTRIBUTING.md) for current build instructions.

## Phase 4 results

The measurement command was `tests\phase4.ps1 -Runs 5` on September 11, 2026.
The complete output is `build\phase4\verification-final.log`.
That run passed 14 native tests in 150.44 seconds and all five normal UIA runs.
The five normal runs completed 76, 76, 78, 77, and 76 assertions, respectively.
The independent handoff check did not reproduce all desktop passes.

The independent command was `tests\phase4.ps1 -SkipMeasurements`.
Its output is `build\phase4\verification-resumed.log`.
The browser focus check failed, and the gallery, window, and scrolling tests timed out.
A separate browser retry also failed at native EDIT keyboard focus.
During the focus probe, Task Manager retained foreground ownership while the browser thread reported EDIT focus.
This observation does not establish the cause of the three timeouts.

The separate binding command was `tests\phase4.ps1 -SkipNativeTests -SkipMeasurements`.
Its output is `build\phase4\bindings-resumed.log`.
All binding builds and unit tests passed. The first normal UIA run failed at stable native keyboard focus.
Separate runs then covered all five clients. Each normal run failed at that focus check, but all five callback-failure runs passed.
`smoke-resumed-results.json` and `*-resumed.log` retain these results.
No assertion changed, and no unrelated process or Windows setting changed.
The desktop failures remain an open handoff limitation, not a new successful validation claim.

All DLL-based packages contain the same native DLL, with SHA-256 `4d6c3af486c194e1bbc6a563de9ebfea3d20f086423ff7451b8665c364ce40bf`.
`build\phase4\binary-manifest.json` records executable sizes, hashes, and PE machine fields.
The manifest reflects the independent rebuild. The measured package sizes still match every current deployment file.
The measurement process checks identified all five applications as native ARM64, without x64 emulation.
The current native executables and DLLs also retain ARM64 PE machine fields.

| Validation | Independent result | Artifact under `build\phase4` |
| --- | --- | --- |
| Native CTest suite | 10/14 passed, 254.83 seconds | `native-tests.log`, `verification-resumed.log` |
| Adversarial ABI checks | 347 assertions passed | `abi-resumed-tests.log` |
| C11 header/layout/import test | Passed | `native-tests.log` |
| C# framework-dependent tests | 17 assertions passed | `dotnet-fdd-tests.log` |
| C# NativeAOT tests | 17 assertions passed | `dotnet-aot-tests.log` |
| Rust safe-wrapper tests | 5/5 passed | `rust-tests.log` |
| Rust raw layout/import test | 1/1 passed | `rust-tests.log` |
| Rust compile-fail thread checks | 2/2 passed | `rust-tests.log` |
| Rust Clippy and format checks | Passed | `rust-clippy.log`, `rust-format.log` |
| Real UIA application runs | 5/5 failed at native keyboard focus | `*-uia-resumed.log` |
| GUI callback-failure runs | 5/5 passed | `*-callback-failure-resumed.log` |

The UIA run names are `cpp_static`, `cpp_abi`, `dotnet_fdd`, `dotnet_aot`, and `rust`.
All five measured applications retained one render target and produced zero custom paints in each measured idle interval.

### Deployment sizes

These are uncompressed file bytes, without PDBs or development import libraries.
The .NET SDK is a build tool, not an application runtime requirement.

| Application | Executable bytes | Native DLL bytes | Required package bytes | External language runtime |
| --- | ---: | ---: | ---: | --- |
| C++ static | 501,248 | 0 | 501,248 | None |
| C++ C ABI | 259,072 | 400,384 | 659,456 | None |
| C# framework-dependent | 140,800 | 400,384 | 580,607 | ARM64 .NET 10 |
| C# NativeAOT | 1,330,688 | 400,384 | 1,731,072 | None |
| Rust | 288,256 | 400,384 | 688,640 | None |

The framework-dependent package also contains the managed assemblies and runtime JSON files.
The installed `Microsoft.NETCore.App\10.0.12` directory contains 90,017,677 file bytes.
Its `hostfxr` directory adds 359,720 bytes. Together, these shared runtime files occupy 86.19 MiB by file length.
This count excludes the SDK, ASP.NET, Windows Desktop, and filesystem allocation overhead.
`installed-runtime.json` records the separate counts.
NativeAOT contains compiled runtime support inside its executable. It does not require those installed .NET directories.
All variants still require the Windows system components.

The dependency reports are `xui.dll.dependencies.log`, `Sample.exe.dependencies.log`, and `xui-sample.exe.dependencies.log`.
The native DLL and Rust executable do not require a redistributable CRT DLL.
`exports.log` records the 22 public C exports.

### Startup and process memory

Each row reports the median of five fresh processes.
The raw measurement timestamp is `2026-09-11T20:24:13.1984472-05:00`.
`summary.json` derives its values from that file. The independent handoff check did not repeat measurements.
The startup range retains every sample, including the 2.58-second first static-C++ sample.
Observed startup includes launcher and probe overhead. It is not a renderer-only timestamp.
The shared desktop and first-use work produce substantial variation, so these results do not establish a reliable startup ranking.

| Application | Startup median, ms | Startup range, ms | Private commit, MiB | Private WS, MiB | Total WS, MiB |
| --- | ---: | ---: | ---: | ---: | ---: |
| C++ static | 216.04 | 188.33–2,583.48 | 26.828 | 11.816 | 43.445 |
| C++ C ABI | 218.98 | 192.90–400.88 | 26.875 | 11.082 | 42.805 |
| C# framework-dependent | 339.29 | 292.92–520.45 | 32.609 | 14.520 | 60.277 |
| C# NativeAOT | 257.70 | 225.41–387.94 | 29.215 | 12.535 | 45.824 |
| Rust | 208.80 | 185.53–310.35 | 26.906 | 11.812 | 43.520 |

The ABI C++ median adds 0.047 MiB of private commit over the equivalent static C++ application.
Rust adds 0.078 MiB. Framework-dependent C# adds 5.781 MiB, and NativeAOT adds 2.387 MiB.
These are observed workload differences, not fixed runtime taxes.
The managed package comparison also excludes its shared installed runtime from per-application deployment bytes.

### Property submission

The table reports median elapsed milliseconds for 64,000 text mutations.
The individual path uses 64,000 calls. The batch path uses 1,000 calls.
The static C++ API has no separate batch boundary in this benchmark.

| Application | Individual calls, ms | Batches of 64, ms |
| --- | ---: | ---: |
| C++ static | 9.672 | Not applicable |
| C++ C ABI | 33.794 | 27.131 |
| C# framework-dependent | 90.918 | 37.501 |
| C# NativeAOT | 34.028 | 31.019 |
| Rust | 29.541 | 20.229 |

The native renderer combines frame invalidation in both paths.
The batch benefit comes from fewer boundary calls and fewer wrapper/preparation allocations, not fewer retained controls.
The results do not include layout, painting, or per-row callbacks.

### Existing browser regression

The browser still uses static linkage and does not import `xui.dll`.
Its DLL deployment overhead is zero bytes.
The browser, gallery, and image executable sizes remain 419,840, 347,648, and 390,656 bytes.
`xui_demo.exe.dependencies.log` records the unchanged static browser dependency boundary.

The before measurement contains three processes. The final after measurement contains five.
Both use the unchanged `tests\measure-browser.ps1` workload and no external UIA client.

| Browser metric | Before median | After median |
| --- | ---: | ---: |
| Idle private commit, MiB | 27.887 | 27.852 |
| Idle private WS, MiB | 13.469 | 13.500 |
| Idle total WS, MiB | 44.160 | 44.145 |
| Ready/painted startup, ms | 172.075 | 168.656 |
| Warm interaction sample, ms | 19.774 | 16.827 |
| Handles | 250 | 250 |
| Threads | 9 | 9 |
| Render targets | 1 | 1 |
| Additional custom idle paints | 0 | 0 |

The raw files are `browser-before.json` and `browser-after.json`.
Earlier runs remain in `browser-after-preliminary.json` and `browser-after-first-five.json`.
One earlier desktop run showed 274 handles and higher working-set values without a browser binary change.
The final repeated run returned to 250 handles. This observation is another reason to retain raw data and avoid universal memory claims.
Preliminary binding measurements also remain in `measurements-preliminary.json` and `measurements-first-no-console.json`.
